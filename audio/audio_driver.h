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

#ifndef __AUDIO_DRIVER__H
#define __AUDIO_DRIVER__H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <boolean.h>
#include "audio_dsp_filter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio_driver
{
   /* Creates and initializes handle to audio driver.
    *
    * Returns: audio driver handle on success, otherwise NULL.
    **/
   void *(*init)(const char *device, unsigned rate, unsigned latency);

   /*
    * @data         : Pointer to audio data handle.
    * @buf          : Audio buffer data.
    * @size         : Size of audio buffer.
    *
    * Write samples to audio driver.
    **/
   ssize_t (*write)(void *data, const void *buf, size_t size);

   /* Stops driver. */
   bool (*stop)(void *data);

   /* Starts driver. */
   bool (*start)(void *data);

   /* Is the audio driver currently running? */
   bool (*alive)(void *data);

   /* Should we care about blocking in audio thread? Fast forwarding. */
   void (*set_nonblock_state)(void *data, bool toggle);

   /* Frees driver data. */
   void (*free)(void *data);

   /* Defines if driver will take standard floating point samples,
    * or int16_t samples. */
   bool (*use_float)(void *data);

   /* Human-readable identifier. */
   const char *ident;

   /* Optional. */
   size_t (*write_avail)(void *data);

   size_t (*buffer_size)(void *data);
} audio_driver_t;

extern audio_driver_t audio_rsound;
extern audio_driver_t audio_oss;
extern audio_driver_t audio_alsa;
extern audio_driver_t audio_alsathread;
extern audio_driver_t audio_roar;
extern audio_driver_t audio_openal;
extern audio_driver_t audio_opensl;
extern audio_driver_t audio_jack;
extern audio_driver_t audio_sdl;
extern audio_driver_t audio_xa;
extern audio_driver_t audio_pulse;
extern audio_driver_t audio_dsound;
extern audio_driver_t audio_coreaudio;
extern audio_driver_t audio_xenon360;
extern audio_driver_t audio_ps3;
extern audio_driver_t audio_gx;
extern audio_driver_t audio_psp1;
extern audio_driver_t audio_ctr;
extern audio_driver_t audio_rwebaudio;
extern audio_driver_t audio_null;

/**
 * audio_driver_find_handle:
 * @index              : index of driver to get handle to.
 *
 * Returns: handle to audio driver at index. Can be NULL
 * if nothing found.
 **/
const void *audio_driver_find_handle(int index);

/**
 * audio_driver_find_ident:
 * @index              : index of driver to get handle to.
 *
 * Returns: Human-readable identifier of audio driver at index. Can be NULL
 * if nothing found.
 **/
const char *audio_driver_find_ident(int index);

bool audio_driver_mute_toggle(void);

/*
 * audio_driver_readjust_input_rate:
 *
 * Readjust the audio input rate.
 */
void audio_driver_readjust_input_rate(void);

bool audio_driver_alive(void);

bool audio_driver_start(void);

bool audio_driver_stop(void);

void audio_driver_set_nonblock_state(bool toggle);

void audio_driver_set_nonblocking_state(bool enable);

/**
 * config_get_audio_driver_options:
 *
 * Get an enumerated list of all audio driver names, separated by '|'.
 *
 * Returns: string listing of all audio driver names, separated by '|'.
 **/
const char* config_get_audio_driver_options(void);

void find_audio_driver(void);

void uninit_audio(void);

void init_audio(void);

ssize_t audio_driver_write(const void *buf, size_t size);

bool audio_driver_flush(const int16_t *data, size_t samples);

void audio_driver_sample(int16_t left, int16_t right);

size_t audio_driver_sample_batch(const int16_t *data, size_t frames);

void audio_driver_sample_rewind(int16_t left, int16_t right);

size_t audio_driver_sample_batch_rewind(const int16_t *data, size_t frames);

void audio_driver_set_volume_gain(float gain);

void audio_driver_dsp_filter_free(void);

void audio_driver_dsp_filter_init(const char *device);

void audio_driver_setup_rewind(void);

void audio_driver_frame_is_reverse(void);

void audio_driver_set_buffer_size(size_t bufsize);

void audio_driver_set_callback(const void *info);

bool audio_driver_has_callback(void);

void audio_driver_callback(void);

void audio_driver_callback_set_state(bool state);

#ifdef __cplusplus
}
#endif

#endif
