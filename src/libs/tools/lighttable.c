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
  GtkWidget *zoom;
  GtkWidget *zoom_entry;
  int current_zoom;
  gboolean combo_evt_reset;
} dt_lib_tool_lighttable_t;

/* set zoom proxy function */
static void _lib_lighttable_set_zoom(dt_lib_module_t *self, gint zoom);
static gint _lib_lighttable_get_zoom(dt_lib_module_t *self);

/* zoom slider change callback */
static void _lib_lighttable_zoom_slider_changed(GtkWidget *widget, gpointer user_data);

static void _set_zoom(dt_lib_module_t *self, int zoom);

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

gboolean _zoom_in_action(GtkAccelGroup *accel_group, GObject *accelerable, guint keyval,
                         GdkModifierType modifier, gpointer data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)data;
  int current_level = _lib_lighttable_get_zoom(self);
  int new_level = CLAMP(current_level - 1, 1, 12);
  _lib_lighttable_set_zoom(self, new_level);
  dt_conf_set_int("plugins/lighttable/images_in_row_backup", new_level);
  return TRUE;
}

gboolean _zoom_out_action(GtkAccelGroup *accel_group, GObject *accelerable, guint keyval,
                          GdkModifierType modifier, gpointer data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)data;
  int current_level = _lib_lighttable_get_zoom(self);
  int new_level = CLAMP(current_level + 1, 1, 12);
  _lib_lighttable_set_zoom(self, new_level);
  dt_conf_set_int("plugins/lighttable/images_in_row_backup", new_level);
  return TRUE;
}

static void _dt_collection_changed_callback(gpointer instance, dt_collection_change_t query_change,
                                            dt_collection_properties_t changed_property, gpointer imgs,
                                            const int next, gpointer user_data)
{
  if(!user_data) return;
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  int current_level = _lib_lighttable_get_zoom(self);
  int num_images = dt_collection_get_count(darktable.collection);

  switch(num_images)
  {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
      _lib_lighttable_set_zoom(self, num_images);
      dt_conf_set_int("plugins/lighttable/images_in_row_backup", current_level);
      break;
    case 6:
      _lib_lighttable_set_zoom(self, 3);
      dt_conf_set_int("plugins/lighttable/images_in_row_backup", current_level);
      break;
    case 7:
    case 8:
      _lib_lighttable_set_zoom(self, 4);
      dt_conf_set_int("plugins/lighttable/images_in_row_backup", current_level);
      break;
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
      _lib_lighttable_set_zoom(self, 5);
      dt_conf_set_int("plugins/lighttable/images_in_row_backup", current_level);
      break;
    default:
      if(dt_conf_key_exists("plugins/lighttable/images_in_row_backup"))
        _lib_lighttable_set_zoom(self, dt_conf_get_int("plugins/lighttable/images_in_row_backup"));
  }
}

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)g_malloc0(sizeof(dt_lib_tool_lighttable_t));
  self->data = (void *)d;

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_halign(self->widget, GTK_ALIGN_END);

  d->current_zoom = dt_conf_get_int("plugins/lighttable/images_in_row");

  /* Zoom */
  GtkWidget *label = gtk_label_new(C_("quickfilter", "Columns"));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(label), FALSE, FALSE, 0);
  dt_gui_add_class(label, "quickfilter-label");

  /* create horizontal zoom slider */
  d->zoom = gtk_spin_button_new_with_range(1., 12., 1.);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(d->zoom), FALSE, FALSE, 0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->zoom), d->current_zoom);

  // Capturing focus collides with lighttable key navigation, and it is useless
  // because we already have zoom in/out global shortcuts
  gtk_widget_set_can_focus(d->zoom, FALSE);

  g_signal_connect(G_OBJECT(d->zoom), "value-changed", G_CALLBACK(_lib_lighttable_zoom_slider_changed), self);

  dt_accels_new_lighttable_action(_zoom_in_action, self, N_("Lighttable/Actions"), N_("Zoom in the thumbtable grid"),
                                  GDK_KEY_plus, GDK_CONTROL_MASK);
  dt_accels_new_lighttable_action(_zoom_out_action, self, N_("Lighttable/Actions"), N_("Zoom out the thumbtable grid"),
                                  GDK_KEY_minus, GDK_CONTROL_MASK);

  DT_DEBUG_CONTROL_SIGNAL_CONNECT(darktable.signals, DT_SIGNAL_COLLECTION_CHANGED,
                                  G_CALLBACK(_dt_collection_changed_callback), self);

  _lib_lighttable_zoom_slider_changed(d->zoom, self); // the slider defaults to 1 and GTK doesn't
                                                      // fire a value-changed signal when setting
                                                      // it to 1 => empty text box
}

void gui_cleanup(dt_lib_module_t *self)
{
  DT_DEBUG_CONTROL_SIGNAL_DISCONNECT(darktable.signals, G_CALLBACK(_dt_collection_changed_callback), self);
  g_free(self->data);
  self->data = NULL;
}

static void _set_zoom(dt_lib_module_t *self, int zoom)
{
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)self->data;
  if(zoom != dt_conf_get_int("plugins/lighttable/images_in_row"))
  {
    dt_conf_set_int("plugins/lighttable/images_in_row", zoom);
    dt_thumbtable_t *table = dt_ui_thumbtable(darktable.gui->ui);
    gtk_widget_queue_draw(table->grid);
    d->current_zoom = zoom;
  }
}

static void _lib_lighttable_zoom_slider_changed(GtkWidget *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)self->data;
  _set_zoom(self, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->zoom)));
  dt_conf_set_int("plugins/lighttable/images_in_row_backup",
                  gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->zoom)));
}

static void _lib_lighttable_set_zoom(dt_lib_module_t *self, gint zoom)
{
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)self->data;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->zoom), zoom);
  _set_zoom(self, zoom);
}

static gint _lib_lighttable_get_zoom(dt_lib_module_t *self)
{
  dt_lib_tool_lighttable_t *d = (dt_lib_tool_lighttable_t *)self->data;
  return d->current_zoom;
}

#ifdef USE_LUA

static int zoom_level_cb(lua_State *L)
{
  dt_lib_module_t *self = lua_touserdata(L, lua_upvalueindex(1));
  const gint tmp = _lib_lighttable_get_zoom(self);
  if(lua_gettop(L) > 0){
    int value;
    luaA_to(L, int, &value, 1);
    _lib_lighttable_set_zoom(self, value);
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
  lua_pushcclosure(L, zoom_level_cb, 1);
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
