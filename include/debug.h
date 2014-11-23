#pragma once

#include <string.h>
#ifdef _3DS
#include <3ds.h>
#endif

/*! print debug message
 *
 *  @param[in] fmt format string
 *  @param[in] ap  varargs list
 *
 *  @returns number of characters written
 */
static inline int
vdebug(const char *fmt,
       va_list    ap)
{
#ifdef _3DS
  int  rc;
  char buffer[256];

  memset(buffer, 0, sizeof(buffer));

  /* print to buffer */
  rc = vsnprintf(buffer, sizeof(buffer)-1, fmt, ap);

  /* call debug service with buffer */
  svcOutputDebugString(buffer, rc < sizeof(buffer) ? rc : sizeof(buffer));
  return rc;
#else
  /* just print to stdout */
  return vprintf(fmt, ap);
#endif
}

__attribute__((format(printf,1,2)))
/*! print debug message
 *
 *  @param[in] fmt format string
 *  @param[in] ... format arguments
 *
 *  @returns number of characters written
 */
static inline int
debug(const char *fmt, ...)
{
  int  rc;
  va_list ap;
  va_start(ap, fmt);
  rc = vdebug(fmt, ap);
  va_end(ap);
  return rc;
}
