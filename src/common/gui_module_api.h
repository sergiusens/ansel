#pragma once

#include <glib.h>

typedef struct dt_gui_module_t dt_gui_module_t;

/**
 * @brief
 *
 * The dt_gui_module_t type is the intersection between a dt_lib_module_t and a
 * dt_iop_module_t structure. It acts as an abstract class from which we can connect to
 * the common fields of both structures, for the sake of blindly connecting bauhaus widgets without
 * inheriting modules. Indeed, modules need to inheritate the bauhaus API to instanciate its widgets.
 * But then, if the bauhaus API also inheritates modules, the circular dependency becomes a mess.
 * This allows to reference parent modules in bauhaus widget without inheriting their API,
 * and without caring if the parent is a dt_iop_module_t or a dt_lib_module_t.
 *
 * The beginning of both structures needs to match exactly this abstract class, so
 * we can cast them when needed.
 *
 * Warning: keep in sync with the number and order of elements in libs/lib.h and develop/imageop.h
 */

struct dt_gui_module_t
{
  /* list of children widgets */
  GList *widget_list;
  GList *widget_list_bh;

  /** translated name of the module */
  char *name;

  char *instance_name;

  /** translated name of the view */
  char *view;

  /** this module will not appear in view for new edits */
  gboolean deprecated;

  /** give focus to the current module and adapt other parts of the GUI if needed
   * @param toggle if TRUE, adopt a show/hide behaviour. Otherwise, always show.
  */
  int (*focus)(dt_gui_module_t *module, gboolean toggle);

  char *accel_path;
};

/* Cast dt_lib_module_t and dt_iop_module_t to dt_gui_module_t */
#define DT_GUI_MODULE(x) ((dt_gui_module_t *)x)
