#ifndef _FTP_3DS_FSFILE_H
#define _FTP_3DS_FSFILE_H

#include <stdlib.h>

#ifdef _3DS
#include <3ds.h>
typedef struct {
    Handle fileHandle;
    u64    offset;
    int    fsErrno;
    //char buf[BUFSIZE]; //is buf write needed?
} FSFILE;

#else

typedef struct {
    int    fd;
} FSFILE;

#ifndef s64
#define s64 signed long long
#endif

#endif

void FSFILE_Init();
void FSFILE_Exit();
FSFILE* FSFILE_Fopen(const char *path, const char *mode);
int FSFILE_Fread(FSFILE *f, void *buf, size_t count);
int FSFILE_Fwrite(FSFILE *f, void *buf, size_t count);
int FSFILE_Fflush(FSFILE *f);
int FSFILE_Fclose(FSFILE *f);
s64 FSFILE_Fsize(FSFILE *f);

#endif