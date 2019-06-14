#ifndef SYSEMU_NUMA_H
#define SYSEMU_NUMA_H

#include "qemu/bitmap.h"
#include "sysemu/sysemu.h"
#include "sysemu/hostmem.h"
#include "hw/boards.h"

struct NodeInfo {
    uint64_t node_mem;
    struct HostMemoryBackend *node_memdev;
    bool present;
    bool is_initiator;
    bool is_target;
    uint8_t distance[MAX_NODES];
};

extern NodeInfo numa_info[MAX_NODES];

struct NumaNodeMem {
    uint64_t node_mem;
    uint64_t node_plugged_mem;
};

typedef struct NumaMemRange {
    uint64_t base;
    uint64_t length;
    uint32_t node;
} NumaMemRange;

struct NumaState {
    /* Number of NUMA nodes */
    int num_nodes;

    /* Allow setting NUMA distance for different NUMA nodes */
    bool have_numa_distance;

    /* NUMA nodes information */
    NodeInfo nodes[MAX_NODES];

    /* Number of NUMA memory ranges */
    uint32_t mem_ranges_num;

    /* NUMA memory ranges */
    GArray *mem_ranges;
};
typedef struct NumaState NumaState;

void parse_numa_opts(MachineState *ms);
void numa_complete_configuration(MachineState *ms);
void query_numa_node_mem(NumaNodeMem node_mem[], MachineState *ms);
extern QemuOptsList qemu_numa_opts;
void numa_legacy_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                 int nb_nodes, ram_addr_t size);
void numa_default_auto_assign_ram(MachineClass *mc, NodeInfo *nodes,
                                  int nb_nodes, ram_addr_t size);
void numa_cpu_pre_plug(const CPUArchId *slot, DeviceState *dev, Error **errp);
#endif
