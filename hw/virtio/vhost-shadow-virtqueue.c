/*
 * vhost shadow virtqueue
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/virtio/vhost-shadow-virtqueue.h"

#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "linux-headers/linux/vhost.h"

/**
 * Validate the transport device features that both guests can use with the SVQ
 * and SVQs can use with the device.
 *
 * @dev_features  The features. If success, the acknowledged features. If
 *                failure, the minimal set from it.
 *
 * Returns true if SVQ can go with a subset of these, false otherwise.
 */
bool vhost_svq_valid_features(uint64_t *features)
{
    bool r = true;

    for (uint64_t b = VIRTIO_TRANSPORT_F_START; b <= VIRTIO_TRANSPORT_F_END;
         ++b) {
        switch (b) {
        case VIRTIO_F_ANY_LAYOUT:
            continue;

        case VIRTIO_F_ACCESS_PLATFORM:
            /* SVQ trust in the host's IOMMU to translate addresses */
        case VIRTIO_F_VERSION_1:
            /* SVQ trust that the guest vring is little endian */
            if (!(*features & BIT_ULL(b))) {
                set_bit(b, features);
                r = false;
            }
            continue;

        default:
            if (*features & BIT_ULL(b)) {
                clear_bit(b, features);
            }
        }
    }

    return r;
}

/**
 * Number of descriptors that the SVQ can make available from the guest.
 *
 * @svq   The svq
 */
static uint16_t vhost_svq_available_slots(const VhostShadowVirtqueue *svq)
{
    return svq->vring.num - (svq->avail_idx_shadow - svq->shadow_used_idx);
}

static void vhost_svq_set_notification(VhostShadowVirtqueue *svq, bool enable)
{
    uint16_t notification_flag;

    if (svq->notification == enable) {
        return;
    }

    notification_flag = cpu_to_le16(VRING_AVAIL_F_NO_INTERRUPT);

    svq->notification = enable;
    if (enable) {
        svq->vring.avail->flags &= ~notification_flag;
    } else {
        svq->vring.avail->flags |= notification_flag;
        /* Make sure the flag is written before the read of used_idx */
        smp_mb();
    }
}

static void vhost_vring_write_descs(VhostShadowVirtqueue *svq,
                                    const struct iovec *iovec,
                                    size_t num, bool more_descs, bool write)
{
    uint16_t i = svq->free_head, last = svq->free_head;
    unsigned n;
    uint16_t flags = write ? cpu_to_le16(VRING_DESC_F_WRITE) : 0;
    vring_desc_t *descs = svq->vring.desc;

    if (num == 0) {
        return;
    }

    for (n = 0; n < num; n++) {
        if (more_descs || (n + 1 < num)) {
            descs[i].flags = flags | cpu_to_le16(VRING_DESC_F_NEXT);
        } else {
            descs[i].flags = flags;
        }
        descs[i].addr = cpu_to_le64((hwaddr)iovec[n].iov_base);
        descs[i].len = cpu_to_le32(iovec[n].iov_len);

        last = i;
        i = cpu_to_le16(descs[i].next);
    }

    svq->free_head = le16_to_cpu(descs[last].next);
}

static bool vhost_svq_add_split(VhostShadowVirtqueue *svq,
                                VirtQueueElement *elem,
                                unsigned *head)
{
    unsigned avail_idx;
    vring_avail_t *avail = svq->vring.avail;

    *head = svq->free_head;

    /* We need some descriptors here */
    if (unlikely(!elem->out_num && !elem->in_num)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "Guest provided element with no descriptors");
        return false;
    }

    vhost_vring_write_descs(svq, elem->out_sg, elem->out_num,
                            elem->in_num > 0, false);
    vhost_vring_write_descs(svq, elem->in_sg, elem->in_num, false, true);

    /*
     * Put the entry in the available array (but don't update avail->idx until
     * they do sync).
     */
    avail_idx = svq->avail_idx_shadow & (svq->vring.num - 1);
    avail->ring[avail_idx] = cpu_to_le16(*head);
    svq->avail_idx_shadow++;

    /* Update the avail index after write the descriptor */
    smp_wmb();
    avail->idx = cpu_to_le16(svq->avail_idx_shadow);

    return true;
}

static bool vhost_svq_add(VhostShadowVirtqueue *svq, VirtQueueElement *elem)
{
    unsigned qemu_head;
    bool ok = vhost_svq_add_split(svq, elem, &qemu_head);
    if (unlikely(!ok)) {
        return false;
    }

    svq->ring_id_maps[qemu_head] = elem;
    return true;
}

static void vhost_svq_kick(VhostShadowVirtqueue *svq)
{
    /*
     * We need to expose the available array entries before checking the used
     * flags
     */
    smp_mb();
    if (svq->vring.used->flags & VRING_USED_F_NO_NOTIFY) {
        return;
    }

    event_notifier_set(&svq->hdev_kick);
}

/**
 * Forward available buffers.
 *
 * @svq Shadow VirtQueue
 *
 * Note that this function does not guarantee that all guest's available
 * buffers are available to the device in SVQ avail ring. The guest may have
 * exposed a GPA / GIOVA contiguous buffer, but it may not be contiguous in
 * qemu vaddr.
 *
 * If that happens, guest's kick notifications will be disabled until the
 * device uses some buffers.
 */
static void vhost_handle_guest_kick(VhostShadowVirtqueue *svq)
{
    /* Clear event notifier */
    event_notifier_test_and_clear(&svq->svq_kick);

    /* Forward to the device as many available buffers as possible */
    do {
        virtio_queue_set_notification(svq->vq, false);

        while (true) {
            VirtQueueElement *elem;
            bool ok;

            if (svq->next_guest_avail_elem) {
                elem = g_steal_pointer(&svq->next_guest_avail_elem);
            } else {
                elem = virtqueue_pop(svq->vq, sizeof(*elem));
            }

            if (!elem) {
                break;
            }

            if (elem->out_num + elem->in_num >
                vhost_svq_available_slots(svq)) {
                /*
                 * This condition is possible since a contiguous buffer in GPA
                 * does not imply a contiguous buffer in qemu's VA
                 * scatter-gather segments. If that happens, the buffer exposed
                 * to the device needs to be a chain of descriptors at this
                 * moment.
                 *
                 * SVQ cannot hold more available buffers if we are here:
                 * queue the current guest descriptor and ignore further kicks
                 * until some elements are used.
                 */
                svq->next_guest_avail_elem = elem;
                return;
            }

            ok = vhost_svq_add(svq, elem);
            if (unlikely(!ok)) {
                /* VQ is broken, just return and ignore any other kicks */
                return;
            }
            vhost_svq_kick(svq);
        }

        virtio_queue_set_notification(svq->vq, true);
    } while (!virtio_queue_empty(svq->vq));
}

/**
 * Handle guest's kick.
 *
 * @n guest kick event notifier, the one that guest set to notify svq.
 */
static void vhost_handle_guest_kick_notifier(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             svq_kick);
    event_notifier_test_and_clear(n);
    vhost_handle_guest_kick(svq);
}

static bool vhost_svq_more_used(VhostShadowVirtqueue *svq)
{
    if (svq->last_used_idx != svq->shadow_used_idx) {
        return true;
    }

    svq->shadow_used_idx = cpu_to_le16(svq->vring.used->idx);

    return svq->last_used_idx != svq->shadow_used_idx;
}

static VirtQueueElement *vhost_svq_get_buf(VhostShadowVirtqueue *svq)
{
    vring_desc_t *descs = svq->vring.desc;
    const vring_used_t *used = svq->vring.used;
    vring_used_elem_t used_elem;
    uint16_t last_used;

    if (!vhost_svq_more_used(svq)) {
        return NULL;
    }

    /* Only get used array entries after they have been exposed by dev */
    smp_rmb();
    last_used = svq->last_used_idx & (svq->vring.num - 1);
    used_elem.id = le32_to_cpu(used->ring[last_used].id);
    used_elem.len = le32_to_cpu(used->ring[last_used].len);

    svq->last_used_idx++;
    if (unlikely(used_elem.id >= svq->vring.num)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Device %s says index %u is used",
                      svq->vdev->name, used_elem.id);
        return NULL;
    }

    if (unlikely(!svq->ring_id_maps[used_elem.id])) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "Device %s says index %u is used, but it was not available",
            svq->vdev->name, used_elem.id);
        return NULL;
    }

    descs[used_elem.id].next = svq->free_head;
    svq->free_head = used_elem.id;

    svq->ring_id_maps[used_elem.id]->len = used_elem.len;
    return g_steal_pointer(&svq->ring_id_maps[used_elem.id]);
}

static void vhost_svq_flush(VhostShadowVirtqueue *svq,
                            bool check_for_avail_queue)
{
    VirtQueue *vq = svq->vq;

    /* Forward as many used buffers as possible. */
    do {
        unsigned i = 0;

        vhost_svq_set_notification(svq, false);
        while (true) {
            g_autofree VirtQueueElement *elem = vhost_svq_get_buf(svq);
            if (!elem) {
                break;
            }

            if (unlikely(i >= svq->vring.num)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                         "More than %u used buffers obtained in a %u size SVQ",
                         i, svq->vring.num);
                virtqueue_fill(vq, elem, elem->len, i);
                virtqueue_flush(vq, i);
                return;
            }
            virtqueue_fill(vq, elem, elem->len, i++);
        }

        virtqueue_flush(vq, i);
        event_notifier_set(&svq->svq_call);

        if (check_for_avail_queue && svq->next_guest_avail_elem) {
            /*
             * Avail ring was full when vhost_svq_flush was called, so it's a
             * good moment to make more descriptors available if possible.
             */
            vhost_handle_guest_kick(svq);
        }

        vhost_svq_set_notification(svq, true);
    } while (vhost_svq_more_used(svq));
}

/**
 * Forward used buffers.
 *
 * @n hdev call event notifier, the one that device set to notify svq.
 *
 * Note that we are not making any buffers available in the loop, there is no
 * way that it runs more than virtqueue size times.
 */
static void vhost_svq_handle_call(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue,
                                             hdev_call);
    event_notifier_test_and_clear(n);
    vhost_svq_flush(svq, true);
}

/**
 * Set the call notifier for the SVQ to call the guest
 *
 * @svq Shadow virtqueue
 * @call_fd call notifier
 *
 * Called on BQL context.
 */
void vhost_svq_set_guest_call_notifier(VhostShadowVirtqueue *svq, int call_fd)
{
    if (call_fd == VHOST_FILE_UNBIND) {
        /*
         * Fail event_notifier_set if called handling device call.
         *
         * SVQ still needs device notifications, since it needs to keep
         * forwarding used buffers even with the unbind.
         */
        memset(&svq->svq_call, 0, sizeof(svq->svq_call));
    } else {
        event_notifier_init_fd(&svq->svq_call, call_fd);
    }
}

/*
 * Get the shadow vq vring address.
 * @svq Shadow virtqueue
 * @addr Destination to store address
 */
void vhost_svq_get_vring_addr(const VhostShadowVirtqueue *svq,
                              struct vhost_vring_addr *addr)
{
    addr->desc_user_addr = (uint64_t)svq->vring.desc;
    addr->avail_user_addr = (uint64_t)svq->vring.avail;
    addr->used_user_addr = (uint64_t)svq->vring.used;
}

size_t vhost_svq_driver_area_size(const VhostShadowVirtqueue *svq)
{
    size_t desc_size = sizeof(vring_desc_t) * svq->vring.num;
    size_t avail_size = offsetof(vring_avail_t, ring) +
                                             sizeof(uint16_t) * svq->vring.num;

    return ROUND_UP(desc_size + avail_size, qemu_real_host_page_size);
}

size_t vhost_svq_device_area_size(const VhostShadowVirtqueue *svq)
{
    size_t used_size = offsetof(vring_used_t, ring) +
                                    sizeof(vring_used_elem_t) * svq->vring.num;
    return ROUND_UP(used_size, qemu_real_host_page_size);
}

/**
 * Set a new file descriptor for the guest to kick the SVQ and notify for avail
 *
 * @svq          The svq
 * @svq_kick_fd  The svq kick fd
 *
 * Note that the SVQ will never close the old file descriptor.
 */
void vhost_svq_set_svq_kick_fd(VhostShadowVirtqueue *svq, int svq_kick_fd)
{
    EventNotifier *svq_kick = &svq->svq_kick;
    bool poll_stop = VHOST_FILE_UNBIND != event_notifier_get_fd(svq_kick);
    bool poll_start = svq_kick_fd != VHOST_FILE_UNBIND;

    if (poll_stop) {
        event_notifier_set_handler(svq_kick, NULL);
    }

    /*
     * event_notifier_set_handler already checks for guest's notifications if
     * they arrive at the new file descriptor in the switch, so there is no
     * need to explicitly check for them.
     */
    if (poll_start) {
        event_notifier_init_fd(svq_kick, svq_kick_fd);
        event_notifier_set(svq_kick);
        event_notifier_set_handler(svq_kick, vhost_handle_guest_kick_notifier);
    }
}

/**
 * Start the shadow virtqueue operation.
 *
 * @svq Shadow Virtqueue
 * @vdev        VirtIO device
 * @vq          Virtqueue to shadow
 */
void vhost_svq_start(VhostShadowVirtqueue *svq, VirtIODevice *vdev,
                     VirtQueue *vq)
{
    size_t desc_size, driver_size, device_size;

    svq->next_guest_avail_elem = NULL;
    svq->avail_idx_shadow = 0;
    svq->shadow_used_idx = 0;
    svq->last_used_idx = 0;
    svq->vdev = vdev;
    svq->vq = vq;

    svq->vring.num = virtio_queue_get_num(vdev, virtio_get_queue_index(vq));
    driver_size = vhost_svq_driver_area_size(svq);
    device_size = vhost_svq_device_area_size(svq);
    svq->vring.desc = qemu_memalign(qemu_real_host_page_size, driver_size);
    desc_size = sizeof(vring_desc_t) * svq->vring.num;
    svq->vring.avail = (void *)((char *)svq->vring.desc + desc_size);
    memset(svq->vring.desc, 0, driver_size);
    svq->vring.used = qemu_memalign(qemu_real_host_page_size, device_size);
    memset(svq->vring.used, 0, device_size);
    svq->ring_id_maps = g_new0(VirtQueueElement *, svq->vring.num);
    for (unsigned i = 0; i < svq->vring.num - 1; i++) {
        svq->vring.desc[i].next = cpu_to_le16(i + 1);
    }
}

/**
 * Stop the shadow virtqueue operation.
 * @svq Shadow Virtqueue
 */
void vhost_svq_stop(VhostShadowVirtqueue *svq)
{
    event_notifier_set_handler(&svq->svq_kick, NULL);
    g_autofree VirtQueueElement *next_avail_elem = NULL;

    if (!svq->vq) {
        return;
    }

    /* Send all pending used descriptors to guest */
    vhost_svq_flush(svq, false);

    for (unsigned i = 0; i < svq->vring.num; ++i) {
        g_autofree VirtQueueElement *elem = NULL;
        elem = g_steal_pointer(&svq->ring_id_maps[i]);
        if (elem) {
            virtqueue_detach_element(svq->vq, elem, elem->len);
        }
    }

    next_avail_elem = g_steal_pointer(&svq->next_guest_avail_elem);
    if (next_avail_elem) {
        virtqueue_detach_element(svq->vq, next_avail_elem,
                                 next_avail_elem->len);
    }
    svq->vq = NULL;
    g_free(svq->ring_id_maps);
    qemu_vfree(svq->vring.desc);
    qemu_vfree(svq->vring.used);
}

/**
 * Creates vhost shadow virtqueue, and instructs the vhost device to use the
 * shadow methods and file descriptors.
 *
 * Returns the new virtqueue or NULL.
 *
 * In case of error, reason is reported through error_report.
 */
VhostShadowVirtqueue *vhost_svq_new(void)
{
    g_autofree VhostShadowVirtqueue *svq = g_new0(VhostShadowVirtqueue, 1);
    int r;

    r = event_notifier_init(&svq->hdev_kick, 0);
    if (r != 0) {
        error_report("Couldn't create kick event notifier: %s (%d)",
                     g_strerror(errno), errno);
        goto err_init_hdev_kick;
    }

    r = event_notifier_init(&svq->hdev_call, 0);
    if (r != 0) {
        error_report("Couldn't create call event notifier: %s (%d)",
                     g_strerror(errno), errno);
        goto err_init_hdev_call;
    }

    event_notifier_init_fd(&svq->svq_kick, VHOST_FILE_UNBIND);
    event_notifier_set_handler(&svq->hdev_call, vhost_svq_handle_call);
    return g_steal_pointer(&svq);

err_init_hdev_call:
    event_notifier_cleanup(&svq->hdev_kick);

err_init_hdev_kick:
    return NULL;
}

/**
 * Free the resources of the shadow virtqueue.
 *
 * @pvq  gpointer to SVQ so it can be used by autofree functions.
 */
void vhost_svq_free(gpointer pvq)
{
    VhostShadowVirtqueue *vq = pvq;
    vhost_svq_stop(vq);
    event_notifier_cleanup(&vq->hdev_kick);
    event_notifier_set_handler(&vq->hdev_call, NULL);
    event_notifier_cleanup(&vq->hdev_call);
    g_free(vq);
}
