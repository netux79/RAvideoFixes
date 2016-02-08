/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 *  Copyright (C) 2012-2014 - OV2
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

#ifdef _XBOX
#include <xtl.h>
#endif

#include "../d3d/d3d.h"
#include "../common/win32_common.h"

#include "../../runloop.h"
#include "../video_monitor.h"

#ifdef _MSC_VER
#ifndef _XBOX
#pragma comment( lib, "d3d9" )
#pragma comment( lib, "d3dx9" )
#pragma comment( lib, "cgd3d9" )
#pragma comment( lib, "dxguid" )
#endif
#endif

#if defined(_XBOX1)
#define XBOX_PRESENTATIONINTERVAL D3DRS_PRESENTATIONINTERVAL
#define PresentationInterval FullScreen_PresentationInterval
#elif defined(_XBOX360)
#define XBOX_PRESENTATIONINTERVAL D3DRS_PRESENTINTERVAL
#endif

#ifdef _XBOX
static bool widescreen_mode = false;
#endif

static d3d_video_t *curD3D = NULL;
static bool d3d_quit = false;
static void *dinput;

extern bool d3d_restore(d3d_video_t *data);

static void d3d_resize(void *data, unsigned new_width, unsigned new_height)
{
   d3d_video_t *d3d      = (d3d_video_t*)curD3D;
   LPDIRECT3DDEVICE d3dr = (LPDIRECT3DDEVICE)d3d->dev;

   if (!d3dr)
      return;

   (void)data;

   if (new_width != d3d->video_info.width || new_height != d3d->video_info.height)
   {
      RARCH_LOG("[D3D]: Resize %ux%u.\n", new_width, new_height);
      d3d->video_info.width  = new_width;
      d3d->video_info.height = new_height;
      video_driver_set_size_width(new_width);
      video_driver_set_size_height(new_height);
      d3d_restore(d3d);
   }
}

#ifdef HAVE_WINDOW
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message,
      WPARAM wParam, LPARAM lParam)
{
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();

   switch (message)
   {
      case WM_CREATE:
         {
            LPCREATESTRUCT p_cs   = (LPCREATESTRUCT)lParam;
            curD3D                = (d3d_video_t*)p_cs->lpCreateParams;
         }
         break;
      case WM_CHAR:
      case WM_KEYDOWN:
      case WM_KEYUP:
      case WM_SYSKEYUP:
      case WM_SYSKEYDOWN:
         return win32_handle_keyboard_event(hWnd, message, wParam, lParam);

      case WM_DESTROY:
         d3d_quit = true;
         return 0;
      case WM_SIZE:
         {
            unsigned new_width  = LOWORD(lParam);
            unsigned new_height = HIWORD(lParam);

            if (new_width && new_height)
               d3d_resize(driver->video_data, new_width, new_height);
         }
         return 0;
      case WM_COMMAND:
         if (settings->ui.menubar_enable)
         {
            d3d_video_t *d3d = (d3d_video_t*)driver->video_data;
            HWND        d3dr = d3d->hWnd;
            LRESULT      ret = win32_menu_loop(d3dr, wParam);
         }
         break;
   }

   if (dinput_handle_message(dinput, message, wParam, lParam))
      return 0;
   return DefWindowProc(hWnd, message, wParam, lParam);
}
#endif

static void gfx_ctx_d3d_swap_buffers(void *data)
{
   d3d_video_t      *d3d = (d3d_video_t*)data;
   LPDIRECT3DDEVICE d3dr = (LPDIRECT3DDEVICE)d3d->dev;

   d3d_swap(d3d, d3dr);
}

static void gfx_ctx_d3d_update_title(void *data)
{
   char buf[128]        = {0};
   char buffer_fps[128] = {0};
   d3d_video_t *d3d     = (d3d_video_t*)data;
   settings_t *settings = config_get_ptr();

   if (video_monitor_get_fps(buf, sizeof(buf),
            buffer_fps, sizeof(buffer_fps)))
   {
#ifndef _XBOX
      SetWindowText(d3d->hWnd, buf);
#endif
   }

   if (settings->fps_show)
   {
#ifdef _XBOX
      MEMORYSTATUS stat;
      char mem[128] = {0};

      GlobalMemoryStatus(&stat);
      snprintf(mem, sizeof(mem), "|| MEM: %.2f/%.2fMB",
            stat.dwAvailPhys/(1024.0f*1024.0f), stat.dwTotalPhys/(1024.0f*1024.0f));
      strlcat(buffer_fps, mem, sizeof(buffer_fps));
#endif
      rarch_main_msg_queue_push(buffer_fps, 1, 1, false);
   }
}

static void gfx_ctx_d3d_show_mouse(void *data, bool state)
{
   (void)data;

   win32_show_cursor(state);
}

static void gfx_ctx_d3d_check_window(void *data, bool *quit,
      bool *resize, unsigned *width,
      unsigned *height, unsigned frame_count)
{
   d3d_video_t *d3d = (d3d_video_t*)data;

   (void)data;

   *quit = false;
   *resize = false;

   if (d3d_quit)
      *quit = true;
   if (d3d->should_resize)
      *resize = true;

   win32_check_window();
}

#ifdef _XBOX
static HANDLE GetFocus(void)
{
   driver_t *driver = driver_get_ptr();
   d3d_video_t *d3d = (d3d_video_t*)driver->video_data;
   return d3d->hWnd;
}
#endif

static bool gfx_ctx_d3d_has_focus(void *data)
{
   d3d_video_t *d3d = (d3d_video_t*)data;
   if (!d3d)
      return false;
   return GetFocus() == d3d->hWnd;
}

static bool gfx_ctx_d3d_suppress_screensaver(void *data, bool enable)
{
   (void)data;
   (void)enable;

   return false;
}

static bool gfx_ctx_d3d_has_windowed(void *data)
{
   (void)data;

#ifdef _XBOX
   return false;
#else
   return true;
#endif
}

static bool gfx_ctx_d3d_bind_api(void *data,
      enum gfx_ctx_api api, unsigned major, unsigned minor)
{
   (void)data;
   (void)major;
   (void)minor;
   (void)api;

#if defined(HAVE_D3D8)
   return api == GFX_CTX_DIRECT3D8_API;
#else
   /* As long as we don't have a D3D11 implementation, we default to this */
   return api == GFX_CTX_DIRECT3D9_API;
#endif
}

static bool gfx_ctx_d3d_init(void *data)
{
   (void)data;

   d3d_quit = false;

   return true;
}

static void gfx_ctx_d3d_destroy(void *data)
{
   (void)data;
}

static void gfx_ctx_d3d_input_driver(void *data,
      const input_driver_t **input, void **input_data)
{
#ifdef _XBOX
   void *xinput = input_xinput.init();
   *input       = xinput ? (const input_driver_t*)&input_xinput : NULL;
   *input_data  = xinput;
#else
   dinput       = input_dinput.init();
   *input       = dinput ? &input_dinput : NULL;
   *input_data  = dinput;
#endif
   (void)data;
}

static void gfx_ctx_d3d_get_video_size(void *data,
      unsigned *width, unsigned *height)
{
   d3d_video_t *d3d = (d3d_video_t*)data;

#ifdef _XBOX
   (void)width;
   (void)height;
#if defined(_XBOX360)
   XVIDEO_MODE video_mode;

   XGetVideoMode(&video_mode);

   *width  = video_mode.dwDisplayWidth;
   *height = video_mode.dwDisplayHeight;

   d3d->resolution_hd_enable = false;

   if(video_mode.fIsHiDef)
   {
      *width = 1280;
      *height = 720;
      d3d->resolution_hd_enable = true;
   }
   else
   {
      *width = 640;
      *height = 480;
   }

   widescreen_mode = video_mode.fIsWideScreen;
#elif defined(_XBOX1)
   DWORD video_mode = XGetVideoFlags();

   *width  = 640;
   *height = 480;

   widescreen_mode = false;

   /* Only valid in PAL mode, not valid for HDTV modes! */

   if(XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I)
   {
      /* Check for 16:9 mode (PAL REGION) */
      if(video_mode & XC_VIDEO_FLAGS_WIDESCREEN)
      {
         *width = 720;
         //60 Hz, 720x480i
         if(video_mode & XC_VIDEO_FLAGS_PAL_60Hz)
            *height = 480;
         else //50 Hz, 720x576i
            *height = 576;
         widescreen_mode = true;
      }
   }
   else
   {
      /* Check for 16:9 mode (NTSC REGIONS) */
      if(video_mode & XC_VIDEO_FLAGS_WIDESCREEN)
      {
         *width = 720;
         *height = 480;
         widescreen_mode = true;
      }
   }

   if(XGetAVPack() == XC_AV_PACK_HDTV)
   {
      if(video_mode & XC_VIDEO_FLAGS_HDTV_480p)
      {
         *width = 640;
         *height  = 480;
         widescreen_mode = false;
         d3d->resolution_hd_enable = true;
      }
      else if(video_mode & XC_VIDEO_FLAGS_HDTV_720p)
      {
         *width = 1280;
         *height  = 720;
         widescreen_mode = true;
         d3d->resolution_hd_enable = true;
      }
      else if(video_mode & XC_VIDEO_FLAGS_HDTV_1080i)
      {
         *width = 1920;
         *height  = 1080;
         widescreen_mode = true;
         d3d->resolution_hd_enable = true;
      }
   }
#endif
#endif
}

static void gfx_ctx_d3d_swap_interval(void *data, unsigned interval)
{
   d3d_video_t      *d3d = (d3d_video_t*)data;
#ifdef _XBOX
   LPDIRECT3DDEVICE d3dr = (LPDIRECT3DDEVICE)d3d->dev;
   unsigned d3d_interval = interval ? 
      D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

   d3dr->SetRenderState(XBOX_PRESENTATIONINTERVAL, d3d_interval);
#else
   d3d_restore(d3d);
#endif
}

static bool gfx_ctx_d3d_get_metrics(void *data,
	enum display_metric_types type, float *value)
{
   return win32_get_metrics(data, type, value);
}

const gfx_ctx_driver_t gfx_ctx_d3d = {
   gfx_ctx_d3d_init,
   gfx_ctx_d3d_destroy,
   gfx_ctx_d3d_bind_api,
   gfx_ctx_d3d_swap_interval,
   NULL,
   gfx_ctx_d3d_get_video_size,
   NULL, /* get_video_output_size */
   NULL, /* get_video_output_prev */
   NULL, /* get_video_output_next */
   gfx_ctx_d3d_get_metrics,
   NULL,
   gfx_ctx_d3d_update_title,
   gfx_ctx_d3d_check_window,
   d3d_resize,
   gfx_ctx_d3d_has_focus,
   gfx_ctx_d3d_suppress_screensaver,
   gfx_ctx_d3d_has_windowed,
   gfx_ctx_d3d_swap_buffers,
   gfx_ctx_d3d_input_driver,
   NULL,
   NULL,
   NULL,
   gfx_ctx_d3d_show_mouse,
   "d3d",
};
