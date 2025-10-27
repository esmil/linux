# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y := -Werror
subdir-ccflags-y += -I$(srctree)/drivers/media/platform/camera

obj-y += vcam_rsvmem.o

obj-y += pwrctrl.o

obj-y += vcam_clk.o

obj-$(CONFIG_SPACEMIT_K3_VCAM_CCIC) += vcamccic.o
vcamccic-objs += vcam_ccic.o

obj-$(CONFIG_SPACEMIT_K3_VCAM_CCIC_DMA) += vcamccicdma.o
vcamccicdma-objs += vcam_ccic_dma.o

obj-$(CONFIG_SPACEMIT_K3_VCAM_SENSOR) += vcamsensor.o
vcamsensor-objs += vcam_sensor.o
