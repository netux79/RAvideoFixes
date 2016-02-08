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

#include <file/file_path.h>
#include "../menu.h"
#include "../menu_cbs.h"
#include "../menu_entry.h"
#include "../menu_setting.h"

#include "../../runloop_data.h"

int action_scan_file(const char *path,
      const char *label, unsigned type, size_t idx)
{
   char fullpath[PATH_MAX_LENGTH] = {0};
   const char *menu_label         = NULL;
   const char *menu_path          = NULL;
   menu_handle_t *menu            = menu_driver_get_ptr();
   menu_list_t *menu_list         = menu_list_get_ptr();
   if (!menu || !menu_list)
      return -1;

   menu_list_get_last_stack(menu_list, &menu_path, &menu_label, NULL, NULL);

   fill_pathname_join(fullpath, menu_path, path, sizeof(fullpath));

   rarch_main_data_msg_queue_push(DATA_TYPE_DB, fullpath, "cb_db_scan_file", 0, 1, true);
   return 0;
}

int action_scan_directory(const char *path,
      const char *label, unsigned type, size_t idx)
{
   char fullpath[PATH_MAX_LENGTH] = {0};
   const char *menu_label         = NULL;
   const char *menu_path          = NULL;
   menu_handle_t *menu            = menu_driver_get_ptr();
   menu_list_t *menu_list         = menu_list_get_ptr();
   if (!menu || !menu_list)
      return -1;

   menu_list_get_last_stack(menu_list, &menu_path, &menu_label, NULL, NULL);

   strlcpy(fullpath, menu_path, sizeof(fullpath));

   if (path)
      fill_pathname_join(fullpath, fullpath, path, sizeof(fullpath));

   rarch_main_data_msg_queue_push(DATA_TYPE_DB, fullpath, "cb_db_scan_folder", 0, 1, true);
   return 0;
}

static int menu_cbs_init_bind_scan_compare_type(menu_file_list_cbs_t *cbs,
      unsigned type)
{
   switch (type)
   {
      case MENU_FILE_DIRECTORY:
         cbs->action_scan = action_scan_directory;
         break;
      case MENU_FILE_CARCHIVE:
      case MENU_FILE_PLAIN:
         cbs->action_scan = action_scan_file;
         break;
      default:
         return -1;
   }

   return 0;
}

int menu_cbs_init_bind_scan(menu_file_list_cbs_t *cbs,
      const char *path, const char *label, unsigned type, size_t idx,
      const char *elem0, const char *elem1,
      uint32_t label_hash, uint32_t menu_label_hash)
{
   if (!cbs)
      return -1;

   cbs->action_scan = NULL;

   menu_cbs_init_bind_scan_compare_type(cbs, type);

   return -1;
}
