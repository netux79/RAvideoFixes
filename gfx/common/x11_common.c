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

#include "x11_common.h"
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <math.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "../../general.h"

static void x11_hide_mouse(Display *dpy, Window win)
{
   static char bm_no_data[] = {0, 0, 0, 0, 0, 0, 0, 0};
   Cursor no_ptr;
   Pixmap bm_no;
   XColor black, dummy;
   Colormap colormap = DefaultColormap(dpy, DefaultScreen(dpy));

   if (!XAllocNamedColor(dpy, colormap, "black", &black, &dummy))
      return;

   bm_no  = XCreateBitmapFromData(dpy, win, bm_no_data, 8, 8);
   no_ptr = XCreatePixmapCursor(dpy, bm_no, bm_no, &black, &black, 0, 0);

   XDefineCursor(dpy, win, no_ptr);
   XFreeCursor(dpy, no_ptr);

   if (bm_no != None)
      XFreePixmap(dpy, bm_no);

   XFreeColors(dpy, colormap, &black.pixel, 1, 0);
}

void x11_show_mouse(Display *dpy, Window win, bool state)
{
   if (state)
      XUndefineCursor(dpy, win);
   else
      x11_hide_mouse(dpy, win);
}

static Atom XA_NET_WM_STATE;
static Atom XA_NET_WM_STATE_FULLSCREEN;
static Atom XA_NET_MOVERESIZE_WINDOW;

#define XA_INIT(x) XA##x = XInternAtom(dpy, #x, False)
#define _NET_WM_STATE_ADD 1
#define MOVERESIZE_GRAVITY_CENTER 5
#define MOVERESIZE_X_SHIFT 8
#define MOVERESIZE_Y_SHIFT 9

void x11_windowed_fullscreen(Display *dpy, Window win)
{
   XEvent xev = {0};

   XA_INIT(_NET_WM_STATE);
   XA_INIT(_NET_WM_STATE_FULLSCREEN);

   xev.xclient.type = ClientMessage;
   xev.xclient.send_event = True;
   xev.xclient.message_type = XA_NET_WM_STATE;
   xev.xclient.window = win;
   xev.xclient.format = 32;
   xev.xclient.data.l[0] = _NET_WM_STATE_ADD;
   xev.xclient.data.l[1] = XA_NET_WM_STATE_FULLSCREEN;

   XSendEvent(dpy, DefaultRootWindow(dpy), False,
         SubstructureRedirectMask | SubstructureNotifyMask,
         &xev);
}

/* Try to be nice to tiling WMs if possible. */

void x11_move_window(Display *dpy, Window win, int x, int y,
      unsigned width, unsigned height)
{
   XEvent xev = {0};

   XA_INIT(_NET_MOVERESIZE_WINDOW);

   xev.xclient.type = ClientMessage;
   xev.xclient.send_event = True;
   xev.xclient.message_type = XA_NET_MOVERESIZE_WINDOW;
   xev.xclient.window = win;
   xev.xclient.format = 32;
   xev.xclient.data.l[0] = (1 << MOVERESIZE_X_SHIFT) | (1 << MOVERESIZE_Y_SHIFT);
   xev.xclient.data.l[1] = x;
   xev.xclient.data.l[2] = y;

   XSendEvent(dpy, DefaultRootWindow(dpy), False,
         SubstructureRedirectMask | SubstructureNotifyMask,
         &xev);
}

static void x11_set_window_class(Display *dpy, Window win)
{
   XClassHint hint = {0};

   hint.res_name   = (char*)"retroarch"; /* Broken header. */
   hint.res_class  = (char*)"retroarch";
   XSetClassHint(dpy, win, &hint);
}

void x11_set_window_attr(Display *dpy, Window win)
{
   x11_set_window_class(dpy, win);
}

void x11_suspend_screensaver(Window wnd)
{
   int ret;
   char cmd[64] = {0};

   RARCH_LOG("Suspending screensaver (X11).\n");

   snprintf(cmd, sizeof(cmd), "xdg-screensaver suspend %d", (int)wnd);

   ret = system(cmd);
   if (ret == -1)
      RARCH_WARN("Failed to launch xdg-screensaver.\n");
   else if (WEXITSTATUS(ret))
      RARCH_WARN("Could not suspend screen saver.\n");
}

static bool get_video_mode(Display *dpy, unsigned width, unsigned height,
      XF86VidModeModeInfo *mode, XF86VidModeModeInfo *desktop_mode)
{
   float refresh_mod;
   int i, num_modes = 0;
   bool ret = false;
   float minimum_fps_diff = 0.0f;
   XF86VidModeModeInfo **modes = NULL;
   settings_t *settings = config_get_ptr();

   XF86VidModeGetAllModeLines(dpy, DefaultScreen(dpy), &num_modes, &modes);

   if (!num_modes)
   {
      XFree(modes);
      return false;
   }

   *desktop_mode = *modes[0];

   /* If we use black frame insertion, we fake a 60 Hz monitor 
    * for 120 Hz one, etc, so try to match that. */
   refresh_mod = settings->video.black_frame_insertion ? 0.5f : 1.0f;

   for (i = 0; i < num_modes; i++)
   {
      float refresh, diff;
      const XF86VidModeModeInfo *m = modes[i];

      if (!m)
         continue;

      if (m->hdisplay != width)
         continue;
      if (m->vdisplay != height)
         continue;

      refresh = refresh_mod * m->dotclock * 1000.0f / (m->htotal * m->vtotal);
      diff    = fabsf(refresh - settings->video.refresh_rate);

      if (!ret || diff < minimum_fps_diff)
      {
         *mode = *m;
         minimum_fps_diff = diff;
      }
      ret = true;
   }

   XFree(modes);
   return ret;
}

bool x11_enter_fullscreen(Display *dpy, unsigned width,
      unsigned height, XF86VidModeModeInfo *desktop_mode)
{
   XF86VidModeModeInfo mode;

   if (!get_video_mode(dpy, width, height, &mode, desktop_mode))
      return false;

   if (!XF86VidModeSwitchToMode(dpy, DefaultScreen(dpy), &mode))
      return false;

   XF86VidModeSetViewPort(dpy, DefaultScreen(dpy), 0, 0);
   return true;
}

void x11_exit_fullscreen(Display *dpy, XF86VidModeModeInfo *desktop_mode)
{
   XF86VidModeSwitchToMode(dpy, DefaultScreen(dpy), desktop_mode);
   XF86VidModeSetViewPort(dpy, DefaultScreen(dpy), 0, 0);
}

#ifdef HAVE_XINERAMA
static XineramaScreenInfo *x11_query_screens(Display *dpy, int *num_screens)
{
   int major, minor;

   if (!XineramaQueryExtension(dpy, &major, &minor))
      return NULL;

   XineramaQueryVersion(dpy, &major, &minor);
   RARCH_LOG("[X11]: Xinerama version: %d.%d.\n", major, minor);

   if (!XineramaIsActive(dpy))
      return NULL;

   return XineramaQueryScreens(dpy, num_screens);
}

bool x11_get_xinerama_coord(Display *dpy, int screen,
      int *x, int *y, unsigned *w, unsigned *h)
{
   int i, num_screens = 0;
   bool ret = false;

   XineramaScreenInfo *info = x11_query_screens(dpy, &num_screens);
   RARCH_LOG("[X11]: Xinerama screens: %d.\n", num_screens);

   for (i = 0; i < num_screens; i++)
   {
      if (info[i].screen_number != screen)
         continue;

      *x = info[i].x_org;
      *y = info[i].y_org;
      *w = info[i].width;
      *h = info[i].height;
      ret = true;
      break;
   }

   XFree(info);
   return ret;
}

unsigned x11_get_xinerama_monitor(Display *dpy, int x, int y,
      int w, int h)
{
   int i, num_screens = 0;
   unsigned monitor   = 0;
   int largest_area   = 0;

   XineramaScreenInfo *info = x11_query_screens(dpy, &num_screens);
   RARCH_LOG("[X11]: Xinerama screens: %d.\n", num_screens);

   for (i = 0; i < num_screens; i++)
   {
      int area;
      int max_lx = max(x, info[i].x_org);
      int min_rx = min(x + w, info[i].x_org + info[i].width);
      int max_ty = max(y, info[i].y_org);
      int min_by = min(y + h, info[i].y_org + info[i].height);

      int len_x = min_rx - max_lx;
      int len_y = min_by - max_ty;

      /* The whole window is outside the screen. */
      if (len_x < 0 || len_y < 0)
         continue;

      area = len_x * len_y;

      if (area > largest_area)
      {
         monitor = i;
         largest_area = area;
      }
   }

   XFree(info);
   return monitor;
}
#endif

bool x11_create_input_context(Display *dpy, Window win, XIM *xim, XIC *xic)
{
   *xim = XOpenIM(dpy, NULL, NULL, NULL);

   if (!*xim)
   {
      RARCH_ERR("[X11]: Failed to open input method.\n");
      return false;
   }

   *xic = XCreateIC(*xim, XNInputStyle,
         XIMPreeditNothing | XIMStatusNothing, XNClientWindow, win, NULL);

   if (!*xic)
   {
      RARCH_ERR("[X11]: Failed to create input context.\n");
      return false;
   }

   XSetICFocus(*xic);
   return true;
}

void x11_destroy_input_context(XIM *xim, XIC *xic)
{
   if (*xic)
   {
      XDestroyIC(*xic);
      *xic = NULL;
   }

   if (*xim)
   {
      XCloseIM(*xim);
      *xim = NULL;
   }
}

bool x11_get_metrics(void *data,
      enum display_metric_types type, float *value)
{
   int pixels_x, pixels_y, physical_width, physical_height;
   unsigned     screen_no  = 0;
   Display           *dpy  = (Display*)XOpenDisplay(NULL);
   pixels_x                = DisplayWidth(dpy, screen_no);
   pixels_y                = DisplayHeight(dpy, screen_no);
   physical_width          = DisplayWidthMM(dpy, screen_no);
   physical_height         = DisplayHeightMM(dpy, screen_no);

   (void)pixels_y;

   XCloseDisplay(dpy);

   switch (type)
   {
      case DISPLAY_METRIC_MM_WIDTH:
         *value = (float)physical_width;
         break;
      case DISPLAY_METRIC_MM_HEIGHT:
         *value = (float)physical_height;
         break;
      case DISPLAY_METRIC_DPI:
         *value = ((((float)pixels_x) * 25.4) / ((float)physical_width));
         break;
      case DISPLAY_METRIC_NONE:
      default:
         *value = 0;
         return false;
   }

   return true;
}
