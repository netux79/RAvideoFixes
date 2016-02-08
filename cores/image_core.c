#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <boolean.h>

#define STB_IMAGE_IMPLEMENTATION

#ifdef RARCH_INTERNAL
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_SUPPORT_ZLIB
#endif

#include "stb_image.h"

#ifdef RARCH_INTERNAL
#include "../libretro.h"
#define IMAGE_CORE_PREFIX(s) libretro_imageviewer_##s
#else
#include "libretro.h"
#define IMAGE_CORE_PREFIX(s) s
#endif

static retro_log_printf_t IMAGE_CORE_PREFIX(log_cb);
static retro_video_refresh_t IMAGE_CORE_PREFIX(video_cb);
static retro_input_poll_t IMAGE_CORE_PREFIX(input_poll_cb);
static retro_input_state_t IMAGE_CORE_PREFIX(input_state_cb);
static retro_audio_sample_batch_t IMAGE_CORE_PREFIX(audio_batch_cb);
static retro_environment_t IMAGE_CORE_PREFIX(environ_cb);

static uint32_t* image_buffer;
static int       image_width;
static int       image_height;
static bool      image_uploaded;

#if 0
#define DUPE_TEST
#endif

void IMAGE_CORE_PREFIX(retro_get_system_info)(struct retro_system_info *info)
{
   info->library_name     = "image display";
   info->library_version  = "v0.1";
   info->need_fullpath    = true;
   info->block_extract    = false;
#ifdef RARCH_INTERNAL
   info->valid_extensions = "jpg|jpeg|png|bmp|tga";
#else
   info->valid_extensions = "jpg|jpeg|png|bmp|psd|tga|gif|hdr|pic|ppm|pgm";
#endif
}

void IMAGE_CORE_PREFIX(retro_get_system_av_info)(struct retro_system_av_info *info)
{
   info->geometry.base_width   = image_width;
   info->geometry.base_height  = image_height;
   info->geometry.max_width    = image_width;
   info->geometry.max_height   = image_height;
   info->geometry.aspect_ratio = 0;
   info->timing.fps            = 60.0;
   info->timing.sample_rate    = 44100.0;
}

void IMAGE_CORE_PREFIX(retro_init)(void)
{
   struct retro_log_callback log;

   if (IMAGE_CORE_PREFIX(environ_cb)(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      IMAGE_CORE_PREFIX(log_cb) = log.log;
   else
      IMAGE_CORE_PREFIX(log_cb) = NULL;

   image_buffer = NULL;
   image_width  = 0;
   image_height = 0;

}

void IMAGE_CORE_PREFIX(retro_deinit)(void)
{
   if (image_buffer)
      free(image_buffer);
   image_buffer = NULL;
   image_width  = 0;
   image_height = 0;
}

void IMAGE_CORE_PREFIX(retro_set_environment)(retro_environment_t cb)
{
   static const struct retro_variable vars[] = {
      { NULL, NULL },
   };

   IMAGE_CORE_PREFIX(environ_cb) = cb;

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

void IMAGE_CORE_PREFIX(retro_set_video_refresh)(retro_video_refresh_t cb)
{
   IMAGE_CORE_PREFIX(video_cb) = cb;
}

void IMAGE_CORE_PREFIX(retro_set_audio_sample)(retro_audio_sample_t unused)
{
}

void IMAGE_CORE_PREFIX(retro_set_audio_sample_batch)(retro_audio_sample_batch_t cb)
{
   IMAGE_CORE_PREFIX(audio_batch_cb) = cb;
}

void IMAGE_CORE_PREFIX(retro_set_input_poll)(retro_input_poll_t cb)
{
   IMAGE_CORE_PREFIX(input_poll_cb) = cb;
}

void IMAGE_CORE_PREFIX(retro_set_input_state)(retro_input_state_t cb)
{
   IMAGE_CORE_PREFIX(input_state_cb) = cb;
}

void IMAGE_CORE_PREFIX(retro_set_controller_port_device)(unsigned a, unsigned b)
{
}

void IMAGE_CORE_PREFIX(retro_reset)(void)
{
   image_uploaded = false;
}

size_t IMAGE_CORE_PREFIX(retro_serialize_size)(void)
{
   return 0;
}

bool IMAGE_CORE_PREFIX(retro_serialize)(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool IMAGE_CORE_PREFIX(retro_unserialize)(const void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void IMAGE_CORE_PREFIX(retro_cheat_reset)(void)
{
}

void IMAGE_CORE_PREFIX(retro_cheat_set)(unsigned a, bool b, const char * c)
{
}

bool IMAGE_CORE_PREFIX(retro_load_game)(const struct retro_game_info *info)
{
   int comp;
   uint32_t *buf, *end;
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  
   image_buffer = (uint32_t*)stbi_load (info->path,&image_width, &image_height, &comp, 4);
   /* RGBA > XRGB8888 */
   buf = &image_buffer[0];
   end = buf + (image_width*image_height*sizeof(uint32_t))/4;

   while(buf < end)
   {
    uint32_t pixel = *buf;
    *buf = (pixel & 0xff00ff00) | ((pixel << 16) & 0x00ff0000) | ((pixel >> 16) & 0xff);
    buf++;
   }
  
   if (!IMAGE_CORE_PREFIX(environ_cb)(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (IMAGE_CORE_PREFIX(log_cb))
         IMAGE_CORE_PREFIX(log_cb)(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
      return false;
   }


   return true;
}

bool IMAGE_CORE_PREFIX(retro_load_game_special)(unsigned a, const struct retro_game_info *b, size_t c)
{
   return false;
}

void IMAGE_CORE_PREFIX(retro_unload_game)(void)
{
   if (image_buffer)
      free(image_buffer);
   image_buffer = NULL;
   image_width  = 0;
   image_height = 0;
}

unsigned IMAGE_CORE_PREFIX(retro_get_region)(void)
{
   return RETRO_REGION_NTSC;
}

void *IMAGE_CORE_PREFIX(retro_get_memory_data)(unsigned id)
{
   return NULL;
}

size_t IMAGE_CORE_PREFIX(retro_get_memory_size)(unsigned id)
{
   return 0;
}

void IMAGE_CORE_PREFIX(retro_run)(void)
{
   IMAGE_CORE_PREFIX(input_poll_cb)();

#ifdef DUPE_TEST
   if (!image_uploaded)
   {
      IMAGE_CORE_PREFIX(video_cb)(image_buffer, image_width, image_height, image_width * sizeof(uint32_t));
      image_uploaded = true;
   }
   else
      IMAGE_CORE_PREFIX(video_cb)(NULL, image_width, image_height, image_width * sizeof(uint32_t));
#else
   IMAGE_CORE_PREFIX(video_cb)(image_buffer, image_width, image_height, image_width * sizeof(uint32_t));
#endif
}

unsigned IMAGE_CORE_PREFIX(retro_api_version)(void)
{
   return RETRO_API_VERSION;
}
