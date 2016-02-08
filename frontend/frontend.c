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

#include <file/file_path.h>

#include "../system.h"
#include "../driver.h"
#include "../general.h"
#include "../retroarch.h"
#include "../runloop.h"
#include "../runloop_data.h"

#include "frontend.h"

#define MAX_ARGS 32

/**
 * main_exit_save_config:
 *
 * Saves configuration file to disk, and (optionally)
 * autosave state.
 **/
void main_exit_save_config(void)
{
   settings_t *settings = config_get_ptr();
   global_t   *global   = global_get_ptr();

   if (settings->config_save_on_exit && *global->config_path)
   {
      /* restore original paths in case per-core organization is enabled */
      if (settings->sort_savefiles_enable && orig_savefile_dir[0] != '\0')
	     strlcpy(global->savefile_dir,orig_savefile_dir,sizeof(global->savefile_dir));
      if (settings->sort_savestates_enable && orig_savestate_dir[0] != '\0')
	     strlcpy(global->savestate_dir,orig_savestate_dir,sizeof(global->savestate_dir));

      /* Save last core-specific config to the default config location,
       * needed on consoles for core switching and reusing last good 
       * config for new cores.
       */
      config_save_file(global->config_path);

      /* Flush out the core specific config. */
      if (*global->core_specific_config_path &&
            settings->core_specific_config)
         config_save_file(global->core_specific_config_path);
   }

   event_command(EVENT_CMD_AUTOSAVE_STATE);
}

/**
 * main_exit:
 *
 * Cleanly exit RetroArch.
 *
 * Also saves configuration files to disk,
 * and (optionally) autosave state.
 **/
void main_exit(void *args)
{
   driver_t *driver                      = driver_get_ptr();
   settings_t *settings                  = config_get_ptr();
   global_t   *global                    = global_get_ptr();
   const frontend_ctx_driver_t *frontend = frontend_get_ptr();
   const ui_companion_driver_t *ui       = ui_companion_get_ptr();

   main_exit_save_config();

   if (global->main_is_init)
   {
#ifdef HAVE_MENU
      /* Do not want menu context to live any more. */
      driver->menu_data_own = false;
#endif
      rarch_main_deinit();
   }

   event_command(EVENT_CMD_PERFCNT_REPORT_FRONTEND_LOG);

#if defined(HAVE_LOGGER) && !defined(ANDROID)
   logger_shutdown();
#endif

   if (frontend)
   {
      if (frontend->deinit)
         frontend->deinit(args);

      if (frontend->exitspawn)
         frontend->exitspawn(settings->libretro,
               sizeof(settings->libretro));
   }

   rarch_main_free();

   if (ui)
   {
      if (ui->deinit)
         ui->deinit(driver->ui_companion_data);
   }

   if (frontend)
   {
      if (frontend->shutdown)
         frontend->shutdown(false);
   }

   driver_free();
}

static void check_defaults_dirs(void)
{
   if (*g_defaults.core_assets_dir)
      path_mkdir(g_defaults.core_assets_dir);
   if (*g_defaults.remap_dir)
      path_mkdir(g_defaults.remap_dir);
   if (*g_defaults.autoconfig_dir)
      path_mkdir(g_defaults.autoconfig_dir);
   if (*g_defaults.audio_filter_dir)
      path_mkdir(g_defaults.audio_filter_dir);
   if (*g_defaults.video_filter_dir)
      path_mkdir(g_defaults.video_filter_dir);
   if (*g_defaults.assets_dir)
      path_mkdir(g_defaults.assets_dir);
   if (*g_defaults.playlist_dir)
      path_mkdir(g_defaults.playlist_dir);
   if (*g_defaults.core_dir)
      path_mkdir(g_defaults.core_dir);
   if (*g_defaults.core_info_dir)
      path_mkdir(g_defaults.core_info_dir);
   if (*g_defaults.overlay_dir)
      path_mkdir(g_defaults.overlay_dir);
   if (*g_defaults.port_dir)
      path_mkdir(g_defaults.port_dir);
   if (*g_defaults.shader_dir)
      path_mkdir(g_defaults.shader_dir);
   if (*g_defaults.savestate_dir)
      path_mkdir(g_defaults.savestate_dir);
   if (*g_defaults.sram_dir)
      path_mkdir(g_defaults.sram_dir);
   if (*g_defaults.system_dir)
      path_mkdir(g_defaults.system_dir);
   if (*g_defaults.resampler_dir)
      path_mkdir(g_defaults.resampler_dir);
   if (*g_defaults.menu_config_dir)
      path_mkdir(g_defaults.menu_config_dir);
   if (*g_defaults.content_history_dir)
      path_mkdir(g_defaults.content_history_dir);
   if (*g_defaults.extraction_dir)
      path_mkdir(g_defaults.extraction_dir);
   if (*g_defaults.database_dir)
      path_mkdir(g_defaults.database_dir);
   if (*g_defaults.cursor_dir)
      path_mkdir(g_defaults.cursor_dir);
   if (*g_defaults.cheats_dir)
      path_mkdir(g_defaults.cheats_dir);
}

static void history_playlist_push(content_playlist_t *playlist,
      const char *path, const char *core_path,
      struct retro_system_info *info)
{
   char tmp[PATH_MAX_LENGTH]             = {0};
   global_t                    *global   = global_get_ptr();
   rarch_system_info_t *system           = rarch_system_info_get_ptr();

   if (!playlist || (global->core_type == CORE_TYPE_DUMMY) || !info)
      return;

   /* Path can be relative here.
    * Ensure we're pushing absolute path. */

   strlcpy(tmp, path, sizeof(tmp));

   if (*tmp)
      path_resolve_realpath(tmp, sizeof(tmp));

   if (system->no_content || *tmp)
      content_playlist_push(playlist,
            *tmp ? tmp : NULL,
            NULL,
            core_path,
            info->library_name,
            NULL,
            NULL);
}

/**
 * main_load_content:
 * @argc             : Argument count.
 * @argv             : Argument variable list.
 * @args             : Arguments passed from callee.
 * @environ_get      : Function passed for environment_get function.
 * @process_args     : Function passed for process_args function.
 *
 * Loads content file and starts up RetroArch.
 * If no content file can be loaded, will start up RetroArch
 * as-is.
 *
 * Returns: false (0) if rarch_main_init failed, otherwise true (1).
 **/
bool main_load_content(int argc, char **argv, void *args,
      environment_get_t environ_get,
      process_args_t process_args)
{
   unsigned i;
   bool retval                       = true;
   int ret = 0, rarch_argc           = 0;
   char *rarch_argv[MAX_ARGS]        = {NULL};
   char *argv_copy [MAX_ARGS]        = {NULL};
   char **rarch_argv_ptr             = (char**)argv;
   int *rarch_argc_ptr               = (int*)&argc;
   global_t *global                  = global_get_ptr();
   struct rarch_main_wrap *wrap_args = (struct rarch_main_wrap*)
      calloc(1, sizeof(*wrap_args));

   if (!wrap_args)
      return false;

   (void)rarch_argc_ptr;
   (void)rarch_argv_ptr;
   (void)ret;

   rarch_assert(wrap_args);

   if (environ_get)
      environ_get(rarch_argc_ptr, rarch_argv_ptr, args, wrap_args);

   check_defaults_dirs();

   if (wrap_args->touched)
   {
      rarch_main_init_wrap(wrap_args, &rarch_argc, rarch_argv);
      memcpy(argv_copy, rarch_argv, sizeof(rarch_argv));
      rarch_argv_ptr = (char**)rarch_argv;
      rarch_argc_ptr = (int*)&rarch_argc;
   }

   if (global->main_is_init)
      rarch_main_deinit();

   if ((ret = rarch_main_init(*rarch_argc_ptr, rarch_argv_ptr)))
   {
      retval = false;
      goto error;
   }

   event_command(EVENT_CMD_RESUME);

   if (process_args)
      process_args(rarch_argc_ptr, rarch_argv_ptr);

error:
   for (i = 0; i < ARRAY_SIZE(argv_copy); i++)
      free(argv_copy[i]);
   free(wrap_args);
   return retval;
}

/**
 * main_entry:
 *
 * Main function of RetroArch.
 *
 * If HAVE_MAIN is not defined, will contain main loop and will not
 * be exited from until we exit the program. Otherwise, will 
 * just do initialization.
 *
 * Returns: varies per platform.
 **/
int rarch_main(int argc, char *argv[], void *data)
{
   void *args                      = (void*)data;
   int ret                         = 0;
   settings_t *settings            = NULL;
   driver_t *driver                = NULL;

   rarch_main_alloc();

   driver = driver_get_ptr();

   if (driver)
      driver->frontend_ctx = (frontend_ctx_driver_t*)frontend_ctx_init_first();

   if (!driver || !driver->frontend_ctx)
      RARCH_WARN("Frontend context could not be initialized.\n");

   if (driver->frontend_ctx && driver->frontend_ctx->init)
      driver->frontend_ctx->init(args);

   rarch_main_new();

   if (driver->frontend_ctx)
   {
      if (!(ret = (main_load_content(argc, argv, args,
                     driver->frontend_ctx->environment_get,
                     driver->frontend_ctx->process_args))))
         return ret;
   }

   event_command(EVENT_CMD_HISTORY_INIT);

   settings = config_get_ptr();

   if (settings->history_list_enable)
   {
      global_t *global             = global_get_ptr();
      rarch_system_info_t *system = rarch_system_info_get_ptr();

      if (global->content_is_init || system->no_content)
         history_playlist_push(
               g_defaults.history,
               global->fullpath,
               settings->libretro,
               system ? &system->info : NULL);
   }

   if (driver)
      driver->ui_companion = (ui_companion_driver_t*)ui_companion_init_first();

   if (driver->ui_companion && driver->ui_companion->toggle)
   {
      if (settings->ui.companion_start_on_boot)
         driver->ui_companion->toggle(driver->ui_companion_data);
   }

#ifndef HAVE_MAIN
   do{
      ret = rarch_main_iterate();
      rarch_main_data_iterate();
   }while(ret != -1);

   main_exit(args);
#endif

   return 0;
}

#ifndef HAVE_MAIN
int main(int argc, char *argv[])
{
   return rarch_main(argc, argv, NULL);
}
#endif
