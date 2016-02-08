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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/xf86vmode.h>

#ifdef HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#include <boolean.h>
#include <retro_inline.h>
#include "../../driver.h"
#include "../keyboard_line.h"
#include "../input_keymaps.h"

static INLINE unsigned leading_ones(uint8_t c)
{
   unsigned ones = 0;
   while (c & 0x80)
   {
      ones++;
      c <<= 1;
   }

   return ones;
}

/* Simple implementation. Assumes the sequence is 
 * properly synchronized and terminated. */

static size_t conv_utf8_utf32(uint32_t *out,
      size_t out_chars, const char *in, size_t in_size)
{
   unsigned i;
   size_t ret = 0;
   while (in_size && out_chars)
   {
      unsigned ones, extra, shift;
      uint32_t c;
      uint8_t first = *in++;

      ones = leading_ones(first);
      if (ones > 6 || ones == 1) /* Invalid or desync. */
         break;

      extra = ones ? ones - 1 : ones;
      if (1 + extra > in_size) /* Overflow. */
         break;

      shift = (extra - 1) * 6;
      c     = (first & ((1 << (7 - ones)) - 1)) << (6 * extra);

      for (i = 0; i < extra; i++, in++, shift -= 6)
         c |= (*in & 0x3f) << shift;

      *out++ = c;
      in_size -= 1 + extra;
      out_chars--;
      ret++;
   }

   return ret;
}

void x11_handle_key_event(XEvent *event, XIC ic, bool filter)
{
   int i;
   unsigned state, key;
   uint16_t mod = 0;
   char keybuf[32] = {0};
   uint32_t chars[32] = {0};

   bool down     = event->type == KeyPress;
   int num       = 0;
   KeySym keysym = 0;

   if (down && !filter)
   {
#ifdef X_HAVE_UTF8_STRING
      Status status = 0;

      /* XwcLookupString doesn't seem to work. */
      num = Xutf8LookupString(ic, &event->xkey, keybuf, ARRAY_SIZE(keybuf), &keysym, &status);

      /* libc functions need UTF-8 locale to work properly, 
       * which makes mbrtowc a bit impractical.
       *
       * Use custom UTF8 -> UTF-32 conversion. */
      num = conv_utf8_utf32(chars, ARRAY_SIZE(chars), keybuf, num);
#else
      (void)ic;
      num = XLookupString(&event->xkey, keybuf, sizeof(keybuf), &keysym, NULL); /* ASCII only. */
      for (i = 0; i < num; i++)
         chars[i] = keybuf[i] & 0x7f;
#endif
   }

   key   = input_keymaps_translate_keysym_to_rk(keysym);
   state = event->xkey.state;

   if (state & ShiftMask)
      mod |= RETROKMOD_SHIFT;
   if (state & LockMask)
      mod |= RETROKMOD_CAPSLOCK;
   if (state & ControlMask)
      mod |= RETROKMOD_CTRL;
   if (state & Mod1Mask)
      mod |= RETROKMOD_ALT;
   if (state & Mod4Mask)
      mod |= RETROKMOD_META;
   if (IsKeypadKey(keysym))
      mod |= RETROKMOD_NUMLOCK;

   input_keyboard_event(down, key, chars[0], mod, RETRO_DEVICE_KEYBOARD);

   for (i = 1; i < num; i++)
      input_keyboard_event(down, RETROK_UNKNOWN,
            chars[i], mod, RETRO_DEVICE_KEYBOARD);
}
