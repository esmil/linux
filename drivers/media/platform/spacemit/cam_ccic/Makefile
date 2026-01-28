# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y := -Werror -Wno-maybe-uninitialized # zmc: to be removed

obj-$(CONFIG_SPACEMIT_K3_CCIC) += spacemit_ccic.o
spacemit_ccic-objs += ccic_hwreg.o
spacemit_ccic-objs += csiphy.o
spacemit_ccic-objs += ccic_drv.o
spacemit_ccic-objs += dptc_drv.o
spacemit_ccic-objs += ccic_vdev.o
spacemit_ccic-objs += ccic_iommu.o
spacemit_ccic-objs += ccic_dma.o
