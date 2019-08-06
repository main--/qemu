/*
 * QEMU Secure Encrypted Virutualization (SEV) support
 *
 * Copyright: Advanced Micro Devices, 2016-2018
 *
 * Authors:
 *  Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SEV_H
#define QEMU_SEV_H

#include "sysemu/kvm.h"

void *sev_guest_init(const char *id);
int sev_encrypt_data(void *handle, uint8_t *ptr, uint64_t len);
int sev_save_setup(void *handle, const char *pdh, const char *plat_cert,
                   const char *amd_cert);
int sev_save_outgoing_page(void *handle, QEMUFile *f, uint8_t *ptr,
                           uint32_t size, uint64_t *bytes_sent);
#endif
