#include "console.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#ifdef _3DS
#include <3ds.h>
#endif
#include "debug.h"
#include "gfx.h"

#ifdef _3DS
#include "banner_bin.h"

static PrintConsole status_console;
static PrintConsole main_console;

/*! initialize console subsystem */
void
console_init(void)
{
  consoleInit(GFX_TOP, &status_console);
  consoleSetWindow(&status_console, 0, 0, 50, 1);

  consoleInit(GFX_TOP, &main_console);
  consoleSetWindow(&main_console, 0, 1, 50, 29);

  consoleSelect(&main_console);

  consoleDebugInit(debugDevice_NULL);
}

/*! set status bar contents
 *
 *  @param[in] fmt format string
 *  @param[in] ... format arguments
 */
void
console_set_status(const char *fmt, ...)
{
  va_list ap;

  consoleSelect(&status_console);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  consoleSelect(&main_console);
}

/*! add text to the console
 *
 *  @param[in] fmt format string
 *  @param[in] ... format arguments
 */
void
console_print(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

/*! draw console to screen */
void
console_render(void)
{
  /* clear all screens */
  gfxDrawSprite(GFX_BOTTOM, GFX_LEFT, (u8*)banner_bin, 240, 320, 0, 0);

  /* flush framebuffer */
  gfxFlushBuffers();
  gspWaitForVBlank();
  gfxSwapBuffers();
}
#else

/* this is a lot easier when you have a real console */

void
console_init(void)
{
}

void
console_set_status(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  fputc('\n', stdout);
}

void
console_print(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

void console_render(void)
{
}
#endif
