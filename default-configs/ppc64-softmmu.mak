# Default configuration for ppc64-softmmu

# Include all 32-bit boards
include ppc-softmmu.mak

# For PowerNV
CONFIG_POWERNV=y
CONFIG_IPMI=y
CONFIG_IPMI_LOCAL=y
CONFIG_IPMI_EXTERN=y
CONFIG_ISA_IPMI_BT=y

# For pSeries
CONFIG_PSERIES=y
CONFIG_VIRTIO_VGA=y
CONFIG_XICS=$(CONFIG_PSERIES)
CONFIG_XICS_SPAPR=$(CONFIG_PSERIES)
CONFIG_XICS_KVM=$(call land,$(CONFIG_PSERIES),$(CONFIG_KVM))
CONFIG_XIVE=$(CONFIG_PSERIES)
CONFIG_XIVE_SPAPR=$(CONFIG_PSERIES)
CONFIG_MEM_HOTPLUG=y
