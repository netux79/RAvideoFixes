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

#include <stdint.h>
#include <stddef.h>

#include <xtl.h>
#include <xbdm.h>

#include <file/file_path.h>
#ifndef IS_SALAMANDER
#include <file/file_list.h>
#endif
#include <retro_miscellaneous.h>

#include "platform_xdk.h"
#include "../../general.h"

static bool exit_spawn;
static bool exitspawn_start_game;

#ifdef _XBOX360

typedef struct _STRING 
{
    USHORT Length;
    USHORT MaximumLength;
    PCHAR Buffer;
} STRING, *PSTRING;

#ifdef __cplusplus
extern "C" {
#endif
VOID RtlInitAnsiString(PSTRING DestinationString, PCHAR SourceString);	
HRESULT ObDeleteSymbolicLink(PSTRING SymbolicLinkName);
HRESULT ObCreateSymbolicLink(PSTRING SymbolicLinkName, PSTRING DeviceName);
#ifdef __cplusplus
}
#endif

HRESULT xbox_io_mount(const char* szDrive, char* szDevice)
{
	STRING DeviceName, LinkName;
	CHAR szDestinationDrive[PATH_MAX_LENGTH];
	sprintf_s(szDestinationDrive, PATH_MAX_LENGTH, "\\??\\%s", szDrive);
	RtlInitAnsiString(&DeviceName, szDevice);
	RtlInitAnsiString(&LinkName, szDestinationDrive);
	ObDeleteSymbolicLink(&LinkName);
	return (HRESULT)ObCreateSymbolicLink(&LinkName, &DeviceName);
}
#endif

#ifdef _XBOX1
static HRESULT xbox_io_mount(char *szDrive, char *szDevice)
{
#ifndef IS_SALAMANDER
   global_t            *global = global_get_ptr();
   bool original_verbose       = global->verbosity;
   global->verbosity           = true;
#endif
   char szSourceDevice[48]     = {0};
   char szDestinationDrive[16] = {0};

   snprintf(szSourceDevice, sizeof(szSourceDevice),
         "\\Device\\%s", szDevice);
   snprintf(szDestinationDrive, sizeof(szDestinationDrive),
         "\\??\\%s", szDrive);
   RARCH_LOG("xbox_io_mount() - source device: %s.\n",
         szSourceDevice);
   RARCH_LOG("xbox_io_mount() - destination drive: %s.\n",
         szDestinationDrive);

   STRING DeviceName =
   {
      strlen(szSourceDevice),
      strlen(szSourceDevice) + 1,
      szSourceDevice
   };

   STRING LinkName =
   {
      strlen(szDestinationDrive),
      strlen(szDestinationDrive) + 1,
      szDestinationDrive
   };

   IoCreateSymbolicLink(&LinkName, &DeviceName);

#ifndef IS_SALAMANDER
   global->verbosity = original_verbose;
#endif
   return S_OK;
}

static HRESULT xbox_io_unmount(char *szDrive)
{
   char szDestinationDrive[16] = {0};

   snprintf(szDestinationDrive, sizeof(szDestinationDrive),
         "\\??\\%s", szDrive);

   STRING LinkName =
   {
      strlen(szDestinationDrive),
      strlen(szDestinationDrive) + 1,
      szDestinationDrive
   };

   IoDeleteSymbolicLink(&LinkName);

   return S_OK;
}
#endif

static void frontend_xdk_get_environment_settings(int *argc, char *argv[],
      void *args, void *params_data)
{
   HRESULT ret;
   (void)ret;

#ifndef IS_SALAMANDER
   global_t      *global = global_get_ptr();
   bool original_verbose = global->verbosity;

   global->verbosity = true;
#endif

#ifndef IS_SALAMANDER
#if defined(HAVE_LOGGER)
   logger_init();
#elif defined(HAVE_FILE_LOGGER)
   global->log_file = fopen("/retroarch-log.txt", "w");
#endif
#endif

#ifdef _XBOX360
   // detect install environment
   unsigned long license_mask;
   DWORD volume_device_type;

   if (XContentGetLicenseMask(&license_mask, NULL) != ERROR_SUCCESS)
      RARCH_LOG("RetroArch was launched as a standalone DVD, or using DVD emulation, or from the development area of the HDD.\n");
   else
   {
      XContentQueryVolumeDeviceType("GAME",&volume_device_type, NULL);

      switch(volume_device_type)
      {
         case XCONTENTDEVICETYPE_HDD:
            RARCH_LOG("RetroArch was launched from a content package on HDD.\n");
            break;
         case XCONTENTDEVICETYPE_MU:
            RARCH_LOG("RetroArch was launched from a content package on USB or Memory Unit.\n");
            break;
         case XCONTENTDEVICETYPE_ODD:
            RARCH_LOG("RetroArch was launched from a content package on Optical Disc Drive.\n");
            break;
         default:
            RARCH_LOG("RetroArch was launched from a content package on an unknown device type.\n");
            break;
      }
   }
#endif

#if defined(_XBOX1)
   strlcpy(g_defaults.core_dir, "D:", sizeof(g_defaults.core_dir));
   strlcpy(g_defaults.core_info_dir, "D:", sizeof(g_defaults.core_info_dir));
   fill_pathname_join(g_defaults.config_path, g_defaults.core_dir,
         "retroarch.cfg", sizeof(g_defaults.config_path));
   fill_pathname_join(g_defaults.savestate_dir, g_defaults.core_dir,
         "savestates", sizeof(g_defaults.savestate_dir));
   fill_pathname_join(g_defaults.sram_dir, g_defaults.core_dir,
         "savefiles", sizeof(g_defaults.sram_dir));
   fill_pathname_join(g_defaults.system_dir, g_defaults.core_dir,
         "system", sizeof(g_defaults.system_dir));
   fill_pathname_join(g_defaults.screenshot_dir, g_defaults.core_dir,
         "screenshots", sizeof(g_defaults.screenshot_dir));
#elif defined(_XBOX360)
   strlcpy(g_defaults.core_dir, "game:", sizeof(g_defaults.core_dir));
   strlcpy(g_defaults.core_info_dir,
         "game:", sizeof(g_defaults.core_info_dir));
   strlcpy(g_defaults.config_path,
         "game:\\retroarch.cfg", sizeof(g_defaults.config_path));
   strlcpy(g_defaults.screenshot_dir,
         "game:", sizeof(g_defaults.screenshot_dir));
   strlcpy(g_defaults.savestate_dir,
         "game:\\savestates", sizeof(g_defaults.savestate_dir));
   strlcpy(g_defaults.playlist_dir,
         "game:\\playlists", sizeof(g_defaults.playlist_dir));
   strlcpy(g_defaults.sram_dir,
         "game:\\savefiles", sizeof(g_defaults.sram_dir));
   strlcpy(g_defaults.system_dir,
         "game:\\system", sizeof(g_defaults.system_dir));
#endif

#ifndef IS_SALAMANDER
   static char path[PATH_MAX_LENGTH];
   *path = '\0';
#if defined(_XBOX1)
   LAUNCH_DATA ptr;
   DWORD launch_type;

   if (XGetLaunchInfo(&launch_type, &ptr) == ERROR_SUCCESS)
   {
      if (launch_type == LDT_FROM_DEBUGGER_CMDLINE)
      {
         RARCH_LOG("Launched from commandline debugger.\n");
         goto exit;
      }
      else
      {
         char *extracted_path = (char*)&ptr.Data;

         if (extracted_path && extracted_path[0] != '\0'
            && (strstr(extracted_path, "Pool") == NULL)
            /* Hack. Unknown problem */)
         {
            strlcpy(path, extracted_path, sizeof(path));
            RARCH_LOG("Auto-start game %s.\n", path);
         }
      }
   }
#elif defined(_XBOX360)
   DWORD dwLaunchDataSize;
   if (XGetLaunchDataSize(&dwLaunchDataSize) == ERROR_SUCCESS)
   {
      BYTE* pLaunchData = new BYTE[dwLaunchDataSize];
      XGetLaunchData(pLaunchData, dwLaunchDataSize);
	  AURORA_LAUNCHDATA_EXECUTABLE* aurora = (AURORA_LAUNCHDATA_EXECUTABLE*)pLaunchData;
	  char* extracted_path = new char[dwLaunchDataSize];
	  memset(extracted_path, 0, dwLaunchDataSize);
	  if (aurora->ApplicationId == AURORA_LAUNCHDATA_APPID && aurora->FunctionId == AURORA_LAUNCHDATA_EXECUTABLE_FUNCID)
	  {
		  if (xbox_io_mount("aurora:", aurora->SystemPath) >= 0)
			  sprintf_s(extracted_path, dwLaunchDataSize, "aurora:%s%s", aurora->RelativePath, aurora->Exectutable);
		  else
			  RARCH_LOG("Failed to mount %s as aurora:.\n", aurora->SystemPath);
	  }
	  else
		  sprintf_s(extracted_path, dwLaunchDataSize, "%s", pLaunchData);
      if (extracted_path && extracted_path[0] != '\0')
      {
         strlcpy(path, extracted_path, sizeof(path));
         RARCH_LOG("Auto-start game %s.\n", path);
      }

      if (pLaunchData)
         delete []pLaunchData;
   }
#endif
   if (path && path[0] != '\0')
   {
         struct rarch_main_wrap *args = (struct rarch_main_wrap*)params_data;

         if (args)
         {
            args->touched        = true;
            args->no_content     = false;
            args->verbose        = false;
            args->config_path    = NULL;
            args->sram_path      = NULL;
            args->state_path     = NULL;
            args->content_path   = path;
            args->libretro_path  = NULL;

            RARCH_LOG("Auto-start game %s.\n", path);
         }
   }
#endif

#ifndef IS_SALAMANDER
exit:
   global->verbosity = original_verbose;
#endif
}

static void frontend_xdk_init(void *data)
{
   (void)data;
#if defined(_XBOX1) && !defined(IS_SALAMANDER)
   // Mount drives
   xbox_io_mount("A:", "cdrom0");
   xbox_io_mount("C:", "Harddisk0\\Partition0");
   xbox_io_mount("E:", "Harddisk0\\Partition1");
   xbox_io_mount("Z:", "Harddisk0\\Partition2");
   xbox_io_mount("F:", "Harddisk0\\Partition6");
   xbox_io_mount("G:", "Harddisk0\\Partition7");
#endif
}

static void frontend_xdk_exec(const char *path, bool should_load_game);

static void frontend_xdk_set_fork(bool exit, bool start_game)
{
   exit_spawn = exit;
   exitspawn_start_game = start_game;
}

static void frontend_xdk_exitspawn(char *s, size_t len)
{
   bool should_load_game = false;
#ifndef IS_SALAMANDER
   should_load_game = exitspawn_start_game;

   if (!exit_spawn)
      return;
#endif
   frontend_xdk_exec(s, should_load_game);
}

static void frontend_xdk_exec(const char *path, bool should_load_game)
{
#ifndef IS_SALAMANDER
   global_t *global = global_get_ptr();
   bool original_verbose = global->verbosity;
   global->verbosity = true;
#endif
   (void)should_load_game;

   RARCH_LOG("Attempt to load executable: [%s].\n", path);
#ifdef IS_SALAMANDER
   if (path[0] != '\0')
      XLaunchNewImage(path, NULL);
#else
#if defined(_XBOX1)
   LAUNCH_DATA ptr;
   memset(&ptr, 0, sizeof(ptr));

   if (should_load_game && global->fullpath[0] != '\0')
      snprintf((char*)ptr.Data, sizeof(ptr.Data), "%s", global->fullpath);

   if (path[0] != '\0')
      XLaunchNewImage(path, ptr.Data[0] != '\0' ? &ptr : NULL);
#elif defined(_XBOX360)
   char game_path[1024] = {0};

   if (should_load_game && global->fullpath[0] != '\0')
   {
      strlcpy(game_path, global->fullpath, sizeof(game_path));
      XSetLaunchData(game_path, MAX_LAUNCH_DATA_SIZE);
   }

   if (path[0] != '\0')
      XLaunchNewImage(path, NULL);
#endif
#endif
#ifndef IS_SALAMANDER
   global->verbosity = original_verbose;
#endif
}

static int frontend_xdk_get_rating(void)
{
#if defined(_XBOX360)
   return 11;
#elif defined(_XBOX1)
   return 7;
#endif
}

enum frontend_architecture frontend_xdk_get_architecture(void)
{
#if defined(_XBOX360)
   return FRONTEND_ARCH_PPC;
#elif defined(_XBOX1)
   return FRONTEND_ARCH_X86;
#else
   return FRONTEND_ARCH_NONE;
#endif
}

static int frontend_xdk_parse_drive_list(void *data)
{
#ifndef IS_SALAMANDER
   file_list_t *list = (file_list_t*)data;

#if defined(_XBOX1)
   menu_list_push(list,
         "C:", "", MENU_FILE_DIRECTORY, 0, 0);
   menu_list_push(list,
         "D:", "", MENU_FILE_DIRECTORY, 0, 0);
   menu_list_push(list,
         "E:", "", MENU_FILE_DIRECTORY, 0, 0);
   menu_list_push(list,
         "F:", "", MENU_FILE_DIRECTORY, 0, 0);
   menu_list_push(list,
         "G:", "", MENU_FILE_DIRECTORY, 0, 0);
#elif defined(_XBOX360)
   menu_list_push(list,
         "game:", "", MENU_FILE_DIRECTORY, 0, 0);
#endif
#endif

   return 0;
}

const frontend_ctx_driver_t frontend_ctx_xdk = {
   frontend_xdk_get_environment_settings,
   frontend_xdk_init,
   NULL,                         /* deinit */
   frontend_xdk_exitspawn,
   NULL,                         /* process_args */
   frontend_xdk_exec,
   frontend_xdk_set_fork,
   NULL,                         /* shutdown */
   NULL,                         /* get_name */
   NULL,                         /* get_os */
   frontend_xdk_get_rating,
   NULL,                         /* load_content */
   frontend_xdk_get_architecture,
   NULL,                         /* get_powerstate */
   frontend_xdk_parse_drive_list,
   "xdk",
};
