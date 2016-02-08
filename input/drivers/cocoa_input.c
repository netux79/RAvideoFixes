/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 *  Copyright (C) 2013-2014 - Jason Fetters
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

#include <stdint.h>
#include <unistd.h>

#include "../input_common.h"
#include "../input_joypad.h"
#include "../input_keymaps.h"
#include "cocoa_input.h"
#include "../../general.h"
#include "../../driver.h"

#include "apple_keycode.h"

const struct apple_key_name_map_entry apple_key_name_map[] =
{
   { "left", KEY_Left },
   { "right", KEY_Right },
   { "up", KEY_Up },
   { "down", KEY_Down },
   { "enter", KEY_Enter },
   { "kp_enter", KP_Enter },
   { "space", KEY_Space },
   { "tab", KEY_Tab },
   { "shift", KEY_LeftShift },
   { "rshift", KEY_RightShift },
   { "ctrl", KEY_LeftControl },
   { "alt", KEY_LeftAlt },
   { "escape", KEY_Escape },
   { "backspace", KEY_DeleteForward },
   { "backquote", KEY_Grave },
   { "pause", KEY_Pause },
   { "f1", KEY_F1 },
   { "f2", KEY_F2 },
   { "f3", KEY_F3 },
   { "f4", KEY_F4 },
   { "f5", KEY_F5 },
   { "f6", KEY_F6 },
   { "f7", KEY_F7 },
   { "f8", KEY_F8 },
   { "f9", KEY_F9 },
   { "f10", KEY_F10 },
   { "f11", KEY_F11 },
   { "f12", KEY_F12 },
   { "num0", KEY_0 },
   { "num1", KEY_1 },
   { "num2", KEY_2 },
   { "num3", KEY_3 },
   { "num4", KEY_4 },
   { "num5", KEY_5 },
   { "num6", KEY_6 },
   { "num7", KEY_7 },
   { "num8", KEY_8 },
   { "num9", KEY_9 },

   { "insert", KEY_Insert },
   { "del", KEY_DeleteForward },
   { "home", KEY_Home },
   { "end", KEY_End },
   { "pageup", KEY_PageUp },
   { "pagedown", KEY_PageDown },

   { "add", KP_Add },
   { "subtract", KP_Subtract },
   { "multiply", KP_Multiply },
   { "divide", KP_Divide },
   { "keypad0", KP_0 },
   { "keypad1", KP_1 },
   { "keypad2", KP_2 },
   { "keypad3", KP_3 },
   { "keypad4", KP_4 },
   { "keypad5", KP_5 },
   { "keypad6", KP_6 },
   { "keypad7", KP_7 },
   { "keypad8", KP_8 },
   { "keypad9", KP_9 },

   { "period", KEY_Period },
   { "capslock", KEY_CapsLock },
   { "numlock", KP_NumLock },
   { "print_screen", KEY_PrintScreen },
   { "scroll_lock", KEY_ScrollLock },

   { "a", KEY_A },
   { "b", KEY_B },
   { "c", KEY_C },
   { "d", KEY_D },
   { "e", KEY_E },
   { "f", KEY_F },
   { "g", KEY_G },
   { "h", KEY_H },
   { "i", KEY_I },
   { "j", KEY_J },
   { "k", KEY_K },
   { "l", KEY_L },
   { "m", KEY_M },
   { "n", KEY_N },
   { "o", KEY_O },
   { "p", KEY_P },
   { "q", KEY_Q },
   { "r", KEY_R },
   { "s", KEY_S },
   { "t", KEY_T },
   { "u", KEY_U },
   { "v", KEY_V },
   { "w", KEY_W },
   { "x", KEY_X },
   { "y", KEY_Y },
   { "z", KEY_Z },

   { "nul", 0x00},
};

void cocoa_input_enable_small_keyboard(bool on)
{
   driver_t *driver = driver_get_ptr();
   cocoa_input_data_t *apple = (cocoa_input_data_t*)driver->input_data;
   if (apple)
      apple->small_keyboard_enabled = on;
}

void cocoa_input_enable_icade(bool on)
{
   driver_t *driver = driver_get_ptr();
   cocoa_input_data_t *apple = (cocoa_input_data_t*)driver->input_data;
    
   if (!apple)
      return;

   apple->icade_enabled = on;
   apple->icade_buttons = 0;
}

void cocoa_input_reset_icade_buttons(void)
{
   driver_t *driver = driver_get_ptr();
   cocoa_input_data_t *apple = (cocoa_input_data_t*)driver->input_data;
    
   if (apple)
      apple->icade_buttons = 0;
}

int32_t cocoa_input_find_any_key(void)
{
   unsigned i;
   driver_t *driver =driver_get_ptr();
   cocoa_input_data_t *apple = (cocoa_input_data_t*)driver->input_data;
    
   if (!apple)
      return 0;
    
   if (apple->joypad)
       apple->joypad->poll();

   for (i = 0; apple_key_name_map[i].hid_id; i++)
      if (apple->key_state[apple_key_name_map[i].hid_id])
         return apple_key_name_map[i].hid_id;

   return 0;
}

int32_t cocoa_input_find_any_button(uint32_t port)
{
   unsigned i, buttons = 0;
   driver_t *driver = driver_get_ptr();
   cocoa_input_data_t *apple = (cocoa_input_data_t*)driver->input_data;

   if (!apple)
      return -1;
    
   if (apple->joypad)
       apple->joypad->poll();

   buttons = apple->buttons[port];
   if (port == 0 && apple->icade_enabled)
      BIT32_SET(buttons, apple->icade_buttons);

   if (buttons)
      for (i = 0; i < 32; i++)
         if (buttons & (1 << i))
            return i;

   return -1;
}

int32_t cocoa_input_find_any_axis(uint32_t port)
{
   int i;
   driver_t *driver = driver_get_ptr();
   cocoa_input_data_t *apple = (cocoa_input_data_t*)driver->input_data;
    
   if (apple && apple->joypad)
       apple->joypad->poll();

   for (i = 0; i < 4; i++)
   {
      int16_t value = apple->axes[port][i];
      
      if (abs(value) > 0x4000)
         return (value < 0) ? -(i + 1) : i + 1;
   }

   return 0;
}

static int16_t cocoa_input_is_pressed(cocoa_input_data_t *apple, unsigned port_num,
   const struct retro_keybind *binds, unsigned id)
{
   if (id < RARCH_BIND_LIST_END)
   {
      const struct retro_keybind *bind = &binds[id];
      unsigned bit = input_keymaps_translate_rk_to_keysym(bind->key);
      return bind->valid && apple->key_state[bit];
   }
   return 0;
}

static void *cocoa_input_init(void)
{
   settings_t *settings = config_get_ptr();
   cocoa_input_data_t *apple = (cocoa_input_data_t*)calloc(1, sizeof(*apple));
   if (!apple)
      return NULL;
    
   input_keymaps_init_keyboard_lut(rarch_key_map_apple_hid);

   apple->joypad = input_joypad_init_driver(settings->input.joypad_driver, apple);
    
   return apple;
}

static void cocoa_input_poll(void *data)
{
   uint32_t i;
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;
    
   if (!apple)
       return;

   for (i = 0; i < apple->touch_count; i++)
      input_translate_coord_viewport(
            apple->touches[i].screen_x,
            apple->touches[i].screen_y,
            &apple->touches[i].fixed_x,
            &apple->touches[i].fixed_y,
            &apple->touches[i].full_x,
            &apple->touches[i].full_y);

   if (apple->joypad)
      apple->joypad->poll();

   if (apple->icade_enabled)
      BIT32_SET(apple->buttons[0], apple->icade_buttons);
}

static int16_t cocoa_mouse_state(cocoa_input_data_t *apple,
      unsigned id)
{
   switch (id)
   {
      case RETRO_DEVICE_ID_MOUSE_X:
         return apple->mouse_x;
      case RETRO_DEVICE_ID_MOUSE_Y:
         return apple->mouse_y;
      case RETRO_DEVICE_ID_MOUSE_LEFT:
         return apple->mouse_buttons & 1;
      case RETRO_DEVICE_ID_MOUSE_RIGHT:
         return apple->mouse_buttons & 2;
       case RETRO_DEVICE_ID_MOUSE_WHEELUP:
           return apple->mouse_wu;
       case RETRO_DEVICE_ID_MOUSE_WHEELDOWN:
           return apple->mouse_wd;
   }

   return 0;
}

static int16_t cocoa_pointer_state(cocoa_input_data_t *apple,
      unsigned device, unsigned idx, unsigned id)
{
   const bool want_full = (device == RARCH_DEVICE_POINTER_SCREEN);

   if (idx < apple->touch_count && (idx < MAX_TOUCHES))
   {
      int16_t x, y;
      const cocoa_touch_data_t *touch = (const cocoa_touch_data_t *)
         &apple->touches[idx];
       
       if (!touch)
           return 0;
       
      x = touch->fixed_x;
      y = touch->fixed_y;
      
      if (want_full)
      {
         x = touch->full_x;
         y = touch->full_y;
      }

      switch (id)
      {
         case RETRO_DEVICE_ID_POINTER_PRESSED:
            return (x != -0x8000) && (y != -0x8000);
         case RETRO_DEVICE_ID_POINTER_X:
            return x;
         case RETRO_DEVICE_ID_POINTER_Y:
            return y;
      }
   }

   return 0;
}

static int16_t cocoa_keyboard_state(cocoa_input_data_t *apple, unsigned id)
{
   unsigned bit = input_keymaps_translate_rk_to_keysym((enum retro_key)id);
   return (id < RETROK_LAST) && apple->key_state[bit];
}

static int16_t cocoa_input_state(void *data,
      const struct retro_keybind **binds, unsigned port,
      unsigned device, unsigned idx, unsigned id)
{
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;

   if (!apple || !apple->joypad)
      return 0;

   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         return cocoa_input_is_pressed(apple, port, binds[port], id) ||
            input_joypad_pressed(apple->joypad, port, binds[port], id);
      case RETRO_DEVICE_ANALOG:
         return input_joypad_analog(apple->joypad, port,
               idx, id, binds[port]);
      case RETRO_DEVICE_KEYBOARD:
         return cocoa_keyboard_state(apple, id);
      case RETRO_DEVICE_MOUSE:
         return cocoa_mouse_state(apple, id);
      case RETRO_DEVICE_POINTER:
      case RARCH_DEVICE_POINTER_SCREEN:
         return cocoa_pointer_state(apple, device, idx, id);
   }

   return 0;
}

static bool cocoa_input_bind_button_pressed(void *data, int key)
{
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;
   settings_t *settings = config_get_ptr();
   if (apple && apple->joypad)
      return cocoa_input_is_pressed(apple, 0, settings->input.binds[0], key) ||
         input_joypad_pressed(apple->joypad, 0, settings->input.binds[0], key);
   return false;
}

static void cocoa_input_free(void *data)
{
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;
    
   if (!apple || !data)
      return;
    
   if (apple->joypad)
      apple->joypad->destroy();
    
   free(apple);
}

static bool cocoa_input_set_rumble(void *data,
   unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;
    
   if (apple && apple->joypad)
      return input_joypad_set_rumble(apple->joypad,
            port, effect, strength);
   return false;
}

static uint64_t cocoa_input_get_capabilities(void *data)
{
   (void)data;

   return 
      (1 << RETRO_DEVICE_JOYPAD)   |
      (1 << RETRO_DEVICE_MOUSE)    |
      (1 << RETRO_DEVICE_KEYBOARD) |
      (1 << RETRO_DEVICE_POINTER)  |
      (1 << RETRO_DEVICE_ANALOG);
}

static void cocoa_input_grab_mouse(void *data, bool state)
{
   /* Dummy for now. Might be useful in the future. */
   (void)data;
   (void)state;
}

static const input_device_driver_t *cocoa_input_get_joypad_driver(void *data)
{
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;
    
   if (apple && apple->joypad)
      return apple->joypad;
   return NULL;
}

static bool cocoa_input_keyboard_mapping_is_blocked(void *data)
{
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;
   if (!apple)
      return false;
   return apple->blocked;
}

static void cocoa_input_keyboard_mapping_set_block(void *data, bool value)
{
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;
   if (!apple)
      return;
   apple->blocked = value;
}

input_driver_t input_cocoa = {
   cocoa_input_init,
   cocoa_input_poll,
   cocoa_input_state,
   cocoa_input_bind_button_pressed,
   cocoa_input_free,
   NULL,
   NULL,
   cocoa_input_get_capabilities,
   "cocoa",
   cocoa_input_grab_mouse,
   NULL,
   cocoa_input_set_rumble,
   cocoa_input_get_joypad_driver,
   cocoa_input_keyboard_mapping_is_blocked,
   cocoa_input_keyboard_mapping_set_block,
};
