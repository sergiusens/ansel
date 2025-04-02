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

#include <gdk/gdkkeysyms.h>

#include "common/collection.h"
#include "common/debug.h"
#include "common/selection.h"
#include "control/conf.h"
#include "control/control.h"
#include "dtgtk/button.h"
#include "dtgtk/thumbtable.h"
#include "dtgtk/togglebutton.h"

#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

typedef struct dt_lib_tool_lighttable_t
{
  GtkWidget *jpg;
  GtkWidget *focus;
  GtkWidget *columns;
  GtkWidget *zoom;
  int current_columns;
  gboolean combo_evt_reset;
} dt_lib_tool_lighttable_t;

/* set columns proxy function */
static void _lib_lighttable_set_columns(dt_lib_module_t *self, gint columns);
static gint _lib_lighttable_get_columns(dt_lib_module_t *self);

/* columns slider change callback */
static void _lib_lighttable_columns_slider_changed(GtkWidget *widget, gpointer user_data);

static void _set_columns(dt_lib_module_t *self, int columns);

const char *name(struct dt_lib_module_t *self)
{
  return _("lighttable");
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
  return 1001;
}

gboolean _columns_in_action(GtkAccelGroup *accel_group, GObject *accelerable, guint keyval,
                         GdkModifierType modifier, gpointer data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)data;
  int current_level = _lib_lighttable_get_columns(self);
  int new_level = CLAMP(current_level - 1, 1, 12);
  _lib_lighttable_set_columns(self, new_level);
  dt_conf_set_int("plugins/lighttable/images_in_row_backup", new_level);
  return TRUE;
}

gboolean _columns_out_action(GtkAccelGroup *accel_group, GObject *accelerable, guint keyval,
                          GdkModifierType modifier, gpointer data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)data;
  int current_level = _lib_lighttable_get_columns(self);
  int new_level = CLAMP(current_level + 1, 1, 12);
  _lib_lighttable_set_columns(self, new_level);
  dt_conf_set_int("plugins/lighttable/images_in_row_backup", new_level);
  return TRUE;
}

static void _dt_collection_changed_callback(gpointer instance, dt_collection_change_t query_change,
                                            dt_collection_properties_t changed_property, gpointer imgs,
                                            const int next, gpointer user_data)
{
  if(!user_data) return;
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;

  #if 0
  int current_level = _lib_lighttable_get_columns(self);
  int num_images = dt_collection_get_count(darktable.collection);

  switch(num_images)
  {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
      _lib_lighttable_set_columns(self, num_images);
      dt_conf_set_int("plugins/lighttable/images_in_row_backup", current_level);
      break;
    case 6:
      _lib_lighttable_set_columns(self, 3);
      dt_conf_set_int("plugins/lighttable/images_in_row_backup", current_level);
      break;
    case 7:
    case 8:
      _lib_lighttable_set_columns(self, 4);
      dt_conf_set_int("plugins/lighttable/images_in_row_backup", current_level);
      break;
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
      _lib_lighttable_set_columns(self, 5);
      dt_conf_set_int("plugins/lighttable/images_in_row_backup", current_level);
      break;
    default:
      if(dt_conf_key_exists("plugins/lighttable/images_in_row_backup"))
        _lib_lighttable_set_columns(self, dt_conf_get_int("plugins/lighttable/images_in_row_backup"));
  }
  #endif

  // Reset zoom
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)self->data;
  gtk_combo_box_set_active(GTK_COMBO_BOX(d->zoom), 0);
  dt_thumbtable_set_zoom(darktable.gui->ui->thumbtable_lighttable, 0);
}

static void _zoom_combobox_changed(GtkWidget *widget, gpointer user_data)
{
  int level = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  dt_thumbtable_set_zoom(darktable.gui->ui->thumbtable_lighttable, level);
}

static void _jpg_combobox_changed(GtkWidget *widget, gpointer user_data)
{
  int mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  if(mode == dt_conf_get_int("lighttable/embedded_jpg")) return;
  dt_conf_set_int("lighttable/embedded_jpg", mode);
}

// Ctrl + Scroll changes the number of columns
static gboolean _thumbtable_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)data;

  if(dt_modifier_is(event->state, GDK_CONTROL_MASK))
  {
    int scroll_y;
    dt_gui_get_scroll_unit_deltas(event, NULL, &scroll_y);

    int current_level = _lib_lighttable_get_columns(self);
    int new_level = CLAMP(current_level + CLAMP(scroll_y, -1, 1), 1, 12);

    _lib_lighttable_set_columns(self, new_level);
    dt_conf_set_int("plugins/lighttable/images_in_row_backup", new_level);
    return TRUE;
  }
  return FALSE;
}

void _focus_toggled(GtkToggleButton *self, gpointer user_data)
{
  dt_thumbtable_set_focus(darktable.gui->ui->thumbtable_lighttable, gtk_toggle_button_get_active(self));
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)g_malloc0(sizeof(dt_lib_tool_lighttable_t));
  self->data = (void *)d;

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  dt_gui_add_class(self->widget, "lighttable_box");
  gtk_widget_set_halign(self->widget, GTK_ALIGN_END);

  GtkWidget *label = gtk_label_new(C_("quickfilter", "Embedded JPEG"));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(label), FALSE, FALSE, 0);

  d->jpg = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->jpg), _("Never"));
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->jpg), _("Unedited"));
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->jpg), _("Always"));
  gtk_box_pack_start(GTK_BOX(self->widget), d->jpg, FALSE, FALSE, 0);
  gtk_combo_box_set_active(GTK_COMBO_BOX(d->jpg), dt_conf_get_int("lighttable/embedded_jpg"));
  g_signal_connect(G_OBJECT(d->jpg), "changed", G_CALLBACK(_jpg_combobox_changed), (gpointer)self);
  gtk_widget_set_tooltip_markup(
      d->jpg, _("Choose if the raw embedded thumbnail should be displayed\n"
                "in the lightttable instead of a full rendering from raw.\n"
                "\"Never\" always renders thumbnails from raw (slow but consistent with darkroom)\n"
                "\"Unedited\" uses the embedded JPG for unedited pictures (faster)\n"
                "\"Always\" uses the embedded JPG for all pictures (fast but inconsistent with darkroom)"));

  // dumb empty flexible spacer at the end
  GtkWidget *spacer = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(self->widget), spacer, TRUE, TRUE, 0);

  d->focus = gtk_toggle_button_new_with_label(_("Focus zones"));
  gtk_box_pack_start(GTK_BOX(self->widget), d->focus, FALSE, FALSE, 0);
  g_signal_connect(G_OBJECT(d->focus), "toggled", G_CALLBACK(_focus_toggled), NULL);
  gtk_widget_set_name(d->focus, "quickfilter-culling");

  // dumb empty flexible spacer at the end
  spacer = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_pack_start(GTK_BOX(self->widget), spacer, TRUE, TRUE, 0);

  label = gtk_label_new(C_("quickfilter", "Zoom"));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(label), FALSE, FALSE, 0);

  d->zoom = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->zoom), _("Fit"));
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->zoom), _("50 %"));
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->zoom), _("100 %"));
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->zoom), _("200 %"));
  gtk_box_pack_start(GTK_BOX(self->widget), d->zoom, FALSE, FALSE, 0);
  gtk_combo_box_set_active(GTK_COMBO_BOX(d->zoom), 0);
  g_signal_connect(G_OBJECT(d->zoom), "changed", G_CALLBACK(_zoom_combobox_changed), (gpointer)self);

  d->current_columns = dt_conf_get_int("plugins/lighttable/images_in_row");

  label = gtk_label_new(C_("quickfilter", "Columns"));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(label), FALSE, FALSE, 0);

  d->columns = gtk_spin_button_new_with_range(1., 12., 1.);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(d->columns), FALSE, FALSE, 0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->columns), d->current_columns);
  dt_accels_disconnect_on_text_input(d->columns);

  g_signal_connect(G_OBJECT(d->columns), "value-changed", G_CALLBACK(_lib_lighttable_columns_slider_changed), self);

  dt_accels_new_lighttable_action(_columns_in_action, self, N_("Lighttable/Actions"), N_("Zoom in the thumbtable grid"),
                                  GDK_KEY_plus, GDK_CONTROL_MASK);
  dt_accels_new_lighttable_action(_columns_out_action, self, N_("Lighttable/Actions"), N_("Zoom out the thumbtable grid"),
                                  GDK_KEY_minus, GDK_CONTROL_MASK);

  DT_DEBUG_CONTROL_SIGNAL_CONNECT(darktable.signals, DT_SIGNAL_COLLECTION_CHANGED,
                                  G_CALLBACK(_dt_collection_changed_callback), self);

  _lib_lighttable_columns_slider_changed(d->columns, self); // the slider defaults to 1 and GTK doesn't
                                                      // fire a value-changed signal when setting
                                                      // it to 1 => empty text box

  // Wire a scroll event handler on thumbtable here. This avoids us a proxy
  dt_thumbtable_t *table = darktable.gui->ui->thumbtable_lighttable;
  g_signal_connect(G_OBJECT(table->scroll_window), "scroll-event", G_CALLBACK(_thumbtable_scroll), self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  DT_DEBUG_CONTROL_SIGNAL_DISCONNECT(darktable.signals, G_CALLBACK(_dt_collection_changed_callback), self);
  g_free(self->data);
  self->data = NULL;
}

static void _set_columns(dt_lib_module_t *self, int columns)
{
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)self->data;
  dt_conf_set_int("plugins/lighttable/images_in_row", columns);
  dt_thumbtable_t *table = darktable.gui->ui->thumbtable_lighttable;

  dt_thumbtable_set_active_rowid(table);
  dt_thumbtable_redraw(table);
  g_idle_add((GSourceFunc) dt_thumbtable_scroll_to_active_rowid, table);
  d->current_columns = columns;
}

static void _lib_lighttable_columns_slider_changed(GtkWidget *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)self->data;
  const int cols = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->columns));
  _set_columns(self, cols);
  dt_conf_set_int("plugins/lighttable/images_in_row_backup", cols);
}

static void _lib_lighttable_set_columns(dt_lib_module_t *self, gint columns)
{
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)self->data;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->columns), columns);
  _set_columns(self, columns);
}

static gint _lib_lighttable_get_columns(dt_lib_module_t *self)
{
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)self->data;
  return d->current_columns;
}

#ifdef USE_LUA

static int columns_level_cb(lua_State *L)
{
  dt_lib_module_t *self = lua_touserdata(L, lua_upvalueindex(1));
  const gint tmp = _lib_lighttable_get_columns(self);
  if(lua_gettop(L) > 0){
    int value;
    luaA_to(L, int, &value, 1);
    _lib_lighttable_set_columns(self, value);
  }
  luaA_push(L, int, &tmp);
  return 1;
}

void init(struct dt_lib_module_t *self)
{
  lua_State *L = darktable.lua_state.state;
  int my_type = dt_lua_module_entry_get_type(L, "lib", self->plugin_name);
  lua_pushlightuserdata(L, self);
  dt_lua_gtk_wrap(L);
  lua_pushcclosure(L, dt_lua_type_member_common, 1);
  lua_pushlightuserdata(L, self);
  lua_pushcclosure(L, columns_level_cb, 1);
  dt_lua_gtk_wrap(L);
  lua_pushcclosure(L, dt_lua_type_member_common, 1);
  dt_lua_type_register_const_type(L, my_type, "zoom_level");
}
#endif
// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
