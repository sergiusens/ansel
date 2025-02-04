#include "common/history.h"

#include <glib.h>

#pragma once

/**
 * @file develop/dev_history.h
 *
 * The `common/history.h` defines methods to handle histories from/to database.
 * They work out of any GUI or development stack, so they don't care about modules .so.
 * This file defines binders between that and the GUI/dev objects.
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DT_IOP_PARAMS_T
#define DT_IOP_PARAMS_T
typedef void dt_iop_params_t;
#endif

struct dt_iop_module_t;
struct dt_develop_blend_params_t;
struct dt_develop_t;

#ifdef __cplusplus
}
#endif


typedef struct dt_dev_history_item_t
{
  struct dt_iop_module_t *module; // pointer to image operation module
  gboolean enabled;               // switched respective module on/off
  dt_iop_params_t *params;        // parameters for this operation
  struct dt_develop_blend_params_t *blend_params;
  char op_name[32];
  int iop_order;
  int multi_priority;
  char multi_name[128];
  GList *forms; // snapshot of dt_develop_t->forms
  int num; // num of history on database

  uint64_t hash; // module params hash.
} dt_dev_history_item_t;


/** Free the whole GList attached to dev->history  */
void dt_dev_history_free_history(GList *history);

/* WARNING: non-thread-safe. Should be called in function locking the dev->history_mutex lock */
const dt_dev_history_item_t *dt_dev_get_history_item(struct dt_develop_t *dev, struct dt_iop_module_t *module);
/* Return TRUE if the pipeline topology may need to be updated, aka new module node inserted */
gboolean dt_dev_add_history_item_ext(struct dt_develop_t *dev, struct dt_iop_module_t *module, gboolean enable, gboolean force_new_item,
                                     gboolean no_image, gboolean include_masks);
void dt_dev_add_history_item_real(struct dt_develop_t *dev, struct dt_iop_module_t *module, gboolean enable);
#define dt_dev_add_history_item(dev, module, enable) DT_DEBUG_TRACE_WRAPPER(DT_DEBUG_DEV, dt_dev_add_history_item_real, (dev), (module), (enable))

void dt_dev_reload_history_items(struct dt_develop_t *dev);
void dt_dev_pop_history_items_ext(struct dt_develop_t *dev, int32_t cnt);
void dt_dev_pop_history_items(struct dt_develop_t *dev, int32_t cnt);
void dt_dev_write_history_ext(GList *dev_history, GList *iop_order_list, const int imgid);
void dt_dev_write_history_end_ext(const int history_end, const int imgid);
void dt_dev_write_history(struct dt_develop_t *dev);
int dt_dev_write_history_item(const int imgid, dt_dev_history_item_t *h, int32_t num);
void dt_dev_read_history_ext(struct dt_develop_t *dev, const int imgid, gboolean no_image);
void dt_dev_read_history(struct dt_develop_t *dev);
void dt_dev_free_history_item(gpointer data);
void dt_dev_invalidate_history_module(GList *list, struct dt_iop_module_t *module);

// We allow pipelines to run partial histories, up to a certain index
// stored privately in dev->history_end. Use these getter/setters
// that will check validity, instead of directly reading/writing the private data.

// Get the index of the last active history element from a GUI perspective.
// It means that dev->history_end is shifted by a +1 offset, so index 0 is the raw image,
// therefore outside of the actual dev->history list, then dev->history_end = 1 is
// actually the first element of history, and dev->history_end = length(dev->history) is the last.
// Note: the value is sanitized with the actual history size.
// It needs to run after dev->history is fully populated
int32_t dt_dev_get_history_end(struct dt_develop_t *dev);

// Set the index of the last active history element from a GUI perspective.
// It means that dev->history_end is shifted by a +1 offset, so index 0 is the raw image,
// therefore outside of the actual dev->history list, then dev->history_end = 1 is
// actually the first element of history, and dev->history_end = length(dev->history) is the last.
// Note: the value is sanitized with the actual history size.
// It needs to run after dev->history is fully populated
void dt_dev_set_history_end(struct dt_develop_t *dev, const uint32_t index);


void dt_dev_get_history_item_label(dt_dev_history_item_t *hist, char *label, const int cnt);


gboolean dt_history_module_skip_copy(const int flags);


/** adds to dev_dest module mod_src */
int dt_history_merge_module_into_history(struct dt_develop_t *dev_dest, struct dt_develop_t *dev_src, struct dt_iop_module_t *mod_src, GList **_modules_used);


/** copy history from imgid and pasts on dest_imgid, merge or overwrite... */
int dt_history_copy_and_paste_on_image(int32_t imgid, int32_t dest_imgid, GList *ops, gboolean copy_iop_order, const gboolean copy_full);
