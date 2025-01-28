#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif


/**
 * @brief Find the numpad equivalent key of any given key.
 * Use this to define/handle alternative shortcuts.
 *
 * @param key_val
 * @return guint
 */
guint dt_accels_keypad_alternatives(const guint key_val);
