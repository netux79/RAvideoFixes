
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
#include <string>
#include <vector>
#include <retro_inline.h>
#include <Cg/cg.h>
#include <Cg/cgD3D9.h>
#include "render_chain_driver.h"
#include "../video_driver.h"
#include "../../general.h"
#include "d3d.h"

struct lut_info
{
   LPDIRECT3DTEXTURE tex;
   char id[64];
   bool smooth;
};

struct Vertex
{
   float x, y, z;
   float u, v;
   float lut_u, lut_v;
   float r, g, b, a;
};

struct Pass
{
   LinkInfo info;
   LPDIRECT3DTEXTURE tex;
   LPDIRECT3DVERTEXBUFFER vertex_buf;
   CGprogram vPrg, fPrg;
   unsigned last_width, last_height;
   LPDIRECT3DVERTEXDECLARATION vertex_decl;
   std::vector<unsigned> attrib_map;
};

typedef struct cg_renderchain
{
   LPDIRECT3DDEVICE dev;
   unsigned pixel_size;
   const video_info_t *video_info;
   state_tracker_t *tracker;
   struct state_tracker_uniform uniform_info[MAX_VARIABLES];
   unsigned uniform_cnt;
   struct
   {
      LPDIRECT3DTEXTURE tex[TEXTURES];
      LPDIRECT3DVERTEXBUFFER vertex_buf[TEXTURES];
      unsigned ptr;
      unsigned last_width[TEXTURES];
      unsigned last_height[TEXTURES];
   } prev;
   std::vector<Pass> passes;
   CGprogram vStock, fStock;
   std::vector<lut_info> luts;
   D3DVIEWPORT *final_viewport;
   unsigned frame_count;
   std::vector<unsigned> bound_tex;
   std::vector<unsigned> bound_vert;
   CGcontext cgCtx;
} cg_renderchain_t;

static INLINE D3DTEXTUREFILTERTYPE translate_filter(unsigned type)
{
   settings_t *settings = config_get_ptr();

   switch (type)
   {
		case RARCH_FILTER_UNSPEC:
            return settings->video.smooth ? D3DTEXF_LINEAR : D3DTEXF_POINT;
		case RARCH_FILTER_LINEAR:
			return D3DTEXF_LINEAR;
		case RARCH_FILTER_NEAREST:
			return D3DTEXF_POINT;
   }
   
   return D3DTEXF_POINT;
}

static INLINE D3DTEXTUREFILTERTYPE translate_filter(bool smooth)
{
   if (smooth)
      return D3DTEXF_LINEAR;
   return D3DTEXF_POINT;
}

static const char *stock_program =
    "void main_vertex"
    "("
    "	float4 position : POSITION,"
    "	float2 texCoord : TEXCOORD0,"
    "  float4 color : COLOR,"
    ""
    "  uniform float4x4 modelViewProj,"
    ""
    "	out float4 oPosition : POSITION,"
    "	out float2 otexCoord : TEXCOORD0,"
    "  out float4 oColor : COLOR"
    ")"
    "{"
    "	oPosition = mul(modelViewProj, position);"
    "	otexCoord = texCoord;"
    "  oColor = color;"
    "}"
    ""
    "float4 main_fragment(in float4 color : COLOR, float2 tex : TEXCOORD0, uniform sampler2D s0 : TEXUNIT0) : COLOR"
    "{"
    "   return color * tex2D(s0, tex);"
    "}";

static INLINE bool validate_param_name(const char *name)
{
   unsigned i;
   static const char *illegal[] = {
      "PREV.",
      "PREV1.",
      "PREV2.",
      "PREV3.",
      "PREV4.",
      "PREV5.",
      "PREV6.",
      "ORIG.",
      "IN.",
      "PASS",
   };

   if (!name)
      return false;

   for (i = 0; i < sizeof(illegal) / sizeof(illegal[0]); i++)
      if (strstr(name, illegal[i]) == name)
         return false;

   return true;
}

static INLINE CGparameter find_param_from_semantic(
      CGparameter param, const char *sem)
{
   while (param)
   {
      if (cgGetParameterType(param) == CG_STRUCT)
      {
         CGparameter ret = find_param_from_semantic(
               cgGetFirstStructParameter(param), sem);

         if (ret)
            return ret;
      }
      else
      {
         if (cgGetParameterSemantic(param) &&
               !strcmp(sem, cgGetParameterSemantic(param)) &&
               cgGetParameterDirection(param) == CG_IN &&
               cgGetParameterVariability(param) == CG_VARYING &&
               validate_param_name(cgGetParameterName(param)))
            return param;
      }
      param = cgGetNextParameter(param);
   }

   return NULL;
}

static INLINE CGparameter find_param_from_semantic(CGprogram prog,
      const char *sem)
{
   CGparameter param = cgGetFirstParameter(prog, CG_PROGRAM);
   return find_param_from_semantic(param, sem);
}

static bool renderchain_compile_shaders(void *data,
      void *fragment_data, void *vertex_data, const std::string &shader)
{
   CGprogram *fPrg            = (CGprogram*)fragment_data;
   CGprogram *vPrg            = (CGprogram*)vertex_data;
   cg_renderchain_t *chain       = (cg_renderchain_t*)data;
   CGprofile vertex_profile   = cgD3D9GetLatestVertexProfile();
   CGprofile fragment_profile = cgD3D9GetLatestPixelProfile();
   const char **fragment_opts = cgD3D9GetOptimalOptions(fragment_profile);
   const char **vertex_opts   = cgD3D9GetOptimalOptions(vertex_profile);

   RARCH_LOG("[D3D Cg]: Vertex profile: %s\n", cgGetProfileString(vertex_profile));
   RARCH_LOG("[D3D Cg]: Fragment profile: %s\n", cgGetProfileString(fragment_profile));

   if (shader.length() > 0)
   {
      RARCH_LOG("[D3D Cg]: Compiling shader: %s.\n", shader.c_str());
      *fPrg = cgCreateProgramFromFile(chain->cgCtx, CG_SOURCE,
            shader.c_str(), fragment_profile, "main_fragment", fragment_opts);

      if (cgGetLastListing(chain->cgCtx))
         RARCH_ERR("[D3D Cg]: Fragment error:\n%s\n", cgGetLastListing(chain->cgCtx));

      *vPrg = cgCreateProgramFromFile(chain->cgCtx, CG_SOURCE,
            shader.c_str(), vertex_profile, "main_vertex", vertex_opts);

      if (cgGetLastListing(chain->cgCtx))
         RARCH_ERR("[D3D Cg]: Vertex error:\n%s\n", cgGetLastListing(chain->cgCtx));
   }
   else
   {
      RARCH_LOG("[D3D Cg]: Compiling stock shader.\n");

      *fPrg = cgCreateProgram(chain->cgCtx, CG_SOURCE, stock_program,
            fragment_profile, "main_fragment", fragment_opts);

      if (cgGetLastListing(chain->cgCtx))
         RARCH_ERR("[D3D Cg]: Fragment error:\n%s\n", cgGetLastListing(chain->cgCtx));

      *vPrg = cgCreateProgram(chain->cgCtx, CG_SOURCE, stock_program,
            vertex_profile, "main_vertex", vertex_opts);

      if (cgGetLastListing(chain->cgCtx))
         RARCH_ERR("[D3D Cg]: Vertex error:\n%s\n", cgGetLastListing(chain->cgCtx));
   }

   if (!fPrg || !vPrg)
      return false;

   cgD3D9LoadProgram(*fPrg, true, 0);
   cgD3D9LoadProgram(*vPrg, true, 0);
   return true;
}

static void renderchain_set_shaders(void *data, void *fragment_data, void *vertex_data)
{
   CGprogram *fPrg = (CGprogram*)fragment_data;
   CGprogram *vPrg = (CGprogram*)vertex_data;

   cgD3D9BindProgram(*fPrg);
   cgD3D9BindProgram(*vPrg);
}

#if 0
static void cg_d3d9_renderchain_destroy_stock_shader(void *data)
{
   cg_renderchain_t *chain = (cg_renderchain_t*)data;

   if (!chain)
      return;

   if (chain->fStock)
      cgDestroyProgram(chain->fStock);
   if (chain->vStock)
      cgDestroyProgram(chain->vStock);
}
#endif

static void renderchain_destroy_shader(void *data, int i)
{
   cg_renderchain_t *chain = (cg_renderchain_t*)data;

   if (!chain)
      return;

   if (chain->passes[i].fPrg)
      cgDestroyProgram(chain->passes[i].fPrg);
   if (chain->passes[i].vPrg)
      cgDestroyProgram(chain->passes[i].vPrg);
}

static void renderchain_set_shader_mvp(void *data, void *shader_data, void *matrix_data)
{
   CGprogram              *vPrg = (CGprogram*)shader_data;
   const D3DXMATRIX     *matrix = (const D3DXMATRIX*)matrix_data;
   CGparameter cgpModelViewProj = cgGetNamedParameter(*vPrg, "modelViewProj");
   if (cgpModelViewProj)
      cgD3D9SetUniformMatrix(cgpModelViewProj, matrix);
}

#define set_cg_param(prog, param, val) do { \
   CGparameter cgp = cgGetNamedParameter(prog, param); \
   if (cgp) \
      cgD3D9SetUniform(cgp, &val); \
} while(0)

static void renderchain_set_shader_params(void *data, void *pass_data,
            unsigned video_w, unsigned video_h,
            unsigned tex_w, unsigned tex_h,
            unsigned viewport_w, unsigned viewport_h)
{
   float frame_cnt;
   D3DXVECTOR2 video_size, texture_size, output_size;
   Pass           *pass = (Pass*)pass_data;
   cg_renderchain_t *chain = (cg_renderchain_t*)data;
   video_size.x         = video_w;
   video_size.y         = video_h;
   texture_size.x       = tex_w;
   texture_size.y       = tex_h;
   output_size.x        = viewport_w;
   output_size.y        = viewport_h;

   set_cg_param(pass->vPrg, "IN.video_size", video_size);
   set_cg_param(pass->fPrg, "IN.video_size", video_size);
   set_cg_param(pass->vPrg, "IN.texture_size", texture_size);
   set_cg_param(pass->fPrg, "IN.texture_size", texture_size);
   set_cg_param(pass->vPrg, "IN.output_size", output_size);
   set_cg_param(pass->fPrg, "IN.output_size", output_size);

   frame_cnt            = chain->frame_count;

   if (pass->info.pass->frame_count_mod)
      frame_cnt         = chain->frame_count % pass->info.pass->frame_count_mod;

   set_cg_param(pass->fPrg, "IN.frame_count", frame_cnt);
   set_cg_param(pass->vPrg, "IN.frame_count", frame_cnt);
}

static void renderchain_bind_tracker(void *data, void *pass_data, unsigned pass_index)
{
   unsigned i;
   Pass           *pass  = (Pass*)pass_data;
   cg_renderchain_t *chain  = (cg_renderchain_t*)data;
   if (!chain->tracker)
      return;

   if (pass_index == 1)
      chain->uniform_cnt = state_tracker_get_uniform(chain->tracker,
            chain->uniform_info, MAX_VARIABLES, chain->frame_count);

   for (i = 0; i < chain->uniform_cnt; i++)
   {
      set_cg_param(pass->fPrg, chain->uniform_info[i].id,
            chain->uniform_info[i].value);
      set_cg_param(pass->vPrg, chain->uniform_info[i].id,
            chain->uniform_info[i].value);
   }
}

#define DECL_FVF_POSITION(stream) \
   { (WORD)(stream), 0 * sizeof(float), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, \
      D3DDECLUSAGE_POSITION, 0 }
#define DECL_FVF_TEXCOORD(stream, offset, index) \
   { (WORD)(stream), (WORD)(offset * sizeof(float)), D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, \
      D3DDECLUSAGE_TEXCOORD, (BYTE)(index) }
#define DECL_FVF_COLOR(stream, offset, index) \
   { (WORD)(stream), (WORD)(offset * sizeof(float)), D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, \
      D3DDECLUSAGE_COLOR, (BYTE)(index) } \

static bool cg_d3d9_renderchain_init_shader_fvf(void *data, void *pass_data)
{
   CGparameter param;
   unsigned index, i, count;
   unsigned tex_index                          = 0;
   bool texcoord0_taken                        = false;
   bool texcoord1_taken                        = false;
   bool stream_taken[4]                        = {false};
   Pass          *pass                         = (Pass*)pass_data;
   cg_renderchain_t *chain                        = (cg_renderchain_t*)data;
   static const D3DVERTEXELEMENT decl_end      = D3DDECL_END();
   static const D3DVERTEXELEMENT position_decl = DECL_FVF_POSITION(0);
   static const D3DVERTEXELEMENT tex_coord0    = DECL_FVF_TEXCOORD(1, 3, 0);
   static const D3DVERTEXELEMENT tex_coord1    = DECL_FVF_TEXCOORD(2, 5, 1);
   static const D3DVERTEXELEMENT color         = DECL_FVF_COLOR(3, 7, 0);
   D3DVERTEXELEMENT decl[MAXD3DDECLLENGTH]     = {{0}};

   if (cgD3D9GetVertexDeclaration(pass->vPrg, decl) == CG_FALSE)
      return false;

   for (count = 0; count < MAXD3DDECLLENGTH; count++)
   {
      if (memcmp(&decl_end, &decl[count], sizeof(decl_end)) == 0)
         break;
   }

   /* This is completely insane.
    * We do not have a good and easy way of setting up our
    * attribute streams, so we have to do it ourselves, yay!
    *
    * Stream 0      => POSITION
    * Stream 1      => TEXCOORD0
    * Stream 2      => TEXCOORD1
    * Stream 3      => COLOR     (Not really used for anything.)
    * Stream {4..N} => Texture coord streams for varying resources 
    *                  which have no semantics.
    */

   std::vector<bool> indices(count);

   param = find_param_from_semantic(pass->vPrg, "POSITION");
   if (!param)
      param = find_param_from_semantic(pass->vPrg, "POSITION0");

   if (param)
   {
      stream_taken[0] = true;
      RARCH_LOG("[FVF]: POSITION semantic found.\n");
      index           = cgGetParameterResourceIndex(param);
      decl[index]     = position_decl;
      indices[index]  = true;
   }

   param = find_param_from_semantic(pass->vPrg, "TEXCOORD");
   if (!param)
      param = find_param_from_semantic(pass->vPrg, "TEXCOORD0");

   if (param)
   {
      stream_taken[1] = true;
      texcoord0_taken = true;
      RARCH_LOG("[FVF]: TEXCOORD0 semantic found.\n");
      index           = cgGetParameterResourceIndex(param);
      decl[index]     = tex_coord0;
      indices[index]  = true;
   }

   param = find_param_from_semantic(pass->vPrg, "TEXCOORD1");
   if (param)
   {
      stream_taken[2] = true;
      texcoord1_taken = true;
      RARCH_LOG("[FVF]: TEXCOORD1 semantic found.\n");
      index           = cgGetParameterResourceIndex(param);
      decl[index]     = tex_coord1;
      indices[index]  = true;
   }

   param = find_param_from_semantic(pass->vPrg, "COLOR");
   if (!param)
      param = find_param_from_semantic(pass->vPrg, "COLOR0");

   if (param)
   {
      stream_taken[3] = true;
      RARCH_LOG("[FVF]: COLOR0 semantic found.\n");
      index           = cgGetParameterResourceIndex(param);
      decl[index]     = color;
      indices[index]  = true;
   }

   /* Stream {0, 1, 2, 3} might be already taken. Find first vacant stream. */
   for (index = 0; index < 4 && stream_taken[index]; index++);

   /* Find first vacant texcoord declaration. */
   if (texcoord0_taken && texcoord1_taken)
      tex_index = 2;
   else if (texcoord1_taken && !texcoord0_taken)
      tex_index = 0;
   else if (texcoord0_taken && !texcoord1_taken)
      tex_index = 1;

   for (i = 0; i < count; i++)
   {
      if (indices[i])
         pass->attrib_map.push_back(0);
      else
      {
         D3DVERTEXELEMENT elem = DECL_FVF_TEXCOORD(index, 3, tex_index);

         pass->attrib_map.push_back(index);

         decl[i]     = elem;

         /* Find next vacant stream. */
         index++;

         while (index < 4 && stream_taken[index])
            index++;

         /* Find next vacant texcoord declaration. */
         tex_index++;
         if (tex_index == 1 && texcoord1_taken)
            tex_index++;
      }
   }

   if (FAILED(chain->dev->CreateVertexDeclaration(
               decl, &pass->vertex_decl)))
      return false;

   return true;
}

static void renderchain_bind_orig(void *data, void *pass_data)
{
   unsigned index;
   CGparameter param;
   D3DXVECTOR2 video_size, texture_size;
   Pass           *pass = (Pass*)pass_data;
   cg_renderchain_t *chain = (cg_renderchain_t*)data;
   video_size.x         = chain->passes[0].last_width;
   video_size.y         = chain->passes[0].last_height;
   texture_size.x       = chain->passes[0].info.tex_w;
   texture_size.y       = chain->passes[0].info.tex_h;

   set_cg_param(pass->vPrg, "ORIG.video_size", video_size);
   set_cg_param(pass->fPrg, "ORIG.video_size", video_size);
   set_cg_param(pass->vPrg, "ORIG.texture_size", texture_size);
   set_cg_param(pass->fPrg, "ORIG.texture_size", texture_size);

   param = cgGetNamedParameter(pass->fPrg, "ORIG.texture");
   if (param)
   {
      index = cgGetParameterResourceIndex(param);
      d3d_set_texture(chain->dev, index, chain->passes[0].tex);
      d3d_set_sampler_magfilter(chain->dev, index,
            translate_filter(chain->passes[0].info.pass->filter));
      d3d_set_sampler_minfilter(chain->dev, index, 
            translate_filter(chain->passes[0].info.pass->filter));
      d3d_set_sampler_address_u(chain->dev, index, D3DTADDRESS_BORDER);
      d3d_set_sampler_address_v(chain->dev, index, D3DTADDRESS_BORDER);
      chain->bound_tex.push_back(index);
   }

   param = cgGetNamedParameter(pass->vPrg, "ORIG.tex_coord");
   if (param)
   {
      index = pass->attrib_map[cgGetParameterResourceIndex(param)];

      d3d_set_stream_source(chain->dev, index, chain->passes[0].vertex_buf, 0, sizeof(Vertex));
      chain->bound_vert.push_back(index);
   }
}

static void renderchain_bind_prev(void *data, void *pass_data)
{
   unsigned i, index;
   D3DXVECTOR2 texture_size;
   char attr_texture[64]    = {0};
   char attr_input_size[64] = {0};
   char attr_tex_size[64]   = {0};
   char attr_coord[64]      = {0};
   Pass               *pass = (Pass*)pass_data;
   cg_renderchain_t  *chain = (cg_renderchain_t*)data;
   static const char *prev_names[] = {
      "PREV",
      "PREV1",
      "PREV2",
      "PREV3",
      "PREV4",
      "PREV5",
      "PREV6",
   };

   texture_size.x = chain->passes[0].info.tex_w;
   texture_size.y = chain->passes[0].info.tex_h;

   for (i = 0; i < TEXTURES - 1; i++)
   {
      CGparameter param;
      D3DXVECTOR2 video_size;

      snprintf(attr_texture,    sizeof(attr_texture),    "%s.texture",      prev_names[i]);
      snprintf(attr_input_size, sizeof(attr_input_size), "%s.video_size",   prev_names[i]);
      snprintf(attr_tex_size,   sizeof(attr_tex_size),   "%s.texture_size", prev_names[i]);
      snprintf(attr_coord,      sizeof(attr_coord),      "%s.tex_coord",    prev_names[i]);

      video_size.x = chain->prev.last_width[(chain->prev.ptr - (i + 1)) & TEXTURESMASK];
      video_size.y = chain->prev.last_height[(chain->prev.ptr - (i + 1)) & TEXTURESMASK];

      set_cg_param(pass->vPrg, attr_input_size, video_size);
      set_cg_param(pass->fPrg, attr_input_size, video_size);
      set_cg_param(pass->vPrg, attr_tex_size, texture_size);
      set_cg_param(pass->fPrg, attr_tex_size, texture_size);

      param = cgGetNamedParameter(pass->fPrg, attr_texture);
      if (param)
      {
         LPDIRECT3DTEXTURE tex;

         index = cgGetParameterResourceIndex(param);

         tex = (LPDIRECT3DTEXTURE)
            chain->prev.tex[(chain->prev.ptr - (i + 1)) & TEXTURESMASK];

         d3d_set_texture(chain->dev, index, tex);
         chain->bound_tex.push_back(index);

         d3d_set_sampler_magfilter(chain->dev, index,
               translate_filter(chain->passes[0].info.pass->filter));
         d3d_set_sampler_minfilter(chain->dev, index, 
               translate_filter(chain->passes[0].info.pass->filter));
         d3d_set_sampler_address_u(chain->dev, index, D3DTADDRESS_BORDER);
         d3d_set_sampler_address_v(chain->dev, index, D3DTADDRESS_BORDER);
      }

      param = cgGetNamedParameter(pass->vPrg, attr_coord);
      if (param)
      {
         LPDIRECT3DVERTEXBUFFER vert_buf;

         index = pass->attrib_map[cgGetParameterResourceIndex(param)];
         vert_buf = (LPDIRECT3DVERTEXBUFFER)
            chain->prev.vertex_buf[(chain->prev.ptr - (i + 1)) & TEXTURESMASK];
         chain->bound_vert.push_back(index);

         d3d_set_stream_source(chain->dev, index, vert_buf, 0, sizeof(Vertex));
      }
   }
}

static void cg_d3d9_renderchain_add_lut(void *data,
      unsigned index, unsigned i)
{
   cg_renderchain_t *chain = (cg_renderchain_t*)data;
   if (!chain)
      return;

   d3d_set_texture(chain->dev, index, chain->luts[i].tex);
   d3d_set_sampler_magfilter(chain->dev, index,
         translate_filter(chain->luts[i].smooth));
   d3d_set_sampler_minfilter(chain->dev, index, 
         translate_filter(chain->luts[i].smooth));
   d3d_set_sampler_address_u(chain->dev, index, D3DTADDRESS_BORDER);
   d3d_set_sampler_address_v(chain->dev, index, D3DTADDRESS_BORDER);
   chain->bound_tex.push_back(index);
}

static void renderchain_bind_luts(void *data, void *pass_data)
{
   unsigned i, index;
   Pass           *pass = (Pass*)pass_data;
   cg_renderchain_t *chain = (cg_renderchain_t*)data;

   for (i = 0; i < chain->luts.size(); i++)
   {
      CGparameter vparam;
      CGparameter fparam = cgGetNamedParameter(pass->fPrg, chain->luts[i].id);
      int bound_index    = -1;

      if (fparam)
      {
         index           = cgGetParameterResourceIndex(fparam);
         bound_index     = index;

         cg_d3d9_renderchain_add_lut(chain, index, i);
      }

      vparam             = cgGetNamedParameter(pass->vPrg, chain->luts[i].id);

      if (vparam)
      {
         index           = cgGetParameterResourceIndex(vparam);
         if (index != (unsigned)bound_index)
            cg_d3d9_renderchain_add_lut(chain, index, i);
      }
   }
}

static void renderchain_bind_pass(void *data, void *pass_data, unsigned pass_index)
{
   unsigned i, index;
   Pass           *pass = (Pass*)pass_data;
   cg_renderchain_t *chain = (cg_renderchain_t*)data;

   /* We only bother binding passes which are two indices behind. */
   if (pass_index < 3)
      return;

   for (i = 1; i < pass_index - 1; i++)
   {
      CGparameter param;
      D3DXVECTOR2 video_size, texture_size;
      char pass_base[64]       = {0};
      char attr_texture[64]    = {0};
      char attr_input_size[64] = {0};
      char attr_tex_size[64]   = {0};
      char attr_coord[64]      = {0};

      snprintf(pass_base,       sizeof(pass_base),       "PASS%u",          i);
      snprintf(attr_texture,    sizeof(attr_texture),    "%s.texture",      pass_base);
      snprintf(attr_input_size, sizeof(attr_input_size), "%s.video_size",   pass_base);
      snprintf(attr_tex_size,   sizeof(attr_tex_size),   "%s.texture_size", pass_base);
      snprintf(attr_coord,      sizeof(attr_coord),      "%s.tex_coord",    pass_base);

      video_size.x   = chain->passes[i].last_width;
      video_size.y   = chain->passes[i].last_height;
      texture_size.x = chain->passes[i].info.tex_w;
      texture_size.y = chain->passes[i].info.tex_h;

      set_cg_param(pass->vPrg, attr_input_size,   video_size);
      set_cg_param(pass->fPrg, attr_input_size,   video_size);
      set_cg_param(pass->vPrg, attr_tex_size,     texture_size);
      set_cg_param(pass->fPrg, attr_tex_size,     texture_size);

      param = cgGetNamedParameter(pass->fPrg, attr_texture);
      if (param)
      {
         index = cgGetParameterResourceIndex(param);
         chain->bound_tex.push_back(index);

         d3d_set_texture(chain->dev, index, chain->passes[i].tex);
         d3d_set_sampler_magfilter(chain->dev, index,
               translate_filter(chain->passes[i].info.pass->filter));
         d3d_set_sampler_minfilter(chain->dev, index, 
               translate_filter(chain->passes[i].info.pass->filter));
         d3d_set_sampler_address_u(chain->dev, index, D3DTADDRESS_BORDER);
         d3d_set_sampler_address_v(chain->dev, index, D3DTADDRESS_BORDER);
      }

      param = cgGetNamedParameter(pass->vPrg, attr_coord);
      if (param)
      {
         index = pass->attrib_map[cgGetParameterResourceIndex(param)];

         d3d_set_stream_source(chain->dev, index, chain->passes[i].vertex_buf,
               0, sizeof(Vertex));
         chain->bound_vert.push_back(index);
      }
   }
}

#if 0
static void cg_d3d9_renderchain_clear(void *data)
{
   unsigned i;
   cg_renderchain_t *chain = (cg_renderchain_t*)data;

   for (i = 0; i < TEXTURES; i++)
   {
      if (chain->prev.tex[i])
         d3d_texture_free(chain->prev.tex[i]);
      if (chain->prev.vertex_buf[i])
         d3d_vertex_buffer_free(chain->prev.vertex_buf[i], NULL);
   }

   d3d_vertex_buffer_free(NULL, chain->passes[0].vertex_decl);

   for (i = 1; i < chain->passes.size(); i++)
   {
      if (chain->passes[i].tex)
         d3d_texture_free(chain->passes[i].tex);
      d3d_vertex_buffer_free(chain->passes[i].vertex_buf, chain->passes[i].vertex_decl);
      renderchain_destroy_shader(chain, i);
   }

   for (i = 0; i < chain->luts.size(); i++)
   {
      if (chain->luts[i].tex)
         d3d_texture_free(chain->luts[i].tex);
   }

   chain->passes.clear();
   chain->luts.clear();
}
#endif

static void cg_d3d9_renderchain_deinit_shader(void *data)
{
   cg_renderchain_t *chain = (cg_renderchain_t*)data;
   if (!chain->cgCtx)
      return;

   cgD3D9UnloadAllPrograms();
   cgD3D9SetDevice(NULL);
   cgDestroyContext(chain->cgCtx);
   chain->cgCtx = NULL;
}

#if 0
static void cg_d3d9_renderchain_deinit(void *data)
{
   cg_renderchain_t *renderchain = (cg_renderchain_t*)data;

   if (renderchain)
      free(renderchain);
}
#endif

void cg_d3d9_renderchain_free(void *data)
{
   cg_renderchain_t *chain = (cg_renderchain_t*)data;

   if (!chain)
      return;

   cg_d3d9_renderchain_deinit_shader(chain);
#if 0
   cg_d3d9_renderchain_clear(chain);
   cg_d3d9_renderchain_destroy_stock_shader(chain);
   if (chain->tracker)
      state_tracker_free(chain->tracker);
#endif
   //cg_d3d9_renderchain_deinit();
}

void *cg_d3d9_renderchain_new(void)
{
   cg_renderchain_t *renderchain = (cg_renderchain_t*)calloc(1, sizeof(*renderchain));
   if (!renderchain)
      return NULL;

   return renderchain;
}

static bool cg_d3d9_renderchain_init_shader(void *data,
      void *renderchain_data)
{
   d3d_video_t *d3d              = (d3d_video_t*)data;
   cg_renderchain_t *renderchain = (cg_renderchain_t*)renderchain_data;

   if (!d3d || !renderchain)
      return false;

   renderchain->cgCtx = cgCreateContext();
   if (!renderchain->cgCtx)
      return false;

   RARCH_LOG("[D3D]: Created shader context.\n");

   HRESULT ret = cgD3D9SetDevice(d3d->dev);
   if (FAILED(ret))
      return false;
   return true;
}


static void renderchain_log_info(void *data, const void *info_data)
{
   const LinkInfo *info = (const LinkInfo*)info_data;
   RARCH_LOG("[D3D]: Render pass info:\n");
   RARCH_LOG("\tTexture width: %u\n", info->tex_w);
   RARCH_LOG("\tTexture height: %u\n", info->tex_h);

   RARCH_LOG("\tScale type (X): ");

   switch (info->pass->fbo.type_x)
   {
      case RARCH_SCALE_INPUT:
         RARCH_LOG("Relative @ %fx\n", info->pass->fbo.scale_x);
         break;

      case RARCH_SCALE_VIEWPORT:
         RARCH_LOG("Viewport @ %fx\n", info->pass->fbo.scale_x);
         break;

      case RARCH_SCALE_ABSOLUTE:
         RARCH_LOG("Absolute @ %u px\n", info->pass->fbo.abs_x);
         break;
   }

   RARCH_LOG("\tScale type (Y): ");

   switch (info->pass->fbo.type_y)
   {
      case RARCH_SCALE_INPUT:
         RARCH_LOG("Relative @ %fx\n", info->pass->fbo.scale_y);
         break;

      case RARCH_SCALE_VIEWPORT:
         RARCH_LOG("Viewport @ %fx\n", info->pass->fbo.scale_y);
         break;

      case RARCH_SCALE_ABSOLUTE:
         RARCH_LOG("Absolute @ %u px\n", info->pass->fbo.abs_y);
         break;
   }

   RARCH_LOG("\tBilinear filter: %s\n",
         info->pass->filter == RARCH_FILTER_LINEAR ? "true" : "false");
}

static bool renderchain_create_first_pass(void *data, const void *info_data,
      unsigned fmt)
{
   unsigned i;
   Pass pass;
   D3DXMATRIX ident;
   const LinkInfo *info  = (const LinkInfo*)info_data;
   cg_renderchain_t *chain  = (cg_renderchain_t*)data;
   LPDIRECT3DDEVICE d3dr = NULL;

   if (!chain)
	   return false;
   
   d3dr = (LPDIRECT3DDEVICE)chain->dev;

   D3DXMatrixIdentity(&ident);

   d3d_set_transform(d3dr, D3DTS_WORLD, &ident);
   d3d_set_transform(d3dr, D3DTS_VIEW, &ident);

   pass.info        = *info;
   pass.last_width  = 0;
   pass.last_height = 0;

   chain->prev.ptr  = 0;

   for (i = 0; i < TEXTURES; i++)
   {
      chain->prev.last_width[i]  = 0;
      chain->prev.last_height[i] = 0;
      chain->prev.vertex_buf[i]  = d3d_vertex_buffer_new(
            d3dr, 4 * sizeof(Vertex), 0, 0, D3DPOOL_DEFAULT, NULL);

      if (!chain->prev.vertex_buf[i])
         return false;

      chain->prev.tex[i] = (LPDIRECT3DTEXTURE)d3d_texture_new(
      d3dr, NULL, info->tex_w, info->tex_h, 1, 0,
      (fmt == RETRO_PIXEL_FORMAT_RGB565) ? D3DFMT_R5G6B5 : D3DFMT_X8R8G8B8,
      D3DPOOL_MANAGED, 0, 0, 0, NULL, NULL);

      if (!chain->prev.tex[i])
         return false;

      d3d_set_texture(d3dr, 0, chain->prev.tex[i]);
      d3d_set_sampler_minfilter(d3dr, 0,
            translate_filter(info->pass->filter));
      d3d_set_sampler_magfilter(d3dr, 0,
            translate_filter(info->pass->filter));
      d3d_set_sampler_address_u(d3dr, 0, D3DTADDRESS_BORDER);
      d3d_set_sampler_address_v(d3dr, 0, D3DTADDRESS_BORDER);
      d3d_set_texture(d3dr, 0, NULL);
   }

   renderchain_compile_shaders(chain, &pass.fPrg,
         &pass.vPrg, info->pass->source.path);

   if (!cg_d3d9_renderchain_init_shader_fvf(chain, &pass))
      return false;
   chain->passes.push_back(pass);
   return true;
}

static bool cg_d3d9_renderchain_init(void *data,
      const void *_video_info,
      void *dev_,
      const void *final_viewport_,
      const void *info_data, unsigned fmt)
{
   const LinkInfo *info           = (const LinkInfo*)info_data;
   cg_renderchain_t *chain        = (cg_renderchain_t*)data;
   const video_info_t *video_info = (const video_info_t*)_video_info;

   if (!chain)
      return false;

   chain->dev            = (LPDIRECT3DDEVICE)dev_;
   chain->video_info     = video_info;
   chain->tracker        = NULL;
   chain->final_viewport = (D3DVIEWPORT*)final_viewport_;
   chain->frame_count    = 0;
   chain->pixel_size     = (fmt == RETRO_PIXEL_FORMAT_RGB565) ? 2 : 4;

   if (!renderchain_create_first_pass(chain, info, fmt))
      return false;
   renderchain_log_info(chain, info);
   if (!renderchain_compile_shaders(chain, &chain->fStock, &chain->vStock, ""))
      return false;

   return true;
}

static bool renderchain_set_pass_size(void *data, unsigned pass_index,
      unsigned width, unsigned height)
{
   cg_renderchain_t *chain  = (cg_renderchain_t*)data;
   LPDIRECT3DDEVICE d3dr = chain->dev;
   Pass *pass = (Pass*)&chain->passes[pass_index];

   if (width != pass->info.tex_w || height != pass->info.tex_h)
   {
      d3d_texture_free(pass->tex);

      pass->info.tex_w = width;
      pass->info.tex_h = height;
      pass->tex        = (LPDIRECT3DTEXTURE)d3d_texture_new(
      d3dr, NULL, width, height, 1,
         D3DUSAGE_RENDERTARGET,
         chain->passes.back().info.pass->fbo.fp_fbo ? 
         D3DFMT_A32B32G32R32F : D3DFMT_A8R8G8B8,
         D3DPOOL_DEFAULT, 0, 0, 0,
         NULL, NULL);
      
      if (!pass->tex)
         return false;

      d3d_set_texture(d3dr, 0, pass->tex);
      d3d_set_sampler_address_u(d3dr, 0, D3DTADDRESS_BORDER);
      d3d_set_sampler_address_v(d3dr, 0, D3DTADDRESS_BORDER);
      d3d_set_texture(d3dr, 0, NULL);
   }

   return true;
}

static void cg_d3d9_renderchain_convert_geometry(
	  void *data, const void *info_data,
      unsigned *out_width, unsigned *out_height,
      unsigned width, unsigned height,
      void *final_viewport_data)
{
   const LinkInfo *info        = (const LinkInfo*)info_data;
   cg_renderchain_t *chain     = (cg_renderchain_t*)data;
   D3DVIEWPORT *final_viewport = (D3DVIEWPORT*)final_viewport_data; 

   if (!chain || !info)
      return;

   switch (info->pass->fbo.type_x)
   {
      case RARCH_SCALE_VIEWPORT:
         *out_width = info->pass->fbo.scale_x * final_viewport->Width;
         break;

      case RARCH_SCALE_ABSOLUTE:
         *out_width = info->pass->fbo.abs_x;
         break;

      case RARCH_SCALE_INPUT:
         *out_width = info->pass->fbo.scale_x * width;
         break;
   }

   switch (info->pass->fbo.type_y)
   {
      case RARCH_SCALE_VIEWPORT:
         *out_height = info->pass->fbo.scale_y * final_viewport->Height;
         break;

      case RARCH_SCALE_ABSOLUTE:
         *out_height = info->pass->fbo.abs_y;
         break;

      case RARCH_SCALE_INPUT:
         *out_height = info->pass->fbo.scale_y * height;
         break;
   }
}

static void d3d_recompute_pass_sizes(d3d_video_t *d3d,
	void *renderchain_data)
{
   unsigned i;
   LinkInfo link_info                = {0};
   link_info.pass                    = &d3d->shader.pass[0];
   link_info.tex_w = link_info.tex_h = 
      d3d->video_info.input_scale * RARCH_SCALE_BASE;

   unsigned current_width            = link_info.tex_w;
   unsigned current_height           = link_info.tex_h;
   unsigned out_width                = 0;
   unsigned out_height               = 0;
   cg_renderchain_t *chain           = (cg_renderchain_t*)renderchain_data;

   if (!renderchain_set_pass_size(chain, 0,
            current_width, current_height))
   {
      RARCH_ERR("[D3D]: Failed to set pass size.\n");
      return;
   }

   for (i = 1; i < d3d->shader.passes; i++)
   {
      cg_d3d9_renderchain_convert_geometry(chain,
		    &link_info,
            &out_width, &out_height,
            current_width, current_height, &d3d->final_viewport);

      link_info.tex_w = next_pow2(out_width);
      link_info.tex_h = next_pow2(out_height);

      if (!renderchain_set_pass_size(chain, i,
               link_info.tex_w, link_info.tex_h))
      {
         RARCH_ERR("[D3D]: Failed to set pass size.\n");
         return;
      }

      current_width  = out_width;
      current_height = out_height;

      link_info.pass = &d3d->shader.pass[i];
   }
}

static void cg_d3d9_renderchain_set_final_viewport(void *data,
      void *renderchain_data, const void *viewport_data)
{
   d3d_video_t                  *d3d = (d3d_video_t*)data;
   cg_renderchain_t              *chain = (cg_renderchain_t*)renderchain_data;
   const D3DVIEWPORT *final_viewport = (const D3DVIEWPORT*)viewport_data;

   if (chain)
      chain->final_viewport = (D3DVIEWPORT*)final_viewport;

   d3d_recompute_pass_sizes(d3d, chain);
}

static bool cg_d3d9_renderchain_add_pass(void *data, const void *info_data)
{
   Pass pass;
   const LinkInfo *info     = (const LinkInfo*)info_data;
   cg_renderchain_t *chain  = (cg_renderchain_t*)data;
   LPDIRECT3DDEVICE d3dr    = (LPDIRECT3DDEVICE)chain->dev;

   pass.info                = *info;
   pass.last_width          = 0;
   pass.last_height         = 0;

   renderchain_compile_shaders(chain, &pass.fPrg, 
        &pass.vPrg, info->pass->source.path);

   if (!cg_d3d9_renderchain_init_shader_fvf(chain, &pass))
      return false;

   pass.vertex_buf = (LPDIRECT3DVERTEXBUFFER)d3d_vertex_buffer_new(d3dr, 4 * sizeof(Vertex),
	   0, 0, D3DPOOL_DEFAULT, NULL);

   if (!pass.vertex_buf)
      return false;

   pass.tex = d3d_texture_new(d3dr, NULL, info->tex_w, info->tex_h, 1,
               D3DUSAGE_RENDERTARGET,
               chain->passes.back().info.pass->fbo.fp_fbo 
               ? D3DFMT_A32B32G32R32F : D3DFMT_A8R8G8B8,
               D3DPOOL_DEFAULT, 0, 0, 0, NULL, NULL);

   if (!pass.tex)
      return false;

   d3d_set_texture(d3dr, 0, pass.tex);
   d3d_set_sampler_address_u(d3dr, 0, D3DTADDRESS_BORDER);
   d3d_set_sampler_address_v(d3dr, 0, D3DTADDRESS_BORDER);
   d3d_set_texture(d3dr, 0, NULL);

   chain->passes.push_back(pass);

   renderchain_log_info(chain, info);
   return true;
}

static bool cg_d3d9_renderchain_add_lut(void *data,
      const char *id, const char *path, bool smooth)
{
   lut_info info;
   cg_renderchain_t *chain  = (cg_renderchain_t*)data;
   LPDIRECT3DDEVICE d3dr = chain->dev;
   LPDIRECT3DTEXTURE lut = (LPDIRECT3DTEXTURE)
      d3d_texture_new(d3dr,
            path,
            D3DX_DEFAULT_NONPOW2,
            D3DX_DEFAULT_NONPOW2,
            0,
            0,
            D3DFMT_FROM_FILE,
            D3DPOOL_MANAGED,
            smooth ? D3DX_FILTER_LINEAR : D3DX_FILTER_POINT,
            0,
            0,
            NULL,
            NULL
            );

   RARCH_LOG("[D3D]: LUT texture loaded: %s.\n", path);

   info.tex    = lut;
   info.smooth = smooth;
   strcpy(info.id, id);
   if (!lut)
      return false;

   d3d_set_texture(d3dr, 0, lut);
   d3d_set_sampler_address_u(d3dr, 0, D3DTADDRESS_BORDER);
   d3d_set_sampler_address_v(d3dr, 0, D3DTADDRESS_BORDER);
   d3d_set_texture(d3dr, 0, NULL);

   chain->luts.push_back(info);
   return true;
}

static void cg_d3d9_renderchain_add_state_tracker(
      void *data, void *tracker_data)
{
   state_tracker_t *tracker = (state_tracker_t*)tracker_data;
   cg_renderchain_t     *chain = (cg_renderchain_t*)data;
   if (chain->tracker)
      state_tracker_free(chain->tracker);
   chain->tracker = tracker;
}

static void renderchain_start_render(void *data)
{
   cg_renderchain_t *chain         = (cg_renderchain_t*)data;

   if (!chain)
      return;

   chain->passes[0].tex         = chain->prev.tex[chain->prev.ptr];
   chain->passes[0].vertex_buf  = chain->prev.vertex_buf[chain->prev.ptr];
   chain->passes[0].last_width  = chain->prev.last_width[chain->prev.ptr];
   chain->passes[0].last_height = chain->prev.last_height[chain->prev.ptr];
}

static void renderchain_end_render(void *data)
{
   cg_renderchain_t *chain                     = (cg_renderchain_t*)data;

   if (!chain)
      return;

   chain->prev.last_width[chain->prev.ptr]  = chain->passes[0].last_width;
   chain->prev.last_height[chain->prev.ptr] = chain->passes[0].last_height;
   chain->prev.ptr                          = (chain->prev.ptr + 1) & TEXTURESMASK;
}

static void renderchain_set_mvp(void *data, void *vertex_program,
      unsigned vp_width, unsigned vp_height,
      unsigned rotation)
{
   D3DXMATRIX proj, ortho, rot, tmp;
   CGprogram     vPrg   = (CGprogram)vertex_program;
   cg_renderchain_t *chain = (cg_renderchain_t*)data;

   if (!chain)
      return;

   D3DXMatrixOrthoOffCenterLH(&ortho, 0, vp_width, 0, vp_height, 0, 1);
   D3DXMatrixIdentity(&rot);
   D3DXMatrixRotationZ(&rot, rotation * (M_PI / 2.0));

   D3DXMatrixMultiply(&proj, &ortho, &rot);
   D3DXMatrixTranspose(&tmp, &proj);

   renderchain_set_shader_mvp(chain, &vPrg, &tmp);
}

static void renderchain_set_vertices(
	  void *data, void *pass_data,
      unsigned width, unsigned height,
      unsigned out_width, unsigned out_height,
      unsigned vp_width, unsigned vp_height,
      unsigned rotation)
{
   Pass          *pass  = (Pass*)pass_data;
   cg_renderchain_t *chain = (cg_renderchain_t*)data;
   const LinkInfo *info = (const LinkInfo*)&pass->info;

   if (pass->last_width != width || pass->last_height != height)
   {
      Vertex vert[4];
      unsigned i;
      void *verts       = NULL;
      float _u          = float(width)  / info->tex_w;
      float _v          = float(height) / info->tex_h;

      pass->last_width  = width;
      pass->last_height = height;

      for (i = 0; i < 4; i++)
      {
         vert[i].z      = 0.5f;
         vert[i].r      = vert[i].g = vert[i].b = vert[i].a = 1.0f;
      }

      vert[0].x         = 0.0f;
      vert[1].x         = out_width;
      vert[2].x         = 0.0f;
      vert[3].x         = out_width;
      vert[0].y         = out_height;
      vert[1].y         = out_height;
      vert[2].y         = 0.0f;
      vert[3].y         = 0.0f;

      vert[0].u         = 0.0f;
      vert[1].u         = _u;
      vert[2].u         = 0.0f;
      vert[3].u         = _u;
      vert[0].v         = 0.0f;
      vert[1].v         = 0.0f;
      vert[2].v         = _v;
      vert[3].v         = _v;

      vert[0].lut_u     = 0.0f;
      vert[1].lut_u     = 1.0f;
      vert[2].lut_u     = 0.0f;
      vert[3].lut_u     = 1.0f;
      vert[0].lut_v     = 0.0f;
      vert[1].lut_v     = 0.0f;
      vert[2].lut_v     = 1.0f;
      vert[3].lut_v     = 1.0f;

      /* Align texels and vertices. */
      for (i = 0; i < 4; i++)
      {
         vert[i].x     -= 0.5f;
         vert[i].y     += 0.5f;
      }

      verts             = d3d_vertex_buffer_lock(pass->vertex_buf);
      memcpy(verts, vert, sizeof(vert));
      d3d_vertex_buffer_unlock(pass->vertex_buf);
   }

   renderchain_set_mvp(chain, pass->vPrg, vp_width, vp_height, rotation);
   renderchain_set_shader_params(chain, pass,
         width, height,
         info->tex_w, info->tex_h,
         vp_width, vp_height);
}

static void renderchain_set_viewport(void *data, void *viewport_data)
{
   LPDIRECT3DDEVICE d3dr;
   D3DVIEWPORT       *vp = (D3DVIEWPORT*)viewport_data;
   cg_renderchain_t  *chain = (cg_renderchain_t*)data;

   if (!chain)
      return;

   d3dr = (LPDIRECT3DDEVICE)chain->dev;
   d3d_set_viewport(d3dr, vp);
}

static void renderchain_blit_to_texture(void *data,
      const void *frame,
      unsigned width, unsigned height,
      unsigned pitch)
{
   D3DLOCKED_RECT d3dlr;
   cg_renderchain_t *chain = (cg_renderchain_t*)data;
   Pass *first          = (Pass*)&chain->passes[0];
   driver_t *driver     = driver_get_ptr();

   if (first->last_width != width || first->last_height != height)
   {
      d3d_lockrectangle_clear(first->tex, 0, &d3dlr, 
            NULL, first->info.tex_h, D3DLOCK_NOSYSLOCK);
   }

   d3d_texture_blit(chain->pixel_size, first->tex,
         &d3dlr, frame, width, height, pitch);
}

static void renderchain_unbind_all(void *data)
{
   unsigned i;
   cg_renderchain_t *chain  = (cg_renderchain_t*)data;
   LPDIRECT3DDEVICE d3dr = (LPDIRECT3DDEVICE)chain->dev;

   /* Have to be a bit anal about it.
    * Render targets hate it when they have filters apparently.
    */
   for (i = 0; i < chain->bound_tex.size(); i++)
   {
      d3d_set_sampler_minfilter(d3dr,
            chain->bound_tex[i], D3DTEXF_POINT);
      d3d_set_sampler_magfilter(d3dr,
            chain->bound_tex[i], D3DTEXF_POINT);
      d3d_set_texture(d3dr, chain->bound_tex[i], NULL);
   }

   for (i = 0; i < chain->bound_vert.size(); i++)
      d3d_set_stream_source(d3dr, chain->bound_vert[i], 0, 0, 0);

   chain->bound_tex.clear();
   chain->bound_vert.clear();
}

static void renderchain_render_pass(void *data, void *pass_data, unsigned pass_index)
{
   unsigned i;
   Pass           *pass  = (Pass*)pass_data;
   cg_renderchain_t *chain  = (cg_renderchain_t*)data;
   LPDIRECT3DDEVICE d3dr = (LPDIRECT3DDEVICE)chain->dev;

   renderchain_set_shaders(chain, &pass->fPrg, &pass->vPrg);

   d3d_set_texture(d3dr, 0, pass->tex);
   d3d_set_sampler_minfilter(d3dr, 0,
         translate_filter(pass->info.pass->filter));
   d3d_set_sampler_magfilter(d3dr, 0,
         translate_filter(pass->info.pass->filter));

   d3d_set_vertex_declaration(d3dr, pass->vertex_decl);
   for (i = 0; i < 4; i++)
      d3d_set_stream_source(d3dr, i,
            pass->vertex_buf, 0, sizeof(Vertex));

   renderchain_bind_orig(chain, pass);
   renderchain_bind_prev(chain, pass);
   renderchain_bind_pass(chain, pass, pass_index);
   renderchain_bind_luts(chain, pass);
   renderchain_bind_tracker(chain, pass, pass_index);

   d3d_draw_primitive(d3dr, D3DPT_TRIANGLESTRIP, 0, 2);

   /* So we don't render with linear filter into render targets,
    * which apparently looked odd (too blurry). */
   d3d_set_sampler_minfilter(d3dr, 0, D3DTEXF_POINT);
   d3d_set_sampler_magfilter(d3dr, 0, D3DTEXF_POINT);

   renderchain_unbind_all(chain);
}

static bool cg_d3d9_renderchain_render(void *chain_data, const void *data,
      unsigned width, unsigned height, unsigned pitch, unsigned rotation)
{
   Pass *last_pass;
   LPDIRECT3DSURFACE back_buffer, target;
   unsigned i, current_width, current_height, out_width = 0, out_height = 0;
   cg_renderchain_t *chain  = (cg_renderchain_t*)chain_data;
   LPDIRECT3DDEVICE d3dr = (LPDIRECT3DDEVICE)chain->dev;

   renderchain_start_render(chain);

   current_width         = width;
   current_height        = height;
   cg_d3d9_renderchain_convert_geometry(chain, &chain->passes[0].info,
         &out_width, &out_height,
         current_width, current_height, chain->final_viewport);

   renderchain_blit_to_texture(chain, data, width, height, pitch);

   /* Grab back buffer. */
   d3dr->GetRenderTarget(0, &back_buffer);

   /* In-between render target passes. */
   for (i = 0; i < chain->passes.size() - 1; i++)
   {
      D3DVIEWPORT viewport = {0};
      Pass *from_pass = (Pass*)&chain->passes[i];
      Pass *to_pass   = (Pass*)&chain->passes[i + 1];

      to_pass->tex->GetSurfaceLevel(0, &target);
      d3dr->SetRenderTarget(0, target);

      cg_d3d9_renderchain_convert_geometry(chain, &from_pass->info,
            &out_width, &out_height,
            current_width, current_height, chain->final_viewport);

      /* Clear out whole FBO. */
      viewport.Width  = to_pass->info.tex_w;
      viewport.Height = to_pass->info.tex_h;
      viewport.MinZ   = 0.0f;
      viewport.MaxZ   = 1.0f;

      d3d_set_viewport(d3dr, &viewport);
      d3d_clear(d3dr, 0, 0, D3DCLEAR_TARGET, 0, 1, 0);
      
      viewport.Width  = out_width;
      viewport.Height = out_height;
      renderchain_set_viewport(chain, &viewport);

      renderchain_set_vertices(chain, from_pass,
            current_width, current_height,
            out_width, out_height,
            out_width, out_height, 0);

      renderchain_render_pass(chain, from_pass, i + 1);

      current_width = out_width;
      current_height = out_height;
      target->Release();
   }

   /* Final pass */
   d3dr->SetRenderTarget(0, back_buffer);

   last_pass = (Pass*)&chain->passes.back();

   cg_d3d9_renderchain_convert_geometry(chain, &last_pass->info,
         &out_width, &out_height,
         current_width, current_height, chain->final_viewport);
   renderchain_set_viewport(chain, chain->final_viewport);
   renderchain_set_vertices(chain, last_pass,
            current_width, current_height,
            out_width, out_height,
            chain->final_viewport->Width, chain->final_viewport->Height,
            rotation);
   renderchain_render_pass(chain, last_pass, chain->passes.size());

   chain->frame_count++;

   back_buffer->Release();

   renderchain_end_render(chain);
   renderchain_set_shaders(chain, &chain->fStock, &chain->vStock);
   renderchain_set_mvp(chain, chain->vStock, chain->final_viewport->Width,
         chain->final_viewport->Height, 0);

   return true;
}

renderchain_driver_t cg_d3d9_renderchain = {
   cg_d3d9_renderchain_free,
   cg_d3d9_renderchain_new,
   cg_d3d9_renderchain_init_shader,
   cg_d3d9_renderchain_init_shader_fvf,
   NULL,
   cg_d3d9_renderchain_init,
   cg_d3d9_renderchain_set_final_viewport,
   cg_d3d9_renderchain_add_pass,
   cg_d3d9_renderchain_add_lut,
   cg_d3d9_renderchain_add_state_tracker,
   cg_d3d9_renderchain_render,
   cg_d3d9_renderchain_convert_geometry,
   "cg_d3d9",
};
