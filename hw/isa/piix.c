/*
 * QEMU PIIX PCI ISA Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2018 Hervé Poussineau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/range.h"
#include "qapi/error.h"
#include "hw/dma/i8257.h"
#include "hw/intc/i8259.h"
#include "hw/southbridge/piix.h"
#include "hw/timer/i8254.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ide/piix.h"
#include "hw/isa/isa.h"
#include "hw/xen/xen.h"
#include "sysemu/runstate.h"
#include "migration/vmstate.h"
#include "hw/acpi/acpi_aml_interface.h"

#define XEN_PIIX_NUM_PIRQS      128ULL

static void piix_set_irq_pic(PIIXState *piix, int pic_irq)
{
    qemu_set_irq(piix->pic.in_irqs[pic_irq],
                 !!(piix->pic_levels &
                    (((1ULL << PIIX_NUM_PIRQS) - 1) <<
                     (pic_irq * PIIX_NUM_PIRQS))));
}

static void piix_set_irq_level_internal(PIIXState *piix, int pirq, int level)
{
    int pic_irq;
    uint64_t mask;

    pic_irq = piix->dev.config[PIIX_PIRQCA + pirq];
    if (pic_irq >= ISA_NUM_IRQS) {
        return;
    }

    mask = 1ULL << ((pic_irq * PIIX_NUM_PIRQS) + pirq);
    piix->pic_levels &= ~mask;
    piix->pic_levels |= mask * !!level;
}

static void piix_set_irq_level(PIIXState *piix, int pirq, int level)
{
    int pic_irq;

    pic_irq = piix->dev.config[PIIX_PIRQCA + pirq];
    if (pic_irq >= ISA_NUM_IRQS) {
        return;
    }

    piix_set_irq_level_internal(piix, pirq, level);

    piix_set_irq_pic(piix, pic_irq);
}

static void piix_set_irq(void *opaque, int pirq, int level)
{
    PIIXState *piix = opaque;
    piix_set_irq_level(piix, pirq, level);
}

/*
 * Return the global irq number corresponding to a given device irq
 * pin. We could also use the bus number to have a more precise mapping.
 */
static int piix3_pci_slot_get_pirq(PCIDevice *pci_dev, int pci_intx)
{
    int slot_addend;
    slot_addend = PCI_SLOT(pci_dev->devfn) - 1;
    return (pci_intx + slot_addend) & 3;
}

static int piix4_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    int slot;

    slot = PCI_SLOT(pci_dev->devfn);

    switch (slot) {
    /* PIIX4 USB */
    case 10:
        return 3;
    /* AMD 79C973 Ethernet */
    case 11:
        return 1;
    /* Crystal 4281 Sound */
    case 12:
        return 2;
    /* PCI slot 1 to 4 */
    case 18 ... 21:
        return ((slot - 18) + irq_num) & 0x03;
    /* Unknown device, don't do any translation */
    default:
        return irq_num;
    }
}

static PCIINTxRoute piix3_route_intx_pin_to_irq(void *opaque, int pin)
{
    PIIXState *piix3 = opaque;
    int irq = piix3->dev.config[PIIX_PIRQCA + pin];
    PCIINTxRoute route;

    if (irq < ISA_NUM_IRQS) {
        route.mode = PCI_INTX_ENABLED;
        route.irq = irq;
    } else {
        route.mode = PCI_INTX_DISABLED;
        route.irq = -1;
    }
    return route;
}

/* irq routing is changed. so rebuild bitmap */
static void piix_update_irq_levels(PIIXState *piix)
{
    PCIBus *bus = pci_get_bus(&piix->dev);
    int pirq;

    piix->pic_levels = 0;
    for (pirq = 0; pirq < PIIX_NUM_PIRQS; pirq++) {
        piix_set_irq_level(piix, pirq, pci_bus_get_irq_level(bus, pirq));
    }
}

static void piix_write_config(PCIDevice *dev, uint32_t address, uint32_t val,
                              int len)
{
    pci_default_write_config(dev, address, val, len);
    if (ranges_overlap(address, len, PIIX_PIRQCA, 4)) {
        PIIXState *piix = PIIX_PCI_DEVICE(dev);
        int pic_irq;

        pci_bus_fire_intx_routing_notifier(pci_get_bus(&piix->dev));
        piix_update_irq_levels(piix);
        for (pic_irq = 0; pic_irq < ISA_NUM_IRQS; pic_irq++) {
            piix_set_irq_pic(piix, pic_irq);
        }
    }
}

static void piix3_write_config_xen(PCIDevice *dev,
                                   uint32_t address, uint32_t val, int len)
{
    int i;

    /* Scan for updates to PCI link routes (0x60-0x63). */
    for (i = 0; i < len; i++) {
        uint8_t v = (val >> (8 * i)) & 0xff;
        if (v & 0x80) {
            v = 0;
        }
        v &= 0xf;
        if (((address + i) >= PIIX_PIRQCA) && ((address + i) <= PIIX_PIRQCD)) {
            xen_set_pci_link_route(address + i - PIIX_PIRQCA, v);
        }
    }

    piix_write_config(dev, address, val, len);
}

static void piix_reset(DeviceState *dev)
{
    PIIXState *d = PIIX_PCI_DEVICE(dev);
    uint8_t *pci_conf = d->dev.config;

    pci_conf[0x04] = 0x07; /* master, memory and I/O */
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; /* PCI_status_devsel_medium */
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[PIIX_PIRQCA] = d->pci_irq_reset_mappings[0];
    pci_conf[PIIX_PIRQCB] = d->pci_irq_reset_mappings[1];
    pci_conf[PIIX_PIRQCC] = d->pci_irq_reset_mappings[2];
    pci_conf[PIIX_PIRQCD] = d->pci_irq_reset_mappings[3];
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa2] = 0x00;
    pci_conf[0xa3] = 0x00;
    pci_conf[0xa4] = 0x00;
    pci_conf[0xa5] = 0x00;
    pci_conf[0xa6] = 0x00;
    pci_conf[0xa7] = 0x00;
    pci_conf[0xa8] = 0x0f;
    pci_conf[0xaa] = 0x00;
    pci_conf[0xab] = 0x00;
    pci_conf[0xac] = 0x00;
    pci_conf[0xae] = 0x00;

    d->pic_levels = 0;
    d->rcr = 0;
}

static int piix3_post_load(void *opaque, int version_id)
{
    PIIXState *piix3 = opaque;
    int pirq;

    /*
     * Because the i8259 has not been deserialized yet, qemu_irq_raise
     * might bring the system to a different state than the saved one;
     * for example, the interrupt could be masked but the i8259 would
     * not know that yet and would trigger an interrupt in the CPU.
     *
     * Here, we update irq levels without raising the interrupt.
     * Interrupt state will be deserialized separately through the i8259.
     */
    piix3->pic_levels = 0;
    for (pirq = 0; pirq < PIIX_NUM_PIRQS; pirq++) {
        piix_set_irq_level_internal(piix3, pirq,
            pci_bus_get_irq_level(pci_get_bus(&piix3->dev), pirq));
    }
    return 0;
}

static int piix4_post_load(void *opaque, int version_id)
{
    PIIXState *s = opaque;

    if (version_id == 2) {
        s->rcr = 0;
    }

    return piix3_post_load(opaque, version_id);
}

static int piix3_pre_save(void *opaque)
{
    int i;
    PIIXState *piix3 = opaque;

    for (i = 0; i < ARRAY_SIZE(piix3->pci_irq_levels_vmstate); i++) {
        piix3->pci_irq_levels_vmstate[i] =
            pci_bus_get_irq_level(pci_get_bus(&piix3->dev), i);
    }

    return 0;
}

static bool piix3_rcr_needed(void *opaque)
{
    PIIXState *piix3 = opaque;

    return (piix3->rcr != 0);
}

static const VMStateDescription vmstate_piix3_rcr = {
    .name = "PIIX3/rcr",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = piix3_rcr_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(rcr, PIIXState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_piix3 = {
    .name = "PIIX3",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = piix3_post_load,
    .pre_save = piix3_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PIIXState),
        VMSTATE_INT32_ARRAY_V(pci_irq_levels_vmstate, PIIXState,
                              PIIX_NUM_PIRQS, 3),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_piix3_rcr,
        NULL
    }
};

static const VMStateDescription vmstate_piix4 = {
    .name = "PIIX4",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = piix4_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PIIXState),
        VMSTATE_UINT8_V(rcr, PIIXState, 3),
        VMSTATE_END_OF_LIST()
    }
};

static void rcr_write(void *opaque, hwaddr addr, uint64_t val, unsigned len)
{
    PIIXState *d = opaque;

    if (val & 4) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
    }
    d->rcr = val & 2; /* keep System Reset type only */
}

static uint64_t rcr_read(void *opaque, hwaddr addr, unsigned len)
{
    PIIXState *d = opaque;

    return d->rcr;
}

static const MemoryRegionOps rcr_ops = {
    .read = rcr_read,
    .write = rcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pci_piix_realize(PCIDevice *dev, const char *uhci_type,
                             Error **errp)
{
    PIIXState *d = PIIX_PCI_DEVICE(dev);
    PCIBus *pci_bus = pci_get_bus(dev);
    ISABus *isa_bus;

    isa_bus = isa_bus_new(DEVICE(d), pci_address_space(dev),
                          pci_address_space_io(dev), errp);
    if (!isa_bus) {
        return;
    }

    /* PIC */
    if (!qdev_realize(DEVICE(&d->pic), BUS(isa_bus), errp)) {
        return;
    }

    isa_bus_irqs(isa_bus, d->pic.in_irqs);

    memory_region_init_io(&d->rcr_mem, OBJECT(dev), &rcr_ops, d,
                          "piix-reset-control", 1);
    memory_region_add_subregion_overlap(pci_address_space_io(dev),
                                        PIIX_RCR_IOPORT, &d->rcr_mem, 1);

    i8257_dma_init(isa_bus, 0);

    /* RTC */
    qdev_prop_set_int32(DEVICE(&d->rtc), "base_year", 2000);
    if (!qdev_realize(DEVICE(&d->rtc), BUS(isa_bus), errp)) {
        return;
    }

    /* IDE */
    qdev_prop_set_int32(DEVICE(&d->ide), "addr", dev->devfn + 1);
    if (!qdev_realize(DEVICE(&d->ide), BUS(pci_bus), errp)) {
        return;
    }

    /* USB */
    if (d->has_usb) {
        object_initialize_child(OBJECT(dev), "uhci", &d->uhci, uhci_type);
        qdev_prop_set_int32(DEVICE(&d->uhci), "addr", dev->devfn + 2);
        if (!qdev_realize(DEVICE(&d->uhci), BUS(pci_bus), errp)) {
            return;
        }
    }

    /* Power Management */
    if (d->has_acpi) {
        object_initialize_child(OBJECT(d), "pm", &d->pm, TYPE_PIIX4_PM);
        qdev_prop_set_int32(DEVICE(&d->pm), "addr", dev->devfn + 3);
        qdev_prop_set_uint32(DEVICE(&d->pm), "smb_io_base", d->smb_io_base);
        qdev_prop_set_bit(DEVICE(&d->pm), "smm-enabled", d->smm_enabled);
        if (!qdev_realize(DEVICE(&d->pm), BUS(pci_bus), errp)) {
            return;
        }
        qdev_connect_gpio_out(DEVICE(&d->pm), 0,
                              qdev_get_gpio_in(DEVICE(&d->pic), 9));
    }
}

static void build_pci_isa_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    BusChild *kid;
    BusState *bus = qdev_get_child_bus(DEVICE(adev), "isa.0");

    /* PIIX PCI to ISA irq remapping */
    aml_append(scope, aml_operation_region("P40C", AML_PCI_CONFIG,
                                         aml_int(0x60), 0x04));
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        call_dev_aml_func(DEVICE(kid->child), scope);
    }
}

static void pci_piix3_init(Object *obj)
{
    PIIXState *d = PIIX_PCI_DEVICE(obj);

    object_initialize_child(obj, "pic", &d->pic, TYPE_ISA_PIC);
    object_initialize_child(obj, "rtc", &d->rtc, TYPE_MC146818_RTC);
    object_initialize_child(obj, "ide", &d->ide, TYPE_PIIX3_IDE);
}

static Property pci_piix_props[] = {
    DEFINE_PROP_UINT32("smb_io_base", PIIXState, smb_io_base, 0),
    DEFINE_PROP_UINT8("pirqa", PIIXState, pci_irq_reset_mappings[0], 0x80),
    DEFINE_PROP_UINT8("pirqb", PIIXState, pci_irq_reset_mappings[1], 0x80),
    DEFINE_PROP_UINT8("pirqc", PIIXState, pci_irq_reset_mappings[2], 0x80),
    DEFINE_PROP_UINT8("pirqd", PIIXState, pci_irq_reset_mappings[3], 0x80),
    DEFINE_PROP_BOOL("has-acpi", PIIXState, has_acpi, true),
    DEFINE_PROP_BOOL("has-usb", PIIXState, has_usb, true),
    DEFINE_PROP_BOOL("smm-enabled", PIIXState, smm_enabled, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void pci_piix3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);

    dc->reset       = piix_reset;
    dc->desc        = "ISA bridge";
    dc->vmsd        = &vmstate_piix3;
    dc->hotpluggable   = false;
    k->vendor_id    = PCI_VENDOR_ID_INTEL;
    /* 82371SB PIIX3 PCI-to-ISA bridge (Step A1) */
    k->device_id    = PCI_DEVICE_ID_INTEL_82371SB_0;
    k->class_id     = PCI_CLASS_BRIDGE_ISA;
    /*
     * Reason: part of PIIX3 southbridge, needs to be wired up by
     * pc_piix.c's pc_init1()
     */
    dc->user_creatable = false;
    device_class_set_props(dc, pci_piix_props);
    adevc->build_dev_aml = build_pci_isa_aml;
}

static const TypeInfo piix3_pci_type_info = {
    .name = TYPE_PIIX3_PCI_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PIIXState),
    .instance_init = pci_piix3_init,
    .abstract = true,
    .class_init = pci_piix3_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static void piix3_realize(PCIDevice *dev, Error **errp)
{
    ERRP_GUARD();
    PIIXState *piix3 = PIIX_PCI_DEVICE(dev);
    PCIBus *pci_bus = pci_get_bus(dev);

    pci_piix_realize(dev, TYPE_PIIX3_USB_UHCI, errp);
    if (*errp) {
        return;
    }

    pci_bus_irqs(pci_bus, piix_set_irq, piix3_pci_slot_get_pirq,
                 piix3, PIIX_NUM_PIRQS);
    pci_bus_set_route_irq_fn(pci_bus, piix3_route_intx_pin_to_irq);
}

static void piix3_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_write = piix_write_config;
    k->realize = piix3_realize;
}

static const TypeInfo piix3_info = {
    .name          = TYPE_PIIX3_DEVICE,
    .parent        = TYPE_PIIX3_PCI_DEVICE,
    .class_init    = piix3_class_init,
};

static void piix3_xen_realize(PCIDevice *dev, Error **errp)
{
    ERRP_GUARD();
    PIIXState *piix3 = PIIX_PCI_DEVICE(dev);
    PCIBus *pci_bus = pci_get_bus(dev);

    pci_piix_realize(dev, TYPE_PIIX3_USB_UHCI, errp);
    if (*errp) {
        return;
    }

    /*
     * Xen supports additional interrupt routes from the PCI devices to
     * the IOAPIC: the four pins of each PCI device on the bus are also
     * connected to the IOAPIC directly.
     * These additional routes can be discovered through ACPI.
     */
    pci_bus_irqs(pci_bus, xen_piix3_set_irq, xen_pci_slot_get_pirq,
                 piix3, XEN_PIIX_NUM_PIRQS);
}

static void piix3_xen_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_write = piix3_write_config_xen;
    k->realize = piix3_xen_realize;
}

static const TypeInfo piix3_xen_info = {
    .name          = TYPE_PIIX3_XEN_DEVICE,
    .parent        = TYPE_PIIX3_PCI_DEVICE,
    .class_init    = piix3_xen_class_init,
};

static void piix4_realize(PCIDevice *dev, Error **errp)
{
    ERRP_GUARD();
    PIIXState *s = PIIX_PCI_DEVICE(dev);
    PCIBus *pci_bus = pci_get_bus(dev);
    ISABus *isa_bus;

    pci_piix_realize(dev, TYPE_PIIX4_USB_UHCI, errp);
    if (*errp) {
        return;
    }

    isa_bus = ISA_BUS(qdev_get_child_bus(DEVICE(dev), "isa.0"));

    /* initialize pit */
    i8254_pit_init(isa_bus, 0x40, 0, NULL);

    /* RTC */
    s->rtc.irq = qdev_get_gpio_in(DEVICE(&s->pic), s->rtc.isairq);

    pci_bus_irqs(pci_bus, piix_set_irq, piix4_pci_slot_get_pirq, s,
                 PIIX_NUM_PIRQS);
}

static void piix4_init(Object *obj)
{
    PIIXState *s = PIIX_PCI_DEVICE(obj);

    object_initialize_child(obj, "pic", &s->pic, TYPE_ISA_PIC);
    object_initialize_child(obj, "rtc", &s->rtc, TYPE_MC146818_RTC);
    object_initialize_child(obj, "ide", &s->ide, TYPE_PIIX4_IDE);
}

static void piix4_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_write = piix_write_config;
    k->realize = piix4_realize;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82371AB_0;
    k->class_id = PCI_CLASS_BRIDGE_ISA;
    dc->reset = piix_reset;
    dc->desc = "ISA bridge";
    dc->vmsd = &vmstate_piix4;
    /*
     * Reason: part of PIIX4 southbridge, needs to be wired up,
     * e.g. by mips_malta_init()
     */
    dc->user_creatable = false;
    dc->hotpluggable = false;
    device_class_set_props(dc, pci_piix_props);
}

static const TypeInfo piix4_info = {
    .name          = TYPE_PIIX4_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PIIXState),
    .instance_init = piix4_init,
    .class_init    = piix4_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void piix3_register_types(void)
{
    type_register_static(&piix3_pci_type_info);
    type_register_static(&piix3_info);
    type_register_static(&piix3_xen_info);
    type_register_static(&piix4_info);
}

type_init(piix3_register_types)
