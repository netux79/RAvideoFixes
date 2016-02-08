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
#include "input_driver.h"
#include "../driver.h"
#include "../general.h"
#include "../libretro.h"


static const input_driver_t *input_drivers[] = {
#ifdef __CELLOS_LV2__
   &input_ps3,
#endif
#if defined(SN_TARGET_PSP2) || defined(PSP)
   &input_psp,
#endif
#if defined(_3DS)
   &input_ctr,
#endif
#if defined(HAVE_SDL) || defined(HAVE_SDL2)
   &input_sdl,
#endif
#ifdef HAVE_DINPUT
   &input_dinput,
#endif
#ifdef HAVE_X11
   &input_x,
#endif
#ifdef XENON
   &input_xenon360,
#endif
#if defined(HAVE_XINPUT2) || defined(HAVE_XINPUT_XBOX1)
   &input_xinput,
#endif
#ifdef GEKKO
   &input_gx,
#endif
#ifdef ANDROID
   &input_android,
#endif
#ifdef HAVE_UDEV
   &input_udev,
#endif
#if defined(__linux__) && !defined(ANDROID)
   &input_linuxraw,
#endif
#if defined(HAVE_COCOA) || defined(HAVE_COCOATOUCH)
   &input_cocoa,
#endif
#ifdef __QNX__
   &input_qnx,
#endif
#ifdef EMSCRIPTEN
   &input_rwebinput,
#endif
   &input_null,
   NULL,
};

/**
 * input_driver_find_handle:
 * @idx                : index of driver to get handle to.
 *
 * Returns: handle to input driver at index. Can be NULL
 * if nothing found.
 **/
const void *input_driver_find_handle(int idx)
{
   const void *drv = input_drivers[idx];
   if (!drv)
      return NULL;
   return drv;
}

/**
 * input_driver_find_ident:
 * @idx                : index of driver to get handle to.
 *
 * Returns: Human-readable identifier of input driver at index. Can be NULL
 * if nothing found.
 **/
const char *input_driver_find_ident(int idx)
{
   const input_driver_t *drv = input_drivers[idx];
   if (!drv)
      return NULL;
   return drv->ident;
}

/**
 * config_get_input_driver_options:
 *
 * Get an enumerated list of all input driver names, separated by '|'.
 *
 * Returns: string listing of all input driver names, separated by '|'.
 **/
const char* config_get_input_driver_options(void)
{
   union string_list_elem_attr attr;
   unsigned i;
   char *options = NULL;
   int options_len = 0;
   struct string_list *options_l = string_list_new();

   attr.i = 0;

   if (!options_l)
      return NULL;

   for (i = 0; input_driver_find_handle(i); i++)
   {
      const char *opt = input_driver_find_ident(i);
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

void find_input_driver(void)
{
   driver_t *driver = driver_get_ptr();
   settings_t *settings = config_get_ptr();
   int i = find_driver_index("input_driver", settings->input.driver);

   if (i >= 0)
      driver->input = (const input_driver_t*)input_driver_find_handle(i);
   else
   {
      unsigned d;
      RARCH_ERR("Couldn't find any input driver named \"%s\"\n",
            settings->input.driver);
      RARCH_LOG_OUTPUT("Available input drivers are:\n");
      for (d = 0; input_driver_find_handle(d); d++)
         RARCH_LOG_OUTPUT("\t%s\n", input_driver_find_ident(d));
      RARCH_WARN("Going to default to first input driver...\n");

      driver->input = (const input_driver_t*)input_driver_find_handle(0);

      if (!driver->input)
         rarch_fail(1, "find_input_driver()");
   }
}

static const input_driver_t *input_get_ptr(driver_t *driver)
{
   if (!driver)
      return NULL;
   return driver->input;
}

/**
 * input_driver_set_rumble_state:
 * @port               : User number.
 * @effect             : Rumble effect.
 * @strength           : Strength of rumble effect.
 *
 * Sets the rumble state.
 * Used by RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE.
 **/
bool input_driver_set_rumble_state(unsigned port,
      enum retro_rumble_effect effect, uint16_t strength)
{
   driver_t            *driver = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   if (input->set_rumble)
      return input->set_rumble(driver->input_data,
            port, effect, strength);
   return false;
}


bool input_driver_key_pressed(int key)
{
   driver_t            *driver = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   if (!driver || !input)
      return false;
   return input->key_pressed(driver->input_data, key);
}

retro_input_t input_driver_keys_pressed(void)
{
   int key;
   retro_input_t           ret = 0;
   driver_t            *driver = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   for (key = 0; key < RARCH_BIND_LIST_END; key++)
   {
      bool state = false;
      if ((!driver->block_libretro_input && (key < RARCH_FIRST_META_KEY)) ||
            !driver->block_hotkey)
         state = input->key_pressed(driver->input_data, key);

#ifdef HAVE_OVERLAY
      state = state || (driver->overlay_state.buttons & (1ULL << key));
#endif

#ifdef HAVE_COMMAND
      if (driver->command)
         state = state || rarch_cmd_get(driver->command, key);
#endif

      if (state)
         ret |= (1ULL << key);
   }
   return ret;
}

int16_t input_driver_state(const struct retro_keybind **retro_keybinds,
      unsigned port, unsigned device, unsigned index, unsigned id)
{
   driver_t            *driver = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   return input->input_state(driver->input_data, retro_keybinds,
         port, device, index, id);
}

void input_driver_poll(void)
{
   driver_t            *driver = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   input->poll(driver->input_data);
}


const input_device_driver_t *input_driver_get_joypad_driver(void)
{
   driver_t            *driver = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   if (input->get_joypad_driver)
      return input->get_joypad_driver(driver->input_data);
   return NULL;
}

uint64_t input_driver_get_capabilities(void)
{
   driver_t            *driver = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   if (input->get_capabilities)
      return input->get_capabilities(driver->input_data);
   return 0; 
}

bool input_driver_grab_mouse(bool state)
{
   driver_t            *driver = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   if (input->grab_mouse)
   {
      input->grab_mouse(driver->input_data, state);
      return true;
   }
   return false;
}

bool input_driver_grab_stdin(void)
{
   driver_t            *driver = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   if (input->grab_stdin)
      return input->grab_stdin(driver->input_data);
   return false;
}

void *input_driver_init(void)
{
   driver_t *driver               = driver_get_ptr();

   if (driver && driver->input)
      return driver->input->init();
   return NULL;
}

void input_driver_free(void)
{
   driver_t *driver               = driver_get_ptr();

   if (driver && driver->input)
      driver->input->free(driver->input_data);
}

bool input_driver_keyboard_mapping_is_blocked(void)
{
   driver_t *driver               = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   if (input->keyboard_mapping_is_blocked)
      return driver->input->keyboard_mapping_is_blocked(
            driver->input_data);
   return false;
}

void input_driver_keyboard_mapping_set_block(bool value)
{
   driver_t *driver               = driver_get_ptr();
   const input_driver_t *input = input_get_ptr(driver);

   if (input->keyboard_mapping_set_block)
      driver->input->keyboard_mapping_set_block(driver->input_data, value);
}
