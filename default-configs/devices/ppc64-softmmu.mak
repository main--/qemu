# Default configuration for ppc64-softmmu

# Include all 32-bit boards
include ppc-softmmu.mak

# For PowerNV
CONFIG_POWERNV=y

# For pSeries
CONFIG_PSERIES=y
CONFIG_NVDIMM=y
CONFIG_VOF=y
