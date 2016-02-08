/* RetroArch - A frontend for libretro.
* Copyright (C) 2010-2014 - Hans-Kristian Arntzen
* Copyright (C) 2011-2015 - Daniel De Matteis
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

#if defined(HAVE_CG) || defined(HAVE_HLSL) || defined(HAVE_GLSL)
#define HAVE_SHADERS
#endif

#if defined(HAVE_ZLIB) || defined(HAVE_7ZIP)
#define HAVE_COMPRESSION
#endif

#if defined(_MSC_VER)
#include <compat/posix_string.h>
#endif

/*============================================================
CONSOLE EXTENSIONS
============================================================ */
#ifdef RARCH_CONSOLE

#if defined(HAVE_LOGGER) && defined(__PSL1GHT__)
#include "../logger/netlogger/psl1ght_logger.c"
#elif defined(HAVE_LOGGER) && !defined(ANDROID)
#include "../logger/netlogger/logger.c"
#endif

#ifdef HW_DOL
#include "../ngc/ssaram.c"
#endif

#endif

#ifdef HAVE_ZLIB
#include "../libretro-common/file/file_extract.c"
#include "../decompress/zip_support.c"
#endif

/*============================================================
PERFORMANCE
============================================================ */

#ifdef ANDROID
#include "../performance/performance_android.c"
#endif

#include "../performance.c"

/*============================================================
COMPATIBILITY
============================================================ */
#include "../compat/compat.c"
#include "../libretro-common/compat/compat_fnmatch.c"

/*============================================================
CONFIG FILE
============================================================ */
#if defined(_MSC_VER)
#undef __LIBRETRO_SDK_COMPAT_POSIX_STRING_H
#undef __LIBRETRO_SDK_COMPAT_MSVC_H
#undef strcasecmp
#endif

#include "../libretro-common/file/config_file.c"
#include "../libretro-common/file/config_file_userdata.c"
#include "../core_options.c"

/*============================================================
CHEATS
============================================================ */
#include "../cheats.c"
#include "../libretro-common/hash/rhash.c"

/*============================================================
UI COMMON CONTEXT
============================================================ */
#if defined(_WIN32)
#include "../gfx/common/win32_common.c"
#endif

/*============================================================
VIDEO CONTEXT
============================================================ */

#include "../gfx/video_context_driver.c"
#include "../gfx/drivers_context/gfx_null_ctx.c"

#if defined(__CELLOS_LV2__)
#include "../gfx/drivers_context/ps3_ctx.c"
#elif defined(ANDROID)
#include "../gfx/drivers_context/androidegl_ctx.c"
#elif defined(__QNX__)
#include "../gfx/drivers_context/bbqnx_ctx.c"
#elif defined(EMSCRIPTEN)
#include "../gfx/drivers_context/emscriptenegl_ctx.c"
#elif defined(__APPLE__) && !defined(TARGET_IPHONE_SIMULATOR) && !defined(TARGET_OS_IPHONE)
#include "../gfx/drivers_context/cgl_ctx.c"
#endif


#if defined(HAVE_OPENGL)

#if defined(HAVE_KMS)
#include "../gfx/drivers_context/drm_egl_ctx.c"
#endif
#if defined(HAVE_VIDEOCORE)
#include "../gfx/drivers_context/vc_egl_ctx.c"
#endif


#if defined(_WIN32) && !defined(_XBOX)
#include "../gfx/drivers_context/wgl_ctx.c"
#include "../gfx/drivers_wm/win32_shader_dlg.c"
#endif

#endif

#if defined(HAVE_X11)
#include "../gfx/common/x11_common.c"

#ifndef HAVE_OPENGLES
#include "../gfx/drivers_context/glx_ctx.c"
#endif

#ifdef HAVE_EGL
#include "../gfx/drivers_context/xegl_ctx.c"
#endif

#endif


/*============================================================
VIDEO SHADERS
============================================================ */

#ifdef HAVE_SHADERS
#include "../gfx/video_shader_driver.c"
#include "../gfx/video_shader_parse.c"

#include "../gfx/drivers_shader/shader_null.c"

#ifdef HAVE_CG
#ifdef HAVE_OPENGL
#include "../gfx/drivers_shader/shader_gl_cg.c"
#endif
#endif

#ifdef HAVE_HLSL
#include "../gfx/drivers_shader/shader_hlsl.c"
#endif

#ifdef HAVE_GLSL
#include "../gfx/drivers_shader/shader_glsl.c"
#endif

#endif

/*============================================================
VIDEO IMAGE
============================================================ */

#include "../gfx/image/image.c"
#include "../gfx/video_texture.c"

#include "../libretro-common/formats/tga/tga_decode.c"

#ifdef HAVE_IMAGEVIEWER
#include "../cores/image_core.c"
#endif

#ifdef HAVE_RPNG
#include "../libretro-common/formats/png/rpng_fbio.c"
#include "../libretro-common/formats/png/rpng_nbio.c"
#include "../libretro-common/formats/png/rpng_decode.c"
#include "../libretro-common/formats/png/rpng_encode.c"
#endif

/*============================================================
VIDEO DRIVER
============================================================ */

#if defined(GEKKO)
#ifdef HW_RVL
#include "../wii/vi_encoder.c"
#include "../wii/mem2_manager.c"
#endif
#endif

#ifdef HAVE_VG
#include "../gfx/drivers/vg.c"
#include "../libretro-common/gfx/math/matrix_3x3.c"
#endif

#ifdef HAVE_OMAP
#include "../gfx/drivers/omap_gfx.c"
#endif

#ifdef HAVE_OPENGL
#include "../libretro-common/gfx/math/matrix_4x4.c"

#include "../gfx/drivers/gl.c"
#include "../gfx/drivers/gl_common.c"

#ifndef HAVE_PSGL
#include "../libretro-common/glsym/rglgen.c"
#ifdef HAVE_OPENGLES2
#include "../libretro-common/glsym/glsym_es2.c"
#else
#include "../libretro-common/glsym/glsym_gl.c"
#endif
#endif

#endif

#ifdef HAVE_XVIDEO
#include "../gfx/drivers/xvideo.c"
#endif

#if defined(HAVE_D3D)
#include "../gfx/d3d/render_chain_driver.c"
#include "../gfx/d3d/render_chain_null.c"
#endif

#if defined(GEKKO)
#include "../gfx/drivers/gx_gfx.c"
#elif defined(PSP)
#include "../gfx/drivers/psp1_gfx.c"
#elif defined(_3DS)
#include "../gfx/drivers/ctr_gfx.c"
#elif defined(XENON)
#include "../gfx/drivers/xenon360_gfx.c"
#endif
#include "../gfx/drivers/nullgfx.c"

/*============================================================
FONTS
============================================================ */

#include "../gfx/font_renderer_driver.c"
#include "../gfx/drivers_font_renderer/bitmapfont.c"
#include "../gfx/font_driver.c"

#if defined(HAVE_FREETYPE)
#include "../gfx/drivers_font_renderer/freetype.c"
#endif

#if defined(__APPLE__) && defined(HAVE_CORETEXT)
#include "../gfx/drivers_font_renderer/coretext.c"
#endif

#if defined(HAVE_LIBDBGFONT)
#include "../gfx/drivers_font/ps_libdbgfont.c"
#endif

#if defined(HAVE_OPENGL)
#include "../gfx/drivers_font/gl_raster_font.c"
#endif

#if defined(_XBOX1)
#include "../gfx/drivers_font/xdk1_xfonts.c"
#endif

#if defined(_XBOX360)
#include "../gfx/drivers_font/xdk360_fonts.c"
#endif

/*============================================================
INPUT
============================================================ */
#include "../input/input_autodetect.c"
#include "../input/input_joypad_driver.c"
#include "../input/input_joypad.c"
#include "../input/input_hid_driver.c"
#include "../input/input_common.c"
#include "../input/input_keymaps.c"
#include "../input/input_remapping.c"
#include "../input/input_sensor.c"
#include "../input/keyboard_line.c"

#ifdef HAVE_OVERLAY
#include "../input/input_overlay.c"
#include "../tasks/task_overlay.c"
#endif

#if defined(__CELLOS_LV2__)
#include "../input/drivers/ps3_input.c"
#include "../input/drivers_joypad/ps3_joypad.c"
#include "../input/autoconf/builtin_ps3.c"
#elif defined(SN_TARGET_PSP2) || defined(PSP)
#include "../input/drivers/psp_input.c"
#include "../input/drivers_joypad/psp_joypad.c"
#include "../input/autoconf/builtin_psp.c"
#elif defined(_3DS)
#include "../input/drivers/ctr_input.c"
#include "../input/drivers_joypad/ctr_joypad.c"
#include "../input/autoconf/builtin_ctr.c"
#elif defined(GEKKO)
#ifdef HAVE_LIBSICKSAXIS
#include "../input/drivers_joypad/gx_sicksaxis.c"
#endif
#include "../input/drivers/gx_input.c"
#include "../input/drivers_joypad/gx_joypad.c"
#include "../input/autoconf/builtin_gx.c"
#elif defined(_XBOX)
#include "../input/drivers/xdk_xinput_input.c"
#include "../input/drivers_joypad/xdk_joypad.c"
#include "../input/autoconf/builtin_xdk.c"
#elif defined(_WIN32)
#include "../input/autoconf/builtin_win.c"
#elif defined(XENON)
#include "../input/drivers/xenon360_input.c"
#elif defined(ANDROID)
#include "../input/drivers/android_input.c"
#include "../input/drivers_joypad/android_joypad.c"
#elif defined(HAVE_COCOA) || defined(HAVE_COCOATOUCH)
#include "../input/drivers/cocoa_input.c"
#elif defined(__QNX__)
#include "../input/drivers/qnx_input.c"
#include "../input/drivers_joypad/qnx_joypad.c"
#elif defined(EMSCRIPTEN)
#include "../input/drivers/rwebinput_input.c"
#endif

#ifdef HAVE_DINPUT
#include "../input/drivers/dinput.c"
#include "../input/drivers_joypad/dinput_joypad.c"
#endif

#ifdef HAVE_XINPUT
#include "../input/drivers_joypad/xinput_joypad.c"
#endif

#if defined(__linux__) && !defined(ANDROID)
#include "../input/drivers/linuxraw_input.c"
#include "../input/drivers_joypad/linuxraw_joypad.c"
#endif

#ifdef HAVE_X11
#include "../input/drivers/x11_input.c"
#endif

#ifdef HAVE_UDEV
#include "../input/drivers/udev_input.c"
#include "../input/drivers_joypad/udev_joypad.c"
#endif

#include "../input/drivers/nullinput.c"
#include "../input/drivers_joypad/null_joypad.c"

/*============================================================
INPUT (HID)
============================================================ */
#include "../input/drivers_joypad/hid_joypad.c"

#include "../input/drivers_hid/null_hid.c"

#if defined(HAVE_LIBUSB)
#include "../input/drivers_hid/libusb_hid.c"
#endif

#if defined(__APPLE__) && defined(IOS)
#include "../input/drivers_hid/btstack_hid.c"
#elif defined(__APPLE__) && defined(HAVE_IOHIDMANAGER)
#include "../input/drivers_hid/iohidmanager_hid.c"
#endif

#ifdef HAVE_HID
#include "../input/connect/joypad_connection.c"
#include "../input/connect/connect_ps3.c"
#include "../input/connect/connect_ps4.c"
#include "../input/connect/connect_wii.c"
#endif

/*============================================================
 KEYBOARD EVENT
 ============================================================ */

#if defined(_WIN32) && !defined(_XBOX)
#include "../input/drivers_keyboard/keyboard_event_win32.c"
#endif

#ifdef HAVE_X11
#include "../input/drivers_keyboard/keyboard_event_x11.c"
#endif

#ifdef __APPLE__
#include "../input/drivers_keyboard/keyboard_event_apple.c"
#endif

#ifdef HAVE_XKBCOMMON
#include "../input/drivers_keyboard/keyboard_event_xkb.c"
#endif

/*============================================================
STATE TRACKER
============================================================ */
#include "../gfx/video_state_tracker.c"

#ifdef HAVE_PYTHON
#include "../gfx/video_state_python.c"
#endif

/*============================================================
FIFO BUFFER
============================================================ */
#include "../libretro-common/queues/fifo_buffer.c"

/*============================================================
AUDIO RESAMPLER
============================================================ */
#include "../audio/audio_resampler_driver.c"
#include "../audio/drivers_resampler/sinc.c"
#include "../audio/drivers_resampler/nearest.c"
#include "../audio/drivers_resampler/cc_resampler.c"

/*============================================================
CAMERA
============================================================ */
#if defined(ANDROID)
#include "../camera/drivers/android.c"
#elif defined(EMSCRIPTEN)
#include "../camera/drivers/rwebcam.c"
#endif

#ifdef HAVE_V4L2
#include "../camera/drivers/video4linux2.c"
#endif

#include "../camera/drivers/nullcamera.c"

/*============================================================
LOCATION
============================================================ */
#if defined(ANDROID)
#include "../location/drivers/android.c"
#endif

#include "../location/drivers/nulllocation.c"

/*============================================================
RSOUND
============================================================ */
#ifdef HAVE_RSOUND
#include "../audio/librsound.c"
#include "../audio/drivers/rsound.c"
#endif

/*============================================================
AUDIO
============================================================ */
#if defined(__CELLOS_LV2__)
#include "../audio/drivers/ps3_audio.c"
#elif defined(XENON)
#include "../audio/drivers/xenon360_audio.c"
#elif defined(GEKKO)
#include "../audio/drivers/gx_audio.c"
#elif defined(EMSCRIPTEN)
#include "../audio/drivers/rwebaudio.c"
#elif defined(PSP)
#include "../audio/drivers/psp1_audio.c"
#elif defined(_3DS)
#include "../audio/drivers/ctr_audio.c"
#endif

#ifdef HAVE_DSOUND
#include "../audio/drivers/dsound.c"
#endif

#ifdef HAVE_SL
#include "../audio/drivers/opensl.c"
#endif

#ifdef HAVE_ALSA
#ifdef __QNX__
#include "../audio/drivers/alsa_qsa.c"
#else
#include "../audio/drivers/alsa.c"
#include "../audio/drivers/alsathread.c"
#endif
#endif

#ifdef HAVE_AL
#include "../audio/drivers/openal.c"
#endif

#ifdef HAVE_COREAUDIO
#include "../audio/drivers/coreaudio.c"
#endif

#include "../audio/drivers/nullaudio.c"

/*============================================================
DRIVERS
============================================================ */
#include "../gfx/video_driver.c"
#include "../gfx/video_pixel_converter.c"
#include "../gfx/video_viewport.c"
#include "../input/input_driver.c"
#include "../audio/audio_driver.c"
#include "../camera/camera_driver.c"
#include "../location/location_driver.c"
#include "../menu/menu_driver.c"
#include "../driver.c"

/*============================================================
SCALERS
============================================================ */
#include "../libretro-common/gfx/scaler/scaler_filter.c"
#include "../libretro-common/gfx/scaler/pixconv.c"
#include "../libretro-common/gfx/scaler/scaler.c"
#include "../libretro-common/gfx/scaler/scaler_int.c"

/*============================================================
FILTERS
============================================================ */

#ifdef HAVE_FILTERS_BUILTIN
#include "../gfx/video_filters/2xsai.c"
#include "../gfx/video_filters/super2xsai.c"
#include "../gfx/video_filters/supereagle.c"
#include "../gfx/video_filters/2xbr.c"
#include "../gfx/video_filters/darken.c"
#include "../gfx/video_filters/epx.c"
#include "../gfx/video_filters/scale2x.c"
#include "../gfx/video_filters/blargg_ntsc_snes.c"
#include "../gfx/video_filters/lq2x.c"
#include "../gfx/video_filters/phosphor2x.c"

#include "../audio/audio_filters/echo.c"
#include "../audio/audio_filters/eq.c"
#include "../audio/audio_filters/chorus.c"
#include "../audio/audio_filters/iir.c"
#include "../audio/audio_filters/panning.c"
#include "../audio/audio_filters/phaser.c"
#include "../audio/audio_filters/reverb.c"
#include "../audio/audio_filters/wahwah.c"
#endif
/*============================================================
DYNAMIC
============================================================ */
#include "../libretro-common/dynamic/dylib.c"
#include "../dynamic.c"
#include "../gfx/video_filter.c"
#include "../audio/audio_dsp_filter.c"

/*============================================================
CORES
============================================================ */
#ifdef HAVE_FFMPEG
#include "../cores/ffmpeg_core.c"
#endif

#include "../cores/dynamic_dummy.c"

/*============================================================
FILE
============================================================ */
#include "../content.c"
#include "../libretro-common/file/file_path.c"
#include "../file_path_special.c"
#include "../libretro-common/file/dir_list.c"
#include "../dir_list_special.c"
#include "../libretro-common/string/string_list.c"
#include "../libretro-common/string/stdstring.c"
#include "../file_ops.c"
#include "../libretro-common/file/nbio/nbio_stdio.c"
#include "../libretro-common/file/file_list.c"

/*============================================================
MESSAGE
============================================================ */
#include "../libretro-common/queues/message_queue.c"

/*============================================================
PATCH
============================================================ */
#include "../patch.c"

/*============================================================
CONFIGURATION
============================================================ */
#include "../configuration.c"

/*============================================================
REWIND
============================================================ */
#include "../rewind.c"

/*============================================================
FRONTEND
============================================================ */

#include "../frontend/frontend_driver.c"

#if defined(_WIN32) && !defined(_XBOX)
#include "../frontend/drivers/platform_win32.c"
#endif
#if defined(__CELLOS_LV2__)
#include "../frontend/drivers/platform_ps3.c"
#elif defined(GEKKO)
#include "../frontend/drivers/platform_gx.c"
#ifdef HW_RVL
#include "../frontend/drivers/platform_wii.c"
#endif
#elif defined(_XBOX)
#include "../frontend/drivers/platform_xdk.c"
#elif defined(PSP)
#include "../frontend/drivers/platform_psp.c"
#elif defined(_3DS)
#include "../frontend/drivers/platform_ctr.c"
#elif defined(__QNX__)
#include "../frontend/drivers/platform_qnx.c"
#elif defined(ANDROID)
#include "../frontend/drivers/platform_android.c"
#elif defined(__linux__) && !defined(ANDROID)
#include "../frontend/drivers/platform_linux.c"
#endif
#include "../frontend/drivers/platform_null.c"

#include "../core_info.c"

/*============================================================
UI
============================================================ */
#include "../ui/ui_companion_driver.c"

#include "../ui/drivers/ui_null.c"

#ifdef HAVE_QT
#include "../ui/drivers/ui_qt.c"
#endif

/*============================================================
MAIN
============================================================ */
#if defined(XENON)
#include "../frontend/frontend_xenon.c"
#else
#include "../frontend/frontend.c"
#endif

/*============================================================
GIT
============================================================ */

#ifdef HAVE_GIT_VERSION
#include "../git_version.c"
#endif


/*============================================================
RETROARCH
============================================================ */
#include "../libretro_version_1.c"
#include "../retroarch.c"
#include "../retroarch_info.c"
#include "../runloop.c"
#include "../runloop_data.c"
#include "../runloop_msg.c"
#include "../system.c"

#include "../msg_hash.c"
#include "../intl/msg_hash_de.c"
#include "../intl/msg_hash_es.c"
#include "../intl/msg_hash_eo.c"
#include "../intl/msg_hash_fr.c"
#include "../intl/msg_hash_it.c"
#include "../intl/msg_hash_nl.c"
#include "../intl/msg_hash_pt.c"
#include "../intl/msg_hash_us.c"


/*============================================================
RECORDING
============================================================ */
#include "../movie.c"
#include "../record/record_driver.c"
#include "../record/drivers/record_null.c"

#ifdef HAVE_FFMPEG
#include "../record/drivers/record_ffmpeg.c"
#endif

/*============================================================
THREAD
============================================================ */
#if defined(HAVE_THREADS) && defined(XENON)
#include "../thread/xenon_sdl_threads.c"
#elif defined(HAVE_THREADS)
#include "../libretro-common/rthreads/rthreads.c"
#include "../gfx/video_thread_wrapper.c"
#include "../audio/audio_thread_wrapper.c"
#include "../autosave.c"
#endif


/*============================================================
NETPLAY
============================================================ */
#ifdef HAVE_NETPLAY
#include "../netplay.c"
#include "../libretro-common/net/net_compat.c"
#include "../libretro-common/net/net_http.c"
#include "../tasks/task_http.c"
#endif

/*============================================================
DATA RUNLOOP
============================================================ */
#include "../tasks/task_file_transfer.c"
#ifdef HAVE_LIBRETRODB
#include "../tasks/task_database.c"
#endif

/*============================================================
SCREENSHOTS
============================================================ */
#include "../screenshot.c"

/*============================================================
PLAYLISTS
============================================================ */
#include "../playlist.c"

/*============================================================
MENU
============================================================ */
#ifdef HAVE_MENU
#include "../menu/menu.c"
#include "../menu/menu_hash.c"
#include "../menu/menu_input.c"
#include "../menu/menu_entry.c"
#include "../menu/menu_entries.c"
#include "../menu/menu_setting.c"
#include "../menu/menu_list.c"
#include "../menu/menu_cbs.c"
#include "../menu/cbs/menu_cbs_ok.c"
#include "../menu/cbs/menu_cbs_cancel.c"
#include "../menu/cbs/menu_cbs_select.c"
#include "../menu/cbs/menu_cbs_start.c"
#include "../menu/cbs/menu_cbs_info.c"
#include "../menu/cbs/menu_cbs_refresh.c"
#include "../menu/cbs/menu_cbs_left.c"
#include "../menu/cbs/menu_cbs_right.c"
#include "../menu/cbs/menu_cbs_title.c"
#include "../menu/cbs/menu_cbs_deferred_push.c"
#include "../menu/cbs/menu_cbs_scan.c"
#include "../menu/cbs/menu_cbs_get_value.c"
#include "../menu/cbs/menu_cbs_iterate.c"
#include "../menu/cbs/menu_cbs_up.c"
#include "../menu/cbs/menu_cbs_down.c"
#include "../menu/cbs/menu_cbs_contentlist_switch.c"
#include "../menu/menu_shader.c"
#include "../menu/menu_navigation.c"
#include "../menu/menu_display.c"
#include "../menu/menu_displaylist.c"
#include "../menu/menu_animation.c"

#include "../menu/intl/menu_hash_de.c"
#include "../menu/intl/menu_hash_es.c"
#include "../menu/intl/menu_hash_eo.c"
#include "../menu/intl/menu_hash_fr.c"
#include "../menu/intl/menu_hash_it.c"
#include "../menu/intl/menu_hash_nl.c"
#include "../menu/intl/menu_hash_pt.c"
#include "../menu/intl/menu_hash_us.c"

#include "../menu/drivers/null.c"
#endif


#ifdef HAVE_RMENU
#include "../menu/drivers/rmenu.c"
#endif

#ifdef HAVE_RGUI
#include "../menu/drivers/rgui.c"
#endif

#ifdef HAVE_OPENGL

#ifdef HAVE_XMB
#include "../menu/drivers/xmb.c"
#endif

#ifdef HAVE_GLUI
#include "../menu/drivers/glui.c"
#endif

#endif

#ifdef HAVE_COMMAND
#include "../command.c"
#endif

#include "../command_event.c"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================
DEPENDENCIES
============================================================ */
#ifdef WANT_ZLIB
#include "../deps/zlib/adler32.c"
#include "../deps/zlib/compress.c"
#include "../deps/zlib/crc32.c"
#include "../deps/zlib/deflate.c"
#include "../deps/zlib/gzclose.c"
#include "../deps/zlib/gzlib.c"
#include "../deps/zlib/gzread.c"
#include "../deps/zlib/gzwrite.c"
#include "../deps/zlib/inffast.c"
#include "../deps/zlib/inflate.c"
#include "../deps/zlib/inftrees.c"
#include "../deps/zlib/trees.c"
#include "../deps/zlib/uncompr.c"
#include "../deps/zlib/zutil.c"
#endif

/* Decompression support always requires the next two files */
#if defined(WANT_ZLIB) || defined(HAVE_ZLIB)
#include "../deps/zlib/ioapi.c"
#include "../deps/zlib/unzip.c"
#endif

#ifdef HAVE_7ZIP
#include "../deps/7zip/7zIn.c"
#include "../deps/7zip/7zAlloc.c"
#include "../deps/7zip/Bra86.c"
#include "../deps/7zip/CpuArch.c"
#include "../deps/7zip/7zFile.c"
#include "../deps/7zip/7zStream.c"
#include "../deps/7zip/7zBuf2.c"
#include "../deps/7zip/LzmaDec.c"
#include "../deps/7zip/7zCrcOpt.c"
#include "../deps/7zip/Bra.c"
#include "../deps/7zip/7zDec.c"
#include "../deps/7zip/Bcj2.c"
#include "../deps/7zip/7zCrc.c"
#include "../deps/7zip/Lzma2Dec.c"
#include "../deps/7zip/7zBuf.c"
#include "../decompress/7zip_support.c"
#endif

/*============================================================
XML
============================================================ */
#if 0
#ifndef HAVE_LIBXML2
#define RXML_LIBXML2_COMPAT
#include "../libretro-common/formats/xml/rxml.c"
#endif
#endif

/*============================================================
 AUDIO UTILS
============================================================ */
#include "../audio/audio_utils.c"

/*============================================================
 LIBRETRODB
============================================================ */
#ifdef HAVE_LIBRETRODB
#include "../libretro-db/bintree.c"
#include "../libretro-db/libretrodb.c"
#include "../libretro-db/rmsgpack.c"
#include "../libretro-db/rmsgpack_dom.c"
#include "../libretro-db/query.c"
#include "../database_info.c"
#endif


#ifdef __cplusplus
}
#endif
