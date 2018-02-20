# Default configuration for arm-softmmu

include pci.mak
include usb.mak
CONFIG_VGA=y
CONFIG_NAND=y
CONFIG_ECC=y
CONFIG_SERIAL=y
CONFIG_SERIAL_ISA=y
CONFIG_PTIMER=y
CONFIG_SD=y
CONFIG_MAX7310=y
CONFIG_WM8750=y
CONFIG_TWL92230=y
CONFIG_TSC2005=y
CONFIG_LM832X=y
CONFIG_TMP105=y
CONFIG_TMP421=y
CONFIG_STELLARIS=y
CONFIG_STELLARIS_INPUT=y
CONFIG_STELLARIS_ENET=y
CONFIG_SSD0303=y
CONFIG_SSD0323=y
CONFIG_ADS7846=y
CONFIG_MAX111X=y
CONFIG_SSI=y
CONFIG_SSI_SD=y
CONFIG_SSI_M25P80=y
CONFIG_LAN9118=y
CONFIG_SMC91C111=y
CONFIG_ALLWINNER_EMAC=y
CONFIG_IMX_FEC=y
CONFIG_FTGMAC100=y
CONFIG_DS1338=y
CONFIG_PFLASH_CFI01=y
CONFIG_PFLASH_CFI02=y
CONFIG_MICRODRIVE=y
CONFIG_USB=y
CONFIG_USB_MUSB=y
CONFIG_USB_EHCI_SYSBUS=y
CONFIG_PLATFORM_BUS=y

CONFIG_ARM11MPCORE=y
CONFIG_A9MPCORE=y
CONFIG_A15MPCORE=y

CONFIG_ARM_V7M=y

CONFIG_ARM_GIC=y
CONFIG_ARM_GIC_KVM=$(CONFIG_KVM)
CONFIG_ARM_TIMER=y
CONFIG_ARM_MPTIMER=y
CONFIG_A9_GTIMER=y
CONFIG_PL011=y
CONFIG_PL022=y
CONFIG_PL031=y
CONFIG_PL041=y
CONFIG_PL050=y
CONFIG_PL061=y
CONFIG_PL080=y
CONFIG_PL110=y
CONFIG_PL181=y
CONFIG_PL190=y
CONFIG_PL310=y
CONFIG_PL330=y
CONFIG_CADENCE=y
CONFIG_XGMAC=y
CONFIG_EXYNOS4=y
CONFIG_PXA2XX=y
CONFIG_I2C=y
CONFIG_BITBANG_I2C=y
CONFIG_FRAMEBUFFER=y
CONFIG_XILINX_SPIPS=y
CONFIG_ZYNQ_DEVCFG=y

CONFIG_ARM11SCU=y
CONFIG_A9SCU=y
CONFIG_DIGIC=y
CONFIG_MARVELL_88W8618=y
CONFIG_OMAP=y
CONFIG_TSC210X=y
CONFIG_BLIZZARD=y
CONFIG_ONENAND=y
CONFIG_TUSB6010=y
CONFIG_IMX=y
CONFIG_MAINSTONE=y
CONFIG_MPS2=y
CONFIG_NSERIES=y
CONFIG_RASPI=y
CONFIG_REALVIEW=y
CONFIG_ZAURUS=y
CONFIG_ZYNQ=y
CONFIG_STM32F2XX_TIMER=y
CONFIG_STM32F2XX_USART=y
CONFIG_STM32F2XX_SYSCFG=y
CONFIG_STM32F2XX_ADC=y
CONFIG_STM32F2XX_SPI=y
CONFIG_STM32F205_SOC=y

CONFIG_CMSDK_APB_TIMER=y
CONFIG_CMSDK_APB_UART=y

CONFIG_MPS2_FPGAIO=y
CONFIG_MPS2_SCC=y

CONFIG_TZ_PPC=y

CONFIG_VERSATILE_PCI=y
CONFIG_VERSATILE_I2C=y

CONFIG_PCI_GENERIC=y
CONFIG_VFIO_XGMAC=y
CONFIG_VFIO_AMD_XGBE=y

CONFIG_SDHCI=y
CONFIG_INTEGRATOR_DEBUG=y

CONFIG_ALLWINNER_A10_PIT=y
CONFIG_ALLWINNER_A10_PIC=y
CONFIG_ALLWINNER_A10=y

CONFIG_FSL_IMX6=y
CONFIG_FSL_IMX31=y
CONFIG_FSL_IMX25=y

CONFIG_IMX_I2C=y

CONFIG_PCIE_PORT=y
CONFIG_XIO3130=y
CONFIG_IOH3420=y
CONFIG_I82801B11=y
CONFIG_ACPI=y
CONFIG_SMBIOS=y
CONFIG_ASPEED_SOC=y
CONFIG_GPIO_KEY=y
CONFIG_MSF2=y
CONFIG_FW_CFG_DMA=y
CONFIG_XILINX_AXI=y
