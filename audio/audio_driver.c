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
#include "audio_driver.h"
#include "audio_monitor.h"
#include "audio_utils.h"
#include "audio_thread_wrapper.h"
#include "../driver.h"
#include "../general.h"
#include "../retroarch.h"
#include "../runloop.h"
#include "../performance.h"

#ifndef AUDIO_BUFFER_FREE_SAMPLES_COUNT
#define AUDIO_BUFFER_FREE_SAMPLES_COUNT (8 * 1024)
#endif

typedef struct audio_driver_input_data
{
   float *data;

   size_t data_ptr;
   size_t chunk_size;
   size_t nonblock_chunk_size;
   size_t block_chunk_size;

   double src_ratio;
   float in_rate;

   bool use_float;

   float *outsamples;
   int16_t *conv_outsamples;

   int16_t *rewind_buf;
   size_t rewind_ptr;
   size_t rewind_size;

   rarch_dsp_filter_t *dsp;

   bool rate_control; 
   double orig_src_ratio;
   size_t driver_buffer_size;

   float volume_gain;
   struct retro_audio_callback audio_callback;

   unsigned buffer_free_samples[AUDIO_BUFFER_FREE_SAMPLES_COUNT];
   uint64_t buffer_free_samples_count;
} audio_driver_input_data_t;

static audio_driver_input_data_t audio_data;

static const audio_driver_t *audio_drivers[] = {
#ifdef HAVE_ALSA
   &audio_alsa,
#ifndef __QNX__
   &audio_alsathread,
#endif
#endif
#if defined(HAVE_OSS) || defined(HAVE_OSS_BSD)
   &audio_oss,
#endif
#ifdef HAVE_RSOUND
   &audio_rsound,
#endif
#ifdef HAVE_COREAUDIO
   &audio_coreaudio,
#endif
#ifdef HAVE_AL
   &audio_openal,
#endif
#ifdef HAVE_SL
   &audio_opensl,
#endif
#ifdef HAVE_ROAR
   &audio_roar,
#endif
#ifdef HAVE_JACK
   &audio_jack,
#endif
#if defined(HAVE_SDL) || defined(HAVE_SDL2)
   &audio_sdl,
#endif
#ifdef HAVE_XAUDIO
   &audio_xa,
#endif
#ifdef HAVE_DSOUND
   &audio_dsound,
#endif
#ifdef HAVE_PULSE
   &audio_pulse,
#endif
#ifdef __CELLOS_LV2__
   &audio_ps3,
#endif
#ifdef XENON
   &audio_xenon360,
#endif
#ifdef GEKKO
   &audio_gx,
#endif
#ifdef EMSCRIPTEN
   &audio_rwebaudio,
#endif
#ifdef PSP
   &audio_psp1,
#endif   
#ifdef _3DS
   &audio_ctr,
#endif
   &audio_null,
   NULL,
};

static const audio_driver_t * audio_get_ptr(const driver_t *driver)
{
   if (driver->audio)
      return driver->audio;
   return NULL;
}

/**
 * compute_audio_buffer_statistics:
 *
 * Computes audio buffer statistics.
 *
 **/
static void compute_audio_buffer_statistics(void)
{
   unsigned i, low_water_size, high_water_size, avg, stddev;
   float avg_filled, deviation;
   uint64_t accum = 0, accum_var = 0;
   unsigned low_water_count = 0, high_water_count = 0;
   unsigned samples = 0;
   
   samples = min(audio_data.buffer_free_samples_count,
         AUDIO_BUFFER_FREE_SAMPLES_COUNT);

   if (samples < 3)
      return;

   for (i = 1; i < samples; i++)
      accum += audio_data.buffer_free_samples[i];

   avg = accum / (samples - 1);

   for (i = 1; i < samples; i++)
   {
      int diff = avg - audio_data.buffer_free_samples[i];
      accum_var += diff * diff;
   }

   stddev          = (unsigned)sqrt((double)accum_var / (samples - 2));
   avg_filled      = 1.0f - (float)avg / audio_data.driver_buffer_size;
   deviation       = (float)stddev / audio_data.driver_buffer_size;

   low_water_size  = audio_data.driver_buffer_size * 3 / 4;
   high_water_size = audio_data.driver_buffer_size / 4;

   for (i = 1; i < samples; i++)
   {
      if (audio_data.buffer_free_samples[i] >= low_water_size)
         low_water_count++;
      else if (audio_data.buffer_free_samples[i] <= high_water_size)
         high_water_count++;
   }

   RARCH_LOG("Average audio buffer saturation: %.2f %%, standard deviation (percentage points): %.2f %%.\n",
         avg_filled * 100.0, deviation * 100.0);
   RARCH_LOG("Amount of time spent close to underrun: %.2f %%. Close to blocking: %.2f %%.\n",
         (100.0 * low_water_count) / (samples - 1),
         (100.0 * high_water_count) / (samples - 1));
}

/**
 * audio_driver_find_handle:
 * @idx                : index of driver to get handle to.
 *
 * Returns: handle to audio driver at index. Can be NULL
 * if nothing found.
 **/
const void *audio_driver_find_handle(int idx)
{
   const void *drv = audio_drivers[idx];
   if (!drv)
      return NULL;
   return drv;
}

/**
 * audio_driver_find_ident:
 * @idx                : index of driver to get handle to.
 *
 * Returns: Human-readable identifier of audio driver at index. Can be NULL
 * if nothing found.
 **/
const char *audio_driver_find_ident(int idx)
{
   const audio_driver_t *drv = audio_drivers[idx];
   if (!drv)
      return NULL;
   return drv->ident;
}

/**
 * config_get_audio_driver_options:
 *
 * Get an enumerated list of all audio driver names, separated by '|'.
 *
 * Returns: string listing of all audio driver names, separated by '|'.
 **/
const char* config_get_audio_driver_options(void)
{
   union string_list_elem_attr attr;
   unsigned i;
   char *options   = NULL;
   int options_len = 0;
   struct string_list *options_l = string_list_new();

   attr.i = 0;

   if (!options_l)
      return NULL;

   for (i = 0; audio_driver_find_handle(i); i++)
   {
      const char *opt = audio_driver_find_ident(i);

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

void find_audio_driver(void)
{
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();

   int i = find_driver_index("audio_driver", settings->audio.driver);

   if (i >= 0)
      driver->audio = (const audio_driver_t*)audio_driver_find_handle(i);
   else
   {
      unsigned d;
      RARCH_ERR("Couldn't find any audio driver named \"%s\"\n",
            settings->audio.driver);
      RARCH_LOG_OUTPUT("Available audio drivers are:\n");
      for (d = 0; audio_driver_find_handle(d); d++)
         RARCH_LOG_OUTPUT("\t%s\n", audio_driver_find_ident(d));
      RARCH_WARN("Going to default to first audio driver...\n");

      driver->audio = (const audio_driver_t*)audio_driver_find_handle(0);

      if (!driver->audio)
         rarch_fail(1, "find_audio_driver()");
   }
}

void uninit_audio(void)
{
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();

   if (driver->audio_data && driver->audio)
      driver->audio->free(driver->audio_data);

   free(audio_data.conv_outsamples);
   audio_data.conv_outsamples = NULL;
   audio_data.data_ptr        = 0;

   free(audio_data.rewind_buf);
   audio_data.rewind_buf = NULL;

   if (!settings->audio.enable)
   {
      driver->audio_active = false;
      return;
   }

   rarch_resampler_freep(&driver->resampler,
         &driver->resampler_data);

   free(audio_data.data);
   audio_data.data = NULL;

   free(audio_data.outsamples);
   audio_data.outsamples = NULL;

   event_command(EVENT_CMD_DSP_FILTER_DEINIT);

   compute_audio_buffer_statistics();
}

void init_audio(void)
{
   size_t outsamples_max, max_bufsamples = AUDIO_CHUNK_SIZE_NONBLOCKING * 2;
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();

   audio_convert_init_simd();

   /* Resource leaks will follow if audio is initialized twice. */
   if (driver->audio_data)
      return;

   /* Accomodate rewind since at some point we might have two full buffers. */
   outsamples_max = max_bufsamples * AUDIO_MAX_RATIO * 
      settings->slowmotion_ratio;

   /* Used for recording even if audio isn't enabled. */
   rarch_assert(audio_data.conv_outsamples =
         (int16_t*)malloc(outsamples_max * sizeof(int16_t)));

   if (!audio_data.conv_outsamples)
      goto error;

   audio_data.block_chunk_size    = AUDIO_CHUNK_SIZE_BLOCKING;
   audio_data.nonblock_chunk_size = AUDIO_CHUNK_SIZE_NONBLOCKING;
   audio_data.chunk_size          = audio_data.block_chunk_size;

   /* Needs to be able to hold full content of a full max_bufsamples
    * in addition to its own. */
   rarch_assert(audio_data.rewind_buf = (int16_t*)
         malloc(max_bufsamples * sizeof(int16_t)));

   if (!audio_data.rewind_buf)
      goto error;

   audio_data.rewind_size             = max_bufsamples;

   if (!settings->audio.enable)
   {
      driver->audio_active = false;
      return;
   }

   find_audio_driver();
#ifdef HAVE_THREADS
   if (audio_data.audio_callback.callback)
   {
      RARCH_LOG("Starting threaded audio driver ...\n");
      if (!rarch_threaded_audio_init(&driver->audio, &driver->audio_data,
               *settings->audio.device ? settings->audio.device : NULL,
               settings->audio.out_rate, settings->audio.latency,
               driver->audio))
      {
         RARCH_ERR("Cannot open threaded audio driver ... Exiting ...\n");
         rarch_fail(1, "init_audio()");
      }
   }
   else
#endif
   {
      driver->audio_data = driver->audio->init(*settings->audio.device ?
            settings->audio.device : NULL,
            settings->audio.out_rate, settings->audio.latency);
   }

   if (!driver->audio_data)
   {
      RARCH_ERR("Failed to initialize audio driver. Will continue without audio.\n");
      driver->audio_active = false;
   }

   audio_data.use_float = false;
   if (driver->audio_active && driver->audio->use_float(driver->audio_data))
      audio_data.use_float = true;

   if (!settings->audio.sync && driver->audio_active)
   {
      event_command(EVENT_CMD_AUDIO_SET_NONBLOCKING_STATE);
      audio_data.chunk_size = audio_data.nonblock_chunk_size;
   }

   if (audio_data.in_rate <= 0.0f)
   {
      /* Should never happen. */
      RARCH_WARN("Input rate is invalid (%.3f Hz). Using output rate (%u Hz).\n",
            audio_data.in_rate, settings->audio.out_rate);
      audio_data.in_rate = settings->audio.out_rate;
   }

   audio_data.orig_src_ratio = audio_data.src_ratio =
      (double)settings->audio.out_rate / audio_data.in_rate;

   if (!rarch_resampler_realloc(&driver->resampler_data,
            &driver->resampler,
         settings->audio.resampler, audio_data.orig_src_ratio))
   {
      RARCH_ERR("Failed to initialize resampler \"%s\".\n",
            settings->audio.resampler);
      driver->audio_active = false;
   }

   rarch_assert(audio_data.data = (float*)
         malloc(max_bufsamples * sizeof(float)));

   if (!audio_data.data)
      goto error;

   audio_data.data_ptr = 0;

   rarch_assert(settings->audio.out_rate <
         audio_data.in_rate * AUDIO_MAX_RATIO);
   rarch_assert(audio_data.outsamples = (float*)
         malloc(outsamples_max * sizeof(float)));

   if (!audio_data.outsamples)
      goto error;

   audio_data.rate_control = false;
   if (!audio_data.audio_callback.callback && driver->audio_active &&
         settings->audio.rate_control)
   {
      /* Audio rate control requires write_avail
       * and buffer_size to be implemented. */
      if (driver->audio->buffer_size)
      {
         audio_data.driver_buffer_size = 
            driver->audio->buffer_size(driver->audio_data);
         audio_data.rate_control = true;
      }
      else
         RARCH_WARN("Audio rate control was desired, but driver does not support needed features.\n");
   }

   event_command(EVENT_CMD_DSP_FILTER_INIT);

   audio_data.buffer_free_samples_count = 0;

   if (driver->audio_active && !settings->audio.mute_enable &&
         audio_data.audio_callback.callback)
   {
      /* Threaded driver is initially stopped. */
      driver->audio->start(driver->audio_data);
   }

   return;

error:
   if (audio_data.conv_outsamples)
      free(audio_data.conv_outsamples);
   audio_data.conv_outsamples = NULL;
   if (audio_data.data)
      free(audio_data.data);
   audio_data.data = NULL;
   if (audio_data.rewind_buf)
      free(audio_data.rewind_buf);
   audio_data.rewind_buf = NULL;
   if (audio_data.outsamples)
      free(audio_data.outsamples);
   audio_data.outsamples = NULL;
}

bool audio_driver_mute_toggle(void)
{
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();

   if (!driver->audio_data || !driver->audio_active)
      return false;

   settings->audio.mute_enable = !settings->audio.mute_enable;

   if (settings->audio.mute_enable)
      event_command(EVENT_CMD_AUDIO_STOP);
   else if (!event_command(EVENT_CMD_AUDIO_START))
   {
      driver->audio_active = false;
      return false;
   }

   return true;
}

static int audio_driver_write_avail(void)
{
   driver_t *driver     = driver_get_ptr();
   const audio_driver_t *audio = audio_get_ptr(driver);

   return audio->write_avail(driver->audio_data);
}

/*
 * audio_driver_readjust_input_rate:
 *
 * Readjust the audio input rate.
 */
void audio_driver_readjust_input_rate(void)
{
   settings_t *settings = config_get_ptr();
   unsigned write_idx   = audio_data.buffer_free_samples_count++ &
      (AUDIO_BUFFER_FREE_SAMPLES_COUNT - 1);
   int      half_size   = audio_data.driver_buffer_size / 2;
   int      avail       = audio_driver_write_avail();
   int      delta_mid   = avail - half_size;
   double   direction   = (double)delta_mid / half_size;
   double   adjust      = 1.0 + settings->audio.rate_control_delta * direction;

#if 0
   RARCH_LOG_OUTPUT("Audio buffer is %u%% full\n",
         (unsigned)(100 - (avail * 100) / audio_data.driver_buffer_size));
#endif

   audio_data.buffer_free_samples[write_idx] = avail;
   audio_data.src_ratio = audio_data.orig_src_ratio * adjust;

#if 0
   RARCH_LOG_OUTPUT("New rate: %lf, Orig rate: %lf\n",
         audio_data.src_ratio, audio_data.orig_src_ratio);
#endif
}

bool audio_driver_alive(void)
{
   driver_t *driver     = driver_get_ptr();
   const audio_driver_t *audio = audio_get_ptr(driver);

   return audio->alive(driver->audio_data);
}


bool audio_driver_start(void)
{
   driver_t *driver      = driver_get_ptr();
   const audio_driver_t *audio = audio_get_ptr(driver);

   return audio->start(driver->audio_data);
}

bool audio_driver_stop(void)
{
   driver_t *driver      = driver_get_ptr();
   const audio_driver_t *audio = audio_get_ptr(driver);

   return audio->stop(driver->audio_data);
}

void audio_driver_set_nonblock_state(bool toggle)
{
   driver_t *driver     = driver_get_ptr();
   const audio_driver_t *audio = audio_get_ptr(driver);

   audio->set_nonblock_state(driver->audio_data, toggle);
}

void audio_driver_set_nonblocking_state(bool enable)
{
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();
   if (driver->audio_active && driver->audio_data)
      audio_driver_set_nonblock_state(settings->audio.sync ? enable : true);

   audio_data.chunk_size = enable ? audio_data.nonblock_chunk_size : 
      audio_data.block_chunk_size;
}

ssize_t audio_driver_write(const void *buf, size_t size)
{
   driver_t *driver      = driver_get_ptr();
   const audio_driver_t *audio = audio_get_ptr(driver);

   return audio->write(driver->audio_data, buf, size);
}

/**
 * audio_driver_flush:
 * @data                 : pointer to audio buffer.
 * @right                : amount of samples to write.
 *
 * Writes audio samples to audio driver. Will first
 * perform DSP processing (if enabled) and resampling.
 *
 * Returns: true (1) if audio samples were written to the audio
 * driver, false (0) in case of an error.
 **/
bool audio_driver_flush(const int16_t *data, size_t samples)
{
   const void *output_data        = NULL;
   unsigned output_frames         = 0;
   size_t   output_size           = sizeof(float);
   struct resampler_data src_data = {0};
   struct rarch_dsp_data dsp_data = {0};
   runloop_t *runloop             = rarch_main_get_ptr();
   driver_t  *driver              = driver_get_ptr();
   settings_t *settings           = config_get_ptr();

   if (driver->recording_data)
   {
      struct ffemu_audio_data ffemu_data = {0};
      ffemu_data.data                    = data;
      ffemu_data.frames                  = samples / 2;

      if (driver->recording && driver->recording->push_audio)
         driver->recording->push_audio(driver->recording_data, &ffemu_data);
   }

   if (runloop->is_paused || settings->audio.mute_enable)
      return true;
   if (!driver->audio_active || !audio_data.data)
      return false;

   RARCH_PERFORMANCE_INIT(audio_convert_s16);
   RARCH_PERFORMANCE_START(audio_convert_s16);
   audio_convert_s16_to_float(audio_data.data, data, samples,
         audio_data.volume_gain);
   RARCH_PERFORMANCE_STOP(audio_convert_s16);

   src_data.data_in               = audio_data.data;
   src_data.input_frames          = samples >> 1;

   dsp_data.input                 = audio_data.data;
   dsp_data.input_frames          = samples >> 1;

   if (audio_data.dsp)
   {
      RARCH_PERFORMANCE_INIT(audio_dsp);
      RARCH_PERFORMANCE_START(audio_dsp);
      rarch_dsp_filter_process(audio_data.dsp, &dsp_data);
      RARCH_PERFORMANCE_STOP(audio_dsp);

      if (dsp_data.output)
      {
         src_data.data_in      = dsp_data.output;
         src_data.input_frames = dsp_data.output_frames;
      }
   }

   src_data.data_out = audio_data.outsamples;

   if (audio_data.rate_control)
      audio_driver_readjust_input_rate();

   src_data.ratio = audio_data.src_ratio;
   if (runloop->is_slowmotion)
      src_data.ratio *= settings->slowmotion_ratio;

   RARCH_PERFORMANCE_INIT(resampler_proc);
   RARCH_PERFORMANCE_START(resampler_proc);
   rarch_resampler_process(driver->resampler,
         driver->resampler_data, &src_data);
   RARCH_PERFORMANCE_STOP(resampler_proc);

   output_data   = audio_data.outsamples;
   output_frames = src_data.output_frames;

   if (!audio_data.use_float)
   {
      RARCH_PERFORMANCE_INIT(audio_convert_float);
      RARCH_PERFORMANCE_START(audio_convert_float);
      audio_convert_float_to_s16(audio_data.conv_outsamples,
            (const float*)output_data, output_frames * 2);
      RARCH_PERFORMANCE_STOP(audio_convert_float);

      output_data = audio_data.conv_outsamples;
      output_size = sizeof(int16_t);
   }

   if (audio_driver_write(output_data, output_frames * output_size * 2) < 0)
   {
      driver->audio_active = false;
      return false;
   }

   return true;
}

/**
 * audio_driver_sample:
 * @left                 : value of the left audio channel.
 * @right                : value of the right audio channel.
 *
 * Audio sample render callback function.
 **/
void audio_driver_sample(int16_t left, int16_t right)
{
   audio_data.conv_outsamples[audio_data.data_ptr++] = left;
   audio_data.conv_outsamples[audio_data.data_ptr++] = right;

   if (audio_data.data_ptr < audio_data.chunk_size)
      return;

   audio_driver_flush(audio_data.conv_outsamples, audio_data.data_ptr);

   audio_data.data_ptr = 0;
}

/**
 * audio_driver_sample_batch:
 * @data                 : pointer to audio buffer.
 * @frames               : amount of audio frames to push.
 *
 * Batched audio sample render callback function.
 *
 * Returns: amount of frames sampled. Will be equal to @frames
 * unless @frames exceeds (AUDIO_CHUNK_SIZE_NONBLOCKING / 2).
 **/
size_t audio_driver_sample_batch(const int16_t *data, size_t frames)
{
   if (frames > (AUDIO_CHUNK_SIZE_NONBLOCKING >> 1))
      frames = AUDIO_CHUNK_SIZE_NONBLOCKING >> 1;

   audio_driver_flush(data, frames << 1);

   return frames;
}

/**
 * audio_driver_sample_rewind:
 * @left                 : value of the left audio channel.
 * @right                : value of the right audio channel.
 *
 * Audio sample render callback function (rewind version). This callback
 * function will be used instead of audio_driver_sample when rewinding is activated.
 **/
void audio_driver_sample_rewind(int16_t left, int16_t right)
{
   audio_data.rewind_buf[--audio_data.rewind_ptr] = right;
   audio_data.rewind_buf[--audio_data.rewind_ptr] = left;
}

/**
 * audio_driver_sample_batch_rewind:
 * @data                 : pointer to audio buffer.
 * @frames               : amount of audio frames to push.
 *
 * Batched audio sample render callback function (rewind version). This callback
 * function will be used instead of audio_driver_sample_batch when rewinding is activated.
 *
 * Returns: amount of frames sampled. Will be equal to @frames
 * unless @frames exceeds (AUDIO_CHUNK_SIZE_NONBLOCKING / 2).
 **/
size_t audio_driver_sample_batch_rewind(const int16_t *data, size_t frames)
{
   size_t i;
   size_t samples   = frames << 1;

   for (i = 0; i < samples; i++)
      audio_data.rewind_buf[--audio_data.rewind_ptr] = data[i];

   return frames;
}

void audio_driver_set_volume_gain(float gain)
{
   audio_data.volume_gain = gain;
}

void audio_driver_dsp_filter_free(void)
{
   if (audio_data.dsp)
      rarch_dsp_filter_free(audio_data.dsp);
   audio_data.dsp = NULL;
}

void audio_driver_dsp_filter_init(const char *device)
{
   audio_data.dsp = rarch_dsp_filter_new(device, audio_data.in_rate);
   if (!audio_data.dsp)
      RARCH_ERR("[DSP]: Failed to initialize DSP filter \"%s\".\n", device);
}

void audio_driver_setup_rewind(void)
{
   unsigned i;

   /* Push audio ready to be played. */
   audio_data.rewind_ptr = audio_data.rewind_size;

   for (i = 0; i < audio_data.data_ptr; i += 2)
   {
      audio_data.rewind_buf[--audio_data.rewind_ptr] =
         audio_data.conv_outsamples[i + 1];

      audio_data.rewind_buf[--audio_data.rewind_ptr] =
         audio_data.conv_outsamples[i + 0];
   }

   audio_data.data_ptr = 0;
}

void audio_driver_frame_is_reverse(void)
{
   /* We just rewound. Flush rewind audio buffer. */
   audio_driver_flush(audio_data.rewind_buf + audio_data.rewind_ptr,
         audio_data.rewind_size - audio_data.rewind_ptr);
}

void audio_monitor_adjust_system_rates(void)
{
   float timing_skew;
   settings_t *settings = config_get_ptr();
   struct retro_system_av_info *av_info = 
      video_viewport_get_system_av_info();
   const struct retro_system_timing *info = 
      av_info ? (const struct retro_system_timing*)&av_info->timing : NULL;

   if (info->sample_rate <= 0.0)
      return;

   timing_skew                 = fabs(1.0f - info->fps / 
                                 settings->video.refresh_rate);
   audio_data.in_rate = info->sample_rate;

   if (timing_skew <= settings->audio.max_timing_skew)
      audio_data.in_rate *= (settings->video.refresh_rate / info->fps);

   RARCH_LOG("Set audio input rate to: %.2f Hz.\n",
         audio_data.in_rate);
}

/**
 * audio_monitor_set_refresh_rate:
 *
 * Sets audio monitor refresh rate to new value.
 **/
void audio_monitor_set_refresh_rate(void)
{
   settings_t *settings = config_get_ptr();

   double new_src_ratio = (double)settings->audio.out_rate / 
                           audio_data.in_rate;

   audio_data.orig_src_ratio = new_src_ratio;
   audio_data.src_ratio      = new_src_ratio;
}

void audio_driver_set_buffer_size(size_t bufsize)
{
   audio_data.driver_buffer_size = bufsize;
}

void audio_driver_set_callback(const void *data)
{
   const struct retro_audio_callback *cb = 
      (const struct retro_audio_callback*)data;

   if (cb)
      audio_data.audio_callback = *cb;
}

bool audio_driver_has_callback(void)
{
   return audio_data.audio_callback.callback;
}

void audio_driver_callback(void)
{
   if (audio_driver_has_callback())
      audio_data.audio_callback.callback();
}

void audio_driver_callback_set_state(bool state)
{
   if (audio_driver_has_callback())
      audio_data.audio_callback.set_state(state);
}
