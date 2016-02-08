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

#ifdef _MSC_VER
#pragma comment(lib, "cg")
#pragma comment(lib, "cggl")
#endif

#include <stdint.h>
#include "../video_shader_driver.h"

#include <Cg/cg.h>

#ifdef HAVE_OPENGL
#include "../drivers/gl_common.h"
#include <Cg/cgGL.h>

#endif

#include <string.h>
#include <compat/strl.h>
#include <compat/posix_string.h>
#include <file/config_file.h>
#include <file/file_path.h>
#include <rhash.h>

#include "../../dynamic.h"

#include "../video_state_tracker.h"

#if 0
#define RARCH_CG_DEBUG
#endif

/* Used when we call deactivate() since just unbinding 
 * the program didn't seem to work... */
static const char *stock_cg_program =
      "struct input"
      "{"
      "  float2 tex_coord;"
      "  float4 color;"
      "  float4 vertex_coord;"
      "  uniform float4x4 mvp_matrix;"
      "  uniform sampler2D texture;"
      "};"
      "struct vertex_data"
      "{"
      "  float2 tex;"
      "  float4 color;"
      "};"
      "void main_vertex"
      "("
      "	out float4 oPosition : POSITION,"
      "  input IN,"
      "  out vertex_data vert"
      ")"
      "{"
      "	oPosition = mul(IN.mvp_matrix, IN.vertex_coord);"
      "  vert = vertex_data(IN.tex_coord, IN.color);"
      "}"
      ""
      "float4 main_fragment(input IN, vertex_data vert, uniform sampler2D s0 : TEXUNIT0) : COLOR"
      "{"
      "  return vert.color * tex2D(s0, vert.tex);"
      "}";

#ifdef RARCH_CG_DEBUG
static void cg_error_handler(CGcontext ctx, CGerror error, void *data)
{
   (void)ctx;
   (void)data;

   switch (error)
   {
      case CG_INVALID_PARAM_HANDLE_ERROR:
         RARCH_ERR("CG: Invalid param handle.\n");
         break;

      case CG_INVALID_PARAMETER_ERROR:
         RARCH_ERR("CG: Invalid parameter.\n");
         break;

      default:
         break;
   }

   RARCH_ERR("CG error: \"%s\"\n", cgGetErrorString(error));
}
#endif


struct cg_fbo_params
{
   CGparameter vid_size_f;
   CGparameter tex_size_f;
   CGparameter vid_size_v;
   CGparameter tex_size_v;
   CGparameter tex;
   CGparameter coord;
};

#define MAX_VARIABLES 64
#define PREV_TEXTURES (GFX_MAX_TEXTURES - 1)

struct cg_program
{
   CGprogram vprg;
   CGprogram fprg;

   CGparameter tex;
   CGparameter lut_tex;
   CGparameter color;
   CGparameter vertex;

   CGparameter vid_size_f;
   CGparameter tex_size_f;
   CGparameter out_size_f;
   CGparameter frame_cnt_f;
   CGparameter frame_dir_f;
   CGparameter vid_size_v;
   CGparameter tex_size_v;
   CGparameter out_size_v;
   CGparameter frame_cnt_v;
   CGparameter frame_dir_v;
   CGparameter mvp;

   struct cg_fbo_params fbo[GFX_MAX_SHADERS];
   struct cg_fbo_params orig;
   struct cg_fbo_params prev[PREV_TEXTURES];
};

typedef struct cg_shader_data
{
   struct cg_program prg[GFX_MAX_SHADERS];
   unsigned active_idx;
   unsigned cg_attrib_idx;
   CGprofile cgVProf;
   CGprofile cgFProf;
   struct video_shader *cg_shader;
   state_tracker_t *state_tracker;
   GLuint lut_textures[GFX_MAX_TEXTURES];
   CGparameter cg_attribs[PREV_TEXTURES + 1 + 4 + GFX_MAX_SHADERS];
   char cg_alias_define[GFX_MAX_SHADERS][128];
   CGcontext cgCtx;
} cg_shader_data_t;

static void gl_cg_reset_attrib(cg_shader_data_t *cg)
{
   unsigned i;
   if (!cg)
      return;

   /* Add sanity check that we did not overflow. */
   rarch_assert(cg->cg_attrib_idx <= ARRAY_SIZE(cg->cg_attribs));

   for (i = 0; i < cg->cg_attrib_idx; i++)
      cgGLDisableClientState(cg->cg_attribs[i]);
   cg->cg_attrib_idx = 0;
}

static bool gl_cg_set_mvp(void *data, const math_matrix_4x4 *mat)
{
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;

   (void)data;

   if (cg && cg->prg[cg->active_idx].mvp)
   {
      cgGLSetMatrixParameterfc(cg->prg[cg->active_idx].mvp, mat->data);
      return true;
   }

   gl_ff_matrix(mat);
   return false;
}

#define SET_COORD(cg, name, coords_name, len) do { \
   if (cg->prg[cg->active_idx].name) \
   { \
      cgGLSetParameterPointer(cg->prg[cg->active_idx].name, len, GL_FLOAT, 0, coords->coords_name); \
      cgGLEnableClientState(cg->prg[cg->active_idx].name); \
      cg->cg_attribs[cg->cg_attrib_idx++] = cg->prg[cg->active_idx].name; \
   } \
} while(0)

static bool gl_cg_set_coords(const void *data)
{
   const struct gl_coords *coords = (const struct gl_coords*)data;
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;

   if (!cg || !coords)
      goto fallback;

   SET_COORD(cg, vertex, vertex, 2);
   SET_COORD(cg, tex, tex_coord, 2);
   SET_COORD(cg, lut_tex, lut_tex_coord, 2);
   SET_COORD(cg, color, color, 4);

   return true;
fallback:
   gl_ff_vertex(coords);
   return false;
}

#define set_param_2f(param, x, y) \
   if (param) cgGLSetParameter2f(param, x, y)
#define set_param_1f(param, x) \
   if (param) cgGLSetParameter1f(param, x)

static void gl_cg_set_params(void *data, unsigned width, unsigned height, 
      unsigned tex_width, unsigned tex_height,
      unsigned out_width, unsigned out_height,
      unsigned frame_count,
      const void *_info,
      const void *_prev_info,
      const void *_fbo_info,
      unsigned fbo_info_cnt)
{
   unsigned i;
   CGparameter param;
   const struct gl_tex_info *info = (const struct gl_tex_info*)_info;
   const struct gl_tex_info *prev_info = (const struct gl_tex_info*)_prev_info;
   const struct gl_tex_info *fbo_info = (const struct gl_tex_info*)_fbo_info;
   driver_t *driver = driver_get_ptr();
   global_t *global = global_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;

   (void)data;
   if (!cg || (cg->active_idx == 0) ||
         (cg->active_idx == GL_SHADER_STOCK_BLEND))
      return;

   /* Set frame. */
   set_param_2f(cg->prg[cg->active_idx].vid_size_f, width, height);
   set_param_2f(cg->prg[cg->active_idx].tex_size_f, tex_width, tex_height);
   set_param_2f(cg->prg[cg->active_idx].out_size_f, out_width, out_height);
   set_param_1f(cg->prg[cg->active_idx].frame_dir_f,
         global->rewind.frame_is_reverse ? -1.0 : 1.0);

   set_param_2f(cg->prg[cg->active_idx].vid_size_v, width, height);
   set_param_2f(cg->prg[cg->active_idx].tex_size_v, tex_width, tex_height);
   set_param_2f(cg->prg[cg->active_idx].out_size_v, out_width, out_height);
   set_param_1f(cg->prg[cg->active_idx].frame_dir_v,
         global->rewind.frame_is_reverse ? -1.0 : 1.0);

   if (cg->prg[cg->active_idx].frame_cnt_f || cg->prg[cg->active_idx].frame_cnt_v)
   {
      unsigned modulo = cg->cg_shader->pass[cg->active_idx - 1].frame_count_mod;
      if (modulo)
         frame_count %= modulo;

      set_param_1f(cg->prg[cg->active_idx].frame_cnt_f, (float)frame_count);
      set_param_1f(cg->prg[cg->active_idx].frame_cnt_v, (float)frame_count);
   }

   /* Set orig texture. */
   param = cg->prg[cg->active_idx].orig.tex;
   if (param)
   {
      cgGLSetTextureParameter(param, info->tex);
      cgGLEnableTextureParameter(param);
   }

   set_param_2f(cg->prg[cg->active_idx].orig.vid_size_v,
         info->input_size[0], info->input_size[1]);
   set_param_2f(cg->prg[cg->active_idx].orig.vid_size_f,
         info->input_size[0], info->input_size[1]);
   set_param_2f(cg->prg[cg->active_idx].orig.tex_size_v,
         info->tex_size[0],   info->tex_size[1]);
   set_param_2f(cg->prg[cg->active_idx].orig.tex_size_f,
         info->tex_size[0],   info->tex_size[1]);

   if (cg->prg[cg->active_idx].orig.coord)
   {
      cgGLSetParameterPointer(cg->prg[cg->active_idx].orig.coord, 2,
            GL_FLOAT, 0, info->coord);
      cgGLEnableClientState(cg->prg[cg->active_idx].orig.coord);
      cg->cg_attribs[cg->cg_attrib_idx++] = cg->prg[cg->active_idx].orig.coord;
   }

   /* Set prev textures. */
   for (i = 0; i < PREV_TEXTURES; i++)
   {
      param = cg->prg[cg->active_idx].prev[i].tex;
      if (param)
      {
         cgGLSetTextureParameter(param, prev_info[i].tex);
         cgGLEnableTextureParameter(param);
      }

      set_param_2f(cg->prg[cg->active_idx].prev[i].vid_size_v,
            prev_info[i].input_size[0], prev_info[i].input_size[1]);
      set_param_2f(cg->prg[cg->active_idx].prev[i].vid_size_f,
            prev_info[i].input_size[0], prev_info[i].input_size[1]);
      set_param_2f(cg->prg[cg->active_idx].prev[i].tex_size_v,
            prev_info[i].tex_size[0],   prev_info[i].tex_size[1]);
      set_param_2f(cg->prg[cg->active_idx].prev[i].tex_size_f,
            prev_info[i].tex_size[0],   prev_info[i].tex_size[1]);

      if (cg->prg[cg->active_idx].prev[i].coord)
      {
         cgGLSetParameterPointer(cg->prg[cg->active_idx].prev[i].coord, 
               2, GL_FLOAT, 0, prev_info[i].coord);
         cgGLEnableClientState(cg->prg[cg->active_idx].prev[i].coord);
         cg->cg_attribs[cg->cg_attrib_idx++] = cg->prg[cg->active_idx].prev[i].coord;
      }
   }

   /* Set lookup textures. */
   for (i = 0; i < cg->cg_shader->luts; i++)
   {
      CGparameter vparam;
      CGparameter fparam = cgGetNamedParameter(
            cg->prg[cg->active_idx].fprg, cg->cg_shader->lut[i].id);

      if (fparam)
      {
         cgGLSetTextureParameter(fparam, cg->lut_textures[i]);
         cgGLEnableTextureParameter(fparam);
      }

      vparam = cgGetNamedParameter(cg->prg[cg->active_idx].vprg,
		  cg->cg_shader->lut[i].id);

      if (vparam)
      {
         cgGLSetTextureParameter(vparam, cg->lut_textures[i]);
         cgGLEnableTextureParameter(vparam);
      }
   }

   /* Set FBO textures. */
   if (cg->active_idx)
   {
      for (i = 0; i < fbo_info_cnt; i++)
      {
         if (cg->prg[cg->active_idx].fbo[i].tex)
         {
            cgGLSetTextureParameter(
                  cg->prg[cg->active_idx].fbo[i].tex, fbo_info[i].tex);
            cgGLEnableTextureParameter(cg->prg[cg->active_idx].fbo[i].tex);
         }

         set_param_2f(cg->prg[cg->active_idx].fbo[i].vid_size_v,
               fbo_info[i].input_size[0], fbo_info[i].input_size[1]);
         set_param_2f(cg->prg[cg->active_idx].fbo[i].vid_size_f,
               fbo_info[i].input_size[0], fbo_info[i].input_size[1]);

         set_param_2f(cg->prg[cg->active_idx].fbo[i].tex_size_v,
               fbo_info[i].tex_size[0], fbo_info[i].tex_size[1]);
         set_param_2f(cg->prg[cg->active_idx].fbo[i].tex_size_f,
               fbo_info[i].tex_size[0], fbo_info[i].tex_size[1]);

         if (cg->prg[cg->active_idx].fbo[i].coord)
         {
            cgGLSetParameterPointer(cg->prg[cg->active_idx].fbo[i].coord,
                  2, GL_FLOAT, 0, fbo_info[i].coord);
            cgGLEnableClientState(cg->prg[cg->active_idx].fbo[i].coord);
            cg->cg_attribs[cg->cg_attrib_idx++] = cg->prg[cg->active_idx].fbo[i].coord;
         }
      }
   }

   /* #pragma parameters. */
   for (i = 0; i < cg->cg_shader->num_parameters; i++)
   {
      CGparameter param_v = cgGetNamedParameter(
            cg->prg[cg->active_idx].vprg, cg->cg_shader->parameters[i].id);
      CGparameter param_f = cgGetNamedParameter(
            cg->prg[cg->active_idx].fprg, cg->cg_shader->parameters[i].id);
      set_param_1f(param_v, cg->cg_shader->parameters[i].current);
      set_param_1f(param_f, cg->cg_shader->parameters[i].current);
   }

   /* Set state parameters. */
   if (cg->state_tracker)
   {
      /* Only query uniforms in first pass. */
      static struct state_tracker_uniform tracker_info[MAX_VARIABLES];
      static unsigned cnt = 0;

      if (cg->active_idx == 1)
         cnt = state_tracker_get_uniform(cg->state_tracker, tracker_info,
               MAX_VARIABLES, frame_count);

      for (i = 0; i < cnt; i++)
      {
         CGparameter param_v = cgGetNamedParameter(
               cg->prg[cg->active_idx].vprg, tracker_info[i].id);
         CGparameter param_f = cgGetNamedParameter(
               cg->prg[cg->active_idx].fprg, tracker_info[i].id);
         set_param_1f(param_v, tracker_info[i].value);
         set_param_1f(param_f, tracker_info[i].value);
      }
   }
}

static void gl_cg_deinit_progs(cg_shader_data_t *cg)
{
   unsigned i;

   if (!cg)
      return;

   RARCH_LOG("CG: Destroying programs.\n");
   cgGLUnbindProgram(cg->cgFProf);
   cgGLUnbindProgram(cg->cgVProf);

   /* Programs may alias [0]. */
   for (i = 1; i < GFX_MAX_SHADERS; i++)
   {
      if (cg->prg[i].fprg && cg->prg[i].fprg != cg->prg[0].fprg)
         cgDestroyProgram(cg->prg[i].fprg);
      if (cg->prg[i].vprg && cg->prg[i].vprg != cg->prg[0].vprg)
         cgDestroyProgram(cg->prg[i].vprg);
   }

   if (cg->prg[0].fprg)
      cgDestroyProgram(cg->prg[0].fprg);
   if (cg->prg[0].vprg)
      cgDestroyProgram(cg->prg[0].vprg);

   memset(cg->prg, 0, sizeof(cg->prg));
}

static void gl_cg_destroy_resources(cg_shader_data_t *cg)
{
   if (!cg)
      return;

   gl_cg_reset_attrib(cg);

   gl_cg_deinit_progs(cg);

   if (cg->cg_shader && cg->cg_shader->luts)
   {
      glDeleteTextures(cg->cg_shader->luts, cg->lut_textures);
      memset(cg->lut_textures, 0, sizeof(cg->lut_textures));
   }

   if (cg->state_tracker)
   {
      state_tracker_free(cg->state_tracker);
      cg->state_tracker = NULL;
   }

   free(cg->cg_shader);
   cg->cg_shader = NULL;
}

/* Final deinit. */
static void gl_cg_deinit_context_state(cg_shader_data_t *cg)
{
   if (cg->cgCtx)
   {
      RARCH_LOG("CG: Destroying context.\n");
      cgDestroyContext(cg->cgCtx);
   }
   cg->cgCtx = NULL;
}

/* Full deinit. */
static void gl_cg_deinit(void)
{
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;

   if (!cg)
      return;

   gl_cg_destroy_resources(cg);
   gl_cg_deinit_context_state(cg);
}

#define SET_LISTING(cg, type) \
{ \
   const char *list = cgGetLastListing(cg->cgCtx); \
   if (list) \
      listing_##type = strdup(list); \
}

static bool load_program(
      cg_shader_data_t *cg,
      unsigned idx,
      const char *prog,
      bool path_is_file)
{
   bool ret = true;
   char *listing_f = NULL;
   char *listing_v = NULL;

   unsigned i, argc = 0;
   const char *argv[2 + GFX_MAX_SHADERS];

   argv[argc++] = "-DPARAMETER_UNIFORM";
   for (i = 0; i < GFX_MAX_SHADERS; i++)
   {
      if (*(cg->cg_alias_define[i]))
         argv[argc++] = cg->cg_alias_define[i];
   }
   argv[argc] = NULL;

   if (path_is_file)
   {
      cg->prg[idx].fprg = cgCreateProgramFromFile(cg->cgCtx, CG_SOURCE,
            prog, cg->cgFProf, "main_fragment", argv);
      SET_LISTING(cg, f);
      cg->prg[idx].vprg = cgCreateProgramFromFile(cg->cgCtx, CG_SOURCE,
            prog, cg->cgVProf, "main_vertex", argv);
      SET_LISTING(cg, v);
   }
   else
   {
      cg->prg[idx].fprg = cgCreateProgram(cg->cgCtx, CG_SOURCE,
            prog, cg->cgFProf, "main_fragment", argv);
      SET_LISTING(cg, f);
      cg->prg[idx].vprg = cgCreateProgram(cg->cgCtx, CG_SOURCE,
            prog, cg->cgVProf, "main_vertex", argv);
      SET_LISTING(cg, v);
   }

   if (!cg->prg[idx].fprg || !cg->prg[idx].vprg)
   {
      RARCH_ERR("CG error: %s\n", cgGetErrorString(cgGetError()));
      if (listing_f)
         RARCH_ERR("Fragment:\n%s\n", listing_f);
      else if (listing_v)
         RARCH_ERR("Vertex:\n%s\n", listing_v);

      ret = false;
      goto end;
   }

   cgGLLoadProgram(cg->prg[idx].fprg);
   cgGLLoadProgram(cg->prg[idx].vprg);

end:
   free(listing_f);
   free(listing_v);
   return ret;
}

static void set_program_base_attrib(cg_shader_data_t *cg, unsigned i);

static bool load_stock(cg_shader_data_t *cg)
{
   if (!load_program(cg, 0, stock_cg_program, false))
   {
      RARCH_ERR("Failed to compile passthrough shader, is something wrong with your environment?\n");
      return false;
   }

   set_program_base_attrib(cg, 0);

   return true;
}

static bool load_plain(cg_shader_data_t *cg, const char *path)
{
   if (!load_stock(cg))
      return false;

   cg->cg_shader = (struct video_shader*)calloc(1, sizeof(*cg->cg_shader));
   if (!cg->cg_shader)
      return false;

   cg->cg_shader->passes = 1;

   if (path)
   {
      RARCH_LOG("Loading Cg file: %s\n", path);
      strlcpy(cg->cg_shader->pass[0].source.path, path,
            sizeof(cg->cg_shader->pass[0].source.path));
      if (!load_program(cg, 1, path, true))
         return false;
   }
   else
   {
      RARCH_LOG("Loading stock Cg file.\n");
      cg->prg[1] = cg->prg[0];
   }

   video_shader_resolve_parameters(NULL, cg->cg_shader);
   return true;
}

static bool gl_cg_load_imports(cg_shader_data_t *cg)
{
   unsigned i;
   struct state_tracker_info tracker_info = {0};

   if (!cg->cg_shader->variables)
      return true;

   for (i = 0; i < cg->cg_shader->variables; i++)
   {
      unsigned memtype;
      switch (cg->cg_shader->variable[i].ram_type)
      {
         case RARCH_STATE_WRAM:
            memtype = RETRO_MEMORY_SYSTEM_RAM;
            break;

         default:
            memtype = -1u;
      }

      if ((memtype != -1u) && 
            (cg->cg_shader->variable[i].addr >= pretro_get_memory_size(memtype)))
      {
         RARCH_ERR("Address out of bounds.\n");
         return false;
      }
   }

   tracker_info.wram = (uint8_t*)
      pretro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
   tracker_info.info      = cg->cg_shader->variable;
   tracker_info.info_elem = cg->cg_shader->variables;

#ifdef HAVE_PYTHON
   if (*cg->cg_shader->script_path)
   {
      tracker_info.script = cg->cg_shader->script_path;
      tracker_info.script_is_file = true;
   }

   tracker_info.script_class = 
      *cg->cg_shader->script_class ? cg->cg_shader->script_class : NULL;
#endif

   cg->state_tracker = state_tracker_init(&tracker_info);
   if (!cg->state_tracker)
      RARCH_WARN("Failed to initialize state tracker.\n");

   return true;
}

static bool load_shader(cg_shader_data_t *cg, unsigned i)
{
   RARCH_LOG("Loading Cg shader: \"%s\".\n",
         cg->cg_shader->pass[i].source.path);

   if (!load_program(cg, i + 1,
            cg->cg_shader->pass[i].source.path, true))
      return false;

   return true;
}

static bool load_preset(cg_shader_data_t *cg, const char *path)
{
   unsigned i;
   config_file_t *conf = NULL;

   if (!load_stock(cg))
      return false;

   RARCH_LOG("Loading Cg meta-shader: %s\n", path);
   conf = config_file_new(path);
   if (!conf)
   {
      RARCH_ERR("Failed to load preset.\n");
      return false;
   }

   cg->cg_shader = (struct video_shader*)calloc(1, sizeof(*cg->cg_shader));
   if (!cg->cg_shader)
   {
      config_file_free(conf);
      return false;
   }

   if (!video_shader_read_conf_cgp(conf, cg->cg_shader))
   {
      RARCH_ERR("Failed to parse CGP file.\n");
      config_file_free(conf);
      return false;
   }

   video_shader_resolve_relative(cg->cg_shader, path);
   video_shader_resolve_parameters(conf, cg->cg_shader);
   config_file_free(conf);

   if (cg->cg_shader->passes > GFX_MAX_SHADERS - 3)
   {
      RARCH_WARN("Too many shaders ... Capping shader amount to %d.\n",
            GFX_MAX_SHADERS - 3);
      cg->cg_shader->passes = GFX_MAX_SHADERS - 3;
   }

   for (i = 0; i < cg->cg_shader->passes; i++)
      if (*cg->cg_shader->pass[i].alias)
         snprintf(cg->cg_alias_define[i],
               sizeof(cg->cg_alias_define[i]),
               "-D%s_ALIAS",
               cg->cg_shader->pass[i].alias);

   for (i = 0; i < cg->cg_shader->passes; i++)
   {
      if (!load_shader(cg, i))
      {
         RARCH_ERR("Failed to load shaders ...\n");
         return false;
      }
   }

   if (!gl_load_luts(cg->cg_shader, cg->lut_textures))
   {
      RARCH_ERR("Failed to load lookup textures ...\n");
      return false;
   }

   if (!gl_cg_load_imports(cg))
   {
      RARCH_ERR("Failed to load imports ...\n");
      return false;
   }

   return true;
}

#define SEMANTIC_TEXCOORD     0x92ee91cdU
#define SEMANTIC_TEXCOORD0    0xf0c0cb9dU
#define SEMANTIC_TEXCOORD1    0xf0c0cb9eU
#define SEMANTIC_COLOR        0x0ce809a4U
#define SEMANTIC_COLOR0       0xa9e93e54U
#define SEMANTIC_POSITION     0xd87309baU

static void set_program_base_attrib(cg_shader_data_t *cg, unsigned i)
{
   CGparameter param = cgGetFirstParameter(cg->prg[i].vprg, CG_PROGRAM);
   for (; param; param = cgGetNextParameter(param))
   {
      uint32_t semantic_hash;
      const char *semantic = NULL;
      if (cgGetParameterDirection(param) != CG_IN 
            || cgGetParameterVariability(param) != CG_VARYING)
         continue;

      semantic = cgGetParameterSemantic(param);
      if (!semantic)
         continue;

      RARCH_LOG("CG: Found semantic \"%s\" in prog #%u.\n", semantic, i);

      semantic_hash = djb2_calculate(semantic);

      switch (semantic_hash)
      {
         case SEMANTIC_TEXCOORD:
         case SEMANTIC_TEXCOORD0:
            cg->prg[i].tex     = param;
            break;
         case SEMANTIC_COLOR:
         case SEMANTIC_COLOR0:
            cg->prg[i].color   = param;
            break;
         case SEMANTIC_POSITION:
            cg->prg[i].vertex  = param;
            break;
         case SEMANTIC_TEXCOORD1:
            cg->prg[i].lut_tex = param;
            break;
      }
   }

   if (!cg->prg[i].tex)
      cg->prg[i].tex = cgGetNamedParameter  (cg->prg[i].vprg, "IN.tex_coord");
   if (!cg->prg[i].color)
      cg->prg[i].color = cgGetNamedParameter(cg->prg[i].vprg, "IN.color");
   if (!cg->prg[i].vertex)
      cg->prg[i].vertex = cgGetNamedParameter   (cg->prg[i].vprg, "IN.vertex_coord");
   if (!cg->prg[i].lut_tex)
      cg->prg[i].lut_tex = cgGetNamedParameter  (cg->prg[i].vprg, "IN.lut_tex_coord");
}

static void set_pass_attrib(struct cg_program *program, struct cg_fbo_params *fbo,
      const char *attr)
{
   char attr_buf[64] = {0};

   snprintf(attr_buf, sizeof(attr_buf), "%s.texture", attr);
   if (!fbo->tex)
      fbo->tex = cgGetNamedParameter(program->fprg, attr_buf);

   snprintf(attr_buf, sizeof(attr_buf), "%s.video_size", attr);
   if (!fbo->vid_size_v)
      fbo->vid_size_v = cgGetNamedParameter(program->vprg, attr_buf);
   if (!fbo->vid_size_f)
      fbo->vid_size_f = cgGetNamedParameter(program->fprg, attr_buf);

   snprintf(attr_buf, sizeof(attr_buf), "%s.texture_size", attr);
   if (!fbo->tex_size_v)
      fbo->tex_size_v = cgGetNamedParameter(program->vprg, attr_buf);
   if (!fbo->tex_size_f)
      fbo->tex_size_f = cgGetNamedParameter(program->fprg, attr_buf);

   snprintf(attr_buf, sizeof(attr_buf), "%s.tex_coord", attr);
   if (!fbo->coord)
      fbo->coord = cgGetNamedParameter(program->vprg, attr_buf);
}

static void set_program_attributes(cg_shader_data_t *cg, unsigned i)
{
   unsigned j;

   if (!cg)
      return;

   cgGLBindProgram(cg->prg[i].fprg);
   cgGLBindProgram(cg->prg[i].vprg);

   set_program_base_attrib(cg, i);

   cg->prg[i].vid_size_f = cgGetNamedParameter (cg->prg[i].fprg, "IN.video_size");
   cg->prg[i].tex_size_f = cgGetNamedParameter (cg->prg[i].fprg, "IN.texture_size");
   cg->prg[i].out_size_f = cgGetNamedParameter (cg->prg[i].fprg, "IN.output_size");
   cg->prg[i].frame_cnt_f = cgGetNamedParameter(cg->prg[i].fprg, "IN.frame_count");
   cg->prg[i].frame_dir_f = cgGetNamedParameter(cg->prg[i].fprg, "IN.frame_direction");
   cg->prg[i].vid_size_v = cgGetNamedParameter (cg->prg[i].vprg, "IN.video_size");
   cg->prg[i].tex_size_v = cgGetNamedParameter (cg->prg[i].vprg, "IN.texture_size");
   cg->prg[i].out_size_v = cgGetNamedParameter (cg->prg[i].vprg, "IN.output_size");
   cg->prg[i].frame_cnt_v = cgGetNamedParameter(cg->prg[i].vprg, "IN.frame_count");
   cg->prg[i].frame_dir_v = cgGetNamedParameter(cg->prg[i].vprg, "IN.frame_direction");

   cg->prg[i].mvp = cgGetNamedParameter(cg->prg[i].vprg, "modelViewProj");
   if (!cg->prg[i].mvp)
      cg->prg[i].mvp = cgGetNamedParameter(cg->prg[i].vprg, "IN.mvp_matrix");

   cg->prg[i].orig.tex = cgGetNamedParameter(cg->prg[i].fprg, "ORIG.texture");
   cg->prg[i].orig.vid_size_v = cgGetNamedParameter(cg->prg[i].vprg, "ORIG.video_size");
   cg->prg[i].orig.vid_size_f = cgGetNamedParameter(cg->prg[i].fprg, "ORIG.video_size");
   cg->prg[i].orig.tex_size_v = cgGetNamedParameter(cg->prg[i].vprg, "ORIG.texture_size");
   cg->prg[i].orig.tex_size_f = cgGetNamedParameter(cg->prg[i].fprg, "ORIG.texture_size");
   cg->prg[i].orig.coord = cgGetNamedParameter(cg->prg[i].vprg, "ORIG.tex_coord");

   if (i > 1)
   {
      char pass_str[64] = {0};

      snprintf(pass_str, sizeof(pass_str), "PASSPREV%u", i);
      set_pass_attrib(&cg->prg[i], &cg->prg[i].orig, pass_str);
   }

   for (j = 0; j < PREV_TEXTURES; j++)
   {
      char attr_buf_tex[64]      = {0};
      char attr_buf_vid_size[64] = {0};
      char attr_buf_tex_size[64] = {0};
      char attr_buf_coord[64]    = {0};
      static const char *prev_names[PREV_TEXTURES] = {
         "PREV",
         "PREV1",
         "PREV2",
         "PREV3",
         "PREV4",
         "PREV5",
         "PREV6",
      };

      snprintf(attr_buf_tex,      sizeof(attr_buf_tex),     
            "%s.texture", prev_names[j]);
      snprintf(attr_buf_vid_size, sizeof(attr_buf_vid_size),
            "%s.video_size", prev_names[j]);
      snprintf(attr_buf_tex_size, sizeof(attr_buf_tex_size),
            "%s.texture_size", prev_names[j]);
      snprintf(attr_buf_coord,    sizeof(attr_buf_coord),
            "%s.tex_coord", prev_names[j]);

      cg->prg[i].prev[j].tex = cgGetNamedParameter(cg->prg[i].fprg, attr_buf_tex);

      cg->prg[i].prev[j].vid_size_v = 
         cgGetNamedParameter(cg->prg[i].vprg, attr_buf_vid_size);
      cg->prg[i].prev[j].vid_size_f = 
         cgGetNamedParameter(cg->prg[i].fprg, attr_buf_vid_size);

      cg->prg[i].prev[j].tex_size_v = 
         cgGetNamedParameter(cg->prg[i].vprg, attr_buf_tex_size);
      cg->prg[i].prev[j].tex_size_f = 
         cgGetNamedParameter(cg->prg[i].fprg, attr_buf_tex_size);

      cg->prg[i].prev[j].coord = cgGetNamedParameter(cg->prg[i].vprg, attr_buf_coord);
   }

   for (j = 0; j + 1 < i; j++)
   {
      char pass_str[64] = {0};

      snprintf(pass_str, sizeof(pass_str), "PASS%u", j + 1);
      set_pass_attrib(&cg->prg[i], &cg->prg[i].fbo[j], pass_str);
      snprintf(pass_str, sizeof(pass_str), "PASSPREV%u", i - (j + 1));
      set_pass_attrib(&cg->prg[i], &cg->prg[i].fbo[j], pass_str);

      if (*cg->cg_shader->pass[j].alias)
         set_pass_attrib(&cg->prg[i], &cg->prg[i].fbo[j], cg->cg_shader->pass[j].alias);
   }
}

static bool gl_cg_init(void *data, const char *path)
{
   cg_shader_data_t *cg = NULL;
   driver_t *driver     = NULL;
   unsigned i;

   (void)data;

   cg     = (cg_shader_data_t*)calloc(1, sizeof(cg_shader_data_t));
   driver = driver_get_ptr();

   if (!cg)
      return false;

#ifdef HAVE_CG_RUNTIME_COMPILER
   cgRTCgcInit();
#endif

   cg->cgCtx = cgCreateContext();

   if (!cg->cgCtx)
   {
      RARCH_ERR("Failed to create Cg context\n");
      free(cg);
      return false;
   }

#ifdef RARCH_CG_DEBUG
   cgGLSetDebugMode(CG_TRUE);
   cgSetErrorHandler(cg_error_handler, NULL);
#endif

   cg->cgFProf = cgGLGetLatestProfile(CG_GL_FRAGMENT);
   cg->cgVProf = cgGLGetLatestProfile(CG_GL_VERTEX);

   if (
         cg->cgFProf == CG_PROFILE_UNKNOWN ||
         cg->cgVProf == CG_PROFILE_UNKNOWN)
   {
      RARCH_ERR("Invalid profile type\n");
      free(cg);
      goto error;
   }

   RARCH_LOG("[Cg]: Vertex profile: %s\n",   cgGetProfileString(cg->cgVProf));
   RARCH_LOG("[Cg]: Fragment profile: %s\n", cgGetProfileString(cg->cgFProf));
   cgGLSetOptimalOptions(cg->cgFProf);
   cgGLSetOptimalOptions(cg->cgVProf);
   cgGLEnableProfile(cg->cgFProf);
   cgGLEnableProfile(cg->cgVProf);

   memset(cg->cg_alias_define, 0, sizeof(cg->cg_alias_define));

   if (path && !strcmp(path_get_extension(path), "cgp"))
   {
      if (!load_preset(cg, path))
         goto error;
   }
   else
   {
      if (!load_plain(cg, path))
         goto error;
   }

   cg->prg[0].mvp = cgGetNamedParameter(cg->prg[0].vprg, "IN.mvp_matrix");
   for (i = 1; i <= cg->cg_shader->passes; i++)
      set_program_attributes(cg, i);

   /* If we aren't using last pass non-FBO shader, 
    * this shader will be assumed to be "fixed-function".
    *
    * Just use prg[0] for that pass, which will be
    * pass-through. */
   cg->prg[cg->cg_shader->passes + 1] = cg->prg[0]; 

   /* No need to apply Android hack in Cg. */
   cg->prg[GL_SHADER_STOCK_BLEND] = cg->prg[0];

   cgGLBindProgram(cg->prg[1].fprg);
   cgGLBindProgram(cg->prg[1].vprg);

   driver->video_shader_data = cg;

   return true;

error:
   gl_cg_destroy_resources(cg);
   if (!cg)
      free(cg);
   return false;
}

static void gl_cg_use(void *data, unsigned idx)
{
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;
   (void)data;

   if (cg && cg->prg[idx].vprg && cg->prg[idx].fprg)
   {
      gl_cg_reset_attrib(cg);

      cg->active_idx = idx;
      cgGLBindProgram(cg->prg[idx].vprg);
      cgGLBindProgram(cg->prg[idx].fprg);
   }
}

static unsigned gl_cg_num(void)
{
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;
   if (!cg)
      return 0;
   return cg->cg_shader->passes;
}

static bool gl_cg_filter_type(unsigned idx, bool *smooth)
{
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;
   if (cg && idx &&
         (cg->cg_shader->pass[idx - 1].filter != RARCH_FILTER_UNSPEC)
      )
   {
      *smooth = (cg->cg_shader->pass[idx - 1].filter == RARCH_FILTER_LINEAR);
      return true;
   }

   return false;
}

static enum gfx_wrap_type gl_cg_wrap_type(unsigned idx)
{
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;
   if (cg && idx)
      return cg->cg_shader->pass[idx - 1].wrap;
   return RARCH_WRAP_BORDER;
}

static void gl_cg_shader_scale(unsigned idx, struct gfx_fbo_scale *scale)
{
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;
   if (cg && idx)
      *scale = cg->cg_shader->pass[idx - 1].fbo;
   else
      scale->valid = false;
}

static unsigned gl_cg_get_prev_textures(void)
{
   unsigned i, j;
   unsigned max_prev = 0;
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;

   if (!cg)
      return 0;

   for (i = 1; i <= cg->cg_shader->passes; i++)
      for (j = 0; j < PREV_TEXTURES; j++)
         if (cg->prg[i].prev[j].tex)
            max_prev = max(j + 1, max_prev);

   return max_prev;
}

static bool gl_cg_mipmap_input(unsigned idx)
{
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;
   if (cg && idx)
      return cg->cg_shader->pass[idx - 1].mipmap;
   return false;
}

static struct video_shader *gl_cg_get_current_shader(void)
{
   driver_t *driver = driver_get_ptr();
   cg_shader_data_t *cg = (cg_shader_data_t*)driver->video_shader_data;
   if (!cg)
      return NULL;
   return cg->cg_shader;
}

const shader_backend_t gl_cg_backend = {
   gl_cg_init,
   gl_cg_deinit,
   gl_cg_set_params,
   gl_cg_use,
   gl_cg_num,
   gl_cg_filter_type,
   gl_cg_wrap_type,
   gl_cg_shader_scale,
   gl_cg_set_coords,
   gl_cg_set_mvp,
   gl_cg_get_prev_textures,
   gl_cg_mipmap_input,
   gl_cg_get_current_shader,

   RARCH_SHADER_CG,
   "gl_cg"
};

