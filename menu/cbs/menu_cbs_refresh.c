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
#include "../menu_hash.h"
#include "../menu_displaylist.h"

static int action_refresh_default(file_list_t *list, file_list_t *menu_list)
{
   return menu_displaylist_push(list, menu_list);
}

int menu_cbs_init_bind_refresh(menu_file_list_cbs_t *cbs,
      const char *path, const char *label, unsigned type, size_t idx,
      const char *elem0, const char *elem1,
      uint32_t label_hash, uint32_t menu_label_hash)
{
   if (!cbs)
      return -1;

   switch (label_hash)
   {
      case MENU_VALUE_MAIN_MENU:
         cbs->action_refresh = NULL;
         break;
      default:
         cbs->action_refresh = action_refresh_default;
         break;
   }


   return -1;
}
