/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2012-2015 - Michael Lelli
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

#include <unistd.h>

#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <termios.h>
#include <signal.h>

#include <boolean.h>

#include "../../general.h"

#include "../input_keymaps.h"
#include "../input_common.h"
#include "../input_joypad.h"

static long oldKbmd                = 0xffff;
static bool linuxraw_stdin_claimed = false;
static struct termios oldTerm, newTerm;

typedef struct linuxraw_input
{
   bool blocked;
   const input_device_driver_t *joypad;
   bool state[0x80];
} linuxraw_input_t;


static void linuxraw_reset_kbmd(void)
{
   if (oldKbmd != 0xffff)
   {
      ioctl(0, KDSKBMODE, oldKbmd);
      tcsetattr(0, TCSAFLUSH, &oldTerm);
      oldKbmd = 0xffff;
   }

   linuxraw_stdin_claimed = false;
}

static void linuxraw_exit_gracefully(int sig)
{
   linuxraw_reset_kbmd();
   kill(getpid(), sig);
}

static void *linuxraw_input_init(void)
{
   struct sigaction sa         = {{0}};
   linuxraw_input_t *linuxraw  = NULL;
   driver_t *driver            = driver_get_ptr();
   settings_t *settings        = config_get_ptr();

   /* Only work on terminals. */
   if (!isatty(0))
      return NULL;

   if (linuxraw_stdin_claimed)
   {
      RARCH_WARN("stdin is already used for content loading. Cannot use stdin for input.\n");
      return NULL;
   }

   linuxraw = (linuxraw_input_t*)calloc(1, sizeof(*linuxraw));
   if (!linuxraw)
      return NULL;

   if (oldKbmd == 0xffff)
   {
      tcgetattr(0, &oldTerm);
      newTerm              = oldTerm;
      newTerm.c_lflag     &= ~(ECHO | ICANON | ISIG);
      newTerm.c_iflag     &= ~(ISTRIP | IGNCR | ICRNL | INLCR | IXOFF | IXON);
      newTerm.c_cc[VMIN]   = 0;
      newTerm.c_cc[VTIME]  = 0;

      if (ioctl(0, KDGKBMODE, &oldKbmd) != 0)
      {
         free(linuxraw);
         return NULL;
      }
   }

   tcsetattr(0, TCSAFLUSH, &newTerm);

   if (ioctl(0, KDSKBMODE, K_MEDIUMRAW) != 0)
   {
      linuxraw_reset_kbmd();
      free(linuxraw);
      return NULL;
   }

   sa.sa_handler = linuxraw_exit_gracefully;
   sa.sa_flags = SA_RESTART | SA_RESETHAND;
   sigemptyset(&sa.sa_mask);

   /* Trap some standard termination codes so we 
    * can restore the keyboard before we lose control. */
   sigaction(SIGABRT, &sa, NULL);
   sigaction(SIGBUS,  &sa, NULL);
   sigaction(SIGFPE,  &sa, NULL);
   sigaction(SIGILL,  &sa, NULL);
   sigaction(SIGQUIT, &sa, NULL);
   sigaction(SIGSEGV, &sa, NULL);

   atexit(linuxraw_reset_kbmd);

   linuxraw->joypad = input_joypad_init_driver(
         settings->input.joypad_driver, linuxraw);
   input_keymaps_init_keyboard_lut(rarch_key_map_linux);

   /* We need to disable use of stdin command interface if 
    * stdin is supposed to be used for input. */
   linuxraw_stdin_claimed = true; 

   return linuxraw;
}

static bool linuxraw_grab_stdin(void *data)
{
   return linuxraw_stdin_claimed;
}

static bool linuxraw_key_pressed(linuxraw_input_t *linuxraw, int key)
{
   unsigned sym = input_keymaps_translate_rk_to_keysym((enum retro_key)key);
   return linuxraw->state[sym];
}

static bool linuxraw_is_pressed(linuxraw_input_t *linuxraw,
      const struct retro_keybind *binds, unsigned id)
{
   const struct retro_keybind *bind = &binds[id];

   if (id >= RARCH_BIND_LIST_END)
      return false;
   if (!bind)
      return false;

   return bind->valid && linuxraw_key_pressed(linuxraw, binds[id].key);
}

static int16_t linuxraw_analog_pressed(linuxraw_input_t *linuxraw,
      const struct retro_keybind *binds, unsigned idx, unsigned id)
{
   int16_t pressed_minus = 0, pressed_plus = 0;
   unsigned id_minus = 0;
   unsigned id_plus  = 0;

   input_conv_analog_id_to_bind_id(idx, id, &id_minus, &id_plus);

   if (linuxraw_is_pressed(linuxraw, binds, id_minus))
      pressed_minus = -0x7fff;
   if (linuxraw_is_pressed(linuxraw, binds, id_plus))
      pressed_plus = 0x7fff;

   return pressed_plus + pressed_minus;
}

static bool linuxraw_bind_button_pressed(void *data, int key)
{
   linuxraw_input_t *linuxraw = (linuxraw_input_t*)data;
   settings_t *settings       = config_get_ptr();

   return linuxraw_is_pressed(linuxraw, settings->input.binds[0], key) ||
      input_joypad_pressed(linuxraw->joypad, 0, settings->input.binds[0], key);
}

static int16_t linuxraw_input_state(void *data,
      const struct retro_keybind **binds, unsigned port,
      unsigned device, unsigned idx, unsigned id)
{
   int16_t ret;
   linuxraw_input_t *linuxraw = (linuxraw_input_t*)data;

   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         return linuxraw_is_pressed(linuxraw, binds[port], id) ||
            input_joypad_pressed(linuxraw->joypad, port, binds[port], id);

      case RETRO_DEVICE_ANALOG:
         ret = linuxraw_analog_pressed(linuxraw, binds[port], idx, id);
         if (!ret)
            ret = input_joypad_analog(linuxraw->joypad, port, idx, id, binds[port]);
         return ret;
   }

   return 0;
}

static void linuxraw_input_free(void *data)
{
   linuxraw_input_t *linuxraw = (linuxraw_input_t*)data;

   if (!linuxraw)
      return;

   if (linuxraw->joypad)
      linuxraw->joypad->destroy();

   linuxraw_reset_kbmd();
   free(data);
}

static bool linuxraw_set_rumble(void *data, unsigned port,
      enum retro_rumble_effect effect, uint16_t strength)
{
   linuxraw_input_t *linuxraw = (linuxraw_input_t*)data;
   if (!linuxraw)
      return false;
   return input_joypad_set_rumble(linuxraw->joypad, port, effect, strength);
}

static const input_device_driver_t *linuxraw_get_joypad_driver(void *data)
{
   linuxraw_input_t *linuxraw = (linuxraw_input_t*)data;
   if (!linuxraw)
      return NULL;
   return linuxraw->joypad;
}

static void linuxraw_input_poll(void *data)
{
   uint8_t c;
   uint16_t t;
   linuxraw_input_t *linuxraw = (linuxraw_input_t*)data;

   while (read(STDIN_FILENO, &c, 1) > 0)
   {
      bool pressed;

      if (c == KEY_C && (linuxraw->state[KEY_LEFTCTRL] || linuxraw->state[KEY_RIGHTCTRL]))
         kill(getpid(), SIGINT);

      pressed = !(c & 0x80);
      c &= ~0x80;

      /* ignore extended scancodes */
      if (!c)
         read(STDIN_FILENO, &t, 2);
      else
         linuxraw->state[c] = pressed;
   }

   if (linuxraw->joypad)
      linuxraw->joypad->poll();
}

static uint64_t linuxraw_get_capabilities(void *data)
{
   uint64_t caps = 0;

   (void)data;

   caps |= (1 << RETRO_DEVICE_JOYPAD);
   caps |= (1 << RETRO_DEVICE_ANALOG);

   return caps;
}

static void linuxraw_grab_mouse(void *data, bool state)
{
   (void)data;
   (void)state;
}

static bool linuxraw_keyboard_mapping_is_blocked(void *data)
{
   linuxraw_input_t *linuxraw = (linuxraw_input_t*)data;
   if (!linuxraw)
      return false;
   return linuxraw->blocked;
}

static void linuxraw_keyboard_mapping_set_block(void *data, bool value)
{
   linuxraw_input_t *linuxraw = (linuxraw_input_t*)data;
   if (!linuxraw)
      return;
   linuxraw->blocked = value;
}

input_driver_t input_linuxraw = {
   linuxraw_input_init,
   linuxraw_input_poll,
   linuxraw_input_state,
   linuxraw_bind_button_pressed,
   linuxraw_input_free,
   NULL,
   NULL,
   linuxraw_get_capabilities,
   "linuxraw",
   linuxraw_grab_mouse,
   linuxraw_grab_stdin,
   linuxraw_set_rumble,
   linuxraw_get_joypad_driver,
   linuxraw_keyboard_mapping_is_blocked,
   linuxraw_keyboard_mapping_set_block,
};
