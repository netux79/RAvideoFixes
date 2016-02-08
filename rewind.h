/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 *  Copyright (C) 2014-2015 - Alfred Agrell
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

#ifndef __RARCH_REWIND_H
#define __RARCH_REWIND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <boolean.h>
#include <stdint.h>

typedef struct state_manager state_manager_t;

state_manager_t *state_manager_new(size_t state_size, size_t buffer_size);

void state_manager_free(state_manager_t *state);

bool state_manager_pop(state_manager_t *state, const void **data);

void state_manager_push_where(state_manager_t *state, void **data);

void state_manager_push_do(state_manager_t *state);

void state_manager_capacity(state_manager_t *state,
      unsigned int *entries, size_t *bytes, bool *full);

void init_rewind(void);


/* Returns the maximum compressed size of a savestate. It is very likely to compress to far less. */
size_t state_manager_raw_maxsize(size_t uncomp);

/*
 * See state_manager_raw_compress for information about this.
 * When you're done with it, send it to free().
 */
void *state_manager_raw_alloc(size_t len, uint16_t uniq);

/*
 * Takes two savestates and creates a patch that turns 'src' into 'dst'.
 * Both 'src' and 'dst' must be returned from state_manager_raw_alloc(), with the same 'len', and different 'uniq'.
 * 'patch' must be size 'state_manager_raw_maxsize(len)' or more.
 * Returns the number of bytes actually written to 'patch'.
 */
size_t state_manager_raw_compress(const void *src, const void *dst, size_t len, void *patch);

/*
 * Takes 'patch' from a previous call to 'state_manager_raw_compress' and applies it to 'data' ('src' from that call),
 * yielding 'dst' in that call.
 * If the given arguments do not match a previous call to state_manager_raw_compress(), anything at all can happen.
 */
void state_manager_raw_decompress(const void *patch, size_t patchlen, void *data, size_t datalen);

#ifdef __cplusplus
}
#endif

#endif
