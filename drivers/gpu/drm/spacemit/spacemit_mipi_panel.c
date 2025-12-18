// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <drm/drm_atomic_helper.h>
#include <linux/atomic.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <video/mipi_display.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include "spacemit_bootloader.h"
#include "spacemit_mipi_panel.h"
#include "spacemit_dsi.h"
#include "spacemit_crtc.h"
#include "dpu/dpu_saturn.h"
#include "sysfs/sysfs_display.h"
#include "./common/spacemit_drm_notifier.h"
#include "./backlight/spacemit-backlight.h"

const char *lcd_name;
#if IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
#include <linux/notifier.h>
#include "../../../input/touchscreen/sitronix_ts/sitronix_ts.h"
#endif


#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT)
#include <linux/notifier.h>
#include "../../../input/touchscreen/omnivision_tcm/omnivision_tcm_core.h"
#endif

#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT) || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
struct spacemit_panel *face_panel;
int (*vh_lcd_tp_event_handler)(struct notifier_block *nb, unsigned long event, void *data) = NULL;
EXPORT_SYMBOL_GPL(vh_lcd_tp_event_handler);

/* tp_ps face in/out */
static void tp_ps_spacemit_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct spacemit_dsi *dsi = encoder_to_dsi(encoder);
	struct spacemit_panel *panel = container_of(dsi->panel, struct spacemit_panel, base);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(encoder->crtc);
	int ret = 0;

	if (a_crtc->is_stopped)
		return;
	mutex_lock(&panel->face_lock);
	DRM_INFO("%s(0)\n", __func__);

	if (dsi->panel && dsi->panel->backlight) {
		ret = backlight_disable(dsi->panel->backlight);
		if (ret < 0)
			DRM_DEV_INFO(dsi->panel->dev, "failed to disable backlight: %d\n", ret);
	}

	if (dsi->panel) {
		drm_panel_disable(dsi->panel);
		drm_panel_unprepare(dsi->panel);
	}

	if (dsi->core && dsi->core->dsi_enable_irq)
		dsi->core->dsi_enable_irq(&dsi->ctx, false);

	DRM_INFO("%s(1)\n", __func__);
	mutex_unlock(&panel->face_lock);
}

int spacemit_drm_panel_enable(struct drm_panel *panel)
{
	int ret;

	if (!panel)
		return -EINVAL;

	DRM_INFO("%s()\n", __func__);

	if (panel->enabled) {
		dev_warn(panel->dev, "Skipping enable of already enabled panel\n");
		return 0;
	}

	if (panel->funcs && panel->funcs->enable) {
		ret = panel->funcs->enable(panel);
		if (ret < 0)
			return ret;
	}
	panel->enabled = true;

	return 0;
}

static void __maybe_unused tp_ps_spacemit_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct spacemit_dsi *dsi = encoder_to_dsi(encoder);
	struct spacemit_panel *panel = container_of(dsi->panel, struct spacemit_panel, base);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(encoder->crtc);
	int ret = 0;

	if (a_crtc->is_stopped)
		return;
	mutex_lock(&panel->face_lock);
	DRM_INFO("%s(0)\n", __func__);

	if (!dsi->core || !dsi->core->dsi_open) {
		DRM_ERROR("%s(), dsi->core is null!\n", __func__);
		return;
	}

	if (panel->encoder == NULL)
		panel->encoder = encoder;

	if (dsi->panel) {
		drm_panel_prepare(dsi->panel);
		spacemit_drm_panel_enable(dsi->panel);
	}

	if (dsi->core && dsi->core->dsi_enable_irq)
		dsi->core->dsi_enable_irq(&dsi->ctx, true);

	msleep(220);
	ret = backlight_enable(dsi->panel->backlight);
	if (ret < 0)
		DRM_DEV_INFO(dsi->panel->dev, "failed to enable backlight: %d\n",
			     ret);

	DRM_INFO("%s(1)\n", __func__);
	mutex_unlock(&panel->face_lock);
}
#endif

static void _spacemit_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct spacemit_dsi *dsi = encoder_to_dsi(encoder);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(encoder->crtc);
	int ret = 0;

	DRM_INFO("%s()\n", __func__);

	if (dsi->panel && dsi->panel->backlight) {
		ret = backlight_disable(dsi->panel->backlight);
		if (ret < 0)
			DRM_DEV_INFO(dsi->panel->dev, "failed to disable backlight: %d\n", ret);
	}

	a_crtc->is_stopped = true;
	spacemit_crtc_stop(a_crtc);

	if (dsi->core && dsi->core->dsi_close_datatx)
		dsi->core->dsi_close_datatx(&dsi->ctx);

	if (dsi->panel) {
		drm_panel_disable(dsi->panel);
		drm_panel_unprepare(dsi->panel);
	}

	if (dsi->core && dsi->core->dsi_close)
		dsi->core->dsi_close(&dsi->ctx);
}

static void _spacemit_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct spacemit_dsi *dsi = encoder_to_dsi(encoder);
	struct spacemit_panel *panel = container_of(dsi->panel, struct spacemit_panel, base);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(encoder->crtc);
	void __iomem *addr = (void __iomem *)ioremap(0xd421a1a8, 100);

	if (!dsi->core || !dsi->core->dsi_open) {
		DRM_ERROR("%s(), dsi->core is null!\n", __func__);
		return;
	}

	if (panel->encoder == NULL)
		panel->encoder = encoder;

	/* Dove dsi online setup */
	writel(0x40000001, addr);
	writel(0x1, addr + 0x10);

	dsi->core->dsi_open(&dsi->ctx, false);

	if (dsi->panel) {
		drm_panel_prepare(dsi->panel);
		drm_panel_enable(dsi->panel);
		if ((spacemit_dpu_logo_booton == false) &&
			(panel->info.bl_enable_delay))
			backlight_disable(dsi->panel->backlight);
	}

	if (dsi->core && dsi->core->dsi_ready_for_datatx)
		dsi->core->dsi_ready_for_datatx(&dsi->ctx);

	if (panel->esd_restarting)
		spacemit_dpu_esd_restart(a_crtc);
	a_crtc->is_stopped = false;
}

#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT)  || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
static void spacemit_mipi_wq_facerecover_panel(struct work_struct *work)
{
	struct spacemit_panel *panel = container_of(work, struct spacemit_panel, work_facerecover_panel);
	struct drm_encoder *encoder = NULL;
	DRM_INFO("%s()\n", __func__);
	encoder = panel->encoder;
	if (encoder == NULL)
		return;

	tp_ps_spacemit_dsi_encoder_enable(encoder);
	panel->tp_ps_restarting = 0;
}

static void spacemit_mipi_wq_facereset_panel(struct work_struct *work)
{
	struct spacemit_panel *panel = container_of(work, struct spacemit_panel, work_facereset_panel);
	struct drm_encoder *encoder = NULL;

	DRM_INFO("%s()\n", __func__);
	encoder = panel->encoder;
	if (encoder == NULL)
		return;

	panel->tp_ps_restarting = 1;
	tp_ps_spacemit_dsi_encoder_disable(encoder);
}
#endif

#if IS_ENABLED(CONFIG_DRM_SPACEMIT_BACKLIGHT)
static int spacemit_mipi_panel_set_brightness(void *devdata, int value)
{
	struct spacemit_panel *panel = (struct spacemit_panel *)devdata;
	int ret = 0;
	int brightness = value;

	if (!atomic_read(&panel->prepare_refcnt)) {
		panel->info.brightness = brightness;
		return 0;
	}

	if (panel && (brightness != panel->info.brightness)) {
		panel->slave->mode_flags &= ~MIPI_DSI_MODE_LPM;
		ret = mipi_dsi_dcs_set_display_brightness(panel->slave, (u16)brightness);
		panel->slave->mode_flags |= MIPI_DSI_MODE_LPM;
		panel->info.brightness = brightness;
	}

	return ret;
}

static int spacemit_mipi_panel_get_brightness(void *devdata)
{
	struct spacemit_panel *panel = (struct spacemit_panel *)devdata;
	int value = 0;

	if (panel)
		value = panel->info.brightness;

	return value;
}

static struct spacemit_bl_ops spacemit_panel_bl_ops = {
	.set_bl = spacemit_mipi_panel_set_brightness,
	.get_bl = spacemit_mipi_panel_get_brightness,
};
#endif

static ssize_t mipi_dsi_device_transfer(struct mipi_dsi_device *dsi,
					struct mipi_dsi_msg *msg)
{
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;

	if (!ops || !ops->transfer)
		return -ENOSYS;

	if (dsi->mode_flags & MIPI_DSI_MODE_LPM)
		msg->flags |= MIPI_DSI_MSG_USE_LPM;
	//msg->flags |= MIPI_DSI_MSG_LASTCOMMAND;

	return ops->transfer(dsi->host, msg);
}

static ssize_t mipi_dsi_spacemit_write(struct mipi_dsi_device *dsi, const void *payload,
			       uint8_t cmd_type, size_t size)
{
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = cmd_type,
		.tx_buf = payload,
		.tx_len = size
	};

	return mipi_dsi_device_transfer(dsi, &msg);
}

static void spacemit_mipi_wq_reset_panel(struct work_struct *work)
{
	struct spacemit_panel *panel = container_of(work, struct spacemit_panel, work_reset_panel);
	struct drm_encoder *encoder = NULL;
	struct spacemit_dsi *dsi = NULL;

	if (panel->gpio_te_irq) {
		enable_irq(panel->gpio_te_irq);
		usleep_range(16000, 18000);
		disable_irq(panel->gpio_te_irq);

		if (panel->gpio_te_cnt) {
			panel->gpio_te_cnt = 0;
			return;
		}
	}

	DRM_INFO("%s()\n", __func__);
	encoder = panel->encoder;
	if (encoder == NULL)
		return;

	dsi = encoder_to_dsi(encoder);

	mutex_lock(&dsi->disable_lock);
	/* hwc send a diable commit */
	if (dsi->is_disabled == true) {
		DRM_INFO("esd abort since lcd disabled\n");
		mutex_unlock(&dsi->disable_lock);
		return;
	}

	DRM_INFO("====== esd recovery start ========\n");
	panel->esd_restarting = 1;
	_spacemit_dsi_encoder_disable(encoder);
	msleep(10);
	_spacemit_dsi_encoder_enable(encoder);
	panel->esd_restarting = 0;
	DRM_INFO("======= esd recovery end =========\n");
	mutex_unlock(&dsi->disable_lock);
}

static void spacemit_mipi_te_esd_timer_handler(struct timer_list *t)
{
	struct spacemit_panel *panel = from_timer(panel, t, te_esd_timer);

	mod_timer(&panel->te_esd_timer,
		 jiffies + msecs_to_jiffies(2000));
	queue_work(system_wq, &panel->work_reset_panel);
}

static int __maybe_unused spacemit_panel_send_cmds(struct mipi_dsi_device *dsi,
				const void *data, int size)
{
	struct spacemit_panel *panel;
	struct spacemit_dsi_cmd_desc *cmds = NULL;
	u16 len;
	int data_off = 0;
	int i = 0;
	unsigned char *tmp = NULL;

	if (dsi == NULL)
		return -EINVAL;

	panel = mipi_dsi_get_drvdata(dsi);
	cmds  = devm_kzalloc(&dsi->dev, sizeof(struct spacemit_dsi_cmd_desc), GFP_KERNEL);
	if (!cmds)
		return -ENOMEM;

	while (size > 0) {
		cmds->cmd_type = *(unsigned char *)(data + data_off++);
		cmds->lp = *(unsigned char *)(data + data_off++);
		cmds->delay = *(unsigned char *)(data + data_off++);
		cmds->length = *(unsigned char *)(data + data_off++);
		for (i = 0; i < cmds->length; i++) {
			tmp = (unsigned char *)data + data_off++;
			cmds->data[i] = *(unsigned char *)tmp;
		}

		len = cmds->length;

		if (cmds->lp)
			dsi->mode_flags |= MIPI_DSI_MODE_LPM;
		else
			dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

		if (panel->info.use_dcs)
			mipi_dsi_dcs_write_buffer(dsi, cmds->data, len);
		else if (panel->info.use_spacemit)
			mipi_dsi_spacemit_write(dsi, cmds->data, cmds->cmd_type, len);
		else
			mipi_dsi_generic_write(dsi, cmds->data, len);

		if (cmds->delay)
			msleep(cmds->delay);
		size -= (len + 4);
	}

	devm_kfree(&dsi->dev, cmds);

	return 0;
}

static int spacemit_panel_unprepare(struct drm_panel *p)
{
	struct spacemit_panel *panel = to_spacemit_panel(p);
#if !IS_ENABLED(CONFIG_TOUCHSCREEN_NT36528)
	struct spacemit_drm_notifier noti_blank;

	/* do nothing before spacemit_panel_prepare been called */
	int blank = DRM_PANEL_BLANK_POWERDOWN;

	noti_blank.blank = blank;

	if (!atomic_read(&panel->prepare_refcnt))
		return 0;

	DRM_INFO("mipi: POWERDOWN!!\n");
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT) || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	noti_blank.blank = blank;
	if (panel->tp_ps_enabled == false)
		spacemit_drm_notifier_call_chain(DRM_PANEL_EARLY_EVENT_BLANK, &noti_blank);
#else
	spacemit_drm_notifier_call_chain(DRM_PANEL_EARLY_EVENT_BLANK, &noti_blank);
#endif
#endif
	DRM_INFO("%s()\n", __func__);
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT) || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	if (panel->tp_ps_enabled == false)
		gpio_direction_output(panel->gpio_reset, panel->info.reset_on_state);
#else
	gpio_direction_output(panel->gpio_reset, panel->info.reset_on_state);
#endif
	if (panel->gpio_bl != INVALID_GPIO)
		gpio_direction_output(panel->gpio_bl, 0);
	msleep(150);

	if (panel->gpio_dc[0] != INVALID_GPIO &&
		panel->gpio_dc[1] != INVALID_GPIO) {
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT)  || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
		if (panel->tp_ps_enabled == false) {
			gpio_direction_output(panel->gpio_dc[0], 0);
			gpio_direction_output(panel->gpio_dc[1], 0);
		}
#else
		gpio_direction_output(panel->gpio_dc[0], 0);
		gpio_direction_output(panel->gpio_dc[1], 0);
#endif
	}

	if (panel->vdd_1v2)
		regulator_disable(panel->vdd_1v2);
	if (panel->vdd_1v8)
		regulator_disable(panel->vdd_1v8);

#if !IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	if (panel->vdd_2v8)
		regulator_disable(panel->vdd_2v8);
#endif
	atomic_set(&panel->prepare_refcnt, 0);
	return 0;
}

static void spacemit_prepare_regulator(struct spacemit_panel *panel)
{
	int ret = 0;

	if (unlikely(spacemit_dpu_logo_booton))
		return;

	if (panel->vdd_2v8 != NULL) {
		ret = regulator_enable(panel->vdd_2v8);
		if (ret)
			DRM_ERROR("enable lcd regulator vdd_2v8 failed\n");
	}

	if (panel->vdd_1v8 != NULL) {
		ret = regulator_enable(panel->vdd_1v8);
		if (ret)
			DRM_ERROR("enable lcd regulator vdd_1v8 failed\n");
	}

	if (panel->vdd_1v2 != NULL) {
		ret = regulator_enable(panel->vdd_1v2);
		if (ret)
			DRM_ERROR("enable lcd regulator vdd_1v2 failed\n");
	}
}

static int spacemit_panel_prepare(struct drm_panel *p)
{
	struct spacemit_panel *panel = to_spacemit_panel(p);
	struct spacemit_drm_notifier noti_blank;
	int i = 0;

	int blank_ = DRM_PANEL_BLANK_UNBLANK;

	/* prevent this function been called twice */
	if (atomic_read(&panel->prepare_refcnt))
		return 0;

	DRM_INFO("%s()\n", __func__);

	spacemit_prepare_regulator(panel);

	if (panel->gpio_dc[0] != INVALID_GPIO &&
		panel->gpio_dc[1] != INVALID_GPIO) {
		gpio_direction_output(panel->gpio_dc[0], 1);
		gpio_direction_output(panel->gpio_dc[1], 1);
	}

	if (panel->gpio_bl != INVALID_GPIO)
		gpio_direction_output(panel->gpio_bl, 1);

	if (unlikely(spacemit_dpu_logo_booton))
		goto out;

	noti_blank.blank = blank_;
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT)  || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	if (panel->tp_ps_enabled == false) {
#if !IS_ENABLED(CONFIG_TOUCHSCREEN_NT36528)
		spacemit_drm_notifier_call_chain(DRM_PANEL_EVENT_BLANK, &noti_blank);
#endif
		gpio_direction_output(panel->gpio_reset, 1);
		for (; i < panel->reset_toggle_cnt; i++) {
			msleep(10);
			gpio_direction_output(panel->gpio_reset, 0);
			msleep(10);
			gpio_direction_output(panel->gpio_reset, 1);
		}
		msleep(panel->delay_after_reset);
	}
	spacemit_drm_notifier_call_chain(DRM_PANEL_TOUCH_INT, &noti_blank);
#else
	spacemit_drm_notifier_call_chain(DRM_PANEL_EVENT_BLANK, &noti_blank);
	gpio_direction_output(panel->gpio_reset, 1);
	for (; i < panel->reset_toggle_cnt; i++) {
		msleep(10);
		gpio_direction_output(panel->gpio_reset, 0);
		msleep(10);
		gpio_direction_output(panel->gpio_reset, 1);
	}
	msleep(panel->delay_after_reset);
	spacemit_drm_notifier_call_chain(DRM_PANEL_TOUCH_INT, &noti_blank);
#endif
	DRM_INFO("mipi: UNBLANK!!\n");

out:
	/* update refcnt */
	atomic_set(&panel->prepare_refcnt, 1);
	return 0;
}

static int spacemit_panel_disable(struct drm_panel *p)
{
	struct spacemit_panel *panel = to_spacemit_panel(p);

	if (!atomic_read(&panel->enable_refcnt))
		return 0;

	DRM_INFO("%s()\n", __func__);

	if (panel->esd_work_pending) {
		cancel_delayed_work_sync(&panel->esd_work);
		panel->esd_work_pending = false;
	}

	cancel_delayed_work_sync(&panel->bl_work);

	if (panel->gpio_te_irq)
		del_timer(&panel->te_esd_timer);
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT)  || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	if (panel->tp_ps_enabled) {
		DRM_INFO("%s(face IN)\n", __func__);
		spacemit_panel_send_cmds(panel->slave,
				panel->info.cmds[CMD_FACE_IN],
				panel->info.cmds_len[CMD_FACE_IN]);
	} else {
		DRM_INFO("%s(sleep IN)\n", __func__);
#if IS_ENABLED(CONFIG_TOUCHSCREEN_NT36528)
		struct spacemit_drm_notifier noti_blank;
		int blank = DRM_PANEL_BLANK_POWERDOWN;

		noti_blank.blank = blank;
		spacemit_drm_notifier_call_chain(DRM_PANEL_EARLY_EVENT_BLANK, &noti_blank);
#endif
		spacemit_panel_send_cmds(panel->slave,
				panel->info.cmds[CMD_CODE_SLEEP_IN],
				panel->info.cmds_len[CMD_CODE_SLEEP_IN]);
	}
#else
	spacemit_panel_send_cmds(panel->slave,
			panel->info.cmds[CMD_CODE_SLEEP_IN],
			panel->info.cmds_len[CMD_CODE_SLEEP_IN]);
#endif
	atomic_set(&panel->enable_refcnt, 0);
	return 0;
}

static int spacemit_panel_enable(struct drm_panel *p)
{
	struct spacemit_panel *panel = to_spacemit_panel(p);

	if (atomic_read(&panel->enable_refcnt))
		return 0;

	DRM_INFO("%s()\n", __func__);

	if (panel->gpio_te_irq)
		mod_timer(&panel->te_esd_timer,
			jiffies + msecs_to_jiffies(2000));

	if (unlikely(spacemit_dpu_logo_booton))
		goto out;

#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT) || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	if (panel->tp_ps_enabled) {
		DRM_INFO("%s(face OUT)\n", __func__);
		spacemit_panel_send_cmds(panel->slave,
				panel->info.cmds[CMD_FACE_OUT],
				panel->info.cmds_len[CMD_FACE_OUT]);
	} else {
		DRM_INFO("%s(sleep OUT)\n", __func__);
		spacemit_panel_send_cmds(panel->slave,
				panel->info.cmds[CMD_CODE_INIT],
				panel->info.cmds_len[CMD_CODE_INIT]);
#if IS_ENABLED(CONFIG_TOUCHSCREEN_NT36528)
		struct spacemit_drm_notifier noti_blank;
		int blank_ = DRM_PANEL_BLANK_UNBLANK;

		noti_blank.blank = blank_;
		spacemit_drm_notifier_call_chain(DRM_PANEL_EVENT_BLANK, &noti_blank);
#endif
	}
#else

	spacemit_panel_send_cmds(panel->slave,
			panel->info.cmds[CMD_CODE_INIT],
			panel->info.cmds_len[CMD_CODE_INIT]);
#endif
	if (panel->info.cmds[CMD_CODE_DSC_PPS]) {
		spacemit_panel_send_cmds(panel->slave,
				panel->info.cmds[CMD_CODE_DSC_PPS],
				panel->info.cmds_len[CMD_CODE_DSC_PPS]);
	}

	if (panel->info.cmds[CMD_CODE_COMPRESS_ON]) {
		spacemit_panel_send_cmds(panel->slave,
				panel->info.cmds[CMD_CODE_COMPRESS_ON],
				panel->info.cmds_len[CMD_CODE_COMPRESS_ON]);
	}

	if (panel->info.esd_check_en) {
		schedule_delayed_work(&panel->esd_work,
				      msecs_to_jiffies(1000));
		panel->esd_work_pending = true;
	}

	if (panel->info.bl_enable_delay) {
		schedule_delayed_work(&panel->bl_work,
			msecs_to_jiffies(panel->info.bl_enable_delay));
	}
out:
	atomic_set(&panel->enable_refcnt, 1);
	return 0;
}

static int spacemit_panel_get_modes(struct drm_panel *p, struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct spacemit_panel *panel = to_spacemit_panel(p);
	int index = 0;
	int cnt = 0;

	DRM_INFO("%s()\n", __func__);

	for (; index < panel->info.mode_num; index++) {
		mode = drm_mode_duplicate(connector->dev, &panel->info.mode[index]);
		if (!mode) {
			DRM_ERROR("failed to add mode %ux%ux@%u\n",
					panel->info.mode[index].hdisplay,
					panel->info.mode[index].vdisplay,
					drm_mode_vrefresh(mode));
			return -ENOMEM;
		}

		drm_mode_set_name(mode);
		// if (index)
		// 	snprintf(mode->name, DRM_DISPLAY_MODE_LEN, "%s_%d", mode->name, index);

		if (drm_mode_vrefresh(mode) == 60)
			mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		else
			mode->type = DRM_MODE_TYPE_DRIVER;
		drm_mode_probed_add(connector, mode);
		cnt++;
	}

	connector->display_info.width_mm = panel->info.mode[0].width_mm;
	connector->display_info.height_mm = panel->info.mode[0].height_mm;


	return cnt;
}

static const struct drm_panel_funcs spacemit_panel_funcs = {
	.get_modes = spacemit_panel_get_modes,
	.enable = spacemit_panel_enable,
	.disable = spacemit_panel_disable,
	.prepare = spacemit_panel_prepare,
	.unprepare = spacemit_panel_unprepare,
};

static int spacemit_mipi_dsi_set_maximum_return_packet_size(struct mipi_dsi_device *dsi,
					    u16 value)
{
	u8 tx[2] = { value & 0xff, value >> 8 };
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE,
		.tx_len = sizeof(tx),
		.tx_buf = tx,
	};
	int ret = mipi_dsi_device_transfer(dsi, &msg);

	return (ret < 0) ? ret : 0;
}

static int spacemit_panel_esd_check(struct spacemit_panel *panel)
{
	struct panel_info *info = &panel->info;
	u8 read_val = 0;

	spacemit_mipi_dsi_set_maximum_return_packet_size(panel->slave, 1);
	mipi_dsi_dcs_read(panel->slave, info->esd_check_reg,
			  &read_val, 1);

	if (read_val != info->esd_check_val) {
		DRM_ERROR("esd check failed, read value = 0x%02x\n",
			  read_val);
		return -EINVAL;
	}
	DRM_INFO("esd check, read value = 0x%02x\n", read_val);

	return 0;
}

static void spacemit_panel_enable_backlight_work_func(struct work_struct *work)
{
	struct spacemit_panel *panel = container_of(work, struct spacemit_panel,
						bl_work.work);
	backlight_enable(panel->base.backlight);
}

static void spacemit_panel_esd_work_func(struct work_struct *work)
{
	struct spacemit_panel *panel = container_of(work, struct spacemit_panel,
						esd_work.work);
	struct panel_info *info = &panel->info;
	int ret;

	ret = spacemit_panel_esd_check(panel);
	if (ret) {
		/*
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;

		encoder = panel->base.connector->encoder;
		funcs = encoder->helper_private;
		panel->esd_work_pending = false;

		DRM_INFO("====== esd recovery start ========\n");
		funcs->disable(encoder);
		funcs->enable(encoder);
		DRM_INFO("======= esd recovery end =========\n");
		*/
	} else
		schedule_delayed_work(&panel->esd_work,
			msecs_to_jiffies(info->esd_check_period));
}

static int spacemit_panel_parse_dt(struct device_node *np, struct spacemit_panel *panel)
{
	u32 val;
	struct device_node *lcd_node;
	struct panel_info *info = &panel->info;
	int bytes, rc;
	const void *p;
	const char *str;
	char lcd_path[60];
	int index = 0;

	rc = of_property_read_string(np, "force-attached", &str);
	if (!rc)
		lcd_name = str;

	sprintf(lcd_path, "/lcds/%s", lcd_name);
	lcd_node = of_find_node_by_path(lcd_path);
	if (!lcd_node) {
		DRM_ERROR("%pOF: could not find %s node\n", np, lcd_name);
		return -ENODEV;
	}
	info->of_node = lcd_node;

	rc = of_property_read_u32(lcd_node, "dsi-work-mode", &val);
	if (!rc) {
		if (val == DSI_MODE_CMD)
			info->mode_flags = 0;
		else if (val == DSI_MODE_VIDEO_BURST)
			info->mode_flags = MIPI_DSI_MODE_VIDEO |
					   MIPI_DSI_MODE_VIDEO_BURST;
		else if (val == DSI_MODE_VIDEO_SYNC_PULSE)
			info->mode_flags = MIPI_DSI_MODE_VIDEO |
					   MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
		else if (val == DSI_MODE_VIDEO_SYNC_EVENT)
			info->mode_flags = MIPI_DSI_MODE_VIDEO;
	} else {
		DRM_ERROR("dsi work mode is not found! use video mode\n");
		info->mode_flags = MIPI_DSI_MODE_VIDEO |
				   MIPI_DSI_MODE_VIDEO_BURST;
	}

	if (of_property_read_bool(lcd_node, "dsi-non-continuous-clock"))
		info->mode_flags |= MIPI_DSI_CLOCK_NON_CONTINUOUS;

	rc = of_property_read_u32(lcd_node, "dsi-lane-number", &val);
	if (!rc)
		info->lanes = val;
	else
		info->lanes = 4;

	rc = of_property_read_string(lcd_node, "dsi-color-format", &str);
	if (rc)
		info->format = MIPI_DSI_FMT_RGB888;
	else if (!strcmp(str, "rgb888"))
		info->format = MIPI_DSI_FMT_RGB888;
	else if (!strcmp(str, "rgb666"))
		info->format = MIPI_DSI_FMT_RGB666;
	else if (!strcmp(str, "rgb666_packed"))
		info->format = MIPI_DSI_FMT_RGB666_PACKED;
	else if (!strcmp(str, "rgb565"))
		info->format = MIPI_DSI_FMT_RGB565;
	else
		DRM_ERROR("dsi-color-format (%s) is not supported\n", str);

	rc = of_property_read_u32(lcd_node, "width-mm", &val);
	if (!rc)
		info->mode[0].width_mm = val;
	else
		info->mode[0].width_mm = 68;

	rc = of_property_read_u32(lcd_node, "height-mm", &val);
	if (!rc)
		info->mode[0].height_mm = val;
	else
		info->mode[0].height_mm = 121;

	rc = of_property_read_u32(lcd_node, "esd-check-enable", &val);
	if (!rc)
		info->esd_check_en = val;

	rc = of_property_read_u32(lcd_node, "esd-check-mode", &val);
	if (!rc)
		info->esd_check_mode = val;
	else
		info->esd_check_mode = 1;

	rc = of_property_read_u32(lcd_node, "esd-check-period", &val);
	if (!rc)
		info->esd_check_period = val;
	else
		info->esd_check_period = 1000;

	rc = of_property_read_u32(lcd_node, "esd-check-register", &val);
	if (!rc)
		info->esd_check_reg = val;
	else
		info->esd_check_reg = 0x0A;

	rc = of_property_read_u32(lcd_node, "esd-check-value", &val);
	if (!rc)
		info->esd_check_val = val;
	else
		info->esd_check_val = 0x9C;


	rc = of_property_read_u32(lcd_node, "bl-enable-delay", &val);
	if (!rc)
		info->bl_enable_delay = val;
	else
		info->bl_enable_delay = 0;

	if (of_property_read_bool(lcd_node, "use-dcs-write"))
		info->use_dcs = true;
	else
		info->use_dcs = false;

	if (of_property_read_bool(lcd_node, "use-spacemit-write"))
		info->use_spacemit = true;
	else
		info->use_spacemit = false;

	if (of_property_read_bool(lcd_node, "oled"))
		info->is_oled = true;
	else
		info->is_oled = false;

	if (info->is_oled) {
		rc = of_property_read_u32(lcd_node, "brightness", &val);
		if (!rc)
			info->brightness = val;
		else
			info->brightness = 255;
		rc = of_property_read_u32(lcd_node, "brightness-max", &val);
		if (!rc)
			info->brightness_max = val;
		else
			info->brightness_max = 255;
	}

	p = of_get_property(lcd_node, "read-id-command", &bytes);
	if (p) {
		info->cmds[CMD_CODE_READ_ID] = p;
		info->cmds_len[CMD_CODE_READ_ID] = bytes;
	} else
		DRM_ERROR("can't find read-id property\n");

	p = of_get_property(lcd_node, "initial-command", &bytes);
	if (p) {
		info->cmds[CMD_CODE_INIT] = p;
		info->cmds_len[CMD_CODE_INIT] = bytes;
	} else
		DRM_ERROR("can't find initial-command property\n");

	p = of_get_property(lcd_node, "sleep-in-command", &bytes);
	if (p) {
		info->cmds[CMD_CODE_SLEEP_IN] = p;
		info->cmds_len[CMD_CODE_SLEEP_IN] = bytes;
	} else
		DRM_ERROR("can't find sleep-in-command property\n");

	p = of_get_property(lcd_node, "sleep-out-command", &bytes);
	if (p) {
		info->cmds[CMD_CODE_SLEEP_OUT] = p;
		info->cmds_len[CMD_CODE_SLEEP_OUT] = bytes;
	} else
		DRM_ERROR("can't find sleep-out-command property\n");

#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT)  || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	p = of_get_property(lcd_node, "face-in-command", &bytes);
	if (p) {
		info->cmds[CMD_FACE_IN] = p;
		info->cmds_len[CMD_FACE_IN] = bytes;
	} else
		DRM_ERROR("can't find CMD_FACE_IN property\n");

	p = of_get_property(lcd_node, "face-out-command", &bytes);
	if (p) {
		info->cmds[CMD_FACE_OUT] = p;
		info->cmds_len[CMD_FACE_OUT] = bytes;
	} else
		DRM_ERROR("can't find CMD_FACE_OUT property\n");
#endif


	rc = of_property_read_u32(lcd_node, "dsc-enable", &val);
	if (rc)
		val = 0;

	info->cmds[CMD_CODE_DSC_PPS] = NULL;
	info->cmds_len[CMD_CODE_DSC_PPS] = 0;
	info->cmds[CMD_CODE_COMPRESS_ON] = NULL;
	info->cmds_len[CMD_CODE_COMPRESS_ON] = 0;
	if (val) {
		p = of_get_property(lcd_node, "dsc-pps-command", &bytes);
		if (p) {
			info->cmds[CMD_CODE_DSC_PPS] = p;
			info->cmds_len[CMD_CODE_DSC_PPS] = bytes;
		} else {
			DRM_ERROR("can't find dsc-pps-command property\n");
		}

		p = of_get_property(lcd_node, "compress-on-command", &bytes);
		if (p) {
			info->cmds[CMD_CODE_COMPRESS_ON] = p;
			info->cmds_len[CMD_CODE_COMPRESS_ON] = bytes;
		} else {
			DRM_ERROR("can't find compress-on-command property\n");
		}
	}

	for (; index < LCD_MODE_MAX; index++) {
		rc = of_get_drm_display_mode(lcd_node, &info->mode[index], 0, index);
		if (rc) {
			DRM_DEBUG("get display timing done\n");
			info->mode_num = index--;
			break;
		}
	}

	rc = of_property_read_u32(lcd_node, "te-esd-en", &val);
	if (!rc)
		info->te_esd_en = val;
	else
		info->te_esd_en = 0;

	rc = of_property_read_u32(lcd_node, "reset-on-state", &val);
	if (!rc)
		info->reset_on_state = val;
	else
		info->reset_on_state = 1; /* defalt reset on state is 1 */

	return 0;
}

static int spacemit_panel_device_create(struct device *parent,
				    struct spacemit_panel *panel)
{
	panel->dev.class = display_class;
	panel->dev.parent = parent;
	panel->dev.of_node = panel->info.of_node;
	dev_set_name(&panel->dev, "panel%d", panel->id);
	dev_set_drvdata(&panel->dev, panel);

	return device_register(&panel->dev);
}

static irqreturn_t spacemit_mipi_gpio_te_irq_handler(int irq, void *dev_id)
{
	struct spacemit_panel *panel = (struct spacemit_panel *)dev_id;

	panel->gpio_te_cnt++;

	return IRQ_HANDLED;

}
#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT)  || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
extern int gcore_proximity_reg_notifier(struct notifier_block *nb);
extern int gcore_proximity_unreg_notifier(struct notifier_block *nb);

static int lcd_tp_event_handler(struct notifier_block *nb, unsigned long event, void *data)
{
	switch (event) {
	case PS_NOTIFY_NEAR:
		DRM_DEBUG("LCD: Received NEAR event\n");
		if (face_panel->user_panel_disabled == true) {
			DRM_DEBUG("user space disable panel now, ignore tp_ps\n");
			break;
		}

		if (face_panel->tp_ps_neared == true) {
			DRM_DEBUG("tp_ps_neared enabled, ignore tp_ps\n");
			break;
		}
		if (face_panel->tp_ps_enabled == false) {
			DRM_DEBUG("tp_ps disabled, ignore tp_ps\n");
			break;
		}
		face_panel->tp_ps_neared = true;
		queue_work(system_wq, &face_panel->work_facereset_panel);
		break;
	case PS_NOTIFY_FAR:
		DRM_DEBUG("LCD: Received AWAY event\n");
		if (face_panel->user_panel_disabled == true) {
			DRM_DEBUG("user space disable panel now, ignore tp_ps\n");
			break;
		}
		if (face_panel->tp_ps_neared == false) {
			DRM_DEBUG("tp_ps_neared disabled, ignore tp_ps\n");
			break;
		}
		if (face_panel->tp_ps_enabled == false) {
			DRM_DEBUG("tp_ps disabled, ignore tp_ps\n");
			break;
		}
		face_panel->tp_ps_neared = false;
		queue_work(system_wq, &face_panel->work_facerecover_panel);
		break;
	case PS_NOTIFY_ENABLE:
		DRM_DEBUG("LCD: Received ENABLE event\n");
		face_panel->tp_ps_enabled = true;
		break;
	case PS_NOTIFY_DISABLE:
		DRM_DEBUG("LCD: Received ENABLE event\n");
		face_panel->tp_ps_enabled = false;
		break;
	default:
		DRM_DEBUG("LCD: Unknown TP Event\n");
		break;
	}

	return NOTIFY_OK;
}

#endif

/* based on of_node_put */
static void spacemit_of_node_put(struct device_node *node)
{
	if (node)
		kobject_put(&node->kobj);
}

/* based on of_find_backlight */
static struct backlight_device *spacemit_of_find_backlight(struct device *dev)
{
	struct backlight_device *bd = NULL;
	struct device_node *np;

	if (!dev)
		return NULL;

	if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
		np = of_parse_phandle(dev->of_node, "backlight", 0);
		if (np) {
			bd = of_find_backlight_by_node(np);
			spacemit_of_node_put(np);
			if (!bd)
				return ERR_PTR(-EPROBE_DEFER);
		}
	}

	return bd;
}

/* based on __devm_add_action_or_reset */
static inline int spacemit__devm_add_action_or_reset(struct device *dev, void (*action)(void *),
				  void *data, const char *name)
{
	int ret;

	ret = __devm_add_action(dev, action, data, name);
	if (ret)
		action(data);

	return ret;
}
#define spacemit_devm_add_action_or_reset(release, action, data) \
	spacemit__devm_add_action_or_reset(release, action, data, #action)

/* based on devm_backlight_release */
static void spacemit_devm_backlight_release(void *data)
{
	struct backlight_device *bd = data;

	put_device(&bd->dev);
}


/* based on devm_of_find_backlight */
static struct backlight_device *spacemit_devm_of_find_backlight(struct device *dev)
{
	struct backlight_device *bd;
	int ret;

	bd = spacemit_of_find_backlight(dev);
	if (IS_ERR_OR_NULL(bd))
		return bd;
	ret = spacemit_devm_add_action_or_reset(dev, spacemit_devm_backlight_release, bd);
	if (ret)
		return ERR_PTR(ret);

	return bd;
}

/* based on drm_panel_of_backlight */
static int spacemit_drm_panel_of_backlight(struct drm_panel *panel, struct device *dev)
{
	struct backlight_device *backlight;

	if (!panel || !panel->dev)
		return -EINVAL;

	backlight = spacemit_devm_of_find_backlight(dev);

	if (IS_ERR(backlight))
		return PTR_ERR(backlight);
	panel->backlight = backlight;
	return 0;
}

static int spacemit_panel_probe(struct mipi_dsi_device *slave)
{
	int ret;
	struct spacemit_panel *panel;
	struct device *dev = &slave->dev;
	u32 tmp;

	panel = devm_kzalloc(&slave->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	if (!of_property_read_u32(dev->of_node, "id", &tmp))
		panel->id = tmp;

	panel->vdd_2v8 = devm_regulator_get(&slave->dev, "vdd_2v8");
	if (IS_ERR(panel->vdd_2v8)) {
		DRM_DEBUG("get lcd regulator vdd_2v8 failed\n");
		panel->vdd_2v8 = NULL;
	} else {
		regulator_set_voltage(panel->vdd_2v8, 2800000, 2800000);
		ret = regulator_enable(panel->vdd_2v8);
		if (ret)
			DRM_ERROR("enable lcd regulator vdd_2v8 failed\n");
	}

	panel->vdd_1v8 = devm_regulator_get(&slave->dev, "vdd_1v8");
	if (IS_ERR(panel->vdd_1v8)) {
		DRM_DEBUG("get lcd regulator vdd_1v8 failed\n");
		panel->vdd_1v8 = NULL;
	} else {
		regulator_set_voltage(panel->vdd_1v8, 1800000, 1800000);
		ret = regulator_enable(panel->vdd_1v8);
		if (ret)
			DRM_ERROR("enable lcd regulator vdd_1v8 failed\n");
	}

	panel->vdd_1v2 = devm_regulator_get(&slave->dev, "vdd_1v2");
	if (IS_ERR(panel->vdd_1v2)) {
		DRM_DEBUG("get regulator vdd_1v2 failed\n");
		panel->vdd_1v2 = NULL;
	} else {
		regulator_set_voltage(panel->vdd_1v2, 1200000, 1200000);
		ret = regulator_enable(panel->vdd_1v2);
		if (ret)
			DRM_ERROR("enable lcd regulator vdd_1v2 failed\n");
	}

	ret = of_property_read_u32(dev->of_node, "gpios-reset", &panel->gpio_reset);
	if (ret || !gpio_is_valid(panel->gpio_reset)) {
		dev_err(dev, "Missing dt property: gpios-reset\n");
		// return -EINVAL;
	} else {
		ret = gpio_request(panel->gpio_reset, NULL);
		if (ret) {
			pr_err("gpio_reset request fail\n");
			return ret;
		}
	}

	ret = of_property_read_u32(dev->of_node, "gpios-bl", &panel->gpio_bl);
	if (ret || !gpio_is_valid(panel->gpio_bl)) {
		dev_dbg(dev, "Missing dt property: gpios-bl\n");
		panel->gpio_bl = INVALID_GPIO;
	} else {
		ret = gpio_request(panel->gpio_bl, NULL);
		if (ret) {
			pr_err("gpio_bl request fail\n");
			return ret;
		}
	}

	ret = of_property_read_u32_array(dev->of_node, "gpios-dc", panel->gpio_dc, 2);
	if (ret || !gpio_is_valid(panel->gpio_dc[0]) || !gpio_is_valid(panel->gpio_dc[1])) {
		dev_dbg(dev, "Missing dt property: gpios-dc\n");
		panel->gpio_dc[0] = INVALID_GPIO;
		panel->gpio_dc[1] = INVALID_GPIO;
	} else {
		ret = gpio_request(panel->gpio_dc[0], NULL);
		ret |= gpio_request(panel->gpio_dc[1], NULL);
		if (ret) {
			pr_err("gpio_dc request fail\n");
			return ret;
		}
	}

#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT)  || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)
	INIT_WORK(&panel->work_facereset_panel, spacemit_mipi_wq_facereset_panel);
	INIT_WORK(&panel->work_facerecover_panel, spacemit_mipi_wq_facerecover_panel);
#endif

	if (of_property_read_u32(dev->of_node, "reset-toggle-cnt", &panel->reset_toggle_cnt))
		panel->reset_toggle_cnt = LCD_PANEL_RESET_CNT;

	if (of_property_read_u32(dev->of_node, "delay-after-reset", &panel->delay_after_reset))
		panel->delay_after_reset = LCD_DELAY_AFTER_RESET;

	ret = spacemit_panel_parse_dt(slave->dev.of_node, panel);
	if (ret) {
		DRM_ERROR("parse panel info failed\n");
		return ret;
	}

	if (panel->info.te_esd_en) {
		ret = of_property_read_u32(dev->of_node, "gpios-te", &panel->gpio_te);
		if (ret || !gpio_is_valid(panel->gpio_te)) {
			dev_info(dev, "Not support gpios-te\n");
		} else {
			ret = gpio_request(panel->gpio_te, NULL);
			if (ret) {
				pr_err("gpio_te request fail\n");
				return ret;
			}
			panel->gpio_te_irq = gpio_to_irq(panel->gpio_te);
			if (panel->gpio_te_irq) {
				timer_setup(&panel->te_esd_timer, spacemit_mipi_te_esd_timer_handler, 0);
				INIT_WORK(&panel->work_reset_panel, spacemit_mipi_wq_reset_panel);
				ret = devm_request_threaded_irq(dev, panel->gpio_te_irq, NULL,
								spacemit_mipi_gpio_te_irq_handler,
								IRQF_TRIGGER_RISING|IRQF_ONESHOT,
								"lcd_panel_te", panel);
				if (ret != 0) {
					dev_err(dev, "Failed to request IRQ: %d\n",
							panel->gpio_te_irq);
					return ret;
				}
				disable_irq(panel->gpio_te_irq);
			}
		}
	}

	ret = spacemit_panel_device_create(&slave->dev, panel);
	if (ret) {
		DRM_ERROR("panel device create failed\n");
		return ret;
	}
#if IS_ENABLED(CONFIG_DRM_SPACEMIT_BACKLIGHT)
	if (panel->info.is_oled) {
		ret = spacemit_bl_device_register(&slave->dev,
			panel, &spacemit_panel_bl_ops, panel->info.brightness_max);
		if (ret) {
			DRM_ERROR("panel backlight device register failed\n");
			return ret;
		}
	}
#endif
	panel->base.dev = &panel->dev;
	panel->base.funcs = &spacemit_panel_funcs;
	drm_panel_init(&panel->base, &panel->dev, &spacemit_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&panel->base);
	if (ret || panel->base.backlight == NULL) {
		ret = spacemit_drm_panel_of_backlight(&panel->base, &slave->dev);
		if (ret || panel->base.backlight == NULL) {
			DRM_ERROR("panel device get backlight failed\n");
#ifndef CONFIG_SOC_SPACEMIT_K3_FPGA
				/* not return to support oled backlight */
				if (!panel->info.is_oled)
					return ret;
#endif
		}
	}

	drm_panel_add(&panel->base);

	backlight_enable(panel->base.backlight);

	slave->lanes = panel->info.lanes;
	slave->format = panel->info.format;
	slave->mode_flags = panel->info.mode_flags;

	ret = mipi_dsi_attach(slave);
	if (ret) {
		DRM_ERROR("failed to attach dsi panel to host\n");
		drm_panel_remove(&panel->base);
		return ret;
	}
	panel->slave = slave;

	spacemit_mipi_panel_sysfs_init(&panel->dev);
	mipi_dsi_set_drvdata(slave, panel);

	/*do esd init work*/
	if (panel->info.esd_check_en) {
		INIT_DELAYED_WORK(&panel->esd_work, spacemit_panel_esd_work_func);
		/*
		 * schedule_delayed_work(&panel->esd_work,
				      msecs_to_jiffies(2000));
		panel->esd_work_pending = true;
		*/
	}

	INIT_DELAYED_WORK(&panel->bl_work, spacemit_panel_enable_backlight_work_func);

	atomic_set(&panel->enable_refcnt, 0);
	atomic_set(&panel->prepare_refcnt, 0);

#if IS_ENABLED(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_FACE_DETECT)  || IS_ENABLED(CONFIG_TOUCHSCREEN_SITRONIX_FACE_DETECT)

	face_panel = panel;
	mutex_init(&panel->face_lock);
	vh_lcd_tp_event_handler = lcd_tp_event_handler;
#endif
	DRM_INFO("panel driver probe success\n");

	return 0;
}

static void spacemit_panel_remove(struct mipi_dsi_device *slave)
{
	struct spacemit_panel *panel = NULL;
	int ret;

	DRM_INFO("%s()\n", __func__);

	if (!slave) {
		DRM_ERROR("%s fail\n", __func__);
		return;
	}

	panel = mipi_dsi_get_drvdata(slave);
	if (!panel) {
		DRM_ERROR("null panel\n");
		return;
	}

	backlight_disable(panel->base.backlight);
	spacemit_panel_disable(&panel->base);
	spacemit_panel_unprepare(&panel->base);

	ret = mipi_dsi_detach(slave);
	if (ret < 0)
		DRM_ERROR("failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&panel->base);

}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "spacemit,mipi-panel", },
	{ }
};
MODULE_DEVICE_TABLE(of, panel_of_match);

static struct mipi_dsi_driver spacemit_panel_driver = {
	.driver = {
		.name = "spacemit-mipi-panel-drv",
		.of_match_table = panel_of_match,
	},
	.probe = spacemit_panel_probe,
	.remove = spacemit_panel_remove,
};
module_mipi_dsi_driver(spacemit_panel_driver);

MODULE_DESCRIPTION("Spacemit MIPI Panel Driver");
MODULE_LICENSE("GPL v2");
