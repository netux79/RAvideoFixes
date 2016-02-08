/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "font_driver.h"
#include "font_renderer_driver.h"
#include "../general.h"

#ifdef HAVE_D3D
static const font_renderer_t *d3d_font_backends[] = {
#if defined(_XBOX1)
   &d3d_xdk1_font,
#elif defined(_XBOX360)
   &d3d_xbox360_font,
#elif defined(_WIN32)
   &d3d_win32_font,
#endif
};

static bool d3d_font_init_first(
      const void **font_driver, void **font_handle,
      void *video_data, const char *font_path, float font_size)
{
   unsigned i;

   for (i = 0; i < ARRAY_SIZE(d3d_font_backends); i++)
   {
      void *data = d3d_font_backends[i]->init(video_data, font_path, font_size);

      if (!data)
         continue;

      *font_driver = d3d_font_backends[i];
      *font_handle = data;

      return true;
   }

   return false;
}
#endif

#ifdef HAVE_OPENGL
static const font_renderer_t *gl_font_backends[] = {
   &gl_raster_font,
#if defined(HAVE_LIBDBGFONT)
   &libdbg_font,
#endif
   NULL,
};

static bool gl_font_init_first(
      const void **font_driver, void **font_handle,
      void *video_data, const char *font_path, float font_size)
{
   unsigned i;

   for (i = 0; gl_font_backends[i]; i++)
   {
      void *data = gl_font_backends[i]->init(video_data, font_path, font_size);

      if (!data)
         continue;

      *font_driver = gl_font_backends[i];
      *font_handle = data;
      return true;
   }

   return false;
}
#endif

bool font_init_first(const void **font_driver, void **font_handle,
      void *video_data, const char *font_path, float font_size,
      enum font_driver_render_api api)
{
   if (font_path && !font_path[0])
      font_path = NULL;

   switch (api)
   {
#ifdef HAVE_D3D
      case FONT_DRIVER_RENDER_DIRECT3D_API:
         return d3d_font_init_first(font_driver, font_handle,
               video_data, font_path, font_size);
#endif
#ifdef HAVE_OPENGL
      case FONT_DRIVER_RENDER_OPENGL_API:
         return gl_font_init_first(font_driver, font_handle,
               video_data, font_path, font_size);
#endif
      case FONT_DRIVER_RENDER_DONT_CARE:
         /* TODO/FIXME - lookup graphics driver's 'API' */
         break;
      default:
         break;
   }

   return false;
}
