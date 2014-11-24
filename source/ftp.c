#include "ftp.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef _3DS
#include <3ds.h>
#else
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#endif
#include "console.h"

#ifdef _3DS
/* not sure what's correct yet */
#undef POLLIN
#define POLLIN          0x001 // looks correct
#undef POLLOUT
#define POLLOUT         0x010 // looks reasonable
#undef POLLERR
#define POLLERR         0x000 // ???
#undef POLLHUP
#define POLLHUP         0x000 // ???
#else
#define SOC_GetErrno() errno
#define closesocket(x) close(x)
#endif
#define POLL_UNKNOWN    (~(POLLIN|POLLOUT))

#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x100000
#define LISTEN_PORT    5000
#ifdef _3DS
#define DATA_PORT      (LISTEN_PORT+1)
#else
#define DATA_PORT      0 /* ephemeral port */
#endif

typedef struct ftp_session ftp_session_t;

#define FTP_DECLARE(x) static int x(ftp_session_t *session, const char *args)
FTP_DECLARE(ALLO);
FTP_DECLARE(APPE);
FTP_DECLARE(CDUP);
FTP_DECLARE(CWD);
FTP_DECLARE(DELE);
FTP_DECLARE(FEAT);
FTP_DECLARE(LIST);
FTP_DECLARE(MKD);
FTP_DECLARE(MODE);
FTP_DECLARE(NLST);
FTP_DECLARE(NOOP);
FTP_DECLARE(PASS);
FTP_DECLARE(PASV);
FTP_DECLARE(PORT);
FTP_DECLARE(PWD);
FTP_DECLARE(QUIT);
FTP_DECLARE(REST);
FTP_DECLARE(RETR);
FTP_DECLARE(RMD);
FTP_DECLARE(RNFR);
FTP_DECLARE(RNTO);
FTP_DECLARE(STOR);
FTP_DECLARE(STOU);
FTP_DECLARE(STRU);
FTP_DECLARE(SYST);
FTP_DECLARE(TYPE);
FTP_DECLARE(USER);

/*! session state */
typedef enum
{
  COMMAND_STATE,       /*!< waiting for a command */
  DATA_CONNECT_STATE,  /*!< waiting for connection after PASV command */
  DATA_TRANSFER_STATE, /*!< data transfer in progress */
} session_state_t;

/*! ftp session */
struct ftp_session
{
  char               cwd[4096]; /*!< current working directory */
  struct sockaddr_in peer_addr; /*!< peer address for data connection */
  struct sockaddr_in pasv_addr; /*!< listen address for PASV connection */
  int                cmd_fd;    /*!< socket for command connection */
  int                pasv_fd;   /*!< listen socket for PASV */
  int                data_fd;   /*!< socket for data transfer */
/*! data transfers in binary mode */
#define SESSION_BINARY (1 << 0)
/*! have pasv_addr ready for data transfer command */
#define SESSION_PASV   (1 << 1)
/*! have peer_addr ready for data transfer command */
#define SESSION_PORT   (1 << 2)
/*! data transfer in source mode */
#define SESSION_RECV   (1 << 3)
/*! data transfer in sink mode */
#define SESSION_SEND   (1 << 4)
/*! last command was RNFR and buffer contains path */
#define SESSION_RENAME (1 << 5)
  int                flags;     /*!< session flags */
  session_state_t    state;     /*!< session state */
  ftp_session_t      *next;     /*!< link to next session */
  ftp_session_t      *prev;     /*!< link to prev session */

  void     (*transfer)(ftp_session_t*); /*! data transfer callback */
  char     buffer[1024];                /*! persistent data between callbacks */
  size_t   bufferpos;                   /*! persistent buffer position between callbacks */
  size_t   buffersize;                  /*! persistent buffer size between callbacks */
  uint64_t filepos;                     /*! persistent file position between callbacks */
  uint64_t filesize;                    /*! persistent file size between callbacks */
#ifdef _3DS
  Handle   fd;                          /*! persistent handle between callbacks */
#else
  union
  {
    DIR    *dp;                         /*! persistent open directory pointer between callbacks */
    int    fd;                          /*! persistent open file descriptor between callbacks */
  };
#endif
};

/*! ftp command descriptor */
typedef struct ftp_command
{
  const char *name;                                   /*!< command name */
  int        (*handler)(ftp_session_t*, const char*); /*!< command callback */
} ftp_command_t;

static ftp_command_t ftp_commands[] =
{
#define FTP_COMMAND(x) { #x, x, }
#define FTP_ALIAS(x,y) { #x, y, }
  FTP_COMMAND(ALLO),
  FTP_COMMAND(APPE),
  FTP_COMMAND(CDUP),
  FTP_COMMAND(CWD),
  FTP_COMMAND(DELE),
  FTP_COMMAND(FEAT),
  FTP_COMMAND(LIST),
  FTP_COMMAND(MKD),
  FTP_COMMAND(MODE),
  FTP_COMMAND(NLST),
  FTP_COMMAND(NOOP),
  FTP_COMMAND(PASS),
  FTP_COMMAND(PASV),
  FTP_COMMAND(PORT),
  FTP_COMMAND(PWD),
  FTP_COMMAND(QUIT),
  FTP_COMMAND(REST),
  FTP_COMMAND(RETR),
  FTP_COMMAND(RMD),
  FTP_COMMAND(RNFR),
  FTP_COMMAND(RNTO),
  FTP_COMMAND(STOR),
  FTP_COMMAND(STOU),
  FTP_COMMAND(STRU),
  FTP_COMMAND(SYST),
  FTP_COMMAND(TYPE),
  FTP_COMMAND(USER),
  FTP_ALIAS(XCUP, CDUP),
  FTP_ALIAS(XMKD, MKD),
  FTP_ALIAS(XPWD, PWD),
  FTP_ALIAS(XRMD, RMD),
};
/*! number of ftp commands */
static const size_t num_ftp_commands = sizeof(ftp_commands)/sizeof(ftp_commands[0]);

/*! compare ftp command descriptors
 *
 *  @param[in] p1 left side of comparison (ftp_command_t*)
 *  @param[in] p2 right side of comparison (ftp_command_t*)
 *
 *  @returns <0 if p1 <  p2
 *  @returns 0 if  p1 == p2
 *  @returns >0 if p1 >  p2
 */
static int
ftp_command_cmp(const void *p1,
                const void *p2)
{
  ftp_command_t *c1 = (ftp_command_t*)p1;
  ftp_command_t *c2 = (ftp_command_t*)p2;

  /* ordered by command name */
  return strcasecmp(c1->name, c2->name);
}

#ifdef _3DS
/*! SOC service buffer */
static u32 *SOC_buffer = NULL;

/*! SDMC archive */
static FS_archive sdmcArchive =
{
  .id = ARCH_SDMC,
  .lowPath =
  {
    .type = PATH_EMPTY,
    .size = 1,
    .data = (u8*)"",
  },
};

/*! convert 3DS dirent name to ASCII
 *
 *  TODO: add support for non-ASCII characters
 *
 *  @param[in] dst output buffer
 *  @param[in] src input buffer
 */
static void
convert_name(char      *dst,
             const u16 *src)
{
  while(*src)
    *dst++ = *src++;
  *dst = 0;
}
#endif

/*! server listen address */
static struct sockaddr_in serv_addr;
/*! listen file descriptor */
static int                listenfd = -1;
/*! list of ftp sessions */
ftp_session_t             *sessions = NULL;

/*! close a socket
 *
 *  @param[in] fd        socket to close
 *  @param[in] connected whether this socket is connected
 */
static void
ftp_closesocket(int fd, int connected)
{
  int                rc;
  struct sockaddr_in addr;
  socklen_t          addrlen = sizeof(addr);

  if(connected)
  {
    /* get peer address and print */
    rc = getpeername(fd, (struct sockaddr*)&addr, &addrlen);
    if(rc != 0)
    {
      console_print("getpeername: %s\n", strerror(SOC_GetErrno()));
      console_print("closing connection to fd=%d\n", fd);
    }
    else
      console_print("closing connection to %s:%u\n",
                    inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    /* shutdown connection */
    rc = shutdown(fd, SHUT_RDWR);
    if(rc != 0)
      console_print("shutdown: %s\n", strerror(SOC_GetErrno()));
  }

  /* close socket */
  rc = closesocket(fd);
  if(rc != 0)
    console_print("closesocket: %s\n", strerror(SOC_GetErrno()));
}

/*! close command socket on ftp session
 *
 *  @param[in] session ftp session
 */
static void
ftp_session_close_cmd(ftp_session_t *session)
{
  /* close command socket */
  ftp_closesocket(session->cmd_fd, 1);
  session->cmd_fd = -1;
}

/*! close listen socket on ftp session
 *
 *  @param[in] session ftp session
 */
static void
ftp_session_close_pasv(ftp_session_t *session)
{
  console_print("stop listening on %s:%u\n",
                inet_ntoa(session->pasv_addr.sin_addr),
                ntohs(session->pasv_addr.sin_port));

  /* close pasv socket */
  ftp_closesocket(session->pasv_fd, 0);
  session->pasv_fd = -1;

  /* allocate new port for next PASV */
#ifdef _3DS
  /* just increment the port */
  /* TODO: use global port variable so separate sessions don't collide */
  session->pasv_addr.sin_port = htons(ntohs(session->pasv_addr.sin_port)+1);
#else
  /* get an ephemeral port */
  session->pasv_addr.sin_port = htons(0);
#endif
}

/*! close data socket on ftp session
 *
 *  @param[in] session ftp session */
static void
ftp_session_close_data(ftp_session_t *session)
{
  /* close data connection */
  ftp_closesocket(session->data_fd, 1);
  session->data_fd = -1;

  /* clear send/recv flags */
  session->flags &= ~(SESSION_RECV|SESSION_SEND);
}

/*! close open file for ftp session
 *
 *  @param[in] session ftp session
 */
static void
ftp_session_close_file(ftp_session_t *session)
{
#ifdef _3DS
  Result ret;

  ret = FSFILE_Close(session->fd);
  if(ret != 0)
    console_print("FSFILE_Close: 0x%08X\n", (unsigned int)ret);
  session->fd = -1;
#else
  int rc;

  rc = close(session->fd);
  if(rc != 0)
    console_print("close: %s\n", strerror(errno));
  session->fd = -1;
#endif
}

/*! open file for reading for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns -1 for error
 */
static int
ftp_session_open_file_read(ftp_session_t *session)
{
#ifdef _3DS
  Result ret;
  u64    size;

  /* open file in read mode */
  ret = FSUSER_OpenFile(NULL, &session->fd, sdmcArchive,
                        FS_makePath(PATH_CHAR, session->buffer),
                        FS_OPEN_READ, FS_ATTRIBUTE_NONE);
  if(ret != 0)
  {
    console_print("FSUSER_OpenFile: 0x%08X\n", (unsigned int)ret);
    return -1;
  }

  /* get the file size */
  ret = FSFILE_GetSize(session->fd, &size);
  if(ret != 0)
  {
    console_print("FSFILE_GetSize: 0x%08X\n", (unsigned int)ret);
    ftp_session_close_file(session);
    return -1;
  }
  session->filesize = size;
#else
  int         rc;
  struct stat st;

  /* open file in read mode */
  session->fd = open(session->buffer, O_RDONLY);
  if(session->fd < 0)
  {
    console_print("open '%s': %s\n", session->buffer, strerror(errno));
    return -1;
  }

  /* get the file size */
  rc = fstat(session->fd, &st);
  if(rc != 0)
  {
    console_print("fstat '%s': %s\n", session->buffer, strerror(errno));
    ftp_session_close_file(session);
    return -1;
  }
  session->filesize = st.st_size;
#endif

  /* reset file position */
  /* TODO: support REST command */
  session->filepos = 0;

  return 0;
}

/*! read from an open file for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns bytes read
 */
static ssize_t
ftp_session_read_file(ftp_session_t *session)
{
#ifdef _3DS
  Result ret;
  u32    bytes;

  /* read file at current position */
  ret = FSFILE_Read(session->fd, &bytes, session->filepos,
                    session->buffer, sizeof(session->buffer));
  if(ret != 0)
  {
    console_print("FSFILE_Read: 0x%08X\n", (unsigned int)ret);
    return -1;
  }

  /* adjust file position */
  session->filepos += bytes;

  return bytes;
#else
  ssize_t rc;

  /* read file at current position */
  /* TODO: maybe use pread? */
  rc = read(session->fd, session->buffer, sizeof(session->buffer));
  if(rc < 0)
  {
    console_print("read: %s\n", strerror(errno));
    return -1;
  }

  /* adjust file position */
  session->filepos += rc;

  return rc;
#endif
}

/*! open file for writing for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns -1 for error
 *
 *  @note truncates file
 */
static int
ftp_session_open_file_write(ftp_session_t *session)
{
#ifdef _3DS
  Result ret;

  /* open file in write and create mode */
  ret = FSUSER_OpenFile(NULL, &session->fd, sdmcArchive,
                        FS_makePath(PATH_CHAR, session->buffer),
                        FS_OPEN_WRITE|FS_OPEN_CREATE, FS_ATTRIBUTE_NONE);
  if(ret != 0)
  {
    console_print("FSUSER_OpenFile: 0x%08X\n", (unsigned int)ret);
    return -1;
  }

  /* truncate file */
  ret = FSFILE_SetSize(session->fd, 0);
  if(ret != 0)
  {
    console_print("FSFILE_SetSize: 0x%08X\n", (unsigned int)ret);
    ftp_session_close_file(session);
  }
#else
  /* open file in write and create mode with truncation */
  session->fd = open(session->buffer, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if(session->fd < 0)
  {
    console_print("open '%s': %s\n", session->buffer, strerror(errno));
    return -1;
  }
#endif

  /* reset file position */
  /* TODO: support REST command */
  session->filepos = 0;

  return 0;
}

/*! write to an open file for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns bytes written
 */
static ssize_t
ftp_session_write_file(ftp_session_t *session)
{
#ifdef _3DS
  Result ret;
  u32    bytes;

  /* write to file at current position */
  ret = FSFILE_Write(session->fd, &bytes, session->filepos,
                    session->buffer + session->bufferpos,
                    session->buffersize - session->bufferpos,
                    FS_WRITE_FLUSH);
  if(ret != 0)
  {
    console_print("FSFILE_Write: 0x%08X\n", (unsigned int)ret);
    return -1;
  }
  else if(bytes == 0)
    console_print("FSFILE_Write: wrote 0 bytes\n");

  /* adjust file position */
  session->filepos += bytes;

  return bytes;
#else
  ssize_t rc;

  /* write to file at current position */
  /* TODO: maybe use writev? */
  rc = write(session->fd, session->buffer + session->bufferpos,
             session->buffersize - session->bufferpos);
  if(rc < 0)
  {
    console_print("write: %s\n", strerror(errno));
    return -1;
  }
  else if(rc == 0)
    console_print("write: wrote 0 bytes\n");

  /* adjust file position */
  session->filepos += rc;

  return rc;
#endif
}

/*! close current working directory for ftp session
 *
 *   @param[in] session ftp session
 */
static void
ftp_session_close_cwd(ftp_session_t *session)
{
#ifdef _3DS
  Result ret;

  /* close open directory handle */
  ret = FSDIR_Close(session->fd);
  if(ret != 0)
    console_print("FSDIR_Close: 0x%08X\n", (unsigned int)ret);
  session->fd = -1;
#else
  int rc;

  /* close open directory pointer */
  rc = closedir(session->dp);
  if(rc != 0)
    console_print("closedir: %s\n", strerror(errno));
  session->dp = NULL;
#endif
}

/*! open current working directory for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @return -1 for failure
 */
static int
ftp_session_open_cwd(ftp_session_t *session)
{
#ifdef _3DS
  Result ret;

  /* open current working directory */
  ret = FSUSER_OpenDirectory(NULL, &session->fd, sdmcArchive,
                             FS_makePath(PATH_CHAR, session->cwd));
  if(ret != 0)
  {
    console_print("FSUSER_OpenDirectory: 0x%08X\n", (unsigned int)ret);
    return -1;
  }
#else
  /* open current working directory */
  session->dp = opendir(session->cwd);
  if(session->dp == NULL)
  {
    console_print("opendir '%s': %s\n", session->cwd, strerror(errno));
    return -1;
  }
#endif

  return 0;
}

/*! set state for ftp session
 *
 *  @param[in] session ftp session
 *  @param[in] state   state to set
 */
static void
ftp_session_set_state(ftp_session_t   *session,
                      session_state_t state)
{
  session->state = state;

  switch(state)
  {
    case COMMAND_STATE:
      /* close pasv and data sockets */
      if(session->pasv_fd >= 0)
        ftp_session_close_pasv(session);
      if(session->data_fd >= 0)
        ftp_session_close_data(session);
      break;

    case DATA_CONNECT_STATE:
      /* close data socket; we are listening for a new one */
      if(session->data_fd >= 0)
        ftp_session_close_data(session);
      break;

    case DATA_TRANSFER_STATE:
      /* close pasv socket; we are connecting for a new one */
      if(session->pasv_fd >= 0)
        ftp_session_close_pasv(session);
  }
}

__attribute__((format(printf,3,4)))
/*! send ftp response to ftp session's peer
 *
 *  @param[in] session ftp session
 *  @param[in] code    response code
 *  @param[in] fmt     format string
 *  @param[in] ...     format arguments
 *
 *  returns bytes send to peer
 */
static ssize_t
ftp_send_response(ftp_session_t *session,
                  int           code,
                  const char    *fmt, ...)
{
  static char buffer[1024];
  ssize_t     rc, to_send;
  va_list     ap;

  /* print response code and message to buffer */
  va_start(ap, fmt);
  rc  = sprintf(buffer, "%d ", code);
  rc += vsnprintf(buffer+rc, sizeof(buffer)-rc, fmt, ap);
  va_end(ap);

  if(rc >= sizeof(buffer))
  {
    /* couldn't fit message; just send code */
    console_print("%s: buffersize too small\n", __func__);
    rc = sprintf(buffer, "%d\r\n", code);
  }

  /* send response */
  to_send = rc;
  console_print("%s", buffer);
  rc = send(session->cmd_fd, buffer, to_send, 0);
  if(rc < 0)
    console_print("send: %s\n", strerror(SOC_GetErrno()));
  else if(rc != to_send)
    console_print("only sent %u/%u bytes\n",
                  (unsigned int)rc, (unsigned int)to_send);

  return rc;
}

/*! destroy ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns next session in list
 */
static ftp_session_t*
ftp_session_destroy(ftp_session_t *session)
{
  ftp_session_t *next = session->next;

  /* close all sockets */
  if(session->cmd_fd >= 0)
    ftp_session_close_cmd(session);
  if(session->pasv_fd >= 0)
    ftp_session_close_pasv(session);
  if(session->data_fd >= 0)
    ftp_session_close_data(session);

  /* unlink from sessions list */
  if(session->next)
    session->next->prev = session->prev;
  if(session == sessions)
    sessions = session->next;
  else
  {
    session->prev->next = session->next;
    if(session == sessions->prev)
      sessions->prev = session->prev;
  }

  /* deallocate */
  free(session);

  return next;
}

/*! allocate new ftp session
 *
 *  @param[in] listen_fd socket to accept connection from
 */
static void
ftp_session_new(int listen_fd)
{
  ssize_t            rc;
  int                new_fd;
  ftp_session_t      *session;
  struct sockaddr_in addr;
  socklen_t          addrlen = sizeof(addr);

  /* accept connection */
  new_fd = accept(listen_fd, (struct sockaddr*)&addr, &addrlen);
  if(new_fd < 0)
  {
    console_print("accept: %s\n", strerror(SOC_GetErrno()));
    return;
  }

  console_print("accepted connection from %s:%u\n",
                inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

  /* allocate a new session */
  session = (ftp_session_t*)malloc(sizeof(ftp_session_t));
  if(session == NULL)
  {
    console_print("failed to allocate session\n");
    ftp_closesocket(new_fd, 1);
    return;
  }

  /* initialize session */
  memset(session->cwd, 0, sizeof(session->cwd));
  strcpy(session->cwd, "/");
  session->peer_addr.sin_addr.s_addr = INADDR_ANY;
  session->cmd_fd   = new_fd;
  session->pasv_fd  = -1;
  session->data_fd  = -1;
  session->flags    = 0;
  session->state    = COMMAND_STATE;
  session->next     = NULL;
  session->prev     = NULL;
  session->transfer = NULL;

  /* link to the sessions list */
  if(sessions == NULL)
  {
    sessions = session;
    session->prev = session;
  }
  else
  {
    sessions->prev->next = session;
    session->prev        = sessions->prev;
    sessions->prev       = session;
  }

  /* copy socket address to pasv address */
  addrlen = sizeof(session->pasv_addr);
  rc = getsockname(new_fd, (struct sockaddr*)&session->pasv_addr, &addrlen);
  if(rc != 0)
  {
    console_print("getsockname: %s\n", strerror(SOC_GetErrno()));
    ftp_send_response(session, 451, "Failed to get connection info\r\n");
    ftp_session_destroy(session);
    return;
  }

  /* replace pasv port with data port */
  /* TODO: use global port variable so separate sessions don't collide */
  session->pasv_addr.sin_port = htons(DATA_PORT);
  session->cmd_fd = new_fd;

  /* send initiator response */
  rc = ftp_send_response(session, 200, "Hello!\r\n");
  if(rc <= 0)
    ftp_session_destroy(session);
}

/*! accept PASV connection for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns -1 for failure
 */
static int
ftp_session_accept(ftp_session_t *session)
{
  int                new_fd;
  struct sockaddr_in addr;
  socklen_t          addrlen = sizeof(addr);

  if(session->flags & SESSION_PASV)
  {
    /* clear PASV flag */
    session->flags &= ~SESSION_PASV;

    /* tell the peer that we're ready to accept the connection */
    ftp_send_response(session, 150, "Ready\r\n");

    /* accept connection from peer */
    new_fd = accept(session->pasv_fd, (struct sockaddr*)&addr, &addrlen);
    if(new_fd < 0)
    {
      console_print("accept: %s\n", strerror(SOC_GetErrno()));
      ftp_session_close_pasv(session);
      ftp_send_response(session, 425, "Failed to establish connection\r\n");
      return -1;
    }

    console_print("accepted connection from %s:%u\n",
                  inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    ftp_session_set_state(session, DATA_TRANSFER_STATE);
    session->data_fd = new_fd;

    return 0;
  }
  else
  {
    /* peer didn't send PASV command */
    ftp_send_response(session, 503, "Bad sequence of commands\r\n");
    return -1;
  }
}

/*! connect to peer for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns -1 for failure
 */
static int
ftp_session_connect(ftp_session_t *session)
{
  int rc;

  if(session->flags & SESSION_PORT)
  {
    /* clear PORT flag */
    session->flags &= ~SESSION_PORT;

    /* create a new socket */
    session->data_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(session->data_fd < 0)
    {
      console_print("socket: %s\n", strerror(SOC_GetErrno()));
      ftp_send_response(session, 425, "Failed to establish connection\r\n");
      return -1;
    }

    /* connect to peer */
    rc = connect(session->data_fd, (struct sockaddr*)&session->peer_addr,
                 sizeof(session->peer_addr));
    if(rc != 0)
    {
      console_print("connect: %s\n", strerror(SOC_GetErrno()));
      ftp_closesocket(session->data_fd, 0);
      session->data_fd = -1;
      ftp_send_response(session, 425, "Failed to establish connection\r\n");
      return -1;
    }

    console_print("connected to %s:%u\n",
                  inet_ntoa(session->peer_addr.sin_addr),
                  ntohs(session->peer_addr.sin_port));

    /* tell peer that connection has been established */
    ftp_send_response(session, 150, "Ready\r\n");
    return 0;
  }
  else
  {
    /* peer didn't send PORT command */
    ftp_send_response(session, 503, "Bad sequence of commands\r\n");
    return -1;
  }
}

/*! read command for ftp session
 *
 *  @param[in] session ftp session
 */
static void
ftp_session_read_command(ftp_session_t *session)
{
  static char   buffer[1024];
  ssize_t       rc;
  char          *args;
  ftp_command_t key, *command;

  memset(buffer, 0, sizeof(buffer));

  /* retrieve command */
  rc = recv(session->cmd_fd, buffer, sizeof(buffer), 0);
  if(rc < 0)
  {
    /* error retrieving command */
    console_print("recv: %s\n", strerror(SOC_GetErrno()));
    ftp_session_close_cmd(session);
    return;
  }
  if(rc == 0)
  {
    /* peer closed connection */
    ftp_session_close_cmd(session);
    return;
  }
  else
  {
    /* split into command and arguments */
    /* TODO: support partial transfers */
    buffer[sizeof(buffer)-1] = 0;

    args = buffer;
    while(*args && *args != '\r' && *args != '\n')
      ++args;
    *args = 0;
    
    args = buffer;
    while(*args && !isspace((int)*args))
      ++args;
    if(*args)
      *args++ = 0;

    /* look up the command */
    key.name = buffer;
    command = bsearch(&key, ftp_commands,
                      num_ftp_commands, sizeof(ftp_command_t),
                      ftp_command_cmp);

    /* execute the command */
    if(command == NULL)
    {
      ftp_send_response(session, 502, "invalid command -> %s %s\r\n",
                        key.name, args);
    }
    else
      command->handler(session, args);
  }
}

/*! poll sockets for ftp session
 *
 *  @param[in] session ftp session
 *
 *  @returns next session
 */
static ftp_session_t*
ftp_session_poll(ftp_session_t *session)
{
  int           rc;
  struct pollfd pollinfo;

  switch(session->state)
  {
    case COMMAND_STATE:
      /* we are waiting to read a command */
      pollinfo.fd      = session->cmd_fd;
      pollinfo.events  = POLLIN;
      pollinfo.revents = 0;
      break;

    case DATA_CONNECT_STATE:
      /* we are waiting for a PASV connection */
      pollinfo.fd      = session->pasv_fd;
      pollinfo.events  = POLLIN;
      pollinfo.revents = 0;
      break;

    case DATA_TRANSFER_STATE:
      /* we need to transfer data */
      pollinfo.fd = session->data_fd;
      if(session->flags & SESSION_RECV)
        pollinfo.events = POLLIN;
      else
        pollinfo.events = POLLOUT;
      pollinfo.revents = 0;
      break;
  }

  /* poll the selected socket */
  rc = poll(&pollinfo, 1, 0);
  if(rc < 0)
    console_print("poll: %s\n", strerror(SOC_GetErrno()));
  else if(rc > 0)
  {
    if(pollinfo.revents != 0)
    {
      /* handle event */
      switch(session->state)
      {
        case COMMAND_STATE:
          if(pollinfo.revents & POLL_UNKNOWN)
            console_print("cmd_fd: revents=0x%08X\n", pollinfo.revents);

          /* we need to read a new command */
          if(pollinfo.revents & (POLLERR|POLLHUP))
            ftp_session_close_cmd(session);
          else if(pollinfo.revents & POLLIN)
            ftp_session_read_command(session);
          break;

        case DATA_CONNECT_STATE:
          if(pollinfo.revents & POLL_UNKNOWN)
            console_print("pasv_fd: revents=0x%08X\n", pollinfo.revents);

          /* we need to accept the PASV connection */
          if(pollinfo.revents & (POLLERR|POLLHUP))
          {
            ftp_session_set_state(session, COMMAND_STATE);
            ftp_send_response(session, 426, "Data connection failed\r\n");
          }
          else if(pollinfo.revents & POLLIN)
          {
            if(ftp_session_accept(session) != 0)
              ftp_session_set_state(session, COMMAND_STATE);
          }
          break;

        case DATA_TRANSFER_STATE:
          if(pollinfo.revents & POLL_UNKNOWN)
            console_print("data_fd: revents=0x%08X\n", pollinfo.revents);

          /* we need to transfer data */
          if(pollinfo.revents & (POLLERR|POLLHUP))
          {
            ftp_session_set_state(session, COMMAND_STATE);
            ftp_send_response(session, 426, "Data connection failed\r\n");
          }
          else if(pollinfo.revents & (POLLIN|POLLOUT))
            session->transfer(session);
          break;
      }
    }
  }

  /* still connected to peer; return next session */
  if(session->cmd_fd >= 0)
    return session->next;

  /* disconnected from peer; destroy it and return next session */
  return ftp_session_destroy(session);
}

/*! initialize ftp subsystem */
int
ftp_init(void)
{
  int rc;

#ifdef _3DS
  Result  ret;

  /* initialize FS service */
  ret = fsInit();
  if(ret != 0)
  {
    console_print("fsInit: 0x%08X\n", (unsigned int)ret);
    return -1;
  }

  /* open SDMC archive */
  ret = FSUSER_OpenArchive(NULL, &sdmcArchive);
  if(ret != 0)
  {
    console_print("FSUSER_OpenArchive: 0x%08X\n", (unsigned int)ret);
    ret = fsExit();
    if(ret != 0)
      console_print("fsExit: 0x%08X\n", (unsigned int)ret);
    return -1;
  }

  /* allocate buffer for SOC service */
  SOC_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
  if(SOC_buffer == NULL)
  {
    console_print("memalign: failed to allocate\n");
    ret = FSUSER_CloseArchive(NULL, &sdmcArchive);
    if(ret != 0)
      console_print("FSUSER_CloseArchive: 0x%08X\n", (unsigned int)ret);
    ret = fsExit();
    if(ret != 0)
      console_print("fsExit: 0x%08X\n", (unsigned int)ret);
    return -1;
  }

  /* initialize SOC service */
  ret = SOC_Initialize(SOC_buffer, SOC_BUFFERSIZE);
  if(ret != 0)
  {
    console_print("SOC_Initialize: 0x%08X\n", (unsigned int)ret);
    free(SOC_buffer);
    ret = FSUSER_CloseArchive(NULL, &sdmcArchive);
    if(ret != 0)
      console_print("FSUSER_CloseArchive: 0x%08X\n", (unsigned int)ret);
    ret = fsExit();
    if(ret != 0)
      console_print("fsExit: 0x%08X\n", (unsigned int)ret);
    return -1;
  }
#endif

  /* allocate socket to listen for clients */
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if(listenfd < 0)
  {
    console_print("socket: %s\n", strerror(SOC_GetErrno()));
    ftp_exit();
    return -1;
  }

  /* get address to listen on */
  serv_addr.sin_family      = AF_INET;
#ifdef _3DS
  serv_addr.sin_addr.s_addr = gethostid();
  serv_addr.sin_port        = htons(LISTEN_PORT);
#else
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port        = htons(LISTEN_PORT);

  /* reuse address */
  {
    int yes = 1;
    rc = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if(rc != 0)
    {
      console_print("setsockopt: %s\n", strerror(SOC_GetErrno()));
      ftp_exit();
      return -1;
    }
  }
#endif

  /* bind socket to listen address */
  rc = bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
  if(rc != 0)
  {
    console_print("bind: %s\n", strerror(SOC_GetErrno()));
    ftp_exit();
    return -1;
  }

  /* listen on socket */
  rc = listen(listenfd, 5);
  if(rc != 0)
  {
    console_print("listen: %s\n", strerror(SOC_GetErrno()));
    ftp_exit();
    return -1;
  }

  /* print server address */
#ifdef _3DS
  console_set_status(STATUS_STRING " IP:%s Port:%u",
                     inet_ntoa(serv_addr.sin_addr),
                     ntohs(serv_addr.sin_port));
#else
  {
    char      hostname[128];
    socklen_t addrlen = sizeof(serv_addr);
    rc = getsockname(listenfd, (struct sockaddr*)&serv_addr, &addrlen);
    if(rc != 0)
    {
      console_print("getsockname: %s\n", strerror(SOC_GetErrno()));
      ftp_exit();
      return -1;
    }

    rc = gethostname(hostname, sizeof(hostname));
    if(rc != 0)
    {
      console_print("gethostname: %s\n", strerror(SOC_GetErrno()));
      ftp_exit();
      return -1;
    }

    console_set_status(STATUS_STRING " IP:%s Port:%u",
                       hostname,
                       ntohs(serv_addr.sin_port));
  }
#endif

  return 0;
}

/*! deinitialize ftp subsystem */
void
ftp_exit(void)
{
#ifdef _3DS
  Result ret;
#endif

  /* clean up all sessions */
  while(sessions != NULL)
    ftp_session_destroy(sessions);

  /* stop listening for new clients */
  if(listenfd >= 0)
    ftp_closesocket(listenfd, 0);

#ifdef _3DS
  /* deinitialize SOC service */
  ret = SOC_Shutdown();
  if(ret != 0)
    console_print("SOC_Shutdown: 0x%08X\n", (unsigned int)ret);
  free(SOC_buffer);

  /* deinitialize FS service */
  ret = fsExit();
  if(ret != 0)
    console_print("fsExit: 0x%08X\n", (unsigned int)ret);
#endif
}

int
ftp_loop(void)
{
  int           rc;
  struct pollfd pollinfo;
  ftp_session_t *session;

  pollinfo.fd      = listenfd;
  pollinfo.events  = POLLIN;
  pollinfo.revents = 0;

  rc = poll(&pollinfo, 1, 0);
  if(rc < 0)
    console_print("poll: %s\n", strerror(SOC_GetErrno()));
  else if(rc > 0)
  {
    if(pollinfo.revents & POLLIN)
    {
      ftp_session_new(listenfd);
    }
    else
    {
      console_print("listenfd: revents=0x%08X\n", pollinfo.revents);
    }
  }

  session = sessions;
  while(session != NULL)
    session = ftp_session_poll(session);

#ifdef _3DS
  hidScanInput();
  if(hidKeysDown() & KEY_B)
    return -1;
#endif

  return 0;
}

static void
cd_up(ftp_session_t *session)
{
  char *slash = NULL, *p;

  for(p = session->cwd; *p; ++p)
  {
    if(*p == '/')
      slash = p;
  }
  *slash = 0;
  if(strlen(session->cwd) == 0)
    strcat(session->cwd, "/");
}

static int
validate_path(const char *args)
{
  const char *p;

  /* make sure no path components are '..' */
  p = args;
  while((p = strstr(p, "/..")) != NULL)
  {
    if(p[3] == 0 || p[3] == '/')
      return -1;
  }

  /* make sure there are no '//' */
  if(strstr(args, "//") != NULL)
    return -1;

  return 0;
}

static void
build_path(ftp_session_t *session,
           const char    *args)
{
  char *p;

  memset(session->buffer, 0, sizeof(session->buffer));

  if(args[0] == '/')
  {
    strncpy(session->buffer, args, sizeof(session->buffer));
  }
  else
  {
    p = session->cwd + strlen(session->cwd);
    while(*--p == '/')
      *p = 0;
    snprintf(session->buffer, sizeof(session->buffer), "%s/%s",
             session->cwd, args);
    p = session->buffer + strlen(session->buffer);
    while(*--p == '/')
      *p = 0;
  }
}

static void
list_transfer(ftp_session_t *session)
{
#ifdef _3DS
  Result  ret;
#endif
  ssize_t rc;

  if(session->bufferpos == session->buffersize)
  {
#ifdef _3DS
    FS_dirent dent;
    u32       entries;
    char      name[256];

    ret = FSDIR_Read(session->fd, &entries, 1, &dent);
    if(ret != 0)
    {
      console_print("FSDIR_Read: 0x%08X\n", (unsigned int)ret);
      ftp_session_close_cwd(session);
      ftp_session_set_state(session, COMMAND_STATE);
      ftp_send_response(session, 450, "failed to read directory\r\n");
      return;
    }

    if(entries == 0)
    {
      ftp_session_close_cwd(session);
      ftp_session_set_state(session, COMMAND_STATE);
      ftp_send_response(session, 226, "OK\r\n");
      return;
    }

    convert_name(name, dent.name);
    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
      return;

    session->buffersize =
        sprintf(session->buffer,
                "%crwxrwxrwx 1 3DS 3DS %llu Jan 1 1970 %s\r\n",
                dent.isDirectory ? 'd' : '-',
                dent.fileSize,
                name);
#else
    struct stat   st;
    struct dirent *dent = readdir(session->dp);
    if(dent == NULL)
    {
      ftp_session_close_cwd(session);
      ftp_session_set_state(session, COMMAND_STATE);
      ftp_send_response(session, 226, "OK\r\n");
      return;
    }

    if(strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0)
      return;

    snprintf(session->buffer, sizeof(session->buffer),
             "%s/%s", session->cwd, dent->d_name);
    rc = lstat(session->buffer, &st);
    if(rc != 0)
    {
      console_print("stat '%s': %s\n", session->buffer, strerror(errno));
      ftp_session_close_cwd(session);
      ftp_session_set_state(session, COMMAND_STATE);
      ftp_send_response(session, 550, "unavailable\r\n");
      return;
    }

    session->buffersize =
        sprintf(session->buffer,
                "%crwxrwxrwx 1 3DS 3DS %llu Jan 1 1970 %s\r\n",
                S_ISDIR(st.st_mode) ? 'd' :
                S_ISLNK(st.st_mode) ? 'l' : '-',
                (unsigned long long)st.st_size,
                dent->d_name);
#endif
    session->bufferpos = 0;
  }

  rc = send(session->data_fd, session->buffer + session->bufferpos,
            session->buffersize - session->bufferpos, 0);
  if(rc <= 0)
  {
    if(rc < 0)
      console_print("send: %s\n", strerror(SOC_GetErrno()));
    else
      console_print("send: %s\n", strerror(ECONNRESET));

    ftp_session_close_cwd(session);
    ftp_session_set_state(session, COMMAND_STATE);
    ftp_send_response(session, 426, "Connection broken during transfer\r\n");
    return;
  }

  session->bufferpos += rc;
}

static void
retrieve_transfer(ftp_session_t *session)
{
  ssize_t rc;

  if(session->bufferpos == session->buffersize)
  {
    rc = ftp_session_read_file(session);
    if(rc <= 0)
    {
      ftp_session_close_file(session);
      ftp_session_set_state(session, COMMAND_STATE);
      if(rc < 0)
        ftp_send_response(session, 451, "Failed to read file\r\n");
      else
        ftp_send_response(session, 226, "OK\r\n");
      return;
    }

    session->bufferpos  = 0;
    session->buffersize = rc;
  }

  rc = send(session->data_fd, session->buffer + session->bufferpos,
            session->buffersize - session->bufferpos, 0);
  if(rc <= 0)
  {
    if(rc < 0)
      console_print("send: %s\n", strerror(SOC_GetErrno()));
    else
      console_print("send: %s\n", strerror(ECONNRESET));

    ftp_session_close_file(session);
    ftp_session_set_state(session, COMMAND_STATE);
    ftp_send_response(session, 426, "Connection broken during transfer\r\n");
    return;
  }

  session->bufferpos += rc;
}

static void
store_transfer(ftp_session_t *session)
{
  ssize_t rc;

  if(session->bufferpos == session->buffersize)
  {
    rc = recv(session->data_fd, session->buffer, sizeof(session->buffer), 0);
    if(rc <= 0)
    {
      if(rc < 0)
        console_print("recv: %s\n", strerror(SOC_GetErrno()));

      ftp_session_close_file(session);
      ftp_session_set_state(session, COMMAND_STATE);

      if(rc == 0)
        ftp_send_response(session, 226, "OK\r\n");
      else
        ftp_send_response(session, 426, "Connection broken during transfer\r\n");
      return;
    }

    session->bufferpos  = 0;
    session->buffersize = rc;
  }

  rc = ftp_session_write_file(session);
  if(rc <= 0)
  {
    ftp_session_close_file(session);
    ftp_session_set_state(session, COMMAND_STATE);
    ftp_send_response(session, 451, "Failed to write file\r\n");
    return;
  }

  session->bufferpos += rc;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *                          F T P   C O M M A N D S                          *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

FTP_DECLARE(ALLO)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 202, "superfluous command\r\n");
}

FTP_DECLARE(APPE)
{
  /* TODO */
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 502, "unavailable\r\n");
}

FTP_DECLARE(CDUP)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  cd_up(session);

  return ftp_send_response(session, 200, "OK\r\n");
}

FTP_DECLARE(CWD)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  if(strcmp(args, "..") == 0)
  {
    cd_up(session);
    return ftp_send_response(session, 200, "OK\r\n");
  }

  if(validate_path(args) != 0)
    return ftp_send_response(session, 553, "invalid file name\r\n");

  build_path(session, args);
   
  {
#ifdef _3DS
    Result ret;
    Handle fd;

    ret = FSUSER_OpenDirectory(NULL, &fd, sdmcArchive,
                               FS_makePath(PATH_CHAR, session->buffer));
    if(ret != 0)
      return ftp_send_response(session, 553, "not a directory\r\n");

    ret = FSDIR_Close(fd);
    if(ret != 0)
      console_print("FSDIR_Close: 0x%08X\n", (unsigned int)ret);
#else
    struct stat st;
    int         rc;

    rc = stat(session->buffer, &st);
    if(rc != 0)
    {
      console_print("stat '%s': %s\n", session->buffer, strerror(errno));
      return ftp_send_response(session, 550, "unavailable\r\n");
    }

    if(!S_ISDIR(st.st_mode))
      return ftp_send_response(session, 553, "not a directory\r\n");
#endif
  }

  strncpy(session->cwd, session->buffer, sizeof(session->cwd));
 
  return ftp_send_response(session, 200, "OK\r\n");
}

FTP_DECLARE(DELE)
{
  /* TODO */
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 502, "unavailable\r\n");
}

FTP_DECLARE(FEAT)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 211, "\r\n");
}

FTP_DECLARE(LIST)
{
  ssize_t rc;

  console_print("%s %s\n", __func__, args ? args : "");

  if(ftp_session_open_cwd(session) != 0)
  {
    ftp_session_set_state(session, COMMAND_STATE);
    return ftp_send_response(session, 550, "unavailable\r\n");
  }
  
  if(session->flags & SESSION_PORT)
  {
    ftp_session_set_state(session, DATA_TRANSFER_STATE);
    rc = ftp_session_connect(session);
    if(rc != 0)
    {
      ftp_session_set_state(session, COMMAND_STATE);
      return ftp_send_response(session, 425, "can't open data connection\r\n");
    }
  }
  else if(session->flags & SESSION_PASV)
    ftp_session_set_state(session, DATA_CONNECT_STATE);
  else
  {
    ftp_session_set_state(session, COMMAND_STATE);
    return ftp_send_response(session, 503, "Bad sequence of commands\r\n");
  }

  session->flags &= ~(SESSION_RECV|SESSION_SEND);
  session->flags |= SESSION_SEND;

  session->transfer   = list_transfer;
  session->bufferpos  = 0;
  session->buffersize = 0;
  return 0;
}

FTP_DECLARE(MKD)
{
#ifdef _3DS
  Result ret;
#else
  int    rc;
#endif

  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  if(validate_path(args) != 0)
  {
    ftp_session_set_state(session, COMMAND_STATE);
    return ftp_send_response(session, 553, "invalid file name\r\n");
  }

  build_path(session, args);

#ifdef _3DS
  ret = FSUSER_CreateDirectory(NULL, sdmcArchive, FS_makePath(PATH_CHAR, session->buffer));
  if(ret != 0)
  {
    console_print("FSUSER_OpenDirectory: 0x%08X\n", (unsigned int)ret);
    return ftp_send_response(session, 550, "failed to create directory\r\n");
  }
#else
  rc = mkdir(session->buffer, 0755);
  if(rc != 0)
  {
    console_print("mkdir: %s\n", strerror(errno));
    return ftp_send_response(session, 550, "failed to create directory\r\n");
  }
#endif

  return ftp_send_response(session, 250, "OK\r\n");
}

FTP_DECLARE(MODE)
{
  /* TODO */
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  if(strcasecmp(args, "S") == 0)
    return ftp_send_response(session, 200, "OK\r\n");

  return ftp_send_response(session, 504, "unavailable\r\n");
}

FTP_DECLARE(NLST)
{
  /* TODO */
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 504, "unavailable\r\n");
}

FTP_DECLARE(NOOP)
{
  console_print("%s %s\n", __func__, args ? args : "");
  return ftp_send_response(session, 200, "OK\r\n");
}

FTP_DECLARE(PASS)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 230, "OK\r\n");
}

FTP_DECLARE(PASV)
{
  int       rc;
  char      buffer[INET_ADDRSTRLEN + 10];
  char      *p;
  in_port_t port;

  console_print("%s %s\n", __func__, args ? args : "");

  memset(buffer, 0, sizeof(buffer));

  ftp_session_set_state(session, COMMAND_STATE);

  session->flags &= ~(SESSION_PASV|SESSION_PORT);

  session->pasv_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(session->pasv_fd < 0)
  {
    console_print("socket: %s\n", strerror(SOC_GetErrno()));
    return ftp_send_response(session, 451, "\r\n");
  }

#ifdef _3DS
  console_print("binding to %s:%u\n",
                inet_ntoa(session->pasv_addr.sin_addr),
                ntohs(session->pasv_addr.sin_port));
#endif
  rc = bind(session->pasv_fd, (struct sockaddr*)&session->pasv_addr,
            sizeof(session->pasv_addr));
  if(rc != 0)
  {
    console_print("bind: ret=%d errno=%d %s\n",
                  rc, SOC_GetErrno(), strerror(SOC_GetErrno()));
    ftp_session_close_pasv(session);
    return ftp_send_response(session, 451, "\r\n");
  }

  rc = listen(session->pasv_fd, 5);
  if(rc != 0)
  {
    console_print("listen: %s\n", strerror(SOC_GetErrno()));
    ftp_session_close_pasv(session);
    return ftp_send_response(session, 451, "\r\n");
  }

#ifndef _3DS
  {
    socklen_t addrlen = sizeof(session->pasv_addr);
    rc = getsockname(session->pasv_fd, (struct sockaddr*)&session->pasv_addr,
                     &addrlen);
    if(rc != 0)
    {
      console_print("getsockname: %s\n", strerror(SOC_GetErrno()));
      ftp_session_close_pasv(session);
      return ftp_send_response(session, 451, "\r\n");
    }
  }
#endif

  console_print("listening on %s:%u\n",
                inet_ntoa(session->pasv_addr.sin_addr),
                ntohs(session->pasv_addr.sin_port));

  session->flags |= SESSION_PASV;

  port = ntohs(session->pasv_addr.sin_port);
  strcpy(buffer, inet_ntoa(session->pasv_addr.sin_addr));
  sprintf(buffer+strlen(buffer), ",%u,%u",
          port >> 8, port & 0xFF);
  for(p = buffer; *p; ++p)
  {
    if(*p == '.')
      *p = ',';
  }

  return ftp_send_response(session, 227, "%s\r\n", buffer);
}

FTP_DECLARE(PORT)
{
  char               *addrstr, *p, *portstr;
  int                commas = 0, rc;
  short              port = 0;
  unsigned long      val;
  struct sockaddr_in addr;

  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  session->flags &= ~(SESSION_PASV|SESSION_PORT);

  addrstr = strdup(args);
  if(addrstr == NULL)
    return ftp_send_response(session, 425, "%s\r\n", strerror(ENOMEM));

  for(p = addrstr; *p; ++p)
  {
    if(*p == ',')
    {
      if(commas != 3)
        *p = '.';
      else
      {
        *p = 0;
        portstr = p+1;
      }
      ++commas;
    }
  }

  if(commas != 5)
  {
    free(addrstr);
    return ftp_send_response(session, 501, "%s\r\n", strerror(EINVAL));
  }

  rc = inet_aton(addrstr, &addr.sin_addr);
  if(rc == 0)
  {
    free(addrstr);
    return ftp_send_response(session, 501, "%s\r\n", strerror(EINVAL));
  }

  val  = 0;
  port = 0;
  for(p = portstr; *p; ++p)
  {
    if(!isdigit((int)*p))
    {
      if(p == portstr || *p != '.' || val > 0xFF)
      {
        free(addrstr);
        return ftp_send_response(session, 501, "%s\r\n", strerror(EINVAL));
      }
      port <<= 8;
      port  += val;
      val    = 0;
    }
    else
    {
      val *= 10;
      val += *p - '0';
    }
  }

  if(val > 0xFF || port > 0xFF)
  {
    free(addrstr);
    return ftp_send_response(session, 501, "%s\r\n", strerror(EINVAL));
  }
  port <<= 8;
  port  += val;

  addr.sin_family = AF_INET;
  addr.sin_port   = htons(port);

  free(addrstr);

  memcpy(&session->peer_addr, &addr, sizeof(addr));

  session->flags |= SESSION_PORT;

  return ftp_send_response(session, 200, "OK\r\n");
}

FTP_DECLARE(PWD)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 257, "\"%s\"\r\n", session->cwd);
}

FTP_DECLARE(QUIT)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_send_response(session, 221, "disconnecting\r\n");
  ftp_session_close_cmd(session);

  return 0;
}

FTP_DECLARE(REST)
{
  /* TODO */
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 502, "unavailable\r\n");
}

FTP_DECLARE(RETR)
{
  int rc;

  console_print("%s %s\n", __func__, args ? args : "");

  if(validate_path(args) != 0)
  {
    ftp_session_set_state(session, COMMAND_STATE);
    return ftp_send_response(session, 553, "invalid file name\r\n");
  }

  build_path(session, args);

  if(ftp_session_open_file_read(session) != 0)
  {
    ftp_session_set_state(session, COMMAND_STATE);
    return ftp_send_response(session, 450, "failed to open file\r\n");
  }

  if(session->flags & SESSION_PORT)
  {
    ftp_session_set_state(session, DATA_TRANSFER_STATE);
    rc = ftp_session_connect(session);
    if(rc != 0)
    {
      ftp_session_set_state(session, COMMAND_STATE);
      return ftp_send_response(session, 425, "can't open data connection\r\n");
    }
  }
  else if(session->flags & SESSION_PASV)
    ftp_session_set_state(session, DATA_CONNECT_STATE);
  else
  {
    ftp_session_set_state(session, COMMAND_STATE);
    return ftp_send_response(session, 503, "Bad sequence of commands\r\n");
  }

  session->flags &= ~(SESSION_RECV|SESSION_SEND);
  session->flags |= SESSION_SEND;

  session->transfer   = retrieve_transfer;
  session->bufferpos  = 0;
  session->buffersize = 0;
  return 0;
}

FTP_DECLARE(RMD)
{
  /* TODO */
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 502, "unavailable\r\n");
}

FTP_DECLARE(RNFR)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  session->flags &= ~SESSION_RENAME;

  if(validate_path(args) != 0)
    return ftp_send_response(session, 553, "invalid file name\r\n");

  build_path(session, args);

  session->flags |= SESSION_RENAME;

  return ftp_send_response(session, 200, "OK\r\n");
}

FTP_DECLARE(RNTO)
{
  char buffer[1024];

  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  if(!(session->flags & SESSION_RENAME))
    return ftp_send_response(session, 503, "Bad sequence of commands\r\n");

  session->flags &= ~SESSION_RENAME;

  memcpy(buffer, session->buffer, 1024);

  if(validate_path(args) != 0)
    return ftp_send_response(session, 554, "invalid file name\r\n");

  build_path(session, args);

  /* TODO perform rename(buffer, session->buffer) */
  return ftp_send_response(session, 502, "unavailable\r\n");
}

FTP_DECLARE(STOR)
{
  int rc;

  console_print("%s %s\n", __func__, args ? args : "");

  if(validate_path(args) != 0)
  {
    ftp_session_set_state(session, COMMAND_STATE);
    return ftp_send_response(session, 553, "invalid file name\r\n");
  }

  build_path(session, args);

  if(ftp_session_open_file_write(session) != 0)
  {
    ftp_session_set_state(session, COMMAND_STATE);
    return ftp_send_response(session, 450, "failed to open file\r\n");
  }

  if(session->flags & SESSION_PORT)
  {
    ftp_session_set_state(session, DATA_TRANSFER_STATE);
    rc = ftp_session_connect(session);
    if(rc != 0)
    {
      ftp_session_set_state(session, COMMAND_STATE);
      return ftp_send_response(session, 425, "can't open data connection\r\n");
    }
  }
  else if(session->flags & SESSION_PASV)
    ftp_session_set_state(session, DATA_CONNECT_STATE);
  else
  {
    ftp_session_set_state(session, COMMAND_STATE);
    return ftp_send_response(session, 503, "Bad sequence of commands\r\n");
  }

  session->flags &= ~(SESSION_RECV|SESSION_SEND);
  session->flags |= SESSION_RECV;

  session->transfer   = store_transfer;
  session->bufferpos  = 0;
  session->buffersize = 0;
  return 0;
}

FTP_DECLARE(STOU)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 502, "unavailable\r\n");
}

FTP_DECLARE(STRU)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  if(strcasecmp(args, "F") == 0)
    return ftp_send_response(session, 200, "OK\r\n");

  return ftp_send_response(session, 504, "unavailable\r\n");
}

FTP_DECLARE(SYST)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 215, "UNIX Type: L8\r\n");
}

FTP_DECLARE(TYPE)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 200, "OK\r\n");
}

FTP_DECLARE(USER)
{
  console_print("%s %s\n", __func__, args ? args : "");

  ftp_session_set_state(session, COMMAND_STATE);

  return ftp_send_response(session, 230, "OK\r\n");
}
