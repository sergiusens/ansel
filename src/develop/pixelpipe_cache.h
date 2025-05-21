/*
    This file is part of darktable,
    Copyright (C) 2009-2020 darktable developers.
    Copyright (C) 2022-2025 Aurélien PIERRE.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <inttypes.h>

struct dt_dev_pixelpipe_t;
struct dt_iop_buffer_dsc_t;
struct dt_iop_roi_t;


/**
 * @file pixelpipe_cache.h
 * @brief Pixelpipe cache for storing intermediate results in the pixelpipe.
 *
 * This cache can be used locally (in the pixelpipe) or globally (in the whole app).
 * Current implementation is global, using `darktable.pipeline_threadsafe` mutex lock
 * to protect cache entries addition/removal accross threads. The mutex lock
 * protects the whole recursive pixelpipe, so no internal locking is needed nor implemented here.
 */

typedef struct dt_dev_pixelpipe_cache_t
{
  GHashTable *entries;
  uint64_t queries;
  uint64_t hits;
  size_t max_memory;
  size_t current_memory;
} dt_dev_pixelpipe_cache_t;

/** constructs a new cache with given cache line count (entries) and float buffer entry size in bytes.
  \param[out] returns 0 if fail to allocate mem cache.
*/
dt_dev_pixelpipe_cache_t * dt_dev_pixelpipe_cache_init(size_t max_memory);
void dt_dev_pixelpipe_cache_cleanup(dt_dev_pixelpipe_cache_t *cache);

/**
 * @brief Get a cache line from the cache.
 *
 * @param cache
 * @param hash State checksum of the cache line.
 * @param size Buffer size in bytes.
 * @param name Name of the cache line (for debugging).
 * @param data Pointer to the buffer pointer (returned).
 * @param dsc Pointer to the buffer descriptor (returned).
 * @return int 1 if the cache line was freshly allocated, 0 if it was found in the cache.
 */
int dt_dev_pixelpipe_cache_get(dt_dev_pixelpipe_cache_t *cache, const uint64_t hash,
                               const size_t size, const char *name, void **data, struct dt_iop_buffer_dsc_t **dsc);

/** test availability of a cache line without destroying another, if it is not found. */
int dt_dev_pixelpipe_cache_available(dt_dev_pixelpipe_cache_t *cache, const uint64_t hash);

/** invalidates all cachelines. */
void dt_dev_pixelpipe_cache_flush(dt_dev_pixelpipe_cache_t *cache);

/** mark the given cache line pointer as invalid. */
void dt_dev_pixelpipe_cache_invalidate(dt_dev_pixelpipe_cache_t *cache, void *data);

/** print out cache lines/hashes (debug). */
void dt_dev_pixelpipe_cache_print(dt_dev_pixelpipe_cache_t *cache);

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
