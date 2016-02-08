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

#include "../drivers/gl_common.h"
#include "../font_driver.h"
#include "../video_shader_driver.h"

/* TODO: Move viewport side effects to the caller: it's a source of bugs. */

#define gl_raster_font_emit(c, vx, vy) do { \
   font_vertex[     2 * (6 * i + c) + 0] = (x + (delta_x + off_x + vx * width) * scale) * inv_win_width; \
   font_vertex[     2 * (6 * i + c) + 1] = (y + (delta_y - off_y - vy * height) * scale) * inv_win_height; \
   font_tex_coords[ 2 * (6 * i + c) + 0] = (tex_x + vx * width) * inv_tex_size_x; \
   font_tex_coords[ 2 * (6 * i + c) + 1] = (tex_y + vy * height) * inv_tex_size_y; \
   font_color[      4 * (6 * i + c) + 0] = color[0]; \
   font_color[      4 * (6 * i + c) + 1] = color[1]; \
   font_color[      4 * (6 * i + c) + 2] = color[2]; \
   font_color[      4 * (6 * i + c) + 3] = color[3]; \
   font_lut_tex_coord[    2 * (6 * i + c) + 0] = gl->coords.lut_tex_coord[0]; \
   font_lut_tex_coord[    2 * (6 * i + c) + 1] = gl->coords.lut_tex_coord[1]; \
} while(0)

#define MAX_MSG_LEN_CHUNK 64

typedef struct
{
   gl_t *gl;
   GLuint tex;
   unsigned tex_width, tex_height;

   const font_renderer_driver_t *font_driver;
   void *font_data;

   gl_font_raster_block_t *block;
} gl_raster_t;

static void *gl_raster_font_init_font(void *data,
      const char *font_path, float font_size)
{
   unsigned width, height;
   uint8_t *tmp_buffer;
   const struct font_atlas *atlas = NULL;
   gl_raster_t *font = (gl_raster_t*)calloc(1, sizeof(*font));

   if (!font)
      return NULL;

   font->gl = (gl_t*)data;

   if (!font_renderer_create_default(&font->font_driver,
            &font->font_data, font_path, font_size))
   {
      RARCH_WARN("Couldn't init font renderer.\n");
      free(font);
      return NULL;
   }

   glGenTextures(1, &font->tex);
   glBindTexture(GL_TEXTURE_2D, font->tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

   atlas = font->font_driver->get_atlas(font->font_data);

   width = next_pow2(atlas->width);
   height = next_pow2(atlas->height);

   /* Ideally, we'd use single component textures, but the 
    * difference in ways to do that between core GL and GLES/legacy GL
    * is too great to bother going down that route. */
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height,
         0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   tmp_buffer = (uint8_t*)malloc(atlas->width * atlas->height * 4);

   if (tmp_buffer)
   {
      unsigned i;
      uint8_t       *dst = tmp_buffer;
      const uint8_t *src = atlas->buffer;

      for (i = 0; i < atlas->width * atlas->height; i++)
      {
         *dst++ = 0xff;
         *dst++ = 0xff;
         *dst++ = 0xff;
         *dst++ = *src++;
      }

      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, atlas->width,
            atlas->height, GL_RGBA, GL_UNSIGNED_BYTE, tmp_buffer);
      free(tmp_buffer);
   }

   font->tex_width  = width;
   font->tex_height = height;

   glBindTexture(GL_TEXTURE_2D, font->gl->texture[font->gl->tex_index]);

   return font;
}

static void gl_raster_font_free_font(void *data)
{
   gl_raster_t *font = (gl_raster_t*)data;
   if (!font)
      return;

   if (font->font_driver && font->font_data)
      font->font_driver->free(font->font_data);

   glDeleteTextures(1, &font->tex);
   free(font);
}

static int gl_get_message_width(void *data, const char *msg, unsigned msg_len_full, float scale)
{
   gl_raster_t *font = (gl_raster_t*)data;
      
   unsigned i;
   unsigned msg_len        = min(msg_len_full, MAX_MSG_LEN_CHUNK);
   int      delta_x        = 0;

   if (!font)
      return 0;

   while (msg_len_full)
   {
      for (i = 0; i < msg_len; i++)
      {
         const struct font_glyph *glyph = 
            font->font_driver->get_glyph(font->font_data, (uint8_t)msg[i]);
         if (!glyph) /* Do something smarter here ... */
            glyph = font->font_driver->get_glyph(font->font_data, '?');
         if (!glyph)
            continue;

         delta_x += glyph->advance_x;
      }

      msg_len_full -= msg_len;
      msg          += msg_len;
      msg_len       = min(msg_len_full, MAX_MSG_LEN_CHUNK);
   }

   return delta_x * scale;
}

static void gl_raster_font_draw_vertices(gl_t *gl, const gl_coords_t *coords)
{
   if (!gl)
      return;

   gl->shader->set_coords(coords);
   gl->shader->set_mvp(gl, &gl->mvp_no_rot);

   glDrawArrays(GL_TRIANGLES, 0, coords->vertices);
}

static void gl_raster_font_render_line(
      gl_raster_t *font, const char *msg, unsigned msg_len_full,
      GLfloat scale, const GLfloat color[4], GLfloat pos_x,
      GLfloat pos_y, unsigned text_align)
{
   int x, y, delta_x, delta_y;
   float inv_tex_size_x, inv_tex_size_y, inv_win_width, inv_win_height;
   unsigned i, msg_len;
   GLfloat font_tex_coords[2 * 6 * MAX_MSG_LEN_CHUNK];
   GLfloat font_vertex[2 * 6 * MAX_MSG_LEN_CHUNK]; 
   GLfloat font_color[4 * 6 * MAX_MSG_LEN_CHUNK];
   GLfloat font_lut_tex_coord[2 * 6 * MAX_MSG_LEN_CHUNK];
   struct gl_coords coords;
   gl_t *gl       = font ? font->gl : NULL;

   if (!gl)
      return;

   msg_len        = min(msg_len_full, MAX_MSG_LEN_CHUNK);

   x              = roundf(pos_x * gl->vp.width);
   y              = roundf(pos_y * gl->vp.height);
   delta_x        = 0;
   delta_y        = 0;

   switch (text_align)
   {
      case TEXT_ALIGN_RIGHT:
         x -= gl_get_message_width(font, msg, msg_len_full, scale);
         break;
      case TEXT_ALIGN_CENTER:
         x -= gl_get_message_width(font, msg, msg_len_full, scale) / 2.0;
         break;
   }

   inv_tex_size_x = 1.0f / font->tex_width;
   inv_tex_size_y = 1.0f / font->tex_height;
   inv_win_width  = 1.0f / font->gl->vp.width;
   inv_win_height = 1.0f / font->gl->vp.height;

   while (msg_len_full)
   {
      for (i = 0; i < msg_len; i++)
      {
         int off_x, off_y, tex_x, tex_y, width, height;
         const struct font_glyph *glyph =
            font->font_driver->get_glyph(font->font_data, (uint8_t)msg[i]);

         if (!glyph) /* Do something smarter here ... */
            glyph = font->font_driver->get_glyph(font->font_data, '?');
         if (!glyph)
            continue;

         off_x  = glyph->draw_offset_x;
         off_y  = glyph->draw_offset_y;
         tex_x  = glyph->atlas_offset_x;
         tex_y  = glyph->atlas_offset_y;
         width  = glyph->width;
         height = glyph->height;

         gl_raster_font_emit(0, 0, 1); /* Bottom-left */
         gl_raster_font_emit(1, 1, 1); /* Bottom-right */
         gl_raster_font_emit(2, 0, 0); /* Top-left */

         gl_raster_font_emit(3, 1, 0); /* Top-right */
         gl_raster_font_emit(4, 0, 0); /* Top-left */
         gl_raster_font_emit(5, 1, 1); /* Bottom-right */

         delta_x += glyph->advance_x;
         delta_y -= glyph->advance_y;
      }

      coords.tex_coord     = font_tex_coords;
      coords.vertex        = font_vertex;
      coords.color         = font_color;
      coords.vertices      = 6 * msg_len;
      coords.lut_tex_coord = font_lut_tex_coord;

      if (font->block)
         gl_coord_array_add(&font->block->carr, &coords, coords.vertices);
      else
         gl_raster_font_draw_vertices(gl, &coords);

      msg_len_full -= msg_len;
      msg          += msg_len;
      msg_len       = min(msg_len_full, MAX_MSG_LEN_CHUNK);
   }
}

static void gl_raster_font_render_message(
      gl_raster_t *font, const char *msg, GLfloat scale,
      const GLfloat color[4], GLfloat pos_x, GLfloat pos_y,
      unsigned text_align)
{
   int lines = 0;
   float line_height;

   if (!msg || !*msg || !font->gl)
      return;

   /* If the font height is not supported just draw as usual */
   if (!font->font_driver->get_line_height)
   {
      gl_raster_font_render_line(font, msg, strlen(msg), scale, color, pos_x, pos_y, text_align);
      return;
   }

   line_height = scale * 1/(float)font->font_driver->get_line_height(font->font_data);

   for (;;)
   {
      const char *delim = strchr(msg, '\n');

      /* Draw the line */
      if (delim)
      {
         unsigned msg_len = delim - msg;
         gl_raster_font_render_line(font, msg, msg_len, scale, color, pos_x, pos_y - (float)lines*line_height, text_align);
         msg += msg_len + 1;
         lines++;
      }
      else
      {
         unsigned msg_len = strlen(msg);
         gl_raster_font_render_line(font, msg, msg_len, scale, color, pos_x, pos_y - (float)lines*line_height, text_align);
         break;
      }
   }
}

static void gl_raster_font_setup_viewport(gl_raster_t *font, bool full_screen)
{
   unsigned width, height;
   gl_t         *gl = font->gl;

   if (!gl)
      return;

   video_driver_get_size(&width, &height);

   video_driver_set_viewport(width, height, full_screen, false);

   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glBlendEquation(GL_FUNC_ADD);

   glBindTexture(GL_TEXTURE_2D, font->tex);

   if (gl->shader && gl->shader->use)
      gl->shader->use(gl, GL_SHADER_STOCK_BLEND);
}

static void gl_raster_font_restore_viewport(gl_t *gl)
{
   unsigned width, height;

   video_driver_get_size(&width, &height);

   glBindTexture(GL_TEXTURE_2D, gl->texture[gl->tex_index]);

   glDisable(GL_BLEND);
   video_driver_set_viewport(width, height, false, true);
}

static void gl_raster_font_render_msg(void *data, const char *msg,
      const void *userdata)
{
   GLfloat x, y, scale, drop_mod;
   GLfloat color[4], color_dark[4];
   int drop_x, drop_y;
   bool full_screen;
   enum text_alignment text_align;
   gl_t *gl = NULL;
   gl_raster_t *font = (gl_raster_t*)data;
   settings_t *settings = config_get_ptr();
   const struct font_params *params = (const struct font_params*)userdata;

   if (!font || !msg || !*msg)
      return;

   gl = font->gl;

   if (params)
   {
      x           = params->x;
      y           = params->y;
      scale       = params->scale;
      full_screen = params->full_screen;
      text_align  = params->text_align;
      drop_x      = params->drop_x;
      drop_y      = params->drop_y;
      drop_mod    = params->drop_mod;

      color[0]    = FONT_COLOR_GET_RED(params->color) / 255.0f;
      color[1]    = FONT_COLOR_GET_GREEN(params->color) / 255.0f;
      color[2]    = FONT_COLOR_GET_BLUE(params->color) / 255.0f;
      color[3]    = FONT_COLOR_GET_ALPHA(params->color) / 255.0f;

      /* If alpha is 0.0f, turn it into default 1.0f */
      if (color[3] <= 0.0f)
         color[3] = 1.0f;
   }
   else
   {
      x           = settings->video.msg_pos_x;
      y           = settings->video.msg_pos_y;
      scale       = 1.0f;
      full_screen = false;
      text_align  = TEXT_ALIGN_LEFT;

      color[0]    = settings->video.msg_color_r;
      color[1]    = settings->video.msg_color_g;
      color[2]    = settings->video.msg_color_b;
      color[3] = 1.0f;

      drop_x = -2;
      drop_y = -2;
      drop_mod = 0.3f;
   }

   if (font->block)
      font->block->fullscreen = full_screen;
   else
      gl_raster_font_setup_viewport(font, full_screen);

   if (drop_x || drop_y)
   {
      color_dark[0] = color[0] * drop_mod;
      color_dark[1] = color[1] * drop_mod;
      color_dark[2] = color[2] * drop_mod;
      color_dark[3] = color[3];

      gl_raster_font_render_message(font, msg, scale, color_dark,
            x + scale * drop_x / gl->vp.width, y + 
            scale * drop_y / gl->vp.height, text_align);
   }

   gl_raster_font_render_message(font, msg, scale, color, x, y, text_align);

   if (!font->block)
      gl_raster_font_restore_viewport(gl);
}

static const struct font_glyph *gl_raster_font_get_glyph(
      void *data, uint32_t code)
{
   gl_raster_t *font = (gl_raster_t*)data;

   if (!font || !font->font_driver)
      return NULL;
   if (!font->font_driver->ident)
       return NULL;
   return font->font_driver->get_glyph((void*)font->font_driver, code);
}

static void gl_raster_font_flush_block(void *data)
{
   gl_raster_t       *font       = (gl_raster_t*)data;
   gl_font_raster_block_t *block = font->block;

   if (font && block->carr.coords.vertices)
   {
      gl_raster_font_setup_viewport(font, block->fullscreen);
      gl_raster_font_draw_vertices(font->gl, (gl_coords_t*)&block->carr.coords);
      gl_raster_font_restore_viewport(font->gl);
   }
}

static void gl_raster_font_bind_block(void *data, void *userdata)
{
   gl_raster_t *font = (gl_raster_t*)data;
   gl_font_raster_block_t *block = (gl_font_raster_block_t*)userdata;

   if (font)
      font->block = block;
}

font_renderer_t gl_raster_font = {
   gl_raster_font_init_font,
   gl_raster_font_free_font,
   gl_raster_font_render_msg,
   "GL raster",
   gl_raster_font_get_glyph,
   gl_raster_font_bind_block,
   gl_raster_font_flush_block,
   gl_get_message_width
};
