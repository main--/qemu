#ifndef HW_NVME_ZNS_H
#define HW_NVME_ZNS_H

#include "qemu/units.h"

#include "nvme.h"
#include "nvm.h"

#define NVME_DEFAULT_ZONE_SIZE   (128 * MiB)

#define TYPE_NVME_NAMESPACE_ZONED "x-nvme-ns-zoned"
OBJECT_DECLARE_SIMPLE_TYPE(NvmeNamespaceZoned, NVME_NAMESPACE_ZONED)

typedef struct NvmeZone {
    NvmeZoneDescr   d;
    uint64_t        w_ptr;
    QTAILQ_ENTRY(NvmeZone) entry;
} NvmeZone;

enum {
    NVME_NS_ZONED_CROSS_READ = 1 << 0,
};

typedef struct NvmeNamespaceZoned {
    NvmeNamespaceNvm parent_obj;

    NvmeIdNsZoned id_ns;

    uint32_t num_zones;
    NvmeZone *zone_array;

    uint64_t zone_size;
    uint32_t zone_size_log2;

    uint64_t zone_capacity;

    uint32_t zd_extension_size;
    uint8_t  *zd_extensions;

    uint32_t max_open_zones;
    int32_t  nr_open_zones;
    uint32_t max_active_zones;
    int32_t  nr_active_zones;

    unsigned long flags;

    QTAILQ_HEAD(, NvmeZone) exp_open_zones;
    QTAILQ_HEAD(, NvmeZone) imp_open_zones;
    QTAILQ_HEAD(, NvmeZone) closed_zones;
    QTAILQ_HEAD(, NvmeZone) full_zones;
} NvmeNamespaceZoned;

static inline NvmeZoneState nvme_zns_state(NvmeZone *zone)
{
    return zone->d.zs >> 4;
}

static inline void nvme_zns_set_state(NvmeZone *zone, NvmeZoneState state)
{
    zone->d.zs = state << 4;
}

static inline uint64_t nvme_zns_read_boundary(NvmeNamespaceZoned *zoned,
                                              NvmeZone *zone)
{
    return zone->d.zslba + zoned->zone_size;
}

static inline uint64_t nvme_zns_write_boundary(NvmeZone *zone)
{
    return zone->d.zslba + zone->d.zcap;
}

static inline bool nvme_zns_wp_valid(NvmeZone *zone)
{
    uint8_t st = nvme_zns_state(zone);

    return st != NVME_ZONE_STATE_FULL &&
           st != NVME_ZONE_STATE_READ_ONLY &&
           st != NVME_ZONE_STATE_OFFLINE;
}

static inline uint32_t nvme_zns_zidx(NvmeNamespaceZoned *zoned,
                                     uint64_t slba)
{
    return zoned->zone_size_log2 > 0 ?
        slba >> zoned->zone_size_log2 : slba / zoned->zone_size;
}

static inline NvmeZone *nvme_zns_get_by_slba(NvmeNamespaceZoned *zoned,
                                             uint64_t slba)
{
    uint32_t zone_idx = nvme_zns_zidx(zoned, slba);

    assert(zone_idx < zoned->num_zones);
    return &zoned->zone_array[zone_idx];
}

static inline uint8_t *nvme_zns_zde(NvmeNamespaceZoned *zoned,
                                    uint32_t zone_idx)
{
    return &zoned->zd_extensions[zone_idx * zoned->zd_extension_size];
}

static inline void nvme_zns_aor_inc_open(NvmeNamespaceZoned *zoned)
{
    assert(zoned->nr_open_zones >= 0);
    if (zoned->max_open_zones) {
        zoned->nr_open_zones++;
        assert(zoned->nr_open_zones <= zoned->max_open_zones);
    }
}

static inline void nvme_zns_aor_dec_open(NvmeNamespaceZoned *zoned)
{
    if (zoned->max_open_zones) {
        assert(zoned->nr_open_zones > 0);
        zoned->nr_open_zones--;
    }
    assert(zoned->nr_open_zones >= 0);
}

static inline void nvme_zns_aor_inc_active(NvmeNamespaceZoned *zoned)
{
    assert(zoned->nr_active_zones >= 0);
    if (zoned->max_active_zones) {
        zoned->nr_active_zones++;
        assert(zoned->nr_active_zones <= zoned->max_active_zones);
    }
}

static inline void nvme_zns_aor_dec_active(NvmeNamespaceZoned *zoned)
{
    if (zoned->max_active_zones) {
        assert(zoned->nr_active_zones > 0);
        zoned->nr_active_zones--;
        assert(zoned->nr_active_zones >= zoned->nr_open_zones);
    }
    assert(zoned->nr_active_zones >= 0);
}

void nvme_zns_init_state(NvmeNamespaceZoned *zoned);
int nvme_zns_configure(NvmeNamespace *ns, Error **errp);
void nvme_zns_clear_zone(NvmeNamespaceZoned *zoned, NvmeZone *zone);
void nvme_zns_shutdown(NvmeNamespace *ns);

#endif /* HW_NVME_ZNS_H */
