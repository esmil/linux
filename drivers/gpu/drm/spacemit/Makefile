# SPDX-License-Identifier: GPL-2.0

spacemit-drm-y := 	spacemit_drm.o \
				spacemit_crtc.o \
				spacemit_cmdlist.o \
				spacemit_wb.o \
				spacemit_planes.o \
				spacemit_dsi.o \
				spacemit_dphy.o \
				spacemit_lib.o \
				spacemit_gem.o \
				spacemit_dmmu.o \
				spacemit_bootloader.o \
				common/spacemit_drm_notifier.o \
				sysfs/sysfs_class.o \
				sysfs/sysfs_dpu.o \
				sysfs/sysfs_dsi.o \
				sysfs/sysfs_dphy.o \
				sysfs/sysfs_mipi_panel.o \
				dpu/dpu_debug.o \
				dpu/dpu_saturn.o \
				dpu/post_process_hee.o \
				dpu/dpu_saturn_hee.o \
				dpu/saturn_fbcmem.o \
				dsi/spacemit_dptc_drv.o \
				dsi/spacemit_dsi_drv.o \
				dphy/spacemit_dphy_drv.o \

spacemit-inno-dp-y := dp/drm_dp.o \
			dp/inno_dp_api.o \
			dp/inno_dp_audio.o \
			dp/inno_dp.o \
			dp/inno_dp_common.o \
			dp/inno_dp_phy_board.o \
			dp/inno_edid.o \
			dp/inno_modes.o \
			dp/inno_parse_edid.o \
			dp/inno_utils.o

obj-$(CONFIG_SPACEMIT_INNO_DP) += spacemit-inno-dp.o


obj-$(CONFIG_DRM_SPACEMIT) += spacemit-drm.o

obj-$(CONFIG_SPACEMIT_MIPI_PANEL) += spacemit_mipi_panel.o
obj-$(CONFIG_DRM_SPACEMIT_BACKLIGHT) += backlight/
obj-$(CONFIG_DRM_SPACEMIT_SELFTEST) += selftest/
obj-$(CONFIG_DRM_RASPBERRYPI_TOUCHSCREEN) += raspberrypi_touchscreen.o
