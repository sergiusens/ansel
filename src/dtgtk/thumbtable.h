/*
    This file is part of darktable,
    Copyright (C) 2019-2021 darktable developers.
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
/** a class to manage a table of thumbnail for lighttable and filmstrip.  */
#include "dtgtk/thumbnail.h"
#include "common/dtpthread.h"
#include <gtk/gtk.h>

#pragma once

typedef enum dt_thumbtable_mode_t
{
  DT_THUMBTABLE_MODE_NONE,
  DT_THUMBTABLE_MODE_FILEMANAGER,
  DT_THUMBTABLE_MODE_FILMSTRIP
} dt_thumbtable_mode_t;

typedef struct dt_thumbtable_cache_t
{
  int imgid;
  dt_thumbnail_t *thumb;
} dt_thumbtable_cache_t;

typedef struct dt_thumbtable_t
{
  dt_thumbtable_mode_t mode;
  dt_thumbnail_overlay_t overlays;

  GtkWidget *grid; // GtkGrid

  // Store the current number of columns in grid
  int grid_cols;

  // list of thumbnails loaded inside main widget (dt_thumbnail_t)
  // for filmstrip and filemanager, this is all the images drawn at screen (even partially)
  // for zoommable, this is all the images in the row drawn at screen. We don't load laterals images on fly.
  GList *list;

  int thumbs_per_row; // number of image in a row (1 for filmstrip ; MAX_ZOOM for zoomable)
  int thumb_width;              // demanded thumb size (real size can differ of 1 due to rounding)
  int thumb_height;
  int view_width, view_height; // last main widget size

  gboolean dragging;
  int last_x, last_y;         // last position of cursor during move
  int drag_dx, drag_dy;       // distance of move of the current dragging session
  dt_thumbnail_t *drag_thumb; // thumb currently dragged (under the mouse)

  // when performing a drag, we store the list of items to drag here
  // as this can change during the drag and drop (esp. because of the image_over_id)
  GList *drag_list;

  // nb of thumbnails loaded
  uint32_t thumb_nb;

  // Set to TRUE once the current collection has been loaded into thumbnails,
  // reset to FALSE on collection changed events.
  // When TRUE, we bypass (re)-init of the thumbnails.
  gboolean collection_inited;
  gboolean thumbs_inited;
  gboolean configured;

  // Checksum of the collection query for caching
  uint64_t collection_hash;
  int collection_count;

  int min_row_id;
  int max_row_id;

  // Our LUT of collection, mapping rowid (index) to imgid (content)
  dt_thumbtable_cache_t *lut;

  GtkWidget *scroll_window;

  // References to the scrollbar adjustments belonging to the parent widget
  GtkAdjustment *v_scrollbar;
  GtkAdjustment *h_scrollbar;
  double x_position;
  double y_position;

  // Overlays in which we insert the grid, in central view and filmstrip
  GtkWidget *overlay_center;
  GtkWidget *overlay_filmstrip;

  // Since GUI and background signals can init/delete/populate/iterate over the same stuff,
  // ensure iterations don't happen on stuff being deleted at the same time.
  // Protect only the loops of dynamic size
  dt_pthread_mutex_t lock;

  // signal that the current collection needs to be entirely flushed unconditionnaly
  gboolean reset_collection;

  // show extended overlays while holding alt key
  gboolean alternate_mode;

} dt_thumbtable_t;


dt_thumbtable_t *dt_thumbtable_new();
void dt_thumbtable_cleanup(dt_thumbtable_t *table);

void dt_thumbtable_update(dt_thumbtable_t *table);
void dt_thumbtable_set_parent(dt_thumbtable_t *table,dt_thumbtable_mode_t mode);

// drag & drop receive function - handles dropping of files in the center view (files are added to the library)
void dt_thumbtable_event_dnd_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *selection_data, guint target_type, guint time, gpointer user_data);

// change the type of overlays that should be shown (over or under the image)
void dt_thumbtable_set_overlays_mode(dt_thumbtable_t *table, dt_thumbnail_overlay_t over);

// signal that the current collection needs to be flushed entirely before being reloaded
void dt_thumbtable_reset_collection(dt_thumbtable_t *table);

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
