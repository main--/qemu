/*
 * QEMU Management Protocol commands
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "qemu/option.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"
#include "qemu/config-file.h"
#include "qemu/uuid.h"
#include "chardev/char.h"
#include "ui/qemu-spice.h"
#include "ui/console.h"
#include "ui/dbus-display.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "sysemu/runstate-action.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-acpi.h"
#include "qapi/qapi-commands-block.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/qapi-commands-stats.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/type-helpers.h"
#include "qapi/qmp/qerror.h"
#include "exec/ramlist.h"
#include "hw/mem/memory-device.h"
#include "hw/acpi/acpi_dev_interface.h"
#include "hw/intc/intc.h"
#include "hw/rdma/rdma.h"
#include "monitor/stats.h"

NameInfo *qmp_query_name(Error **errp)
{
    NameInfo *info = g_malloc0(sizeof(*info));

    if (qemu_name) {
        info->has_name = true;
        info->name = g_strdup(qemu_name);
    }

    return info;
}

KvmInfo *qmp_query_kvm(Error **errp)
{
    KvmInfo *info = g_malloc0(sizeof(*info));

    info->enabled = kvm_enabled();
    info->present = accel_find("kvm");

    return info;
}

UuidInfo *qmp_query_uuid(Error **errp)
{
    UuidInfo *info = g_malloc0(sizeof(*info));

    info->UUID = qemu_uuid_unparse_strdup(&qemu_uuid);
    return info;
}

void qmp_quit(Error **errp)
{
    shutdown_action = SHUTDOWN_ACTION_POWEROFF;
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_QMP_QUIT);
}

void qmp_stop(Error **errp)
{
    /* if there is a dump in background, we should wait until the dump
     * finished */
    if (dump_in_progress()) {
        error_setg(errp, "There is a dump in process, please wait.");
        return;
    }

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        autostart = 0;
    } else {
        vm_stop(RUN_STATE_PAUSED);
    }
}

void qmp_system_reset(Error **errp)
{
    qemu_system_reset_request(SHUTDOWN_CAUSE_HOST_QMP_SYSTEM_RESET);
}

void qmp_system_powerdown(Error **errp)
{
    qemu_system_powerdown_request();
}

void qmp_cont(Error **errp)
{
    BlockBackend *blk;
    BlockJob *job;
    Error *local_err = NULL;

    /* if there is a dump in background, we should wait until the dump
     * finished */
    if (dump_in_progress()) {
        error_setg(errp, "There is a dump in process, please wait.");
        return;
    }

    if (runstate_needs_reset()) {
        error_setg(errp, "Resetting the Virtual Machine is required");
        return;
    } else if (runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    } else if (runstate_check(RUN_STATE_FINISH_MIGRATE)) {
        error_setg(errp, "Migration is not finalized yet");
        return;
    }

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        blk_iostatus_reset(blk);
    }

    for (job = block_job_next(NULL); job; job = block_job_next(job)) {
        block_job_iostatus_reset(job);
    }

    /* Continuing after completed migration. Images have been inactivated to
     * allow the destination to take control. Need to get control back now.
     *
     * If there are no inactive block nodes (e.g. because the VM was just
     * paused rather than completing a migration), bdrv_inactivate_all() simply
     * doesn't do anything. */
    bdrv_invalidate_cache_all(&local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        autostart = 1;
    } else {
        vm_start();
    }
}

void qmp_system_wakeup(Error **errp)
{
    if (!qemu_wakeup_suspend_enabled()) {
        error_setg(errp,
                   "wake-up from suspend is not supported by this guest");
        return;
    }

    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, errp);
}

void qmp_set_password(const char *protocol, const char *password,
                      bool has_connected, const char *connected, Error **errp)
{
    int disconnect_if_connected = 0;
    int fail_if_connected = 0;
    int rc;

    if (has_connected) {
        if (strcmp(connected, "fail") == 0) {
            fail_if_connected = 1;
        } else if (strcmp(connected, "disconnect") == 0) {
            disconnect_if_connected = 1;
        } else if (strcmp(connected, "keep") == 0) {
            /* nothing */
        } else {
            error_setg(errp, QERR_INVALID_PARAMETER, "connected");
            return;
        }
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            return;
        }
        rc = qemu_spice.set_passwd(password, fail_if_connected,
                                   disconnect_if_connected);
    } else if (strcmp(protocol, "vnc") == 0) {
        if (fail_if_connected || disconnect_if_connected) {
            /* vnc supports "connected=keep" only */
            error_setg(errp, QERR_INVALID_PARAMETER, "connected");
            return;
        }
        /* Note that setting an empty password will not disable login through
         * this interface. */
        rc = vnc_display_password(NULL, password);
    } else {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "protocol",
                   "'vnc' or 'spice'");
        return;
    }

    if (rc != 0) {
        error_setg(errp, "Could not set password");
    }
}

void qmp_expire_password(const char *protocol, const char *whenstr,
                         Error **errp)
{
    time_t when;
    int rc;

    if (strcmp(whenstr, "now") == 0) {
        when = 0;
    } else if (strcmp(whenstr, "never") == 0) {
        when = TIME_MAX;
    } else if (whenstr[0] == '+') {
        when = time(NULL) + strtoull(whenstr+1, NULL, 10);
    } else {
        when = strtoull(whenstr, NULL, 10);
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            return;
        }
        rc = qemu_spice.set_pw_expire(when);
    } else if (strcmp(protocol, "vnc") == 0) {
        rc = vnc_display_pw_expire(NULL, when);
    } else {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "protocol",
                   "'vnc' or 'spice'");
        return;
    }

    if (rc != 0) {
        error_setg(errp, "Could not set password expire time");
    }
}

#ifdef CONFIG_VNC
void qmp_change_vnc_password(const char *password, Error **errp)
{
    if (vnc_display_password(NULL, password) < 0) {
        error_setg(errp, "Could not set password");
    }
}
#endif

void qmp_add_client(const char *protocol, const char *fdname,
                    bool has_skipauth, bool skipauth, bool has_tls, bool tls,
                    Error **errp)
{
    Chardev *s;
    int fd;

    fd = monitor_get_fd(monitor_cur(), fdname, errp);
    if (fd < 0) {
        return;
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            close(fd);
            return;
        }
        skipauth = has_skipauth ? skipauth : false;
        tls = has_tls ? tls : false;
        if (qemu_spice.display_add_client(fd, skipauth, tls) < 0) {
            error_setg(errp, "spice failed to add client");
            close(fd);
        }
        return;
#ifdef CONFIG_VNC
    } else if (strcmp(protocol, "vnc") == 0) {
        skipauth = has_skipauth ? skipauth : false;
        vnc_display_add_client(NULL, fd, skipauth);
        return;
#endif
#ifdef CONFIG_DBUS_DISPLAY
    } else if (strcmp(protocol, "@dbus-display") == 0) {
        if (!qemu_using_dbus_display(errp)) {
            close(fd);
            return;
        }
        if (!qemu_dbus_display.add_client(fd, errp)) {
            close(fd);
            return;
        }
        return;
#endif
    } else if ((s = qemu_chr_find(protocol)) != NULL) {
        if (qemu_chr_add_client(s, fd) < 0) {
            error_setg(errp, "failed to add client");
            close(fd);
            return;
        }
        return;
    }

    error_setg(errp, "protocol '%s' is invalid", protocol);
    close(fd);
}


MemoryDeviceInfoList *qmp_query_memory_devices(Error **errp)
{
    return qmp_memory_device_list();
}

ACPIOSTInfoList *qmp_query_acpi_ospm_status(Error **errp)
{
    bool ambig;
    ACPIOSTInfoList *head = NULL;
    ACPIOSTInfoList **prev = &head;
    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, &ambig);

    if (obj) {
        AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(obj);
        AcpiDeviceIf *adev = ACPI_DEVICE_IF(obj);

        adevc->ospm_status(adev, &prev);
    } else {
        error_setg(errp, "command is not supported, missing ACPI device");
    }

    return head;
}

MemoryInfo *qmp_query_memory_size_summary(Error **errp)
{
    MemoryInfo *mem_info = g_malloc0(sizeof(MemoryInfo));
    MachineState *ms = MACHINE(qdev_get_machine());

    mem_info->base_memory = ms->ram_size;

    mem_info->plugged_memory = get_plugged_memory_size();
    mem_info->has_plugged_memory =
        mem_info->plugged_memory != (uint64_t)-1;

    return mem_info;
}

void qmp_display_reload(DisplayReloadOptions *arg, Error **errp)
{
    switch (arg->type) {
    case DISPLAY_RELOAD_TYPE_VNC:
#ifdef CONFIG_VNC
        if (arg->u.vnc.has_tls_certs && arg->u.vnc.tls_certs) {
            vnc_display_reload_certs(NULL, errp);
        }
#else
        error_setg(errp, "vnc is invalid, missing 'CONFIG_VNC'");
#endif
        break;
    default:
        abort();
    }
}

static int qmp_x_query_rdma_foreach(Object *obj, void *opaque)
{
    RdmaProvider *rdma;
    RdmaProviderClass *k;
    GString *buf = opaque;

    if (object_dynamic_cast(obj, INTERFACE_RDMA_PROVIDER)) {
        rdma = RDMA_PROVIDER(obj);
        k = RDMA_PROVIDER_GET_CLASS(obj);
        if (k->format_statistics) {
            k->format_statistics(rdma, buf);
        } else {
            g_string_append_printf(buf,
                                   "RDMA statistics not available for %s.\n",
                                   object_get_typename(obj));
        }
    }

    return 0;
}

HumanReadableText *qmp_x_query_rdma(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    object_child_foreach_recursive(object_get_root(),
                                   qmp_x_query_rdma_foreach, buf);

    return human_readable_text_from_str(buf);
}

HumanReadableText *qmp_x_query_ramblock(Error **errp)
{
    g_autoptr(GString) buf = ram_block_format();

    return human_readable_text_from_str(buf);
}

static int qmp_x_query_irq_foreach(Object *obj, void *opaque)
{
    InterruptStatsProvider *intc;
    InterruptStatsProviderClass *k;
    GString *buf = opaque;

    if (object_dynamic_cast(obj, TYPE_INTERRUPT_STATS_PROVIDER)) {
        intc = INTERRUPT_STATS_PROVIDER(obj);
        k = INTERRUPT_STATS_PROVIDER_GET_CLASS(obj);
        uint64_t *irq_counts;
        unsigned int nb_irqs, i;
        if (k->get_statistics &&
            k->get_statistics(intc, &irq_counts, &nb_irqs)) {
            if (nb_irqs > 0) {
                g_string_append_printf(buf, "IRQ statistics for %s:\n",
                                       object_get_typename(obj));
                for (i = 0; i < nb_irqs; i++) {
                    if (irq_counts[i] > 0) {
                        g_string_append_printf(buf, "%2d: %" PRId64 "\n", i,
                                               irq_counts[i]);
                    }
                }
            }
        } else {
            g_string_append_printf(buf,
                                   "IRQ statistics not available for %s.\n",
                                   object_get_typename(obj));
        }
    }

    return 0;
}

HumanReadableText *qmp_x_query_irq(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");

    object_child_foreach_recursive(object_get_root(),
                                   qmp_x_query_irq_foreach, buf);

    return human_readable_text_from_str(buf);
}

typedef struct StatsCallbacks {
    StatsProvider provider;
    void (*stats_cb)(StatsResults *, StatsFilter *, Error **);
    void (*schemas_cb)(StatsSchemaResults *, Error **);
    QTAILQ_ENTRY(StatsCallbacks) next;
} StatsCallbacks;

static QTAILQ_HEAD(, StatsCallbacks) stats_callbacks =
    QTAILQ_HEAD_INITIALIZER(stats_callbacks);

void add_stats_callbacks(StatsProvider provider,
                         void (*stats_fn)(StatsResults *, StatsFilter*,
                                          Error **),
                         void (*schemas_fn)(StatsSchemaResults *, Error **))
{
    StatsCallbacks *entry = g_malloc0(sizeof(*entry));
    entry->provider = provider;
    entry->stats_cb = stats_fn;
    entry->schemas_cb = schemas_fn;

    QTAILQ_INSERT_TAIL(&stats_callbacks, entry, next);
}

static StatsRequestList *stats_target_filter(StatsFilter *filter)
{
    switch (filter->target) {
    case STATS_TARGET_VM:
        if (!filter->u.vm.has_filters) {
            return NULL;
        }
        return filter->u.vm.filters;
    case STATS_TARGET_VCPU:
        if (!filter->u.vcpu.has_filters) {
            return NULL;
        }
        return filter->u.vcpu.filters;
        break;
    default:
        return NULL;
    }
}

static bool stats_provider_match(StatsProvider provider,
                                 StatsRequestList *request)
{
    return (!request->value->has_provider ||
            (request->value->provider == provider));
}

static bool stats_requested_provider(StatsProvider provider,
                                     StatsFilter *filter)
{
    StatsRequestList *request = stats_target_filter(filter);

    if (!request) {
        return true;
    }
    for (; request; request = request->next) {
        if (stats_provider_match(provider, request)) {
            return true;
        }
    }
    return false;
}

StatsResults *qmp_query_stats(StatsFilter *filter, Error **errp)
{
    StatsResults *stats_results = g_malloc0(sizeof(*stats_results));
    StatsCallbacks *entry;

    QTAILQ_FOREACH(entry, &stats_callbacks, next) {
        if (stats_requested_provider(entry->provider, filter)) {
            entry->stats_cb(stats_results, filter, errp);
        }
    }

    return stats_results;
}

StatsSchemaResults *qmp_query_stats_schemas(bool has_provider,
                                           StatsProvider provider,
                                           Error **errp)
{
    StatsSchemaResults *stats_results = g_malloc0(sizeof(*stats_results));
    StatsCallbacks *entry;

    QTAILQ_FOREACH(entry, &stats_callbacks, next) {
        if (has_provider && (provider != entry->provider)) {
            continue;
        }
        entry->schemas_cb(stats_results, errp);
    }

    return stats_results;
}

void add_vm_stats_entry(StatsList *stats_list, StatsResults *stats_results,
                        StatsProvider provider)
{
    StatsResultsEntry *entry = g_malloc0(sizeof(*entry));
    entry->provider = provider;
    entry->stats = stats_list;

    QAPI_LIST_PREPEND(stats_results->vm, entry);
    stats_results->has_vm = true;
}

void add_vcpu_stats_entry(StatsList *stats_list, StatsResults *stats_results,
                          StatsProvider provider, char *path)
{
    StatsResultsVCPUEntry *value;
    StatsResultsEntry *entry;
    StatsResultsVCPUEntryList **tailp, *tail;

    entry = g_malloc0(sizeof(*entry));
    entry->provider = provider;
    entry->stats = stats_list;

    /* Find the vCPU entry and add to its list; else create it */
    tailp = &stats_results->vcpus;

    for (tail = *tailp; tail; tail = tail->next) {
        if (g_str_equal(tail->value->path, path)) {
            /* Add to the existing vCPU list */
            QAPI_LIST_PREPEND(tail->value->providers, entry);
            return;
        }
        tailp = &tail->next;
    }

    /* Create and populate a new vCPU entry */
    value = g_malloc0(sizeof(*value));
    value->path = g_strdup(path);
    value->providers = g_malloc0(sizeof(*value->providers));
    value->providers->value = entry;
    QAPI_LIST_APPEND(tailp, value);
    stats_results->has_vcpus = true;
}

void add_vm_stats_schema(StatsSchemaValueList *stats_list,
                         StatsSchemaResults *schema_results,
                         StatsProvider provider)
{
    StatsSchemaProvider *entry = g_malloc0(sizeof(*entry));

    entry->provider = provider;
    entry->stats = stats_list;
    QAPI_LIST_PREPEND(schema_results->vm, entry);
    schema_results->has_vm = true;
}

void add_vcpu_stats_schema(StatsSchemaValueList *stats_list,
                           StatsSchemaResults *schema_results,
                           StatsProvider provider)
{
    StatsSchemaProvider *entry = g_malloc0(sizeof(*entry));

    entry->provider = provider;
    entry->stats = stats_list;
    QAPI_LIST_PREPEND(schema_results->vcpu, entry);
    schema_results->has_vcpu = true;
}

static bool str_in_list(const char *name, strList *list)
{
    strList *str_list = NULL;

    for (str_list = list; str_list; str_list = str_list->next) {
        if (g_str_equal(name, str_list->value)) {
            return true;
        }
    }
    return false;
}

bool stats_requested_name(const char *name, StatsProvider provider,
                          StatsFilter *filter)
{
    StatsRequestList *request = stats_target_filter(filter);

    if (!request) {
        return true;
    }
    for (; request; request = request->next) {
        if (!stats_provider_match(provider, request)) {
            continue;
        }
        if (!request->value->has_fields ||
            str_in_list(name, request->value->fields)) {
            return true;
        }
    }
    return false;
}

bool stats_requested_vcpu(const char *path, StatsProvider provider,
                          StatsFilter *filter)
{
    StatsRequestList *request = stats_target_filter(filter);

    if (!request) {
        return true;
    }
    if (!filter->u.vcpu.has_filters) {
        return true;
    }
    if (filter->u.vcpu.has_vcpus && !str_in_list(path, filter->u.vcpu.vcpus)) {
        return false;
    }
    for (; request; request = request->next) {
        if (stats_provider_match(provider, request)) {
            return true;
        }
    }
    return false;
}
