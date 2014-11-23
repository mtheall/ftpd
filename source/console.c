#include "console.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#ifdef _3DS
#include <3ds.h>
#endif
#include "debug.h"

#ifdef _3DS
#include "sans_8_kerning_bin.h"
#include "sans_8_render_bin.h"
#include "sans_10_kerning_bin.h"
#include "sans_10_render_bin.h"
#include "sans_12_kerning_bin.h"
#include "sans_12_render_bin.h"
#include "sans_14_kerning_bin.h"
#include "sans_14_render_bin.h"
#include "sans_16_kerning_bin.h"
#include "sans_16_render_bin.h"

/* TODO: add support for non-ASCII characters */

/*! rendering information */
typedef struct
{
  int  c;      /*!< character */
  int  y_off;  /*!< vertical offset */
  int  width;  /*!< width */
  int  height; /*!< height */
  int  x_adv;  /*!< horizontal advance */
  u8   data[]; /*!< width*height bitmap */
} render_info_t;

/*! kerning information */
typedef struct
{
  int prev;  /*!< previous character */
  int next;  /*!< next character */
  int x_off; /*!< horizontal adjustment */
} kerning_info_t;

/*! font data */
typedef struct
{
  const char     *name;            /*!< font name */
  render_info_t  **render_info;    /*!< render information list */
  kerning_info_t *kerning_info;    /*!< kerning information list */
  size_t         num_render_info;  /*!< number of render information nodes */
  size_t         num_kerning_info; /*!< number of kerning information nodes */
  int            pt;               /*!< font size */
} font_t;

/*! font information */
typedef struct
{
  const char *name;              /*!< font name */
  int        pt;                 /*!< font size */
  const u8   *render_data;       /*!< render data */
  const u8   *kerning_data;      /*!< kerning data */
  const u32  *render_data_size;  /*!< render data size */
  const u32  *kerning_data_size; /*!< kerning data size */
} font_info_t;

/*! font descriptors */
static font_info_t font_info[] =
{
#define FONT_INFO(name, pt) \
  { #name, pt, name##_##pt##_render_bin, name##_##pt##_kerning_bin, \
    &name##_##pt##_render_bin_size, &name##_##pt##_kerning_bin_size, }
  FONT_INFO(sans,  8),
  FONT_INFO(sans, 10),
  FONT_INFO(sans, 12),
  FONT_INFO(sans, 14),
  FONT_INFO(sans, 16),
};
/*! number of font descriptors */
static const size_t num_font_info = sizeof(font_info)/sizeof(font_info[0]);

/*! find next render info
 *
 *  @param[in] info current render info
 *
 *  @returns next render info
 */
static render_info_t*
next_render_info(render_info_t *info)
{
  char *ptr = (char*)info;
  ptr += sizeof(*info) + info->width*info->height;
  ptr = (char*)(((int)ptr + sizeof(int)-1) & ~(sizeof(int)-1));

  return (render_info_t*)ptr;
}

/*! free font info
 *
 *  @param[in] font
 */
static void
free_font(font_t *font)
{
  free(font->render_info);
  free(font);
}

/*! load font info
 *
 *  @param[in] name
 *  @param[in] pt
 *  @param[in] render_data
 *  @param[in] render_data_size
 *  @param[in] kerning data
 *  @param[in] kerning_data_size
 *
 *  @returns font info
 */
static font_t*
load_font(const char *name,
          int        pt,
          const u8   *render_data,
          size_t     render_data_size,
          const u8   *kerning_data,
          size_t     kerning_data_size)
{
  size_t        i;
  render_info_t *rinfo;
  font_t        *font;

  /* allocate new font info */
  font = (font_t*)calloc(1, sizeof(font_t));
  if(font != NULL)
  {
    /* count number of render entries */
    rinfo = (render_info_t*)render_data;
    while((u8*)rinfo < render_data + render_data_size)
    {
      ++font->num_render_info;
      rinfo = next_render_info(rinfo);
    }

    /* allocate array of render info pointers */
    font->render_info = (render_info_t**)calloc(font->num_render_info, sizeof(render_info_t));
    if(font->render_info != NULL)
    {
      /* fill in the pointer list */
      rinfo = (render_info_t*)render_data;
      i     = 0;
      while((u8*)rinfo < render_data + render_data_size)
      {
        font->render_info[i++] = rinfo;
        rinfo = next_render_info(rinfo);
      }

      /* fill in the kerning info */
      font->kerning_info     = (kerning_info_t*)kerning_data;
      font->num_kerning_info = kerning_data_size / sizeof(kerning_info_t);

      /* set font size and name */
      font->pt   = pt;
      font->name = name;
    }
    else
    {
      /* failed to allocate render info list */
      free_font(font);
      font = NULL;
    }
  }

  return font;
}

/*! list of font info entries */
static font_t **fonts;
/*! number of font info entries */
static size_t num_fonts = 0;

/*! compare two fonts
 *
 *  @param[in] p1 left side of comparison (font_t**)
 *  @param[in] p2 right side of comparison (font_t**)
 *
 *  @returns <0 if p1 <  p2
 *  @returns 0 if  p1 == p2
 *  @returns >0 if p1 >  p2
 */
static int
font_cmp(const void *p1,
         const void *p2)
{
  /* interpret parameters */
  font_t *f1 = *(font_t**)p1;
  font_t *f2 = *(font_t**)p2;

  /* major key is font name */
  int rc = strcmp(f1->name, f2->name);
  if(rc != 0)
    return rc;

  /* minor key is font size */
  if(f1->pt < f2->pt)
    return -1;
  if(f1->pt > f2->pt)
    return 1;
  return 0;
}

/*! search for a font by name and size
 *
 *  @param[in] name font name
 *  @param[in] pt   font size
 *
 *  @returns matching font
 */
static font_t*
find_font(const char *name,
          int        pt)
{
  /* create a key to search for */
  font_t key, *keyptr;
  key.name = name;
  key.pt   = pt;
  keyptr   = &key;

  /* search for the key */
  void *font = bsearch(&keyptr, fonts, num_fonts, sizeof(font_t*), font_cmp);
  if(font == NULL)
    return NULL;

  /* found it */
  return *(font_t**)font;
}

/*! initialize console subsystem */
void
console_init(void)
{
  size_t i;

  /* allocate font list */
  fonts = (font_t**)calloc(num_font_info, sizeof(font_t*));
  if(fonts == NULL)
    return;

  /* load fonts */
  for(i = 0; i < num_font_info; ++i)
  {
    font_info_t *info = &font_info[i];
    fonts[num_fonts] = load_font(info->name, info->pt,
                                 info->render_data,
                                 *info->render_data_size,
                                 info->kerning_data,
                                 *info->kerning_data_size);
    if(fonts[num_fonts] != NULL)
      ++num_fonts;
  }

  /* sort the list for bsearch later */
  qsort(fonts, num_fonts, sizeof(font_t*), font_cmp);
}

/*! deinitialize console subsystem */
void
console_exit(void)
{
  int i;

  /* free the font info */
  for(i = 0; i < num_fonts; ++i)
    free_font(fonts[i]);

  /* free the font info list */
  free(fonts);
  fonts = NULL;
}

/*! status bar contents */
static char status[64];
/*! console buffer */
static char buffer[8192];
/*! pointer to end of buffer */
static char *buffer_end = buffer + sizeof(buffer);
/*! pointer to end of console contents */
static char *end = buffer;

/*! count lines in console contents */
static size_t
count_lines(void)
{
  size_t lines = 0;
  char   *p    = buffer;

  /* search for each newline character */
  while(p < end && (p = strchr(p, '\n')) != NULL)
  {
    ++lines;
    ++p;
  }

  return lines;
}

/*! remove lines that have "scrolled" off screen */
static void
reduce_lines(void)
{
  int  lines = count_lines();
  char *p    = buffer;

  /* we can fit 18 lines on the screen */
  /* TODO make based on pt size */
  while(lines > 18)
  {
    p = strchr(p, '\n');
    ++p;
    --lines;
  }

  /* move the new beginning to where it needs to be */
  ptrdiff_t distance = p - buffer;
  memmove(buffer, buffer+distance, end - p);
  end -= distance;
  *end = 0;
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

  va_start(ap, fmt);
  memset(status, 0, sizeof(status));
  vsnprintf(status, sizeof(status)-1, fmt, ap);
  va_end(ap);
}

/*! add text to the console
 *
 *  @param[in] fmt format string
 *  @param[in] ... format arguments
 */
void
console_print(const char *fmt, ...)
{
  int     rc;
  va_list ap;

  /* append to the end of the console buffer */
  va_start(ap, fmt);
  rc = vsnprintf(end, buffer_end - end - 1, fmt, ap);
  va_end(ap);

  /* null terminate buffer */
  end += rc;
  if(end >= buffer_end)
    end = buffer_end - 1;
  *end = 0;

  /* scroll */
  reduce_lines();
}

/*! compare render information
 *
 *  @param[in] p1 left side of comparison (render_info_t**)
 *  @param[in] p2 right side of comparison (render_info_t**)
 *
 *  @returns <0 if p1 <  p2
 *  @returns 0 if  p1 == p2
 *  @returns >0 if p1 >  p2
 */
static int
render_info_cmp(const void *p1,
                const void *p2)
{
  /* interpret parameters */
  render_info_t *r1 = *(render_info_t**)p1;
  render_info_t *r2 = *(render_info_t**)p2;

  /* ordered by character */
  if(r1->c < r2->c)
    return -1;
  else if(r1->c > r2->c)
    return 1;
  return 0;
}

/*! search for render info by character
 *
 *  @param[in] font font info
 *  @param[in] char character
 *
 *  @returns matching render info
 */
static render_info_t*
find_render_info(font_t *font,
                 char   c)
{
  /* create a key to search for */
  render_info_t key, *keyptr;
  key.c = c;
  keyptr = &key;

  /* search for the key */
  void *info = bsearch(&keyptr, font->render_info, font->num_render_info,
                       sizeof(render_info_t*), render_info_cmp);
  if(info == NULL)
    return NULL;

  /* found it */
  return *(render_info_t**)info;
}

/*! compare kerning information
 *
 *  @param[in] p1 left side of comparison (kerning_info_t*)
 *  @param[in] p2 right side of comparison (kerning_info_t*)
 *
 *  @returns <0 if p1 <  p2
 *  @returns 0 if  p1 == p2
 *  @returns >0 if p1 >  p2
 */
static int
kerning_info_cmp(const void *p1,
                 const void *p2)
{
  /* interpret parameters */
  kerning_info_t *k1 = (kerning_info_t*)p1;
  kerning_info_t *k2 = (kerning_info_t*)p2;

  /* major key is prev */
  if(k1->prev < k2->prev)
    return -1;
  if(k1->prev > k2->prev)
    return 1;

  /* minor key is next */
  if(k1->next < k2->next)
    return -1;
  if(k1->next > k2->next)
    return 1;

  return 0;
}

/*! search for kerning info by character pair
 *
 *  @param[in] font font info
 *  @param[in] prev prev character
 *  @param[in] next next character
 *
 *  @returns matching render info
 */
static kerning_info_t*
find_kerning_info(font_t *font,
                  char   prev,
                  char   next)
{
  /* create a key to search for */
  kerning_info_t key;
  key.prev = prev;
  key.next = next;

  /* search for the key */
  void *info = bsearch(&key, font->kerning_info, font->num_kerning_info,
                       sizeof(kerning_info_t), kerning_info_cmp);
  if(info == NULL)
    return NULL;

  /* found it */
  return (kerning_info_t*)info;
}

/*! clear framebuffer
 *
 *  @param[in] screen   screen to clear
 *  @param[in] side     which side on the stereoscopic display
 *  @param[in] rgbColor clear color
 */
static void
clear_screen(gfxScreen_t screen,
             gfx3dSide_t side,
             u8          rgbColor[3])
{
  /* get the framebuffer information */
  u16 fbWidth, fbHeight;
  u8  *fb = gfxGetFramebuffer(screen, side, &fbWidth, &fbHeight);

  /* fill the framebuffer with the clear color */
  int i;
  for(i = 0; i < fbWidth*fbHeight; ++i)
  {
    *(fb++) = rgbColor[2];
    *(fb++) = rgbColor[1];
    *(fb++) = rgbColor[0];
  }
}

/*! draw a quad
 *
 *  @param[in] screen screen to draw to
 *  @param[in] side   which side on the stereoscopic display
 *  @param[in] data   quad data
 *  @param[in] x      quad x position
 *  @param[in] y      quad y position
 *  @param[in] w      quad width
 *  @param[in] h      quad height
 *
 *  @note this quad data is 8-bit alpha-only
 *  @note uses framebuffer native coordinates
 */
static void
draw_quad(gfxScreen_t screen,
          gfx3dSide_t side,
          const u8    *data,
          int         x,
          int         y,
          int         w,
          int         h)
{
  int i, j;
  int index  = 0;
  int stride = w;

  /* get the framebuffer information */
  u16 width, height;
  u8  *fb = gfxGetFramebuffer(screen, side, &width, &height);

  /* this quad is totally offscreen; don't draw */
  if(x > width || y > height)
    return;

  /* this quad is totally offscreen; don't draw */
  if(x + w < 0 || y + h < 0)
    return;

  /* adjust parameters for partially visible quad */
  if(x < 0)
  {
    index -= x;
    w     += x;
    x      =  0;
  }

  /* adjust parameters for partially visible quad */
  if(y < 0)
  {
    index -= y*stride;
    h     += y;
    y      = 0;
  }

  /* adjust parameters for partially visible quad */
  if(x + w > width)
    w = width - x;

  /* adjust parameters for partially visible quad */
  if(y + h > height)
    h = height - y;

  /* move framebuffer pointer to quad start position */
  fb += (y*width + x)*3;

  /* fill in data */
  for(j = 0; j < h; ++j)
  {
    for(i = 0; i < w; ++i)
    {
      /* alpha blending; assuming color is white */
      int v = data[index];
      fb[0] = fb[0]*(0xFF-v)/0xFF + v;
      fb[1] = fb[1]*(0xFF-v)/0xFF + v;
      fb[2] = fb[2]*(0xFF-v)/0xFF + v;

      ++index;
      fb += 3;
    }

    index += (stride-w);
    fb   += (width-w)*3;
  }
}

/*! draw text to framebuffer
 *
 *  @param[in] screen screen to draw to
 *  @param[in] side   which side on the stereoscopic display
 *  @param[in] font   font to use when rendering
 *  @param[in] data   quad data
 *  @param[in] x      quad x position
 *  @param[in] y      quad y position
 *
 *  @note uses intuitive coordinates
 */
static void
draw_text(gfxScreen_t screen,
          gfx3dSide_t side,
          font_t      *font,
          const char  *data,
          int         x,
          int         y)
{
  render_info_t  *rinfo;
  kerning_info_t *kinfo;
  const char     *p;
  int            xoff = x, yoff = y;
  char           prev = 0;

  /* draw each character */
  for(p = data; *p != 0; ++p)
  {
    /* newline; move down a line and all the way left */
    if(*p == '\n')
    {
      xoff  = x;
      yoff += font->pt + font->pt/2;
      prev  = 0;
      continue;
    }

    /* look up the render info for this character */
    rinfo = find_render_info(font, *p);

    /* couldn't find it; just ignore it */
    if(rinfo == NULL)
      continue;

    /* find kerning data */
    kinfo = NULL;
    if(prev != 0)
      kinfo = find_kerning_info(font, prev, *p);

    /* adjust for kerning */
    if(kinfo != NULL)
      xoff += kinfo->x_off >> 6;

    /* save this character for next kerning lookup */
    prev = *p;

    /* render character */
    if(rinfo->width != 0 && rinfo->height != 0)
    {
      int x, y;

      /* get framebuffer info */
      u16 width, height;
      gfxGetFramebuffer(screen, side, &width, &height);

      /* transform intuitive coordinates to framebuffer-native */
      x = width - yoff - font->pt - 2 - (rinfo->height - rinfo->y_off);
      y = xoff;

      /* draw character */
      draw_quad(screen, side, rinfo->data, x, y,
                rinfo->height, rinfo->width);
    }

    /* advance to next character coordinate */
    xoff += rinfo->x_adv >> 6;
  }
}

/*! draw console to screen */
void
console_render(void)
{
  font_t *font;

  /* clear all screens */
  u8 bluish[] = { 0, 0, 127 };
  clear_screen(GFX_TOP,    GFX_LEFT,  bluish);
  clear_screen(GFX_BOTTOM, GFX_LEFT,  bluish);

  /* look up font for status bar and draw status bar */
  font = find_font("sans", 10);
  if(font != NULL)
    draw_text(GFX_TOP, GFX_LEFT,  font, status, 4, 4);
  else
    debug("%s: couldn't find 'sans 10pt'\n", __func__);

  /* look up font for console and draw console */
  font = find_font("sans", 8);
  if(font != NULL)
    draw_text(GFX_TOP, GFX_LEFT,  font, buffer, 4, 20);
  else
    debug("%s: couldn't find 'sans 8pt'\n", __func__);

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
console_exit(void)
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
