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

#ifndef __CAMERA_DRIVER__H
#define __CAMERA_DRIVER__H

#include <stdint.h>
#include <boolean.h>
#include "../libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct camera_driver
{
   /* FIXME: params for initialization - queries for resolution,
    * framerate, color format which might or might not be honored. */
   void *(*init)(const char *device, uint64_t buffer_types,
         unsigned width, unsigned height);

   void (*free)(void *data);

   bool (*start)(void *data);
   void (*stop)(void *data);

   /* Polls the camera driver.
    * Will call the appropriate callback if a new frame is ready.
    * Returns true if a new frame was handled. */
   bool (*poll)(void *data,
         retro_camera_frame_raw_framebuffer_t frame_raw_cb,
         retro_camera_frame_opengl_texture_t frame_gl_cb);

   const char *ident;
} camera_driver_t;

extern camera_driver_t camera_v4l2;
extern camera_driver_t camera_android;
extern camera_driver_t camera_rwebcam;
extern camera_driver_t camera_avfoundation;
extern camera_driver_t camera_null;

/**
 * driver_camera_start:
 *
 * Starts camera driver.
 * Used by RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE.
 *
 * Returns: true (1) if successful, otherwise false (0).
 **/
bool driver_camera_start(void);

/**
 * driver_camera_stop:
 *
 * Stops camera driver.
 * Used by RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE.
 *
 * Returns: true (1) if successful, otherwise false (0).
 **/
void driver_camera_stop(void);

/**
 * driver_camera_poll:
 *
 * Call camera driver's poll function.
 * Used by RETRO_ENVIRONMENT_GET_CAMERA_INTERFACE.
 *
 * Returns: true (1) if successful, otherwise false (0).
 **/
void driver_camera_poll(void);

/**
 * config_get_camera_driver_options:
 *
 * Get an enumerated list of all camera driver names,
 * separated by '|'.
 *
 * Returns: string listing of all camera driver names,
 * separated by '|'.
 **/
const char* config_get_camera_driver_options(void);

/**
 * camera_driver_find_handle:
 * @index              : index of driver to get handle to.
 *
 * Returns: handle to camera driver at index. Can be NULL
 * if nothing found.
 **/
const void *camera_driver_find_handle(int index);

/**
 * camera_driver_find_ident:
 * @index              : index of driver to get handle to.
 *
 * Returns: Human-readable identifier of camera driver at index. Can be NULL
 * if nothing found.
 **/
const char *camera_driver_find_ident(int index);

void find_camera_driver(void);

void init_camera(void);

void uninit_camera(void);

#ifdef __cplusplus
}
#endif

#endif
