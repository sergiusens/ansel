/*
    This file is part of darktable,
    Copyright (C) 2011-2022 darktable developers.

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

#include "common/collection.h"
#include "common/selection.h"
#include "common/darktable.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/develop.h"

#include "gui/gtk.h"
#include "dtgtk/button.h"
#include "libs/lib.h"
#include "libs/lib_api.h"
#include "bauhaus/bauhaus.h"

DT_MODULE(1)

typedef struct dt_lib_tool_filter_t
{
  GtkWidget *stars[8];
  GtkWidget *comparator;
  GtkWidget *sort;
  GtkWidget *reverse;
  GtkWidget *text;
  GtkWidget *colors[6];
  GtkWidget *culling;
  GtkWidget *refresh;
  int time_out;
  double last_key_time;
} dt_lib_tool_filter_t;

#ifdef USE_LUA
typedef enum dt_collection_sort_order_t
{
  DT_COLLECTION_SORT_ORDER_ASCENDING = 0,
  DT_COLLECTION_SORT_ORDER_DESCENDING
} dt_collection_sort_order_t;
#endif

/* proxy function to intelligently reset filter */
static void _lib_filter_reset(dt_lib_module_t *self, gboolean smart_filter);

/* callback for sort combobox change */
static void _lib_filter_sort_combobox_changed(GtkWidget *widget, gpointer user_data);
/* callback for reverse sort check button change */
static void _lib_filter_reverse_button_changed(GtkDarktableToggleButton *widget, gpointer user_data);
/* updates the query and redraws the view */
static void _lib_filter_update_query(dt_lib_module_t *self, dt_collection_properties_t changed_property);

/* save the images order if the first collect filter is on tag*/
static void _lib_filter_set_tag_order(dt_lib_module_t *self);
/* images order change from outside */
static void _lib_filter_images_order_change(gpointer instance, int order, dt_lib_module_t *self);

const dt_collection_sort_t items[] =
{
  DT_COLLECTION_SORT_FILENAME,
  DT_COLLECTION_SORT_DATETIME,
  DT_COLLECTION_SORT_IMPORT_TIMESTAMP,
  DT_COLLECTION_SORT_CHANGE_TIMESTAMP,
  DT_COLLECTION_SORT_EXPORT_TIMESTAMP,
  DT_COLLECTION_SORT_PRINT_TIMESTAMP,
  DT_COLLECTION_SORT_RATING,
  DT_COLLECTION_SORT_ID,
  DT_COLLECTION_SORT_COLOR,
  DT_COLLECTION_SORT_GROUP,
  DT_COLLECTION_SORT_PATH,
  DT_COLLECTION_SORT_CUSTOM_ORDER,
  DT_COLLECTION_SORT_TITLE,
};
#define NB_ITEMS (sizeof(items) / sizeof(dt_collection_sort_t))

static const char *_sort_names[]
  = { N_("filename"),
      N_("captured"),
      N_("imported"),
      N_("modified"),
      N_("exported"),
      N_("printed"),
      N_("rating"),
      N_("id"),
      N_("color label"),
      N_("group"),
      N_("full path"),
      N_("custom sort"),
      N_("title"),
      NULL };

static int _filter_get_items(const dt_collection_sort_t sort)
{
  for(int i = 0; i < NB_ITEMS; i++)
  {
    if(sort == items[i])
    return i;
  }
  return 0;
}

const char *name(struct dt_lib_module_t *self)
{
  return _("filter");
}

const char **views(dt_lib_module_t *self)
{
  static const char *v[] = {"lighttable", NULL};
  return v;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_TOP_SECOND_ROW;
}

int expandable(dt_lib_module_t *self)
{
  return 0;
}

int position()
{
  return 2001;
}

static void _set_widget_dimmed(GtkWidget *widget, const gboolean dimmed)
{
  if(dimmed) dt_gui_add_class(widget, "dt_dimmed");
  else dt_gui_remove_class(widget, "dt_dimmed");
  gtk_widget_queue_draw(GTK_WIDGET(widget));
}

static char *_encode_text_filter(const char *entry)
{
  // by default adds start and end wildcard
  // " removes the corresponding wildcard
  char start[2] = {0};
  char *text = NULL;
  char *p = (char *)entry;
  if(strlen(entry) > 1 && !(entry[0] == '"' && entry[1] == '"'))
  {
    if(entry[0] == '"')
      p++;
    else if(entry[0])
      start[0] = '%';
    if(entry[strlen(entry) - 1] == '"')
    {
      text = g_strconcat(start, (char *)p, NULL);
      text[strlen(text) - 1] = '\0';
    }
    else if(entry[0])
      text = g_strconcat(start, (char *)p, "%", NULL);
  }
  return text;
}

static char *_decode_text_filter(const char *text)
{
  // revert the encoded filter for display
  char start[2] = {0};
  char *text1 = g_strdup(text);
  char *p = text1;
  char *text2;
  if(text1[0])
  {
    if(text1[0] == '%')
      p++;
    else
      start[0] = '\"';
    if(strlen(text1) > 1 && text1[strlen(text1) - 1] == '%')
    {
      text1[strlen(text1) - 1] = '\0';
      text2 = g_strconcat(start, (char *)p, NULL);
    }
    else
      text2 = g_strconcat(start, (char *)p, "\"", NULL);
    g_free(text1);
    return text2;
  }
  else return text1;
}

static gboolean _text_entry_changed_wait(gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  if(d->time_out)
  {
    d->time_out--;
    double clock = dt_get_wtime();
    if(clock - d->last_key_time >= 0.4)
    {
      d->time_out = 1; // force the query execution
      d->last_key_time = clock;
    }

    if(d->time_out == 1)
    { // tell we are busy
      _set_widget_dimmed(d->text, TRUE);
    }
    else if(!d->time_out)
    {
      char *text = _encode_text_filter(gtk_entry_get_text(GTK_ENTRY(d->text)));

      // avoids activating twice the same query
      if(g_strcmp0(dt_collection_get_text_filter(darktable.collection), text))
      {
        dt_collection_set_text_filter(darktable.collection, text);
        dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_SORT, NULL);
      }
      else g_free(text);
      _set_widget_dimmed(d->text, FALSE);
      return FALSE;
    }
  }
  return TRUE;
}

static void _launch_text_query(dt_lib_module_t *self)
{
  // two timeouts 1) 0.4 sec after the last key, 2) 1.5 sec of successive keys
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  d->last_key_time = dt_get_wtime();
  if(!d->time_out)
  {
    d->time_out = 15;
    g_timeout_add(100, _text_entry_changed_wait, self);
  }
}

static void _text_entry_changed(GtkEntry *entry, dt_lib_module_t *self)
{
  _launch_text_query(self);
}

static void _reset_text_filter(dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  dt_collection_set_text_filter(darktable.collection, strdup(""));
  gtk_entry_set_text(GTK_ENTRY(d->text), "");
}

static void _reset_text_entry(GtkButton *button, dt_lib_module_t *self)
{
  _reset_text_filter(self);
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_SORT, NULL);
}

gboolean _focus_search_action(GtkAccelGroup *accel_group, GObject *accelerable, guint keyval,
                              GdkModifierType modifier, gpointer data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)data;
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  gtk_widget_grab_focus(GTK_WIDGET(d->text));
  return TRUE;
}

gboolean _reset_filter_action(GtkAccelGroup *accel_group, GObject *accelerable, guint keyval,
                              GdkModifierType modifier, gpointer data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)data;
  _lib_filter_reset(self, FALSE);
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_SORT, NULL);
  return TRUE;
}


#define CPF_USER_DATA_INCLUDE CPF_USER_DATA
#define CPF_USER_DATA_EXCLUDE CPF_USER_DATA << 1
#define CL_AND_MASK 0x80000000
#define CL_ALL_EXCLUDED 0x3F000
#define CL_ALL_INCLUDED 0x3F

static void _update_colors_filter(dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  int mask = dt_collection_get_colors_filter(darktable.collection);
  int mask_excluded = 0x1000;
  int mask_included = 1;
  int nb = 0;
  for(int i = 0; i <= DT_COLORLABELS_LAST; i++)
  {
    const int i_mask = mask & mask_excluded ? CPF_USER_DATA_EXCLUDE : mask & mask_included ? CPF_USER_DATA_INCLUDE : 0;
    dtgtk_button_set_paint(DTGTK_BUTTON(d->colors[i]), dtgtk_cairo_paint_label_sel,
                           (i | i_mask | CPF_LABEL_PURPLE), NULL);
    gtk_widget_queue_draw(d->colors[i]);
    if((mask & mask_excluded) || (mask & mask_included))
      nb++;
    mask_excluded <<= 1;
    mask_included <<= 1;
  }
  if(nb <= 1)
  {
    mask |= CL_AND_MASK;
    dt_collection_set_colors_filter(darktable.collection, mask);
  }
}

static void _reset_colors_filter(dt_lib_module_t *self)
{
  dt_collection_set_colors_filter(darktable.collection, CL_AND_MASK);
}

static gboolean _colorlabel_clicked(GtkWidget *w, GdkEventButton *e, dt_lib_module_t *self)
{
  const int mask = dt_collection_get_colors_filter(darktable.collection);
  const int k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "colors_index"));
  int mask_k = (1 << k) | (1 << (k + 12));
  if(k == DT_COLORLABELS_LAST)
  {
    if(mask & mask_k)
      mask_k = 0;
    else if(dt_modifier_is(e->state, GDK_CONTROL_MASK))
      mask_k = CL_ALL_EXCLUDED;
    else if(dt_modifier_is(e->state, 0))
      mask_k = CL_ALL_INCLUDED;
    dt_collection_set_colors_filter(darktable.collection, mask_k | (mask & CL_AND_MASK));
  }
  else
  {
    if(mask & mask_k)
      mask_k = 0;
    else if(dt_modifier_is(e->state, GDK_CONTROL_MASK))
      mask_k = 1 << (k + 12);
    else if(dt_modifier_is(e->state, 0))
      mask_k = 1 << k;
    dt_collection_set_colors_filter(darktable.collection, (mask & ~((1 << k) | (1 << (k + 12)))) | mask_k);
  }
  _update_colors_filter(self);
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_COLORLABEL, NULL);
  return FALSE;
}

static void _culling_mode(GtkWidget *widget, gpointer data)
{
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
  {
    darktable.gui->culling_mode = TRUE;
    dt_control_set_mouse_over_id(dt_selection_get_first_id(darktable.selection));
    dt_thumbtable_reset_collection(dt_ui_thumbtable(darktable.gui->ui));
  }
  else
  {
    darktable.gui->culling_mode = FALSE;
    dt_culling_mode_to_selection();
    dt_control_set_mouse_over_id(dt_selection_get_first_id(darktable.selection));
    dt_thumbtable_reset_collection(dt_ui_thumbtable(darktable.gui->ui));
  }

  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_UNDEF, NULL);
  DT_DEBUG_CONTROL_SIGNAL_RAISE(darktable.signals, DT_SIGNAL_SELECTION_CHANGED);
}

#undef CPF_USER_DATA_INCLUDE
#undef CPF_USER_DATA_EXCLUDE
#undef CL_AND_MASK
#undef CL_ALL_EXCLUDED
#undef CL_ALL_INCLUDED

static void _refresh_collection_callback(GtkButton *button, gpointer user_data)
{
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_UNDEF, NULL);
}

void _widget_align_left(GtkWidget *widget)
{
  gtk_widget_set_halign(widget, GTK_ALIGN_START);
  gtk_widget_set_hexpand(widget, TRUE);

  gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(widget, FALSE);
}


const dt_collection_filter_flag_t ratings[7] =
  { COLLECTION_FILTER_REJECTED,
    COLLECTION_FILTER_0_STAR,
    COLLECTION_FILTER_1_STAR,
    COLLECTION_FILTER_2_STAR,
    COLLECTION_FILTER_3_STAR,
    COLLECTION_FILTER_4_STAR,
    COLLECTION_FILTER_5_STAR };


static gboolean _rating_clicked(GtkWidget *w, GdkEventButton *e, dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  dt_collection_filter_flag_t flags = dt_collection_get_filter_flags(darktable.collection);

  // Toggle active state
  gboolean active = !dtgtk_button_get_active(DTGTK_BUTTON(w));
  dtgtk_button_set_active(DTGTK_BUTTON(w), active);

  // Update GUI button state
  if(w == d->stars[0])
  {
    // shitty design: the active rejected state is signaled as a right orientation...
    if(active)
      DTGTK_BUTTON(w)->icon_flags |= CPF_DIRECTION_RIGHT;
    else
      DTGTK_BUTTON(w)->icon_flags &= ~CPF_DIRECTION_RIGHT;
  }
  else
  {
    // fill stars if active
    if(active)
      DTGTK_BUTTON(w)->icon_data = &darktable.bauhaus->color_fg;
    else
      DTGTK_BUTTON(w)->icon_data = NULL;
  }

  gtk_widget_queue_draw(w);

  // Update collection flags
  for(int i = 0; i < 7; i++)
  {
    if(dtgtk_button_get_active(DTGTK_BUTTON(d->stars[i])))
      flags |= ratings[i];
    else
      flags &= ~ratings[i];
  }

  dt_collection_set_filter_flags(darktable.collection, flags);

  /* update the query and view */
  _lib_filter_update_query(self, DT_COLLECTION_PROP_RATING);

  return TRUE;
}


void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)g_malloc0(sizeof(dt_lib_tool_filter_t));
  self->data = (void *)d;

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  _widget_align_left(self->widget);
  dt_gui_add_class(self->widget, "quick_filter_box");

  GtkWidget *label;

  d->refresh = dtgtk_button_new(dtgtk_cairo_paint_refresh, 0, NULL);
  gtk_widget_set_tooltip_text(d->refresh, _("Refresh the current collection to evict images\n"
                                            "which properties have been changed\n"
                                            "and don't match the current filters anymore."));
  g_signal_connect(G_OBJECT(d->refresh), "clicked", G_CALLBACK(_refresh_collection_callback), NULL);
  gtk_widget_set_name(d->sort, "quick-filter-reload");
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(d->refresh), FALSE, FALSE, 0);

  gchar *path = dt_accels_build_path(_("Lighttable/Actions"), _("Reload current collection"));
  dt_accels_new_widget_shortcut(darktable.gui->accels, d->refresh, "activate",
                                darktable.gui->accels->lighttable_accels, path, GDK_KEY_r, GDK_CONTROL_MASK,
                                FALSE);
  g_free(path);

  // dumb empty flexible spacer at the end
  GtkWidget *spacer = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(self->widget), spacer, TRUE, TRUE, 0);

  label = gtk_label_new(_("Filter"));
  gtk_box_pack_start(GTK_BOX(self->widget), label, FALSE, FALSE, 0);

  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), FALSE, FALSE, 0);
  gtk_widget_set_name(hbox, "quick-filter-ratings");

  // star buttons
  dt_collection_filter_flag_t flags = dt_collection_get_filter_flags(darktable.collection);

  for(int k = 0; k < 7; k++)
  {
    if(k == 0)
      d->stars[k] = dtgtk_button_new(dtgtk_cairo_paint_reject, k, NULL);
    else if(k == 1)
      d->stars[k] = dtgtk_button_new(dtgtk_cairo_paint_unratestar, k, NULL);
    else
      d->stars[k] = dtgtk_button_new(dtgtk_cairo_paint_star, k, NULL);

    gboolean active = flags & ratings[k];

    dtgtk_button_set_active(DTGTK_BUTTON(d->stars[k]), active);

    if(active)
    {
      if(k == 0)
        // shitty design: the active rejected state is signaled as a right orientation...
        DTGTK_BUTTON(d->stars[k])->icon_flags |= CPF_DIRECTION_RIGHT;
      else
        // fill the star
        DTGTK_BUTTON(d->stars[k])->icon_data = &darktable.bauhaus->color_fg;
    }

    dt_gui_add_class(d->stars[k], "star");
    dt_gui_add_class(d->stars[k], "dt_no_hover");
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(d->stars[k]), FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(d->stars[k]), "button-press-event", G_CALLBACK(_rating_clicked), self);
  }

  gtk_widget_set_tooltip_text(d->stars[0], _("Toggle filtering in/out rejected images"));
  gtk_widget_set_tooltip_text(d->stars[1], _("Toggle filtering in/out unrated images (0 star)"));
  gtk_widget_set_tooltip_text(d->stars[2], _("Toggle filtering in/out images rated 1 star"));
  gtk_widget_set_tooltip_text(d->stars[3], _("Toggle filtering in/out images rated 2 stars"));
  gtk_widget_set_tooltip_text(d->stars[4], _("Toggle filtering in/out images rated 3 stars"));
  gtk_widget_set_tooltip_text(d->stars[5], _("Toggle filtering in/out images rated 4 stars"));
  gtk_widget_set_tooltip_text(d->stars[6], _("Toggle filtering in/out images rated 5 stars"));

  // colorlabels filter
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), FALSE, FALSE, 0);
  gtk_widget_set_name(hbox, "quickfilters-colors");

  for(int k = 0; k < DT_COLORLABELS_LAST + 1; k++)
  {
    d->colors[k] = dtgtk_button_new(dtgtk_cairo_paint_label_sel, k, NULL);
    dt_gui_add_class(d->colors[k], "dt_no_hover");
    g_object_set_data(G_OBJECT(d->colors[k]), "colors_index", GINT_TO_POINTER(k));
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(d->colors[k]), FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(d->colors[k], _("filter by images color label"
                                                "\nclick to toggle the color label selection"
                                                "\nctrl+click to exclude the color label"
                                                "\nthe gray button affects all color labels"));
    g_signal_connect(G_OBJECT(d->colors[k]), "button-press-event", G_CALLBACK(_colorlabel_clicked), self);
  }
  _update_colors_filter(self);

  // Culling mode
  d->culling = gtk_toggle_button_new_with_label(_("Selected"));
  gtk_widget_set_tooltip_text(d->culling, _("Restrict the current view to only selected pictures"));
  g_signal_connect(G_OBJECT(d->culling), "toggled", G_CALLBACK(_culling_mode), (gpointer)self);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(d->culling), FALSE, FALSE, 0);
  gtk_widget_set_name(d->culling, "quickfilter-culling");

  path = dt_accels_build_path(_("Lighttable/Actions"), _("Toggle culling mode"));
  dt_accels_new_widget_shortcut(darktable.gui->accels, d->culling, "activate",
                                darktable.gui->accels->lighttable_accels, path, GDK_KEY_s, GDK_CONTROL_MASK,
                                FALSE);
  g_free(path);

  // dumb empty flexible spacer at the end
  spacer = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(self->widget), spacer, TRUE, TRUE, 0);

  label = gtk_label_new(_("Sort by"));
  gtk_box_pack_start(GTK_BOX(self->widget), label, FALSE, FALSE, 0);

  /* sort combobox */
  const dt_collection_sort_t sort = dt_collection_get_sort_field(darktable.collection);

  d->sort = gtk_combo_box_text_new();

  for(int i = 0; i < NB_ITEMS; i++)
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(d->sort), NULL, _sort_names[i]);

  gtk_combo_box_set_active(GTK_COMBO_BOX(d->sort ), _filter_get_items(sort));
  g_signal_connect(G_OBJECT(d->sort), "changed", G_CALLBACK(_lib_filter_sort_combobox_changed), (gpointer)self);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(d->sort), FALSE, FALSE, 0);
  gtk_widget_set_name(d->sort, "quick-filter-sort");

  /* reverse order checkbutton */
  d->reverse = dtgtk_togglebutton_new(dtgtk_cairo_paint_sortby, CPF_DIRECTION_UP, NULL);
  if(darktable.collection->params.descending)
    dtgtk_togglebutton_set_paint(DTGTK_TOGGLEBUTTON(d->reverse), dtgtk_cairo_paint_sortby,
                                 CPF_DIRECTION_DOWN, NULL);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(d->reverse), FALSE, FALSE, 0);
  dt_gui_add_class(d->reverse, "dt_ignore_fg_state");

  /* select the last value and connect callback */
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->reverse),
                               dt_collection_get_sort_descending(darktable.collection));
  g_signal_connect(G_OBJECT(d->reverse), "toggled", G_CALLBACK(_lib_filter_reverse_button_changed),
                   (gpointer)self);

  // dumb empty flexible spacer at the end
  spacer = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(self->widget), spacer, TRUE, TRUE, 0);

  // text filter
  d->text = gtk_search_entry_new();
  dt_accels_disconnect_on_text_input(d->text);
  dt_gui_add_class(GTK_WIDGET(d->text), "menu-text-entry");
  char *text = _decode_text_filter(dt_collection_get_text_filter(darktable.collection));
  gtk_entry_set_text(GTK_ENTRY(d->text), text);
  gtk_entry_set_placeholder_text(GTK_ENTRY(d->text), _("Search an image..."));
  g_free(text);
  g_signal_connect(G_OBJECT(d->text), "search-changed", G_CALLBACK(_text_entry_changed), self);
  g_signal_connect(G_OBJECT(d->text), "stop-search", G_CALLBACK(_reset_text_entry), self);
  gtk_entry_set_width_chars(GTK_ENTRY(d->text), 24);
  gtk_widget_set_tooltip_text(d->text,
          /* xgettext:no-c-format */
                              _("filter by text from images metadata, tags, file path and name"
          /* xgettext:no-c-format */
                                "\n`%' is the wildcard character"
          /* xgettext:no-c-format */
                                "\nby default start and end wildcards are auto-applied"
          /* xgettext:no-c-format */
                                "\nstarting or ending with a double quote disables the corresponding wildcard"
          /* xgettext:no-c-format */
                                "\nis dimmed during the search execution"));
  //dt_gui_add_class(d->text, "dt_transparent_background");
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(d->text), FALSE, FALSE, 0);
  gtk_widget_set_name(d->text, "quickfilter-search-box");

  dt_accels_new_lighttable_action(_focus_search_action, self, N_("Lighttable/Actions"), N_("Search a picture"),
                                  GDK_KEY_f, GDK_CONTROL_MASK);

  dt_accels_new_lighttable_action(_reset_filter_action, self, N_("Lighttable/Actions"), N_("Reset the collection filter"),
                                  0, 0);

  // dumb empty flexible spacer at the end
  spacer = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(self->widget), spacer, TRUE, TRUE, 0);

  /* initialize proxy */
  darktable.view_manager->proxy.filter.module = self;
  darktable.view_manager->proxy.filter.reset_filter = _lib_filter_reset;

  DT_DEBUG_CONTROL_SIGNAL_CONNECT(darktable.signals, DT_SIGNAL_IMAGES_ORDER_CHANGE,
                            G_CALLBACK(_lib_filter_images_order_change), self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_collection_set_text_filter(darktable.collection, NULL);
  g_free(self->data);
  self->data = NULL;
}

/* save the images order if the first collect filter is on tag*/
static void _lib_filter_set_tag_order(dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  if(darktable.collection->tagid)
  {
    const uint32_t sort = items[dt_bauhaus_combobox_get(d->sort)];
    const gboolean descending = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->reverse));
    dt_tag_set_tag_order_by_id(darktable.collection->tagid, sort, descending);
  }
}

static void _lib_filter_images_order_change(gpointer instance, const int order, dt_lib_module_t *self)
{
  dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
  dt_bauhaus_combobox_set(d->sort, _filter_get_items(order & ~DT_COLLECTION_ORDER_FLAG));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->reverse), order & DT_COLLECTION_ORDER_FLAG);
}

static void _lib_filter_reverse_button_changed(GtkDarktableToggleButton *widget, gpointer user_data)
{
  const gboolean reverse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

  if(reverse)
    dtgtk_togglebutton_set_paint(widget, dtgtk_cairo_paint_sortby, CPF_DIRECTION_DOWN, NULL);
  else
    dtgtk_togglebutton_set_paint(widget, dtgtk_cairo_paint_sortby, CPF_DIRECTION_UP, NULL);
  gtk_widget_queue_draw(GTK_WIDGET(widget));

  /* update last settings */
  dt_collection_set_sort(darktable.collection, DT_COLLECTION_SORT_NONE, reverse);

  /* save the images order */
  _lib_filter_set_tag_order(user_data);

  /* update query and view */
  _lib_filter_update_query(user_data, DT_COLLECTION_PROP_SORT);
}

static void _lib_filter_sort_combobox_changed(GtkWidget *widget, gpointer user_data)
{
  /* update the ui last settings */
  dt_collection_set_sort(darktable.collection, items[gtk_combo_box_get_active(GTK_COMBO_BOX(widget))], -1);

  /* save the images order */
  _lib_filter_set_tag_order(user_data);

  /* update the query and view */
  _lib_filter_update_query(user_data, DT_COLLECTION_PROP_SORT);
}

static void _lib_filter_update_query(dt_lib_module_t *self, dt_collection_properties_t changed_property)
{
  /* sometimes changes */
  dt_collection_set_query_flags(darktable.collection, COLLECTION_QUERY_FULL);

  /* updates query */
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, changed_property, NULL);
}

static void _reset_stars_filter(dt_lib_module_t *self, gboolean smart_filter)
{
  //dt_lib_tool_filter_t *d = (dt_lib_tool_filter_t *)self->data;
}

static void _lib_filter_reset(dt_lib_module_t *self, gboolean smart_filter)
{
  _reset_stars_filter(self, smart_filter);
  _reset_text_filter(self);
  _reset_colors_filter(self);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
