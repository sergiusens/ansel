/*
    This file is part of darktable,
    Copyright (C) 2019-2022 darktable developers.

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
/** this is the thumbnail class for the lighttable module.  */

#include "common/extra_optimizations.h"

#include "dtgtk/thumbnail.h"

#include "bauhaus/bauhaus.h"
#include "common/collection.h"
#include "common/datetime.h"
#include "common/debug.h"
#include "common/focus.h"
#include "common/focus_peaking.h"
#include "common/grouping.h"
#include "common/image_cache.h"
#include "common/ratings.h"
#include "common/selection.h"
#include "common/variables.h"
#include "control/control.h"
#include "dtgtk/button.h"
#include "dtgtk/icon.h"
#include "dtgtk/thumbnail_btn.h"
#include "gui/drag_and_drop.h"

#include "views/view.h"

/**
 * @file thumbnail.c
 *
 * WARNING: because we create and destroy thumbnail objects dynamically when scrolling,
 * and we don't manually cleanup the Gtk signal handlers attached to widgets,
 * some callbacks/handlers can be left hanging, still record events, but send them
 * to non-existing objects. This is why we need to ensure user_data is not NULL everywhere.
 */
#define thumb_return_if_fails(thumb, ...) { if(!thumb || !thumb->widget) return  __VA_ARGS__; }

static void _thumb_resize_overlays(dt_thumbnail_t *thumb);

static void _set_flag(GtkWidget *w, GtkStateFlags flag, gboolean activate)
{
  if(activate)
    gtk_widget_set_state_flags(w, flag, FALSE);
  else
    gtk_widget_unset_state_flags(w, flag);
}

static void _image_update_group_tooltip(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb);
  if(!thumb->is_grouped)
  {
    gtk_widget_set_has_tooltip(thumb->w_group, FALSE);
    return;
  }

  gchar *tt = NULL;
  int nb = 0;

  // the group leader
  if(thumb->imgid == thumb->groupid)
    tt = g_strdup_printf("\n\u2022 <b>%s (%s)</b>", _("current"), _("leader"));
  else
  {
    const dt_image_t *img = dt_image_cache_get(darktable.image_cache, thumb->groupid, 'r');
    if(img)
    {
      tt = g_strdup_printf("%s\n\u2022 <b>%s (%s)</b>", _("\nclick here to set this image as group leader\n"), img->filename, _("leader"));
      dt_image_cache_read_release(darktable.image_cache, img);
    }
  }

  // and the other images
  sqlite3_stmt *stmt;
  // clang-format off
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
                              "SELECT id, version, filename"
                              " FROM main.images"
                              " WHERE group_id = ?1", -1, &stmt,
                              NULL);
  // clang-format on
  DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, thumb->groupid);
  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    nb++;
    const int id = sqlite3_column_int(stmt, 0);
    const int v = sqlite3_column_int(stmt, 1);

    if(id != thumb->groupid)
    {
      if(id == thumb->imgid)
        tt = dt_util_dstrcat(tt, "\n\u2022 %s", _("current"));
      else
      {
        tt = dt_util_dstrcat(tt, "\n\u2022 %s", sqlite3_column_text(stmt, 2));
        if(v > 0) tt = dt_util_dstrcat(tt, " v%d", v);
      }
    }
  }
  sqlite3_finalize(stmt);

  // and the number of grouped images
  gchar *ttf = g_strdup_printf("%d %s\n%s", nb, _("grouped images"), tt);
  g_free(tt);

  // let's apply the tooltip
  gtk_widget_set_tooltip_markup(thumb->w_group, ttf);
  g_free(ttf);
}

static void _thumb_update_rating_class(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb);
  for(int i = DT_VIEW_DESERT; i <= DT_VIEW_REJECT; i++)
  {
    gchar *cn = g_strdup_printf("dt_thumbnail_rating_%d", i);
    if(thumb->rating == i)
      dt_gui_add_class(thumb->w_main, cn);
    else
      dt_gui_remove_class(thumb->w_main, cn);
    g_free(cn);
  }
}

static void _thumb_write_extension(dt_thumbnail_t *thumb)
{
  // fill the file extension label
  thumb_return_if_fails(thumb);
  if(!thumb->filename) return;
  const char *ext = thumb->filename + strlen(thumb->filename);
  while(ext > thumb->filename && *ext != '.') ext--;
  ext++;
  gchar *uext = dt_view_extend_modes_str(ext, thumb->is_hdr, thumb->is_bw, thumb->is_bw_flow);
  gtk_label_set_text(GTK_LABEL(thumb->w_ext), uext);
  g_free(uext);
}


static void _image_get_infos(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb);

  // we only get here infos that might change, others(exif, ...) are cached on widget creation
  const int old_rating = thumb->rating;
  thumb->rating = 0;
  const dt_image_t *img = dt_image_cache_get(darktable.image_cache, thumb->imgid, 'r');
  if(img)
  {
    thumb->has_localcopy = (img->flags & DT_IMAGE_LOCAL_COPY);
    thumb->rating = img->flags & DT_IMAGE_REJECTED ? DT_VIEW_REJECT : (img->flags & DT_VIEW_RATINGS_MASK);
    thumb->is_bw = dt_image_monochrome_flags(img);
    thumb->is_bw_flow = dt_image_use_monochrome_workflow(img);
    thumb->is_hdr = dt_image_is_hdr(img);
    thumb->filename = g_strdup(img->filename);
    thumb->has_audio = (img->flags & DT_IMAGE_HAS_WAV);

    thumb->iso = img->exif_iso;
    thumb->aperture = img->exif_aperture;
    thumb->speed = img->exif_exposure;
    thumb->exposure_bias = img->exif_exposure_bias;
    thumb->focal = img->exif_focal_length;
    thumb->focus_distance = img->exif_focus_distance;
    dt_datetime_img_to_local(thumb->datetime, sizeof(thumb->datetime), img, FALSE);
    memcpy(&thumb->camera, &img->camera_makermodel, 128 * sizeof(char));
    memcpy(&thumb->lens, &img->exif_lens, 128 * sizeof(char));

    thumb->groupid = img->group_id;
    thumb->colorlabels = img->color_labels;

    dt_image_cache_read_release(darktable.image_cache, img);
  }
  // if the rating as changed, update the rejected
  if(old_rating != thumb->rating)
  {
    _thumb_update_rating_class(thumb);
  }

  // colorlabels
  if(thumb->w_color)
  {
    GtkDarktableThumbnailBtn *btn = (GtkDarktableThumbnailBtn *)thumb->w_color;
    btn->icon_flags = thumb->colorlabels;
  }

  // altered
  thumb->is_altered = dt_image_altered(thumb->imgid);

  // grouping
  DT_DEBUG_SQLITE3_CLEAR_BINDINGS(darktable.view_manager->statements.get_grouped);
  DT_DEBUG_SQLITE3_RESET(darktable.view_manager->statements.get_grouped);
  DT_DEBUG_SQLITE3_BIND_INT(darktable.view_manager->statements.get_grouped, 1, thumb->imgid);
  DT_DEBUG_SQLITE3_BIND_INT(darktable.view_manager->statements.get_grouped, 2, thumb->imgid);
  thumb->is_grouped = (sqlite3_step(darktable.view_manager->statements.get_grouped) == SQLITE_ROW);

  // grouping tooltip
  _image_update_group_tooltip(thumb);

  _thumb_write_extension(thumb);
}

static void _thumb_retrieve_margins(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb);

  if(thumb->img_margin) gtk_border_free(thumb->img_margin);
  // we retrieve image margins from css
  GtkStateFlags state = gtk_widget_get_state_flags(thumb->w_image);
  thumb->img_margin = gtk_border_new();
  GtkStyleContext *context = gtk_widget_get_style_context(thumb->w_image);
  gtk_style_context_get_margin(context, state, thumb->img_margin);

  // and we apply it to the thumb size
  int width, height;
  gtk_widget_get_size_request(thumb->w_main, &width, &height);
  thumb->img_margin->left = MAX(0, thumb->img_margin->left * width / 1000);
  thumb->img_margin->top = MAX(0, thumb->img_margin->top * height / 1000);
  thumb->img_margin->right = MAX(0, thumb->img_margin->right * width / 1000);
  thumb->img_margin->bottom = MAX(0, thumb->img_margin->bottom * height / 1000);
}

static gboolean _event_cursor_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);

  GtkStateFlags state = gtk_widget_get_state_flags(thumb->w_cursor);
  GtkStyleContext *context = gtk_widget_get_style_context(thumb->w_cursor);
  GdkRGBA col;
  gtk_style_context_get_color(context, state, &col);

  cairo_set_source_rgba(cr, col.red, col.green, col.blue, col.alpha);
  cairo_line_to(cr, gtk_widget_get_allocated_width(widget), 0);
  cairo_line_to(cr, gtk_widget_get_allocated_width(widget) / 2, gtk_widget_get_allocated_height(widget));
  cairo_line_to(cr, 0, 0);
  cairo_close_path(cr);
  cairo_fill(cr);

  return TRUE;
}

static void _thumb_set_image_area(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb);
  int image_w = thumb->width - thumb->img_margin->left - thumb->img_margin->right;
  int image_h = thumb->height - thumb->img_margin->top - thumb->img_margin->bottom;
  gtk_widget_set_margin_top(thumb->w_image, thumb->img_margin->top);
  gtk_widget_set_margin_start(thumb->w_image, thumb->img_margin->left);
  gtk_widget_set_margin_bottom(thumb->w_image, thumb->img_margin->bottom);
  gtk_widget_set_margin_end(thumb->w_image, thumb->img_margin->right);

  if(thumb->over == DT_THUMBNAIL_OVERLAYS_ALWAYS_NORMAL)
  {
    int w = 0;
    int h = 0;
    gtk_widget_get_size_request(thumb->w_bottom_eb, &w, &h);
    image_h -= MAX(0, h);
  }

  gtk_widget_set_size_request(thumb->w_image, image_w, image_h);
}

static void _free_image_surface(dt_thumbnail_t *thumb)
{
  if(thumb->img_surf)
  {
    if(cairo_surface_get_reference_count(thumb->img_surf) > 0)
      cairo_surface_destroy(thumb->img_surf);

    thumb->img_surf = NULL;
  }
}

static int _get_image_buffer(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb, FALSE);

  // If image inited, it means we already have a cached image surface at the proper
  // size. The resizing handlers should reset this flag when size changes.
  if(thumb->image_inited && thumb->img_surf) return G_SOURCE_REMOVE;

  _free_image_surface(thumb);

  int image_w = 0;
  int image_h = 0;
  gtk_widget_get_size_request(thumb->w_image, &image_w, &image_h);

  int zoom = (thumb->table) ? thumb->table->zoom : DT_THUMBTABLE_ZOOM_FIT;
  float x_center = 0.f;
  float y_center = 0.f;

  dt_view_surface_value_t res = dt_view_image_get_surface(thumb->imgid, image_w, image_h, &thumb->img_surf, zoom, &x_center, &y_center);

  if(thumb->img_surf && res == DT_VIEW_SURFACE_OK)
  {
    // The image is immediately available
    thumb->img_width = cairo_image_surface_get_width(thumb->img_surf);
    thumb->img_height = cairo_image_surface_get_height(thumb->img_surf);

    // Init the zoom offset using the barycenter of details, to center
    // the zoomed-in image on content that matters: details.
    // Offset is expressed from the center of the image
    if(thumb->table && thumb->table->zoom > DT_THUMBTABLE_ZOOM_FIT
       && x_center > 0.f && y_center > 0.f)
    {
      thumb->zoomx = (double)thumb->img_width / 2. - x_center;
      thumb->zoomy = (double)thumb->img_height / 2. - y_center;
    }
  }
  else
  {
    // A new export pipeline has been queued to generate the image
    // Nothing more we can do here but wait for it to return.
    thumb->busy = TRUE;
    thumb->image_inited = FALSE;
    // When the DT_SIGNAL_DEVELOP_MIPMAP_UPDATED signal will be raised,
    // once the export pipeline will be done generating our image,
    // the corresponding thumb will be set to thumb->busy = FALSE
    // by the signal handler.
    return G_SOURCE_REMOVE;
  }

  // if needed we compute and draw here the big rectangle to show focused areas
  if(thumb->table && thumb->table->focus)
  {
    uint8_t *full_res_thumb = NULL;
    int32_t full_res_thumb_wd, full_res_thumb_ht;
    dt_colorspaces_color_profile_type_t color_space;
    char path[PATH_MAX] = { 0 };
    gboolean from_cache = TRUE;
    dt_image_full_path(thumb->imgid,  path,  sizeof(path),  &from_cache, __FUNCTION__);
    if(!dt_imageio_large_thumbnail(path, &full_res_thumb, &full_res_thumb_wd, &full_res_thumb_ht, &color_space))
    {
      // we look for focus areas
      dt_focus_cluster_t full_res_focus[49];
      const int frows = 5, fcols = 5;
      dt_focus_create_clusters(full_res_focus, frows, fcols, full_res_thumb, full_res_thumb_wd,
                                full_res_thumb_ht);
      // and we draw them on the image
      cairo_t *cri = cairo_create(thumb->img_surf);
      dt_focus_draw_clusters(cri, cairo_image_surface_get_width(thumb->img_surf),
                              cairo_image_surface_get_height(thumb->img_surf), thumb->imgid, full_res_thumb_wd,
                              full_res_thumb_ht, full_res_focus, frows, fcols, 1.0, 0, 0);
      cairo_destroy(cri);
    }
    dt_free_align(full_res_thumb);
  }

  thumb->busy = FALSE;
  thumb->image_inited = TRUE;

  return G_SOURCE_REMOVE;
}

static gboolean
_thumb_draw_image(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);

  // Image is already available or pending a pipe rendering/cache fetching:
  // don't query a new image buffer.
  if((!thumb->image_inited || !thumb->img_surf) && !thumb->busy)
  {
    _get_image_buffer(thumb);
  }

  dt_print(DT_DEBUG_LIGHTTABLE, "[lighttable] redrawing thumbnail %i\n", thumb->imgid);

  int w = 0;
  int h = 0;
  gtk_widget_get_size_request(thumb->w_image, &w, &h);

  if(thumb->busy || !thumb->image_inited || !thumb->img_surf || cairo_surface_get_reference_count(thumb->img_surf) < 1)
  {
    dt_control_draw_busy_msg(cr, w, h);
    return TRUE;
  }

  // we draw the image
  GtkStyleContext *context = gtk_widget_get_style_context(thumb->w_image);

  cairo_save(cr);
  const float scaler = 1.0f / darktable.gui->ppd;
  cairo_scale(cr, scaler, scaler);

  double x_offset = (w * darktable.gui->ppd - thumb->img_width) / 2.;
  double y_offset = (h * darktable.gui->ppd - thumb->img_height) / 2.;

  // Sanitize zoom offsets
  if(thumb->table && thumb->table->zoom > DT_THUMBTABLE_ZOOM_FIT)
  {
    thumb->zoomx = CLAMP(thumb->zoomx, -fabs(x_offset), fabs(x_offset));
    thumb->zoomy = CLAMP(thumb->zoomy, -fabs(y_offset), fabs(y_offset));
  }
  else
  {
    thumb->zoomx = 0.;
    thumb->zoomy = 0.;
  }

  cairo_set_source_surface(cr, thumb->img_surf, thumb->zoomx + x_offset, thumb->zoomy + y_offset);

  // get the transparency value
  GdkRGBA im_color;
  gtk_style_context_get_color(context, gtk_widget_get_state_flags(thumb->w_image), &im_color);
  cairo_paint_with_alpha(cr, im_color.alpha);

  // and eventually the image border
  gtk_render_frame(context, cr, 0, 0, w * darktable.gui->ppd, h * darktable.gui->ppd);
  cairo_restore(cr);

  return TRUE;
}


static void _thumb_update_icons(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb);
  if(!thumb->widget) return;

  gboolean show_nowhere = (thumb->over == DT_THUMBNAIL_OVERLAYS_NONE);

  gtk_widget_set_visible(thumb->w_local_copy, thumb->has_localcopy && !show_nowhere);
  gtk_widget_set_visible(thumb->w_altered, thumb->is_altered && !show_nowhere);
  gtk_widget_set_visible(thumb->w_group, thumb->is_grouped && !show_nowhere);
  gtk_widget_set_visible(thumb->w_audio, thumb->has_audio && !show_nowhere);
  gtk_widget_set_visible(thumb->w_color, thumb->colorlabels != 0 && !show_nowhere);
  gtk_widget_set_visible(thumb->w_bottom_eb, !show_nowhere);
  gtk_widget_set_visible(thumb->w_reject, !show_nowhere);
  gtk_widget_set_visible(thumb->w_ext, !show_nowhere);
  gtk_widget_show(thumb->w_cursor);

  _set_flag(thumb->w_main, GTK_STATE_FLAG_PRELIGHT, thumb->mouse_over);
  _set_flag(thumb->widget, GTK_STATE_FLAG_PRELIGHT, thumb->mouse_over);

  _set_flag(thumb->w_reject, GTK_STATE_FLAG_ACTIVE, (thumb->rating == DT_VIEW_REJECT));

  for(int i = 0; i < MAX_STARS; i++)
  {
    gtk_widget_set_visible(thumb->w_stars[i], !show_nowhere);
    _set_flag(thumb->w_stars[i], GTK_STATE_FLAG_ACTIVE, (thumb->rating > i && thumb->rating < DT_VIEW_REJECT));
  }

  _set_flag(thumb->w_group, GTK_STATE_FLAG_ACTIVE, (thumb->imgid == thumb->groupid));
  _set_flag(thumb->w_main, GTK_STATE_FLAG_SELECTED, thumb->selected);
  _set_flag(thumb->widget, GTK_STATE_FLAG_SELECTED, thumb->selected);
}

static gboolean _event_main_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(!gtk_widget_is_visible(thumb->widget)) return TRUE;

  // Ensure mouse_over_id is set because that's what darkroom uses to open a picture.
  // NOTE: Duplicate module uses that fucking thumbnail without a table...
  if(thumb->table)
    dt_thumbtable_dispatch_over(thumb->table, event->type, thumb->imgid);
  else
    dt_control_set_mouse_over_id(thumb->imgid);

  // raise signal on double click
  if(event->button == 1 && event->type == GDK_2BUTTON_PRESS)
  {
    thumb->dragging = FALSE;
    DT_DEBUG_CONTROL_SIGNAL_RAISE(darktable.signals, DT_SIGNAL_VIEWMANAGER_THUMBTABLE_ACTIVATE, thumb->imgid);
    return TRUE;
  }

  return FALSE;
}

static gboolean _event_main_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  thumb->dragging = FALSE;

  // select on single click only in filemanager mode. Filmstrip mode only raises ACTIVATE signals.
  if(event->button == 1
     && thumb->table && thumb->table->mode == DT_THUMBTABLE_MODE_FILEMANAGER)
  {
    if(dt_modifier_is(event->state, 0))
      dt_selection_select_single(darktable.selection, thumb->imgid);
    else if(dt_modifier_is(event->state, GDK_CONTROL_MASK))
      dt_selection_toggle(darktable.selection, thumb->imgid);
    else if(dt_modifier_is(event->state, GDK_SHIFT_MASK) && thumb->table)
      dt_thumbtable_select_range(thumb->table, thumb->rowid);
    // Because selection might include several images, we handle styling globally
    // in the thumbtable scope, catching the SELECTION_CHANGED signal.
    return TRUE;
  }

  return FALSE;
}

static gboolean _event_rating_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(thumb->disable_actions) return FALSE;
  if(dtgtk_thumbnail_btn_is_hidden(widget)) return FALSE;

  if(event->button == 1)
  {
    dt_view_image_over_t rating = DT_VIEW_DESERT;
    if(widget == thumb->w_reject)
      rating = DT_VIEW_REJECT;
    else if(widget == thumb->w_stars[0])
      rating = DT_VIEW_STAR_1;
    else if(widget == thumb->w_stars[1])
      rating = DT_VIEW_STAR_2;
    else if(widget == thumb->w_stars[2])
      rating = DT_VIEW_STAR_3;
    else if(widget == thumb->w_stars[3])
      rating = DT_VIEW_STAR_4;
    else if(widget == thumb->w_stars[4])
      rating = DT_VIEW_STAR_5;

    if(rating != DT_VIEW_DESERT)
      dt_ratings_apply_on_image(thumb->imgid, rating, TRUE, TRUE, TRUE);
  }
  return TRUE;
}

static gboolean _event_grouping_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(thumb->disable_actions) return FALSE;
  if(dtgtk_thumbnail_btn_is_hidden(widget)) return FALSE;
  return FALSE;
}

static gboolean _event_audio_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(thumb->disable_actions) return FALSE;
  if(dtgtk_thumbnail_btn_is_hidden(widget)) return FALSE;

  if(event->button == 1)
  {
    gboolean start_audio = TRUE;
    if(darktable.view_manager->audio.audio_player_id != -1)
    {
      // don't start the audio for the image we just killed it for
      if(darktable.view_manager->audio.audio_player_id == thumb->imgid) start_audio = FALSE;
      dt_view_audio_stop(darktable.view_manager);
    }

    if(start_audio)
    {
      dt_view_audio_start(darktable.view_manager, thumb->imgid);
    }
  }
  return FALSE;
}



void dt_thumbnail_update_selection(dt_thumbnail_t *thumb, gboolean selected)
{
  thumb_return_if_fails(thumb);
  if(selected != thumb->selected)
  {
    thumb->selected = selected;
    _thumb_update_icons(thumb);
  }
}


// All the text info that we don't have room to display around the image
void _create_alternative_view(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb);
  gtk_label_set_text(GTK_LABEL(thumb->w_filename), thumb->filename);
  gtk_label_set_text(GTK_LABEL(thumb->w_datetime), thumb->datetime);

  const gchar *exposure_field = g_strdup_printf("%.0f ISO - f/%.1f - %s", thumb->iso, thumb->aperture,
                                                dt_util_format_exposure(thumb->speed));

  gtk_label_set_text(GTK_LABEL(thumb->w_exposure_bias), g_strdup_printf("%+.1f EV", thumb->exposure_bias));
  gtk_label_set_text(GTK_LABEL(thumb->w_exposure), exposure_field);
  gtk_label_set_text(GTK_LABEL(thumb->w_camera), thumb->camera);
  gtk_label_set_text(GTK_LABEL(thumb->w_lens), thumb->lens);
  gtk_label_set_text(GTK_LABEL(thumb->w_focal), g_strdup_printf("%0.f mm @ %.2f m", thumb->focal, thumb->focus_distance));
}


void dt_thumbnail_alternative_mode(dt_thumbnail_t *thumb, gboolean enable)
{
  thumb_return_if_fails(thumb);
  if(thumb->alternative_mode == enable) return;
  thumb->alternative_mode = enable;
  if(enable)
    gtk_widget_show_all(thumb->w_alternative);
  else
    gtk_widget_hide(thumb->w_alternative);
  gtk_widget_queue_draw(thumb->widget);
}


static gboolean _event_star_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(thumb->disable_actions) return TRUE;
  _set_flag(thumb->w_bottom_eb, GTK_STATE_FLAG_PRELIGHT, TRUE);

  // we prelight all stars before the current one
  gboolean pre = TRUE;
  for(int i = 0; i < MAX_STARS; i++)
  {
    _set_flag(thumb->w_stars[i], GTK_STATE_FLAG_PRELIGHT, pre);

    // We don't want the active state to overlap the prelight one because
    // it makes the feature hard to read/understand.
    _set_flag(thumb->w_stars[i], GTK_STATE_FLAG_ACTIVE, FALSE);

    if(thumb->w_stars[i] == widget) pre = FALSE;
  }
  return TRUE;
}


static gboolean _event_star_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(thumb->disable_actions) return TRUE;

  for(int i = 0; i < MAX_STARS; i++)
  {
    _set_flag(thumb->w_stars[i], GTK_STATE_FLAG_PRELIGHT, FALSE);

    // restore active state
    _set_flag(thumb->w_stars[i], GTK_STATE_FLAG_ACTIVE, i < thumb->rating && thumb->rating < DT_VIEW_REJECT);
  }
  return TRUE;
}


gboolean _event_expose(GtkWidget *self, cairo_t *cr, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  return FALSE;
}

static gboolean _event_main_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(!gtk_widget_is_visible(thumb->widget)) return TRUE;
  if(!thumb->mouse_over)
  {
    // Thumbnails send leave-notify when in the thumbnail frame but over the image.
    // If we lost the mouse-over in this case, grab it again from mouse motion.
    // Be conservative with sending mouse_over_id events/signal because many
    // places in the soft listen to them and refresh stuff from DB, so it's expensive.
    if(thumb->table)
      dt_thumbtable_dispatch_over(thumb->table, event->type, thumb->imgid);
    else
      dt_control_set_mouse_over_id(thumb->imgid);

    dt_thumbnail_set_mouseover(thumb, TRUE);
  }
  return FALSE;
}

static gboolean _event_main_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(!gtk_widget_is_visible(thumb->widget)) return TRUE;

  if(thumb->table)
    dt_thumbtable_dispatch_over(thumb->table, event->type, thumb->imgid);
  else
    dt_control_set_mouse_over_id(thumb->imgid);

  dt_thumbnail_set_mouseover(thumb, TRUE);
  return FALSE;
}

static gboolean _event_main_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(!gtk_widget_is_visible(thumb->widget)) return TRUE;

  if(thumb->table)
    dt_thumbtable_dispatch_over(thumb->table, event->type, -1);
  else
    dt_control_set_mouse_over_id(-1);

  dt_thumbnail_set_mouseover(thumb, FALSE);
  return FALSE;
}

// lazy-load the history tooltip only when mouse enters the button
static gboolean _altered_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(thumb->is_altered)
  {
    char *tooltip = dt_history_get_items_as_string(thumb->imgid);
    if(tooltip)
    {
      gtk_widget_set_tooltip_text(thumb->w_altered, tooltip);
      g_free(tooltip);
    }
  }
  return FALSE;
}

static gboolean _event_image_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);

  if(event->button == 1 && thumb->table && thumb->table->zoom > DT_THUMBTABLE_ZOOM_FIT)
  {
    thumb->dragging = TRUE;
    thumb->drag_x_start = event->x;
    thumb->drag_y_start = event->y;
  }

  return FALSE;
}

static gboolean _event_image_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  if(thumb->dragging)
  {
    const double delta_x = (event->x - thumb->drag_x_start) * darktable.gui->ppd;
    const double delta_y = (event->y - thumb->drag_y_start) * darktable.gui->ppd;
    const gboolean global_shift = dt_modifier_is(event->state, GDK_SHIFT_MASK) && thumb->table;

    if(global_shift)
    {
      // Offset all thumbnails by this amount
      dt_thumbtable_offset_zoom(thumb->table, delta_x, delta_y);
    }
    else
    {
      // Offset only the current thumbnail
      thumb->zoomx += delta_x;
      thumb->zoomy += delta_y;
    }

    // Reset drag origin
    thumb->drag_x_start = event->x;
    thumb->drag_y_start = event->y;

    if(!global_shift)
      gtk_widget_queue_draw(thumb->w_image);

    return TRUE;
  }
  return FALSE;
}

static gboolean _event_image_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  dt_thumbnail_t *thumb = (dt_thumbnail_t *)user_data;
  thumb_return_if_fails(thumb, TRUE);
  thumb->dragging = FALSE;
  return FALSE;
}

GtkWidget *dt_thumbnail_create_widget(dt_thumbnail_t *thumb)
{
  // Let the background event box capture all user events from its children first,
  // so we don't have to wire leave/enter events to all of them individually.
  // Children buttons will mostly only use button pressed/released events
  thumb->widget = gtk_event_box_new();
  dt_gui_add_class(thumb->widget, "thumb-main");
  gtk_widget_set_events(thumb->widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_STRUCTURE_MASK | GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

  // this is only here to ensure that mouse-over value is updated correctly
  // all dragging actions take place inside thumbatble.c
  gtk_drag_dest_set(thumb->widget, GTK_DEST_DEFAULT_MOTION, target_list_all, n_targets_all, GDK_ACTION_MOVE);
  g_object_set_data(G_OBJECT(thumb->widget), "thumb", thumb);
  gtk_widget_show(thumb->widget);

  g_signal_connect(G_OBJECT(thumb->widget), "button-press-event", G_CALLBACK(_event_main_press), thumb);
  g_signal_connect(G_OBJECT(thumb->widget), "button-release-event", G_CALLBACK(_event_main_release), thumb);
  g_signal_connect(G_OBJECT(thumb->widget), "enter-notify-event", G_CALLBACK(_event_main_enter), thumb);
  g_signal_connect(G_OBJECT(thumb->widget), "leave-notify-event", G_CALLBACK(_event_main_leave), thumb);
  g_signal_connect(G_OBJECT(thumb->widget), "motion-notify-event", G_CALLBACK(_event_main_motion), thumb);
  g_signal_connect(G_OBJECT(thumb->widget), "draw", G_CALLBACK(_event_expose), thumb);

  // Main widget
  thumb->w_main = gtk_overlay_new();
  _thumb_update_rating_class(thumb);
  gtk_container_add(GTK_CONTAINER(thumb->widget), thumb->w_main);
  gtk_widget_show(thumb->w_main);

  // the file extension label
  thumb->w_ext = gtk_label_new("");
  dt_gui_add_class(thumb->w_ext, "thumb-ext");
  gtk_widget_set_valign(thumb->w_ext, GTK_ALIGN_START);
  gtk_widget_set_halign(thumb->w_ext, GTK_ALIGN_START);
  gtk_label_set_justify(GTK_LABEL(thumb->w_ext), GTK_JUSTIFY_CENTER);
  gtk_widget_show(thumb->w_ext);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_ext);
  gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(thumb->w_main), thumb->w_ext, TRUE);

  // triangle to indicate current image(s) in filmstrip
  thumb->w_cursor = gtk_drawing_area_new();
  dt_gui_add_class(thumb->w_cursor, "thumb-cursor");
  gtk_widget_set_valign(thumb->w_cursor, GTK_ALIGN_START);
  gtk_widget_set_halign(thumb->w_cursor, GTK_ALIGN_CENTER);
  g_signal_connect(G_OBJECT(thumb->w_cursor), "draw", G_CALLBACK(_event_cursor_draw), thumb);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_cursor);

  // the image drawing area
  thumb->w_image = gtk_drawing_area_new();
  dt_gui_add_class(thumb->w_image, "thumb-image");
  gtk_widget_set_valign(thumb->w_image, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(thumb->w_image, GTK_ALIGN_CENTER);
  gtk_widget_set_events(thumb->w_image, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
  g_signal_connect(G_OBJECT(thumb->w_image), "draw", G_CALLBACK(_thumb_draw_image), thumb);
  g_signal_connect(G_OBJECT(thumb->w_image), "button-press-event", G_CALLBACK(_event_image_press), thumb);
  g_signal_connect(G_OBJECT(thumb->w_image), "button-release-event", G_CALLBACK(_event_image_release), thumb);
  g_signal_connect(G_OBJECT(thumb->w_image), "motion-notify-event", G_CALLBACK(_event_image_motion), thumb);
  gtk_widget_show(thumb->w_image);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_image);
  gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(thumb->w_main), thumb->w_image, TRUE);

  // determine the overlays parents
  // the infos background
  thumb->w_bottom_eb = gtk_event_box_new();
  dt_gui_add_class(thumb->w_bottom_eb, "thumb-bottom");
  gtk_widget_set_valign(thumb->w_bottom_eb, GTK_ALIGN_END);
  gtk_widget_set_halign(thumb->w_bottom_eb, GTK_ALIGN_CENTER);
  gtk_widget_show(thumb->w_bottom_eb);

  thumb->w_bottom = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(thumb->w_bottom), "");
  dt_gui_add_class(thumb->w_bottom, "thumb-bottom-label");
  gtk_widget_show(thumb->w_bottom);
  gtk_label_set_yalign(GTK_LABEL(thumb->w_bottom), 0.05);
  gtk_label_set_ellipsize(GTK_LABEL(thumb->w_bottom), PANGO_ELLIPSIZE_MIDDLE);
  gtk_container_add(GTK_CONTAINER(thumb->w_bottom_eb), thumb->w_bottom);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_bottom_eb);

  // the reject icon
  thumb->w_reject = dtgtk_thumbnail_btn_new(dtgtk_cairo_paint_reject, 0, NULL);
  dt_gui_add_class(thumb->w_reject, "thumb-reject");
  gtk_widget_set_valign(thumb->w_reject, GTK_ALIGN_END);
  gtk_widget_set_halign(thumb->w_reject, GTK_ALIGN_START);
  gtk_widget_show(thumb->w_reject);
  g_signal_connect(G_OBJECT(thumb->w_reject), "button-release-event", G_CALLBACK(_event_rating_release), thumb);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_reject);

  // the stars
  for(int i = 0; i < MAX_STARS; i++)
  {
    thumb->w_stars[i] = dtgtk_thumbnail_btn_new(dtgtk_cairo_paint_star, 0, NULL);
    g_signal_connect(G_OBJECT(thumb->w_stars[i]), "enter-notify-event", G_CALLBACK(_event_star_enter), thumb);
    g_signal_connect(G_OBJECT(thumb->w_stars[i]), "leave-notify-event", G_CALLBACK(_event_star_leave), thumb);
    g_signal_connect(G_OBJECT(thumb->w_stars[i]), "button-release-event", G_CALLBACK(_event_rating_release),
                      thumb);
    dt_gui_add_class(thumb->w_stars[i], "thumb-star");
    gtk_widget_set_valign(thumb->w_stars[i], GTK_ALIGN_END);
    gtk_widget_set_halign(thumb->w_stars[i], GTK_ALIGN_START);
    gtk_widget_show(thumb->w_stars[i]);
    gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_stars[i]);
  }

  // the color labels
  thumb->w_color = dtgtk_thumbnail_btn_new(dtgtk_cairo_paint_label_flower, thumb->colorlabels, NULL);
  dt_gui_add_class(thumb->w_color, "thumb-colorlabels");
  gtk_widget_set_valign(thumb->w_color, GTK_ALIGN_END);
  gtk_widget_set_halign(thumb->w_color, GTK_ALIGN_END);
  gtk_widget_set_no_show_all(thumb->w_color, TRUE);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_color);

  // the local copy indicator
  thumb->w_local_copy = dtgtk_thumbnail_btn_new(dtgtk_cairo_paint_local_copy, 0, NULL);
  dt_gui_add_class(thumb->w_local_copy, "thumb-localcopy");
  gtk_widget_set_tooltip_text(thumb->w_local_copy, _("local copy"));
  gtk_widget_set_valign(thumb->w_local_copy, GTK_ALIGN_START);
  gtk_widget_set_halign(thumb->w_local_copy, GTK_ALIGN_END);
  gtk_widget_set_no_show_all(thumb->w_local_copy, TRUE);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_local_copy);

  // the altered icon
  thumb->w_altered = dtgtk_thumbnail_btn_new(dtgtk_cairo_paint_altered, 0, NULL);
  g_signal_connect(G_OBJECT(thumb->w_altered), "enter-notify-event", G_CALLBACK(_altered_enter), thumb);
  dt_gui_add_class(thumb->w_altered, "thumb-altered");
  gtk_widget_set_valign(thumb->w_altered, GTK_ALIGN_START);
  gtk_widget_set_halign(thumb->w_altered, GTK_ALIGN_END);
  gtk_widget_set_no_show_all(thumb->w_altered, TRUE);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_altered);

  // the group bouton
  thumb->w_group = dtgtk_thumbnail_btn_new(dtgtk_cairo_paint_grouping, 0, NULL);
  dt_gui_add_class(thumb->w_group, "thumb-group");
  g_signal_connect(G_OBJECT(thumb->w_group), "button-release-event", G_CALLBACK(_event_grouping_release), thumb);
  gtk_widget_set_valign(thumb->w_group, GTK_ALIGN_START);
  gtk_widget_set_halign(thumb->w_group, GTK_ALIGN_END);
  gtk_widget_set_no_show_all(thumb->w_group, TRUE);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_group);

  // the sound icon
  thumb->w_audio = dtgtk_thumbnail_btn_new(dtgtk_cairo_paint_audio, 0, NULL);
  dt_gui_add_class(thumb->w_audio, "thumb-audio");
  g_signal_connect(G_OBJECT(thumb->w_audio), "button-release-event", G_CALLBACK(_event_audio_release), thumb);
  gtk_widget_set_valign(thumb->w_audio, GTK_ALIGN_START);
  gtk_widget_set_halign(thumb->w_audio, GTK_ALIGN_END);
  gtk_widget_set_no_show_all(thumb->w_audio, TRUE);
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_audio);

  thumb->w_alternative = gtk_overlay_new();
  gtk_overlay_add_overlay(GTK_OVERLAY(thumb->w_main), thumb->w_alternative);
  gtk_widget_set_halign(thumb->w_alternative, GTK_ALIGN_FILL);
  gtk_widget_set_valign(thumb->w_alternative, GTK_ALIGN_FILL);
  gtk_widget_hide(thumb->w_alternative);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(thumb->w_alternative), box);
  dt_gui_add_class(box, "thumb-alternative");

  GtkWidget *bbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_valign(bbox, GTK_ALIGN_START);
  gtk_box_pack_start(GTK_BOX(box), bbox, TRUE, TRUE, 0);
  thumb->w_filename = gtk_label_new("");
  gtk_label_set_ellipsize(GTK_LABEL(thumb->w_filename), PANGO_ELLIPSIZE_MIDDLE);
  gtk_box_pack_start(GTK_BOX(bbox), thumb->w_filename, FALSE, FALSE, 0);
  thumb->w_datetime = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(bbox), thumb->w_datetime, FALSE, FALSE, 0);

  bbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_valign(bbox, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(box), bbox, TRUE, TRUE, 0);
  thumb->w_exposure = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(bbox), thumb->w_exposure, FALSE, FALSE, 0);
  thumb->w_exposure_bias = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(bbox), thumb->w_exposure_bias, FALSE, FALSE, 0);

  bbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_valign(bbox, GTK_ALIGN_END);
  gtk_box_pack_start(GTK_BOX(box), bbox, TRUE, TRUE, 0);
  thumb->w_camera = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(bbox), thumb->w_camera, FALSE, FALSE, 0);
  thumb->w_lens = gtk_label_new("");
  gtk_label_set_ellipsize(GTK_LABEL(thumb->w_lens), PANGO_ELLIPSIZE_MIDDLE);
  gtk_box_pack_start(GTK_BOX(bbox), thumb->w_lens, FALSE, FALSE, 0);
  thumb->w_focal = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(bbox), thumb->w_focal, FALSE, FALSE, 0);
  //gtk_widget_set_no_show_all(thumb->w_alternative, TRUE);

  return thumb->widget;
}

dt_thumbnail_t *dt_thumbnail_new(int32_t imgid, int rowid, int32_t groupid,
                                 dt_thumbnail_overlay_t over, dt_thumbtable_t *table)
{
  dt_thumbnail_t *thumb = calloc(1, sizeof(dt_thumbnail_t));

  thumb->imgid = imgid;
  thumb->rowid = rowid;
  thumb->groupid = groupid;
  thumb->over = over;
  thumb->table = table;
  thumb->zoomx = 0.;
  thumb->zoomy = 0.;

  // we create the widget
  dt_thumbnail_create_widget(thumb);

  // Query ratings, extension and such.
  // This will then only run on "image_info_changed" event.
  dt_thumbnail_update_infos(thumb);

  // This will then only run on "selection_changed" event
  dt_thumbnail_update_selection(thumb, dt_selection_is_id_selected(darktable.selection, thumb->imgid));

  return thumb;
}

int dt_thumbnail_destroy(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb, 0);

  // remove multiple delayed gtk_widget_queue_draw triggers
  while(g_idle_remove_by_data(thumb))
  ;
  while(g_idle_remove_by_data(thumb->widget))
  ;

  if(thumb->img_surf && cairo_surface_get_reference_count(thumb->img_surf) > 0)
    cairo_surface_destroy(thumb->img_surf);
  thumb->img_surf = NULL;

  if(thumb->widget)
    gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(thumb->widget)), thumb->widget);
  thumb->widget = NULL;

  if(thumb->filename) g_free(thumb->filename);
  thumb->filename = NULL;

  if(thumb->info_line) g_free(thumb->info_line);
  thumb->info_line = NULL;

  if(thumb->img_margin) gtk_border_free(thumb->img_margin);
  thumb->img_margin = NULL;

  free(thumb);
  thumb = NULL;

  return 0;
}

void dt_thumbnail_update_infos(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb);
  _image_get_infos(thumb);
  _thumb_update_icons(thumb);
  _create_alternative_view(thumb);
}

void dt_thumbnail_set_overlay(dt_thumbnail_t *thumb, dt_thumbnail_overlay_t mode)
{
  thumb_return_if_fails(thumb);
  if(mode == thumb->over)
  {
    thumb->over = mode;
    _thumb_update_icons(thumb);
  }
}

static void _thumb_resize_overlays(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb);
  int width = 0;
  int height = 0;

  int max_size = darktable.gui->icon_size;
  if(max_size < 2)
    max_size = round(1.2f * darktable.bauhaus->line_height); // fallback if toolbar icons are not realized

  gtk_widget_get_size_request(thumb->w_main, &width, &height);
  // we need to squeeze reject + space + stars + space + colorlabels icons on a thumbnail width
  // that means a width of 4 + MAX_STARS icons size
  // all icons and spaces having a width of 2.5 * r1
  // inner margins are defined in css (margin_* values)

  // retrieves the size of the main icons in the top panel, thumbtable overlays shall not exceed that
  const float r1 = fminf(max_size / 2.0f,
                          (width - thumb->img_margin->left - thumb->img_margin->right) / (2.5 * (4 + MAX_STARS)));
  const int icon_size = roundf(2.5 * r1);

  // file extension
  gtk_widget_set_margin_top(thumb->w_ext, thumb->img_margin->top);
  gtk_widget_set_margin_start(thumb->w_ext, thumb->img_margin->left);

  // bottom background
  gtk_widget_set_margin_start(thumb->w_bottom, thumb->img_margin->left);
  gtk_widget_set_margin_end(thumb->w_bottom, thumb->img_margin->right);
  gtk_widget_set_size_request(thumb->w_bottom_eb, width, icon_size * 0.75 + 2 * thumb->img_margin->bottom);

  gtk_label_set_xalign(GTK_LABEL(thumb->w_bottom), 0.5);
  gtk_label_set_yalign(GTK_LABEL(thumb->w_bottom), 0);
  gtk_widget_set_margin_top(thumb->w_bottom, thumb->img_margin->bottom);
  gtk_widget_set_valign(thumb->w_bottom_eb, GTK_ALIGN_END);
  gtk_widget_set_halign(thumb->w_bottom_eb, GTK_ALIGN_CENTER);

  // reject icon
  const int margin_b_icons = MAX(0, thumb->img_margin->bottom - icon_size * 0.125 - 1);
  gtk_widget_set_size_request(thumb->w_reject, icon_size, icon_size);
  gtk_widget_set_valign(thumb->w_reject, GTK_ALIGN_END);
  int pos = MAX(0, thumb->img_margin->left - icon_size * 0.125); // align on the left of the thumb
  gtk_widget_set_margin_start(thumb->w_reject, pos);
  gtk_widget_set_margin_bottom(thumb->w_reject, margin_b_icons);

  // stars
  for(int i = 0; i < MAX_STARS; i++)
  {
    gtk_widget_set_size_request(thumb->w_stars[i], icon_size, icon_size);
    gtk_widget_set_valign(thumb->w_stars[i], GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(thumb->w_stars[i], margin_b_icons);
    gtk_widget_set_margin_start(
        thumb->w_stars[i],
        thumb->img_margin->left
            + (width - thumb->img_margin->left - thumb->img_margin->right - MAX_STARS * icon_size) * 0.5
            + i * icon_size);
  }

  // the color labels
  gtk_widget_set_size_request(thumb->w_color, icon_size, icon_size);
  gtk_widget_set_valign(thumb->w_color, GTK_ALIGN_END);
  gtk_widget_set_halign(thumb->w_color, GTK_ALIGN_START);
  gtk_widget_set_margin_bottom(thumb->w_color, margin_b_icons);
  pos = width - thumb->img_margin->right - icon_size + icon_size * 0.125; // align on the right
  gtk_widget_set_margin_start(thumb->w_color, pos);

  // the local copy indicator
  _set_flag(thumb->w_local_copy, GTK_STATE_FLAG_ACTIVE, FALSE);
  gtk_widget_set_size_request(thumb->w_local_copy, 1.618 * r1, 1.618 * r1);
  gtk_widget_set_halign(thumb->w_local_copy, GTK_ALIGN_END);

  // the altered icon
  gtk_widget_set_size_request(thumb->w_altered, 2.0 * r1, 2.0 * r1);
  gtk_widget_set_halign(thumb->w_altered, GTK_ALIGN_END);
  gtk_widget_set_margin_top(thumb->w_altered, thumb->img_margin->top);
  gtk_widget_set_margin_end(thumb->w_altered, thumb->img_margin->right);

  // the group bouton
  gtk_widget_set_size_request(thumb->w_group, 2.0 * r1, 2.0 * r1);
  gtk_widget_set_halign(thumb->w_group, GTK_ALIGN_END);
  gtk_widget_set_margin_top(thumb->w_group, thumb->img_margin->top);
  gtk_widget_set_margin_end(thumb->w_group, thumb->img_margin->right + 2.5 * r1);

  // the sound icon
  gtk_widget_set_size_request(thumb->w_audio, 2.0 * r1, 2.0 * r1);
  gtk_widget_set_halign(thumb->w_audio, GTK_ALIGN_END);
  gtk_widget_set_margin_top(thumb->w_audio, thumb->img_margin->top);
  gtk_widget_set_margin_end(thumb->w_audio, thumb->img_margin->right + 5.0 * r1);

  // the filmstrip cursor
  gtk_widget_set_size_request(thumb->w_cursor, 6.0 * r1, 1.5 * r1);
}

// This function is called only from the thumbtable, when the grid size changed.
void dt_thumbnail_resize(dt_thumbnail_t *thumb, int width, int height, gboolean force)
{
  thumb_return_if_fails(thumb);
  //fprintf(stdout, "calling resize on %i\n", thumb->imgid);

  int w = 0;
  int h = 0;
  gtk_widget_get_size_request(thumb->w_main, &w, &h);

  // first, we verify that there's something to change
  if(!force && w == width && h == height) return;

  // Flush the image surface
  thumb->image_inited = FALSE;

  // widget resizing
  thumb->width = width;
  thumb->height = height;
  gtk_widget_set_size_request(thumb->w_main, width, height);

  // file extension
  _thumb_retrieve_margins(thumb);
  gtk_widget_set_margin_start(thumb->w_ext, thumb->img_margin->left);
  gtk_widget_set_margin_top(thumb->w_ext, thumb->img_margin->top);

  // retrieves the size of the main icons in the top panel, thumbtable overlays shall not exceed that
  int max_size = darktable.gui->icon_size;
  if(max_size < 2)
    max_size = round(1.2f * darktable.bauhaus->line_height); // fallback if toolbar icons are not realized

  const int fsize = fminf(max_size, (height - thumb->img_margin->top - thumb->img_margin->bottom) / 11.0f);

  PangoAttrList *attrlist = pango_attr_list_new();
  PangoAttribute *attr = pango_attr_size_new_absolute(fsize * PANGO_SCALE);
  pango_attr_list_insert(attrlist, attr);
  // the idea is to reduce line-height, but it doesn't work for whatever reason...
  // PangoAttribute *attr2 = pango_attr_rise_new(-fsize * PANGO_SCALE);
  // pango_attr_list_insert(attrlist, attr2);
  gtk_label_set_attributes(GTK_LABEL(thumb->w_ext), attrlist);
  pango_attr_list_unref(attrlist);

  // for overlays different than block, we compute their size here, so we have valid value for th image area compute
  _thumb_resize_overlays(thumb);

  // we change the size and margins according to the size change. This will be refined after
  _thumb_set_image_area(thumb);
}

void dt_thumbnail_set_group_border(dt_thumbnail_t *thumb, dt_thumbnail_border_t border)
{
  thumb_return_if_fails(thumb);

  if(border == DT_THUMBNAIL_BORDER_NONE)
  {
    dt_gui_remove_class(thumb->w_main, "dt_group_left");
    dt_gui_remove_class(thumb->w_main, "dt_group_top");
    dt_gui_remove_class(thumb->w_main, "dt_group_right");
    dt_gui_remove_class(thumb->w_main, "dt_group_bottom");
    thumb->group_borders = DT_THUMBNAIL_BORDER_NONE;
    return;
  }
  else if(border & DT_THUMBNAIL_BORDER_LEFT)
    dt_gui_add_class(thumb->w_main, "dt_group_left");
  else if(border & DT_THUMBNAIL_BORDER_TOP)
    dt_gui_add_class(thumb->w_main, "dt_group_top");
  else if(border & DT_THUMBNAIL_BORDER_RIGHT)
    dt_gui_add_class(thumb->w_main, "dt_group_right");
  else if(border & DT_THUMBNAIL_BORDER_BOTTOM)
    dt_gui_add_class(thumb->w_main, "dt_group_bottom");

  thumb->group_borders |= border;
}

void dt_thumbnail_set_mouseover(dt_thumbnail_t *thumb, gboolean over)
{
  thumb_return_if_fails(thumb);

  if(thumb->mouse_over == over) return;
  thumb->mouse_over = over;
  if(thumb->table) thumb->table->rowid = thumb->rowid;

  _set_flag(thumb->widget, GTK_STATE_FLAG_PRELIGHT, thumb->mouse_over);
  _set_flag(thumb->w_bottom_eb, GTK_STATE_FLAG_PRELIGHT, thumb->mouse_over);
  _set_flag(thumb->w_main, GTK_STATE_FLAG_PRELIGHT, thumb->mouse_over);

  _thumb_update_icons(thumb);
}

// set if the thumbnail should react (mouse_over) to drag and drop
// note that it's just cosmetic as dropping occurs in thumbtable in any case
void dt_thumbnail_set_drop(dt_thumbnail_t *thumb, gboolean accept_drop)
{
  thumb_return_if_fails(thumb);

  if(accept_drop)
    gtk_drag_dest_set(thumb->w_main, GTK_DEST_DEFAULT_MOTION, target_list_all, n_targets_all, GDK_ACTION_MOVE);
  else
    gtk_drag_dest_unset(thumb->w_main);
}

// Apply new mipmap on thumbnail
int dt_thumbnail_image_refresh_real(dt_thumbnail_t *thumb)
{
  thumb_return_if_fails(thumb, G_SOURCE_REMOVE);
  _thumb_update_icons(thumb);
  thumb->busy = FALSE;
  thumb->drawn = FALSE;
  gtk_widget_queue_draw(thumb->w_image);
  return G_SOURCE_REMOVE;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
