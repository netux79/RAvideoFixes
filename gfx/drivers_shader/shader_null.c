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

#include <boolean.h>
#include <string.h>
#include "../../general.h"
#include <compat/strl.h>
#include <compat/posix_string.h>
#include "../video_state_tracker.h"
#include "../../dynamic.h"
#include <gfx/math/matrix_4x4.h>

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#ifdef HAVE_OPENGL
#include "../drivers/gl_common.h"
#endif

#include "../video_shader_driver.h"
#include <stdlib.h>

static void shader_null_deinit(void) { }
static bool shader_null_init(void *data, const char *path) { return true; }

static void shader_null_set_params(void *data, unsigned width, unsigned height, 
      unsigned tex_width, unsigned tex_height, 
      unsigned out_width, unsigned out_height,
      unsigned frame_count,
      const void *info, 
      const void *prev_info, 
      const void *fbo_info, unsigned fbo_info_cnt)
{
}

static bool shader_null_set_mvp(void *data, const math_matrix_4x4 *mat)
{
#ifdef HAVE_OPENGL
#ifndef NO_GL_FF_MATRIX
   gl_t *gl = (gl_t*)video_driver_get_ptr(NULL);
   if (gl)
   {
      gl_ff_matrix(mat);
   }
#endif
#endif
   return false;
}

static bool shader_null_set_coords(const void *data)
{
#ifdef HAVE_OPENGL
#ifndef NO_GL_FF_VERTEX
   gl_t *gl = (gl_t*)video_driver_get_ptr(NULL);
   if (gl)
   {
      const struct gl_coords *coords = (const struct gl_coords*)data;
      gl_ff_vertex(coords);
   }
#endif
#endif
   return false;
}

static void shader_null_use(void *data, unsigned idx)
{
   (void)data;
   (void)idx;
}

static unsigned shader_null_num(void)
{
   return 0;
}

static bool shader_null_filter_type(unsigned idx, bool *smooth)
{
   (void)idx;
   (void)smooth;
   return false;
}

static enum gfx_wrap_type shader_null_wrap_type(unsigned idx)
{
   (void)idx;
   return RARCH_WRAP_BORDER;
}

static void shader_null_shader_scale(unsigned idx,
      struct gfx_fbo_scale *scale)
{
   (void)idx;
   (void)scale;
}

static unsigned shader_null_get_prev_textures(void)
{
   return 0;
}

static bool shader_null_mipmap_input(unsigned idx)
{
   (void)idx;
   return false;
}

static struct video_shader *shader_null_get_current_shader(void)
{
   return NULL;
}

const shader_backend_t shader_null_backend = {
   shader_null_init,
   shader_null_deinit,
   shader_null_set_params,
   shader_null_use,
   shader_null_num,
   shader_null_filter_type,
   shader_null_wrap_type,
   shader_null_shader_scale,
   shader_null_set_coords,
   shader_null_set_mvp,
   shader_null_get_prev_textures,
   shader_null_mipmap_input,
   shader_null_get_current_shader,

   RARCH_SHADER_NONE,
   "nullshader"
};
