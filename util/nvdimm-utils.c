#include "qemu/osdep.h"
#include "qemu/nvdimm-utils.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/mem/nvdimm.h"

static int nvdimm_device_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_NVDIMM)) {
        *list = g_slist_append(*list, DEVICE(obj));
    }

    object_child_foreach(obj, nvdimm_device_list, opaque);
    return 0;
}

/*
 * inquire NVDIMM devices and link them into the list which is
 * returned to the caller.
 *
 * Note: it is the caller's responsibility to free the list to avoid
 * memory leak.
 */
GSList *nvdimm_get_device_list(void)
{
    GSList *list = NULL;

    object_child_foreach(qdev_get_machine(), nvdimm_device_list, &list);
    return list;
}

int nvdimm_check_target_nodes(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    int nb_numa_nodes = ms->numa_state->num_nodes;
    int node;

    if (!nvdimm_max_target_node) {
        return -1;
    }

    for (node = nb_numa_nodes; node <= nvdimm_max_target_node; node++) {
        if (!test_bit(node, nvdimm_target_nodes)) {
            error_report("nvdimm target-node: Node ID missing: %d", node);
            exit(1);
        }
    }

    return nvdimm_max_target_node;
}
