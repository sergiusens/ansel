#pragma once

#include "common/image.h"
#include "control/control.h"
#include "common/variables.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct dt_control_import_t
{
  GList *imgs;
  GDateTime *datetime;
  gboolean copy;
  char *jobcode;
  char *target_folder;
  char *target_subfolder_pattern;
  char *target_file_pattern;
  char *target_dir;
  const int elements;

  GList *discarded;

} dt_control_import_t;


// free the internal strings of a dt_control_import_t structure. Doesn't free the structure itself.
void dt_control_import_data_free(dt_control_import_t *data);


/**
 * @brief Build a full path for a given image file, given a pattern.
 *
 * @param filename Full path of the original file
 * @param index Incremental number in a sequence
 * @param img dt_image_t object. Needs to be inited with EXIF fields prior to calling this function, otherwise EXIF variables are expanded to defaults/fallback.
 * @param data Import options
 * @return gchar* The full path after variables expansion
 */
gchar *dt_build_filename_from_pattern(const char *const filename, const int index, dt_image_t *img, dt_control_import_t *data);


/**
 * @brief Process a list of images to import with or without copying the files on an arbitrary hard-drive.
 *
 * @param data import informations to transmit through the functions
 */
void dt_control_import(dt_control_import_t data);


#ifdef __cplusplus
}
#endif
