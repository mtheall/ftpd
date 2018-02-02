
#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#ifdef _3DS
#include <3ds.h>
#elif defined(SWITCH)
#include <switch.h>
#endif
#include "console.h"
#include "ftp.h"

/*! looping mechanism
 *
 *  @param[in] callback function to call during each iteration
 *
 *  @returns loop status
 */
static loop_status_t
loop(loop_status_t (*callback)(void))
{
  loop_status_t status = LOOP_CONTINUE;

#ifdef _3DS
  while(aptMainLoop())
  {
    status = callback();
    console_render();
    if(status != LOOP_CONTINUE)
      return status;
  }
  return LOOP_EXIT;
#elif defined(SWITCH)
  while(appletMainLoop())
  {
    status = callback();
    console_render();
    if(status != LOOP_CONTINUE)
      return status;
  }
  return LOOP_EXIT;
#else
  while(status == LOOP_CONTINUE)
    status = callback();
  return status;
#endif
}

#ifdef _3DS
/*! wait until the B button is pressed
 *
 *  @returns loop status
 */
static loop_status_t
wait_for_b(void)
{
  /* update button state */
  hidScanInput();

  /* check if B was pressed */
  if(hidKeysDown() & KEY_B)
    return LOOP_EXIT;

  /* B was not pressed */
  return LOOP_CONTINUE;
}
#elif defined(SWITCH)
/*! wait until the B button is pressed
 *
 *  @returns loop status
 */
static loop_status_t
wait_for_b(void)
{
  /* update button state */
  hidScanInput();

  /* check if B was pressed */
  if(hidKeysDown(CONTROLLER_P1_AUTO) & KEY_B)
    return LOOP_EXIT;

  /* B was not pressed */
  return LOOP_CONTINUE;
}
#endif

/*! entry point
 *
 *  @param[in] argc unused
 *  @param[in] argv unused
 *
 *  returns exit status
 */
int
main(int  argc,
     char *argv[])
{
  loop_status_t status = LOOP_RESTART;

#ifdef _3DS
  /* initialize needed 3DS services */
  acInit();
  gfxInitDefault();
  gfxSet3D(false);
  sdmcWriteSafe(false);
#elif defined(SWITCH)
  //gfxInitResolution(644, 480);
  gfxInitDefault();
#endif

  /* initialize console subsystem */
  console_init();

#ifdef ENABLE_LOGGING
  /* open log file */
#ifdef _3DS
  FILE *fp = freopen("/ftpd.log", "wb", stderr);
#else
  FILE *fp = freopen("ftpd.log", "wb", stderr);
#endif
  if(fp == NULL)
  {
    console_print(RED "freopen: 0x%08X\n" RESET, errno);
    goto log_fail;
  }

  /* truncate log file */
  if(ftruncate(fileno(fp), 0) != 0)
  {
    console_print(RED "ftruncate: 0x%08X\n" RESET, errno);
    goto log_fail;
  }
#endif

  console_set_status("\n" GREEN STATUS_STRING
#ifdef ENABLE_LOGGING
                     " DEBUG"
#endif
                     RESET);

  while(status == LOOP_RESTART)
  {
    /* initialize ftp subsystem */
    if(ftp_init() == 0)
    {
      /* ftp loop */
      status = loop(ftp_loop);

      /* done with ftp */
      ftp_exit();
    }
    else
      status = LOOP_EXIT;
  }

#if defined(_3DS) || defined(SWITCH)
  console_print("Press B to exit\n");
#endif

#ifdef ENABLE_LOGGING
log_fail:
  if(fclose(stderr) != 0)
    console_print(RED "fclose(%d): 0x%08X\n" RESET, fileno(stderr), errno);
#endif

#ifdef _3DS
  loop(wait_for_b);

  /* deinitialize 3DS services */
  gfxExit();
  acExit();
#elif defined(SWITCH)
  loop(wait_for_b);

  /* deinitialize Switch services */
  gfxExit();
#endif
  return 0;
}
