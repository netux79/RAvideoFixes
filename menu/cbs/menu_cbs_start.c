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

#include "../menu.h"
#include "../menu_cbs.h"
#include "../menu_input.h"
#include "../menu_setting.h"
#include "../menu_shader.h"
#include "../menu_hash.h"

#include "../../general.h"
#include "../../retroarch.h"
#include "../../performance.h"

#include "../../input/input_remapping.h"

static int action_start_remap_file_load(unsigned type, const char *label)
{
   settings_t *settings = config_get_ptr();

   if (!settings)
      return -1;

   settings->input.remapping_path[0] = '\0';
   input_remapping_set_defaults();
   return 0;
}

static int action_start_video_filter_file_load(unsigned type, const char *label)
{
   settings_t *settings = config_get_ptr();

   if (!settings)
      return -1;

   settings->video.softfilter_plugin[0] = '\0';
   event_command(EVENT_CMD_REINIT);
   return 0;
}

static int action_start_performance_counters_core(unsigned type, const char *label)
{
   struct retro_perf_counter **counters = (struct retro_perf_counter**)
      perf_counters_libretro;
   unsigned offset = type - MENU_SETTINGS_LIBRETRO_PERF_COUNTERS_BEGIN;

   (void)label;

   if (counters[offset])
   {
      counters[offset]->total = 0;
      counters[offset]->call_cnt = 0;
   }

   return 0;
}

static int action_start_input_desc(unsigned type, const char *label)
{
   settings_t           *settings = config_get_ptr();
   unsigned inp_desc_index_offset = type - MENU_SETTINGS_INPUT_DESC_BEGIN;
   unsigned inp_desc_user         = inp_desc_index_offset / (RARCH_FIRST_CUSTOM_BIND + 4);
   unsigned inp_desc_button_index_offset = inp_desc_index_offset - (inp_desc_user * (RARCH_FIRST_CUSTOM_BIND + 4));

   (void)label;

   if (inp_desc_button_index_offset < RARCH_FIRST_CUSTOM_BIND)
      settings->input.remap_ids[inp_desc_user][inp_desc_button_index_offset] =
         settings->input.binds[inp_desc_user][inp_desc_button_index_offset].id;
   else
      settings->input.remap_ids[inp_desc_user][inp_desc_button_index_offset] =
         inp_desc_button_index_offset - RARCH_FIRST_CUSTOM_BIND;

   return 0;
}

static int action_start_shader_action_parameter(unsigned type, const char *label)
{
#ifdef HAVE_SHADER_MANAGER
   struct video_shader_parameter *param = NULL;
   struct video_shader *shader = video_shader_driver_get_current_shader();

   if (!shader)
      return 0;

   param = &shader->parameters[type - MENU_SETTINGS_SHADER_PARAMETER_0];
   param->current = param->initial;
   param->current = min(max(param->minimum, param->current), param->maximum);

#endif

   return 0;
}

static int action_start_shader_action_preset_parameter(unsigned type, const char *label)
{
#ifdef HAVE_SHADER_MANAGER
   struct video_shader *shader = NULL;
   struct video_shader_parameter *param = NULL;
   menu_handle_t *menu = menu_driver_get_ptr();
   if (!menu)
      return -1;

   if (!(shader = menu->shader))
      return 0;

   param = &shader->parameters[type - MENU_SETTINGS_SHADER_PRESET_PARAMETER_0];
   param->current = param->initial;
   param->current = min(max(param->minimum, param->current), param->maximum);
#endif

   return 0;
}

static int action_start_shader_pass(unsigned type, const char *label)
{
#ifdef HAVE_SHADER_MANAGER
   struct video_shader *shader           = NULL;
   struct video_shader_pass *shader_pass = NULL;
   menu_handle_t *menu                   = menu_driver_get_ptr();
   hack_shader_pass                      = type - MENU_SETTINGS_SHADER_PASS_0;
   if (!menu)
      return -1;

   shader = menu->shader;

   if (shader)
      shader_pass = &shader->pass[hack_shader_pass];

   if (shader_pass)
      *shader_pass->source.path = '\0';
#endif

   return 0;
}


static int action_start_shader_scale_pass(unsigned type, const char *label)
{
#ifdef HAVE_SHADER_MANAGER
   struct video_shader *shader = NULL;
   struct video_shader_pass *shader_pass = NULL;
   unsigned pass = type - MENU_SETTINGS_SHADER_PASS_SCALE_0;
   menu_handle_t *menu = menu_driver_get_ptr();
   if (!menu)
      return -1;

   shader      = menu->shader;

   if (shader)
      shader_pass = &shader->pass[pass];

   if (shader_pass)
   {
      shader_pass->fbo.scale_x = shader_pass->fbo.scale_y = 0;
      shader_pass->fbo.valid = false;
   }
#endif

   return 0;
}

static int action_start_shader_filter_pass(unsigned type, const char *label)
{
#ifdef HAVE_SHADER_MANAGER
   unsigned pass = type - MENU_SETTINGS_SHADER_PASS_FILTER_0;
   struct video_shader *shader = NULL;
   struct video_shader_pass *shader_pass = NULL;
   menu_handle_t *menu = menu_driver_get_ptr();
   if (!menu)
      return -1;

   shader = menu->shader;
   if (!shader)
      return -1;
   shader_pass = &shader->pass[pass];
   if (!shader_pass)
      return -1;

   shader_pass->filter = RARCH_FILTER_UNSPEC;
#endif

   return 0;
}

static int action_start_shader_num_passes(unsigned type, const char *label)
{
#ifdef HAVE_SHADER_MANAGER
   struct video_shader *shader = NULL;
   menu_handle_t *menu = menu_driver_get_ptr();
   if (!menu)
      return -1;

   shader = menu->shader;
   if (!shader)
      return -1;
   if (shader->passes)
      shader->passes = 0;

   menu_entries_set_refresh();
   video_shader_resolve_parameters(NULL, menu->shader);
#endif
   return 0;
}

static int action_start_cheat_num_passes(unsigned type, const char *label)
{
   global_t *global       = global_get_ptr();
   cheat_manager_t *cheat = global->cheat;

   if (!cheat)
      return -1;

   if (cheat->size)
   {
      menu_entries_set_refresh();
      cheat_manager_realloc(cheat, 0);
   }

   return 0;
}

static int action_start_performance_counters_frontend(unsigned type,
      const char *label)
{
   struct retro_perf_counter **counters = (struct retro_perf_counter**)
      perf_counters_rarch;
   unsigned offset = type - MENU_SETTINGS_PERF_COUNTERS_BEGIN;

   (void)label;

   if (counters[offset])
   {
      counters[offset]->total = 0;
      counters[offset]->call_cnt = 0;
   }

   return 0;
}

static int action_start_core_setting(unsigned type,
      const char *label)
{
   unsigned idx           = type - MENU_SETTINGS_CORE_OPTION_START;
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   (void)label;

   if (system)
      core_option_set_default(system->core_options, idx);

   return 0;
}

static int action_start_video_resolution(
      unsigned type, const char *label)
{
   unsigned width = 0, height = 0;
   global_t *global = global_get_ptr();

   video_driver_set_video_mode(640, 480, true);

   if (!global)
      return -1;

   if (video_driver_get_video_output_size(&width, &height))
   {
      char msg[PATH_MAX_LENGTH] = {0};

      video_driver_set_video_mode(width, height, true);
      global->console.screen.resolutions.width = width;
      global->console.screen.resolutions.height = height;

      snprintf(msg, sizeof(msg),"Resetting to: %dx%d",width, height);
      rarch_main_msg_queue_push(msg, 1, 100, true);
   }

   return 0;
}

static int action_start_lookup_setting(unsigned type, const char *label)
{
   return menu_setting_set(type, label, MENU_ACTION_START, false);
}

int menu_cbs_init_bind_start_compare_label(menu_file_list_cbs_t *cbs,
      uint32_t hash)
{
   switch (hash)
   {
      case MENU_LABEL_REMAP_FILE_LOAD:
         cbs->action_start = action_start_remap_file_load;
         break;
      case MENU_LABEL_VIDEO_FILTER:
         cbs->action_start = action_start_video_filter_file_load;
         break;
      case MENU_LABEL_VIDEO_SHADER_PASS:
         cbs->action_start = action_start_shader_pass;
         break;
      case MENU_LABEL_VIDEO_SHADER_SCALE_PASS:
         cbs->action_start = action_start_shader_scale_pass;
         break;
      case MENU_LABEL_VIDEO_SHADER_FILTER_PASS:
         cbs->action_start = action_start_shader_filter_pass;
         break;
      case MENU_LABEL_VIDEO_SHADER_NUM_PASSES:
         cbs->action_start = action_start_shader_num_passes;
         break;
      case MENU_LABEL_CHEAT_NUM_PASSES:
         cbs->action_start = action_start_cheat_num_passes;
         break;
      case MENU_LABEL_SCREEN_RESOLUTION:
         cbs->action_start = action_start_video_resolution;		 
      default:
         return -1;
   }

   return 0;
}

static int menu_cbs_init_bind_start_compare_type(menu_file_list_cbs_t *cbs,
      unsigned type)
{
   if (type >= MENU_SETTINGS_SHADER_PARAMETER_0
         && type <= MENU_SETTINGS_SHADER_PARAMETER_LAST)
      cbs->action_start = action_start_shader_action_parameter;
   else if (type >= MENU_SETTINGS_SHADER_PRESET_PARAMETER_0
         && type <= MENU_SETTINGS_SHADER_PRESET_PARAMETER_LAST)
      cbs->action_start = action_start_shader_action_preset_parameter;
   else if (type >= MENU_SETTINGS_LIBRETRO_PERF_COUNTERS_BEGIN &&
         type <= MENU_SETTINGS_LIBRETRO_PERF_COUNTERS_END)
      cbs->action_start = action_start_performance_counters_core;
   else if (type >= MENU_SETTINGS_INPUT_DESC_BEGIN
         && type <= MENU_SETTINGS_INPUT_DESC_END)
      cbs->action_start = action_start_input_desc;
   else if (type >= MENU_SETTINGS_PERF_COUNTERS_BEGIN &&
         type <= MENU_SETTINGS_PERF_COUNTERS_END)
      cbs->action_start = action_start_performance_counters_frontend;
   else if ((type >= MENU_SETTINGS_CORE_OPTION_START))
      cbs->action_start = action_start_core_setting;
   else if (type == MENU_LABEL_SCREEN_RESOLUTION)
      cbs->action_start = action_start_video_resolution;
   else
      return -1;

   return 0;
}

int menu_cbs_init_bind_start(menu_file_list_cbs_t *cbs,
      const char *path, const char *label, unsigned type, size_t idx,
      const char *elem0, const char *elem1,
      uint32_t label_hash, uint32_t menu_label_hash)
{
   if (!cbs)
      return -1;

   cbs->action_start = action_start_lookup_setting;
   
   if (menu_cbs_init_bind_start_compare_label(cbs, label_hash) == 0)
      return 0;

   if (menu_cbs_init_bind_start_compare_type(cbs, type) == 0)
      return 0;

   return -1;
}
