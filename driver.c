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

#include "driver.h"
#include "general.h"
#include "retroarch.h"
#include "msg_hash.h"
#include "compat/posix_string.h"
#include "gfx/video_monitor.h"
#include "audio/audio_monitor.h"

#include "msg_hash.h"

#ifdef HAVE_MENU
#include "menu/menu.h"
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

static driver_t *g_driver = NULL;

void driver_free(void)
{
   if (g_driver)
      free(g_driver);

   g_driver = NULL;
}

static driver_t *driver_new(void)
{
   driver_t *driver = (driver_t*)calloc(1, sizeof(driver_t));

   if (!driver)
      return NULL;

   return driver;
}

void driver_clear_state(void)
{
   driver_free();
   g_driver  = driver_new();
}

driver_t *driver_get_ptr(void)
{
   return g_driver;
}

#define HASH_LOCATION_DRIVER           0x09189689U
#define HASH_CAMERA_DRIVER             0xf25db959U
#define HASH_MENU_DRIVER               0xd607fb05U
#define HASH_INPUT_DRIVER              0x4c087840U
#define HASH_INPUT_JOYPAD_DRIVER       0xab124146U
#define HASH_VIDEO_DRIVER              0x1805a5e7U
#define HASH_AUDIO_DRIVER              0x26594002U
#define HASH_AUDIO_RESAMPLER_DRIVER    0xedcba9ecU
#define HASH_RECORD_DRIVER             0x144cd2cfU
/**
 * find_driver_nonempty:
 * @label              : string of driver type to be found.
 * @i                  : index of driver.
 * @str                : identifier name of the found driver
 *                       gets written to this string.
 * @len                : size of @str.
 *
 * Find driver based on @label.
 *
 * Returns: NULL if no driver based on @label found, otherwise
 * pointer to driver.
 **/
static const void *find_driver_nonempty(const char *label, int i,
      char *s, size_t len)
{
   const void *drv = NULL;
   uint32_t hash   = msg_hash_calculate(label);

   switch (hash)
   {
      case HASH_CAMERA_DRIVER:
         drv = camera_driver_find_handle(i);
         if (drv)
            strlcpy(s, camera_driver_find_ident(i), len);
         break;
      case HASH_LOCATION_DRIVER:
         drv = location_driver_find_handle(i);
         if (drv)
            strlcpy(s, location_driver_find_ident(i), len);
         break;
      case HASH_MENU_DRIVER:
#ifdef HAVE_MENU
         drv = menu_driver_find_handle(i);
         if (drv)
            strlcpy(s, menu_driver_find_ident(i), len);
#endif
         break;
      case HASH_INPUT_DRIVER:
         drv = input_driver_find_handle(i);
         if (drv)
            strlcpy(s, input_driver_find_ident(i), len);
         break;
      case HASH_INPUT_JOYPAD_DRIVER:
         drv = joypad_driver_find_handle(i);
         if (drv)
            strlcpy(s, joypad_driver_find_ident(i), len);
         break;
      case HASH_VIDEO_DRIVER:
         drv = video_driver_find_handle(i);
         if (drv)
            strlcpy(s, video_driver_find_ident(i), len);
         break;
      case HASH_AUDIO_DRIVER:
         drv = audio_driver_find_handle(i);
         if (drv)
            strlcpy(s, audio_driver_find_ident(i), len);
         break;
      case HASH_RECORD_DRIVER:
         drv = record_driver_find_handle(i);
         if (drv)
            strlcpy(s, record_driver_find_ident(i), len);
         break;
      case HASH_AUDIO_RESAMPLER_DRIVER:
         drv = audio_resampler_driver_find_handle(i);
         if (drv)
            strlcpy(s, audio_resampler_driver_find_ident(i), len);
         break;
   }

   return drv;
}

/**
 * find_driver_index:
 * @label              : string of driver type to be found.
 * @drv                : identifier of driver to be found.
 *
 * Find index of the driver, based on @label.
 *
 * Returns: -1 if no driver based on @label and @drv found, otherwise
 * index number of the driver found in the array.
 **/
int find_driver_index(const char * label, const char *drv)
{
   unsigned i;
   char str[PATH_MAX_LENGTH] = {0};
   const void           *obj = NULL;

   for (i = 0; (obj = (const void*)
            find_driver_nonempty(label, i, str, sizeof(str))) != NULL; i++)
   {
      if (!obj)
         return -1;
      if (str[0] == '\0')
         break;
      if (!strcasecmp(drv, str))
         return i;
   }

   return -1;
}

bool find_first_driver(const char *label, char *s, size_t len)
{
   find_driver_nonempty(label, 0, s, len);
   return true;
}

/**
 * find_prev_driver:
 * @label              : string of driver type to be found.
 * @s                  : identifier of driver to be found.
 * @len                : size of @s.
 *
 * Find previous driver in driver array.
 **/
bool find_prev_driver(const char *label, char *s, size_t len)
{
   int i = find_driver_index(label, s);
   if (i > 0)
      find_driver_nonempty(label, i - 1, s, len);
   else
   {
      RARCH_WARN(
            "Couldn't find any previous driver (current one: \"%s\").\n", s);
      return false;
   }
   return true;
}

/**
 * find_next_driver:
 * @label              : string of driver type to be found.
 * @s                  : identifier of driver to be found.
 * @len                : size of @s.
 *
 * Find next driver in driver array.
 **/
bool find_next_driver(const char *label, char *s, size_t len)
{
   int i = find_driver_index(label, s);
   if (i >= 0 && (strcmp(s, "null") != 0))
      find_driver_nonempty(label, i + 1, s, len);
   else
   {
      RARCH_WARN("Couldn't find any next driver (current one: \"%s\").\n", s);
      return false;
   }
   return true;
}

/**
 * init_drivers_pre:
 *
 * Attempts to find a default driver for 
 * all driver types.
 *
 * Should be run before init_drivers().
 **/
void init_drivers_pre(void)
{
   find_audio_driver();
   find_video_driver();
   find_input_driver();
   find_camera_driver();
   find_location_driver();
#ifdef HAVE_MENU
   find_menu_driver();
#endif
}

static void driver_adjust_system_rates(void)
{
   rarch_system_info_t *system = rarch_system_info_get_ptr();
   driver_t            *driver = driver_get_ptr();

   audio_monitor_adjust_system_rates();
   video_monitor_adjust_system_rates();

   if (!driver->video_data)
      return;

   if (system->force_nonblock)
      event_command(EVENT_CMD_VIDEO_SET_NONBLOCKING_STATE);
   else
      driver_set_nonblock_state(driver->nonblock_state);
}

/**
 * driver_set_refresh_rate:
 * @hz                 : New refresh rate for monitor.
 *
 * Sets monitor refresh rate to new value by calling
 * video_monitor_set_refresh_rate(). Subsequently
 * calls audio_monitor_set_refresh_rate().
 **/
void driver_set_refresh_rate(float hz)
{
   video_monitor_set_refresh_rate(hz);
   audio_monitor_set_refresh_rate();
   driver_adjust_system_rates();
}

/**
 * driver_set_nonblock_state:
 * @enable             : Enable nonblock state?
 *
 * Sets audio and video drivers to nonblock state.
 *
 * If @enable is false, sets blocking state for both
 * audio and video drivers instead.
 **/
void driver_set_nonblock_state(bool enable)
{
   settings_t *settings = config_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();
   driver_t *driver     = driver_get_ptr();

   /* Only apply non-block-state for video if we're using vsync. */
   if (driver->video_active && driver->video_data)
   {
      bool video_nonblock = enable;

      if (!settings->video.vsync || system->force_nonblock)
         video_nonblock = true;
      video_driver_set_nonblock_state(video_nonblock);
   }

   audio_driver_set_nonblocking_state(enable);
}

/**
 * driver_update_system_av_info:
 * @info               : pointer to new A/V info
 *
 * Update the system Audio/Video information. 
 * Will reinitialize audio/video drivers.
 * Used by RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO.
 *
 * Returns: true (1) if successful, otherwise false (0).
 **/
bool driver_update_system_av_info(const struct retro_system_av_info *info)
{
   struct retro_system_av_info *av_info = video_viewport_get_system_av_info();
   driver_t *driver = driver_get_ptr();

   memcpy(av_info, info, sizeof(*av_info));
   event_command(EVENT_CMD_REINIT);

   /* Cannot continue recording with different parameters.
    * Take the easiest route out and just restart the recording. */
   if (driver->recording_data)
   {
      rarch_main_msg_queue_push_new(
            MSG_RESTARTING_RECORDING_DUE_TO_DRIVER_REINIT, 2, 180, false);
      event_command(EVENT_CMD_RECORD_DEINIT);
      event_command(EVENT_CMD_RECORD_INIT);
   }

   return true;
}

/**
 * menu_update_libretro_info:
 *
 * Update menu state which depends on config.
 **/
static void menu_update_libretro_info(void)
{
   global_t *global               = global_get_ptr();
   struct retro_system_info *info = global ? &global->menu.info : NULL;

   if (!global || !info)
      return;

#ifndef HAVE_DYNAMIC
   retro_get_system_info(info);
#endif

   event_command(EVENT_CMD_CORE_INFO_INIT);
   event_command(EVENT_CMD_LOAD_CORE_PERSIST);
}

/**
 * init_drivers:
 * @flags              : Bitmask of drivers to initialize.
 *
 * Initializes drivers.
 * @flags determines which drivers get initialized.
 **/
void init_drivers(int flags)
{
   driver_t *driver   = driver_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   if (flags & DRIVER_VIDEO)
      driver->video_data_own = false;
   if (flags & DRIVER_AUDIO)
      driver->audio_data_own = false;
   if (flags & DRIVER_INPUT)
      driver->input_data_own = false;
   if (flags & DRIVER_CAMERA)
      driver->camera_data_own = false;
   if (flags & DRIVER_LOCATION)
      driver->location_data_own = false;

#ifdef HAVE_MENU
   /* By default, we want the menu to persist through driver reinits. */
   driver->menu_data_own = true;
#endif

   if (flags & (DRIVER_VIDEO | DRIVER_AUDIO))
      driver_adjust_system_rates();

   if (flags & DRIVER_VIDEO)
   {
      const struct retro_hw_render_callback *hw_render = 
         (const struct retro_hw_render_callback*)video_driver_callback();

      video_monitor_reset();

      init_video();

      if (!driver->video_cache_context_ack
            && hw_render->context_reset)
         hw_render->context_reset();
      driver->video_cache_context_ack = false;

      system->frame_time_last = 0;
   }

   if (flags & DRIVER_AUDIO)
      init_audio();

   /* Only initialize camera driver if we're ever going to use it. */
   if ((flags & DRIVER_CAMERA) && driver->camera_active)
      init_camera();

   /* Only initialize location driver if we're ever going to use it. */
   if ((flags & DRIVER_LOCATION) && driver->location_active)
      init_location();

#ifdef HAVE_MENU
   menu_update_libretro_info();

   if (flags & DRIVER_MENU)
   {
      init_menu();
      menu_driver_context_reset();
   }
#endif

   if (flags & (DRIVER_VIDEO | DRIVER_AUDIO))
   {
      /* Keep non-throttled state as good as possible. */
      if (driver->nonblock_state)
         driver_set_nonblock_state(driver->nonblock_state);
   }
}


/**
 * uninit_drivers:
 * @flags              : Bitmask of drivers to deinitialize.
 *
 * Deinitializes drivers.
 * @flags determines which drivers get deinitialized.
 **/
void uninit_drivers(int flags)
{
   driver_t *driver = driver_get_ptr();

#ifdef HAVE_MENU
   if (flags & DRIVER_MENU)
   {
      menu_driver_context_destroy();

      if (!driver->menu_data_own)
      {
         menu_free(driver->menu);
         driver->menu = NULL;
      }
   }
#endif

   if ((flags & DRIVER_LOCATION) && !driver->location_data_own)
   {
      uninit_location();
      driver->location_data = NULL;
   }

   if ((flags & DRIVER_CAMERA) && !driver->camera_data_own)
   {
      uninit_camera();
      driver->camera_data = NULL;
   }

   if (flags & DRIVER_AUDIO)
      uninit_audio();

   if (flags & DRIVERS_VIDEO_INPUT)
      uninit_video_input();

   if (flags & DRIVER_VIDEO)
   {
      const struct retro_hw_render_callback *hw_render = 
         (const struct retro_hw_render_callback*)video_driver_callback();

      if (hw_render->context_destroy && !driver->video_cache_context)
            hw_render->context_destroy();
   }

   if ((flags & DRIVER_VIDEO) && !driver->video_data_own)
      driver->video_data = NULL;

   if ((flags & DRIVER_INPUT) && !driver->input_data_own)
      driver->input_data = NULL;

   if ((flags & DRIVER_AUDIO) && !driver->audio_data_own)
      driver->audio_data = NULL;
}

