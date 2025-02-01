#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

/**
 * @file accelerators.h
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
 *
 * Some keys appear reserved, like Tab or Enter, depending on OS. So we have to use our own, custom,
 * shortcut handler, which is mostly a thin wrapper over Gtk native features.asm
 *
 * This allows us to decide in which order we process the several sets of shortcuts we maintain
 * (global, lighttable, darkroom). Global shortcuts are processed last, for all views.
 * Lighttable and darkroom shortcuts are processed first, for the relevant view.
 * This lets user have different actions mapped to the same shortcuts, depending on view
 * but also have the view-centric shortcuts overwrite global ones if needed.
 */

#pragma once

typedef struct dt_accels_t
{
  char *config_file;
  GtkAccelGroup *global_accels;     // used, aka init it and free it
  GtkAccelGroup *darkroom_accels;   // darkroom-specific accels
  GtkAccelGroup *lighttable_accels; // lighttable-specific accels
  GtkAccelGroup *active_group; // reference to the above group currently loaded in the main window. don't init,
                               // don't free, only update
  GHashTable *acceleratables;  // Key/value list of path/dt_shortcut_t
  GtkWindow *window;           // window capturing shortcuts
  gint reset;                  // ref counter of how many parts disconnected accels
  GdkKeymap *keymap;           // default screen keymap to decode key values
  GdkModifierType default_mod_mask; // set of modifier masks relevant only to key strokes
  gboolean init; // TRUE if we didn't find a keyboardrc config file at startup and we need to init a new one
  GtkAccelKey active_key;      // between key_pressed and key_release events, store the active key strokes

  // Views can register a global callback to handle scroll events
  // for example while keystrokes are on.
  struct scroll {
    gboolean (*callback)(GdkEventScroll event, void *data);
    void *data;
  } scroll;
} dt_accels_t;

typedef enum dt_shortcut_type_t
{
  DT_SHORTCUT_UNSET = 0,     // shortcut non-inited
  DT_SHORTCUT_DEFAULT = 1,   // shortcut inited with compile-time defaults
  DT_SHORTCUT_USER = 2       // shortcut changed by user config
} dt_shortcut_type_t;

typedef struct dt_shortcut_t
{
  GtkWidget *widget;          // link to the widget being accelerated. Can be NULL.
  GClosure *closure;          // callback + data being accelerated. Has to be non-NULL if widget is NULL.
  char *path;                 // global path for that accel
  const char *signal;         // widget signal to be wired to that accel
  GtkAccelGroup *accel_group; // the accel_group to which this shortcut belongs
  guint key;                  // default key
  GdkModifierType mods;       // default modifier
  dt_shortcut_type_t type;
  gboolean locked;            // this will not listen to user config
} dt_shortcut_t;


dt_accels_t *dt_accels_init(char *config_file, GtkWindow *window);
void dt_accels_cleanup(dt_accels_t *accels);


gchar *dt_accels_build_path(const gchar *scope, const gchar *feature);

/**
 * @brief Loads keyboardrc.lang from config dir. This needs to run after we inited the accel map from widgets
 * creation.
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
 * @brief Connect the accels group to the window
 *
 * @param accels
 * @param group any of the following: "global", "darkroom", "lighttable", "active".
 * "darkroom" or "lighttable" will set their group the active one, that can be later
 * retrieved with "active". The "active" option will not work on the first call of this function.
 */
void dt_accels_connect_window(dt_accels_t *accels, const gchar *group);

/**
 * @brief Disconnect the global accels group to the window
 *
 * @param accels
 * @param group any of the following: "global", "active". The active group is tracked internally when connecting
 * windows
 * @param reset set to TRUE if the active group should be reset. This will prevent later calls of
 * `dt_accels_connect_window()` using "active". This has no effect when disconnecting the global group.
 */
void dt_accels_disconnect_window(dt_accels_t *accels, const gchar *group, const gboolean reset);

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
 * @param lock prevent user edition

 */
void dt_accels_new_widget_shortcut(dt_accels_t *accels, GtkWidget *widget, const gchar *signal,
                                   GtkAccelGroup *accel_group, const gchar *accel_path, guint key_val,
                                   GdkModifierType accel_mods, const gboolean lock);


/**
 * @brief Register a new shortcut for a generic action, setting up its path, default keys and accel group.
 * This does everything but connecting it, so exists only as a defined slot to be connected later.
 *
 * The callback should have the following signature:
 *
 * ```C
 *
 * gboolean action_callback(GtkAccelGroup *accel_group, GObject *accelerable, guint keyval, GdkModifierType
 * modifier, gpointer data)
 *
 * ```
 *
 * @param accels
 * @param data
 * @param accel_group
 * @param action_scope Human-readable, translated, category or scope of the action. Will be turned into path
 * internally
 * @param action_name Human-readable, translated, name or description of the action. Will be turned into path
 * internally
 * @param key_val
 * @param accel_mods
 * @param lock prevent user edition
 */
void dt_accels_new_action_shortcut(dt_accels_t *accels, void(*action_callback), gpointer data,
                                   GtkAccelGroup *accel_group, const gchar *action_scope, const gchar *action_name,
                                   guint key_val, GdkModifierType accel_mods, const gboolean lock);


/**
 * @brief Force our listener for all key strokes to bypass reserved Gtk keys.
 *
 * @param w
 * @param event
 * @param user_data
 * @return gboolean
 */
gboolean dt_accels_dispatch(GtkWidget *w, GdkEvent *event, gpointer user_data);

/**
 * @brief Attach a new global scroll event callback
 *
 * @param callback
 * @param data
 */
void dt_accels_attach_scroll_handler(dt_accels_t *accels, gboolean (*callback)(GdkEventScroll event, void *data), void *data);

void dt_accels_detach_scroll_handler(dt_accels_t *accels);
