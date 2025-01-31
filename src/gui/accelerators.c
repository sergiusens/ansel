#include "accelerators.h"
#include "gui/gdkkeys.h"

#include <assert.h>
#include <glib.h>


dt_accels_t * dt_accels_init(char *config_file, GtkWindow *window)
{
  dt_accels_t *accels = malloc(sizeof(dt_accels_t));
  accels->config_file = g_strdup(config_file);
  accels->global_accels = gtk_accel_group_new();
  accels->darkroom_accels = gtk_accel_group_new();
  accels->lighttable_accels = gtk_accel_group_new();
  accels->acceleratables = NULL;
  accels->window = window;
  accels->active_group = NULL;
  accels->reset = 1;
  accels->keymap = gdk_keymap_get_for_display(gdk_display_get_default());
  accels->default_mod_mask = gtk_accelerator_get_default_mod_mask();
  accels->init = !g_file_test(accels->config_file, G_FILE_TEST_EXISTS);
  return accels;
}


static void _clean_shortcut(gpointer data)
{
  dt_shortcut_t *shortcut = (dt_shortcut_t *)data;
  g_free(shortcut->path);
  if(shortcut->closure) g_closure_unref(shortcut->closure);
  g_free(shortcut);
}


void dt_accels_cleanup(dt_accels_t *accels)
{
  gtk_accel_map_save(accels->config_file);

  dt_accels_disconnect_window(accels, "global", FALSE);

  accels->window = NULL;
  accels->active_group = NULL;

  g_object_unref(accels->global_accels);
  g_object_unref(accels->darkroom_accels);
  g_object_unref(accels->lighttable_accels);

  g_slist_free_full(accels->acceleratables, _clean_shortcut);
  g_free(accels->config_file);
  g_free(accels);
}

void dt_accels_connect_window(dt_accels_t *accels, const gchar *group)
{
  // When closing the app, it may happen that a text entry releasing focus
  // will re-connect accels on an already-destroyed window
  if(!accels->window) return;

  if(!g_strcmp0(group, "global") && accels->global_accels)
  {
    gtk_window_add_accel_group(accels->window, accels->global_accels);
    accels->reset--;
    // global is always active
  }
  else if(!g_strcmp0(group, "lighttable") && accels->lighttable_accels)
  {
    gtk_window_add_accel_group(accels->window, accels->lighttable_accels);
    accels->reset--;
    accels->active_group = accels->lighttable_accels;
  }
  else if(!g_strcmp0(group, "darkroom") && accels->darkroom_accels)
  {
    gtk_window_add_accel_group(accels->window, accels->darkroom_accels);
    accels->reset--;
    accels->active_group = accels->darkroom_accels;
  }
  else if(!g_strcmp0(group, "active") && accels->active_group)
  {
    gtk_window_add_accel_group(accels->window, accels->active_group);
    accels->reset--;
  }
  else
  {
    fprintf(stderr, "[dt_accels_connect_window] INFO: unknown value: `%s'\n", group);
  }
}

void dt_accels_disconnect_window(dt_accels_t *accels, const gchar *group, const gboolean reset)
{
  if(!accels->window) return;

  if(!g_strcmp0(group, "global") && accels->global_accels)
  {
    gtk_window_remove_accel_group(accels->window, accels->global_accels);
  }
  else if(!g_strcmp0(group, "active") && accels->active_group)
  {
    gtk_window_remove_accel_group(accels->window, accels->active_group);
    if(reset) accels->active_group = NULL;
  }
  else
  {
    fprintf(stderr, "[dt_accels_disconnect_window] INFO: unknown value: `%s'\n", group);
  }

  accels->reset++;
}

void dt_accels_new_widget_shortcut(dt_accels_t *accels, GtkWidget *widget, const gchar *signal, GtkAccelGroup *accel_group, const gchar *accel_path, guint key_val, GdkModifierType accel_mods)
{
  // Our own circuitery to keep track of things after user-defined shortcuts are updated
  dt_shortcut_t *shortcut = malloc(sizeof(dt_shortcut_t));
  shortcut->accel_group = accel_group;
  shortcut->widget = widget;
  shortcut->closure = NULL;
  shortcut->path = g_strdup(accel_path);
  shortcut->signal = signal;
  shortcut->key = key_val;
  shortcut->mods = accel_mods;
  shortcut->type = DT_SHORTCUT_UNSET;

  // Gtk circuitery with compile-time defaults. Init with no keys so Gtk collects them from user config later.
  gtk_accel_map_add_entry(accel_path, 0, 0);

  accels->acceleratables = g_slist_prepend(accels->acceleratables, shortcut);
}


void dt_accels_new_action_shortcut(dt_accels_t *accels, void (*action_callback), gpointer data, GtkAccelGroup *accel_group, const gchar *action_scope, const gchar *action_name, guint key_val, GdkModifierType accel_mods)
{
  // Our own circuitery to keep track of things after user-defined shortcuts are updated
  dt_shortcut_t *shortcut = malloc(sizeof(dt_shortcut_t));
  shortcut->accel_group = accel_group;
  shortcut->widget = NULL;
  shortcut->closure = g_cclosure_new(G_CALLBACK(action_callback), data, NULL);
  shortcut->path = dt_accels_build_path(action_scope, action_name);
  shortcut->signal = "";
  shortcut->key = key_val;
  shortcut->mods = accel_mods;
  shortcut->type = DT_SHORTCUT_UNSET;

  // Gtk circuitery with compile-time defaults. Init with no keys so Gtk collects them from user config later.
  gtk_accel_map_add_entry(shortcut->path, 0, 0);

  accels->acceleratables = g_slist_prepend(accels->acceleratables, shortcut);
}

/* Print debug stuff
void _foreach_accel(gpointer data, const gchar *accel_path, guint accel_key, GdkModifierType accel_mods,
                    gboolean changed)
{
  fprintf(stdout, "path: %s - accel: %i\n", accel_path, accel_key);
}
*/

void dt_accels_load_user_config(dt_accels_t *accels)
{
  //gtk_accel_map_foreach_unfiltered(NULL, _foreach_accel);
  gtk_accel_map_load(accels->config_file);
  //gtk_accel_map_foreach_unfiltered(NULL, _foreach_accel);
}


gboolean _update_shortcut_state(dt_shortcut_t *shortcut, GtkAccelKey *key, gboolean init)
{
  gboolean changed = FALSE;
  if(shortcut->type == DT_SHORTCUT_UNSET)
  {
    // accel_map table is initially populated with shortcut->type = DT_SHORTCUT_UNSET
    // so that means the entry is new
    if(init)
    {
      // We have no user config file. Init shortcuts with defaults,
      // then a brand new config will be saved on exiting the app.
      // Note: they might still be zero, not all shortcuts are assigned.
      key->accel_key = shortcut->key;
      key->accel_mods = shortcut->mods;
      gtk_accel_map_change_entry(shortcut->path, shortcut->key, shortcut->mods, TRUE);
      shortcut->type = DT_SHORTCUT_DEFAULT;
    }
    else if(key->accel_key == shortcut->key && key->accel_mods == shortcut->mods)
    {
      // We loaded user config file and found our defaults in it. Nothing to do.
      shortcut->type = DT_SHORTCUT_DEFAULT;
    }
    else
    {
      // We loaded user config file, and user made changes in there.
      // We will need to update our "defaults", which now become rather a memory of previous state
      shortcut->key = key->accel_key;
      shortcut->mods = key->accel_mods;
      shortcut->type = DT_SHORTCUT_USER;
    }

    // UNSET state always needs update, it means it's the first time we connect accels
    changed = TRUE;
  }
  else if(key->accel_key != shortcut->key || key->accel_mods != shortcut->mods)
  {
    shortcut->key = key->accel_key;
    shortcut->mods = key->accel_mods;
    shortcut->type = DT_SHORTCUT_USER;
    changed = TRUE;
  }

  return changed;
}


void _add_widget_accel(dt_shortcut_t *shortcut, const GtkAccelKey *key)
{
  gtk_widget_add_accelerator(shortcut->widget, shortcut->signal, shortcut->accel_group, key->accel_key,
                              key->accel_mods, GTK_ACCEL_VISIBLE);

  // Keypad numbers register as different keys. Find the numpad equivalent key here, if any.
  guint alt_char = dt_keys_keypad_alternatives(key->accel_key);
  if(key->accel_key != alt_char)
    gtk_widget_add_accelerator(shortcut->widget, shortcut->signal, shortcut->accel_group, alt_char,
                                key->accel_mods, GTK_ACCEL_VISIBLE);
}


void _remove_widget_accel(dt_shortcut_t *shortcut, const GtkAccelKey *key)
{
  gtk_widget_remove_accelerator(shortcut->widget, shortcut->accel_group, key->accel_key, key->accel_mods);

  // Keypad numbers register as different keys. Find the numpad equivalent key here, if any.
  guint alt_char = dt_keys_keypad_alternatives(key->accel_key);
  if(key->accel_key != alt_char)
    gtk_widget_remove_accelerator(shortcut->widget, shortcut->accel_group, alt_char, key->accel_mods);
}

void dt_accels_connect_accels(dt_accels_t *accels)
{
  for(GSList *item = accels->acceleratables; item; item = g_slist_next(item))
  {
    dt_shortcut_t *shortcut = (dt_shortcut_t *)item->data;

    GtkAccelKey key = { 0 };

    // All shortcuts should be known, they are added to accel_map at init time.
    const gboolean is_known = gtk_accel_map_lookup_entry(shortcut->path, &key);
    if(!is_known) continue;

    const GtkAccelKey oldkey = { .accel_key = shortcut->key, .accel_mods = shortcut->mods, .accel_flags = 0 };
    const dt_shortcut_type_t oldtype = shortcut->type;
    const gboolean changed = _update_shortcut_state(shortcut, &key, accels->init);

    // if old_key was non zero, we already had an accel on the stack.
    // then, if the new shortcut is different, that means we need to remove the old accel.
    const gboolean needs_cleanup = changed && oldkey.accel_key > 0 && oldtype != DT_SHORTCUT_UNSET;

    // if key is non zero and new, or updated, we need to add a new accel
    const gboolean needs_init = changed && key.accel_key > 0;

    // Adding shortcuts without defined keys makes Gtk issue warnings, so avoid it.
    if(shortcut->widget)
    {
      if(needs_cleanup) _remove_widget_accel(shortcut, &oldkey);
      if(needs_init) _add_widget_accel(shortcut, &key);
    }
    else if(shortcut->closure)
    {
      // Need to increase the number of references to avoid loosing the closure just yet.
      g_closure_ref(shortcut->closure);
      g_closure_sink(shortcut->closure);

      if(needs_cleanup)
        gtk_accel_group_disconnect(shortcut->accel_group, shortcut->closure);

      if(needs_init)
        gtk_accel_group_connect(shortcut->accel_group, key.accel_key, key.accel_mods, GTK_ACCEL_VISIBLE,
                                shortcut->closure);
      // closures can be connected only at one accel at a time, so we don't handle keypad duplicates
    }
    else
    {
      fprintf(stderr, "Invalid shortcut definition for path %s: no widget and no closure given\n", shortcut->path);
    }
  }
}


gchar *dt_accels_build_path(const gchar *scope, const gchar *feature)
{
  return g_strdup_printf("<Ansel>/%s/%s", scope, feature);
}

void _accels_keys_decode(dt_accels_t *accels, GdkEvent *event, guint *keyval, GdkModifierType *mods)
{
  // Get modifiers
  gdk_event_get_state(event, mods);

  // Remove all modifiers that are irrelevant to key strokes
  *mods &= accels->default_mod_mask;

  // Get the canonical key code, that is without the modifiers
  GdkModifierType consumed;
  gdk_keymap_translate_keyboard_state(accels->keymap, event->key.hardware_keycode, event->key.state,
                                      event->key.group, // this ensures that numlock or shift are properly decoded
                                      keyval, NULL, NULL, &consumed);

  // Remove the consumed Shift modifier for numbers.
  // For French keyboards, numbers are accessed through Shift, e.g Shift + & = 1.
  // Keeping Shift here would be meaningless and gets in the way.
  if(gdk_keyval_to_lower(*keyval) == gdk_keyval_to_upper(*keyval))
  {
    *mods &= ~consumed;
  }

  // Shift + Tab gets decoded as ISO_Left_Tab and shift is consumed,
  // so it gets absorbed by the previous correction.
  // We need Ctrl+Shift+Tab to work as expected, so correct it.
  if(*keyval == GDK_KEY_ISO_Left_Tab)
  {
    *keyval = GDK_KEY_Tab;
    *mods |= GDK_SHIFT_MASK;
  }

  // Hopefully no more heuristics required...
}

gboolean _key_pressed(GtkWidget *w, GdkEvent *event, dt_accels_t *accels)
{
  gboolean found = FALSE;

  GdkModifierType mods;
  guint keyval;
  _accels_keys_decode(accels, event, &keyval, &mods);

  //fprintf(stdout, "keystroke: %i - %i -> %s\n", keyval, mods, gdk_keyval_name(keyval));

  // Get the accelerator entry from the accel group
  gchar *accel_name = gtk_accelerator_name (keyval, mods);
  GQuark accel_quark = g_quark_from_string (accel_name);
  g_free(accel_name);

  // Look into the active group first, aka darkroom, lighttable, etc.
  if(gtk_accel_group_activate(accels->active_group, accel_quark, G_OBJECT(w), keyval, mods))
    found = TRUE;

  // If nothing found, try again with global accels.
  if(!found && gtk_accel_group_activate(accels->global_accels, accel_quark, G_OBJECT(w), keyval, mods))
    found = TRUE;

  return found;
}


gboolean dt_accels_dispatch(GtkWidget *w, GdkEvent *event, gpointer user_data)
{
  dt_accels_t *accels = (dt_accels_t *)user_data;

  // Ditch everything that is not a key stroke or key strokes that are modifiers alone
  // Abort early for performance.
  if(event->type != GDK_KEY_PRESS
     || accels->active_group == NULL
     || event->key.is_modifier || accels->reset > 0)
    return FALSE;

  return _key_pressed(w, event, accels);

  // If return == FALSE, it is possible that we messed-up key value decoding
  // Default Gtk shortcuts handler will have another chance since the accel groups
  // are connected to the window in a standard way.
}
