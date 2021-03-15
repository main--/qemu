/*
 * vhost software live migration ring
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VHOST_SHADOW_VIRTQUEUE_H
#define VHOST_SHADOW_VIRTQUEUE_H

#include "qemu/osdep.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"

typedef struct VhostShadowVirtqueue VhostShadowVirtqueue;

void vhost_shadow_vq_mask(VhostShadowVirtqueue *svq, EventNotifier *masked);
void vhost_shadow_vq_unmask(VhostShadowVirtqueue *svq);
void vhost_shadow_vq_get_vring_addr(const VhostShadowVirtqueue *svq,
                                    struct vhost_vring_addr *addr);

bool vhost_shadow_vq_start(struct vhost_dev *dev,
                           unsigned idx,
                           VhostShadowVirtqueue *svq);
void vhost_shadow_vq_stop(struct vhost_dev *dev,
                          unsigned idx,
                          VhostShadowVirtqueue *svq);

VhostShadowVirtqueue *vhost_shadow_vq_new(struct vhost_dev *dev, int idx);

void vhost_shadow_vq_free(VhostShadowVirtqueue *vq);

#endif
