/* RetroArch - A frontend for libretro.
 * Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 * Copyright (C) 2011-2015 - Daniel De Matteis
 * Copyright (C) 2012-2015 - Michael Lelli
 *
 * RetroArch is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Found-
 * ation, either version 3 of the License, or (at your option) any later version.
 *
 * RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with RetroArch.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <emscripten/emscripten.h>
#include "../../general.h"
#include <file/config_file.h>
#include "../../content.h"
#include "../frontend.h"
#include "../frontend_driver.h"
#include "../runloop_data.h"

static void emscripten_mainloop(void)
{
   int ret = rarch_main_iterate();
   rarch_main_data_iterate();
   if (ret != -1)
      return;

   main_exit(NULL);
   exit(0);
}

int main(int argc, char *argv[])
{
   settings_t *settings = config_get_ptr();

   emscripten_set_canvas_size(800, 600);
   rarch_main(argc, argv, NULL);
   emscripten_set_main_loop(emscripten_mainloop,
         settings->video.vsync ? 0 : INT_MAX, 1);

   return 0;
}
