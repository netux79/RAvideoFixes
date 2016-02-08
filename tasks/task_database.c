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

#include <compat/strcasestr.h>
#include <compat/strl.h>

#include "../dir_list_special.h"
#include "../file_ops.h"
#include "../msg_hash.h"
#include "../general.h"
#include "../runloop_data.h"
#include "tasks.h"

#define CB_DB_SCAN_FILE    0x70ce56d2U
#define CB_DB_SCAN_FOLDER  0xde2bef8eU

#define HASH_EXTENSION_ZIP 0x0b88c7d8U

#ifdef HAVE_LIBRETRODB

#ifdef HAVE_ZLIB
static int zlib_compare_crc32(const char *name, const char *valid_exts,
      const uint8_t *cdata, unsigned cmode, uint32_t csize, uint32_t size,
      uint32_t crc32, void *userdata)
{
   database_state_handle_t *db_state = (database_state_handle_t*)userdata;
    
   db_state->crc = crc32;

   strlcpy(db_state->zip_name, name, sizeof(db_state->zip_name));

#if 0
   RARCH_LOG("Going to compare CRC 0x%x for %s\n", crc32, name);
#endif

   return 1;
}
#endif

static int database_info_iterate_start
(database_info_handle_t *db, const char *name)
{
   char msg[PATH_MAX_LENGTH] = {0};

   snprintf(msg, sizeof(msg),
#ifdef _WIN32
         "%Iu/%Iu: %s %s...\n",
#else
         "%zu/%zu: %s %s...\n",
#endif
         db->list_ptr,
         db->list->size,
         msg_hash_to_str(MSG_SCANNING),
         name);

   if (msg[0] != '\0')
      rarch_main_msg_queue_push(msg, 1, 180, true);

#if 0
   RARCH_LOG("msg: %s\n", msg);
#endif

   db->status = DATABASE_STATUS_ITERATE;

   return 0;
}

static int database_info_iterate_playlist(
      database_state_handle_t *db_state,
      database_info_handle_t *db, const char *name)
{
   uint32_t extension_hash          = 0;
   char parent_dir[PATH_MAX_LENGTH] = {0};

   path_parent_dir(parent_dir);

   extension_hash = msg_hash_calculate(path_get_extension(name));

   switch (extension_hash)
   {
      case HASH_EXTENSION_ZIP:
#ifdef HAVE_ZLIB
         db->type = DATABASE_TYPE_ITERATE_ZIP;
         memset(&db->state, 0, sizeof(zlib_transfer_t));
         db_state->zip_name[0] = '\0';
         db->state.type = ZLIB_TRANSFER_INIT;

         return 1;
#endif
      default:
         {
            ssize_t ret;
            int read_from            = read_file(name, (void**)&db_state->buf, &ret);

            if (read_from != 1 || ret <= 0)
               return 0;


#ifdef HAVE_ZLIB
            db_state->crc = zlib_crc32_calculate(db_state->buf, ret);
#endif
            db->type = DATABASE_TYPE_CRC_LOOKUP;
         }
         break;
   }

   return 1;
}

static int database_info_list_iterate_end_no_match(database_state_handle_t *db_state)
{
   /* Reached end of database list, CRC match probably didn't succeed. */
   db_state->list_index  = 0;
   db_state->entry_index = 0;

   if (db_state->crc != 0)
      db_state->crc = 0;
   return 0;
}

static int database_info_iterate_next(database_info_handle_t *db)
{
   db->list_ptr++;

   if (db->list_ptr < db->list->size)
      return 0;
   return -1;
}

static int database_info_list_iterate_new(database_state_handle_t *db_state, const char *query)
{
   const char *new_database = db_state->list->elems[db_state->list_index].data;
#if 0
   RARCH_LOG("Check database [%d/%d] : %s\n", (unsigned)db_state->list_index, 
         (unsigned)db_state->list->size, new_database);
#endif
   if (db_state->info)
      database_info_list_free(db_state->info);
   db_state->info = database_info_list_new(new_database, query);
   return 0;
}

static int database_info_list_iterate_found_match(
      database_state_handle_t *db_state,
      database_info_handle_t *db,
      const char *zip_name
      )
{
   char db_crc[PATH_MAX_LENGTH]                = {0};
   char db_playlist_path[PATH_MAX_LENGTH]      = {0};
   char  db_playlist_base_str[PATH_MAX_LENGTH] = {0};
   char entry_path_str[PATH_MAX_LENGTH]        = {0};
   content_playlist_t   *playlist = NULL;
   settings_t           *settings = config_get_ptr();
   const char            *db_path = db_state->list->elems[db_state->list_index].data;
   const char         *entry_path = db ? db->list->elems[db->list_ptr].data : NULL;
   database_info_t *db_info_entry = &db_state->info->list[db_state->entry_index];

   fill_short_pathname_representation(db_playlist_base_str,
         db_path, sizeof(db_playlist_base_str));

   path_remove_extension(db_playlist_base_str);

   strlcat(db_playlist_base_str, ".lpl", sizeof(db_playlist_base_str));
   fill_pathname_join(db_playlist_path, settings->playlist_directory,
         db_playlist_base_str, sizeof(db_playlist_path));

   playlist = content_playlist_init(db_playlist_path, 1000);


   snprintf(db_crc, sizeof(db_crc), "%08X|crc", db_info_entry->crc32);

   strlcpy(entry_path_str, entry_path, sizeof(entry_path_str));

   if (zip_name && zip_name[0] != '\0')
      fill_pathname_join_delim(entry_path_str, entry_path_str, zip_name,
            '#', sizeof(entry_path_str));

#if 0
   RARCH_LOG("Found match in database !\n");

   RARCH_LOG("Path: %s\n", db_path);
   RARCH_LOG("CRC : %s\n", db_crc);
   RARCH_LOG("Playlist Path: %s\n", db_playlist_path);
   RARCH_LOG("Entry Path: %s\n", entry_path);
   RARCH_LOG("Playlist not NULL: %d\n", playlist != NULL);
   RARCH_LOG("ZIP entry: %s\n", zip_name);
   RARCH_LOG("entry path str: %s\n", entry_path_str);
#endif

   content_playlist_push(playlist, entry_path_str,
         db_info_entry->name, "DETECT", "DETECT", db_crc, db_playlist_base_str);

   content_playlist_write_file(playlist);
   content_playlist_free(playlist);
   return 0;
}

/* End of entries in database info list and didn't find a 
 * match, go to the next database. */
static int database_info_list_iterate_next(
      database_state_handle_t *db_state
      )
{
   db_state->list_index++;
   db_state->entry_index = 0;

   database_info_list_free(db_state->info);
   db_state->info        = NULL;

   return 1;
}

static int database_info_iterate_crc_lookup(
      database_state_handle_t *db_state,
      database_info_handle_t *db,
      const char *zip_entry)
{

   if ((unsigned)db_state->list_index == (unsigned)db_state->list->size)
      return database_info_list_iterate_end_no_match(db_state);

   if (db_state->entry_index == 0)
   {
      char query[50] = {0};
      snprintf(query, sizeof(query), "{crc: b\"%08X\"}", swap_if_big32(db_state->crc));

      database_info_list_iterate_new(db_state, query);
   }

   if (db_state->info)
   {
      database_info_t *db_info_entry = &db_state->info->list[db_state->entry_index];

      if (db_info_entry && db_info_entry->crc32)
      {
#if 0
         RARCH_LOG("CRC32: 0x%08X , entry CRC32: 0x%08X (%s).\n",
                   db_state->crc, db_info_entry->crc32, db_info_entry->name);
#endif
         if (db_state->crc == db_info_entry->crc32)
            database_info_list_iterate_found_match(db_state, db, zip_entry);
      }
   }

   db_state->entry_index++;

   if (db_state->entry_index >= db_state->info->count)
      return database_info_list_iterate_next(db_state);

   if (db_state->list_index < db_state->list->size)
   {
      /* Didn't reach the end of the database list yet,
       * continue iterating. */
      return 1;
   }

   if (db_state->info)
      database_info_list_free(db_state->info);
   return 0;
}

static int database_info_iterate_playlist_zip(
      database_state_handle_t *db_state,
      database_info_handle_t *db, const char *name)
{
   bool returnerr = true;
#ifdef HAVE_ZLIB
   if (db_state->crc != 0)
      return database_info_iterate_crc_lookup(db_state, db, db_state->zip_name);
   else
   {
      if (zlib_parse_file_iterate(&db->state,
               &returnerr, name, NULL, zlib_compare_crc32,
               (void*)db_state) != 0)
         return 0;

      if (db_state->crc)
         zlib_parse_file_iterate_stop(&db->state);
   }
#endif

   return 1;
}

static int database_info_iterate(database_state_handle_t *state, database_info_handle_t *db)
{
   const char *name = db ? db->list->elems[db->list_ptr].data : NULL;

   if (!db || !db->list)
      return -1;

   if (!name)
      return 0;

   switch (db->type)
   {
      case DATABASE_TYPE_NONE:
         break;
      case DATABASE_TYPE_ITERATE:
         return database_info_iterate_playlist(state, db, name);
      case DATABASE_TYPE_ITERATE_ZIP:
         return database_info_iterate_playlist_zip(state, db, name);
      case DATABASE_TYPE_CRC_LOOKUP:
         return database_info_iterate_crc_lookup(state, db, NULL);
   }

   return 0;
}

static int database_info_poll(db_handle_t *db)
{
   uint32_t cb_type_hash        = 0;
   char elem0[PATH_MAX_LENGTH]  = {0};
   struct string_list *str_list = NULL;
   const char *path = msg_queue_pull(db->msg_queue);

   if (!path)
      return -1;

   str_list                     = string_split(path, "|"); 

   if (!str_list)
      goto error;

   if (str_list->size > 0)
      strlcpy(elem0, str_list->elems[0].data, sizeof(elem0));
   if (str_list->size > 1)
      cb_type_hash = msg_hash_calculate(str_list->elems[1].data);

   switch (cb_type_hash)
   {
      case CB_DB_SCAN_FILE:
         db->handle = database_info_file_init(elem0, DATABASE_TYPE_ITERATE);
         break;
      case CB_DB_SCAN_FOLDER:
         db->handle = database_info_dir_init(elem0, DATABASE_TYPE_ITERATE);
         break;
   }

   string_list_free(str_list);

   return 0;

error:
   if (str_list)
      string_list_free(str_list);

   return -1;
}

static void rarch_main_data_db_cleanup_state(database_state_handle_t *db_state)
{
   if (!db_state)
      return;

   if (db_state->buf)
      free(db_state->buf);
   db_state->buf = NULL;
}

void rarch_main_data_db_iterate(bool is_thread, void *data)
{
   data_runloop_t         *runloop   = (data_runloop_t*)data;
   database_info_handle_t      *db   = runloop ? runloop->db.handle : NULL;
   database_state_handle_t *db_state = runloop ? &runloop->db.state : NULL;
   const char *name = db ? db->list->elems[db->list_ptr].data : NULL;

   if (!db || !runloop)
      goto do_poll;

   switch (db->status)
   {
      case DATABASE_STATUS_ITERATE_BEGIN:
         if (db_state && !db_state->list)
            db_state->list = dir_list_new_special(NULL, DIR_LIST_DATABASES);
         db->status = DATABASE_STATUS_ITERATE_START;
         break;
      case DATABASE_STATUS_ITERATE_START:
         rarch_main_data_db_cleanup_state(db_state);
         db_state->list_index  = 0;
         db_state->entry_index = 0;
         database_info_iterate_start(db, name);
         break;
      case DATABASE_STATUS_ITERATE:
         if (database_info_iterate(&runloop->db.state, db) == 0)
         {
            db->status = DATABASE_STATUS_ITERATE_NEXT;
            db->type   = DATABASE_TYPE_ITERATE;
         }
         break;
      case DATABASE_STATUS_ITERATE_NEXT:
         if (database_info_iterate_next(db) == 0)
         {
            db->status = DATABASE_STATUS_ITERATE_START;
            db->type   = DATABASE_TYPE_ITERATE;
         }
         else
         {
            rarch_main_msg_queue_push_new(MSG_SCANNING_OF_DIRECTORY_FINISHED, 0, 180, true);
            db->status = DATABASE_STATUS_FREE;
         }
         break;
      case DATABASE_STATUS_FREE:
         if (db_state->list)
            dir_list_free(db_state->list);
         db_state->list = NULL;
         rarch_main_data_db_cleanup_state(db_state);
         database_info_free(db);
         if (runloop->db.handle)
            free(runloop->db.handle);
         runloop->db.handle = NULL;
         break;
      default:
      case DATABASE_STATUS_NONE:
         goto do_poll;
   }

   return;

do_poll:
   if (database_info_poll(&runloop->db) != -1)
   {
      if (runloop->db.handle)
         runloop->db.handle->status = DATABASE_STATUS_ITERATE_BEGIN;
   }
}
#endif
