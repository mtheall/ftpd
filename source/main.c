#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef _3DS
#include <3ds.h>
#endif
#include "console.h"
#include "ftp.h"

/*! looping mechanism
 *
 *  @param[in] callback function to call during each iteration
 */
static void
loop(int (*callback)(void))
{
#ifdef _3DS
  int        rc;
  APP_STATUS status;

  /* check apt status */
  while((status = aptGetStatus()) != APP_EXITING)
  {
    rc = 0;
    if(status == APP_RUNNING)
      rc = callback();
    else if(status == APP_SUSPENDING)
      aptReturnToMenu();
    else if(status == APP_SLEEPMODE)
      aptWaitStatusEvent();

    if(rc == 0)
      console_render();
    else
      return;
  }
#else
  for(;;)
    callback();
#endif
}

/*! wait until the B button is pressed
 *
 *  @returns -1 if B was pressed
 */
static int
wait_for_b(void)
{
#ifdef _3DS
  /* update button state */
  hidScanInput();

  /* check if B was pressed */
  if(hidKeysDown() & KEY_B)
    return -1;

  /* B was not pressed */
  return 0;
#else
  return -1;
#endif
}

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
#ifdef _3DS
  /* initialize needed 3DS services */
  srvInit();
  aptInit();
  hidInit(NULL);
  irrstInit(NULL);
  gfxInit();
  gfxSet3D(false);
#endif

  /* initialize console subsystem */
  console_init();
  console_set_status(STATUS_STRING);

  /* initialize ftp subsystem */
  if(ftp_init() == 0)
  {
    /* ftp loop */
    loop(ftp_loop);

    /* done with ftp */
    ftp_exit();
  }

  console_print("Press B to exit\n");
  loop(wait_for_b);
  console_exit();

#ifdef _3DS
  /* deinitialize 3DS services */
  gfxExit();
  irrstExit();
  hidExit();
  aptExit();
  srvExit();
#endif

  return 0;
}
