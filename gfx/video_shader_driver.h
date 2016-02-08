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

#ifndef VIDEO_SHADER_DRIVER_H__
#define VIDEO_SHADER_DRIVER_H__

#include <boolean.h>

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include "video_context_driver.h"
#include <gfx/math/matrix_4x4.h>

typedef struct shader_backend
{
   bool (*init)(void *data, const char *path);
   void (*deinit)(void);
   void (*set_params)(void *data, unsigned width, unsigned height, 
         unsigned tex_width, unsigned tex_height, 
         unsigned out_width, unsigned out_height,
         unsigned frame_counter,
         const void *info, 
         const void *prev_info,
         const void *fbo_info, unsigned fbo_info_cnt);

   void (*use)(void *data, unsigned index);
   unsigned (*num_shaders)(void);
   bool (*filter_type)(unsigned index, bool *smooth);
   enum gfx_wrap_type (*wrap_type)(unsigned index);
   void (*shader_scale)(unsigned index, struct gfx_fbo_scale *scale);
   bool (*set_coords)(const void *data);
   bool (*set_mvp)(void *data, const math_matrix_4x4 *mat);
   unsigned (*get_prev_textures)(void);
   bool (*mipmap_input)(unsigned index);

   struct video_shader *(*get_current_shader)(void);

   enum rarch_shader_type type;

   /* Human readable string. */
   const char *ident;
} shader_backend_t;

extern const shader_backend_t gl_glsl_backend;
extern const shader_backend_t hlsl_backend;
extern const shader_backend_t gl_cg_backend;
extern const shader_backend_t shader_null_backend;

#if defined(_XBOX360)
#define DEFAULT_SHADER_TYPE RARCH_SHADER_HLSL
#elif defined(__PSL1GHT__)
#define DEFAULT_SHADER_TYPE RARCH_SHADER_GLSL
#elif defined(__CELLOS_LV2__)
#define DEFAULT_SHADER_TYPE RARCH_SHADER_CG
#elif defined(HAVE_OPENGLES2)
#define DEFAULT_SHADER_TYPE RARCH_SHADER_GLSL
#else
#define DEFAULT_SHADER_TYPE RARCH_SHADER_NONE
#endif

#if defined(HAVE_CG) || defined(HAVE_HLSL) || defined(HAVE_GLSL)

#ifndef HAVE_SHADER_MANAGER
#define HAVE_SHADER_MANAGER
#endif

#include "video_shader_parse.h"

#define GL_SHADER_STOCK_BLEND (GFX_MAX_SHADERS - 1)

#endif

/**
 * shader_ctx_find_driver:
 * @ident                   : Identifier of shader context driver to find.
 *
 * Finds shader context driver and initializes.
 *
 * Returns: shader context driver if found, otherwise NULL.
 **/
const shader_backend_t *shader_ctx_find_driver(const char *ident);

/**
 * shader_ctx_init_first:
 *
 * Finds first suitable shader context driver and initializes.
 *
 * Returns: shader context driver if found, otherwise NULL.
 **/
const shader_backend_t *shader_ctx_init_first(void);

struct video_shader *video_shader_driver_get_current_shader(void);

#endif
