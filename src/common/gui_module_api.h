#pragma once

#include <glib.h>


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
typedef struct dt_gui_module_t
{
  /* list of children widgets */
  GSList *widget_list;
  GSList *widget_list_bh;

  /** translated name of the module */
  char *name;

  /** translated name of the view */
  char *view;

} dt_gui_module_t;

/* Cast dt_lib_module_t and dt_iop_module_t to dt_gui_module_t */
#define DT_GUI_MODULE(x) ((dt_gui_module_t *)x)
