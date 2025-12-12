/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_MIPI_PANEL_H_
#define _SPACEMIT_MIPI_PANEL_H_

#include <drm/drm_print.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <linux/backlight.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/atomic.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#define INVALID_GPIO 0xFFFFFFFF

#define LCD_PANEL_RESET_CNT 4
#define LCD_DELAY_AFTER_RESET 50
#define LCD_MODE_MAX	5

enum {
	CMD_CODE_INIT = 0,
	CMD_CODE_SLEEP_IN,
	CMD_CODE_SLEEP_OUT,
	CMD_CODE_READ_ID,
	CMD_CODE_READ_POWER,
	CMD_CODE_DSC_PPS,
	CMD_CODE_COMPRESS_ON,
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT) || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	CMD_FACE_IN,
	CMD_FACE_OUT,
#endif
	CMD_CODE_MAX,
};

enum {
	DSI_MODE_CMD = 0,
	DSI_MODE_VIDEO_BURST,
	DSI_MODE_VIDEO_SYNC_PULSE,
	DSI_MODE_VIDEO_SYNC_EVENT,
};

struct panel_info {
	/* common parameters */
	struct device_node *of_node;
	struct drm_display_mode mode[LCD_MODE_MAX];
	u8 mode_num;
	const void *cmds[CMD_CODE_MAX];
	int cmds_len[CMD_CODE_MAX];

	/* esd check parameters*/
	bool esd_check_en;
	u8 esd_check_mode;
	u16 esd_check_period;
	u32 esd_check_reg;
	u32 esd_check_val;

	/* MIPI DSI specific parameters */
	u32 format;
	u32 lanes;
	u32 mode_flags;
	bool use_dcs;
	bool use_spacemit;
	u32 bl_enable_delay;

	u32 te_esd_en;
	u32 reset_on_state;

	int brightness;
	int brightness_max;
	bool is_oled;
};
struct spacemit_panel {
	int id;
	struct device dev;
	struct drm_panel base;
	struct drm_encoder *encoder;
	struct mipi_dsi_device *slave;
	struct panel_info info;

	struct delayed_work esd_work;
	struct delayed_work bl_work;
	bool esd_work_pending;

	struct regulator *vdd_1v2;
	struct regulator *vdd_1v8;
	struct regulator *vdd_2v8;
	u32 gpio_reset;
	u32 gpio_bl;
	u32 gpio_dc[2];
	u32 gpio_te;
	u32 gpio_te_irq;
	u32 gpio_te_cnt;
	u32 esd_restarting;
	struct work_struct work_reset_panel;
	struct timer_list te_esd_timer;
	atomic_t enable_refcnt;
	atomic_t prepare_refcnt;
	u32 reset_toggle_cnt;
	u32 delay_after_reset;
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT) || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	struct work_struct work_facereset_panel;
	struct work_struct work_facerecover_panel;
	bool tp_ps_enabled;
	bool tp_ps_neared;
	bool user_panel_disabled;
	u32 tp_ps_restarting;
	struct mutex face_lock;
#endif
};

static inline struct spacemit_panel *to_spacemit_panel(struct drm_panel *panel)
{
	return container_of(panel, struct spacemit_panel, base);
}
#endif
