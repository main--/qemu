/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * VFIO API definition
 *
 * Copyright (C) 2012 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef VFIO_H
#define VFIO_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define VFIO_API_VERSION	0


/* Kernel & User level defines for VFIO IOCTLs. */

/* Extensions */

#define VFIO_TYPE1_IOMMU		1
#define VFIO_SPAPR_TCE_IOMMU		2
#define VFIO_TYPE1v2_IOMMU		3
/*
 * IOMMU enforces DMA cache coherence (ex. PCIe NoSnoop stripping).  This
 * capability is subject to change as groups are added or removed.
 */
#define VFIO_DMA_CC_IOMMU		4

/* Check if EEH is supported */
#define VFIO_EEH			5

/* Two-stage IOMMU */
#define VFIO_TYPE1_NESTING_IOMMU	6	/* Implies v2 */

#define VFIO_SPAPR_TCE_v2_IOMMU		7

/*
 * The No-IOMMU IOMMU offers no translation or isolation for devices and
 * supports no ioctls outside of VFIO_CHECK_EXTENSION.  Use of VFIO's No-IOMMU
 * code will taint the host kernel and should be used with extreme caution.
 */
#define VFIO_NOIOMMU_IOMMU		8

/*
 * The IOCTL interface is designed for extensibility by embedding the
 * structure length (argsz) and flags into structures passed between
 * kernel and userspace.  We therefore use the _IO() macro for these
 * defines to avoid implicitly embedding a size into the ioctl request.
 * As structure fields are added, argsz will increase to match and flag
 * bits will be defined to indicate additional fields with valid data.
 * It's *always* the caller's responsibility to indicate the size of
 * the structure passed by setting argsz appropriately.
 */

#define VFIO_TYPE	(';')
#define VFIO_BASE	100

/*
 * For extension of INFO ioctls, VFIO makes use of a capability chain
 * designed after PCI/e capabilities.  A flag bit indicates whether
 * this capability chain is supported and a field defined in the fixed
 * structure defines the offset of the first capability in the chain.
 * This field is only valid when the corresponding bit in the flags
 * bitmap is set.  This offset field is relative to the start of the
 * INFO buffer, as is the next field within each capability header.
 * The id within the header is a shared address space per INFO ioctl,
 * while the version field is specific to the capability id.  The
 * contents following the header are specific to the capability id.
 */
struct vfio_info_cap_header {
	__u16	id;		/* Identifies capability */
	__u16	version;	/* Version specific to the capability ID */
	__u32	next;		/* Offset of next capability */
};

/*
 * Callers of INFO ioctls passing insufficiently sized buffers will see
 * the capability chain flag bit set, a zero value for the first capability
 * offset (if available within the provided argsz), and argsz will be
 * updated to report the necessary buffer size.  For compatibility, the
 * INFO ioctl will not report error in this case, but the capability chain
 * will not be available.
 */

/* -------- IOCTLs for VFIO file descriptor (/dev/vfio/vfio) -------- */

/**
 * VFIO_GET_API_VERSION - _IO(VFIO_TYPE, VFIO_BASE + 0)
 *
 * Report the version of the VFIO API.  This allows us to bump the entire
 * API version should we later need to add or change features in incompatible
 * ways.
 * Return: VFIO_API_VERSION
 * Availability: Always
 */
#define VFIO_GET_API_VERSION		_IO(VFIO_TYPE, VFIO_BASE + 0)

/**
 * VFIO_CHECK_EXTENSION - _IOW(VFIO_TYPE, VFIO_BASE + 1, __u32)
 *
 * Check whether an extension is supported.
 * Return: 0 if not supported, 1 (or some other positive integer) if supported.
 * Availability: Always
 */
#define VFIO_CHECK_EXTENSION		_IO(VFIO_TYPE, VFIO_BASE + 1)

/**
 * VFIO_SET_IOMMU - _IOW(VFIO_TYPE, VFIO_BASE + 2, __s32)
 *
 * Set the iommu to the given type.  The type must be supported by an
 * iommu driver as verified by calling CHECK_EXTENSION using the same
 * type.  A group must be set to this file descriptor before this
 * ioctl is available.  The IOMMU interfaces enabled by this call are
 * specific to the value set.
 * Return: 0 on success, -errno on failure
 * Availability: When VFIO group attached
 */
#define VFIO_SET_IOMMU			_IO(VFIO_TYPE, VFIO_BASE + 2)

/* -------- IOCTLs for GROUP file descriptors (/dev/vfio/$GROUP) -------- */

/**
 * VFIO_GROUP_GET_STATUS - _IOR(VFIO_TYPE, VFIO_BASE + 3,
 *						struct vfio_group_status)
 *
 * Retrieve information about the group.  Fills in provided
 * struct vfio_group_info.  Caller sets argsz.
 * Return: 0 on succes, -errno on failure.
 * Availability: Always
 */
struct vfio_group_status {
	__u32	argsz;
	__u32	flags;
#define VFIO_GROUP_FLAGS_VIABLE		(1 << 0)
#define VFIO_GROUP_FLAGS_CONTAINER_SET	(1 << 1)
};
#define VFIO_GROUP_GET_STATUS		_IO(VFIO_TYPE, VFIO_BASE + 3)

/**
 * VFIO_GROUP_SET_CONTAINER - _IOW(VFIO_TYPE, VFIO_BASE + 4, __s32)
 *
 * Set the container for the VFIO group to the open VFIO file
 * descriptor provided.  Groups may only belong to a single
 * container.  Containers may, at their discretion, support multiple
 * groups.  Only when a container is set are all of the interfaces
 * of the VFIO file descriptor and the VFIO group file descriptor
 * available to the user.
 * Return: 0 on success, -errno on failure.
 * Availability: Always
 */
#define VFIO_GROUP_SET_CONTAINER	_IO(VFIO_TYPE, VFIO_BASE + 4)

/**
 * VFIO_GROUP_UNSET_CONTAINER - _IO(VFIO_TYPE, VFIO_BASE + 5)
 *
 * Remove the group from the attached container.  This is the
 * opposite of the SET_CONTAINER call and returns the group to
 * an initial state.  All device file descriptors must be released
 * prior to calling this interface.  When removing the last group
 * from a container, the IOMMU will be disabled and all state lost,
 * effectively also returning the VFIO file descriptor to an initial
 * state.
 * Return: 0 on success, -errno on failure.
 * Availability: When attached to container
 */
#define VFIO_GROUP_UNSET_CONTAINER	_IO(VFIO_TYPE, VFIO_BASE + 5)

/**
 * VFIO_GROUP_GET_DEVICE_FD - _IOW(VFIO_TYPE, VFIO_BASE + 6, char)
 *
 * Return a new file descriptor for the device object described by
 * the provided string.  The string should match a device listed in
 * the devices subdirectory of the IOMMU group sysfs entry.  The
 * group containing the device must already be added to this context.
 * Return: new file descriptor on success, -errno on failure.
 * Availability: When attached to container
 */
#define VFIO_GROUP_GET_DEVICE_FD	_IO(VFIO_TYPE, VFIO_BASE + 6)

/* --------------- IOCTLs for DEVICE file descriptors --------------- */

/**
 * VFIO_DEVICE_GET_INFO - _IOR(VFIO_TYPE, VFIO_BASE + 7,
 *						struct vfio_device_info)
 *
 * Retrieve information about the device.  Fills in provided
 * struct vfio_device_info.  Caller sets argsz.
 * Return: 0 on success, -errno on failure.
 */
struct vfio_device_info {
	__u32	argsz;
	__u32	flags;
#define VFIO_DEVICE_FLAGS_RESET	(1 << 0)	/* Device supports reset */
#define VFIO_DEVICE_FLAGS_PCI	(1 << 1)	/* vfio-pci device */
#define VFIO_DEVICE_FLAGS_PLATFORM (1 << 2)	/* vfio-platform device */
#define VFIO_DEVICE_FLAGS_AMBA  (1 << 3)	/* vfio-amba device */
#define VFIO_DEVICE_FLAGS_CCW	(1 << 4)	/* vfio-ccw device */
#define VFIO_DEVICE_FLAGS_AP	(1 << 5)	/* vfio-ap device */
	__u32	num_regions;	/* Max region index + 1 */
	__u32	num_irqs;	/* Max IRQ index + 1 */
};
#define VFIO_DEVICE_GET_INFO		_IO(VFIO_TYPE, VFIO_BASE + 7)

/*
 * Vendor driver using Mediated device framework should provide device_api
 * attribute in supported type attribute groups. Device API string should be one
 * of the following corresponding to device flags in vfio_device_info structure.
 */

#define VFIO_DEVICE_API_PCI_STRING		"vfio-pci"
#define VFIO_DEVICE_API_PLATFORM_STRING		"vfio-platform"
#define VFIO_DEVICE_API_AMBA_STRING		"vfio-amba"
#define VFIO_DEVICE_API_CCW_STRING		"vfio-ccw"
#define VFIO_DEVICE_API_AP_STRING		"vfio-ap"

/**
 * VFIO_DEVICE_GET_REGION_INFO - _IOWR(VFIO_TYPE, VFIO_BASE + 8,
 *				       struct vfio_region_info)
 *
 * Retrieve information about a device region.  Caller provides
 * struct vfio_region_info with index value set.  Caller sets argsz.
 * Implementation of region mapping is bus driver specific.  This is
 * intended to describe MMIO, I/O port, as well as bus specific
 * regions (ex. PCI config space).  Zero sized regions may be used
 * to describe unimplemented regions (ex. unimplemented PCI BARs).
 * Return: 0 on success, -errno on failure.
 */
struct vfio_region_info {
	__u32	argsz;
	__u32	flags;
#define VFIO_REGION_INFO_FLAG_READ	(1 << 0) /* Region supports read */
#define VFIO_REGION_INFO_FLAG_WRITE	(1 << 1) /* Region supports write */
#define VFIO_REGION_INFO_FLAG_MMAP	(1 << 2) /* Region supports mmap */
#define VFIO_REGION_INFO_FLAG_CAPS	(1 << 3) /* Info supports caps */
	__u32	index;		/* Region index */
	__u32	cap_offset;	/* Offset within info struct of first cap */
	__u64	size;		/* Region size (bytes) */
	__u64	offset;		/* Region offset from start of device fd */
};
#define VFIO_DEVICE_GET_REGION_INFO	_IO(VFIO_TYPE, VFIO_BASE + 8)

/*
 * The sparse mmap capability allows finer granularity of specifying areas
 * within a region with mmap support.  When specified, the user should only
 * mmap the offset ranges specified by the areas array.  mmaps outside of the
 * areas specified may fail (such as the range covering a PCI MSI-X table) or
 * may result in improper device behavior.
 *
 * The structures below define version 1 of this capability.
 */
#define VFIO_REGION_INFO_CAP_SPARSE_MMAP	1

struct vfio_region_sparse_mmap_area {
	__u64	offset;	/* Offset of mmap'able area within region */
	__u64	size;	/* Size of mmap'able area */
};

struct vfio_region_info_cap_sparse_mmap {
	struct vfio_info_cap_header header;
	__u32	nr_areas;
	__u32	reserved;
	struct vfio_region_sparse_mmap_area areas[];
};

/*
 * The device specific type capability allows regions unique to a specific
 * device or class of devices to be exposed.  This helps solve the problem for
 * vfio bus drivers of defining which region indexes correspond to which region
 * on the device, without needing to resort to static indexes, as done by
 * vfio-pci.  For instance, if we were to go back in time, we might remove
 * VFIO_PCI_VGA_REGION_INDEX and let vfio-pci simply define that all indexes
 * greater than or equal to VFIO_PCI_NUM_REGIONS are device specific and we'd
 * make a "VGA" device specific type to describe the VGA access space.  This
 * means that non-VGA devices wouldn't need to waste this index, and thus the
 * address space associated with it due to implementation of device file
 * descriptor offsets in vfio-pci.
 *
 * The current implementation is now part of the user ABI, so we can't use this
 * for VGA, but there are other upcoming use cases, such as opregions for Intel
 * IGD devices and framebuffers for vGPU devices.  We missed VGA, but we'll
 * use this for future additions.
 *
 * The structure below defines version 1 of this capability.
 */
#define VFIO_REGION_INFO_CAP_TYPE	2

struct vfio_region_info_cap_type {
	struct vfio_info_cap_header header;
	__u32 type;	/* global per bus driver */
	__u32 subtype;	/* type specific */
};

#define VFIO_REGION_TYPE_PCI_VENDOR_TYPE	(1 << 31)
#define VFIO_REGION_TYPE_PCI_VENDOR_MASK	(0xffff)

/* 8086 Vendor sub-types */
#define VFIO_REGION_SUBTYPE_INTEL_IGD_OPREGION	(1)
#define VFIO_REGION_SUBTYPE_INTEL_IGD_HOST_CFG	(2)
#define VFIO_REGION_SUBTYPE_INTEL_IGD_LPC_CFG	(3)

#define VFIO_REGION_TYPE_GFX                    (1)
#define VFIO_REGION_SUBTYPE_GFX_EDID            (1)

/**
 * struct vfio_region_gfx_edid - EDID region layout.
 *
 * Set display link state and EDID blob.
 *
 * The EDID blob has monitor information such as brand, name, serial
 * number, physical size, supported video modes and more.
 *
 * This special region allows userspace (typically qemu) set a virtual
 * EDID for the virtual monitor, which allows a flexible display
 * configuration.
 *
 * For the edid blob spec look here:
 *    https://en.wikipedia.org/wiki/Extended_Display_Identification_Data
 *
 * On linux systems you can find the EDID blob in sysfs:
 *    /sys/class/drm/${card}/${connector}/edid
 *
 * You can use the edid-decode ulility (comes with xorg-x11-utils) to
 * decode the EDID blob.
 *
 * @edid_offset: location of the edid blob, relative to the
 *               start of the region (readonly).
 * @edid_max_size: max size of the edid blob (readonly).
 * @edid_size: actual edid size (read/write).
 * @link_state: display link state (read/write).
 * VFIO_DEVICE_GFX_LINK_STATE_UP: Monitor is turned on.
 * VFIO_DEVICE_GFX_LINK_STATE_DOWN: Monitor is turned off.
 * @max_xres: max display width (0 == no limitation, readonly).
 * @max_yres: max display height (0 == no limitation, readonly).
 *
 * EDID update protocol:
 *   (1) set link-state to down.
 *   (2) update edid blob and size.
 *   (3) set link-state to up.
 */
struct vfio_region_gfx_edid {
	__u32 edid_offset;
	__u32 edid_max_size;
	__u32 edid_size;
	__u32 max_xres;
	__u32 max_yres;
	__u32 link_state;
#define VFIO_DEVICE_GFX_LINK_STATE_UP    1
#define VFIO_DEVICE_GFX_LINK_STATE_DOWN  2
};

#define VFIO_REGION_TYPE_CCW			(2)
/* ccw sub-types */
#define VFIO_REGION_SUBTYPE_CCW_ASYNC_CMD	(1)

/*
 * 10de vendor sub-type
 *
 * NVIDIA GPU NVlink2 RAM is coherent RAM mapped onto the host address space.
 */
#define VFIO_REGION_SUBTYPE_NVIDIA_NVLINK2_RAM	(1)

/*
 * 1014 vendor sub-type
 *
 * IBM NPU NVlink2 ATSD (Address Translation Shootdown) register of NPU
 * to do TLB invalidation on a GPU.
 */
#define VFIO_REGION_SUBTYPE_IBM_NVLINK2_ATSD	(1)

/* Migration region type and sub-type */
#define VFIO_REGION_TYPE_MIGRATION	        (2)
#define VFIO_REGION_SUBTYPE_MIGRATION	        (1)

/**
 * Structure vfio_device_migration_info is placed at 0th offset of
 * VFIO_REGION_SUBTYPE_MIGRATION region to get/set VFIO device related migration
 * information. Field accesses from this structure are only supported at their
 * native width and alignment, otherwise should return error.
 *
 * device_state: (read/write)
 *      To indicate vendor driver the state VFIO device should be transitioned
 *      to. If device state transition fails, write on this field return error.
 *      It consists of 3 bits:
 *      - If bit 0 set, indicates _RUNNING state. When its reset, that indicates
 *        _STOPPED state. When device is changed to _STOPPED, driver should stop
 *        device before write() returns.
 *      - If bit 1 set, indicates _SAVING state.
 *      - If bit 2 set, indicates _RESUMING state.
 *      _SAVING and _RESUMING set at the same time is invalid state.
 *
 * pending bytes: (read only)
 *      Number of pending bytes yet to be migrated from vendor driver
 *
 * data_offset: (read only)
 *      User application should read data_offset in migration region from where
 *      user application should read device data during _SAVING state or write
 *      device data during _RESUMING state or read dirty pages bitmap. See below
 *      for detail of sequence to be followed.
 *
 * data_size: (read/write)
 *      User application should read data_size to get size of data copied in
 *      migration region during _SAVING state and write size of data copied in
 *      migration region during _RESUMING state.
 *
 * start_pfn: (write only)
 *      Start address pfn to get bitmap of dirty pages from vendor driver duing
 *      _SAVING state.
 *
 * page_size: (write only)
 *      User application should write the page_size of pfn.
 *
 * total_pfns: (write only)
 *      Total pfn count from start_pfn for which dirty bitmap is requested.
 *
 * copied_pfns: (read only)
 *      pfn count for which dirty bitmap is copied to migration region.
 *      Vendor driver should copy the bitmap with bits set only for pages to be
 *      marked dirty in migration region.
 *      - Vendor driver should return VFIO_DEVICE_DIRTY_PFNS_NONE if none of the
 *        pages are dirty in requested range or rest of the range.
 *      - Vendor driver should return VFIO_DEVICE_DIRTY_PFNS_ALL to mark all
 *        pages dirty in the given section.
 *      - Vendor driver should return pfn count for which bitmap is written in
 *        the region.
 *
 * Migration region looks like:
 *  ------------------------------------------------------------------
 * |vfio_device_migration_info|    data section                      |
 * |                          |     ///////////////////////////////  |
 * ------------------------------------------------------------------
 *   ^                              ^                              ^
 *  offset 0-trapped part        data_offset                 data_size
 *
 * Data section is always followed by vfio_device_migration_info structure
 * in the region, so data_offset will always be none-0. Offset from where data
 * is copied is decided by kernel driver, data section can be trapped or
 * mapped depending on how kernel driver defines data section. If mmapped,
 * then data_offset should be page aligned, where as initial section which
 * contain vfio_device_migration_info structure might not end at offset which
 * is page aligned.
 * Data_offset can be same or different for device data and dirty page bitmap.
 * Vendor driver should decide whether to partition data section and how to
 * partition the data section. Vendor driver should return data_offset
 * accordingly.
 *
 * Sequence to be followed:
 * In _SAVING|_RUNNING device state or pre-copy phase:
 * a. read pending_bytes. If pending_bytes > 0, go through below steps.
 * b. read data_offset, indicates kernel driver to write data to staging buffer
 *    which is mmapped.
 * c. read data_size, amount of data in bytes written by vendor driver in
 *    migration region.
 * d. if data section is trapped, read from data_offset of data_size.
 * e. if data section is mmaped, read data_size bytes from mmaped buffer from
 *    data_offset in the migration region.
 * f. Write data_size and data to file stream.
 * g. iterate through steps a to f while (pending_bytes > 0)
 *
 * In _SAVING device state or stop-and-copy phase:
 * a. read config space of device and save to migration file stream. This
 *    doesn't need to be from vendor driver. Any other special config state
 *    from driver can be saved as data in following iteration.
 * b. read pending_bytes.
 * c. read data_offset, indicates kernel driver to write data to staging
 *    buffer which is mmapped.
 * d. read data_size, amount of data in bytes written by vendor driver in
 *    migration region.
 * e. if data section is trapped, read from data_offset of data_size.
 * f. if data section is mmaped, read data_size bytes from mmaped buffer from
 *    data_offset in the migration region.
 * g. Write data_size and data to file stream
 * h. iterate through steps b to g while (pending_bytes > 0)
 *
 * When data region is mapped, its user's responsibility to read data from
 * data_offset of data_size before moving to next steps.
 *
 * Dirty page tracking is part of RAM copy state, where vendor driver
 * provides the bitmap of pages which are dirtied by vendor driver through
 * migration region and as part of RAM copy those pages gets copied to file
 * stream.
 *
 * To get dirty page bitmap:
 * a. write start_pfn, page_size and total_pfns.
 * b. read copied_pfns.
 *     - Vendor driver should return VFIO_DEVICE_DIRTY_PFNS_NONE if driver
 *       doesn't have any page to report dirty in given range or rest of the
 *       range. Exit loop.
 *     - Vendor driver should return VFIO_DEVICE_DIRTY_PFNS_ALL to mark all
 *       pages dirty for given range. Mark all pages in the range as dirty and
 *       exit the loop.
 *     - Vendor driver should return copied_pfns and provide bitmap for
 *       copied_pfn, which means that bitmap copied for given range contains
 *       information for all pages where some bits are 0s and some are 1s.
 * c. read data_offset, where vendor driver has written bitmap.
 * d. read bitmap from the region or mmaped part of the region.
 * e. Iterate through steps a to d while (total copied_pfns < total_pfns)
 *
 * In _RESUMING device state:
 * - Load device config state.
 * - While end of data for this device is not reached, repeat below steps:
 *      - read data_size from file stream, read data from file stream of
 *        data_size.
 *      - read data_offset from where User application should write data.
 *          if region is mmaped, write data of data_size to mmaped region.
 *      - write data_size.
 *          In case of mmapped region, write on data_size indicates kernel
 *          driver that data is written in staging buffer.
 *      - if region is trapped, write data of data_size from data_offset.
 *
 * For user application, data is opaque. User should write data in the same
 * order as received.
 */

struct vfio_device_migration_info {
        __u32 device_state;         /* VFIO device state */
#define VFIO_DEVICE_STATE_RUNNING   (1 << 0)
#define VFIO_DEVICE_STATE_SAVING    (1 << 1)
#define VFIO_DEVICE_STATE_RESUMING  (1 << 2)
#define VFIO_DEVICE_STATE_MASK      (VFIO_DEVICE_STATE_RUNNING | \
                                     VFIO_DEVICE_STATE_SAVING | \
                                     VFIO_DEVICE_STATE_RESUMING)
#define VFIO_DEVICE_STATE_INVALID   (VFIO_DEVICE_STATE_SAVING | \
                                     VFIO_DEVICE_STATE_RESUMING)
        __u32 reserved;
        __u64 pending_bytes;
        __u64 data_offset;
        __u64 data_size;
        __u64 start_pfn;
        __u64 page_size;
        __u64 total_pfns;
        __u64 copied_pfns;
#define VFIO_DEVICE_DIRTY_PFNS_NONE     (0)
#define VFIO_DEVICE_DIRTY_PFNS_ALL      (~0ULL)
} __attribute__((packed));

/*
 * The MSIX mappable capability informs that MSIX data of a BAR can be mmapped
 * which allows direct access to non-MSIX registers which happened to be within
 * the same system page.
 *
 * Even though the userspace gets direct access to the MSIX data, the existing
 * VFIO_DEVICE_SET_IRQS interface must still be used for MSIX configuration.
 */
#define VFIO_REGION_INFO_CAP_MSIX_MAPPABLE	3

/*
 * Capability with compressed real address (aka SSA - small system address)
 * where GPU RAM is mapped on a system bus. Used by a GPU for DMA routing
 * and by the userspace to associate a NVLink bridge with a GPU.
 */
#define VFIO_REGION_INFO_CAP_NVLINK2_SSATGT	4

struct vfio_region_info_cap_nvlink2_ssatgt {
	struct vfio_info_cap_header header;
	__u64 tgt;
};

/*
 * Capability with an NVLink link speed. The value is read by
 * the NVlink2 bridge driver from the bridge's "ibm,nvlink-speed"
 * property in the device tree. The value is fixed in the hardware
 * and failing to provide the correct value results in the link
 * not working with no indication from the driver why.
 */
#define VFIO_REGION_INFO_CAP_NVLINK2_LNKSPD	5

struct vfio_region_info_cap_nvlink2_lnkspd {
	struct vfio_info_cap_header header;
	__u32 link_speed;
	__u32 __pad;
};

/**
 * VFIO_DEVICE_GET_IRQ_INFO - _IOWR(VFIO_TYPE, VFIO_BASE + 9,
 *				    struct vfio_irq_info)
 *
 * Retrieve information about a device IRQ.  Caller provides
 * struct vfio_irq_info with index value set.  Caller sets argsz.
 * Implementation of IRQ mapping is bus driver specific.  Indexes
 * using multiple IRQs are primarily intended to support MSI-like
 * interrupt blocks.  Zero count irq blocks may be used to describe
 * unimplemented interrupt types.
 *
 * The EVENTFD flag indicates the interrupt index supports eventfd based
 * signaling.
 *
 * The MASKABLE flags indicates the index supports MASK and UNMASK
 * actions described below.
 *
 * AUTOMASKED indicates that after signaling, the interrupt line is
 * automatically masked by VFIO and the user needs to unmask the line
 * to receive new interrupts.  This is primarily intended to distinguish
 * level triggered interrupts.
 *
 * The NORESIZE flag indicates that the interrupt lines within the index
 * are setup as a set and new subindexes cannot be enabled without first
 * disabling the entire index.  This is used for interrupts like PCI MSI
 * and MSI-X where the driver may only use a subset of the available
 * indexes, but VFIO needs to enable a specific number of vectors
 * upfront.  In the case of MSI-X, where the user can enable MSI-X and
 * then add and unmask vectors, it's up to userspace to make the decision
 * whether to allocate the maximum supported number of vectors or tear
 * down setup and incrementally increase the vectors as each is enabled.
 */
struct vfio_irq_info {
	__u32	argsz;
	__u32	flags;
#define VFIO_IRQ_INFO_EVENTFD		(1 << 0)
#define VFIO_IRQ_INFO_MASKABLE		(1 << 1)
#define VFIO_IRQ_INFO_AUTOMASKED	(1 << 2)
#define VFIO_IRQ_INFO_NORESIZE		(1 << 3)
	__u32	index;		/* IRQ index */
	__u32	count;		/* Number of IRQs within this index */
};
#define VFIO_DEVICE_GET_IRQ_INFO	_IO(VFIO_TYPE, VFIO_BASE + 9)

/**
 * VFIO_DEVICE_SET_IRQS - _IOW(VFIO_TYPE, VFIO_BASE + 10, struct vfio_irq_set)
 *
 * Set signaling, masking, and unmasking of interrupts.  Caller provides
 * struct vfio_irq_set with all fields set.  'start' and 'count' indicate
 * the range of subindexes being specified.
 *
 * The DATA flags specify the type of data provided.  If DATA_NONE, the
 * operation performs the specified action immediately on the specified
 * interrupt(s).  For example, to unmask AUTOMASKED interrupt [0,0]:
 * flags = (DATA_NONE|ACTION_UNMASK), index = 0, start = 0, count = 1.
 *
 * DATA_BOOL allows sparse support for the same on arrays of interrupts.
 * For example, to mask interrupts [0,1] and [0,3] (but not [0,2]):
 * flags = (DATA_BOOL|ACTION_MASK), index = 0, start = 1, count = 3,
 * data = {1,0,1}
 *
 * DATA_EVENTFD binds the specified ACTION to the provided __s32 eventfd.
 * A value of -1 can be used to either de-assign interrupts if already
 * assigned or skip un-assigned interrupts.  For example, to set an eventfd
 * to be trigger for interrupts [0,0] and [0,2]:
 * flags = (DATA_EVENTFD|ACTION_TRIGGER), index = 0, start = 0, count = 3,
 * data = {fd1, -1, fd2}
 * If index [0,1] is previously set, two count = 1 ioctls calls would be
 * required to set [0,0] and [0,2] without changing [0,1].
 *
 * Once a signaling mechanism is set, DATA_BOOL or DATA_NONE can be used
 * with ACTION_TRIGGER to perform kernel level interrupt loopback testing
 * from userspace (ie. simulate hardware triggering).
 *
 * Setting of an event triggering mechanism to userspace for ACTION_TRIGGER
 * enables the interrupt index for the device.  Individual subindex interrupts
 * can be disabled using the -1 value for DATA_EVENTFD or the index can be
 * disabled as a whole with: flags = (DATA_NONE|ACTION_TRIGGER), count = 0.
 *
 * Note that ACTION_[UN]MASK specify user->kernel signaling (irqfds) while
 * ACTION_TRIGGER specifies kernel->user signaling.
 */
struct vfio_irq_set {
	__u32	argsz;
	__u32	flags;
#define VFIO_IRQ_SET_DATA_NONE		(1 << 0) /* Data not present */
#define VFIO_IRQ_SET_DATA_BOOL		(1 << 1) /* Data is bool (u8) */
#define VFIO_IRQ_SET_DATA_EVENTFD	(1 << 2) /* Data is eventfd (s32) */
#define VFIO_IRQ_SET_ACTION_MASK	(1 << 3) /* Mask interrupt */
#define VFIO_IRQ_SET_ACTION_UNMASK	(1 << 4) /* Unmask interrupt */
#define VFIO_IRQ_SET_ACTION_TRIGGER	(1 << 5) /* Trigger interrupt */
	__u32	index;
	__u32	start;
	__u32	count;
	__u8	data[];
};
#define VFIO_DEVICE_SET_IRQS		_IO(VFIO_TYPE, VFIO_BASE + 10)

#define VFIO_IRQ_SET_DATA_TYPE_MASK	(VFIO_IRQ_SET_DATA_NONE | \
					 VFIO_IRQ_SET_DATA_BOOL | \
					 VFIO_IRQ_SET_DATA_EVENTFD)
#define VFIO_IRQ_SET_ACTION_TYPE_MASK	(VFIO_IRQ_SET_ACTION_MASK | \
					 VFIO_IRQ_SET_ACTION_UNMASK | \
					 VFIO_IRQ_SET_ACTION_TRIGGER)
/**
 * VFIO_DEVICE_RESET - _IO(VFIO_TYPE, VFIO_BASE + 11)
 *
 * Reset a device.
 */
#define VFIO_DEVICE_RESET		_IO(VFIO_TYPE, VFIO_BASE + 11)

/*
 * The VFIO-PCI bus driver makes use of the following fixed region and
 * IRQ index mapping.  Unimplemented regions return a size of zero.
 * Unimplemented IRQ types return a count of zero.
 */

enum {
	VFIO_PCI_BAR0_REGION_INDEX,
	VFIO_PCI_BAR1_REGION_INDEX,
	VFIO_PCI_BAR2_REGION_INDEX,
	VFIO_PCI_BAR3_REGION_INDEX,
	VFIO_PCI_BAR4_REGION_INDEX,
	VFIO_PCI_BAR5_REGION_INDEX,
	VFIO_PCI_ROM_REGION_INDEX,
	VFIO_PCI_CONFIG_REGION_INDEX,
	/*
	 * Expose VGA regions defined for PCI base class 03, subclass 00.
	 * This includes I/O port ranges 0x3b0 to 0x3bb and 0x3c0 to 0x3df
	 * as well as the MMIO range 0xa0000 to 0xbffff.  Each implemented
	 * range is found at it's identity mapped offset from the region
	 * offset, for example 0x3b0 is region_info.offset + 0x3b0.  Areas
	 * between described ranges are unimplemented.
	 */
	VFIO_PCI_VGA_REGION_INDEX,
	VFIO_PCI_NUM_REGIONS = 9 /* Fixed user ABI, region indexes >=9 use */
				 /* device specific cap to define content. */
};

enum {
	VFIO_PCI_INTX_IRQ_INDEX,
	VFIO_PCI_MSI_IRQ_INDEX,
	VFIO_PCI_MSIX_IRQ_INDEX,
	VFIO_PCI_ERR_IRQ_INDEX,
	VFIO_PCI_REQ_IRQ_INDEX,
	VFIO_PCI_NUM_IRQS
};

/*
 * The vfio-ccw bus driver makes use of the following fixed region and
 * IRQ index mapping. Unimplemented regions return a size of zero.
 * Unimplemented IRQ types return a count of zero.
 */

enum {
	VFIO_CCW_CONFIG_REGION_INDEX,
	VFIO_CCW_NUM_REGIONS
};

enum {
	VFIO_CCW_IO_IRQ_INDEX,
	VFIO_CCW_NUM_IRQS
};

/**
 * VFIO_DEVICE_GET_PCI_HOT_RESET_INFO - _IORW(VFIO_TYPE, VFIO_BASE + 12,
 *					      struct vfio_pci_hot_reset_info)
 *
 * Return: 0 on success, -errno on failure:
 *	-enospc = insufficient buffer, -enodev = unsupported for device.
 */
struct vfio_pci_dependent_device {
	__u32	group_id;
	__u16	segment;
	__u8	bus;
	__u8	devfn; /* Use PCI_SLOT/PCI_FUNC */
};

struct vfio_pci_hot_reset_info {
	__u32	argsz;
	__u32	flags;
	__u32	count;
	struct vfio_pci_dependent_device	devices[];
};

#define VFIO_DEVICE_GET_PCI_HOT_RESET_INFO	_IO(VFIO_TYPE, VFIO_BASE + 12)

/**
 * VFIO_DEVICE_PCI_HOT_RESET - _IOW(VFIO_TYPE, VFIO_BASE + 13,
 *				    struct vfio_pci_hot_reset)
 *
 * Return: 0 on success, -errno on failure.
 */
struct vfio_pci_hot_reset {
	__u32	argsz;
	__u32	flags;
	__u32	count;
	__s32	group_fds[];
};

#define VFIO_DEVICE_PCI_HOT_RESET	_IO(VFIO_TYPE, VFIO_BASE + 13)

/**
 * VFIO_DEVICE_QUERY_GFX_PLANE - _IOW(VFIO_TYPE, VFIO_BASE + 14,
 *                                    struct vfio_device_query_gfx_plane)
 *
 * Set the drm_plane_type and flags, then retrieve the gfx plane info.
 *
 * flags supported:
 * - VFIO_GFX_PLANE_TYPE_PROBE and VFIO_GFX_PLANE_TYPE_DMABUF are set
 *   to ask if the mdev supports dma-buf. 0 on support, -EINVAL on no
 *   support for dma-buf.
 * - VFIO_GFX_PLANE_TYPE_PROBE and VFIO_GFX_PLANE_TYPE_REGION are set
 *   to ask if the mdev supports region. 0 on support, -EINVAL on no
 *   support for region.
 * - VFIO_GFX_PLANE_TYPE_DMABUF or VFIO_GFX_PLANE_TYPE_REGION is set
 *   with each call to query the plane info.
 * - Others are invalid and return -EINVAL.
 *
 * Note:
 * 1. Plane could be disabled by guest. In that case, success will be
 *    returned with zero-initialized drm_format, size, width and height
 *    fields.
 * 2. x_hot/y_hot is set to 0xFFFFFFFF if no hotspot information available
 *
 * Return: 0 on success, -errno on other failure.
 */
struct vfio_device_gfx_plane_info {
	__u32 argsz;
	__u32 flags;
#define VFIO_GFX_PLANE_TYPE_PROBE (1 << 0)
#define VFIO_GFX_PLANE_TYPE_DMABUF (1 << 1)
#define VFIO_GFX_PLANE_TYPE_REGION (1 << 2)
	/* in */
	__u32 drm_plane_type;	/* type of plane: DRM_PLANE_TYPE_* */
	/* out */
	__u32 drm_format;	/* drm format of plane */
	__u64 drm_format_mod;   /* tiled mode */
	__u32 width;	/* width of plane */
	__u32 height;	/* height of plane */
	__u32 stride;	/* stride of plane */
	__u32 size;	/* size of plane in bytes, align on page*/
	__u32 x_pos;	/* horizontal position of cursor plane */
	__u32 y_pos;	/* vertical position of cursor plane*/
	__u32 x_hot;    /* horizontal position of cursor hotspot */
	__u32 y_hot;    /* vertical position of cursor hotspot */
	union {
		__u32 region_index;	/* region index */
		__u32 dmabuf_id;	/* dma-buf id */
	};
};

#define VFIO_DEVICE_QUERY_GFX_PLANE _IO(VFIO_TYPE, VFIO_BASE + 14)

/**
 * VFIO_DEVICE_GET_GFX_DMABUF - _IOW(VFIO_TYPE, VFIO_BASE + 15, __u32)
 *
 * Return a new dma-buf file descriptor for an exposed guest framebuffer
 * described by the provided dmabuf_id. The dmabuf_id is returned from VFIO_
 * DEVICE_QUERY_GFX_PLANE as a token of the exposed guest framebuffer.
 */

#define VFIO_DEVICE_GET_GFX_DMABUF _IO(VFIO_TYPE, VFIO_BASE + 15)

/**
 * VFIO_DEVICE_IOEVENTFD - _IOW(VFIO_TYPE, VFIO_BASE + 16,
 *                              struct vfio_device_ioeventfd)
 *
 * Perform a write to the device at the specified device fd offset, with
 * the specified data and width when the provided eventfd is triggered.
 * vfio bus drivers may not support this for all regions, for all widths,
 * or at all.  vfio-pci currently only enables support for BAR regions,
 * excluding the MSI-X vector table.
 *
 * Return: 0 on success, -errno on failure.
 */
struct vfio_device_ioeventfd {
	__u32	argsz;
	__u32	flags;
#define VFIO_DEVICE_IOEVENTFD_8		(1 << 0) /* 1-byte write */
#define VFIO_DEVICE_IOEVENTFD_16	(1 << 1) /* 2-byte write */
#define VFIO_DEVICE_IOEVENTFD_32	(1 << 2) /* 4-byte write */
#define VFIO_DEVICE_IOEVENTFD_64	(1 << 3) /* 8-byte write */
#define VFIO_DEVICE_IOEVENTFD_SIZE_MASK	(0xf)
	__u64	offset;			/* device fd offset of write */
	__u64	data;			/* data to be written */
	__s32	fd;			/* -1 for de-assignment */
};

#define VFIO_DEVICE_IOEVENTFD		_IO(VFIO_TYPE, VFIO_BASE + 16)

/* -------- API for Type1 VFIO IOMMU -------- */

/**
 * VFIO_IOMMU_GET_INFO - _IOR(VFIO_TYPE, VFIO_BASE + 12, struct vfio_iommu_info)
 *
 * Retrieve information about the IOMMU object. Fills in provided
 * struct vfio_iommu_info. Caller sets argsz.
 *
 * XXX Should we do these by CHECK_EXTENSION too?
 */
struct vfio_iommu_type1_info {
	__u32	argsz;
	__u32	flags;
#define VFIO_IOMMU_INFO_PGSIZES (1 << 0)	/* supported page sizes info */
	__u64	iova_pgsizes;		/* Bitmap of supported page sizes */
};

#define VFIO_IOMMU_GET_INFO _IO(VFIO_TYPE, VFIO_BASE + 12)

/**
 * VFIO_IOMMU_MAP_DMA - _IOW(VFIO_TYPE, VFIO_BASE + 13, struct vfio_dma_map)
 *
 * Map process virtual addresses to IO virtual addresses using the
 * provided struct vfio_dma_map. Caller sets argsz. READ &/ WRITE required.
 */
struct vfio_iommu_type1_dma_map {
	__u32	argsz;
	__u32	flags;
#define VFIO_DMA_MAP_FLAG_READ (1 << 0)		/* readable from device */
#define VFIO_DMA_MAP_FLAG_WRITE (1 << 1)	/* writable from device */
	__u64	vaddr;				/* Process virtual address */
	__u64	iova;				/* IO virtual address */
	__u64	size;				/* Size of mapping (bytes) */
};

#define VFIO_IOMMU_MAP_DMA _IO(VFIO_TYPE, VFIO_BASE + 13)

/**
 * VFIO_IOMMU_UNMAP_DMA - _IOWR(VFIO_TYPE, VFIO_BASE + 14,
 *							struct vfio_dma_unmap)
 *
 * Unmap IO virtual addresses using the provided struct vfio_dma_unmap.
 * Caller sets argsz.  The actual unmapped size is returned in the size
 * field.  No guarantee is made to the user that arbitrary unmaps of iova
 * or size different from those used in the original mapping call will
 * succeed.
 */
struct vfio_iommu_type1_dma_unmap {
	__u32	argsz;
	__u32	flags;
	__u64	iova;				/* IO virtual address */
	__u64	size;				/* Size of mapping (bytes) */
};

#define VFIO_IOMMU_UNMAP_DMA _IO(VFIO_TYPE, VFIO_BASE + 14)

/*
 * IOCTLs to enable/disable IOMMU container usage.
 * No parameters are supported.
 */
#define VFIO_IOMMU_ENABLE	_IO(VFIO_TYPE, VFIO_BASE + 15)
#define VFIO_IOMMU_DISABLE	_IO(VFIO_TYPE, VFIO_BASE + 16)

/* -------- Additional API for SPAPR TCE (Server POWERPC) IOMMU -------- */

/*
 * The SPAPR TCE DDW info struct provides the information about
 * the details of Dynamic DMA window capability.
 *
 * @pgsizes contains a page size bitmask, 4K/64K/16M are supported.
 * @max_dynamic_windows_supported tells the maximum number of windows
 * which the platform can create.
 * @levels tells the maximum number of levels in multi-level IOMMU tables;
 * this allows splitting a table into smaller chunks which reduces
 * the amount of physically contiguous memory required for the table.
 */
struct vfio_iommu_spapr_tce_ddw_info {
	__u64 pgsizes;			/* Bitmap of supported page sizes */
	__u32 max_dynamic_windows_supported;
	__u32 levels;
};

/*
 * The SPAPR TCE info struct provides the information about the PCI bus
 * address ranges available for DMA, these values are programmed into
 * the hardware so the guest has to know that information.
 *
 * The DMA 32 bit window start is an absolute PCI bus address.
 * The IOVA address passed via map/unmap ioctls are absolute PCI bus
 * addresses too so the window works as a filter rather than an offset
 * for IOVA addresses.
 *
 * Flags supported:
 * - VFIO_IOMMU_SPAPR_INFO_DDW: informs the userspace that dynamic DMA windows
 *   (DDW) support is present. @ddw is only supported when DDW is present.
 */
struct vfio_iommu_spapr_tce_info {
	__u32 argsz;
	__u32 flags;
#define VFIO_IOMMU_SPAPR_INFO_DDW	(1 << 0)	/* DDW supported */
	__u32 dma32_window_start;	/* 32 bit window start (bytes) */
	__u32 dma32_window_size;	/* 32 bit window size (bytes) */
	struct vfio_iommu_spapr_tce_ddw_info ddw;
};

#define VFIO_IOMMU_SPAPR_TCE_GET_INFO	_IO(VFIO_TYPE, VFIO_BASE + 12)

/*
 * EEH PE operation struct provides ways to:
 * - enable/disable EEH functionality;
 * - unfreeze IO/DMA for frozen PE;
 * - read PE state;
 * - reset PE;
 * - configure PE;
 * - inject EEH error.
 */
struct vfio_eeh_pe_err {
	__u32 type;
	__u32 func;
	__u64 addr;
	__u64 mask;
};

struct vfio_eeh_pe_op {
	__u32 argsz;
	__u32 flags;
	__u32 op;
	union {
		struct vfio_eeh_pe_err err;
	};
};

#define VFIO_EEH_PE_DISABLE		0	/* Disable EEH functionality */
#define VFIO_EEH_PE_ENABLE		1	/* Enable EEH functionality  */
#define VFIO_EEH_PE_UNFREEZE_IO		2	/* Enable IO for frozen PE   */
#define VFIO_EEH_PE_UNFREEZE_DMA	3	/* Enable DMA for frozen PE  */
#define VFIO_EEH_PE_GET_STATE		4	/* PE state retrieval        */
#define  VFIO_EEH_PE_STATE_NORMAL	0	/* PE in functional state    */
#define  VFIO_EEH_PE_STATE_RESET	1	/* PE reset in progress      */
#define  VFIO_EEH_PE_STATE_STOPPED	2	/* Stopped DMA and IO        */
#define  VFIO_EEH_PE_STATE_STOPPED_DMA	4	/* Stopped DMA only          */
#define  VFIO_EEH_PE_STATE_UNAVAIL	5	/* State unavailable         */
#define VFIO_EEH_PE_RESET_DEACTIVATE	5	/* Deassert PE reset         */
#define VFIO_EEH_PE_RESET_HOT		6	/* Assert hot reset          */
#define VFIO_EEH_PE_RESET_FUNDAMENTAL	7	/* Assert fundamental reset  */
#define VFIO_EEH_PE_CONFIGURE		8	/* PE configuration          */
#define VFIO_EEH_PE_INJECT_ERR		9	/* Inject EEH error          */

#define VFIO_EEH_PE_OP			_IO(VFIO_TYPE, VFIO_BASE + 21)

/**
 * VFIO_IOMMU_SPAPR_REGISTER_MEMORY - _IOW(VFIO_TYPE, VFIO_BASE + 17, struct vfio_iommu_spapr_register_memory)
 *
 * Registers user space memory where DMA is allowed. It pins
 * user pages and does the locked memory accounting so
 * subsequent VFIO_IOMMU_MAP_DMA/VFIO_IOMMU_UNMAP_DMA calls
 * get faster.
 */
struct vfio_iommu_spapr_register_memory {
	__u32	argsz;
	__u32	flags;
	__u64	vaddr;				/* Process virtual address */
	__u64	size;				/* Size of mapping (bytes) */
};
#define VFIO_IOMMU_SPAPR_REGISTER_MEMORY	_IO(VFIO_TYPE, VFIO_BASE + 17)

/**
 * VFIO_IOMMU_SPAPR_UNREGISTER_MEMORY - _IOW(VFIO_TYPE, VFIO_BASE + 18, struct vfio_iommu_spapr_register_memory)
 *
 * Unregisters user space memory registered with
 * VFIO_IOMMU_SPAPR_REGISTER_MEMORY.
 * Uses vfio_iommu_spapr_register_memory for parameters.
 */
#define VFIO_IOMMU_SPAPR_UNREGISTER_MEMORY	_IO(VFIO_TYPE, VFIO_BASE + 18)

/**
 * VFIO_IOMMU_SPAPR_TCE_CREATE - _IOWR(VFIO_TYPE, VFIO_BASE + 19, struct vfio_iommu_spapr_tce_create)
 *
 * Creates an additional TCE table and programs it (sets a new DMA window)
 * to every IOMMU group in the container. It receives page shift, window
 * size and number of levels in the TCE table being created.
 *
 * It allocates and returns an offset on a PCI bus of the new DMA window.
 */
struct vfio_iommu_spapr_tce_create {
	__u32 argsz;
	__u32 flags;
	/* in */
	__u32 page_shift;
	__u32 __resv1;
	__u64 window_size;
	__u32 levels;
	__u32 __resv2;
	/* out */
	__u64 start_addr;
};
#define VFIO_IOMMU_SPAPR_TCE_CREATE	_IO(VFIO_TYPE, VFIO_BASE + 19)

/**
 * VFIO_IOMMU_SPAPR_TCE_REMOVE - _IOW(VFIO_TYPE, VFIO_BASE + 20, struct vfio_iommu_spapr_tce_remove)
 *
 * Unprograms a TCE table from all groups in the container and destroys it.
 * It receives a PCI bus offset as a window id.
 */
struct vfio_iommu_spapr_tce_remove {
	__u32 argsz;
	__u32 flags;
	/* in */
	__u64 start_addr;
};
#define VFIO_IOMMU_SPAPR_TCE_REMOVE	_IO(VFIO_TYPE, VFIO_BASE + 20)

/* ***************************************************************** */

#endif /* VFIO_H */
