#include "accelerators.h"

#include <assert.h>
#include <glib.h>


guint dt_accels_keypad_alternatives(const guint key_val)
{
  guint alt_char = key_val;
  switch(key_val)
  {
    case GDK_KEY_0:
      alt_char = GDK_KEY_KP_0;
      break;
    case GDK_KEY_1:
      alt_char = GDK_KEY_KP_1;
      break;
    case GDK_KEY_2:
      alt_char = GDK_KEY_KP_2;
      break;
    case GDK_KEY_3:
      alt_char = GDK_KEY_KP_3;
      break;
    case GDK_KEY_4:
      alt_char = GDK_KEY_KP_4;
      break;
    case GDK_KEY_5:
      alt_char = GDK_KEY_KP_5;
      break;
    case GDK_KEY_6:
      alt_char = GDK_KEY_KP_6;
      break;
    case GDK_KEY_7:
      alt_char = GDK_KEY_KP_7;
      break;
    case GDK_KEY_8:
      alt_char = GDK_KEY_KP_8;
      break;
    case GDK_KEY_9:
      alt_char = GDK_KEY_KP_9;
      break;
    case GDK_KEY_Left:
      alt_char = GDK_KEY_KP_Left;
      break;
    case GDK_KEY_Right:
      alt_char = GDK_KEY_KP_Right;
      break;
    case GDK_KEY_Up:
      alt_char = GDK_KEY_KP_Up;
      break;
    case GDK_KEY_Down:
      alt_char = GDK_KEY_KP_Down;
      break;
    case GDK_KEY_Home:
      alt_char = GDK_KEY_KP_Home;
      break;
    case GDK_KEY_End:
      alt_char = GDK_KEY_KP_End;
      break;
    case GDK_KEY_Insert:
      alt_char = GDK_KEY_KP_Insert;
      break;
    case GDK_KEY_Return:
      alt_char = GDK_KEY_KP_Enter;
      break;
    case GDK_KEY_Page_Up:
      alt_char = GDK_KEY_KP_Page_Up;
      break;
    case GDK_KEY_Page_Down:
      alt_char = GDK_KEY_KP_Page_Down;
      break;
    default:
      break;
  }

  return alt_char;
}


dt_accels_t * dt_accels_init(char *config_file)
{
  dt_accels_t *accels = malloc(sizeof(dt_accels_t));
  accels->config_file = g_strdup(config_file);
  accels->global_accels = gtk_accel_group_new();
  accels->acceleratables = NULL;
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
  g_object_unref(accels->global_accels);
  g_slist_free_full(accels->acceleratables, _clean_shortcut);
  g_free(accels->config_file);
  g_free(accels);
}

void dt_accels_connect_window(dt_accels_t *accels, GtkWindow *window)
{
  gtk_window_add_accel_group(window, accels->global_accels);
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


void dt_accels_connect_accels(dt_accels_t *accels)
{
  for(GSList *item = accels->acceleratables; item; item = g_slist_next(item))
  {
    dt_shortcut_t *shortcut = (dt_shortcut_t *)item->data;
    GtkAccelKey key = { 0 };

    // If we found a path but we have no shortcut defined for it: reset to default keys
    if(gtk_accel_map_lookup_entry(shortcut->path, &key) && key.accel_key == 0)
    {
      key.accel_key = shortcut->key;
      key.accel_mods = shortcut->mods;
      gtk_accel_map_change_entry(shortcut->path, shortcut->key, shortcut->mods, FALSE);
    }

    // Adding shortcuts without defined keys makes Gtk issue warnings, so avoid it.
    if(key.accel_key > 0)
    {
      if(shortcut->widget)
      {
        assert(shortcut->signal);

        gtk_widget_add_accelerator(shortcut->widget, shortcut->signal, shortcut->accel_group, key.accel_key,
                                   key.accel_mods, GTK_ACCEL_VISIBLE);

        // Keypad numbers register as different keys. Find the numpad equivalent key here, if any.
        guint alt_char = dt_accels_keypad_alternatives(key.accel_key);
        if(key.accel_key != alt_char)
          gtk_widget_add_accelerator(shortcut->widget, shortcut->signal, shortcut->accel_group, alt_char,
                                     key.accel_mods, GTK_ACCEL_VISIBLE);
      }
      else if(shortcut->closure)
      {
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
}


gchar *dt_accels_build_path(const gchar *scope, const gchar *feature)
{
  return g_strdup_printf("<Ansel>/%s/%s", scope, feature);
}
