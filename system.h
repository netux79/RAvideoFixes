/*  RetroArch - A frontend for libretro.
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

#ifndef __RARCH_SYSTEM_H
#define __RARCH_SYSTEM_H

#include "configuration.h"
#include "core_options.h"
#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rarch_system_info
{
   char title_buf[64];

   struct retro_system_info info;

   unsigned rotation;
   bool shutdown;
   unsigned performance_level;

   bool block_extract;
   bool force_nonblock;
   bool no_content;

   const char *input_desc_btn[MAX_USERS][RARCH_FIRST_META_KEY];
   char valid_extensions[PATH_MAX_LENGTH];

   retro_keyboard_event_t key_event;

   struct retro_disk_control_callback disk_control; 
   struct retro_camera_callback camera_callback;
   struct retro_location_callback location_callback;

   struct retro_frame_time_callback frame_time;
   retro_usec_t frame_time_last;

   core_option_manager_t *core_options;

   struct retro_subsystem_info *special;
   unsigned num_special;

   struct retro_controller_info *ports;
   unsigned num_ports;
} rarch_system_info_t;

rarch_system_info_t *rarch_system_info_get_ptr(void);

void rarch_system_info_free(void);

void rarch_system_info_init(void);

#ifdef __cplusplus
}
#endif

#endif
