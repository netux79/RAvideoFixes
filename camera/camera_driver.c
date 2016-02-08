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

#include <string.h>
#include <string/string_list.h>
#include "camera_driver.h"
#include "../driver.h"
#include "../general.h"
#include "../system.h"

static const camera_driver_t *camera_drivers[] = {
#ifdef HAVE_V4L2
   &camera_v4l2,
#endif
#ifdef EMSCRIPTEN
   &camera_rwebcam,
#endif
#ifdef ANDROID
   &camera_android,
#endif
#if defined(HAVE_AVFOUNDATION)
#if defined(HAVE_COCOA) || defined(HAVE_COCOATOUCH)
    &camera_avfoundation,
#endif
#endif
   &camera_null,
   NULL,
};

/**
 * camera_driver_find_handle:
 * @idx                : index of driver to get handle to.
 *
 * Returns: handle to camera driver at index. Can be NULL
 * if nothing found.
 **/
const void *camera_driver_find_handle(int idx)
{
   const void *drv = camera_drivers[idx];
   if (!drv)
      return NULL;
   return drv;
}

/**
 * camera_driver_find_ident:
 * @idx                : index of driver to get handle to.
 *
 * Returns: Human-readable identifier of camera driver at index. Can be NULL
 * if nothing found.
 **/
const char *camera_driver_find_ident(int idx)
{
   const camera_driver_t *drv = camera_drivers[idx];
   if (!drv)
      return NULL;
   return drv->ident;
}

/**
 * config_get_camera_driver_options:
 *
 * Get an enumerated list of all camera driver names,
 * separated by '|'.
 *
 * Returns: string listing of all camera driver names,
 * separated by '|'.
 **/
const char* config_get_camera_driver_options(void)
{
   union string_list_elem_attr attr;
   unsigned i;
   char *options = NULL;
   int options_len = 0;
   struct string_list *options_l = string_list_new();

   attr.i = 0;

   if (!options_l)
      return NULL;

   for (i = 0; camera_driver_find_handle(i); i++)
   {
      const char *opt = camera_driver_find_ident(i);
      options_len += strlen(opt) + 1;
      string_list_append(options_l, opt, attr);
   }

   options = (char*)calloc(options_len, sizeof(char));

   if (!options)
   {
      string_list_free(options_l);
      options_l = NULL;
      return NULL;
   }

   string_list_join_concat(options, options_len, options_l, "|");

   string_list_free(options_l);
   options_l = NULL;

   return options;
}

void find_camera_driver(void)
{
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();
   int i = find_driver_index("camera_driver", settings->camera.driver);

   if (i >= 0)
      driver->camera = (const camera_driver_t*)camera_driver_find_handle(i);
   else
   {
      unsigned d;
      RARCH_ERR("Couldn't find any camera driver named \"%s\"\n",
            settings->camera.driver);
      RARCH_LOG_OUTPUT("Available camera drivers are:\n");
      for (d = 0; camera_driver_find_handle(d); d++)
         RARCH_LOG_OUTPUT("\t%s\n", camera_driver_find_ident(d));
       
      RARCH_WARN("Going to default to first camera driver...\n");
       
      driver->camera = (const camera_driver_t*)camera_driver_find_handle(0);
       
      if (!driver->camera)
         rarch_fail(1, "find_camera_driver()");
   }
}

/**
 * driver_camera_start:
 *
 * Starts camera driver interface.
 * Used by RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE.
 *
 * Returns: true (1) if successful, otherwise false (0).
 **/
bool driver_camera_start(void)
{
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();

   if (driver->camera && driver->camera_data && driver->camera->start)
   {
      if (settings->camera.allow)
         return driver->camera->start(driver->camera_data);

      rarch_main_msg_queue_push(
            "Camera is explicitly disabled.\n", 1, 180, false);
   }
   return false;
}

/**
 * driver_camera_stop:
 *
 * Stops camera driver.
 * Used by RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE.
 *
 * Returns: true (1) if successful, otherwise false (0).
 **/
void driver_camera_stop(void)
{
   driver_t *driver = driver_get_ptr();
   if (driver->camera && driver->camera->stop && driver->camera_data)
      driver->camera->stop(driver->camera_data);
}

/**
 * driver_camera_poll:
 *
 * Call camera driver's poll function.
 * Used by RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE.
 *
 * Returns: true (1) if successful, otherwise false (0).
 **/
void driver_camera_poll(void)
{
   driver_t            *driver = driver_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   if (driver->camera && driver->camera->poll && driver->camera_data)
      driver->camera->poll(driver->camera_data,
            system->camera_callback.frame_raw_framebuffer,
            system->camera_callback.frame_opengl_texture);
}

void init_camera(void)
{
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   /* Resource leaks will follow if camera is initialized twice. */
   if (driver->camera_data)
      return;

   find_camera_driver();

   driver->camera_data = driver->camera->init(
         *settings->camera.device ? settings->camera.device : NULL,
         system->camera_callback.caps,
         settings->camera.width ?
         settings->camera.width : system->camera_callback.width,
         settings->camera.height ?
         settings->camera.height : system->camera_callback.height);

   if (!driver->camera_data)
   {
      RARCH_ERR("Failed to initialize camera driver. Will continue without camera.\n");
      driver->camera_active = false;
   }

   if (system->camera_callback.initialized)
      system->camera_callback.initialized();
}

void uninit_camera(void)
{
   driver_t *driver = driver_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   if (driver->camera_data && driver->camera)
   {
      if (system->camera_callback.deinitialized)
         system->camera_callback.deinitialized();

      if (driver->camera->free)
         driver->camera->free(driver->camera_data);
   }
   driver->camera_data = NULL;
}
