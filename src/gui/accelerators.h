#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#pragma once

typedef struct dt_accels_t {
  char *config_file;
  GtkAccelGroup *global_accels;
  GSList *acceleratables;     // list of dt_shortcut_t
} dt_accels_t;


typedef struct dt_shortcut_t {
  GtkWidget *widget;          // link to the widget being accelerated. Can be NULL.
  GClosure *closure;          // callback + data being accelerated. Has to be non-NULL if widget is NULL.
  char *path;                 // global path for that accel
  const char *signal;         // widget signal to be wired to that accel
  GtkAccelGroup *accel_group; // the accel_group to which this shortcut belongs
  guint key;                  // default key
  GdkModifierType mods;       // default modifier
} dt_shortcut_t;


dt_accels_t * dt_accels_init(char *config_file);
void dt_accels_cleanup(dt_accels_t *accels);

/**
 * @brief Handle default and user-set shortcuts (accelerators)
 *
 * Gtk is a bit weird here, so we need to :
 *
 *  1. have each acceleratable widget declare its accel path and default shortcut (if any),
 *  2. read the accels map file, which only import accels for already-known pathes (from previous step),
 *     and may update the default shortcut with user-defined one,
 *  3. connect widget relevant signal and shortcut through the active GtkAccelGroup. But the actual
 *     signal depends on the widget type.
 *
 * Because of that, we need to prepare the list of acceleratable widgets first, populate the accel maps
 * with their pathes, and only after fetching user-defined accels, we can connect actual actions.
 * So it's not straight-forward.
 */


/**
 * @brief Find the numpad equivalent key of any given key.
 * Use this to define/handle alternative shortcuts.
 *
 * @param key_val
 * @return guint
 */
guint dt_accels_keypad_alternatives(const guint key_val);


gchar *dt_accels_build_path(const gchar *scope, const gchar *feature);

/**
 * @brief Loads keyboardrc.lang from config dir. This needs to run after we inited the accel map from widgets creation.
 *
 * @param accels
 */
void dt_accels_load_user_config(dt_accels_t *accels);


/**
 * @brief Actually enable accelerators after having loaded user config
 *
 * @param accels
 */
void dt_accels_connect_accels(dt_accels_t *accels);


/**
 * @brief Connect the global accels group to the window
 *
 * @param accels
 * @param window
 */
void dt_accels_connect_window(dt_accels_t *accels, GtkWindow *window);

/**
 * @brief Register a new shortcut for a widget, setting up its path, default keys and accel group.
 * This does everything but connecting it, so exists only as a defined slot to be connected later.
 *
 * @param accels
 * @param widget
 * @param signal
 * @param accel_group
 * @param accel_path
 * @param key_val
 * @param accel_mods
 */
void dt_accels_new_widget_shortcut(dt_accels_t *accels, GtkWidget *widget, const gchar *signal,
                                   GtkAccelGroup *accel_group, const gchar *accel_path, guint key_val,
                                   GdkModifierType accel_mods);


/**
 * @brief Register a new shortcut for a generic action, setting up its path, default keys and accel group.
 * This does everything but connecting it, so exists only as a defined slot to be connected later.
 *
 * The callback should have the following signature:
 *
 * ```C
 *
 * gboolean action_callback(GtkAccelGroup *accel_group, GObject *accelerable, guint keyval, GdkModifierType modifier, gpointer data)
 *
 * ```
 *
 * @param accels
 * @param data
 * @param accel_group
 * @param action_name Human-readable, translated, name or description of the action. Will be turned into path internally
 * @param key_val
 * @param accel_mods
 */
void dt_accels_new_action_shortcut(dt_accels_t *accels, void(*action_callback), gpointer data,
                                   GtkAccelGroup *accel_group, const gchar *action_name, guint key_val,
                                   GdkModifierType accel_mods);
