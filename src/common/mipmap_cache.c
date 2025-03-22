/*
    This file is part of darktable,
    Copyright (C) 2011-2021 darktable developers.

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

#include "common/mipmap_cache.h"
#include "common/darktable.h"
#include "common/debug.h"
#include "common/exif.h"
#include "common/file_location.h"
#include "common/grealpath.h"
#include "common/image_cache.h"
#include "common/imageio.h"
#include "common/imageio_jpeg.h"
#include "common/imageio_module.h"
#include "control/conf.h"
#include "control/jobs.h"
#include "develop/imageop_math.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

#if !defined(_WIN32)
#include <sys/statvfs.h>
#else
//statvfs does not exist in Windows, providing implementation
#include "win/statvfs.h"
#endif

#define DT_MIPMAP_CACHE_FILE_MAGIC 0xD71337
#define DT_MIPMAP_CACHE_FILE_VERSION 23
#define DT_MIPMAP_CACHE_DEFAULT_FILE_NAME "mipmaps"

typedef enum dt_mipmap_buffer_dsc_flags
{
  DT_MIPMAP_BUFFER_DSC_FLAG_NONE = 0,
  DT_MIPMAP_BUFFER_DSC_FLAG_GENERATE = 1 << 0,
  DT_MIPMAP_BUFFER_DSC_FLAG_INVALIDATE = 1 << 1
} dt_mipmap_buffer_dsc_flags;

// the embedded Exif data to tag thumbnails as sRGB or AdobeRGB
static const uint8_t dt_mipmap_cache_exif_data_srgb[] = {
  0x45, 0x78, 0x69, 0x66, 0x00, 0x00, 0x49, 0x49, 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x69,
  0x87, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x01, 0xa0, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t dt_mipmap_cache_exif_data_adobergb[] = {
  0x45, 0x78, 0x69, 0x66, 0x00, 0x00, 0x49, 0x49, 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x69,
  0x87, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
  0x01, 0xa0, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const int dt_mipmap_cache_exif_data_srgb_length
                      = sizeof(dt_mipmap_cache_exif_data_srgb) / sizeof(*dt_mipmap_cache_exif_data_srgb);
static const int dt_mipmap_cache_exif_data_adobergb_length
                      = sizeof(dt_mipmap_cache_exif_data_adobergb) / sizeof(*dt_mipmap_cache_exif_data_adobergb);

struct dt_mipmap_buffer_dsc
{
  uint32_t width;
  uint32_t height;
  float iscale;
  size_t size;
  dt_mipmap_buffer_dsc_flags flags;
  dt_colorspaces_color_profile_type_t color_space;

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
  // do not touch!
  // must be the last element.
  // must be no less than 16bytes
  char redzone[16];
#endif

  /* NB: sizeof must be a multiple of 4*sizeof(float) */
} __attribute__((packed, aligned(DT_CACHELINE_BYTES)));

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
static const size_t dt_mipmap_buffer_dsc_size __attribute__((unused))
= sizeof(struct dt_mipmap_buffer_dsc) - sizeof(((struct dt_mipmap_buffer_dsc *)0)->redzone);
#else
static const size_t dt_mipmap_buffer_dsc_size __attribute__((unused)) = sizeof(struct dt_mipmap_buffer_dsc);
#endif

static uint8_t * _shift_buffer(struct dt_mipmap_buffer_dsc *dsc);
static void _realloc_shifted_buffer(struct dt_mipmap_buffer_dsc *dsc);

int dt_mipmap_ready_idle_signal(gpointer data)
{
  DT_DEBUG_CONTROL_SIGNAL_RAISE(darktable.signals, DT_SIGNAL_DEVELOP_MIPMAP_UPDATED, GPOINTER_TO_INT(data));
  return G_SOURCE_REMOVE;
}

// last resort mem alloc for dead images. sizeof(dt_mipmap_buffer_dsc) + dead image pixels (8x8)
// Must be alignment to 4 * sizeof(float).
static float dt_mipmap_cache_static_dead_image[sizeof(struct dt_mipmap_buffer_dsc) / sizeof(float) + 64 * 4]
    __attribute__((aligned(DT_CACHELINE_BYTES)));

static inline void dead_image_8(dt_mipmap_buffer_t *buf)
{
  if(!buf->buf) return;
  struct dt_mipmap_buffer_dsc *dsc = (struct dt_mipmap_buffer_dsc *)buf->buf - 1;
  dsc->width = dsc->height = 8;
  dsc->iscale = 1.0f;
  dsc->color_space = DT_COLORSPACE_DISPLAY;
  assert(dsc->size > 64 * sizeof(uint32_t));
  const uint32_t X = 0xffffffffu;
  const uint32_t o = 0u;
  const uint32_t image[]
      = { o, o, o, o, o, o, o, o, o, o, X, X, X, X, o, o, o, X, o, X, X, o, X, o, o, X, X, X, X, X, X, o,
          o, o, X, o, o, X, o, o, o, o, o, o, o, o, o, o, o, o, X, X, X, X, o, o, o, o, o, o, o, o, o, o };
  memcpy(buf->buf, image, sizeof(uint32_t) * 64);
}

static inline void dead_image_f(dt_mipmap_buffer_t *buf)
{
  if(!buf->buf) return;
  struct dt_mipmap_buffer_dsc *dsc = (struct dt_mipmap_buffer_dsc *)buf->buf - 1;
  dsc->width = dsc->height = 8;
  dsc->iscale = 1.0f;
  dsc->color_space = DT_COLORSPACE_DISPLAY;
  assert(dsc->size > 64 * 4 * sizeof(float));

  if(darktable.codepath.OPENMP_SIMD)
  {
    const float X = 1.0f;
    const float o = 0.0f;

    const float image[64 * 4]
        = { o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o,
            o, o, o, o, o, o, o, o, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, o, o, o, o, o, o, o, o,
            o, o, o, o, X, X, X, X, o, o, o, o, X, X, X, X, X, X, X, X, o, o, o, o, X, X, X, X, o, o, o, o,
            o, o, o, o, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, o, o, o, o,
            o, o, o, o, o, o, o, o, X, X, X, X, o, o, o, o, o, o, o, o, X, X, X, X, o, o, o, o, o, o, o, o,
            o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o,
            o, o, o, o, o, o, o, o, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, o, o, o, o, o, o, o, o,
            o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o, o };

    memcpy(buf->buf, image, sizeof(float) * 4 * 64);
  }
#if defined(__SSE__)
  else if(darktable.codepath.SSE2)
  {
    const __m128 X = _mm_set1_ps(1.0f);
    const __m128 o = _mm_set1_ps(0.0f);
    const __m128 image[]
        = { o, o, o, o, o, o, o, o, o, o, X, X, X, X, o, o, o, X, o, X, X, o, X, o, o, X, X, X, X, X, X, o,
            o, o, X, o, o, X, o, o, o, o, o, o, o, o, o, o, o, o, X, X, X, X, o, o, o, o, o, o, o, o, o, o };

    memcpy(buf->buf, image, sizeof(__m128) * 64);
  }
#endif
  else
    dt_unreachable_codepath();
}

#ifndef NDEBUG
static inline int32_t buffer_is_broken(dt_mipmap_buffer_t *buf)
{
  if(!buf->buf) return 0;
  struct dt_mipmap_buffer_dsc *dsc = (struct dt_mipmap_buffer_dsc *)buf->buf - 1;
  if(buf->width != dsc->width) return 1;
  if(buf->height != dsc->height) return 2;
  // somewhat loose bound:
  if(buf->width * buf->height > dsc->size) return 3;
  return 0;
}
#endif

static inline int32_t get_key(const int32_t imgid, const dt_mipmap_size_t size)
{
  // imgid can't be >= 2^28 (~250 million images)
  return (((int32_t)size) << 28) | (imgid - 1);
}

static inline uint32_t get_imgid(const uint32_t key)
{
  return (key & 0xfffffff) + 1;
}

static inline dt_mipmap_size_t get_size(const uint32_t key)
{
  return (dt_mipmap_size_t)(key >> 28);
}

static int dt_mipmap_cache_get_filename(gchar *mipmapfilename, size_t size)
{
  int r = -1;
  char *abspath = NULL;

  // Directory
  char cachedir[PATH_MAX] = { 0 };
  dt_loc_get_user_cache_dir(cachedir, sizeof(cachedir));

  // Build the mipmap filename fram hashing the path of the library DB
  const gchar *dbfilename = dt_database_get_path(darktable.db);

  if(!strcmp(dbfilename, ":memory:"))
  {
    mipmapfilename[0] = '\0';
    r = 0;
    goto exit;
  }

  abspath = g_realpath(dbfilename);
  if(!abspath) abspath = g_strdup(dbfilename);

  GChecksum *chk = g_checksum_new(G_CHECKSUM_SHA1);
  g_checksum_update(chk, (guchar *)abspath, strlen(abspath));
  const gchar *filename = g_checksum_get_string(chk);

  if(!filename || filename[0] == '\0')
    snprintf(mipmapfilename, size, "%s/%s", cachedir, DT_MIPMAP_CACHE_DEFAULT_FILE_NAME);
  else
    snprintf(mipmapfilename, size, "%s/%s-%s", cachedir, DT_MIPMAP_CACHE_DEFAULT_FILE_NAME, filename);

  g_checksum_free(chk);
  r = 0;

exit:
  g_free(abspath);

  return r;
}

static void _init_f(dt_mipmap_buffer_t *mipmap_buf, float *buf, uint32_t *width, uint32_t *height, float *iscale,
                    const int32_t imgid);
static void _init_8(uint8_t *buf, uint32_t *width, uint32_t *height, float *iscale,
                    dt_colorspaces_color_profile_type_t *color_space, const int32_t imgid,
                    const dt_mipmap_size_t size);

// callback for the imageio core to allocate memory.
// only needed for _F and _FULL buffers, as they change size
// with the input image. will allocate img->width*img->height*img->bpp bytes.
void *dt_mipmap_cache_alloc(dt_mipmap_buffer_t *buf, const dt_image_t *img)
{
  assert(buf->size > DT_MIPMAP_F && buf->size < DT_MIPMAP_NONE);

  dt_cache_entry_t *entry = buf->cache_entry;
  struct dt_mipmap_buffer_dsc *dsc = (struct dt_mipmap_buffer_dsc *)entry->data;

  const int wd = img->width;
  const int ht = img->height;

  const size_t bpp = dt_iop_buffer_dsc_to_bpp(&img->buf_dsc);
  const size_t buffer_size = (size_t)wd * ht * bpp + sizeof(*dsc);

  // buf might have been alloc'ed before,
  // so only check size and re-alloc if necessary:
  if(!buf->buf || ((void *)dsc == (void *)dt_mipmap_cache_static_dead_image) || (entry->data_size < buffer_size))
  {
    if((void *)dsc != (void *)dt_mipmap_cache_static_dead_image) dt_free_align(entry->data);

    entry->data_size = 0;

    entry->data = dt_alloc_align(buffer_size);

    if(!entry->data)
    {
      // return fallback: at least alloc size for a dead image:
      entry->data = (void *)dt_mipmap_cache_static_dead_image;

      // allocator holds the pointer. but let imageio client know that allocation failed:
      return NULL;
    }

    entry->data_size = buffer_size;

    // set buffer size only if we're making it larger.
    dsc = (struct dt_mipmap_buffer_dsc *)entry->data;
  }

  dsc->size = buffer_size;

  dsc->width = wd;
  dsc->height = ht;
  dsc->iscale = 1.0f;
  dsc->color_space = DT_COLORSPACE_NONE;
  dsc->flags = DT_MIPMAP_BUFFER_DSC_FLAG_GENERATE;
  buf->buf = _shift_buffer(dsc);

  // fprintf(stderr, "full buffer allocating img %u %d x %d = %u bytes (%p)\n", img->id, img->width,
  // img->height, buffer_size, *buf);

  assert(entry->data_size);
  assert(dsc->size);
  assert(dsc->size <= entry->data_size);

  // Free the buffer
  ASAN_POISON_MEMORY_REGION(entry->data, entry->data_size);
  _realloc_shifted_buffer(dsc);

  // return pointer to start of payload
  return _shift_buffer(dsc);
}

// callback for the cache backend to initialize payload pointers
void dt_mipmap_cache_allocate_dynamic(void *data, dt_cache_entry_t *entry)
{
  dt_mipmap_cache_t *cache = (dt_mipmap_cache_t *)data;
  // for full image buffers
  struct dt_mipmap_buffer_dsc *dsc = entry->data;
  const dt_mipmap_size_t mip = get_size(entry->key);

  assert(mip < DT_MIPMAP_NONE);

  // alloc mere minimum for the header + broken image buffer:
  if(!dsc)
  {
    // these are fixed-size:
    if(mip <= DT_MIPMAP_F)
      entry->data_size = cache->buffer_size[mip];
    else
      entry->data_size = sizeof(*dsc) + sizeof(float) * 4 * 64;

    entry->data = dt_alloc_align(entry->data_size);

    // fprintf(stderr, "[mipmap cache] alloc dynamic for key %u %p\n", key, *buf);
    if(!(entry->data))
    {
      fprintf(stderr, "[mipmap cache] memory allocation failed!\n");
      exit(1);
    }

    dsc = entry->data;

    if(mip <= DT_MIPMAP_F)
    {
      dsc->width = cache->max_width[mip];
      dsc->height = cache->max_height[mip];
      dsc->iscale = 1.0f;
      dsc->size = entry->data_size;
      dsc->color_space = DT_COLORSPACE_NONE;
    }
    else
    {
      dsc->width = 0;
      dsc->height = 0;
      dsc->iscale = 0.0f;
      dsc->color_space = DT_COLORSPACE_NONE;
      dsc->size = entry->data_size;
    }
  }

  assert(dsc->size >= sizeof(*dsc));

  dsc->flags = DT_MIPMAP_BUFFER_DSC_FLAG_GENERATE;

  if(cache->cachedir[0] && dt_conf_get_bool("cache_disk_backend") && mip < DT_MIPMAP_F)
  {
    // try and load from disk, if successful set flag
    char filename[PATH_MAX] = {0};
    snprintf(filename, sizeof(filename), "%s.d/%d/%" PRIu32 ".jpg", cache->cachedir, (int)mip,
             get_imgid(entry->key));

    gboolean io_error = FALSE;
    gchar *error = NULL;
    uint8_t *blob = NULL;
    dt_colorspaces_color_profile_type_t color_space = DT_COLORSPACE_DISPLAY;
    dt_imageio_jpeg_t jpg;

    FILE *f = g_fopen(filename, "rb");
    if(f == NULL) goto finish; // file doesn't exist

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if(len <= 0)
    {
      error = "empty file";
      io_error = TRUE;
      goto finish;
    }

    blob = (uint8_t *)dt_alloc_align(len);
    if(blob == NULL)
    {
      error = "out of memory";
      io_error = TRUE;
      goto finish;
    }

    fseek(f, 0, SEEK_SET);
    const size_t rd = fread(blob, sizeof(uint8_t), len, f);
    if(rd != len)
    {
      error = "corrupted file";
      io_error = TRUE;
      goto finish;
    }

    if(dt_imageio_jpeg_decompress_header(blob, len, &jpg))
    {
      error = "couldn't decompress header";
      io_error = TRUE;
      goto finish;
    }

    // Tolerate a 2 px error on the pictures dimensions for rounding errors.
    // that will still discard the cache all the time for input images (JPG) smaller than required thumbnail
    // dimension. FIXME ?
    if(!(jpg.width >= cache->max_width[mip] - 2 || jpg.height >= cache->max_height[mip] - 2))
    {
      error = "bad size";
      io_error = TRUE;
      goto finish;
    }

    color_space = dt_imageio_jpeg_read_color_space(&jpg);

    if(dt_imageio_jpeg_decompress(&jpg, (uint8_t *)entry->data + sizeof(*dsc)))
    {
      error = "couldn't decompress JPEG";
      io_error = TRUE;
      goto finish;
    }

    dt_print(DT_DEBUG_CACHE, "[mipmap_cache] grab mip %d for image %" PRIu32 " (%ix%i) from disk cache\n", mip,
              get_imgid(entry->key), jpg.width, jpg.height);
    dsc->width = jpg.width;
    dsc->height = jpg.height;
    dsc->iscale = 1.0f;
    dsc->color_space = color_space;
    dsc->flags = 0;

finish:
    if(f && io_error)
    {
      // Delete the file, we will regenerate it
      g_unlink(filename);
      fprintf(stderr, "[mipmap_cache] failed to open thumbnail for image %" PRIu32 " from `%s'. Reason: %s\n",
              get_imgid(entry->key), filename, error);
    }

    if(blob) dt_free_align(blob);
    if(f) fclose(f);
  }

  // cost is just flat one for the buffer, as the buffers might have different sizes,
  // to make sure quota is meaningful.
  if(mip >= DT_MIPMAP_F)
    entry->cost = 1;
  else
    entry->cost = cache->buffer_size[mip];
}

static void dt_mipmap_cache_unlink_ondisk_thumbnail(void *data, int32_t imgid, dt_mipmap_size_t mip)
{
  dt_mipmap_cache_t *cache = (dt_mipmap_cache_t *)data;

  // also remove jpg backing (always try to do that, in case user just temporarily switched it off,
  // to avoid inconsistencies.
  // if(dt_conf_get_bool("cache_disk_backend"))
  if(cache->cachedir[0])
  {
    char filename[PATH_MAX] = { 0 };
    snprintf(filename, sizeof(filename), "%s.d/%d/%"PRIu32".jpg", cache->cachedir, (int)mip, imgid);
    g_unlink(filename);
  }
}

void dt_mipmap_cache_deallocate_dynamic(void *data, dt_cache_entry_t *entry)
{
  dt_mipmap_cache_t *cache = (dt_mipmap_cache_t *)data;
  const dt_mipmap_size_t mip = get_size(entry->key);
  if(mip < DT_MIPMAP_F)
  {
    struct dt_mipmap_buffer_dsc *dsc = (struct dt_mipmap_buffer_dsc *)entry->data;
    // don't write skulls:
    if(dsc->width > 8 && dsc->height > 8)
    {
      if(dsc->flags & DT_MIPMAP_BUFFER_DSC_FLAG_INVALIDATE)
      {
        dt_mipmap_cache_unlink_ondisk_thumbnail(data, get_imgid(entry->key), mip);
      }
      else if(cache->cachedir[0] && (dt_conf_get_bool("cache_disk_backend") && mip < DT_MIPMAP_F))
      {
        // serialize to disk
        char filename[PATH_MAX] = {0};
        snprintf(filename, sizeof(filename), "%s.d/%d", cache->cachedir, mip);
        const int mkd = g_mkdir_with_parents(filename, 0750);
        if(!mkd)
        {
          snprintf(filename, sizeof(filename), "%s.d/%d/%" PRIu32 ".jpg", cache->cachedir, (int)mip,
                   get_imgid(entry->key));
          // Don't write existing files as both performance and quality (lossy jpg) suffer
          FILE *f = NULL;
          if (!g_file_test(filename, G_FILE_TEST_EXISTS) && (f = g_fopen(filename, "wb")))
          {
            // first check the disk isn't full
            struct statvfs vfsbuf;
            if (!statvfs(filename, &vfsbuf))
            {
              const int64_t free_mb = ((vfsbuf.f_frsize * vfsbuf.f_bavail) >> 20);
              if (free_mb < 100)
              {
                fprintf(stderr, "Aborting image write as only %" PRId64 " MB free to write %s\n", free_mb, filename);
                goto write_error;
              }
            }
            else
            {
              fprintf(stderr, "Aborting image write since couldn't determine free space available to write %s\n", filename);
              goto write_error;
            }

            const int cache_quality = dt_conf_get_int("database_cache_quality");
            const uint8_t *exif = NULL;
            int exif_len = 0;
            if(dsc->color_space == DT_COLORSPACE_SRGB)
            {
              exif = dt_mipmap_cache_exif_data_srgb;
              exif_len = dt_mipmap_cache_exif_data_srgb_length;
            }
            else if(dsc->color_space == DT_COLORSPACE_ADOBERGB)
            {
              exif = dt_mipmap_cache_exif_data_adobergb;
              exif_len = dt_mipmap_cache_exif_data_adobergb_length;
            }
            if(dt_imageio_jpeg_write(filename, (uint8_t *)entry->data + sizeof(*dsc), dsc->width, dsc->height, MIN(100, MAX(10, cache_quality)), exif, exif_len))
            {
write_error:
              g_unlink(filename);
            }
          }
          if(f) fclose(f);
        }
      }
    }
  }
  dt_free_align(entry->data);
}

static uint32_t nearest_power_of_two(const uint32_t value)
{
  uint32_t rc = 1;
  while(rc < value) rc <<= 1;
  return rc;
}

void dt_mipmap_cache_init(dt_mipmap_cache_t *cache)
{
  dt_mipmap_cache_get_filename(cache->cachedir, sizeof(cache->cachedir));
  // make sure static memory is initialized
  struct dt_mipmap_buffer_dsc *dsc = (struct dt_mipmap_buffer_dsc *)dt_mipmap_cache_static_dead_image;
  dead_image_f((dt_mipmap_buffer_t *)_shift_buffer(dsc));

  // Fixed sizes for the thumbnail mip levels, selected for coverage of most screen sizes
  size_t mipsizes[DT_MIPMAP_F][2] = {
    { 180, 110 },             // mip0 - ~1/2 size previous one
    { 360, 225 },             // mip1 - 1/2 size previous one
    { 720, 450 },             // mip2 - 1/2 size previous one
    { 1440, 900 },            // mip3 - covers 720p and 1366x768
    { 1920, 1200 },           // mip4 - covers 1080p and 1600x1200
    { 2560, 1600 },           // mip5 - covers 2560x1440
    { 4096, 2560 },           // mip6 - covers 4K and UHD
    { 5120, 3200 },           // mip7 - covers 5120x2880 panels
  };
  // Set mipf to mip2 size as at most the user will be using an 8K screen and
  // have a preview that's ~4x smaller
  cache->max_width[DT_MIPMAP_F] = mipsizes[DT_MIPMAP_2][0];
  cache->max_height[DT_MIPMAP_F] = mipsizes[DT_MIPMAP_2][1];
  for(int k = DT_MIPMAP_F-1; k >= 0; k--)
  {
    cache->max_width[k]  = mipsizes[k][0];
    cache->max_height[k] = mipsizes[k][1];
  }
    // header + buffer
  for(int k = DT_MIPMAP_F-1; k >= 0; k--)
    cache->buffer_size[k] = sizeof(struct dt_mipmap_buffer_dsc)
                                + cache->max_width[k] * cache->max_height[k] * 4;

  // clear stats:
  cache->mip_thumbs.stats_requests = 0;
  cache->mip_thumbs.stats_near_match = 0;
  cache->mip_thumbs.stats_misses = 0;
  cache->mip_thumbs.stats_fetches = 0;
  cache->mip_thumbs.stats_standin = 0;
  cache->mip_f.stats_requests = 0;
  cache->mip_f.stats_near_match = 0;
  cache->mip_f.stats_misses = 0;
  cache->mip_f.stats_fetches = 0;
  cache->mip_f.stats_standin = 0;
  cache->mip_full.stats_requests = 0;
  cache->mip_full.stats_near_match = 0;
  cache->mip_full.stats_misses = 0;
  cache->mip_full.stats_fetches = 0;
  cache->mip_full.stats_standin = 0;

  dt_cache_init(&cache->mip_thumbs.cache, 0, dt_get_mipmap_mem());
  dt_cache_set_allocate_callback(&cache->mip_thumbs.cache, dt_mipmap_cache_allocate_dynamic, cache);
  dt_cache_set_cleanup_callback(&cache->mip_thumbs.cache, dt_mipmap_cache_deallocate_dynamic, cache);

  // 2 buffers (in/out) per thread.
  // We don't allow more than 2 pixel pipelines concurrent renderings, because
  // more than 1 already increases memory contention and degrades performance.
  // That's 4 buffers total.
  const int32_t max_mem_bufs = nearest_power_of_two(4);

  // for this buffer, because it can be very busy during import
  dt_cache_init(&cache->mip_full.cache, 0, max_mem_bufs);
  dt_cache_set_allocate_callback(&cache->mip_full.cache, dt_mipmap_cache_allocate_dynamic, cache);
  dt_cache_set_cleanup_callback(&cache->mip_full.cache, dt_mipmap_cache_deallocate_dynamic, cache);
  cache->buffer_size[DT_MIPMAP_FULL] = 0;

  // same for mipf:
  dt_cache_init(&cache->mip_f.cache, 0, max_mem_bufs);
  dt_cache_set_allocate_callback(&cache->mip_f.cache, dt_mipmap_cache_allocate_dynamic, cache);
  dt_cache_set_cleanup_callback(&cache->mip_f.cache, dt_mipmap_cache_deallocate_dynamic, cache);
  cache->buffer_size[DT_MIPMAP_F] = sizeof(struct dt_mipmap_buffer_dsc)
                                        + 4 * sizeof(float) * cache->max_width[DT_MIPMAP_F]
                                          * cache->max_height[DT_MIPMAP_F];
}

void dt_mipmap_cache_cleanup(dt_mipmap_cache_t *cache)
{
  dt_cache_cleanup(&cache->mip_thumbs.cache);
  dt_cache_cleanup(&cache->mip_full.cache);
  dt_cache_cleanup(&cache->mip_f.cache);
}

void dt_mipmap_cache_print(dt_mipmap_cache_t *cache)
{
  printf("[mipmap_cache] thumbs fill %.2f/%.2f MB (%.2f%%)\n",
         cache->mip_thumbs.cache.cost / (1024.0 * 1024.0),
         cache->mip_thumbs.cache.cost_quota / (1024.0 * 1024.0),
         100.0f * (float)cache->mip_thumbs.cache.cost / (float)cache->mip_thumbs.cache.cost_quota);
  printf("[mipmap_cache] float fill %"PRIu32"/%"PRIu32" slots (%.2f%%)\n",
         (uint32_t)cache->mip_f.cache.cost, (uint32_t)cache->mip_f.cache.cost_quota,
         100.0f * (float)cache->mip_f.cache.cost / (float)cache->mip_f.cache.cost_quota);
  printf("[mipmap_cache] full  fill %"PRIu32"/%"PRIu32" slots (%.2f%%)\n",
         (uint32_t)cache->mip_full.cache.cost, (uint32_t)cache->mip_full.cache.cost_quota,
         100.0f * (float)cache->mip_full.cache.cost / (float)cache->mip_full.cache.cost_quota);

  uint64_t sum = 0;
  uint64_t sum_fetches = 0;
  uint64_t sum_standins = 0;
  sum += cache->mip_thumbs.stats_requests;
  sum_fetches += cache->mip_thumbs.stats_fetches;
  sum_standins += cache->mip_thumbs.stats_standin;
  sum += cache->mip_f.stats_requests;
  sum_fetches += cache->mip_f.stats_fetches;
  sum_standins += cache->mip_f.stats_standin;
  sum += cache->mip_full.stats_requests;
  sum_fetches += cache->mip_full.stats_fetches;
  sum_standins += cache->mip_full.stats_standin;
  printf("[mipmap_cache] level | near match | miss | stand-in | fetches | total rq\n");
  printf("[mipmap_cache] thumb | %6.2f%% | %6.2f%% | %6.2f%%  | %6.2f%% | %6.2f%%\n",
         100.0 * cache->mip_thumbs.stats_near_match / (float)cache->mip_thumbs.stats_requests,
         100.0 * cache->mip_thumbs.stats_misses / (float)cache->mip_thumbs.stats_requests,
         100.0 * cache->mip_thumbs.stats_standin / (float)sum_standins,
         100.0 * cache->mip_thumbs.stats_fetches / (float)sum_fetches,
         100.0 * cache->mip_thumbs.stats_requests / (float)sum);
  printf("[mipmap_cache] float | %6.2f%% | %6.2f%% | %6.2f%%  | %6.2f%% | %6.2f%%\n",
         100.0 * cache->mip_f.stats_near_match / (float)cache->mip_f.stats_requests,
         100.0 * cache->mip_f.stats_misses / (float)cache->mip_f.stats_requests,
         100.0 * cache->mip_f.stats_standin / (float)sum_standins,
         100.0 * cache->mip_f.stats_fetches / (float)sum_fetches,
         100.0 * cache->mip_f.stats_requests / (float)sum);
  printf("[mipmap_cache] full  | %6.2f%% | %6.2f%% | %6.2f%%  | %6.2f%% | %6.2f%%\n",
         100.0 * cache->mip_full.stats_near_match / (float)cache->mip_full.stats_requests,
         100.0 * cache->mip_full.stats_misses / (float)cache->mip_full.stats_requests,
         100.0 * cache->mip_full.stats_standin / (float)sum_standins,
         100.0 * cache->mip_full.stats_fetches / (float)sum_fetches,
         100.0 * cache->mip_full.stats_requests / (float)sum);
  printf("\n\n");
}

static dt_mipmap_cache_one_t *_get_cache(dt_mipmap_cache_t *cache, const dt_mipmap_size_t mip)
{
  switch(mip)
  {
    case DT_MIPMAP_FULL:
      return &cache->mip_full;
    case DT_MIPMAP_F:
      return &cache->mip_f;
    default:
      return &cache->mip_thumbs;
  }
}

// We don't use the best effort for float32 input images
static gboolean _find_nearest_mipmap(dt_mipmap_cache_t *cache, dt_mipmap_buffer_t *buf, const int32_t imgid,
                                     const dt_mipmap_size_t mip, const dt_mipmap_get_flags_t flags,
                                     const char mode, const char *file, int line)
{
  __sync_fetch_and_add(&(_get_cache(cache, mip)->stats_requests), 1);

  // Find any existing thumbnail at least as large as the required size
  for(int k = mip; k < DT_MIPMAP_F; k++)
  {
    // already loaded?
    dt_mipmap_cache_get(cache, buf, imgid, k, DT_MIPMAP_TESTLOCK, 'r');
    if(buf->buf && buf->width > 0 && buf->height > 0)
    {
      __sync_fetch_and_add(&(_get_cache(cache, mip)->stats_standin), 1);

      dt_print(DT_DEBUG_CACHE, "[mipmap_cache] grab mip %d for image %" PRIu32 " (%ix%i) from RAM cache\n", mip,
              imgid, buf->width, buf->height);

      /* raise signal that mipmaps have been flushed to cache */
      dt_mipmap_ready_idle_signal(GINT_TO_POINTER(imgid));
      return 1;
    }
    // didn't succeed the first time? prefetch for later!
    if(mip == k)
    {
      __sync_fetch_and_add(&(_get_cache(cache, mip)->stats_standin), 1);
      dt_mipmap_cache_get(cache, buf, imgid, mip, DT_MIPMAP_PREFETCH, 'r');
    }
  }

  // Last chance: look for smaller sizes
  for(int k = mip; k > DT_MIPMAP_0; k--)
  {
    // already loaded?
    dt_mipmap_cache_get(cache, buf, imgid, k, DT_MIPMAP_TESTLOCK, 'r');
    if(buf->buf && buf->width > 0 && buf->height > 0)
    {
      __sync_fetch_and_add(&(_get_cache(cache, mip)->stats_near_match), 1);

      dt_print(DT_DEBUG_CACHE, "[mipmap_cache] grab mip %d for image %" PRIu32 " (%ix%i) from RAM cache\n", mip,
              imgid, buf->width, buf->height);

      /* raise signal that mipmaps have been flushed to cache */
      dt_mipmap_ready_idle_signal(GINT_TO_POINTER(imgid));
      return 1;
    }
  }

  __sync_fetch_and_add(&(_get_cache(cache, mip)->stats_misses), 1);
  return 0;
}


static uint8_t * _shift_buffer(struct dt_mipmap_buffer_dsc *dsc)
{
  // skip to next 8-byte alignment, for sse buffers.
  // FIXME: first, WTF ? Then, we need 64 bits aligments.
  // Finally, the struct itself should be declared aligned on 64 bits and that stuff is outright dangerous.
  return (uint8_t *)(dsc + 1);
}


static void _realloc_shifted_buffer(struct dt_mipmap_buffer_dsc *dsc)
{
  ASAN_UNPOISON_MEMORY_REGION(_shift_buffer(dsc), dsc->size - sizeof(struct dt_mipmap_buffer_dsc));
}


static void _realloc_buffer(struct dt_mipmap_buffer_dsc *dsc)
{
  ASAN_UNPOISON_MEMORY_REGION(dsc, dt_mipmap_buffer_dsc_size);
}


// update buffer params with dsc
static void _sync_dsc_to_buf(dt_mipmap_buffer_t *buf, struct dt_mipmap_buffer_dsc *dsc, const int32_t imgid,
                             const dt_mipmap_size_t mip)
{
  buf->width = dsc->width;
  buf->height = dsc->height;
  buf->iscale = dsc->iscale;
  buf->color_space = dsc->color_space;
  buf->imgid = imgid;
  buf->size = mip;
}


// flag a buffer as invalid and set it NULL
static void _invalidate_buffer(dt_mipmap_buffer_t *buf)
{
  buf->buf = NULL;
  buf->imgid = UNKNOWN_IMAGE;
  buf->size = DT_MIPMAP_NONE;
  buf->width = buf->height = 0;
  buf->iscale = 0.0f;
  buf->color_space = DT_COLORSPACE_NONE;
}

// if we get a zero-sized image, paint skulls to signal a missing image
static void _paint_skulls(dt_mipmap_buffer_t *buf, struct dt_mipmap_buffer_dsc *dsc, const dt_mipmap_size_t mip)
{
  if(dsc->width == 0 || dsc->height == 0)
  {
    // fprintf(stderr, "[mipmap cache get] got a zero-sized image for img %u mip %d!\n", imgid, mip);
    if(mip < DT_MIPMAP_F)
      dead_image_8(buf);
    else if(mip == DT_MIPMAP_F)
      dead_image_f(buf);
    else
      buf->buf = NULL; // full images with NULL buffer have to be handled, indicates `missing image', but still return locked slot
  }
}


static void _validate_buffer(dt_mipmap_buffer_t *buf, struct dt_mipmap_buffer_dsc *dsc, const int32_t imgid,
                             const dt_mipmap_size_t mip)
{
  _sync_dsc_to_buf(buf, dsc, imgid, mip);
  _realloc_shifted_buffer(dsc);
  buf->buf = _shift_buffer(dsc);
}


void dt_mipmap_cache_get_with_caller(dt_mipmap_cache_t *cache, dt_mipmap_buffer_t *buf, const int32_t imgid,
                                     const dt_mipmap_size_t mip, const dt_mipmap_get_flags_t flags,
                                     const char mode, const char *file, int line)
{
  assert(mip <= DT_MIPMAP_NONE && mip >= DT_MIPMAP_0);

  const int32_t key = get_key(imgid, mip);
  if(flags == DT_MIPMAP_TESTLOCK)
  {
    // simple case: only get and lock if it's there.
    dt_cache_entry_t *entry = dt_cache_testget(&_get_cache(cache, mip)->cache, key, mode);
    buf->cache_entry = entry;
    if(entry)
    {
      struct dt_mipmap_buffer_dsc *dsc = (struct dt_mipmap_buffer_dsc *)entry->data;
      _realloc_buffer(dsc);
      _validate_buffer(buf, dsc, imgid, mip);
    }
    else
    {
      _invalidate_buffer(buf);
    }
  }
  else if(flags == DT_MIPMAP_PREFETCH)
  {
    // and opposite: prefetch without locking
    dt_control_add_job(darktable.control, DT_JOB_QUEUE_SYSTEM_FG, dt_image_load_job_create(imgid, mip));
    // will raise DT_SIGNAL_DEVELOP_MIPMAP_UPDATED internally when finished
  }
  else if(flags == DT_MIPMAP_PREFETCH_DISK)
  {
    // only prefetch if the disk cache exists:
    if(!cache->cachedir[0]) return;

    // load from disk if file exists
    char filename[PATH_MAX] = {0};
    snprintf(filename, sizeof(filename), "%s.d/%d/%"PRIu32".jpg", cache->cachedir, (int)mip, (uint32_t)key);

    // will raise DT_SIGNAL_DEVELOP_MIPMAP_UPDATED internally when finished
    if(g_file_test(filename, G_FILE_TEST_EXISTS))
      dt_control_add_job(darktable.control, DT_JOB_QUEUE_SYSTEM_FG, dt_image_load_job_create(imgid, mip));
    else
      return;
  }
  else if(flags == DT_MIPMAP_BLOCKING)
  {
    // simple case: blocking get
    dt_cache_entry_t *entry =  dt_cache_get_with_caller(&_get_cache(cache, mip)->cache, key, mode, file, line);
    struct dt_mipmap_buffer_dsc *dsc = (struct dt_mipmap_buffer_dsc *)entry->data;
    _realloc_buffer(dsc);
    buf->cache_entry = entry;

    if(dsc->flags & DT_MIPMAP_BUFFER_DSC_FLAG_GENERATE)
    {
      __sync_fetch_and_add(&(_get_cache(cache, mip)->stats_fetches), 1);
      // we're write locked here, as requested by the alloc callback.
      // now fill it with data:
      if(mip == DT_MIPMAP_FULL)
      {
        // load the image:
        // make sure we access the r/w lock as shortly as possible!
        dt_image_t buffered_image;
        gboolean no_buffer = TRUE;

        const dt_image_t *cimg = dt_image_cache_get(darktable.image_cache, imgid, 'r');
        if(cimg)
        {
          buffered_image = *cimg;
          no_buffer = FALSE;
        }
        dt_image_cache_read_release(darktable.image_cache, cimg);

        if(no_buffer) return;

        char filename[PATH_MAX] = { 0 };
        gboolean from_cache = TRUE;
        dt_image_full_path(buffered_image.id,  filename,  sizeof(filename),  &from_cache, __FUNCTION__);
        buf->imgid = imgid;
        buf->size = mip;
        buf->buf = NULL;
        buf->width = buf->height = 0;
        buf->iscale = 0.0f;
        buf->color_space = DT_COLORSPACE_NONE; // TODO: does the full buffer need to know this?
        dt_imageio_retval_t ret = dt_imageio_open(&buffered_image, filename, buf); // TODO: color_space?
        // might have been reallocated:
        dsc = (struct dt_mipmap_buffer_dsc *)buf->cache_entry->data;
        _realloc_buffer(dsc);

        if(ret != DT_IMAGEIO_OK)
        {
          // we can only return a zero dimension buffer if the buffer has been allocated.
          // in case dsc couldn't be allocated and points to the static buffer, it contains
          // a dead image already.
          if((void *)dsc != (void *)dt_mipmap_cache_static_dead_image)
          {
            dsc->width = dsc->height = 0;
            buf->iscale = 0.0f;
            dsc->color_space = DT_COLORSPACE_NONE;
          }
        }
        else
        {
          // swap back new image data:
          dt_image_t *img = dt_image_cache_get(darktable.image_cache, imgid, 'w');
          *img = buffered_image;

          // don't write xmp for this (we only changed db stuff):
          dt_image_cache_write_release(darktable.image_cache, img, DT_IMAGE_CACHE_RELAXED);

          // Notify the DB that mipmap is in sync with current history hash
          dt_history_hash_set_mipmap(imgid);
        }
      }
      else if(mip == DT_MIPMAP_F)
      {
        _realloc_shifted_buffer(dsc);
        dt_print(DT_DEBUG_CACHE,
                 "[mipmap_cache] compute mip %d for image %" PRIu32 " (%ix%i) from original file \n", mip,
                 get_imgid(entry->key), dsc->width, dsc->height);
        _init_f(buf, (float *)_shift_buffer(dsc), &dsc->width, &dsc->height, &dsc->iscale, imgid);
      }
      else
      {
        // 8-bit thumbs
        _realloc_shifted_buffer(dsc);
        dt_print(DT_DEBUG_CACHE,
                 "[mipmap_cache] compute mip %d for image %" PRIu32 " (%ix%i) from original file \n", mip,
                 get_imgid(entry->key), dsc->width, dsc->height);
        _init_8((uint8_t *)_shift_buffer(dsc), &dsc->width, &dsc->height, &dsc->iscale, &buf->color_space, imgid, mip);
      }
      dsc->color_space = buf->color_space;
      dsc->flags &= ~DT_MIPMAP_BUFFER_DSC_FLAG_GENERATE;
    }

    // image cache is leaving the write lock in place in case the image has been newly allocated.
    // this leads to a slight increase in thread contention, so we opt for dropping the write lock
    // and acquiring a read lock immediately after. since this opens a small window for other threads
    // to get in between, we need to take some care to re-init cache entries and dsc.
    // note that concurrencykit has rw locks that can be demoted from w->r without losing the lock in between.
    if(mode == 'r')
    {
      entry->_lock_demoting = 1;
      // drop the write lock
      dt_cache_release(&_get_cache(cache, mip)->cache, entry);
      // get a read lock
      buf->cache_entry = entry = dt_cache_get(&_get_cache(cache, mip)->cache, key, mode);
      dsc = (struct dt_mipmap_buffer_dsc *)buf->cache_entry->data;
      _realloc_buffer(dsc);
      entry->_lock_demoting = 0;
    }

    // The cache can't be locked twice from the same thread,
    // because then it would never unlock.
#ifdef _DEBUG
    const pthread_t writer = dt_pthread_rwlock_get_writer(&(buf->cache_entry->lock));
    if(mode == 'w')
    {
      assert(pthread_equal(writer, pthread_self()));
    }
    else
    {
      assert(!pthread_equal(writer, pthread_self()));
    }
#endif

    _validate_buffer(buf, dsc, imgid, mip);
    _paint_skulls(buf, dsc, mip);
  }
  else if(flags == DT_MIPMAP_BEST_EFFORT && mip < DT_MIPMAP_F)
  {
    // We don't use the best effort for float32 input images
    if(!_find_nearest_mipmap(cache, buf, imgid, mip, flags, mode, file, line))
    {
      _invalidate_buffer(buf);
    }
  }
  else
  {
    _invalidate_buffer(buf);
  }
}

void dt_mipmap_cache_write_get_with_caller(dt_mipmap_cache_t *cache, dt_mipmap_buffer_t *buf, const int32_t imgid, const int mip, const char *file, int line)
{
  dt_mipmap_cache_get_with_caller(cache, buf, imgid, mip, DT_MIPMAP_BLOCKING, 'w', file, line);
}

void dt_mipmap_cache_release_with_caller(dt_mipmap_cache_t *cache, dt_mipmap_buffer_t *buf, const char *file,
                                         int line)
{
  if(buf->size == DT_MIPMAP_NONE) return;
  assert(buf->imgid > 0);
  assert(buf->size >= DT_MIPMAP_0);
  assert(buf->size < DT_MIPMAP_NONE);
  assert(buf->cache_entry);
  dt_cache_release_with_caller(&_get_cache(cache, buf->size)->cache, buf->cache_entry, file, line);
  buf->size = DT_MIPMAP_NONE;
  buf->buf = NULL;
}


// return index dt_mipmap_size_t having at least width & height requested instead of minimum combined diff
// please note that the requested size is in pixels not dots.
dt_mipmap_size_t dt_mipmap_cache_get_matching_size(const dt_mipmap_cache_t *cache, const int32_t width,
                                                   const int32_t height)
{
  dt_mipmap_size_t best = DT_MIPMAP_NONE;
  for(int k = DT_MIPMAP_0; k < DT_MIPMAP_F; k++)
  {
    best = k;
    if((cache->max_width[k] >= width) && (cache->max_height[k] >= height))
    {
      dt_print(DT_DEBUG_IMAGEIO, "[dt_mipmap_cache_get_matching_size] will load a mipmap of size %lux%lu px\n", cache->max_width[k], cache->max_height[k]);
      break;
    }
  }
  return best;
}

void dt_mipmap_cache_remove_at_size(dt_mipmap_cache_t *cache, const int32_t imgid, const dt_mipmap_size_t mip)
{
  if(mip > DT_MIPMAP_7 || mip < DT_MIPMAP_0) return;
  // get rid of all ldr thumbnails:
  const uint32_t key = get_key(imgid, mip);
  dt_cache_entry_t *entry = dt_cache_testget(&_get_cache(cache, mip)->cache, key, 'w');
  if(entry)
  {
    struct dt_mipmap_buffer_dsc *dsc = (struct dt_mipmap_buffer_dsc *)entry->data;
    _realloc_buffer(dsc);
    dsc->flags |= DT_MIPMAP_BUFFER_DSC_FLAG_INVALIDATE;
    dt_cache_release(&_get_cache(cache, mip)->cache, entry);

    // due to DT_MIPMAP_BUFFER_DSC_FLAG_INVALIDATE, removes thumbnail from disc
    dt_cache_remove(&_get_cache(cache, mip)->cache, key);
  }

  // ugly, but avoids alloc'ing thumb if it is not there.
  dt_mipmap_cache_unlink_ondisk_thumbnail((&_get_cache(cache, mip)->cache)->cleanup_data, imgid, mip);
}

void dt_mipmap_cache_remove(dt_mipmap_cache_t *cache, const int32_t imgid)
{
  // get rid of all ldr thumbnails:

  for(dt_mipmap_size_t k = DT_MIPMAP_0; k < DT_MIPMAP_F; k++)
  {
    dt_mipmap_cache_remove_at_size(cache, imgid, k);
  }
}
void dt_mipmap_cache_evict_at_size(dt_mipmap_cache_t *cache, const int32_t imgid, const dt_mipmap_size_t mip)
{
  const uint32_t key = get_key(imgid, mip);
  // write thumbnail to disc if not existing there
  dt_cache_remove(&_get_cache(cache, mip)->cache, key);
}

void dt_mimap_cache_evict(dt_mipmap_cache_t *cache, const int32_t imgid)
{
  for(dt_mipmap_size_t k = DT_MIPMAP_0; k < DT_MIPMAP_F; k++)
  {
    const uint32_t key = get_key(imgid, k);

    // write thumbnail to disc if not existing there
    dt_cache_remove(&_get_cache(cache, k)->cache, key);
  }
}

static void _init_f(dt_mipmap_buffer_t *mipmap_buf, float *out, uint32_t *width, uint32_t *height, float *iscale,
                    const int32_t imgid)
{
  const uint32_t wd = *width, ht = *height;

  /* do not even try to process file if it isn't available */
  char filename[PATH_MAX] = { 0 };
  gboolean from_cache = TRUE;
  dt_image_full_path(imgid,  filename,  sizeof(filename),  &from_cache, __FUNCTION__);
  if(!*filename || !g_file_test(filename, G_FILE_TEST_EXISTS))
  {
    *width = *height = 0;
    *iscale = 0.0f;
    return;
  }

  dt_mipmap_buffer_t buf;
  dt_mipmap_cache_get(darktable.mipmap_cache, &buf, imgid, DT_MIPMAP_FULL, DT_MIPMAP_BLOCKING, 'r');

  // lock image after we have the buffer, we might need to lock the image struct for
  // writing during raw loading, to write to width/height.
  const dt_image_t *image = dt_image_cache_get(darktable.image_cache, imgid, 'r');

  dt_iop_roi_t roi_in, roi_out;
  roi_in.x = roi_in.y = 0;
  roi_in.width = image->width;
  roi_in.height = image->height;
  roi_in.scale = 1.0f;

  roi_out.x = roi_out.y = 0;

  // now let's figure out the scaling...

  // MIP_F is 4 channels, and we do not demosaic here
  const float coeff = (image->buf_dsc.filters) ? 2.0f : 1.0f;

  roi_out.scale = fminf((coeff * (float)wd) / (float)image->width, (coeff * (float)ht) / (float)image->height);
  roi_out.width = roi_out.scale * roi_in.width;
  roi_out.height = roi_out.scale * roi_in.height;

  if(!buf.buf)
  {
    dt_control_log(_("image `%s' is not available!"), image->filename);
    dt_image_cache_read_release(darktable.image_cache, image);
    *width = *height = 0;
    *iscale = 0.0f;
    return;
  }

  assert(!buffer_is_broken(&buf));

  mipmap_buf->color_space = DT_COLORSPACE_NONE; // TODO: do we need that information in this buffer?

  if(image->buf_dsc.filters)
  {
    if(image->buf_dsc.filters != 9u && image->buf_dsc.datatype == TYPE_FLOAT)
    {
      dt_iop_clip_and_zoom_mosaic_half_size_f((float *const)out, (const float *const)buf.buf, &roi_out, &roi_in,
                                              roi_out.width, roi_in.width, image->buf_dsc.filters);
    }
    else if(image->buf_dsc.filters != 9u && image->buf_dsc.datatype == TYPE_UINT16)
    {
      dt_iop_clip_and_zoom_mosaic_half_size((uint16_t * const)out, (const uint16_t *)buf.buf, &roi_out, &roi_in,
                                            roi_out.width, roi_in.width, image->buf_dsc.filters);
    }
    else if(image->buf_dsc.filters == 9u && image->buf_dsc.datatype == TYPE_UINT16)
    {
      dt_iop_clip_and_zoom_mosaic_third_size_xtrans((uint16_t * const)out, (const uint16_t *)buf.buf, &roi_out,
                                                    &roi_in, roi_out.width, roi_in.width, image->buf_dsc.xtrans);
    }
    else if(image->buf_dsc.filters == 9u && image->buf_dsc.datatype == TYPE_FLOAT)
    {
      dt_iop_clip_and_zoom_mosaic_third_size_xtrans_f(out, (const float *)buf.buf, &roi_out, &roi_in,
                                                      roi_out.width, roi_in.width, image->buf_dsc.xtrans);
    }
    else
    {
      dt_unreachable_codepath();
    }
  }
  else
  {
    // downsample
    dt_iop_clip_and_zoom(out, (const float *)buf.buf, &roi_out, &roi_in, roi_out.width, roi_in.width);
  }

  dt_mipmap_cache_release(darktable.mipmap_cache, &buf);

  *width = roi_out.width;
  *height = roi_out.height;
  *iscale = (float)image->width / (float)roi_out.width;

  dt_image_cache_read_release(darktable.image_cache, image);
}


// dummy functions for `export' to mipmap buffers:
typedef struct _dummy_data_t
{
  dt_imageio_module_data_t head;
  uint8_t *buf;
} _dummy_data_t;

static int _levels(dt_imageio_module_data_t *data)
{
  return IMAGEIO_RGB | IMAGEIO_INT8;
}

static int _bpp(dt_imageio_module_data_t *data)
{
  return 8;
}

static int _write_image(dt_imageio_module_data_t *data, const char *filename, const void *in,
                        dt_colorspaces_color_profile_type_t over_type, const char *over_filename,
                        void *exif, int exif_len, int32_t imgid, int num, int total, dt_dev_pixelpipe_t *pipe,
                        const gboolean export_masks)
{
  _dummy_data_t *d = (_dummy_data_t *)data;
  memcpy(d->buf, in, sizeof(uint32_t) * data->width * data->height);
  return 0;
}

static void _init_8(uint8_t *buf, uint32_t *width, uint32_t *height, float *iscale,
                    dt_colorspaces_color_profile_type_t *color_space, const int32_t imgid,
                    const dt_mipmap_size_t size)
{
  if(size >= DT_MIPMAP_F) return;

  *iscale = 1.0f;
  const uint32_t wd = *width, ht = *height;
  char filename[PATH_MAX] = { 0 };
  gboolean from_cache = TRUE;

  /* do not even try to process file if it isn't available */
  dt_image_full_path(imgid,  filename,  sizeof(filename),  &from_cache, __FUNCTION__);
  if(!*filename || !g_file_test(filename, G_FILE_TEST_EXISTS))
  {
    *width = *height = 0;
    *iscale = 0.0f;
    *color_space = DT_COLORSPACE_NONE;
    return;
  }

  int res = 1;

  if(res && size < DT_MIPMAP_F - 1)
  {
    // try to generate mip from larger mip
    // This expects that invalid mips will be flushed, so the assumption is:
    // if mip then it's valid (aka current w.r.t. current history)
    for(dt_mipmap_size_t k = size + 1; k < DT_MIPMAP_F; k++)
    {
      dt_mipmap_buffer_t tmp;
      dt_mipmap_cache_get(darktable.mipmap_cache, &tmp, imgid, k, DT_MIPMAP_TESTLOCK, 'r');
      if(tmp.buf == NULL) continue;

      dt_print(DT_DEBUG_CACHE, "[mipmap_cache] generate mip %d for image %d from level %d\n", size, imgid, k);
      *color_space = tmp.color_space;
      // downsample
      dt_iop_flip_and_zoom_8(tmp.buf, tmp.width, tmp.height, buf, wd, ht, ORIENTATION_NONE, width, height);

      dt_mipmap_cache_release(darktable.mipmap_cache, &tmp);
      res = 0;
      break;
    }
  }

  const gboolean altered = dt_image_altered(imgid);

  const dt_image_t *cimg = dt_image_cache_get(darktable.image_cache, imgid, 'r');
  // the orientation for this camera is not read correctly from exiv2, so we need
  // to go the full path (as the thumbnail will be flipped the wrong way round)
  const int incompatible = !strncmp(cimg->exif_maker, "Phase One", 9);
  dt_image_cache_read_release(darktable.image_cache, cimg);

  if(res && !altered && !incompatible)
  {
    const dt_image_orientation_t orientation = dt_image_get_orientation(imgid);

    from_cache = TRUE;
    memset(filename, 0, sizeof(filename));
    dt_image_full_path(imgid,  filename,  sizeof(filename),  &from_cache, __FUNCTION__);

    const char *c = filename + strlen(filename);
    while(*c != '.' && c > filename) c--;
    if(!strcasecmp(c, ".jpg") || !strcasecmp(c, ".jpeg"))
    {
      // try to load jpg
      dt_imageio_jpeg_t jpg;
      if(!dt_imageio_jpeg_read_header(filename, &jpg))
      {
        uint8_t *tmp = (uint8_t *)dt_alloc_align(sizeof(uint8_t) * jpg.width * jpg.height * 4);
        *color_space = dt_imageio_jpeg_read_color_space(&jpg);
        if(!dt_imageio_jpeg_read(&jpg, tmp))
        {
          // scale to fit
          dt_print(DT_DEBUG_CACHE, "[mipmap_cache] generate mip %d for image %d from jpeg\n", size, imgid);
          dt_iop_flip_and_zoom_8(tmp, jpg.width, jpg.height, buf, wd, ht, orientation, width, height);
          res = 0;
        }
        dt_free_align(tmp);
      }
    }
    else
    {
      // try to load the embedded thumbnail in raw
      uint8_t *tmp = NULL;
      int32_t thumb_width, thumb_height;
      res = dt_imageio_large_thumbnail(filename, &tmp, &thumb_width, &thumb_height, color_space);
      if(!res)
      {
        // if the thumbnail is not large enough, we compute one
        const dt_image_t *img2 = dt_image_cache_get(darktable.image_cache, imgid, 'r');
        const int imgwd = img2->width;
        const int imght = img2->height;
        dt_image_cache_read_release(darktable.image_cache, img2);
        if(thumb_width < wd && thumb_height < ht && thumb_width < imgwd - 4 && thumb_height < imght - 4)
        {
          res = 1;
        }
        else
        {
          // scale to fit
          dt_print(DT_DEBUG_CACHE, "[mipmap_cache] generate mip %d for image %d from embedded jpeg\n", size, imgid);
          dt_iop_flip_and_zoom_8(tmp, thumb_width, thumb_height, buf, wd, ht, orientation, width, height);
        }
        dt_free_align(tmp);
      }
    }
  }

  if(res)
  {
    // try the real thing: rawspeed + pixelpipe
    dt_imageio_module_format_t format;
    _dummy_data_t dat;
    format.bpp = _bpp;
    format.write_image = _write_image;
    format.levels = _levels;
    dat.head.max_width = wd;
    dat.head.max_height = ht;
    dat.buf = buf;
    // export with flags: ignore exif (don't load from disk), don't swap byte order, don't do hq processing,
    // no upscaling and signal we want thumbnail export
    res = dt_imageio_export_with_flags(imgid, "unused", &format, (dt_imageio_module_data_t *)&dat, TRUE, FALSE, FALSE,
                                       FALSE, TRUE, NULL, FALSE, FALSE, DT_COLORSPACE_NONE, NULL, DT_INTENT_LAST, NULL,
                                       NULL, 1, 1, NULL);
    if(!res)
    {
      dt_print(DT_DEBUG_CACHE, "[mipmap_cache] generated mip %d for image %d from scratch\n", size, imgid);
      // might be smaller, or have a different aspect than what we got as input.
      *width = dat.head.width;
      *height = dat.head.height;
      *iscale = 1.0f;
      *color_space = DT_COLORSPACE_ADOBERGB;
    }
  }

  // any errors?
  if(res)
  {
    // fprintf(stderr, "[mipmap_cache] could not process thumbnail!\n");
    *width = *height = 0;
    *iscale = 0.0f;
    *color_space = DT_COLORSPACE_NONE;
    return;
  }
}

void dt_mipmap_cache_copy_thumbnails(const dt_mipmap_cache_t *cache, const uint32_t dst_imgid, const uint32_t src_imgid)
{
  if(cache->cachedir[0] && dt_conf_get_bool("cache_disk_backend"))
  {
    for(dt_mipmap_size_t mip = DT_MIPMAP_0; mip < DT_MIPMAP_F; mip++)
    {
      // try and load from disk, if successful set flag
      char srcpath[PATH_MAX] = {0};
      char dstpath[PATH_MAX] = {0};
      snprintf(srcpath, sizeof(srcpath), "%s.d/%d/%"PRIu32".jpg", cache->cachedir, (int)mip, src_imgid);
      snprintf(dstpath, sizeof(dstpath), "%s.d/%d/%"PRIu32".jpg", cache->cachedir, (int)mip, dst_imgid);
      GFile *src = g_file_new_for_path(srcpath);
      GFile *dst = g_file_new_for_path(dstpath);
      GError *gerror = NULL;
      g_file_copy(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &gerror);
      // ignore errors, we tried what we could.
      g_object_unref(dst);
      g_object_unref(src);
      g_clear_error(&gerror);
    }
  }
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
