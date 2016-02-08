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

#include <file/file_path.h>
#include <file/config_file.h>

#include "menu.h"
#include "menu_input.h"
#include "menu_setting.h"
#include "menu_hash.h"

#include "../driver.h"
#include "../general.h"
#include "../gfx/video_monitor.h"
#include "../dynamic.h"
#include "../input/input_common.h"
#include "../input/input_autodetect.h"
#include "../config.def.h"
#include "../file_ext.h"
#include "../performance.h"

#if defined(__CELLOS_LV2__)
#include <sdk_version.h>

#if (CELL_SDK_VERSION > 0x340000)
#include <sysutil/sysutil_bgmplayback.h>
#endif

#endif

static void menu_settings_info_list_free(rarch_setting_info_t *list_info)
{
   if (list_info)
      free(list_info);
   list_info = NULL;
}

static bool menu_settings_list_append(rarch_setting_t **list,
      rarch_setting_info_t *list_info, rarch_setting_t value)
{
   if (!list || !*list || !list_info)
      return false;

   if (list_info->index == list_info->size)
   {
      list_info->size *= 2;
      *list = (rarch_setting_t*)
         realloc(*list, sizeof(rarch_setting_t) * list_info->size);
      if (!*list)
         return false;
   }

   value.name_hash = value.name ? menu_hash_calculate(value.name) : 0;

   (*list)[list_info->index++] = value;
   return true;
}

static void null_write_handler(void *data)
{
   (void)data;
}

static void menu_settings_list_current_add_bind_type(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      unsigned type)
{
   unsigned idx = list_info->index - 1;
   (*list)[idx].bind_type = type;
}

static void menu_settings_list_current_add_flags(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      unsigned values)
{
   unsigned idx = list_info->index - 1;
   (*list)[idx].flags |= values;

   if (values & SD_FLAG_IS_DEFERRED)
   {
      (*list)[idx].deferred_handler = (*list)[idx].change_handler;
      (*list)[idx].change_handler = null_write_handler;
   }
}

static void menu_settings_list_current_add_range(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      float min, float max, float step,
      bool enforce_minrange_enable, bool enforce_maxrange_enable)
{
   unsigned idx = list_info->index - 1;

   (*list)[idx].min               = min;
   (*list)[idx].step              = step;
   (*list)[idx].max               = max;
   (*list)[idx].enforce_minrange  = enforce_minrange_enable;
   (*list)[idx].enforce_maxrange  = enforce_maxrange_enable;

   menu_settings_list_current_add_flags(list, list_info, SD_FLAG_HAS_RANGE);
}

static void menu_settings_list_current_add_values(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *values)
{
   unsigned idx = list_info->index - 1;
   (*list)[idx].values = values;
}

static void menu_settings_list_current_add_cmd(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      enum event_command values)
{
   unsigned idx = list_info->index - 1;
   (*list)[idx].cmd_trigger.idx = values;
}


static rarch_setting_t *menu_setting_list_new(unsigned size)
{
   rarch_setting_t *list = (rarch_setting_t*)calloc(size, sizeof(*list));
   if (!list)
      return NULL;

   return list;
}

int menu_setting_set_flags(rarch_setting_t *setting)
{
   if (!setting)
      return 0;

   if (setting->flags & SD_FLAG_IS_DRIVER)
      return MENU_SETTING_DRIVER;

   switch (setting->type)
   {
      case ST_ACTION:
         return MENU_SETTING_ACTION;
      case ST_PATH:
         return MENU_FILE_PATH;
      case ST_GROUP:
         return MENU_SETTING_GROUP;
      case ST_SUB_GROUP:
         return MENU_SETTING_SUBGROUP;
      default:
         break;
   }

   return 0;
}

static int setting_generic_action_ok_default(void *data, bool wraparound)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   (void)wraparound;

   if (setting->cmd_trigger.idx != EVENT_CMD_NONE)
      setting->cmd_trigger.triggered = true;

   return 0;
}

int menu_setting_generic(rarch_setting_t *setting, bool wraparound)
{
   if (setting_generic_action_ok_default(setting, wraparound) != 0)
      return -1;

   if (setting->change_handler)
      setting->change_handler(setting);

   if (setting->flags & SD_FLAG_EXIT
         && setting->cmd_trigger.triggered)
   {
      setting->cmd_trigger.triggered = false;
      return -1;
   }

   return 0;
}

static int setting_handler(rarch_setting_t *setting, unsigned action)
{
   if (!setting)
      return -1;

   switch (action)
   {
      case MENU_ACTION_UP:
         if (setting->action_up)
            return setting->action_up(setting);
         break;
      case MENU_ACTION_DOWN:
         if (setting->action_down)
            return setting->action_down(setting);
         break;
      case MENU_ACTION_LEFT:
         if (setting->action_left)
            return setting->action_left(setting, false);
         break;
      case MENU_ACTION_RIGHT:
         if (setting->action_right)
            return setting->action_right(setting, false);
         break;
      case MENU_ACTION_SELECT:
         if (setting->action_select)
            return setting->action_select(setting, true);
         break;
      case MENU_ACTION_OK:
         if (setting->action_ok)
            return setting->action_ok(setting, false);
         break;
      case MENU_ACTION_CANCEL:
         if (setting->action_cancel)
            return setting->action_cancel(setting);
         break;
      case MENU_ACTION_START:
         if (setting->action_start)
            return setting->action_start(setting);
         break;
   }

   return -1;
}

int menu_action_handle_setting(rarch_setting_t *setting,
      unsigned type, unsigned action, bool wraparound)
{
   menu_displaylist_info_t  info = {0};
   menu_navigation_t        *nav = menu_navigation_get_ptr();

   if (!setting)
      return -1;

   switch (setting->type)
   {
      case ST_PATH:
         if (action == MENU_ACTION_OK)
         {
            menu_list_t   *menu_list = menu_list_get_ptr();

            info.list           = menu_list->menu_stack;
            info.type           = type;
            info.directory_ptr  = nav->selection_ptr;
            strlcpy(info.path, setting->default_value.string, sizeof(info.path));
            strlcpy(info.label, setting->name, sizeof(info.label));

            menu_displaylist_push_list(&info, DISPLAYLIST_GENERIC);
         }
         /* fall-through. */
      case ST_BOOL:
      case ST_INT:
      case ST_UINT:
      case ST_HEX:
      case ST_FLOAT:
      case ST_STRING:
      case ST_DIR:
      case ST_BIND:
      case ST_ACTION:
         if (setting_handler(setting, action) == 0)
            return menu_setting_generic(setting, wraparound);
         break;
      default:
         break;
   }

   return -1;
}

static rarch_setting_t *menu_setting_get_ptr(void)
{
   menu_entries_t *entries = menu_entries_get_ptr();

   if (!entries)
      return NULL;
   return entries->list_settings;
}

/**
 * menu_setting_find:
 * @settings           : pointer to settings
 * @name               : name of setting to search for
 *
 * Search for a setting with a specified name (@name).
 *
 * Returns: pointer to setting if found, NULL otherwise.
 **/
rarch_setting_t *menu_setting_find(const char *label)
{
   rarch_setting_t *settings = menu_setting_get_ptr();
   uint32_t needle = 0;

   if (!settings)
      return NULL;
   if (!label)
      return NULL;

   needle = menu_hash_calculate(label);

   for (; settings->type != ST_NONE; settings++)
   {
      if (needle == settings->name_hash && settings->type <= ST_GROUP)
      {
         /* make sure this isn't a collision */
         if (strcmp(label, settings->name) != 0)
            continue;

         if (settings->short_description && settings->short_description[0] == '\0')
            return NULL;

         if (settings->read_handler)
            settings->read_handler(settings);

         return settings;
      }
   }

   return NULL;
}

int menu_setting_set(unsigned type, const char *label,
      unsigned action, bool wraparound)
{
   int ret                  = 0;
   rarch_setting_t *setting = NULL;
   menu_navigation_t   *nav = menu_navigation_get_ptr();
   menu_list_t   *menu_list = menu_list_get_ptr();

   setting = menu_setting_find(
         menu_list->selection_buf->list
         [nav->selection_ptr].label);

   if (!setting)
      return 0;

   ret = menu_action_handle_setting(setting,
         type, action, wraparound);

   if (ret == -1)
      return 0;
   return ret;
}

void menu_setting_apply_deferred(void)
{
   rarch_setting_t *setting = menu_setting_get_ptr();
    
   if (!setting)
      return;
    
   for (; setting->type != ST_NONE; setting++)
   {
      if (setting->type >= ST_GROUP)
         continue;

      if (!(setting->flags & SD_FLAG_IS_DEFERRED))
         continue;

      switch (setting->type)
      {
         case ST_BOOL:
            if (*setting->value.boolean != setting->original_value.boolean)
            {
               setting->original_value.boolean = *setting->value.boolean;
               setting->deferred_handler(setting);
            }
            break;
         case ST_INT:
            if (*setting->value.integer != setting->original_value.integer)
            {
               setting->original_value.integer = *setting->value.integer;
               setting->deferred_handler(setting);
            }
            break;
         case ST_UINT:
            if (*setting->value.unsigned_integer != setting->original_value.unsigned_integer)
            {
               setting->original_value.unsigned_integer = *setting->value.unsigned_integer;
               setting->deferred_handler(setting);
            }
            break;
         case ST_FLOAT:
            if (*setting->value.fraction != setting->original_value.fraction)
            {
               setting->original_value.fraction = *setting->value.fraction;
               setting->deferred_handler(setting);
            }
            break;
         case ST_PATH:
         case ST_DIR:
         case ST_STRING:
         case ST_BIND:
            /* Always run the deferred write handler */
            setting->deferred_handler(setting);
            break;
         default:
            break;
      }
   }
}


/**
 * setting_reset_setting:
 * @setting            : pointer to setting
 *
 * Reset a setting's value to its defaults.
 **/
static void setting_reset_setting(rarch_setting_t* setting)
{
   if (!setting)
      return;

   switch (setting->type)
   {
      case ST_BOOL:
         *setting->value.boolean          = setting->default_value.boolean;
         break;
      case ST_INT:
         *setting->value.integer          = setting->default_value.integer;
         break;
      case ST_UINT:
         *setting->value.unsigned_integer = setting->default_value.unsigned_integer;
         break;
      case ST_FLOAT:
         *setting->value.fraction         = setting->default_value.fraction;
         break;
      case ST_BIND:
         *setting->value.keybind          = *setting->default_value.keybind;
         break;
      case ST_STRING:
      case ST_PATH:
      case ST_DIR:
         if (setting->default_value.string)
         {
            if (setting->type == ST_STRING)
               setting_set_with_string_representation(setting, setting->default_value.string);
            else
               fill_pathname_expand_special(setting->value.string,
                     setting->default_value.string, setting->size);
         }
         break;
         /* TODO */
      case ST_ACTION:
         break;
      case ST_HEX:
         break;
      case ST_GROUP:
         break;
      case ST_SUB_GROUP:
         break;
      case ST_END_GROUP:
         break;
      case ST_END_SUB_GROUP:
         break;
      case ST_NONE:
         break;
   }

   if (setting->change_handler)
      setting->change_handler(setting);
}


/**
 * setting_set_with_string_representation:
 * @setting            : pointer to setting
 * @value              : value for the setting (string)
 *
 * Set a settings' value with a string. It is assumed
 * that the string has been properly formatted.
 **/
int setting_set_with_string_representation(rarch_setting_t* setting,
      const char* value)
{
   uint32_t value_hash;
   if (!setting || !value)
      return -1;

   switch (setting->type)
   {
      case ST_INT:
         sscanf(value, "%d", setting->value.integer);
         if (setting->flags & SD_FLAG_HAS_RANGE)
         {
            if (setting->enforce_minrange && *setting->value.integer < setting->min)
               *setting->value.integer = setting->min;
            if (setting->enforce_maxrange && *setting->value.integer > setting->max)
            {
               settings_t *settings = config_get_ptr();

               if (settings && settings->menu.navigation.wraparound.setting_enable)
                  *setting->value.integer = setting->min;
               else
                  *setting->value.integer = setting->max;
            }
         }
         break;
      case ST_UINT:
         sscanf(value, "%u", setting->value.unsigned_integer);
         if (setting->flags & SD_FLAG_HAS_RANGE)
         {
            if (setting->enforce_minrange && *setting->value.unsigned_integer < setting->min)
               *setting->value.unsigned_integer = setting->min;
            if (setting->enforce_maxrange && *setting->value.unsigned_integer > setting->max)
            {
               settings_t *settings = config_get_ptr();

               if (settings && settings->menu.navigation.wraparound.setting_enable)
                  *setting->value.unsigned_integer = setting->min;
               else
                  *setting->value.unsigned_integer = setting->max;
            }
         }
         break;      
      case ST_FLOAT:
         sscanf(value, "%f", setting->value.fraction);
         if (setting->flags & SD_FLAG_HAS_RANGE)
         {
            if (setting->enforce_minrange && *setting->value.fraction < setting->min)
               *setting->value.fraction = setting->min;
            if (setting->enforce_maxrange && *setting->value.fraction > setting->max)
            {
               settings_t *settings = config_get_ptr();

               if (settings && settings->menu.navigation.wraparound.setting_enable)
                  *setting->value.fraction = setting->min;
               else
                  *setting->value.fraction = setting->max;
            }
         }
         break;
      case ST_PATH:
      case ST_DIR:
      case ST_STRING:
      case ST_ACTION:
         strlcpy(setting->value.string, value, setting->size);
         break;
      case ST_BOOL:
         value_hash = menu_hash_calculate(value);

         switch (value_hash)
         {
            case MENU_VALUE_TRUE:
               *setting->value.boolean = true;
               break;
            case MENU_VALUE_FALSE:
               *setting->value.boolean = false;
               break;
         }
         break;

         /* TODO */
      case ST_HEX:
         break;
      case ST_GROUP:
         break;
      case ST_SUB_GROUP:
         break;
      case ST_END_GROUP:
         break;
      case ST_END_SUB_GROUP:
         break;
      case ST_NONE:
         break;
      case ST_BIND:
         break;
   }

   if (setting->change_handler)
      setting->change_handler(setting);

   return 0;
}

/**
 * setting_get_string_representation:
 * @setting            : pointer to setting
 * @s                  : buffer to write contents of string representation to.
 * @len                : size of the buffer (@s)
 *
 * Get a setting value's string representation.
 **/
void setting_get_string_representation(void *data, char *s, size_t len)
{
   rarch_setting_t* setting = (rarch_setting_t*)data;
   if (!setting || !s)
      return;

   if (setting->get_string_representation)
      setting->get_string_representation(setting, s, len);
}

/**
 ******* ACTION START CALLBACK FUNCTIONS *******
**/

/**
 * setting_action_start_savestates:
 * @data               : pointer to setting
 *
 * Function callback for 'Savestate' action's 'Action Start'
 * function pointer.
 *
 * Returns: 0 on success, -1 on error.
 **/
static int setting_action_start_bind_device(void *data)
{
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();

   if (!setting)
      return -1;

   settings->input.joypad_map[setting->index_offset] = setting->index_offset;
   return 0;
}

static int setting_generic_action_start_default(void *data)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   setting_reset_setting(setting);

   return 0;
}


static int setting_action_start_analog_dpad_mode(void *data)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   *setting->value.unsigned_integer = 0;

   return 0;
}

static int setting_action_start_libretro_device_type(void *data)
{
   unsigned current_device, i, devices[128], types = 0, port = 0;
   const struct retro_controller_info *desc = NULL;
   rarch_setting_t   *setting  = (rarch_setting_t*)data;
   settings_t        *settings = config_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   if (setting_generic_action_start_default(setting) != 0)
      return -1;

   port = setting->index_offset;

   devices[types++] = RETRO_DEVICE_NONE;
   devices[types++] = RETRO_DEVICE_JOYPAD;

   /* Only push RETRO_DEVICE_ANALOG as default if we use an 
    * older core which doesn't use SET_CONTROLLER_INFO. */
   if (!system->num_ports)
      devices[types++] = RETRO_DEVICE_ANALOG;

   desc = port < system->num_ports ?
      &system->ports[port] : NULL;

   if (desc)
   {
      for (i = 0; i < desc->num_types; i++)
      {
         unsigned id = desc->types[i].id;
         if (types < ARRAY_SIZE(devices) &&
               id != RETRO_DEVICE_NONE &&
               id != RETRO_DEVICE_JOYPAD)
            devices[types++] = id;
      }
   }

   current_device = RETRO_DEVICE_JOYPAD;

   settings->input.libretro_device[port] = current_device;
   pretro_set_controller_port_device(port, current_device);

   return 0;
}

static int setting_action_start_video_refresh_rate_auto(
      void *data)
{
   video_monitor_reset();

   return 0;
}

static int setting_string_action_start_generic(void *data)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   *setting->value.string = '\0';

   return 0;
}

static int setting_bind_action_start(void *data)
{
   struct retro_keybind *keybind   = NULL;
   rarch_setting_t *setting        = (rarch_setting_t*)data;
   struct retro_keybind *def_binds = (struct retro_keybind *)retro_keybinds_1;
   global_t                *global = global_get_ptr();

   if (!setting)
      return -1;

   keybind = (struct retro_keybind*)setting->value.keybind;
   if (!keybind)
      return -1;

   if (!global->menu.bind_mode_keyboard)
   {
      keybind->joykey = NO_BTN;
      keybind->joyaxis = AXIS_NONE;
      return 0;
   }

   if (setting->index_offset)
      def_binds = (struct retro_keybind*)retro_keybinds_rest;

   if (!def_binds)
      return -1;

   keybind->key = def_binds[setting->bind_type - MENU_SETTINGS_BIND_BEGIN].key;

   return 0;
}

/**
 ******* ACTION TOGGLE CALLBACK FUNCTIONS *******
**/

static int setting_action_left_analog_dpad_mode(void *data, bool wraparound)
{
   unsigned port = 0;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();

   if (!setting)
      return -1;

   port = setting->index_offset;

   settings->input.analog_dpad_mode[port] =
      (settings->input.analog_dpad_mode
       [port] + ANALOG_DPAD_LAST - 1) % ANALOG_DPAD_LAST;

   return 0;
}

static int setting_action_right_analog_dpad_mode(void *data, bool wraparound)
{
   unsigned port = 0;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();

   if (!setting)
      return -1;

   port = setting->index_offset;

   settings->input.analog_dpad_mode[port] =
      (settings->input.analog_dpad_mode[port] + 1)
      % ANALOG_DPAD_LAST;

   return 0;
}

static int setting_action_left_libretro_device_type(
      void *data, bool wraparound)
{
   unsigned current_device, current_idx, i, devices[128],
            types = 0, port = 0;
   const struct retro_controller_info *desc = NULL;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   if (!setting)
      return -1;

   port = setting->index_offset;

   devices[types++] = RETRO_DEVICE_NONE;
   devices[types++] = RETRO_DEVICE_JOYPAD;

   /* Only push RETRO_DEVICE_ANALOG as default if we use an 
    * older core which doesn't use SET_CONTROLLER_INFO. */
   if (!system->num_ports)
      devices[types++] = RETRO_DEVICE_ANALOG;

   if (port < system->num_ports)
      desc = &system->ports[port];

   if (desc)
   {
      for (i = 0; i < desc->num_types; i++)
      {
         unsigned id = desc->types[i].id;
         if (types < ARRAY_SIZE(devices) &&
               id != RETRO_DEVICE_NONE &&
               id != RETRO_DEVICE_JOYPAD)
            devices[types++] = id;
      }
   }

   current_device = settings->input.libretro_device[port];
   current_idx    = 0;
   for (i = 0; i < types; i++)
   {
      if (current_device != devices[i])
         continue;

      current_idx = i;
      break;
   }

   current_device = devices
      [(current_idx + types - 1) % types];

   settings->input.libretro_device[port] = current_device;
   pretro_set_controller_port_device(port, current_device);

   return 0;
}

static int setting_action_right_libretro_device_type(
      void *data, bool wraparound)
{
   unsigned current_device, current_idx, i, devices[128],
            types = 0, port = 0;
   const struct retro_controller_info *desc = NULL;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   if (!setting)
      return -1;

   port = setting->index_offset;

   devices[types++] = RETRO_DEVICE_NONE;
   devices[types++] = RETRO_DEVICE_JOYPAD;

   /* Only push RETRO_DEVICE_ANALOG as default if we use an 
    * older core which doesn't use SET_CONTROLLER_INFO. */
   if (!system->num_ports)
      devices[types++] = RETRO_DEVICE_ANALOG;

   if (port < system->num_ports)
      desc = &system->ports[port];

   if (desc)
   {
      for (i = 0; i < desc->num_types; i++)
      {
         unsigned id = desc->types[i].id;
         if (types < ARRAY_SIZE(devices) &&
               id != RETRO_DEVICE_NONE &&
               id != RETRO_DEVICE_JOYPAD)
            devices[types++] = id;
      }
   }

   current_device = settings->input.libretro_device[port];
   current_idx    = 0;
   for (i = 0; i < types; i++)
   {
      if (current_device != devices[i])
         continue;

      current_idx = i;
      break;
   }

   current_device = devices
      [(current_idx + 1) % types];

   settings->input.libretro_device[port] = current_device;
   pretro_set_controller_port_device(port, current_device);

   return 0;
}

static int setting_action_left_bind_device(void *data, bool wraparound)
{
   unsigned               *p = NULL;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();

   if (!setting)
      return -1;

   p = &settings->input.joypad_map[setting->index_offset];

   if ((*p) >= settings->input.max_users)
      *p = settings->input.max_users - 1;
   else if ((*p) > 0)
      (*p)--;

   return 0;
}

static int setting_action_right_bind_device(void *data, bool wraparound)
{
   unsigned               *p = NULL;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();

   if (!setting)
      return -1;

   p = &settings->input.joypad_map[setting->index_offset];

   if (*p < settings->input.max_users)
      (*p)++;

   return 0;
}

static int setting_bool_action_toggle_default(void *data, bool wraparound)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   *setting->value.boolean = !(*setting->value.boolean);

   return 0;
}

static int setting_uint_action_left_default(void *data, bool wraparound)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   if (*setting->value.unsigned_integer != setting->min)
      *setting->value.unsigned_integer =
         *setting->value.unsigned_integer - setting->step;

   if (setting->enforce_minrange)
   {
      if (*setting->value.unsigned_integer < setting->min)
         *setting->value.unsigned_integer = setting->min;
   }

   return 0;
}

static int setting_uint_action_right_default(void *data, bool wraparound)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   *setting->value.unsigned_integer =
      *setting->value.unsigned_integer + setting->step;

   if (setting->enforce_maxrange)
   {
      if (*setting->value.unsigned_integer > setting->max)
      {
         settings_t *settings = config_get_ptr();

         if (settings && settings->menu.navigation.wraparound.setting_enable)
            *setting->value.unsigned_integer = setting->min;
         else
            *setting->value.unsigned_integer = setting->max;
      }
   }

   return 0;
}

static int setting_fraction_action_left_default(
      void *data, bool wraparound)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   *setting->value.fraction =
      *setting->value.fraction - setting->step;

   if (setting->enforce_minrange)
   {
      if (*setting->value.fraction < setting->min)
         *setting->value.fraction = setting->min;
   }

   return 0;
}

static int setting_fraction_action_right_default(
      void *data, bool wraparound)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   *setting->value.fraction = 
      *setting->value.fraction + setting->step;

   if (setting->enforce_maxrange)
   {
      if (*setting->value.fraction > setting->max)
      {
         settings_t *settings = config_get_ptr();

         if (settings && settings->menu.navigation.wraparound.setting_enable)
            *setting->value.fraction = setting->min;
         else
            *setting->value.fraction = setting->max;
      }
   }

   return 0;
}

static int setting_string_action_left_driver(void *data,
      bool wraparound)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   if (!find_prev_driver(setting->name, setting->value.string, setting->size))
   {
#if 0
      if (wraparound)
         find_last_driver(setting->name, setting->value.string, setting->size);
#endif
   }

   return 0;
}

static int setting_string_action_right_driver(void *data,
      bool wraparound)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   if (!find_next_driver(setting->name, setting->value.string, setting->size))
   {
      settings_t *settings = config_get_ptr();

      if (settings && settings->menu.navigation.wraparound.setting_enable)
         find_first_driver(setting->name, setting->value.string, setting->size);
   }

   return 0;
}

#if defined(HAVE_DYNAMIC) || defined(HAVE_LIBRETRO_MANAGEMENT)
static int core_list_action_toggle(void *data, bool wraparound)
{
   rarch_setting_t *setting  = (rarch_setting_t *)data;
   settings_t      *settings = config_get_ptr();

   /* If the user CANCELs the browse, then settings->libretro is now
    * set to a directory, which is very bad and will cause a crash
    * later on. I need to be able to add something to call when a
    * cancel happens.
    */
   return setting_set_with_string_representation(setting, settings->libretro_directory);
}
#endif

/**
 ******* ACTION OK CALLBACK FUNCTIONS *******
**/

static int setting_action_ok_bind_all(void *data, bool wraparound)
{
   global_t      *global     = global_get_ptr();

   (void)wraparound;

   if (!global)
      return -1;

   if (global->menu.bind_mode_keyboard)
      menu_input_set_keyboard_bind_mode(data, MENU_INPUT_BIND_ALL);
   else
      menu_input_set_input_device_bind_mode(data, MENU_INPUT_BIND_ALL);

   return 0;
}

static int setting_action_ok_bind_defaults(void *data, bool wraparound)
{
   unsigned i;
   struct retro_keybind *target = NULL;
   const struct retro_keybind *def_binds = NULL;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   menu_input_t *menu_input  = menu_input_get_ptr();
   settings_t    *settings   = config_get_ptr();
   global_t      *global     = global_get_ptr();

   (void)wraparound;

   if (!menu_input)
      return -1;
   if (!setting)
      return -1;

   target = (struct retro_keybind*)
      &settings->input.binds[setting->index_offset][0];
   def_binds =  (setting->index_offset) ? 
      retro_keybinds_rest : retro_keybinds_1;

   if (!target)
      return -1;

   menu_input->binds.begin = MENU_SETTINGS_BIND_BEGIN;
   menu_input->binds.last  = MENU_SETTINGS_BIND_LAST;

   for (i = MENU_SETTINGS_BIND_BEGIN;
         i <= MENU_SETTINGS_BIND_LAST; i++, target++)
   {
      if (global->menu.bind_mode_keyboard)
         target->key = def_binds[i - MENU_SETTINGS_BIND_BEGIN].key;
      else
      {
         target->joykey = NO_BTN;
         target->joyaxis = AXIS_NONE;
      }
   }

   return 0;
}

static int setting_action_ok_video_refresh_rate_auto(void *data, bool wraparound)
{
   double video_refresh_rate = 0.0;
   double deviation          = 0.0;
   unsigned sample_points    = 0;
   rarch_setting_t *setting  = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   if (video_monitor_fps_statistics(&video_refresh_rate,
            &deviation, &sample_points))
   {
      driver_set_refresh_rate(video_refresh_rate);
      /* Incase refresh rate update forced non-block video. */
      event_command(EVENT_CMD_VIDEO_SET_BLOCKING_STATE);
   }

   if (setting_generic_action_ok_default(setting, wraparound) != 0)
      return -1;

   return 0;
}

static int setting_generic_action_ok_linefeed(void *data, bool wraparound)
{
   input_keyboard_line_complete_t cb = NULL;
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   (void)wraparound;

   switch (setting->type)
   {
      case ST_UINT:
         cb = menu_input_st_uint_callback;
         break;
      case ST_HEX:
         cb = menu_input_st_hex_callback;
         break;
      case ST_STRING:
         cb = menu_input_st_string_callback;
         break;
      default:
         break;
   }

   menu_input_key_start_line(setting->short_description,
         setting->name, 0, 0, cb);

   return 0;
}

static int setting_action_action_ok(void *data, bool wraparound)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (!setting)
      return -1;

   (void)wraparound;

   if (setting->cmd_trigger.idx != EVENT_CMD_NONE)
      event_command(setting->cmd_trigger.idx);

   return 0;
}

static int setting_bind_action_ok(void *data, bool wraparound)
{
   global_t      *global     = global_get_ptr();
   (void)wraparound;

   if (global->menu.bind_mode_keyboard)
      menu_input_set_keyboard_bind_mode(data, MENU_INPUT_BIND_SINGLE);
   else
      menu_input_set_input_device_bind_mode(data, MENU_INPUT_BIND_SINGLE);

   return 0;
}


/**
 ******* SET LABEL CALLBACK FUNCTIONS *******
**/

/**
 * setting_get_string_representation_st_bool:
 * @setting            : pointer to setting
 * @s                  : string for the type to be represented on-screen as
 *                       a label.
 * @len                : size of @s
 *
 * Set a settings' label value. The setting is of type ST_BOOL.
 **/
static void setting_get_string_representation_st_bool(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (setting)
      strlcpy(s, *setting->value.boolean ? setting->boolean.on_label :
            setting->boolean.off_label, len);
}

static void setting_get_string_representation_default(void *data,
      char *s, size_t len)
{
   (void)data;
   strlcpy(s, "...", len);
}

/**
 * setting_get_string_representation_st_float:
 * @setting            : pointer to setting
 * @s                  : string for the type to be represented on-screen as
 *                       a label.
 * @len                : size of @s
 *
 * Set a settings' label value. The setting is of type ST_FLOAT.
 **/
static void setting_get_string_representation_st_float(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (setting)
      snprintf(s, len, setting->rounding_fraction,
            *setting->value.fraction);
}

static void setting_get_string_representation_st_float_video_refresh_rate_auto(void *data,
      char *s, size_t len)
{
   double video_refresh_rate = 0.0;
   double deviation          = 0.0;
   unsigned sample_points    = 0;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   if (!setting)
      return;

   if (video_monitor_fps_statistics(&video_refresh_rate, &deviation, &sample_points))
   {
      menu_animation_t *anim = menu_animation_get_ptr();

      snprintf(s, len, "%.3f Hz (%.1f%% dev, %u samples)",
            video_refresh_rate, 100.0 * deviation, sample_points);

      if (anim)
         anim->label.is_updated = true;
   }
   else
      strlcpy(s, menu_hash_to_str(MENU_VALUE_NOT_AVAILABLE), len);
}

static void setting_get_string_representation_st_dir(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (setting)
      strlcpy(s,
            *setting->value.string ?
            setting->value.string : setting->dir.empty_path,
            len);
}

static void setting_get_string_representation_st_path(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (setting)
      fill_short_pathname_representation(s, setting->value.string, len);
}

static void setting_get_string_representation_st_string(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (setting)
      strlcpy(s, setting->value.string, len);
}

static void setting_get_string_representation_st_bind(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting              = (rarch_setting_t*)data;
   const struct retro_keybind* keybind   = NULL;
   const struct retro_keybind* auto_bind = NULL;

   if (!setting)
      return;
   
   keybind   = (const struct retro_keybind*)setting->value.keybind;
   auto_bind = (const struct retro_keybind*)
      input_get_auto_bind(setting->index_offset, keybind->id);

   input_get_bind_string(s, keybind, auto_bind, len);
}

#if 0
static void setting_get_string_representation_int(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (setting)
      snprintf(s, len, "%d", *setting->value.integer);
}
#endif

static void setting_get_string_representation_uint_video_monitor_index(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;
   if (!setting)
      return;

   if (*setting->value.unsigned_integer)
      snprintf(s, len, "%u",
            *setting->value.unsigned_integer);
   else
      strlcpy(s, "0 (Auto)", len);
}

static void setting_get_string_representation_uint_video_rotation(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;
   if (setting)
      strlcpy(s, rotation_lut[*setting->value.unsigned_integer],
            len);
}

static void setting_get_string_representation_uint_aspect_ratio_index(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;
   if (setting)
      strlcpy(s,
            aspectratio_lut[*setting->value.unsigned_integer].name,
            len);
}

static void setting_get_string_representation_uint_libretro_device(void *data,
      char *s, size_t len)
{
   const struct retro_controller_description *desc = NULL;
   const char *name = NULL;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   if (!setting)
      return;

   if (setting->index_offset < system->num_ports)
      desc = libretro_find_controller_description(
            &system->ports[setting->index_offset],
            settings->input.libretro_device
            [setting->index_offset]);

   if (desc)
      name = desc->desc;

   if (!name)
   {
      /* Find generic name. */

      switch (settings->input.libretro_device
            [setting->index_offset])
      {
         case RETRO_DEVICE_NONE:
            name = menu_hash_to_str(MENU_VALUE_NONE);
            break;
         case RETRO_DEVICE_JOYPAD:
            name = menu_hash_to_str(MENU_VALUE_RETROPAD);
            break;
         case RETRO_DEVICE_ANALOG:
            name = "RetroPad w/ Analog";
            break;
         default:
            name = menu_hash_to_str(MENU_VALUE_UNKNOWN);
            break;
      }
   }

   strlcpy(s, name, len);
}

static void setting_get_string_representation_uint_archive_mode(void *data,
      char *s, size_t len)
{
   const char          *name = menu_hash_to_str(MENU_VALUE_UNKNOWN);
   settings_t      *settings = config_get_ptr();

   (void)data;

   switch (settings->archive.mode)
   {
      case 0:
         name = menu_hash_to_str(MENU_VALUE_ASK_ARCHIVE);
         break;
      case 1:
         name = menu_hash_to_str(MENU_VALUE_LOAD_ARCHIVE);
         break;
      case 2:
         name = menu_hash_to_str(MENU_VALUE_OPEN_ARCHIVE);
         break;
   }

   strlcpy(s, name, len);
}

static void setting_get_string_representation_uint_analog_dpad_mode(void *data,
      char *s, size_t len)
{
   const char *modes[3];
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();

   modes[0] = menu_hash_to_str(MENU_VALUE_NONE);
   modes[1] = menu_hash_to_str(MENU_VALUE_LEFT_ANALOG);
   modes[2] = menu_hash_to_str(MENU_VALUE_RIGHT_ANALOG);

   if (setting)
      strlcpy(s, modes[settings->input.analog_dpad_mode
            [setting->index_offset] % ANALOG_DPAD_LAST],
            len);
}

static void setting_get_string_representation_uint_autosave_interval(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;
   if (!setting)
      return;

   if (*setting->value.unsigned_integer)
      snprintf(s, len, "%u %s",
            *setting->value.unsigned_integer, menu_hash_to_str(MENU_VALUE_SECONDS));
   else
      strlcpy(s, menu_hash_to_str(MENU_VALUE_OFF), len);
}

static void setting_get_string_representation_uint_user_language(void *data,
      char *s, size_t len)
{
   const char *modes[RETRO_LANGUAGE_LAST];
   settings_t      *settings = config_get_ptr();

   modes[RETRO_LANGUAGE_ENGLISH]             = menu_hash_to_str(MENU_VALUE_LANG_ENGLISH);
   modes[RETRO_LANGUAGE_JAPANESE]            = menu_hash_to_str(MENU_VALUE_LANG_JAPANESE);
   modes[RETRO_LANGUAGE_FRENCH]              = menu_hash_to_str(MENU_VALUE_LANG_FRENCH);
   modes[RETRO_LANGUAGE_SPANISH]             = menu_hash_to_str(MENU_VALUE_LANG_SPANISH);
   modes[RETRO_LANGUAGE_GERMAN]              = menu_hash_to_str(MENU_VALUE_LANG_GERMAN);
   modes[RETRO_LANGUAGE_ITALIAN]             = menu_hash_to_str(MENU_VALUE_LANG_ITALIAN);
   modes[RETRO_LANGUAGE_DUTCH]               = menu_hash_to_str(MENU_VALUE_LANG_DUTCH);
   modes[RETRO_LANGUAGE_PORTUGUESE]          = menu_hash_to_str(MENU_VALUE_LANG_PORTUGUESE);
   modes[RETRO_LANGUAGE_RUSSIAN]             = menu_hash_to_str(MENU_VALUE_LANG_RUSSIAN);
   modes[RETRO_LANGUAGE_KOREAN]              = menu_hash_to_str(MENU_VALUE_LANG_KOREAN);
   modes[RETRO_LANGUAGE_CHINESE_TRADITIONAL] = menu_hash_to_str(MENU_VALUE_LANG_CHINESE_TRADITIONAL);
   modes[RETRO_LANGUAGE_CHINESE_SIMPLIFIED]  = menu_hash_to_str(MENU_VALUE_LANG_CHINESE_SIMPLIFIED);
   modes[RETRO_LANGUAGE_ESPERANTO]           = menu_hash_to_str(MENU_VALUE_LANG_ESPERANTO);

   if (settings)
      strlcpy(s, modes[settings->user_language], len);
}

static void setting_get_string_representation_uint_libretro_log_level(void *data,
      char *s, size_t len)
{
   static const char *modes[] = {
      "0 (Debug)",
      "1 (Info)",
      "2 (Warning)",
      "3 (Error)"
   };
   rarch_setting_t *setting = (rarch_setting_t*)data;

   if (setting)
      strlcpy(s, modes[*setting->value.unsigned_integer],
            len);
}

static void setting_get_string_representation_uint(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;
   if (setting)
      snprintf(s, len, "%u",
            *setting->value.unsigned_integer);
}

static void setting_get_string_representation_hex(void *data,
      char *s, size_t len)
{
   rarch_setting_t *setting = (rarch_setting_t*)data;
   if (setting)
      snprintf(s, len, "%08x",
            *setting->value.unsigned_integer);
}

/**
 ******* LIST BUILDING HELPER FUNCTIONS *******
**/

/**
 * setting_action_setting:
 * @name               : Name of setting.
 * @short_description  : Short description of setting.
 * @group              : Group that the setting belongs to.
 * @subgroup           : Subgroup that the setting belongs to.
 *
 * Initializes a setting of type ST_ACTION.
 *
 * Returns: setting of type ST_ACTION.
 **/
static rarch_setting_t setting_action_setting(const char* name,
      const char* short_description,
      const char *group, const char *subgroup,
      const char *parent_group)
{
   rarch_setting_t result = {ST_NONE};

   result.type                      = ST_ACTION;
   result.name                      = name;

   result.short_description         = short_description;
   result.parent_group              = parent_group;
   result.group                     = group;
   result.subgroup                  = subgroup;
   result.change_handler            = NULL;
   result.deferred_handler          = NULL;
   result.read_handler              = NULL;
   result.get_string_representation = &setting_get_string_representation_default;
   result.action_start              = NULL;
   result.action_iterate            = NULL;
   result.action_left               = NULL;
   result.action_right              = NULL;
   result.action_ok                 = setting_action_action_ok;
   result.action_select             = setting_action_action_ok;
   result.action_cancel             = NULL;

   return result;
}

/**
 * setting_group_setting:
 * @type               : type of settting.
 * @name               : name of setting.
 *
 * Initializes a setting of type ST_GROUP.
 *
 * Returns: setting of type ST_GROUP.
 **/
static rarch_setting_t setting_group_setting(enum setting_type type, const char* name,
      const char *parent_group)
{
   rarch_setting_t result   = {ST_NONE};

   result.parent_group      = parent_group;
   result.type              = type;
   result.name              = name;
   result.short_description = name;

   result.get_string_representation       = &setting_get_string_representation_default;

   return result;
}

/**
 * setting_subgroup_setting:
 * @type               : type of settting.
 * @name               : name of setting.
 * @parent_name        : group that the subgroup setting belongs to.
 *
 * Initializes a setting of type ST_SUBGROUP.
 *
 * Returns: setting of type ST_SUBGROUP.
 **/
static rarch_setting_t setting_subgroup_setting(enum setting_type type,
      const char* name, const char *parent_name, const char *parent_group)
{
   rarch_setting_t result   = {ST_NONE};

   result.type              = type;
   result.name              = name;

   result.short_description = name;
   result.group             = parent_name;
   result.parent_group      = parent_group;

   result.get_string_representation       = &setting_get_string_representation_default;

   return result;
}

/**
 * setting_float_setting:
 * @name               : name of setting.
 * @short_description  : Short description of setting.
 * @target             : Target of float setting.
 * @default_value      : Default value (in float).
 * @rounding           : Rounding (for float-to-string representation).
 * @group              : Group that the setting belongs to.
 * @subgroup           : Subgroup that the setting belongs to.
 * @change_handler     : Function callback for change handler function pointer.
 * @read_handler       : Function callback for read handler function pointer.
 *
 * Initializes a setting of type ST_FLOAT.
 *
 * Returns: setting of type ST_FLOAT.
 **/
static rarch_setting_t setting_float_setting(const char* name,
      const char* short_description, float* target, float default_value,
      const char *rounding, const char *group, const char *subgroup,
      const char *parent_group,
      change_handler_t change_handler, change_handler_t read_handler)
{
   rarch_setting_t result         = {ST_NONE};

   result.type                    = ST_FLOAT;
   result.name                    = name;
   result.size                    = sizeof(float);
   result.short_description       = short_description;
   result.group                   = group;
   result.subgroup                = subgroup;
   result.parent_group            = parent_group;

   result.rounding_fraction       = rounding;
   result.change_handler          = change_handler;
   result.read_handler            = read_handler;
   result.value.fraction          = target;
   result.original_value.fraction = *target;
   result.default_value.fraction  = default_value;
   result.action_start            = setting_generic_action_start_default;
   result.action_left             = setting_fraction_action_left_default;
   result.action_right            = setting_fraction_action_right_default;
   result.action_ok               = setting_generic_action_ok_default;
   result.action_select           = setting_generic_action_ok_default;
   result.action_cancel           = NULL;

   result.get_string_representation       = &setting_get_string_representation_st_float;

   return result;
}

/**
 * setting_bool_setting:
 * @name               : name of setting.
 * @short_description  : Short description of setting.
 * @target             : Target of bool setting.
 * @default_value      : Default value (in bool format).
 * @off                : String value for "Off" label.
 * @on                 : String value for "On"  label.
 * @group              : Group that the setting belongs to.
 * @subgroup           : Subgroup that the setting belongs to.
 * @change_handler     : Function callback for change handler function pointer.
 * @read_handler       : Function callback for read handler function pointer.
 *
 * Initializes a setting of type ST_BOOL.
 *
 * Returns: setting of type ST_BOOL.
 **/
static rarch_setting_t setting_bool_setting(const char* name,
      const char* short_description, bool* target, bool default_value,
      const char *off, const char *on,
      const char *group, const char *subgroup, const char *parent_group,
      change_handler_t change_handler, change_handler_t read_handler)
{
   rarch_setting_t result        = {ST_NONE};

   result.type                   = ST_BOOL;
   result.name                   = name;
   result.size                   = sizeof(bool);
   result.short_description      = short_description;
   result.group                  = group;
   result.subgroup               = subgroup;
   result.parent_group           = parent_group;

   result.change_handler         = change_handler;
   result.read_handler           = read_handler;
   result.value.boolean          = target;
   result.original_value.boolean = *target;
   result.default_value.boolean  = default_value;
   result.boolean.off_label      = off;
   result.boolean.on_label       = on;

   result.action_start           = setting_generic_action_start_default;
   result.action_left            = setting_bool_action_toggle_default;
   result.action_right           = setting_bool_action_toggle_default;
   result.action_ok              = setting_generic_action_ok_default;
   result.action_select          = setting_generic_action_ok_default;
   result.action_cancel          = NULL;

   result.get_string_representation       = &setting_get_string_representation_st_bool;
   return result;
}

/**
 * setting_int_setting:
 * @name               : name of setting.
 * @short_description  : Short description of setting.
 * @target             : Target of signed integer setting.
 * @default_value      : Default value (in signed integer format).
 * @group              : Group that the setting belongs to.
 * @subgroup           : Subgroup that the setting belongs to.
 * @change_handler     : Function callback for change handler function pointer.
 * @read_handler       : Function callback for read handler function pointer.
 *
 * Initializes a setting of type ST_INT. 
 *
 * Returns: setting of type ST_INT.
 **/
#if 0
static rarch_setting_t setting_int_setting(const char* name,
      const char* short_description, int* target, int default_value,
      const char *group, const char *subgroup, const char *parent_group,
      change_handler_t change_handler,
      change_handler_t read_handler)
{
   rarch_setting_t result        = {0};

   result.type                   = ST_INT;
   result.name                   = name;
   result.size                   = sizeof(int);
   result.short_description      = short_description;
   result.group                  = group;
   result.subgroup               = subgroup;
   result.parent_group           = parent_group;

   result.change_handler         = change_handler;
   result.read_handler           = read_handler;
   result.value.integer          = target;
   result.original_value.integer = *target;
   result.default_value.integer  = default_value;

   result.get_string_representation       = &setting_get_string_representation_int;

   return result;
}
#endif

/**
 * setting_uint_setting:
 * @name               : name of setting.
 * @short_description  : Short description of setting.
 * @target             : Target of unsigned integer setting.
 * @default_value      : Default value (in unsigned integer format).
 * @group              : Group that the setting belongs to.
 * @subgroup           : Subgroup that the setting belongs to.
 * @change_handler     : Function callback for change handler function pointer.
 * @read_handler       : Function callback for read handler function pointer.
 *
 * Initializes a setting of type ST_UINT. 
 *
 * Returns: setting of type ST_UINT.
 **/
static rarch_setting_t setting_uint_setting(const char* name,
      const char* short_description, unsigned int* target,
      unsigned int default_value,
      const char *group, const char *subgroup, const char *parent_group,
      change_handler_t change_handler, change_handler_t read_handler)
{
   rarch_setting_t result                 = {ST_NONE};

   result.type                            = ST_UINT;
   result.name                            = name;
   result.size                            = sizeof(unsigned int);
   result.short_description               = short_description;
   result.group                           = group;
   result.subgroup                        = subgroup;
   result.parent_group                    = parent_group;

   result.change_handler                  = change_handler;
   result.read_handler                    = read_handler;
   result.value.unsigned_integer          = target;
   result.original_value.unsigned_integer = *target;
   result.default_value.unsigned_integer  = default_value;
   result.action_start                    = setting_generic_action_start_default;
   result.action_left                     = setting_uint_action_left_default;
   result.action_right                    = setting_uint_action_right_default;
   result.action_ok                       = setting_generic_action_ok_default;
   result.action_select                   = setting_generic_action_ok_default;
   result.action_cancel                   = NULL;
   result.get_string_representation       = &setting_get_string_representation_uint;

   return result;
}

/**
 * setting_hex_setting:
 * @name               : name of setting.
 * @short_description  : Short description of setting.
 * @target             : Target of unsigned integer setting.
 * @default_value      : Default value (in unsigned integer format).
 * @group              : Group that the setting belongs to.
 * @subgroup           : Subgroup that the setting belongs to.
 * @change_handler     : Function callback for change handler function pointer.
 * @read_handler       : Function callback for read handler function pointer.
 *
 * Initializes a setting of type ST_HEX.
 *
 * Returns: setting of type ST_HEX.
 **/
static rarch_setting_t setting_hex_setting(const char* name,
      const char* short_description, unsigned int* target,
      unsigned int default_value,
      const char *group, const char *subgroup, const char *parent_group,
      change_handler_t change_handler, change_handler_t read_handler)
{
   rarch_setting_t result                 = {ST_NONE};

   result.type                            = ST_HEX;
   result.name                            = name;
   result.size                            = sizeof(unsigned int);
   result.short_description               = short_description;
   result.group                           = group;
   result.subgroup                        = subgroup;
   result.parent_group                    = parent_group;

   result.change_handler                  = change_handler;
   result.read_handler                    = read_handler;
   result.value.unsigned_integer          = target;
   result.original_value.unsigned_integer = *target;
   result.default_value.unsigned_integer  = default_value;
   result.action_start                    = setting_generic_action_start_default;
   result.action_left                     = NULL;
   result.action_right                    = NULL;
   result.action_ok                       = setting_generic_action_ok_default;
   result.action_select                   = setting_generic_action_ok_default;
   result.action_cancel                   = NULL;
   result.get_string_representation       = &setting_get_string_representation_hex;

   return result;
}

/**
 * setting_bind_setting:
 * @name               : name of setting.
 * @short_description  : Short description of setting.
 * @target             : Target of bind setting.
 * @idx                : Index of bind setting.
 * @idx_offset         : Index offset of bind setting.
 * @default_value      : Default value (in bind format).
 * @group              : Group that the setting belongs to.
 * @subgroup           : Subgroup that the setting belongs to.
 *
 * Initializes a setting of type ST_BIND. 
 *
 * Returns: setting of type ST_BIND.
 **/
static rarch_setting_t setting_bind_setting(const char* name,
      const char* short_description, struct retro_keybind* target,
      uint32_t idx, uint32_t idx_offset,
      const struct retro_keybind* default_value,
      const char *group, const char *subgroup, const char *parent_group)
{
   rarch_setting_t result       = {ST_NONE};

   result.type                  = ST_BIND;
   result.name                  = name;
   result.size                  = 0;
   result.short_description     = short_description;
   result.group                 = group;
   result.subgroup              = subgroup;
   result.parent_group          = parent_group;

   result.value.keybind         = target;
   result.default_value.keybind = default_value;
   result.index                 = idx;
   result.index_offset          = idx_offset;
   result.action_start          = setting_bind_action_start;
   result.action_ok             = setting_bind_action_ok;
   result.action_select         = setting_bind_action_ok;
   result.action_cancel         = NULL;
   result.get_string_representation       = &setting_get_string_representation_st_bind;

   return result;
}

/**
 * setting_string_setting:
 * @type               : type of setting.
 * @name               : name of setting.
 * @short_description  : Short description of setting.
 * @target             : Target of string setting.
 * @size               : Size of string setting.
 * @default_value      : Default value (in string format).
 * @empty              : TODO/FIXME: ???
 * @group              : Group that the setting belongs to.
 * @subgroup           : Subgroup that the setting belongs to.
 * @change_handler     : Function callback for change handler function pointer.
 * @read_handler       : Function callback for read handler function pointer.
 *
 * Initializes a string setting (of type @type). 
 *
 * Returns: String setting of type @type.
 **/
rarch_setting_t setting_string_setting(enum setting_type type,
      const char* name, const char* short_description, char* target,
      unsigned size, const char* default_value, const char *empty,
      const char *group, const char *subgroup, const char *parent_group,
      change_handler_t change_handler,
      change_handler_t read_handler)
{
   rarch_setting_t result      = {ST_NONE};

   result.type                 = type;
   result.name                 = name;
   result.size                 = size;
   result.short_description    = short_description;
   result.group                = group;
   result.subgroup             = subgroup;
   result.parent_group         = parent_group;

   result.dir.empty_path       = empty;
   result.change_handler       = change_handler;
   result.read_handler         = read_handler;
   result.value.string         = target;
   result.default_value.string = default_value;
   result.action_start         = NULL;
   result.get_string_representation       = &setting_get_string_representation_st_string;

   switch (type)
   {
      case ST_DIR:
         result.action_start           = setting_string_action_start_generic;
         result.browser_selection_type = ST_DIR;
         result.get_string_representation = &setting_get_string_representation_st_dir;
         break;
      case ST_PATH:
         result.action_start           = setting_string_action_start_generic;
         result.browser_selection_type = ST_PATH;
         result.get_string_representation = &setting_get_string_representation_st_path;
         break;
      default:
         break;
   }

   return result;
}

/**
 * setting_string_setting_options:
 * @type               : type of settting.
 * @name               : name of setting.
 * @short_description  : Short description of setting.
 * @target             : Target of bind setting.
 * @size               : Size of string setting.
 * @default_value      : Default value.
 * @empty              : N/A.
 * @values             : Values, separated by a delimiter.
 * @group              : Group that the setting belongs to.
 * @subgroup           : Subgroup that the setting belongs to.
 * @change_handler     : Function callback for change handler function pointer.
 * @read_handler       : Function callback for read handler function pointer.
 *
 * Initializes a string options list setting. 
 *
 * Returns: string option list setting.
 **/
static rarch_setting_t setting_string_setting_options(enum setting_type type,
      const char* name, const char* short_description, char* target,
      unsigned size, const char* default_value,
      const char *empty, const char *values,
      const char *group, const char *subgroup, const char *parent_group,
      change_handler_t change_handler, change_handler_t read_handler)
{
  rarch_setting_t result = setting_string_setting(type, name,
        short_description, target, size, default_value, empty, group,
        subgroup, parent_group, change_handler, read_handler);

  result.parent_group    = parent_group;
  result.values          = values;
  return result;
}

/**
 * setting_get_description:
 * @label              : identifier label of setting
 * @s                  : output message 
 * @len                : size of @s
 *
 * Writes a 'Help' description message to @s if there is
 * one available based on the identifier label of the setting
 * (@label).
 *
 * Returns: 0 (always for now). TODO: make it handle -1 as well.
 **/
int setting_get_description(const char *label, char *s,
      size_t len)
{
   uint32_t label_hash       = menu_hash_calculate(label);

   return menu_hash_get_help(label_hash, s, len);
}

static void get_string_representation_bind_device(void * data, char *s,
      size_t len)
{
   unsigned map = 0;
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();

   if (!setting)
      return;

   map = settings->input.joypad_map[setting->index_offset];

   if (map < settings->input.max_users)
   {
      const char *device_name = settings->input.device_names[map];

      if (*device_name)
         strlcpy(s, device_name, len);
      else
         snprintf(s, len,
               "%s (%s #%u)",
               menu_hash_to_str(MENU_VALUE_NOT_AVAILABLE),
               menu_hash_to_str(MENU_VALUE_PORT),
               map);
   }
   else
      strlcpy(s, menu_hash_to_str(MENU_VALUE_DISABLED), len);
}


/**
 * setting_get_label:
 * @list               : File list on which to perform the search
 * @s                  : String for the type to be represented on-screen as
 *                       a label.
 * @len                : Size of @s
 * @w                  : Width of the string (for text label representation
 *                       purposes in the menu display driver).
 * @type               : Identifier of setting.
 * @menu_label         : Menu Label identifier of setting.
 * @label              : Label identifier of setting.
 * @idx                : Index identifier of setting.
 *
 * Get associated label of a setting.
 **/
void setting_get_label(file_list_t *list, char *s,
      size_t len, unsigned *w, unsigned type, 
      const char *menu_label, const char *label, unsigned idx)
{
   rarch_setting_t *setting      = NULL;
   if (!label)
      return;

   setting = menu_setting_find(list->list[idx].label);

   if (setting)
      setting_get_string_representation(setting, s, len);
}

static void general_read_handler(void *data)
{
   rarch_setting_t *setting  = (rarch_setting_t*)data;
   settings_t      *settings = config_get_ptr();
   uint32_t hash             = setting ? menu_hash_calculate(setting->name) : 0;

   if (!setting)
      return;

   switch (hash)
   {
      case MENU_LABEL_AUDIO_RATE_CONTROL_DELTA:
         *setting->value.fraction = settings->audio.rate_control_delta;
         if (*setting->value.fraction < 0.0005)
         {
            settings->audio.rate_control = false;
            settings->audio.rate_control_delta = 0.0;
         }
         else
         {
            settings->audio.rate_control = true;
            settings->audio.rate_control_delta = *setting->value.fraction;
         }
         break;
      case MENU_LABEL_AUDIO_MAX_TIMING_SKEW:
         *setting->value.fraction = settings->audio.max_timing_skew;
         break;
      case MENU_LABEL_VIDEO_REFRESH_RATE_AUTO:
         *setting->value.fraction = settings->video.refresh_rate;
         break;
      case MENU_LABEL_INPUT_PLAYER1_JOYPAD_INDEX:
         *setting->value.integer = settings->input.joypad_map[0];
         break;
      case MENU_LABEL_INPUT_PLAYER2_JOYPAD_INDEX:
         *setting->value.integer = settings->input.joypad_map[1];
         break;
      case MENU_LABEL_INPUT_PLAYER3_JOYPAD_INDEX:
         *setting->value.integer = settings->input.joypad_map[2];
         break;
      case MENU_LABEL_INPUT_PLAYER4_JOYPAD_INDEX:
         *setting->value.integer = settings->input.joypad_map[3];
         break;
      case MENU_LABEL_INPUT_PLAYER5_JOYPAD_INDEX:
         *setting->value.integer = settings->input.joypad_map[4];
         break;
   }
}

static void general_write_handler(void *data)
{
   enum event_command rarch_cmd = EVENT_CMD_NONE;
   menu_displaylist_info_t info = {0};
   rarch_setting_t *setting = (rarch_setting_t*)data;
   settings_t *settings     = config_get_ptr();
   driver_t *driver         = driver_get_ptr();
   global_t *global         = global_get_ptr();
   menu_list_t *menu_list   = menu_list_get_ptr();
   rarch_system_info_t *system = rarch_system_info_get_ptr();
   uint32_t hash            = setting ? menu_hash_calculate(setting->name) : 0;

   if (!setting)
      return;

   if (setting->cmd_trigger.idx != EVENT_CMD_NONE)
   {
      if (setting->flags & SD_FLAG_EXIT)
      {
         if (*setting->value.boolean)
            *setting->value.boolean = false;
      }
      if (setting->cmd_trigger.triggered ||
            (setting->flags & SD_FLAG_CMD_APPLY_AUTO))
         rarch_cmd = setting->cmd_trigger.idx;
   }

   switch (hash)
   {
      case MENU_LABEL_HELP:
         if (!menu_list)
            return;

         if (*setting->value.boolean)
         {
            info.list          = menu_list->menu_stack;
            info.type          = 0; 
            info.directory_ptr = 0;
            strlcpy(info.label,
                  menu_hash_to_str(MENU_LABEL_HELP), sizeof(info.label));

            menu_displaylist_push_list(&info, DISPLAYLIST_GENERIC);
            setting_set_with_string_representation(setting, "false");
         }
         break;
      case MENU_LABEL_AUDIO_MAX_TIMING_SKEW:
         settings->audio.max_timing_skew = *setting->value.fraction;
         break;
      case MENU_LABEL_AUDIO_RATE_CONTROL_DELTA:
         if (*setting->value.fraction < 0.0005)
         {
            settings->audio.rate_control = false;
            settings->audio.rate_control_delta = 0.0;
         }
         else
         {
            settings->audio.rate_control = true;
            settings->audio.rate_control_delta = *setting->value.fraction;
         }
         break;
      case MENU_LABEL_VIDEO_REFRESH_RATE_AUTO:
         if (driver->video && driver->video_data)
         {
            driver_set_refresh_rate(*setting->value.fraction);

            /* In case refresh rate update forced non-block video. */
            rarch_cmd = EVENT_CMD_VIDEO_SET_BLOCKING_STATE;
         }
         break;
      case MENU_LABEL_VIDEO_SCALE:
         settings->video.scale = roundf(*setting->value.fraction);

         if (!settings->video.fullscreen)
            rarch_cmd = EVENT_CMD_REINIT;
         break;
      case MENU_LABEL_INPUT_PLAYER1_JOYPAD_INDEX:
         settings->input.joypad_map[0] = *setting->value.integer;
         break;
      case MENU_LABEL_INPUT_PLAYER2_JOYPAD_INDEX:
         settings->input.joypad_map[1] = *setting->value.integer;
         break;
      case MENU_LABEL_INPUT_PLAYER3_JOYPAD_INDEX:
         settings->input.joypad_map[2] = *setting->value.integer;
         break;
      case MENU_LABEL_INPUT_PLAYER4_JOYPAD_INDEX:
         settings->input.joypad_map[3] = *setting->value.integer;
         break;
      case MENU_LABEL_INPUT_PLAYER5_JOYPAD_INDEX:
         settings->input.joypad_map[4] = *setting->value.integer;
         break;
      case MENU_LABEL_LOG_VERBOSITY:
         global->verbosity         = *setting->value.boolean;
         global->has_set_verbosity = *setting->value.boolean;
         break;
      case MENU_LABEL_VIDEO_SMOOTH:
         video_driver_set_filtering(1, settings->video.smooth);
         break;
      case MENU_LABEL_VIDEO_ROTATION:
         video_driver_set_rotation(
               (*setting->value.unsigned_integer +
                system->rotation) % 4);
         break;
      case MENU_LABEL_AUDIO_VOLUME:
         audio_driver_set_volume_gain(db_to_gain(*setting->value.fraction));
         break;
      case MENU_LABEL_AUDIO_LATENCY:
         rarch_cmd = EVENT_CMD_AUDIO_REINIT;
         break;
      case MENU_LABEL_PAL60_ENABLE:
         if (*setting->value.boolean && global->console.screen.pal_enable)
            rarch_cmd = EVENT_CMD_REINIT;
         else
            setting_set_with_string_representation(setting, "false");
         break;
      case MENU_LABEL_SYSTEM_BGM_ENABLE:
         if (*setting->value.boolean)
         {
#if defined(__CELLOS_LV2__) && (CELL_SDK_VERSION > 0x340000)
            cellSysutilEnableBgmPlayback();
#endif         
         }
         else
         {
#if defined(__CELLOS_LV2__) && (CELL_SDK_VERSION > 0x340000)
            cellSysutilDisableBgmPlayback();
#endif
         }
         break;
      case MENU_LABEL_NETPLAY_IP_ADDRESS:
#ifdef HAVE_NETPLAY
         global->has_set_netplay_ip_address = (setting->value.string[0] != '\0');
#endif
         break;
      case MENU_LABEL_NETPLAY_MODE:
#ifdef HAVE_NETPLAY
         if (!global->netplay_is_client)
            *global->netplay_server = '\0';
         global->has_set_netplay_mode = true;
#endif
         break;
      case MENU_LABEL_NETPLAY_SPECTATOR_MODE_ENABLE:
#ifdef HAVE_NETPLAY
         if (global->netplay_is_spectate)
            *global->netplay_server = '\0';
#endif
         break;
      case MENU_LABEL_NETPLAY_DELAY_FRAMES:
#ifdef HAVE_NETPLAY
         global->has_set_netplay_delay_frames = (global->netplay_sync_frames > 0);
#endif
         break;
   }

   if (rarch_cmd || setting->cmd_trigger.triggered)
      event_command(rarch_cmd);
}

#define START_GROUP(group_info, NAME, parent_group) \
{ \
   group_info.name = NAME; \
   if (!(menu_settings_list_append(list, list_info, setting_group_setting (ST_GROUP, NAME, parent_group)))) return false; \
}

#define END_GROUP(list, list_info, parent_group) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_group_setting (ST_END_GROUP, 0, parent_group)))) return false; \
}

#define START_SUB_GROUP(list, list_info, NAME, group_info, subgroup_info, parent_group) \
{ \
   subgroup_info.name = NAME; \
   if (!(menu_settings_list_append(list, list_info, setting_subgroup_setting (ST_SUB_GROUP, NAME, group_info, parent_group)))) return false; \
}

#define END_SUB_GROUP(list, list_info, parent_group) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_group_setting (ST_END_SUB_GROUP, 0, parent_group)))) return false; \
}

#define CONFIG_ACTION(NAME, SHORT, group_info, subgroup_info, parent_group) \
{ \
   if (!menu_settings_list_append(list, list_info, setting_action_setting  (NAME, SHORT, group_info, subgroup_info, parent_group))) return false; \
}

#define CONFIG_BOOL(TARGET, NAME, SHORT, DEF, OFF, ON, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER) \
{ \
   if (!menu_settings_list_append(list, list_info, setting_bool_setting  (NAME, SHORT, &TARGET, DEF, OFF, ON, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER)))return false; \
}

#define CONFIG_INT(TARGET, NAME, SHORT, DEF, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_int_setting   (NAME, SHORT, &TARGET, DEF, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER)))) return false; \
}

#define CONFIG_UINT(TARGET, NAME, SHORT, DEF, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_uint_setting  (NAME, SHORT, &TARGET, DEF, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER)))) return false; \
}

#define CONFIG_FLOAT(TARGET, NAME, SHORT, DEF, ROUNDING, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_float_setting (NAME, SHORT, &TARGET, DEF, ROUNDING, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER)))) return false; \
}

#define CONFIG_PATH(TARGET, NAME, SHORT, DEF, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_string_setting(ST_PATH, NAME, SHORT, TARGET, sizeof(TARGET), DEF, "", group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER)))) return false; \
}

#define CONFIG_DIR(TARGET, NAME, SHORT, DEF, EMPTY, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_string_setting(ST_DIR, NAME, SHORT, TARGET, sizeof(TARGET), DEF, EMPTY, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER)))) return false; \
}

#define CONFIG_STRING(TARGET, NAME, SHORT, DEF, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_string_setting(ST_STRING, NAME, SHORT, TARGET, sizeof(TARGET), DEF, "", group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER)))) return false; \
}

#define CONFIG_STRING_OPTIONS(TARGET, NAME, SHORT, DEF, OPTS, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER) \
{ \
  if (!(menu_settings_list_append(list, list_info, setting_string_setting_options(ST_STRING, NAME, SHORT, TARGET, sizeof(TARGET), DEF, "", OPTS, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER)))) return false; \
}

#define CONFIG_HEX(TARGET, NAME, SHORT, DEF, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_hex_setting(NAME, SHORT, &TARGET, DEF, group_info, subgroup_info, parent_group, CHANGE_HANDLER, READ_HANDLER)))) return false; \
}

/* Please strdup() NAME and SHORT */
#define CONFIG_BIND(TARGET, PLAYER, PLAYER_OFFSET, NAME, SHORT, DEF, group_info, subgroup_info, parent_group) \
{ \
   if (!(menu_settings_list_append(list, list_info, setting_bind_setting  (NAME, SHORT, &TARGET, PLAYER, PLAYER_OFFSET, DEF, group_info, subgroup_info, parent_group)))) return false; \
}

#ifdef GEKKO
#define MAX_GAMMA_SETTING 2
#else
#define MAX_GAMMA_SETTING 1
#endif

static void setting_add_special_callbacks(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      unsigned values)
{
   unsigned idx = list_info->index - 1;

   if (values & SD_FLAG_ALLOW_INPUT)
   {
      (*list)[idx].action_ok     = setting_generic_action_ok_linefeed;
      (*list)[idx].action_select = setting_generic_action_ok_linefeed;

      switch ((*list)[idx].type)
      {
         case ST_UINT:
            (*list)[idx].action_cancel = NULL;
            break;
         case ST_HEX:
            (*list)[idx].action_cancel = NULL;
            break;
         case ST_STRING:
            (*list)[idx].action_start  = setting_string_action_start_generic;
            (*list)[idx].action_cancel = NULL;
            break;
         default:
            break;
      }
   }
   else if (values & SD_FLAG_IS_DRIVER)
   {
      (*list)[idx].action_left  = setting_string_action_left_driver;
      (*list)[idx].action_right = setting_string_action_right_driver;
   }
}

static void settings_data_list_current_add_flags(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      unsigned values)
{
   menu_settings_list_current_add_flags(
         list,
         list_info,
         values);
   setting_add_special_callbacks(list, list_info, values);
}

static void overlay_enable_toggle_change_handler(void *data)
{
   rarch_setting_t *setting = (rarch_setting_t *)data;

   if (!setting)
      return;

   if (setting->value.boolean)
      event_command(EVENT_CMD_OVERLAY_INIT);
   else
      event_command(EVENT_CMD_OVERLAY_DEINIT);
}

static bool setting_append_list_main_menu_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group,
      unsigned mask)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings  = config_get_ptr();
   global_t *global      = global_get_ptr();
   const char *main_menu = menu_hash_to_str(MENU_VALUE_MAIN_MENU);

   (void)settings;

   START_GROUP(group_info,main_menu, parent_group);
   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

#if defined(HAVE_DYNAMIC) || defined(HAVE_LIBRETRO_MANAGEMENT)
   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_CORE_LIST),
         menu_hash_to_str(MENU_LABEL_VALUE_CORE_LIST),
         group_info.name,
         subgroup_info.name,
         parent_group);
   (*list)[list_info->index - 1].size = sizeof(settings->libretro);
   (*list)[list_info->index - 1].value.string = settings->libretro;
   (*list)[list_info->index - 1].values = EXT_EXECUTABLES;
   /* It is not a good idea to have chosen action_toggle as the place
    * to put this callback. It should be called whenever the browser
    * needs to get the directory to browse into. It's not quite like
    * get_string_representation, but it is close. */
   (*list)[list_info->index - 1].action_left  = core_list_action_toggle;
   (*list)[list_info->index - 1].action_right = core_list_action_toggle;
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_LOAD_CORE);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_BROWSER_ACTION);
#endif

   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_LOAD_CONTENT_LIST),
         menu_hash_to_str(MENU_LABEL_VALUE_LOAD_CONTENT_LIST),
         group_info.name,
         subgroup_info.name,
         parent_group);

   if (settings->history_list_enable)
   {
      CONFIG_ACTION(
            menu_hash_to_str(MENU_LABEL_LOAD_CONTENT_HISTORY),
            menu_hash_to_str(MENU_LABEL_VALUE_LOAD_CONTENT_HISTORY),
            group_info.name,
            subgroup_info.name,
            parent_group);
   }

#if defined(HAVE_NETWORKING) || defined(HAVE_LIBRETRODB)
   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_ADD_CONTENT_LIST),
         menu_hash_to_str(MENU_LABEL_VALUE_ADD_CONTENT_LIST),
         group_info.name,
         subgroup_info.name,
         parent_group);
#endif

#ifdef HAVE_NETWORKING
   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_ONLINE_UPDATER),
         menu_hash_to_str(MENU_LABEL_VALUE_ONLINE_UPDATER),
         group_info.name,
         subgroup_info.name,
         parent_group);
#endif

   if (global->main_is_init && (global->core_type != CORE_TYPE_DUMMY))
   {
      CONFIG_ACTION(
            menu_hash_to_str(MENU_LABEL_CONTENT_SETTINGS),
            menu_hash_to_str(MENU_LABEL_VALUE_CONTENT_SETTINGS),
            group_info.name,
            subgroup_info.name,
            parent_group);
   }

   if (mask & SL_FLAG_MAIN_MENU_SETTINGS)
   {
      CONFIG_ACTION(
            menu_hash_to_str(MENU_LABEL_SETTINGS),
            menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS),
            group_info.name,
            subgroup_info.name,
            parent_group);
   }

   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_INFORMATION_LIST),
         menu_hash_to_str(MENU_LABEL_VALUE_INFORMATION_LIST),
         group_info.name,
         subgroup_info.name,
         parent_group);

#ifndef HAVE_DYNAMIC
   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_RESTART_RETROARCH),
         menu_hash_to_str(MENU_LABEL_VALUE_RESTART_RETROARCH),
         group_info.name,
         subgroup_info.name,
         parent_group);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_RESTART_RETROARCH);
#endif

   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_CONFIGURATIONS),
         menu_hash_to_str(MENU_LABEL_VALUE_CONFIGURATIONS),
         group_info.name,
         subgroup_info.name,
         parent_group);

   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_SAVE_NEW_CONFIG),
         menu_hash_to_str(MENU_LABEL_VALUE_SAVE_NEW_CONFIG),
         group_info.name,
         subgroup_info.name,
         parent_group);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_MENU_SAVE_CONFIG);

   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_HELP),
         menu_hash_to_str(MENU_LABEL_VALUE_HELP),
         group_info.name,
         subgroup_info.name,
         parent_group);

#if !defined(IOS)
   /* Apple rejects iOS apps that lets you forcibly quit an application. */
   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_QUIT_RETROARCH),
         menu_hash_to_str(MENU_LABEL_VALUE_QUIT_RETROARCH),
         group_info.name,
         subgroup_info.name,
         parent_group);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_QUIT_RETROARCH);
#endif

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_driver_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
   
   START_GROUP(group_info, menu_hash_to_str(MENU_LABEL_VALUE_DRIVER_SETTINGS), parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name,
         subgroup_info, parent_group);
   
   CONFIG_STRING_OPTIONS(
         settings->input.driver,
         menu_hash_to_str(MENU_LABEL_INPUT_DRIVER),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_DRIVER),
         config_get_default_input(),
         config_get_input_driver_options(),
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DRIVER);

   CONFIG_STRING_OPTIONS(
         settings->input.joypad_driver,
         menu_hash_to_str(MENU_LABEL_JOYPAD_DRIVER),
         menu_hash_to_str(MENU_LABEL_VALUE_JOYPAD_DRIVER),
         config_get_default_joypad(),
         config_get_joypad_driver_options(),
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DRIVER);

   CONFIG_STRING_OPTIONS(
         settings->video.driver,
         menu_hash_to_str(MENU_LABEL_VIDEO_DRIVER),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_DRIVER),
         config_get_default_video(),
         config_get_video_driver_options(),
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DRIVER);

   CONFIG_STRING_OPTIONS(
         settings->audio.driver,
         menu_hash_to_str(MENU_LABEL_AUDIO_DRIVER),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_DRIVER),
         config_get_default_audio(),
         config_get_audio_driver_options(),
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DRIVER);

   CONFIG_STRING_OPTIONS(
         settings->audio.resampler,
         menu_hash_to_str(MENU_LABEL_AUDIO_RESAMPLER_DRIVER),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_RESAMPLER_DRIVER),
         config_get_default_audio_resampler(),
         config_get_audio_resampler_driver_options(),
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DRIVER);

   CONFIG_STRING_OPTIONS(
         settings->camera.driver,
         menu_hash_to_str(MENU_LABEL_CAMERA_DRIVER),
         menu_hash_to_str(MENU_LABEL_VALUE_CAMERA_DRIVER),
         config_get_default_camera(),
         config_get_camera_driver_options(),
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DRIVER);

   CONFIG_STRING_OPTIONS(
         settings->location.driver,
         menu_hash_to_str(MENU_LABEL_LOCATION_DRIVER),
         menu_hash_to_str(MENU_LABEL_VALUE_LOCATION_DRIVER),
         config_get_default_location(),
         config_get_location_driver_options(),
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DRIVER);

   CONFIG_STRING_OPTIONS(
         settings->menu.driver,
         menu_hash_to_str(MENU_LABEL_MENU_DRIVER),
         menu_hash_to_str(MENU_LABEL_VALUE_MENU_DRIVER),
         config_get_default_menu(),
         config_get_menu_driver_options(),
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DRIVER);

   CONFIG_STRING_OPTIONS(
         settings->record.driver,
         menu_hash_to_str(MENU_LABEL_RECORD_DRIVER),
         menu_hash_to_str(MENU_LABEL_VALUE_RECORD_DRIVER),
         config_get_default_record(),
         config_get_record_driver_options(),
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DRIVER);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_core_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info, menu_hash_to_str(MENU_LABEL_VALUE_CORE_SETTINGS), parent_group);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info,
         parent_group);

   CONFIG_BOOL(
         settings->video.shared_context,
         menu_hash_to_str(MENU_LABEL_VIDEO_SHARED_CONTEXT),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_SHARED_CONTEXT),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->load_dummy_on_core_shutdown,
         menu_hash_to_str(MENU_LABEL_DUMMY_ON_CORE_SHUTDOWN),
         menu_hash_to_str(MENU_LABEL_VALUE_DUMMY_ON_CORE_SHUTDOWN),
         load_dummy_on_core_shutdown,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->core.set_supports_no_game_enable,
         menu_hash_to_str(MENU_LABEL_CORE_SET_SUPPORTS_NO_CONTENT_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_CORE_SET_SUPPORTS_NO_CONTENT_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_configuration_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_CONFIGURATION_SETTINGS), parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info,
         parent_group);

   CONFIG_BOOL(settings->config_save_on_exit,
         menu_hash_to_str(MENU_LABEL_CONFIG_SAVE_ON_EXIT),
         menu_hash_to_str(MENU_LABEL_VALUE_CONFIG_SAVE_ON_EXIT),
         config_save_on_exit,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->core_specific_config,
         menu_hash_to_str(MENU_LABEL_CORE_SPECIFIC_CONFIG),
         menu_hash_to_str(MENU_LABEL_VALUE_CORE_SPECIFIC_CONFIG),
         default_core_specific_config,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->auto_overrides_enable,
         menu_hash_to_str(MENU_LABEL_AUTO_OVERRIDES_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_AUTO_OVERRIDES_ENABLE),
         default_auto_overrides_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->auto_remaps_enable,
         menu_hash_to_str(MENU_LABEL_AUTO_REMAPS_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_AUTO_REMAPS_ENABLE),
         default_auto_remaps_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_saving_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info, menu_hash_to_str(MENU_LABEL_VALUE_SAVING_SETTINGS), parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info,
         parent_group);

   CONFIG_BOOL(
         settings->sort_savefiles_enable,
         menu_hash_to_str(MENU_LABEL_SORT_SAVEFILES_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_SORT_SAVEFILES_ENABLE),
         default_sort_savefiles_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->sort_savestates_enable,
         menu_hash_to_str(MENU_LABEL_SORT_SAVESTATES_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_SORT_SAVESTATES_ENABLE),
         default_sort_savestates_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->block_sram_overwrite,
         menu_hash_to_str(MENU_LABEL_BLOCK_SRAM_OVERWRITE),
         menu_hash_to_str(MENU_LABEL_VALUE_BLOCK_SRAM_OVERWRITE),
         block_sram_overwrite,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

#ifdef HAVE_THREADS
   CONFIG_UINT(
         settings->autosave_interval,
         menu_hash_to_str(MENU_LABEL_AUTOSAVE_INTERVAL),
         menu_hash_to_str(MENU_LABEL_VALUE_AUTOSAVE_INTERVAL),
         autosave_interval,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_AUTOSAVE_INIT);
   menu_settings_list_current_add_range(list, list_info, 0, 0, 10, true, false);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO);
   (*list)[list_info->index - 1].get_string_representation = 
      &setting_get_string_representation_uint_autosave_interval;
#endif

   CONFIG_BOOL(
         settings->savestate_auto_index,
         menu_hash_to_str(MENU_LABEL_SAVESTATE_AUTO_INDEX),
         menu_hash_to_str(MENU_LABEL_VALUE_SAVESTATE_AUTO_INDEX),
         savestate_auto_index,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->savestate_auto_save,
         menu_hash_to_str(MENU_LABEL_SAVESTATE_AUTO_SAVE),
         menu_hash_to_str(MENU_LABEL_VALUE_SAVESTATE_AUTO_SAVE),
         savestate_auto_save,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->savestate_auto_load,
         menu_hash_to_str(MENU_LABEL_SAVESTATE_AUTO_LOAD),
         menu_hash_to_str(MENU_LABEL_VALUE_SAVESTATE_AUTO_LOAD),
         savestate_auto_load,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_logging_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
   global_t   *global   = global_get_ptr();

   START_GROUP(group_info, menu_hash_to_str(MENU_LABEL_VALUE_LOGGING_SETTINGS), parent_group);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info,
         parent_group);

   CONFIG_BOOL(
         global->verbosity,
         menu_hash_to_str(MENU_LABEL_LOG_VERBOSITY),
         menu_hash_to_str(MENU_LABEL_VALUE_LOG_VERBOSITY),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);


   CONFIG_UINT(settings->libretro_log_level,
         menu_hash_to_str(MENU_LABEL_LIBRETRO_LOG_LEVEL),
         menu_hash_to_str(MENU_LABEL_VALUE_LIBRETRO_LOG_LEVEL),
         libretro_log_level,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 3, 1.0, true, true);
   (*list)[list_info->index - 1].get_string_representation = 
      &setting_get_string_representation_uint_libretro_log_level;
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   END_SUB_GROUP(list, list_info, parent_group);

   START_SUB_GROUP(list, list_info, "Performance Counters", group_info.name, subgroup_info,
         parent_group);

   CONFIG_BOOL(global->perfcnt_enable,
         menu_hash_to_str(MENU_LABEL_PERFCNT_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_PERFCNT_ENABLE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_frame_throttling_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_FRAME_THROTTLE_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         settings->fastforward_ratio_throttle_enable,
         menu_hash_to_str(MENU_LABEL_FRAME_THROTTLE_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_FRAME_THROTTLE_ENABLE),
         fastforward_ratio_throttle_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_FLOAT(
         settings->fastforward_ratio,
         menu_hash_to_str(MENU_LABEL_FASTFORWARD_RATIO),
         menu_hash_to_str(MENU_LABEL_VALUE_FASTFORWARD_RATIO),
         fastforward_ratio,
         "%.1fx",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1, 10, 0.1, true, true);

   CONFIG_FLOAT(
         settings->slowmotion_ratio,
         menu_hash_to_str(MENU_LABEL_SLOWMOTION_RATIO),
         menu_hash_to_str(MENU_LABEL_VALUE_SLOWMOTION_RATIO),
         slowmotion_ratio,
         "%.1fx",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1, 10, 1.0, true, true);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_rewind_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info, const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info, menu_hash_to_str(MENU_LABEL_VALUE_REWIND_SETTINGS), parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);


   CONFIG_BOOL(
         settings->rewind_enable,
         menu_hash_to_str(MENU_LABEL_REWIND_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_REWIND_ENABLE),
         rewind_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_REWIND_TOGGLE);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO);
#if 0
   CONFIG_SIZE(
         settings->rewind_buffer_size,
         "rewind_buffer_size",
         "Rewind Buffer Size",
         rewind_buffer_size,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler)
#endif
      CONFIG_UINT(
            settings->rewind_granularity,
            menu_hash_to_str(MENU_LABEL_REWIND_GRANULARITY),
            menu_hash_to_str(MENU_LABEL_VALUE_REWIND_GRANULARITY),
            rewind_granularity,
            group_info.name,
            subgroup_info.name,
            parent_group,
            general_write_handler,
            general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1, 32768, 1, true, false);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_recording_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info, const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
   global_t   *global   = global_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_RECORDING_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         global->record.enable,
         menu_hash_to_str(MENU_LABEL_RECORD_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_RECORD_ENABLE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_PATH(
         global->record.config,
         menu_hash_to_str(MENU_LABEL_RECORD_CONFIG),
         menu_hash_to_str(MENU_LABEL_VALUE_RECORD_CONFIG),
         "",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_values(list, list_info, "cfg");
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_EMPTY);

   CONFIG_STRING(
         global->record.path,
         menu_hash_to_str(MENU_LABEL_RECORD_PATH),
         menu_hash_to_str(MENU_LABEL_VALUE_RECORD_PATH),
         "",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);

   CONFIG_BOOL(
         global->record.use_output_dir,
         menu_hash_to_str(MENU_LABEL_RECORD_USE_OUTPUT_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_RECORD_USE_OUTPUT_DIRECTORY),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   END_SUB_GROUP(list, list_info, parent_group);

   START_SUB_GROUP(list, list_info, "Miscellaneous", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         settings->video.post_filter_record,
         menu_hash_to_str(MENU_LABEL_VIDEO_POST_FILTER_RECORD),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_POST_FILTER_RECORD),
         post_filter_record,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->video.gpu_record,
         menu_hash_to_str(MENU_LABEL_VIDEO_GPU_RECORD),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_GPU_RECORD),
         gpu_record,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_video_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   global_t *global     = global_get_ptr();
   settings_t *settings = config_get_ptr();
    
   (void)global;

   START_GROUP(group_info, menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_SETTINGS), parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(settings->fps_show,
         menu_hash_to_str(MENU_LABEL_FPS_SHOW),
         menu_hash_to_str(MENU_LABEL_VALUE_FPS_SHOW),
         fps_show,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);


   END_SUB_GROUP(list, list_info, parent_group);
   START_SUB_GROUP(list, list_info, "Monitor", group_info.name, subgroup_info, parent_group);

#if defined(GEKKO) || defined(__CELLOS_LV2__)
   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_SCREEN_RESOLUTION),
         menu_hash_to_str(MENU_LABEL_VALUE_SCREEN_RESOLUTION),
         group_info.name,
         subgroup_info.name,
         parent_group);
#endif

   CONFIG_UINT(
         settings->video.monitor_index,
         menu_hash_to_str(MENU_LABEL_VIDEO_MONITOR_INDEX),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_MONITOR_INDEX),
         monitor_index,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_REINIT);
   menu_settings_list_current_add_range(list, list_info, 0, 1, 1, true, false);
   (*list)[list_info->index - 1].get_string_representation = 
      &setting_get_string_representation_uint_video_monitor_index;
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

#if !defined(RARCH_CONSOLE) && !defined(RARCH_MOBILE)
   CONFIG_BOOL(
         settings->video.fullscreen,
         menu_hash_to_str(MENU_LABEL_VIDEO_FULLSCREEN),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FULLSCREEN),
         fullscreen,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_REINIT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO);
#endif
   CONFIG_BOOL(
         settings->video.windowed_fullscreen,
         menu_hash_to_str(MENU_LABEL_VIDEO_WINDOWED_FULLSCREEN),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_WINDOWED_FULLSCREEN),
         windowed_fullscreen,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_FLOAT(
         settings->video.refresh_rate,
         menu_hash_to_str(MENU_LABEL_VIDEO_REFRESH_RATE),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_REFRESH_RATE),
         refresh_rate,
         "%.3f Hz",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 0, 0.001, true, false);

   CONFIG_FLOAT(
         settings->video.refresh_rate,
         menu_hash_to_str(MENU_LABEL_VIDEO_REFRESH_RATE_AUTO),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_REFRESH_RATE_AUTO),
         refresh_rate,
         "%.3f Hz",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   (*list)[list_info->index - 1].action_start  = &setting_action_start_video_refresh_rate_auto;
   (*list)[list_info->index - 1].action_ok     = &setting_action_ok_video_refresh_rate_auto;
   (*list)[list_info->index - 1].action_select = &setting_action_ok_video_refresh_rate_auto;
   (*list)[list_info->index - 1].get_string_representation = 
      &setting_get_string_representation_st_float_video_refresh_rate_auto;

   CONFIG_BOOL(
         settings->video.force_srgb_disable,
         menu_hash_to_str(MENU_LABEL_VIDEO_FORCE_SRGB_DISABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FORCE_SRGB_DISABLE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_REINIT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO|SD_FLAG_ADVANCED);

   END_SUB_GROUP(list, list_info, parent_group);
   START_SUB_GROUP(list, list_info, "Aspect", group_info.name, subgroup_info, parent_group);
   CONFIG_BOOL(
         settings->video.force_aspect,
         menu_hash_to_str(MENU_LABEL_VIDEO_FORCE_ASPECT),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FORCE_ASPECT),
         force_aspect,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->video.aspect_ratio_auto,
         menu_hash_to_str(MENU_LABEL_VIDEO_ASPECT_RATIO_AUTO),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_ASPECT_RATIO_AUTO),
         aspect_ratio_auto,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_UINT(
         settings->video.aspect_ratio_idx,
         menu_hash_to_str(MENU_LABEL_VIDEO_ASPECT_RATIO_INDEX),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_ASPECT_RATIO_INDEX),
         aspect_ratio_idx,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(
         list,
         list_info,
         EVENT_CMD_VIDEO_SET_ASPECT_RATIO);
   menu_settings_list_current_add_range(
         list,
         list_info,
         0,
         LAST_ASPECT_RATIO,
         1,
         true,
         true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO);
   (*list)[list_info->index - 1].get_string_representation = 
      &setting_get_string_representation_uint_aspect_ratio_index;

   CONFIG_ACTION(
         menu_hash_to_str(MENU_LABEL_CUSTOM_RATIO),
         menu_hash_to_str(MENU_LABEL_VALUE_CUSTOM_RATIO),
         group_info.name,
         subgroup_info.name,
         parent_group);

   END_SUB_GROUP(list, list_info, parent_group);
   START_SUB_GROUP(list, list_info, "Scaling", group_info.name, subgroup_info, parent_group);

#if !defined(RARCH_CONSOLE) && !defined(RARCH_MOBILE)
   CONFIG_FLOAT(
         settings->video.scale,
         menu_hash_to_str(MENU_LABEL_VIDEO_SCALE),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_SCALE),
         scale,
         "%.1fx",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1.0, 10.0, 1.0, true, true);
#endif

   CONFIG_BOOL(
         settings->video.scale_integer,
         menu_hash_to_str(MENU_LABEL_VIDEO_SCALE_INTEGER),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_SCALE_INTEGER),
         scale_integer,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

#ifdef GEKKO
   CONFIG_UINT(
         settings->video.viwidth,
         menu_hash_to_str(MENU_LABEL_VIDEO_VI_WIDTH),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_VI_WIDTH),
         video_viwidth,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 640, 720, 2, true, true);

   CONFIG_BOOL(
         settings->video.vfilter,
         menu_hash_to_str(MENU_LABEL_VIDEO_VFILTER),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_VFILTER),
         video_vfilter,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
#endif

   CONFIG_BOOL(
         settings->video.smooth,
         menu_hash_to_str(MENU_LABEL_VIDEO_SMOOTH),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_SMOOTH),
         video_smooth,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

#if defined(__CELLOS_LV2__)
   CONFIG_BOOL(
         global->console.screen.pal60_enable,
         menu_hash_to_str(MENU_LABEL_PAL60_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_PAL60_ENABLE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
#endif

   CONFIG_UINT(
         settings->video.rotation,
         menu_hash_to_str(MENU_LABEL_VIDEO_ROTATION),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_ROTATION),
         0,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 3, 1, true, true);
   (*list)[list_info->index - 1].get_string_representation = 
      &setting_get_string_representation_uint_video_rotation;
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

#if defined(HW_RVL) || defined(_XBOX360)
   CONFIG_UINT(
         global->console.screen.gamma_correction,
         menu_hash_to_str(MENU_LABEL_VIDEO_GAMMA),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_GAMMA),
         0,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(
         list,
         list_info,
         EVENT_CMD_VIDEO_APPLY_STATE_CHANGES);
   menu_settings_list_current_add_range(
         list,
         list_info,
         0,
         MAX_GAMMA_SETTING,
         1,
         true,
         true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO|SD_FLAG_ADVANCED);

#endif
   END_SUB_GROUP(list, list_info, parent_group);
   START_SUB_GROUP(
         list,
         list_info,
         "Synchronization",
         group_info.name,
         subgroup_info,
         parent_group);

#if defined(HAVE_THREADS) && !defined(RARCH_CONSOLE)
   CONFIG_BOOL(
         settings->video.threaded,
         menu_hash_to_str(MENU_LABEL_VIDEO_THREADED),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_THREADED),
         video_threaded,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_REINIT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO|SD_FLAG_ADVANCED);
#endif

   CONFIG_BOOL(
         settings->video.vsync,
         menu_hash_to_str(MENU_LABEL_VIDEO_VSYNC),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_VSYNC),
         vsync,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_UINT(
         settings->video.swap_interval,
         menu_hash_to_str(MENU_LABEL_VIDEO_SWAP_INTERVAL),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_SWAP_INTERVAL),
         swap_interval,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_VIDEO_SET_BLOCKING_STATE);
   menu_settings_list_current_add_range(list, list_info, 1, 4, 1, true, true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO|SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->video.hard_sync,
         menu_hash_to_str(MENU_LABEL_VIDEO_HARD_SYNC),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_HARD_SYNC),
         hard_sync,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_UINT(
         settings->video.hard_sync_frames,
         menu_hash_to_str(MENU_LABEL_VIDEO_HARD_SYNC_FRAMES),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_HARD_SYNC_FRAMES),
         hard_sync_frames,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 3, 1, true, true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_UINT(
         settings->video.frame_delay,
         menu_hash_to_str(MENU_LABEL_VIDEO_FRAME_DELAY),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FRAME_DELAY),
         frame_delay,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 15, 1, true, true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

#if !defined(RARCH_MOBILE)
   CONFIG_BOOL(
         settings->video.black_frame_insertion,
         menu_hash_to_str(MENU_LABEL_VIDEO_BLACK_FRAME_INSERTION),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_BLACK_FRAME_INSERTION),
         black_frame_insertion,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
#endif
   END_SUB_GROUP(list, list_info, parent_group);
   START_SUB_GROUP(
         list,
         list_info,
         "Miscellaneous",
         group_info.name,
         subgroup_info,
         parent_group);

   CONFIG_BOOL(
         settings->video.gpu_screenshot,
         menu_hash_to_str(MENU_LABEL_VIDEO_GPU_SCREENSHOT),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_GPU_SCREENSHOT),
         gpu_screenshot,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->video.allow_rotate,
         menu_hash_to_str(MENU_LABEL_VIDEO_ALLOW_ROTATE),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_ALLOW_ROTATE),
         allow_rotate,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->video.crop_overscan,
         menu_hash_to_str(MENU_LABEL_VIDEO_CROP_OVERSCAN),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_CROP_OVERSCAN),
         crop_overscan,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

#if defined(_XBOX1) || defined(HW_RVL)
   CONFIG_BOOL(
         global->console.softfilter_enable,
         menu_hash_to_str(MENU_LABEL_VIDEO_SOFT_FILTER),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_SOFT_FILTER),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(
         list,
         list_info,
         EVENT_CMD_VIDEO_APPLY_STATE_CHANGES);
#endif

#ifndef HAVE_FILTERS_BUILTIN
   CONFIG_PATH(
         settings->video.softfilter_plugin,
         menu_hash_to_str(MENU_LABEL_VIDEO_FILTER),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FILTER),
         settings->video.filter_dir,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_values(list, list_info, "filt");
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_REINIT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_EMPTY);
#endif

#ifdef _XBOX1
   CONFIG_UINT(
         settings->video.swap_interval,
         menu_hash_to_str(MENU_LABEL_VIDEO_FILTER_FLICKER),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FILTER_FLICKER),
         0,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 5, 1, true, true);
#endif
   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_font_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_ONSCREEN_DISPLAY_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "Messages", group_info.name, subgroup_info, parent_group);

#ifndef RARCH_CONSOLE
   CONFIG_BOOL(
         settings->video.font_enable,
         menu_hash_to_str(MENU_LABEL_VIDEO_FONT_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FONT_ENABLE),
         font_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
#endif

   CONFIG_PATH(
         settings->video.font_path,
         menu_hash_to_str(MENU_LABEL_VIDEO_FONT_PATH),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FONT_PATH),
         "",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_EMPTY);

   CONFIG_FLOAT(
         settings->video.font_size,
         menu_hash_to_str(MENU_LABEL_VIDEO_FONT_SIZE),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FONT_SIZE),
         font_size,
         "%.1f",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1.00, 100.00, 1.0, true, true);

   CONFIG_FLOAT(
         settings->video.msg_pos_x,
         menu_hash_to_str(MENU_LABEL_VIDEO_MESSAGE_POS_X),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_MESSAGE_POS_X),
         message_pos_offset_x,
         "%.3f",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 1, 0.01, true, true);

   CONFIG_FLOAT(
         settings->video.msg_pos_y,
         menu_hash_to_str(MENU_LABEL_VIDEO_MESSAGE_POS_Y),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_MESSAGE_POS_Y),
         message_pos_offset_y,
         "%.3f",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 1, 0.01, true, true);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_audio_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
   global_t   *global   = global_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_SETTINGS), parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   (void)global;

   CONFIG_BOOL(
         settings->audio.enable,
         menu_hash_to_str(MENU_LABEL_AUDIO_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_ENABLE),
         audio_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->audio.mute_enable,
         menu_hash_to_str(MENU_LABEL_AUDIO_MUTE),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_MUTE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_FLOAT(
         settings->audio.volume,
         menu_hash_to_str(MENU_LABEL_AUDIO_VOLUME),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_VOLUME),
         audio_volume,
         "%.1f",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, -80, 12, 1.0, true, true);

#ifdef __CELLOS_LV2__
   CONFIG_BOOL(
         global->console.sound.system_bgm_enable,
         menu_hash_to_str(MENU_LABEL_SYSTEM_BGM_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_SYSTEM_BGM_ENABLE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
#endif

   END_SUB_GROUP(list, list_info, parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(
         list,
         list_info,
         "Synchronization",
         group_info.name,
         subgroup_info,
         parent_group);

   CONFIG_BOOL(
         settings->audio.sync,
         menu_hash_to_str(MENU_LABEL_AUDIO_SYNC),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_SYNC),
         audio_sync,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_UINT(
         settings->audio.latency,
         menu_hash_to_str(MENU_LABEL_AUDIO_LATENCY),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_LATENCY),
         g_defaults.settings.out_latency ? 
         g_defaults.settings.out_latency : out_latency,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1, 256, 1.0, true, true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_IS_DEFERRED);

   CONFIG_FLOAT(
         settings->audio.rate_control_delta,
         menu_hash_to_str(MENU_LABEL_AUDIO_RATE_CONTROL_DELTA),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_RATE_CONTROL_DELTA),
         rate_control_delta,
         "%.3f",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(
         list,
         list_info,
         0,
         0,
         0.001,
         true,
         false);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_FLOAT(
         settings->audio.max_timing_skew,
         menu_hash_to_str(MENU_LABEL_AUDIO_MAX_TIMING_SKEW),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_MAX_TIMING_SKEW),
         max_timing_skew,
         "%.2f",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(
         list,
         list_info,
         0.01,
         0.5,
         0.01,
         true,
         true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_UINT(
         settings->audio.block_frames,
         menu_hash_to_str(MENU_LABEL_AUDIO_BLOCK_FRAMES),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_BLOCK_FRAMES),
         0,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   END_SUB_GROUP(list, list_info, parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(
         list,
         list_info,
         "Miscellaneous",
         group_info.name,
         subgroup_info,
         parent_group);

   CONFIG_STRING(
         settings->audio.device,
         menu_hash_to_str(MENU_LABEL_AUDIO_DEVICE),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_DEVICE),
         "",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT | SD_FLAG_ADVANCED);

   CONFIG_UINT(
         settings->audio.out_rate,
         menu_hash_to_str(MENU_LABEL_AUDIO_OUTPUT_RATE),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_OUTPUT_RATE),
         out_rate,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_PATH(
         settings->audio.dsp_plugin,
         menu_hash_to_str(MENU_LABEL_AUDIO_DSP_PLUGIN),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_DSP_PLUGIN),
         settings->audio.filter_dir,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_values(list, list_info, "dsp");
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_DSP_FILTER_INIT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_EMPTY);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_input_hotkey_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   unsigned i;
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_HOTKEY_BINDS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info,
         parent_group);

   for (i = 0; i < RARCH_BIND_LIST_END; i ++)
   {
      const struct input_bind_map* keybind = (const struct input_bind_map*)
         &input_config_bind_map[i];

      if (!keybind || !keybind->meta)
         continue;

      CONFIG_BIND(settings->input.binds[0][i], 0, 0,
            strdup(keybind->base), strdup(keybind->desc), &retro_keybinds_1[i],
            group_info.name, subgroup_info.name, parent_group);
      menu_settings_list_current_add_bind_type(list, list_info, i + MENU_SETTINGS_BIND_BEGIN);
   }

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}


static bool setting_append_list_input_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   unsigned user;
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
   global_t   *global   = global_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_UINT(
         settings->input.max_users,
         menu_hash_to_str(MENU_LABEL_INPUT_MAX_USERS),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_MAX_USERS),
         input_max_users,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1, MAX_USERS, 1, true, true);

   CONFIG_BOOL(
         settings->input.remap_binds_enable,
         menu_hash_to_str(MENU_LABEL_INPUT_REMAP_BINDS_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_REMAP_BINDS_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->input.autodetect_enable,
         menu_hash_to_str(MENU_LABEL_INPUT_AUTODETECT_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_AUTODETECT_ENABLE),
         input_autodetect_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->input.autoconfig_descriptor_label_show,
         menu_hash_to_str(MENU_LABEL_AUTOCONFIG_DESCRIPTOR_LABEL_SHOW),
         menu_hash_to_str(MENU_LABEL_VALUE_AUTOCONFIG_DESCRIPTOR_LABEL_SHOW),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->input.input_descriptor_label_show,
         menu_hash_to_str(MENU_LABEL_INPUT_DESCRIPTOR_LABEL_SHOW),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_DESCRIPTOR_LABEL_SHOW),
         input_descriptor_label_show,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->input.input_descriptor_hide_unbound,
         menu_hash_to_str(MENU_LABEL_INPUT_DESCRIPTOR_HIDE_UNBOUND),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_DESCRIPTOR_HIDE_UNBOUND),
         input_descriptor_hide_unbound,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   END_SUB_GROUP(list, list_info, parent_group);

   START_SUB_GROUP(
         list,
         list_info,
         "Input Device Mapping",
         group_info.name,
         subgroup_info,
         parent_group);

   CONFIG_BOOL(
         global->menu.bind_mode_keyboard,
         menu_hash_to_str(MENU_LABEL_INPUT_BIND_MODE),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_BIND_MODE),
         false,
         menu_hash_to_str(MENU_VALUE_RETROPAD),
         menu_hash_to_str(MENU_VALUE_RETROKEYBOARD),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   for (user = 0; user < settings->input.max_users; user ++)
   {
      /* These constants match the string lengths.
       * Keep them up to date or you'll get some really obvious bugs.
       * 2 is the length of '99'; we don't need more users than that.
       */
      /* FIXME/TODO - really need to clean up this mess in some way. */
      static char key[MAX_USERS][64];
      static char key_type[MAX_USERS][64];
      static char key_analog[MAX_USERS][64];
      static char key_bind_all[MAX_USERS][64];
      static char key_bind_defaults[MAX_USERS][64];

      static char label[MAX_USERS][64];
      static char label_type[MAX_USERS][64];
      static char label_analog[MAX_USERS][64];
      static char label_bind_all[MAX_USERS][64];
      static char label_bind_defaults[MAX_USERS][64];

      snprintf(key[user], sizeof(key[user]),
               "input_player%u_joypad_index", user + 1);
      snprintf(key_type[user], sizeof(key_type[user]),
               "input_libretro_device_p%u", user + 1);
      snprintf(key_analog[user], sizeof(key_analog[user]),
               "input_player%u_analog_dpad_mode", user + 1);
      snprintf(key_bind_all[user], sizeof(key_bind_all[user]),
               "input_player%u_bind_all", user + 1);
      snprintf(key_bind_defaults[user], sizeof(key_bind_defaults[user]),
               "input_player%u_bind_defaults", user + 1);

      snprintf(label[user], sizeof(label[user]),
               "%s %u Device Index", menu_hash_to_str(MENU_VALUE_USER), user + 1);
      snprintf(label_type[user], sizeof(label_type[user]),
               "%s %u Device Type", menu_hash_to_str(MENU_VALUE_USER), user + 1);
      snprintf(label_analog[user], sizeof(label_analog[user]),
               "%s %u Analog To Digital Type", menu_hash_to_str(MENU_VALUE_USER), user + 1);
      snprintf(label_bind_all[user], sizeof(label_bind_all[user]),
               "%s %u Bind All", menu_hash_to_str(MENU_VALUE_USER), user + 1);
      snprintf(label_bind_defaults[user], sizeof(label_bind_defaults[user]),
               "%s %u Bind Default All", menu_hash_to_str(MENU_VALUE_USER), user + 1);

      CONFIG_UINT(
            settings->input.libretro_device[user],
            key_type[user],
            label_type[user],
            user,
            group_info.name,
            subgroup_info.name,
            parent_group,
            general_write_handler,
            general_read_handler);
      (*list)[list_info->index - 1].index = user + 1;
      (*list)[list_info->index - 1].index_offset = user;
      (*list)[list_info->index - 1].action_left   = &setting_action_left_libretro_device_type;
      (*list)[list_info->index - 1].action_right  = &setting_action_right_libretro_device_type;
      (*list)[list_info->index - 1].action_select = &setting_action_right_libretro_device_type;
      (*list)[list_info->index - 1].action_start  = &setting_action_start_libretro_device_type;
      (*list)[list_info->index - 1].get_string_representation = 
         &setting_get_string_representation_uint_libretro_device;

      CONFIG_UINT(
            settings->input.analog_dpad_mode[user],
            key_analog[user],
            label_analog[user],
            user,
            group_info.name,
            subgroup_info.name,
            parent_group,
            general_write_handler,
            general_read_handler);
      (*list)[list_info->index - 1].index = user + 1;
      (*list)[list_info->index - 1].index_offset = user;
      (*list)[list_info->index - 1].action_left   = &setting_action_left_analog_dpad_mode;
      (*list)[list_info->index - 1].action_right  = &setting_action_right_analog_dpad_mode;
      (*list)[list_info->index - 1].action_select = &setting_action_right_analog_dpad_mode;
      (*list)[list_info->index - 1].action_start = &setting_action_start_analog_dpad_mode;
      (*list)[list_info->index - 1].get_string_representation = 
         &setting_get_string_representation_uint_analog_dpad_mode;

      CONFIG_ACTION(
            key[user],
            label[user],
            group_info.name,
            subgroup_info.name,
            parent_group);
      (*list)[list_info->index - 1].index = user + 1;
      (*list)[list_info->index - 1].index_offset = user;
      (*list)[list_info->index - 1].action_start  = &setting_action_start_bind_device;
      (*list)[list_info->index - 1].action_left   = &setting_action_left_bind_device;
      (*list)[list_info->index - 1].action_right  = &setting_action_right_bind_device;
      (*list)[list_info->index - 1].action_select = &setting_action_right_bind_device;
      (*list)[list_info->index - 1].get_string_representation = &get_string_representation_bind_device;

      CONFIG_ACTION(
            key_bind_all[user],
            label_bind_all[user],
            group_info.name,
            subgroup_info.name,
            parent_group);
      (*list)[list_info->index - 1].index          = user + 1;
      (*list)[list_info->index - 1].index_offset   = user;
      (*list)[list_info->index - 1].action_ok      = &setting_action_ok_bind_all;
      (*list)[list_info->index - 1].action_cancel  = NULL;

      CONFIG_ACTION(
            key_bind_defaults[user],
            label_bind_defaults[user],
            group_info.name,
            subgroup_info.name,
            parent_group);
      (*list)[list_info->index - 1].index          = user + 1;
      (*list)[list_info->index - 1].index_offset   = user;
      (*list)[list_info->index - 1].action_ok      = &setting_action_ok_bind_defaults;
      (*list)[list_info->index - 1].action_cancel  = NULL;
   }

   START_SUB_GROUP(
         list,
         list_info,
         "Turbo/Deadzone",
         group_info.name,
         subgroup_info,
         parent_group);

   CONFIG_FLOAT(
         settings->input.axis_threshold,
         menu_hash_to_str(MENU_LABEL_INPUT_AXIS_THRESHOLD),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_AXIS_THRESHOLD),
         axis_threshold,
         "%.3f",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 1.00, 0.001, true, true);

   CONFIG_UINT(
         settings->input.turbo_period,
         menu_hash_to_str(MENU_LABEL_INPUT_TURBO_PERIOD),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_TURBO_PERIOD),
         turbo_period,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1, 0, 1, true, false);

   CONFIG_UINT(
         settings->input.turbo_duty_cycle,
         menu_hash_to_str(MENU_LABEL_INPUT_DUTY_CYCLE),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_DUTY_CYCLE),
         turbo_duty_cycle,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1, 0, 1, true, false);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_overlay_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
#ifdef HAVE_OVERLAY
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
   global_t   *global   = global_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_OVERLAY_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         settings->input.overlay_enable,
         menu_hash_to_str(MENU_LABEL_INPUT_OVERLAY_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_OVERLAY_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   (*list)[list_info->index - 1].change_handler = overlay_enable_toggle_change_handler;

   CONFIG_BOOL(
         settings->input.overlay_enable,
         menu_hash_to_str(MENU_LABEL_OVERLAY_AUTOLOAD_PREFERRED),
         menu_hash_to_str(MENU_LABEL_VALUE_OVERLAY_AUTOLOAD_PREFERRED),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   (*list)[list_info->index - 1].change_handler = overlay_enable_toggle_change_handler;

   CONFIG_BOOL(
         settings->osk.enable,
         menu_hash_to_str(MENU_LABEL_INPUT_OSK_OVERLAY_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_OSK_OVERLAY_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_PATH(
         settings->input.overlay,
         menu_hash_to_str(MENU_LABEL_OVERLAY_PRESET),
         menu_hash_to_str(MENU_LABEL_VALUE_OVERLAY_PRESET),
         global->overlay_dir,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_values(list, list_info, "cfg");
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_OVERLAY_INIT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_EMPTY);

   CONFIG_FLOAT(
         settings->input.overlay_opacity,
         menu_hash_to_str(MENU_LABEL_OVERLAY_OPACITY),
         menu_hash_to_str(MENU_LABEL_VALUE_OVERLAY_OPACITY),
         0.7f,
         "%.2f",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_OVERLAY_SET_ALPHA_MOD);
   menu_settings_list_current_add_range(list, list_info, 0, 1, 0.01, true, true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO);

   CONFIG_FLOAT(
         settings->input.overlay_scale,
         menu_hash_to_str(MENU_LABEL_OVERLAY_SCALE),
         menu_hash_to_str(MENU_LABEL_VALUE_OVERLAY_SCALE),
         1.0f,
         "%.2f",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_OVERLAY_SET_SCALE_FACTOR);
   menu_settings_list_current_add_range(list, list_info, 0, 2, 0.01, true, true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO);

   END_SUB_GROUP(list, list_info, parent_group);

   START_SUB_GROUP(list, list_info, "Onscreen Keyboard Overlay", group_info.name, subgroup_info, parent_group);

   CONFIG_PATH(
         settings->osk.overlay,
         menu_hash_to_str(MENU_LABEL_KEYBOARD_OVERLAY_PRESET),
         menu_hash_to_str(MENU_LABEL_VALUE_KEYBOARD_OVERLAY_PRESET),
         global->osk_overlay_dir,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_values(list, list_info, "cfg");
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_EMPTY);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);
#endif

   return true;
}

static bool setting_append_list_menu_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_MENU_SETTINGS),
         parent_group);
   
   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_PATH(
         settings->menu.wallpaper,
         menu_hash_to_str(MENU_LABEL_MENU_WALLPAPER),
         menu_hash_to_str(MENU_LABEL_VALUE_MENU_WALLPAPER),
         "",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_values(list, list_info, "png");
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_EMPTY);

   CONFIG_BOOL(
         settings->menu.dynamic_wallpaper_enable,
         menu_hash_to_str(MENU_LABEL_DYNAMIC_WALLPAPER),
         menu_hash_to_str(MENU_LABEL_VALUE_DYNAMIC_WALLPAPER),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);


   CONFIG_BOOL(
         settings->menu.pause_libretro,
         menu_hash_to_str(MENU_LABEL_PAUSE_LIBRETRO),
         menu_hash_to_str(MENU_LABEL_VALUE_PAUSE_LIBRETRO),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_MENU_PAUSE_LIBRETRO);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO);

   CONFIG_BOOL(
         settings->menu.mouse.enable,
         menu_hash_to_str(MENU_LABEL_MOUSE_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_MOUSE_ENABLE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->menu.pointer.enable,
         menu_hash_to_str(MENU_LABEL_POINTER_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_POINTER_ENABLE),
         pointer_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   END_SUB_GROUP(list, list_info, parent_group);

   START_SUB_GROUP(list, list_info, "Navigation", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         settings->menu.navigation.wraparound.horizontal_enable,
         menu_hash_to_str(MENU_LABEL_NAVIGATION_WRAPAROUND_HORIZONTAL),
         menu_hash_to_str(MENU_LABEL_VALUE_NAVIGATION_WRAPAROUND_HORIZONTAL),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->menu.navigation.wraparound.vertical_enable,
         menu_hash_to_str(MENU_LABEL_NAVIGATION_WRAPAROUND_VERTICAL),
         menu_hash_to_str(MENU_LABEL_VALUE_NAVIGATION_WRAPAROUND_VERTICAL),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   END_SUB_GROUP(list, list_info, parent_group);
   START_SUB_GROUP(list, list_info, "Settings View", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         settings->menu.show_advanced_settings,
         menu_hash_to_str(MENU_LABEL_SHOW_ADVANCED_SETTINGS),
         menu_hash_to_str(MENU_LABEL_VALUE_SHOW_ADVANCED_SETTINGS),
         show_advanced_settings,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

#ifdef HAVE_THREADS
   CONFIG_BOOL(
         settings->menu.threaded_data_runloop_enable,
         menu_hash_to_str(MENU_LABEL_THREADED_DATA_RUNLOOP_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_THREADED_DATA_RUNLOOP_ENABLE),
         threaded_data_runloop_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);
#endif

   /* These colors are hints. The menu driver is not required to use them. */
   CONFIG_HEX(
         settings->menu.entry_normal_color,
         menu_hash_to_str(MENU_LABEL_ENTRY_NORMAL_COLOR),
         menu_hash_to_str(MENU_LABEL_VALUE_ENTRY_NORMAL_COLOR),
         menu_entry_normal_color,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_HEX(
         settings->menu.entry_hover_color,
         menu_hash_to_str(MENU_LABEL_ENTRY_HOVER_COLOR),
         menu_hash_to_str(MENU_LABEL_VALUE_ENTRY_HOVER_COLOR),
         menu_entry_hover_color,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_HEX(
         settings->menu.title_color,
         menu_hash_to_str(MENU_LABEL_TITLE_COLOR),
         menu_hash_to_str(MENU_LABEL_VALUE_TITLE_COLOR),
         menu_title_color,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   END_SUB_GROUP(list, list_info, parent_group);


   START_SUB_GROUP(list, list_info, "Display", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         settings->menu.dpi.override_enable,
         menu_hash_to_str(MENU_LABEL_DPI_OVERRIDE_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_DPI_OVERRIDE_ENABLE),
         menu_dpi_override_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_UINT(
         settings->menu.dpi.override_value,
         menu_hash_to_str(MENU_LABEL_DPI_OVERRIDE_VALUE),
         menu_hash_to_str(MENU_LABEL_VALUE_DPI_OVERRIDE_VALUE),
         menu_dpi_override_value,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 72, 999, 1, true, true);

   CONFIG_BOOL(
         settings->menu_show_start_screen,
         menu_hash_to_str(MENU_LABEL_RGUI_SHOW_START_SCREEN),
         menu_hash_to_str(MENU_LABEL_VALUE_RGUI_SHOW_START_SCREEN),
         menu_show_start_screen,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->menu.boxart_enable,
         menu_hash_to_str(MENU_LABEL_BOXART),
         menu_hash_to_str(MENU_LABEL_VALUE_BOXART),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->menu.timedate_enable,
         menu_hash_to_str(MENU_LABEL_TIMEDATE_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_TIMEDATE_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->menu.core_enable,
         menu_hash_to_str(MENU_LABEL_CORE_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_CORE_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

#if defined(HAVE_IMAGEVIEWER) || defined(HAVE_FFMPEG)
static bool setting_append_list_multimedia_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
    
   (void)settings;

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_MULTIMEDIA_SETTINGS),
         parent_group);
   
   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   if (strcmp(settings->record.driver, "null") != 0)
      CONFIG_BOOL(
            settings->multimedia.builtin_mediaplayer_enable,
            menu_hash_to_str(MENU_LABEL_USE_BUILTIN_PLAYER),
            menu_hash_to_str(MENU_LABEL_VALUE_USE_BUILTIN_PLAYER),
            true,
            menu_hash_to_str(MENU_VALUE_OFF),
            menu_hash_to_str(MENU_VALUE_ON),
            group_info.name,
            subgroup_info.name,
            parent_group,
            general_write_handler,
            general_read_handler);


#ifdef HAVE_IMAGEVIEWER
   CONFIG_BOOL(
         settings->multimedia.builtin_imageviewer_enable,
         menu_hash_to_str(MENU_LABEL_USE_BUILTIN_IMAGE_VIEWER),
         menu_hash_to_str(MENU_LABEL_VALUE_USE_BUILTIN_IMAGE_VIEWER),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
#endif

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}
#endif

static bool setting_append_list_ui_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_UI_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         settings->video.disable_composition,
         menu_hash_to_str(MENU_LABEL_VIDEO_DISABLE_COMPOSITION),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_DISABLE_COMPOSITION),
         disable_composition,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_REINIT);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_CMD_APPLY_AUTO);

   CONFIG_BOOL(
         settings->pause_nonactive,
         menu_hash_to_str(MENU_LABEL_PAUSE_NONACTIVE),
         menu_hash_to_str(MENU_LABEL_VALUE_PAUSE_NONACTIVE),
         pause_nonactive,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   
   CONFIG_BOOL(
         settings->ui.companion_start_on_boot,
         menu_hash_to_str(MENU_LABEL_UI_COMPANION_START_ON_BOOT),
         menu_hash_to_str(MENU_LABEL_VALUE_UI_COMPANION_START_ON_BOOT),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_BOOL(
         settings->ui.menubar_enable,
         menu_hash_to_str(MENU_LABEL_UI_MENUBAR_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_UI_MENUBAR_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->ui.suspend_screensaver_enable,
         menu_hash_to_str(MENU_LABEL_SUSPEND_SCREENSAVER_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_SUSPEND_SCREENSAVER_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_menu_file_browser_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_MENU_FILE_BROWSER_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         settings->menu.navigation.browser.filter.supported_extensions_enable,
         menu_hash_to_str(MENU_LABEL_NAVIGATION_BROWSER_FILTER_SUPPORTED_EXTENSIONS_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_NAVIGATION_BROWSER_FILTER_SUPPORTED_EXTENSIONS_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_UINT(
         settings->archive.mode,
         menu_hash_to_str(MENU_LABEL_ARCHIVE_MODE),
         menu_hash_to_str(MENU_LABEL_VALUE_ARCHIVE_MODE),
         0,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 2, 1, true, true);
   (*list)[list_info->index - 1].get_string_representation = 
      &setting_get_string_representation_uint_archive_mode;

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_core_updater_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
#ifdef HAVE_NETWORKING
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_CORE_UPDATER_SETTINGS),
         parent_group);
   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_STRING(
         settings->network.buildbot_url,
         menu_hash_to_str(MENU_LABEL_CORE_UPDATER_BUILDBOT_URL),
         menu_hash_to_str(MENU_LABEL_VALUE_CORE_UPDATER_BUILDBOT_URL),
         buildbot_server_url, 
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);

   CONFIG_STRING(
         settings->network.buildbot_assets_url,
         menu_hash_to_str(MENU_LABEL_BUILDBOT_ASSETS_URL),
         menu_hash_to_str(MENU_LABEL_VALUE_BUILDBOT_ASSETS_URL),
         buildbot_assets_server_url, 
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);

   CONFIG_BOOL(
         settings->network.buildbot_auto_extract_archive,
         menu_hash_to_str(MENU_LABEL_CORE_UPDATER_AUTO_EXTRACT_ARCHIVE),
         menu_hash_to_str(MENU_LABEL_VALUE_CORE_UPDATER_AUTO_EXTRACT_ARCHIVE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);
#endif

   return true;
}

static bool setting_append_list_netplay_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
#ifdef HAVE_NETPLAY
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
   global_t   *global   = global_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_NETWORK_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "Netplay", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         global->netplay_enable,
         menu_hash_to_str(MENU_LABEL_NETPLAY_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_NETPLAY_ENABLE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         settings->input.netplay_client_swap_input,
         menu_hash_to_str(MENU_LABEL_NETPLAY_CLIENT_SWAP_INPUT),
         menu_hash_to_str(MENU_LABEL_VALUE_NETPLAY_CLIENT_SWAP_INPUT),
         netplay_client_swap_input,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_STRING(
         global->netplay_server,
         menu_hash_to_str(MENU_LABEL_NETPLAY_IP_ADDRESS),
         menu_hash_to_str(MENU_LABEL_VALUE_NETPLAY_IP_ADDRESS),
         "",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);

   CONFIG_BOOL(
         global->netplay_is_client,
         menu_hash_to_str(MENU_LABEL_NETPLAY_MODE),
         menu_hash_to_str(MENU_LABEL_VALUE_NETPLAY_MODE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         global->netplay_is_spectate,
         menu_hash_to_str(MENU_LABEL_NETPLAY_SPECTATOR_MODE_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_NETPLAY_SPECTATOR_MODE_ENABLE),
         false,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   
   CONFIG_UINT(
         global->netplay_sync_frames,
         menu_hash_to_str(MENU_LABEL_NETPLAY_DELAY_FRAMES),
         menu_hash_to_str(MENU_LABEL_VALUE_NETPLAY_DELAY_FRAMES),
         0,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 10, 1, true, false);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   CONFIG_UINT(
         global->netplay_port,
         menu_hash_to_str(MENU_LABEL_NETPLAY_TCP_UDP_PORT),
         menu_hash_to_str(MENU_LABEL_VALUE_NETPLAY_TCP_UDP_PORT),
         RARCH_DEFAULT_PORT,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 1, 99999, 1, true, true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);

   END_SUB_GROUP(list, list_info, parent_group);

   START_SUB_GROUP(
         list,
         list_info,
         "Miscellaneous",
         group_info.name,
         subgroup_info,
         parent_group);


#if defined(HAVE_NETWORK_CMD)
   CONFIG_BOOL(
         settings->network_cmd_enable,
         menu_hash_to_str(MENU_LABEL_NETWORK_CMD_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_NETWORK_CMD_ENABLE),
         network_cmd_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);
    
   CONFIG_UINT(
         settings->network_cmd_port,
         menu_hash_to_str(MENU_LABEL_NETWORK_CMD_PORT),
         menu_hash_to_str(MENU_LABEL_VALUE_NETWORK_CMD_PORT),
         network_cmd_port,
         group_info.name,
         subgroup_info.name,
         parent_group,
         NULL,
         NULL);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);
    
   CONFIG_BOOL(
         settings->stdin_cmd_enable,
         menu_hash_to_str(MENU_LABEL_STDIN_CMD_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_STDIN_CMD_ENABLE),
         stdin_cmd_enable,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);
#endif
   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);
#endif

   return true;
}

#if 0
static bool setting_append_list_patch_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};

   START_GROUP(group_info, "Patch Settings", parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info);

   CONFIG_BOOL(
         global->ups_pref,
         "ups_pref",
         "UPS Patching Enable",
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         global->bps_pref,
         "bps_pref",
         "BPS Patching Enable",
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         general_write_handler,
         general_read_handler);

   CONFIG_BOOL(
         global->ips_pref,
         "ips_pref",
         "IPS Patching Enable",
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         general_write_handler,
         general_read_handler);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}
#endif

static bool setting_append_list_playlist_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_PLAYLIST_SETTINGS),
         parent_group);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ADVANCED);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "History", group_info.name, subgroup_info, parent_group);

   CONFIG_BOOL(
         settings->history_list_enable,
         menu_hash_to_str(MENU_LABEL_HISTORY_LIST_ENABLE),
         menu_hash_to_str(MENU_LABEL_VALUE_HISTORY_LIST_ENABLE),
         true,
         menu_hash_to_str(MENU_VALUE_OFF),
         menu_hash_to_str(MENU_VALUE_ON),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);

   CONFIG_UINT(
         settings->content_history_size,
         menu_hash_to_str(MENU_LABEL_CONTENT_HISTORY_SIZE),
         menu_hash_to_str(MENU_LABEL_CONTENT_HISTORY_SIZE),
         default_content_history_size,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(list, list_info, 0, 0, 1.0, true, false);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_user_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_USER_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_STRING(
         settings->username,
         menu_hash_to_str(MENU_LABEL_NETPLAY_NICKNAME),
         menu_hash_to_str(MENU_LABEL_VALUE_NETPLAY_NICKNAME),
         "",
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);

   CONFIG_UINT(
         settings->user_language,
         menu_hash_to_str(MENU_LABEL_USER_LANGUAGE),
         menu_hash_to_str(MENU_LABEL_VALUE_USER_LANGUAGE),
         def_user_language,
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_range(
         list,
         list_info,
         0,
         RETRO_LANGUAGE_LAST-1,
         1,
         true,
         true);
   settings_data_list_current_add_flags(list, list_info, SD_FLAG_ALLOW_INPUT);
   (*list)[list_info->index - 1].get_string_representation = 
      &setting_get_string_representation_uint_user_language;

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_directory_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
   global_t *global     = global_get_ptr();

   START_GROUP(group_info,
        menu_hash_to_str(MENU_LABEL_VALUE_DIRECTORY_SETTINGS),
         parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State", group_info.name, subgroup_info, parent_group);

   CONFIG_DIR(
         settings->core_assets_directory,
         menu_hash_to_str(MENU_LABEL_CORE_ASSETS_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_CORE_ASSETS_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->assets_directory,
         menu_hash_to_str(MENU_LABEL_ASSETS_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_ASSETS_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->dynamic_wallpapers_directory,
         menu_hash_to_str(MENU_LABEL_DYNAMIC_WALLPAPERS_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_DYNAMIC_WALLPAPERS_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->boxarts_directory,
         menu_hash_to_str(MENU_LABEL_BOXARTS_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_BOXARTS_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->menu_content_directory,
         menu_hash_to_str(MENU_LABEL_RGUI_BROWSER_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_RGUI_BROWSER_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);


   CONFIG_DIR(
         settings->menu_config_directory,
         menu_hash_to_str(MENU_LABEL_RGUI_CONFIG_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_RGUI_CONFIG_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);


   CONFIG_DIR(
         settings->libretro_directory,
         menu_hash_to_str(MENU_LABEL_LIBRETRO_DIR_PATH),
         menu_hash_to_str(MENU_LABEL_VALUE_LIBRETRO_DIR_PATH),
         g_defaults.core_dir,
         menu_hash_to_str(MENU_VALUE_DIRECTORY_NONE),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_CORE_INFO_INIT);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->libretro_info_path,
         menu_hash_to_str(MENU_LABEL_LIBRETRO_INFO_PATH),
         menu_hash_to_str(MENU_LABEL_VALUE_LIBRETRO_INFO_PATH),
         g_defaults.core_info_dir,
         menu_hash_to_str(MENU_VALUE_DIRECTORY_NONE),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   menu_settings_list_current_add_cmd(list, list_info, EVENT_CMD_CORE_INFO_INIT);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

#ifdef HAVE_LIBRETRODB
   CONFIG_DIR(
         settings->content_database,
         menu_hash_to_str(MENU_LABEL_CONTENT_DATABASE_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_CONTENT_DATABASE_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_NONE),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->cursor_directory,
         menu_hash_to_str(MENU_LABEL_CURSOR_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_CURSOR_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_NONE),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);
#endif

   CONFIG_DIR(
         settings->cheat_database,
         menu_hash_to_str(MENU_LABEL_CHEAT_DATABASE_PATH),
         menu_hash_to_str(MENU_LABEL_VALUE_CHEAT_DATABASE_PATH),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_NONE),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->video.filter_dir,
         menu_hash_to_str(MENU_LABEL_VIDEO_FILTER_DIR),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_FILTER_DIR),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->audio.filter_dir,
         menu_hash_to_str(MENU_LABEL_AUDIO_FILTER_DIR),
         menu_hash_to_str(MENU_LABEL_VALUE_AUDIO_FILTER_DIR),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->video.shader_dir,
         menu_hash_to_str(MENU_LABEL_VIDEO_SHADER_DIR),
         menu_hash_to_str(MENU_LABEL_VALUE_VIDEO_SHADER_DIR),
         g_defaults.shader_dir,
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   if (strcmp(settings->record.driver, "null") != 0)
   {
      CONFIG_DIR(
            global->record.output_dir,
            menu_hash_to_str(MENU_LABEL_RECORDING_OUTPUT_DIRECTORY),
            menu_hash_to_str(MENU_LABEL_VALUE_RECORDING_OUTPUT_DIRECTORY),
            "",
            menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
            group_info.name,
            subgroup_info.name,
            parent_group,
            general_write_handler,
            general_read_handler);
         settings_data_list_current_add_flags(
            list,
            list_info,
            SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

      CONFIG_DIR(
            global->record.config_dir,
            menu_hash_to_str(MENU_LABEL_RECORDING_CONFIG_DIRECTORY),
            menu_hash_to_str(MENU_LABEL_VALUE_RECORDING_CONFIG_DIRECTORY),
            "",
            menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
            group_info.name,
            subgroup_info.name,
            parent_group,
            general_write_handler,
            general_read_handler);
      settings_data_list_current_add_flags(
            list,
            list_info,
            SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);
   }
#ifdef HAVE_OVERLAY
   CONFIG_DIR(
         global->overlay_dir,
         menu_hash_to_str(MENU_LABEL_OVERLAY_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_OVERLAY_DIRECTORY),
         g_defaults.overlay_dir,
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         global->osk_overlay_dir,
         menu_hash_to_str(MENU_LABEL_OSK_OVERLAY_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_OSK_OVERLAY_DIRECTORY),
         g_defaults.osk_overlay_dir,
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);
#endif

   CONFIG_DIR(
         settings->screenshot_directory,
         menu_hash_to_str(MENU_LABEL_SCREENSHOT_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_SCREENSHOT_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_CONTENT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->input.autoconfig_dir,
         menu_hash_to_str(MENU_LABEL_JOYPAD_AUTOCONFIG_DIR),
         menu_hash_to_str(MENU_LABEL_VALUE_JOYPAD_AUTOCONFIG_DIR),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->input_remapping_directory,
         menu_hash_to_str(MENU_LABEL_INPUT_REMAPPING_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_REMAPPING_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_NONE),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->playlist_directory,
         menu_hash_to_str(MENU_LABEL_PLAYLIST_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_PLAYLIST_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_DEFAULT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         global->savefile_dir,
         menu_hash_to_str(MENU_LABEL_SAVEFILE_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_SAVEFILE_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_CONTENT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         global->savestate_dir,
         menu_hash_to_str(MENU_LABEL_SAVESTATE_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_SAVESTATE_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_CONTENT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->system_directory,
         menu_hash_to_str(MENU_LABEL_SYSTEM_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_SYSTEM_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_CONTENT),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   CONFIG_DIR(
         settings->extraction_directory,
         menu_hash_to_str(MENU_LABEL_EXTRACTION_DIRECTORY),
         menu_hash_to_str(MENU_LABEL_VALUE_EXTRACTION_DIRECTORY),
         "",
         menu_hash_to_str(MENU_VALUE_DIRECTORY_NONE),
         group_info.name,
         subgroup_info.name,
         parent_group,
         general_write_handler,
         general_read_handler);
   settings_data_list_current_add_flags(
         list,
         list_info,
         SD_FLAG_ALLOW_EMPTY | SD_FLAG_PATH_DIR | SD_FLAG_BROWSER_ACTION);

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_privacy_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group)
{
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();

   START_GROUP(group_info,
         menu_hash_to_str(MENU_LABEL_VALUE_PRIVACY_SETTINGS), parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(list, list_info, "State",
         group_info.name, subgroup_info, parent_group);

   if (strcmp(settings->camera.driver, "null") != 0)
   {
      CONFIG_BOOL(
            settings->camera.allow,
            menu_hash_to_str(MENU_LABEL_CAMERA_ALLOW),
            menu_hash_to_str(MENU_LABEL_VALUE_CAMERA_ALLOW),
            false,
            menu_hash_to_str(MENU_VALUE_OFF),
            menu_hash_to_str(MENU_VALUE_ON),
            group_info.name,
            subgroup_info.name,
            parent_group,
            general_write_handler,
            general_read_handler);
   }

   if (strcmp(settings->location.driver, "null") != 0)
   {
      CONFIG_BOOL(
            settings->location.allow,
            menu_hash_to_str(MENU_LABEL_LOCATION_ALLOW),
            menu_hash_to_str(MENU_LABEL_VALUE_LOCATION_ALLOW),
            false,
            menu_hash_to_str(MENU_VALUE_OFF),
            menu_hash_to_str(MENU_VALUE_ON),
            group_info.name,
            subgroup_info.name,
            parent_group,
            general_write_handler,
            general_read_handler);
   }

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

static bool setting_append_list_input_player_options(
      rarch_setting_t **list,
      rarch_setting_info_t *list_info,
      const char *parent_group,
      unsigned user)
{
   /* This constants matches the string length.
    * Keep it up to date or you'll get some really obvious bugs.
    * 2 is the length of '99'; we don't need more users than that.
    */
   static char buffer[MAX_USERS][13+2+1];
   static char group_lbl[MAX_USERS][PATH_MAX_LENGTH];
   unsigned i;
   rarch_setting_group_info_t group_info    = {0};
   rarch_setting_group_info_t subgroup_info = {0};
   settings_t *settings = config_get_ptr();
   global_t   *global   = global_get_ptr();
   const struct retro_keybind* const defaults =
      (user == 0) ? retro_keybinds_1 : retro_keybinds_rest;
   rarch_system_info_t *system = rarch_system_info_get_ptr();

   snprintf(buffer[user],    sizeof(buffer[user]),
         "%s %u", menu_hash_to_str(MENU_VALUE_USER), user + 1);
   snprintf(group_lbl[user], sizeof(group_lbl[user]),
         menu_hash_to_str(MENU_LABEL_VALUE_INPUT_USER_BINDS), user + 1);

   START_GROUP(group_info, group_lbl[user], parent_group);

   parent_group = menu_hash_to_str(MENU_LABEL_VALUE_SETTINGS);

   START_SUB_GROUP(
         list,
         list_info,
         buffer[user],
         group_info.name,
         subgroup_info,
         parent_group);

   for (i = 0; i < RARCH_BIND_LIST_END; i ++)
   {
      char label[PATH_MAX_LENGTH];
      char name[PATH_MAX_LENGTH];
      bool do_add = true;
      const struct input_bind_map* keybind = 
         (const struct input_bind_map*)&input_config_bind_map[i];

      if (!keybind || keybind->meta)
         continue;

      strlcpy(label, buffer[user], sizeof(label));
      strlcat(label, " ", sizeof(label));
      if (
            settings->input.input_descriptor_label_show
            && (i < RARCH_FIRST_META_KEY)
            && (global->has_set_input_descriptors)
            && (i != RARCH_TURBO_ENABLE)
         )
      {
         if (system->input_desc_btn[user][i])
            strlcat(label, 
                  system->input_desc_btn[user][i],
                  sizeof(label));
         else
         {
            strlcat(label, menu_hash_to_str(MENU_VALUE_NOT_AVAILABLE),
                  sizeof(label));

            if (settings->input.input_descriptor_hide_unbound)
               do_add = false;
         }
      }
      else
         strlcat(label, keybind->desc, sizeof(label));

      snprintf(name, sizeof(name), "p%u_%s", user + 1, keybind->base);

      if (do_add)
      {
         CONFIG_BIND(
               settings->input.binds[user][i],
               user + 1,
               user,
               strdup(name), /* TODO: Find a way to fix these memleaks. */
               strdup(label),
               &defaults[i],
               group_info.name,
               subgroup_info.name,
               parent_group);
         menu_settings_list_current_add_bind_type(list, list_info, i + MENU_SETTINGS_BIND_BEGIN);
      }
   }

   END_SUB_GROUP(list, list_info, parent_group);
   END_GROUP(list, list_info, parent_group);

   return true;
}

void menu_setting_free(rarch_setting_t *list)
{
   rarch_setting_t *setting = list;

   if (!list)
      return;

   for (; setting->type != ST_NONE; setting++)
   {
      if (setting->flags & SD_FLAG_IS_DRIVER)
      {
         if (setting->values)
            free((void*)setting->values);
      }

      if (setting->type == ST_BIND)
      {
         free((void*)setting->name);
         free((void*)setting->short_description);
      }
   }

   free(list);
}

/**
 * menu_setting_new:
 * @mask               : Bitmask of settings to include.
 *
 * Request a list of settings based on @mask.
 *
 * Returns: settings list composed of all requested
 * settings on success, otherwise NULL.
 **/
rarch_setting_t *menu_setting_new(unsigned mask)
{
   rarch_setting_t terminator      = { ST_NONE };
   rarch_setting_t* list           = NULL;
   rarch_setting_t* resized_list   = NULL;
   rarch_setting_info_t *list_info = (rarch_setting_info_t*)
      calloc(1, sizeof(*list_info));
   const char *root                = menu_hash_to_str(MENU_VALUE_MAIN_MENU);

   if (!list_info)
      return NULL;

   list_info->size  = 32;
   list = menu_setting_list_new(list_info->size);
   if (!list)
      goto error;

   if (mask & SL_FLAG_MAIN_MENU)
   {
      if (!setting_append_list_main_menu_options(&list, list_info, root, mask))
         goto error;
   }

   if (mask & SL_FLAG_DRIVER_OPTIONS)
   {
      if (!setting_append_list_driver_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_CORE_OPTIONS)
   {
      if (!setting_append_list_core_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_CONFIGURATION_OPTIONS)
   {
      if (!setting_append_list_configuration_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_LOGGING_OPTIONS)
   {
      if (!setting_append_list_logging_options(&list, list_info, root))
         goto error;
   }
   
   if (mask & SL_FLAG_SAVING_OPTIONS)
   {
      if (!setting_append_list_saving_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_REWIND_OPTIONS)
   {
      if (!setting_append_list_rewind_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_VIDEO_OPTIONS)
   {
      if (!setting_append_list_video_options(&list, list_info, root))
         goto error;

#if 0
      if (!setting_append_list_video_shader_options(&list, list_info, root))
         goto error;
#endif
   }

   if (mask & SL_FLAG_AUDIO_OPTIONS)
   {
      if (!setting_append_list_audio_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_INPUT_OPTIONS)
   {
      unsigned user;
      settings_t      *settings = config_get_ptr();

      if (!setting_append_list_input_options(&list, list_info, root))
         goto error;

      for (user = 0; user < settings->input.max_users; user++)
         setting_append_list_input_player_options(&list, list_info, root, user);
   }


    
   if (mask & SL_FLAG_INPUT_HOTKEY_OPTIONS)
   {
      if (!setting_append_list_input_hotkey_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_RECORDING_OPTIONS)
   {
      settings_t      *settings = config_get_ptr();

      if (strcmp(settings->record.driver, "null") != 0)
      {
         if (!setting_append_list_recording_options(&list, list_info, root))
            goto error;
      }
   }

   if (mask & SL_FLAG_FRAME_THROTTLE_OPTIONS)
   {
      if (!setting_append_list_frame_throttling_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_FONT_OPTIONS)
   {
      if (!setting_append_list_font_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_OVERLAY_OPTIONS)
   {
      if (!setting_append_list_overlay_options(&list, list_info, root))
         goto error;
   }
   
   if (mask & SL_FLAG_MENU_OPTIONS)
   {
      if (!setting_append_list_menu_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_MENU_BROWSER_OPTIONS)
   {
      if (!setting_append_list_menu_file_browser_options(&list, list_info, root))
         goto error;
   }

#if defined(HAVE_IMAGEVIEWER) || defined(HAVE_FFMPEG)
   if (mask & SL_FLAG_MULTIMEDIA_OPTIONS)
   {
      if (!setting_append_list_multimedia_options(&list, list_info, root))
         goto error;
   }
#endif

   if (mask & SL_FLAG_UI_OPTIONS)
   {
      if (!setting_append_list_ui_options(&list, list_info, root))
         goto error;
   }

#if 0
   if (mask & SL_FLAG_PATCH_OPTIONS)
   {
      if (!setting_append_list_patch_options(&list, list_info, root))
         goto error;
   }
#endif

   if (mask & SL_FLAG_PLAYLIST_OPTIONS)
   {
      if (!setting_append_list_playlist_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_CORE_UPDATER_OPTIONS)
   {
      if (!setting_append_list_core_updater_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_NETPLAY_OPTIONS)
   {
      if (!setting_append_list_netplay_options(&list, list_info, root))
         goto error;
   }


   if (mask & SL_FLAG_USER_OPTIONS)
   {
      if (!setting_append_list_user_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_DIRECTORY_OPTIONS)
   {
      if (!setting_append_list_directory_options(&list, list_info, root))
         goto error;
   }

   if (mask & SL_FLAG_PRIVACY_OPTIONS)
   {
      if (!setting_append_list_privacy_options(&list, list_info, root))
         goto error;
   }

   if (!(menu_settings_list_append(&list, list_info, terminator)))
      goto error;

   /* flatten this array to save ourselves some kilobytes. */
   resized_list = (rarch_setting_t*) realloc(list, list_info->index * sizeof(rarch_setting_t));
   if (resized_list)
      list = resized_list;
   else
      goto error;

   menu_settings_info_list_free(list_info);

   return list;

error:
   RARCH_ERR("Allocation failed.\n");
   menu_settings_info_list_free(list_info);
   menu_setting_free(list);

   return NULL;
}

bool menu_setting_is_of_path_type(rarch_setting_t *setting)
{
   if    (
         setting &&
         setting->type == ST_ACTION &&
         (setting->flags & SD_FLAG_BROWSER_ACTION) &&
         (setting->action_right || setting->action_left || setting->action_select) &&
         setting->change_handler)
      return true;
   return false;
}

bool menu_setting_is_of_general_type(rarch_setting_t *setting)
{
   if    (
         setting &&
         (setting->type > ST_ACTION) &&
         (setting->type < ST_GROUP)
         )
      return true;
   return false;
}

bool menu_setting_is_of_numeric_type(rarch_setting_t *setting)
{
   if    (
         setting &&
         ((setting->type == ST_INT)  ||
          (setting->type == ST_UINT) ||
          (setting->type == ST_FLOAT))
         )
      return true;
   return false;
}

bool menu_setting_is_of_enum_type(rarch_setting_t *setting)
{
   if    (
         setting &&
         (setting->type == ST_STRING)  &&
         setting->values
         )
      return true;
   return false;
}
