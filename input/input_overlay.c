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

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include <compat/posix_string.h>
#include <file/file_path.h>
#include <string/string_list.h>
#include <clamping.h>
#include <rhash.h>

#include "input_overlay.h"
#include "../driver.h"
#include "input_common.h"

#define BOX_RADIAL       0x18df06d2U
#define BOX_RECT         0x7c9d4d93U

#define KEY_ANALOG_LEFT  0x56b92e81U
#define KEY_ANALOG_RIGHT 0x2e4dc654U

/**
 * input_overlay_scale:
 * @ol                    : Overlay handle.
 * @scale                 : Scaling factor.
 *
 * Scales overlay and all its associated descriptors
 * by a given scaling factor (@scale).
 **/
static void input_overlay_scale(struct overlay *ol, float scale)
{
   size_t i;

   if (!ol)
      return;

   if (ol->block_scale)
      scale = 1.0f;

   ol->scale = scale;
   ol->mod_w = ol->w * scale;
   ol->mod_h = ol->h * scale;
   ol->mod_x = ol->center_x +
      (ol->x - ol->center_x) * scale;
   ol->mod_y = ol->center_y +
      (ol->y - ol->center_y) * scale;

   for (i = 0; i < ol->size; i++)
   {
      float scale_w, scale_h, adj_center_x, adj_center_y;
      struct overlay_desc *desc = &ol->descs[i];

      if (!desc)
         continue;

      scale_w = ol->mod_w * desc->range_x;
      scale_h = ol->mod_h * desc->range_y;

      desc->mod_w = 2.0f * scale_w;
      desc->mod_h = 2.0f * scale_h;

      adj_center_x = ol->mod_x + desc->x * ol->mod_w;
      adj_center_y = ol->mod_y + desc->y * ol->mod_h;
      desc->mod_x = adj_center_x - scale_w;
      desc->mod_y = adj_center_y - scale_h;
   }
}

static void input_overlay_set_vertex_geom(input_overlay_t *ol)
{
   size_t i;

   if (!ol)
      return;
   if (ol->active->image.pixels)
      ol->iface->vertex_geom(ol->iface_data, 0,
            ol->active->mod_x, ol->active->mod_y,
            ol->active->mod_w, ol->active->mod_h);

   for (i = 0; i < ol->active->size; i++)
   {
      struct overlay_desc *desc = &ol->active->descs[i];

      if (!desc)
         continue;

      if (!desc->image.pixels)
         continue;

      ol->iface->vertex_geom(ol->iface_data, desc->image_index,
            desc->mod_x, desc->mod_y, desc->mod_w, desc->mod_h);
   }
}

/**
 * input_overlay_set_scale_factor:
 * @ol                    : Overlay handle.
 * @scale                 : Factor of scale to apply.
 *
 * Scales the overlay by a factor of scale.
 **/
void input_overlay_set_scale_factor(input_overlay_t *ol, float scale)
{
   size_t i;

   if (!ol)
      return;

   for (i = 0; i < ol->size; i++)
      input_overlay_scale(&ol->overlays[i], scale);

   input_overlay_set_vertex_geom(ol);
}

static void input_overlay_free_overlay(struct overlay *overlay)
{
   size_t i;

   if (!overlay)
      return;

   for (i = 0; i < overlay->size; i++)
      texture_image_free(&overlay->descs[i].image);

   free(overlay->load_images);
   free(overlay->descs);
   texture_image_free(&overlay->image);
}

static void input_overlay_free_overlays(input_overlay_t *ol)
{
   size_t i;

   if (!ol)
      return;

   for (i = 0; i < ol->size; i++)
      input_overlay_free_overlay(&ol->overlays[i]);

   free(ol->overlays);
}

static bool input_overlay_load_texture_image(struct overlay *overlay,
      struct texture_image *image, const char *path)
{
   struct texture_image img = {0};

   if (!texture_image_load(&img, path))
      return false;

   *image = img;
   overlay->load_images[overlay->load_images_size++] = *image;
   
   return true;
}

static bool input_overlay_load_desc_image(input_overlay_t *ol,
      struct overlay_desc *desc,
      struct overlay *input_overlay,
      unsigned ol_idx, unsigned desc_idx)
{
   char overlay_desc_image_key[64]  = {0};
   char image_path[PATH_MAX_LENGTH] = {0};
 
   snprintf(overlay_desc_image_key, sizeof(overlay_desc_image_key),
         "overlay%u_desc%u_overlay", ol_idx, desc_idx);

   if (config_get_path(ol->conf, overlay_desc_image_key,
            image_path, sizeof(image_path)))
   {
      char path[PATH_MAX_LENGTH] = {0};
      fill_pathname_resolve_relative(path, ol->overlay_path,
            image_path, sizeof(path));

      if (input_overlay_load_texture_image(input_overlay, &desc->image, path))
         desc->image_index = input_overlay->load_images_size - 1;
   }

   input_overlay->pos ++;

   return true;
}

static bool input_overlay_load_desc(input_overlay_t *ol,
      struct overlay_desc *desc,
      struct overlay *input_overlay,
      unsigned ol_idx, unsigned desc_idx,
      unsigned width, unsigned height,
      bool normalized, float alpha_mod, float range_mod)
{
   float width_mod, height_mod;
   uint32_t box_hash, key_hash;
   bool ret                             = true;
   bool by_pixel                        = false;
   char overlay_desc_key[64]            = {0};
   char conf_key[64]                    = {0};
   char overlay_desc_normalized_key[64] = {0};
   char overlay[256]                    = {0};
   char *save                           = NULL;
   char *key                            = NULL;
   struct string_list *list             = NULL;
   const char *x                        = NULL;
   const char *y                        = NULL;
   const char *box                      = NULL;

   snprintf(overlay_desc_key, sizeof(overlay_desc_key),
         "overlay%u_desc%u", ol_idx, desc_idx);

   snprintf(overlay_desc_normalized_key, sizeof(overlay_desc_normalized_key),
         "overlay%u_desc%u_normalized", ol_idx, desc_idx);
   config_get_bool(ol->conf, overlay_desc_normalized_key, &normalized);

   by_pixel = !normalized;

   if (by_pixel && (width == 0 || height == 0))
   {
      RARCH_ERR("[Overlay]: Base overlay is not set and not using normalized coordinates.\n");
      return false;
   }

   if (!config_get_array(ol->conf, overlay_desc_key, overlay, sizeof(overlay)))
   {
      RARCH_ERR("[Overlay]: Didn't find key: %s.\n", overlay_desc_key);
      return false;
   }

   list = string_split(overlay, ", ");

   if (!list)
   {
      RARCH_ERR("[Overlay]: Failed to split overlay desc.\n");
      return false;
   }

   if (list->size < 6)
   {
      string_list_free(list);
      RARCH_ERR("[Overlay]: Overlay desc is invalid. Requires at least 6 tokens.\n");
      return false;
   }

   key = list->elems[0].data;
   x   = list->elems[1].data;
   y   = list->elems[2].data;
   box = list->elems[3].data;

   box_hash = djb2_calculate(box);
   key_hash = djb2_calculate(key);

   desc->key_mask = 0;

   switch (key_hash)
   {
      case KEY_ANALOG_LEFT:
         desc->type = OVERLAY_TYPE_ANALOG_LEFT;
         break;
      case KEY_ANALOG_RIGHT:
         desc->type = OVERLAY_TYPE_ANALOG_RIGHT;
         break;
      default:
         if (strstr(key, "retrok_") == key)
         {
            desc->type = OVERLAY_TYPE_KEYBOARD;
            desc->key_mask = input_translate_str_to_rk(key + 7);
         }
         else
         {
            const char *tmp = NULL;

            desc->type = OVERLAY_TYPE_BUTTONS;
            for (tmp = strtok_r(key, "|", &save); tmp; tmp = strtok_r(NULL, "|", &save))
            {
               if (strcmp(tmp, "nul") != 0)
                  desc->key_mask |= UINT64_C(1) << input_translate_str_to_bind_id(tmp);
            }

            if (desc->key_mask & (UINT64_C(1) << RARCH_OVERLAY_NEXT))
            {
               char overlay_target_key[64] = {0};

               snprintf(overlay_target_key, sizeof(overlay_target_key),
                     "overlay%u_desc%u_next_target", ol_idx, desc_idx);
               config_get_array(ol->conf, overlay_target_key,
                     desc->next_index_name, sizeof(desc->next_index_name));
            }
         }
         break;
   }

   width_mod  = 1.0f;
   height_mod = 1.0f;
   
   if (by_pixel)
   {
      width_mod  /= width;
      height_mod /= height;
   }

   desc->x = (float)strtod(x, NULL) * width_mod;
   desc->y = (float)strtod(y, NULL) * height_mod;

   switch (box_hash)
   {
      case BOX_RADIAL:
         desc->hitbox = OVERLAY_HITBOX_RADIAL;
         break;
      case BOX_RECT:
         desc->hitbox = OVERLAY_HITBOX_RECT;
         break;
      default:
         RARCH_ERR("[Overlay]: Hitbox type (%s) is invalid. Use \"radial\" or \"rect\".\n", box);
         ret = false;
         goto end;
   }

   switch (desc->type)
   {
      case OVERLAY_TYPE_ANALOG_LEFT:
      case OVERLAY_TYPE_ANALOG_RIGHT:
         {
            char overlay_analog_saturate_key[64] = {0};

            if (desc->hitbox != OVERLAY_HITBOX_RADIAL)
            {
               RARCH_ERR("[Overlay]: Analog hitbox type must be \"radial\".\n");
               ret = false;
               goto end;
            }

            snprintf(overlay_analog_saturate_key, sizeof(overlay_analog_saturate_key),
                  "overlay%u_desc%u_saturate_pct", ol_idx, desc_idx);
            if (!config_get_float(ol->conf, overlay_analog_saturate_key,
                     &desc->analog_saturate_pct))
               desc->analog_saturate_pct = 1.0f;
         }
         break;
      default:
         /* OVERLAY_TYPE_BUTTONS  - unhandled */
         /* OVERLAY_TYPE_KEYBOARD - unhandled */
         break;
   }

   desc->range_x = (float)strtod(list->elems[4].data, NULL) * width_mod;
   desc->range_y = (float)strtod(list->elems[5].data, NULL) * height_mod;

   desc->mod_x   = desc->x - desc->range_x;
   desc->mod_w   = 2.0f * desc->range_x;
   desc->mod_y   = desc->y - desc->range_y;
   desc->mod_h   = 2.0f * desc->range_y;

   snprintf(conf_key, sizeof(conf_key),
         "overlay%u_desc%u_alpha_mod", ol_idx, desc_idx);
   desc->alpha_mod = alpha_mod;
   config_get_float(ol->conf, conf_key, &desc->alpha_mod);

   snprintf(conf_key, sizeof(conf_key),
         "overlay%u_desc%u_range_mod", ol_idx, desc_idx);
   desc->range_mod = range_mod;
   config_get_float(ol->conf, conf_key, &desc->range_mod);

   snprintf(conf_key, sizeof(conf_key),
         "overlay%u_desc%u_movable", ol_idx, desc_idx);
   desc->movable = false;
   desc->delta_x = 0.0f;
   desc->delta_y = 0.0f;
   config_get_bool(ol->conf, conf_key, &desc->movable);

   desc->range_x_mod = desc->range_x;
   desc->range_y_mod = desc->range_y;

   input_overlay->pos ++;

end:
   if (list)
      string_list_free(list);
   return ret;
}

static ssize_t input_overlay_find_index(const struct overlay *ol,
      const char *name, size_t size)
{
   size_t i;

   if (!ol)
      return -1;

   for (i = 0; i < size; i++)
   {
      if (!strcmp(ol[i].name, name))
         return i;
   }

   return -1;
}

static bool input_overlay_resolve_targets(struct overlay *ol,
      size_t idx, size_t size)
{
   size_t i;
   struct overlay *current = NULL;

   if (!ol)
      return false;
   
   current = (struct overlay*)&ol[idx];

   for (i = 0; i < current->size; i++)
   {
      const char *next = current->descs[i].next_index_name;

      if (*next)
      {
         ssize_t next_idx = input_overlay_find_index(ol, next, size);

         if (next_idx < 0)
         {
            RARCH_ERR("[Overlay]: Couldn't find overlay called: \"%s\".\n",
                  next);
            return false;
         }

         current->descs[i].next_index = next_idx;
      }
      else
         current->descs[i].next_index = (idx + 1) % size;
   }

   return true;
}

static void input_overlay_load_active(input_overlay_t *ol,
      float opacity)
{
   if (!ol)
      return;

   ol->iface->load(ol->iface_data, ol->active->load_images,
         ol->active->load_images_size);

   input_overlay_set_alpha_mod(ol, opacity);
   input_overlay_set_vertex_geom(ol);
   ol->iface->full_screen(ol->iface_data, ol->active->full_screen);
}

bool input_overlay_load_overlays_resolve_iterate(input_overlay_t *ol)
{
   bool not_done = true;

   if (!ol)
      return false;

   not_done = ol->resolve_pos < ol->size;

   if (!not_done)
   {
      ol->state = OVERLAY_STATUS_DEFERRED_DONE;
      return true;
   }

   if (!input_overlay_resolve_targets(ol->overlays, ol->resolve_pos, ol->size))
   {
      RARCH_ERR("[Overlay]: Failed to resolve next targets.\n");
      goto error;
   }

   if (ol->resolve_pos == 0)
   {
      ol->active = &ol->overlays[0];

      input_overlay_load_active(ol, ol->deferred.opacity);
      input_overlay_enable(ol, ol->deferred.enable);
   }

   ol->resolve_pos += 1;

   return true;
error:
   ol->state = OVERLAY_STATUS_DEFERRED_ERROR;

   return false;
}


static bool input_overlay_load_overlay_image_done(struct overlay *overlay)
{
   overlay->pos = 0;
   /* Divide iteration steps by half of total descs if size is even,
    * otherwise default to 8 (arbitrary value for now to speed things up). */
   overlay->pos_increment = (overlay->size / 2) ? (overlay->size / 2) : 8;

#if 0
   RARCH_LOG("pos increment: %u\n", overlay->pos_increment);
#endif

   return true;
}

bool input_overlay_load_overlays_iterate(input_overlay_t *ol)
{
   size_t i                = 0;
   bool not_done           = true;
   struct overlay *overlay = NULL;
   
   if (!ol)
      return false;

   overlay = &ol->overlays[ol->pos];

   not_done = ol->pos < ol->size;

   if (!not_done)
   {
      ol->state = OVERLAY_STATUS_DEFERRED_LOADING_RESOLVE;
      return true;
   }

   switch (ol->loading_status)
   {
      case OVERLAY_IMAGE_TRANSFER_NONE:
      case OVERLAY_IMAGE_TRANSFER_BUSY:
         ol->loading_status = OVERLAY_IMAGE_TRANSFER_DONE;
         break;
      case OVERLAY_IMAGE_TRANSFER_DONE:
         input_overlay_load_overlay_image_done(&ol->overlays[ol->pos]);
         ol->loading_status = OVERLAY_IMAGE_TRANSFER_DESC_IMAGE_ITERATE;
         ol->overlays[ol->pos].pos = 0;
         break;
      case OVERLAY_IMAGE_TRANSFER_DESC_IMAGE_ITERATE:
         for (i = 0; i < overlay->pos_increment; i++)
         {
            if (overlay->pos < overlay->size)
            {
               input_overlay_load_desc_image(ol,
                     &overlay->descs[overlay->pos], overlay,
                     ol->pos, overlay->pos);
            }
            else
            {
               overlay->pos       = 0;
               ol->loading_status = OVERLAY_IMAGE_TRANSFER_DESC_ITERATE;
               break;
            }

         }
         break;
      case OVERLAY_IMAGE_TRANSFER_DESC_ITERATE:
         for (i = 0; i < overlay->pos_increment; i++)
         {
            if (overlay->pos < overlay->size)
            {
               if (!input_overlay_load_desc(ol, &overlay->descs[overlay->pos], overlay,
                        ol->pos, overlay->pos,
                        overlay->image.width, overlay->image.height,
                        overlay->config.normalized,
                        overlay->config.alpha_mod, overlay->config.range_mod))
               {
                  RARCH_ERR("[Overlay]: Failed to load overlay descs for overlay #%u.\n",
                        (unsigned)overlay->pos);
                  goto error;
               }

            }
            else
            {
               overlay->pos       = 0;
               ol->loading_status = OVERLAY_IMAGE_TRANSFER_DESC_DONE;
               break;
            }

         }
         break;
      case OVERLAY_IMAGE_TRANSFER_DESC_DONE:
         if (ol->pos == 0)
            input_overlay_load_overlays_resolve_iterate(ol);
         ol->pos += 1;
         ol->loading_status = OVERLAY_IMAGE_TRANSFER_NONE;
         break;
      case OVERLAY_IMAGE_TRANSFER_ERROR:
         goto error;
   }

   return true;

error:
   ol->state = OVERLAY_STATUS_DEFERRED_ERROR;

   return false;
}


bool input_overlay_load_overlays(input_overlay_t *ol)
{
   unsigned i;

   for (i = 0; i < ol->pos_increment; i++, ol->pos++)
   {
      char conf_key[64]                = {0};
      char overlay_full_screen_key[64] = {0};
      struct overlay          *overlay = NULL;
      bool                     to_cont = ol->pos < ol->size;
      
      if (!to_cont)
      {
         ol->pos   = 0;
         ol->state = OVERLAY_STATUS_DEFERRED_LOADING;
         break;
      }

      overlay = &ol->overlays[ol->pos];

      if (!overlay)
         continue;

      snprintf(overlay->config.descs.key,
            sizeof(overlay->config.descs.key), "overlay%u_descs", ol->pos);

      if (!config_get_uint(ol->conf, overlay->config.descs.key, &overlay->config.descs.size))
      {
         RARCH_ERR("[Overlay]: Failed to read number of descs from config key: %s.\n",
               overlay->config.descs.key);
         goto error;
      }

      overlay->descs = (struct overlay_desc*)
         calloc(overlay->config.descs.size, sizeof(*overlay->descs));

      if (!overlay->descs)
      {
         RARCH_ERR("[Overlay]: Failed to allocate descs.\n");
         goto error;
      }

      overlay->size = overlay->config.descs.size;

      snprintf(overlay_full_screen_key, sizeof(overlay_full_screen_key),
            "overlay%u_full_screen", ol->pos);
      overlay->full_screen = false;
      config_get_bool(ol->conf, overlay_full_screen_key, &overlay->full_screen);

      overlay->config.normalized = false;
      overlay->config.alpha_mod  = 1.0f;
      overlay->config.range_mod  = 1.0f;

      snprintf(conf_key, sizeof(conf_key),
            "overlay%u_normalized", ol->pos);
      config_get_bool(ol->conf, conf_key, &overlay->config.normalized);

      snprintf(conf_key, sizeof(conf_key), "overlay%u_alpha_mod", ol->pos);
      config_get_float(ol->conf, conf_key, &overlay->config.alpha_mod);

      snprintf(conf_key, sizeof(conf_key), "overlay%u_range_mod", ol->pos);
      config_get_float(ol->conf, conf_key, &overlay->config.range_mod);

      /* Precache load image array for simplicity. */
      overlay->load_images = (struct texture_image*)
         calloc(1 + overlay->size, sizeof(struct texture_image));

      if (!overlay->load_images)
      {
         RARCH_ERR("[Overlay]: Failed to allocate load_images.\n");
         goto error;
      }

      snprintf(overlay->config.paths.key, sizeof(overlay->config.paths.key),
            "overlay%u_overlay", ol->pos);

      config_get_path(ol->conf, overlay->config.paths.key,
               overlay->config.paths.path, sizeof(overlay->config.paths.path));

      if (overlay->config.paths.path[0] != '\0')
      {
         char overlay_resolved_path[PATH_MAX_LENGTH] = {0};

         fill_pathname_resolve_relative(overlay_resolved_path, ol->overlay_path,
               overlay->config.paths.path, sizeof(overlay_resolved_path));

         if (!input_overlay_load_texture_image(overlay, &overlay->image, overlay_resolved_path))
         {
            RARCH_ERR("[Overlay]: Failed to load image: %s.\n",
                  overlay_resolved_path);
            ol->loading_status = OVERLAY_IMAGE_TRANSFER_ERROR;
            goto error;
         }

      }

      snprintf(overlay->config.names.key, sizeof(overlay->config.names.key),
            "overlay%u_name", ol->pos);
      config_get_array(ol->conf, overlay->config.names.key,
            overlay->name, sizeof(overlay->name));

      /* By default, we stretch the overlay out in full. */
      overlay->x = overlay->y = 0.0f;
      overlay->w = overlay->h = 1.0f;

      snprintf(overlay->config.rect.key, sizeof(overlay->config.rect.key),
            "overlay%u_rect", ol->pos);

      if (config_get_array(ol->conf, overlay->config.rect.key,
               overlay->config.rect.array, sizeof(overlay->config.rect.array)))
      {
         struct string_list *list = string_split(overlay->config.rect.array, ", ");

         if (!list || list->size < 4)
         {
            RARCH_ERR("[Overlay]: Failed to split rect \"%s\" into at least four tokens.\n",
                  overlay->config.rect.array);
            string_list_free(list);
            goto error;
         }

         overlay->x = (float)strtod(list->elems[0].data, NULL);
         overlay->y = (float)strtod(list->elems[1].data, NULL);
         overlay->w = (float)strtod(list->elems[2].data, NULL);
         overlay->h = (float)strtod(list->elems[3].data, NULL);
         string_list_free(list);
      }

      /* Assume for now that scaling center is in the middle.
       * TODO: Make this configurable. */
      overlay->block_scale = false;
      overlay->center_x = overlay->x + 0.5f * overlay->w;
      overlay->center_y = overlay->y + 0.5f * overlay->h;
   }


   return true;

error:
   ol->pos   = 0;
   ol->state = OVERLAY_STATUS_DEFERRED_ERROR;

   return false;
}


bool input_overlay_new_done(input_overlay_t *ol)
{
   if (!ol)
      return false;

   input_overlay_set_alpha_mod(ol, ol->deferred.opacity);
   input_overlay_set_scale_factor(ol, ol->deferred.scale_factor);
   ol->next_index = (ol->index + 1) % ol->size;

   ol->state = OVERLAY_STATUS_ALIVE;

   if (ol->conf)
      config_file_free(ol->conf);
   ol->conf = NULL;

   return true;
}

static bool input_overlay_load_overlays_init(input_overlay_t *ol)
{
   if (!config_get_uint(ol->conf, "overlays", &ol->config.overlays.size))
   {
      RARCH_ERR("overlays variable not defined in config.\n");
      goto error;
   }

   if (!ol->config.overlays.size)
      goto error;

   ol->overlays = (struct overlay*)calloc(
         ol->config.overlays.size, sizeof(*ol->overlays));
   if (!ol->overlays)
      goto error;

   ol->size          = ol->config.overlays.size;
   ol->pos           = 0;
   ol->resolve_pos   = 0;
   ol->pos_increment = (ol->size / 4) ? (ol->size / 4) : 4;

   return true;

error:
   ol->state = OVERLAY_STATUS_DEFERRED_ERROR;

   return false;
}

/**
 * input_overlay_new:
 * @path                  : Path to overlay file.
 * @enable                : Enable the overlay after initializing it?
 *
 * Creates and initializes an overlay handle.
 *
 * Returns: Overlay handle on success, otherwise NULL.
 **/
input_overlay_t *input_overlay_new(const char *path, bool enable,
      float opacity, float scale_factor)
{
   input_overlay_t *ol = (input_overlay_t*)calloc(1, sizeof(*ol));
   driver_t *driver    = driver_get_ptr();

   if (!ol)
      goto error;

   ol->overlay_path    = strdup(path);
   if (!ol->overlay_path)
   {
      free(ol);
      return NULL;
   }

   ol->conf            = config_file_new(ol->overlay_path);

   if (!ol->conf)
      goto error;

   if (!video_driver_overlay_interface(&ol->iface))
   {
      RARCH_ERR("Overlay interface is not present in video driver.\n");
      goto error;
   }

   ol->iface_data = driver->video_data;

   if (!ol->iface)
      goto error;

   ol->state                 = OVERLAY_STATUS_DEFERRED_LOAD;
   ol->deferred.enable       = enable;
   ol->deferred.opacity      = opacity;
   ol->deferred.scale_factor = scale_factor;
   ol->pos                   = 0;

   input_overlay_load_overlays_init(ol);

   return ol;

error:
   input_overlay_free(ol);
   return NULL;
}

/**
 * input_overlay_enable:
 * @ol                    : Overlay handle.
 * @enable                : Enable or disable the overlay
 *
 * Enable or disable the overlay.
 **/
void input_overlay_enable(input_overlay_t *ol, bool enable)
{
   if (!ol)
      return;
   ol->enable = enable;
   ol->iface->enable(ol->iface_data, enable);
}

/**
 * inside_hitbox:
 * @desc                  : Overlay descriptor handle.
 * @x                     : X coordinate value.
 * @y                     : Y coordinate value.
 *
 * Check whether the given @x and @y coordinates of the overlay
 * descriptor @desc is inside the overlay descriptor's hitbox.
 *
 * Returns: true (1) if X, Y coordinates are inside a hitbox, otherwise false (0). 
 **/
static bool inside_hitbox(const struct overlay_desc *desc, float x, float y)
{
   if (!desc)
      return false;

   switch (desc->hitbox)
   {
      case OVERLAY_HITBOX_RADIAL:
      {
         /* Ellipsis. */
         float x_dist  = (x - desc->x) / desc->range_x_mod;
         float y_dist  = (y - desc->y) / desc->range_y_mod;
         float sq_dist = x_dist * x_dist + y_dist * y_dist;
         return (sq_dist <= 1.0f);
      }

      case OVERLAY_HITBOX_RECT:
         return (fabs(x - desc->x) <= desc->range_x_mod) &&
            (fabs(y - desc->y) <= desc->range_y_mod);
   }

   return false;
}

/**
 * input_overlay_poll:
 * @ol                    : Overlay handle.
 * @out                   : Polled output data.
 * @norm_x                : Normalized X coordinate.
 * @norm_y                : Normalized Y coordinate.
 *
 * Polls input overlay.
 *
 * @norm_x and @norm_y are the result of
 * input_translate_coord_viewport().
 **/
void input_overlay_poll(input_overlay_t *ol, input_overlay_state_t *out,
      int16_t norm_x, int16_t norm_y)
{
   size_t i;
   float x, y;

   memset(out, 0, sizeof(*out));

   if (!ol->enable)
   {
      ol->blocked = false;
      return;
   }

   /* norm_x and norm_y is in [-0x7fff, 0x7fff] range,
    * like RETRO_DEVICE_POINTER. */
   x = (float)(norm_x + 0x7fff) / 0xffff;
   y = (float)(norm_y + 0x7fff) / 0xffff;

   x -= ol->active->mod_x;
   y -= ol->active->mod_y;
   x /= ol->active->mod_w;
   y /= ol->active->mod_h;

   for (i = 0; i < ol->active->size; i++)
   {
      float x_dist, y_dist;
      struct overlay_desc *desc = &ol->active->descs[i];

      if (!desc)
         continue;
      if (!inside_hitbox(desc, x, y))
         continue;

      desc->updated = true;
      x_dist        = x - desc->x;
      y_dist        = y - desc->y;

      if (desc->type == OVERLAY_TYPE_BUTTONS)
      {
         uint64_t mask = desc->key_mask;

         out->buttons |= mask;

         if (mask & (UINT64_C(1) << RARCH_OVERLAY_NEXT))
            ol->next_index = desc->next_index;
      }
      else if (desc->type == OVERLAY_TYPE_KEYBOARD)
      {
         if (desc->key_mask < RETROK_LAST)
            OVERLAY_SET_KEY(out, desc->key_mask);
      }
      else
      {
         float x_val     = x_dist / desc->range_x;
         float y_val     = y_dist / desc->range_y;
         float x_val_sat = x_val / desc->analog_saturate_pct;
         float y_val_sat = y_val / desc->analog_saturate_pct;

         unsigned int base = (desc->type == OVERLAY_TYPE_ANALOG_RIGHT) ? 2 : 0;

         out->analog[base + 0] = clamp_float(x_val_sat, -1.0f, 1.0f) * 32767.0f;
         out->analog[base + 1] = clamp_float(y_val_sat, -1.0f, 1.0f) * 32767.0f;
      }

      if (desc->movable)
      {
         desc->delta_x = clamp_float(x_dist, -desc->range_x, desc->range_x)
            * ol->active->mod_w;
         desc->delta_y = clamp_float(y_dist, -desc->range_y, desc->range_y)
            * ol->active->mod_h;
      }
   }

   if (!out->buttons)
      ol->blocked = false;
   else if (ol->blocked)
      memset(out, 0, sizeof(*out));
}

/**
 * input_overlay_update_desc_geom:
 * @ol                    : overlay handle.
 * @desc                  : overlay descriptors handle.
 * 
 * Update input overlay descriptors' vertex geometry.
 **/
static void input_overlay_update_desc_geom(input_overlay_t *ol,
      struct overlay_desc *desc)
{
   if (!desc || !desc->image.pixels)
      return;
   if (!desc->movable)
      return;

   ol->iface->vertex_geom(ol->iface_data, desc->image_index,
      desc->mod_x + desc->delta_x, desc->mod_y + desc->delta_y,
      desc->mod_w, desc->mod_h);

   desc->delta_x = 0.0f;
   desc->delta_y = 0.0f;
}

/**
 * input_overlay_post_poll:
 * @ol                    : overlay handle
 *
 * Called after all the input_overlay_poll() calls to
 * update the range modifiers for pressed/unpressed regions
 * and alpha mods.
 **/
void input_overlay_post_poll(input_overlay_t *ol, float opacity)
{
   size_t i;

   if (!ol)
      return;

   input_overlay_set_alpha_mod(ol, opacity);

   for (i = 0; i < ol->active->size; i++)
   {
      struct overlay_desc *desc = &ol->active->descs[i];

      if (!desc)
         continue;

      desc->range_x_mod = desc->range_x;
      desc->range_y_mod = desc->range_y;

      if (desc->updated)
      {
         /* If pressed this frame, change the hitbox. */
         desc->range_x_mod *= desc->range_mod;
         desc->range_y_mod *= desc->range_mod;

         if (desc->image.pixels)
            ol->iface->set_alpha(ol->iface_data, desc->image_index,
                  desc->alpha_mod * opacity);
      }

      input_overlay_update_desc_geom(ol, desc);
      desc->updated = false;
   }
}

/**
 * input_overlay_poll_clear:
 * @ol                    : overlay handle
 *
 * Call when there is nothing to poll. Allows overlay to
 * clear certain state.
 **/
void input_overlay_poll_clear(input_overlay_t *ol, float opacity)
{
   size_t i;

   if (!ol)
      return;

   ol->blocked = false;

   input_overlay_set_alpha_mod(ol, opacity);

   for (i = 0; i < ol->active->size; i++)
   {
      struct overlay_desc *desc = &ol->active->descs[i];

      if (!desc)
         continue;

      desc->range_x_mod = desc->range_x;
      desc->range_y_mod = desc->range_y;
      desc->updated = false;

      desc->delta_x = 0.0f;
      desc->delta_y = 0.0f;
      input_overlay_update_desc_geom(ol, desc);
   }
}

/**
 * input_overlay_next:
 * @ol                    : Overlay handle.
 *
 * Switch to the next available overlay
 * screen.
 **/
void input_overlay_next(input_overlay_t *ol, float opacity)
{
   if (!ol)
      return;

   ol->index = ol->next_index;
   ol->active = &ol->overlays[ol->index];

   input_overlay_load_active(ol, opacity);

   ol->blocked = true;
   ol->next_index = (ol->index + 1) % ol->size;
}

/**
 * input_overlay_full_screen:
 * @ol                    : Overlay handle.
 *
 * Checks if the overlay is fullscreen.
 *
 * Returns: true (1) if overlay is fullscreen, otherwise false (0).
 **/
bool input_overlay_full_screen(input_overlay_t *ol)
{
   if (!ol)
      return false;
   return ol->active->full_screen;
}

/**
 * input_overlay_free:
 * @ol                    : Overlay handle.
 *
 * Frees overlay handle.
 **/
void input_overlay_free(input_overlay_t *ol)
{
   if (!ol)
      return;

   input_overlay_free_overlays(ol);

   if (ol->conf)
      config_file_free(ol->conf);
   ol->conf = NULL;

   if (ol->iface)
      ol->iface->enable(ol->iface_data, false);

   free(ol->overlay_path);
   free(ol);
}

/**
 * input_overlay_set_alpha_mod:
 * @ol                    : Overlay handle.
 * @mod                   : New modulating factor to apply.
 *
 * Sets a modulating factor for alpha channel. Default is 1.0.
 * The alpha factor is applied for all overlays.
 **/
void input_overlay_set_alpha_mod(input_overlay_t *ol, float mod)
{
   unsigned i;

   if (!ol)
      return;

   for (i = 0; i < ol->active->load_images_size; i++)
      ol->iface->set_alpha(ol->iface_data, i, mod);
}
