// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_of.h>
#include <linux/component.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <video/mipi_display.h>
#include <linux/regmap.h>

#include "spacemit_lib.h"
#include "spacemit_crtc.h"
#include "spacemit_dsi.h"
#include "sysfs/sysfs_display.h"
#include "spacemit_bootloader.h"
#include "spacemit_mipi_panel.h"

LIST_HEAD(dsi_core_head);

static void spacemit_dsi_encoder_enable(struct drm_encoder *encoder)
{
	struct spacemit_dsi *dsi = encoder_to_dsi(encoder);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(encoder->crtc);
	struct spacemit_dsi_device *ctx = &dsi->ctx;
	struct spacemit_dphy *spacemit_dphy = ctx->phy;
	struct spacemit_dphy_ctx *dphy_ctx = &spacemit_dphy->ctx;

	DRM_INFO("%s()\n", __func__);

	if (dsi->panel == NULL)
		return;

	if (spacemit_dpu_logo_booton) {
		dsi->panel->enabled = true;
		dsi->panel->prepared = true;
		dphy_ctx->status = DPHY_STATUS_INIT;
		return;
	}

	if (!dsi->core || !dsi->core->dsi_open) {
		DRM_ERROR("%s(), dsi->core is null!\n", __func__);
		return;
	}

	dsi->core->dsi_open(&dsi->ctx, false);

	if (dsi->panel) {
		drm_panel_prepare(dsi->panel);
		drm_panel_enable(dsi->panel);
	}

	if (dsi->core && dsi->core->dsi_ready_for_datatx)
		dsi->core->dsi_ready_for_datatx(&dsi->ctx);

	a_crtc->is_stopped = false;
	dsi->is_disabled = false;
}

static void spacemit_dsi_encoder_disable(struct drm_encoder *encoder)
{
	struct spacemit_dsi *dsi = encoder_to_dsi(encoder);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(encoder->crtc);
	int ret = 0;

	if (dsi->panel == NULL)
		return;

	mutex_lock(&dsi->disable_lock);
	DRM_INFO("%s()\n", __func__);

	if (dsi->panel && dsi->panel->backlight) {
		ret = backlight_disable(dsi->panel->backlight);
		if (ret < 0)
			DRM_DEV_INFO(dsi->panel->dev, "failed to disable backlight: %d\n", ret);
	}

	a_crtc->is_stopped = true;
	if (!a_crtc->is_offline_mode)
		spacemit_crtc_stop(a_crtc);

	if (dsi->core && dsi->core->dsi_close_datatx)
		dsi->core->dsi_close_datatx(&dsi->ctx);

	if (dsi->panel) {
		drm_panel_disable(dsi->panel);
		drm_panel_unprepare(dsi->panel);
	}

	if (dsi->core && dsi->core->dsi_close)
		dsi->core->dsi_close(&dsi->ctx);
	dsi->is_disabled = true;
	mutex_unlock(&dsi->disable_lock);
}

static void spacemit_dsi_encoder_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adj_mode)
{
	struct spacemit_dsi *dsi = encoder_to_dsi(encoder);

	DRM_INFO("%s()\n", __func__);

	drm_display_mode_to_videomode(mode, &dsi->ctx.vm);
}

static int spacemit_dsi_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	return 0;
}

static const struct drm_encoder_helper_funcs spacemit_encoder_helper_funcs = {
	.atomic_check = spacemit_dsi_encoder_atomic_check,
	.mode_set = spacemit_dsi_encoder_mode_set,
	.enable = spacemit_dsi_encoder_enable,
	.disable = spacemit_dsi_encoder_disable,
};

static const struct drm_encoder_funcs spacemit_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int spacemit_dsi_encoder_init(struct drm_device *drm, struct spacemit_dsi *dsi)
{
	struct drm_encoder *encoder = &dsi->encoder;
	struct device *dev = dsi->host.dev;
	u32 crtc_mask;
	int ret;

	DRM_DEBUG("%s()\n", __func__);

	crtc_mask = drm_of_find_possible_crtcs(drm, dev->of_node);
	if (!crtc_mask) {
		DRM_ERROR("failed to find crtc mask\n");
		return -EINVAL;
	}
	DRM_INFO("find possible crtcs: 0x%08x\n", crtc_mask);

	encoder->possible_crtcs = crtc_mask;
	ret = drm_encoder_init(drm, encoder, &spacemit_encoder_funcs,
			       DRM_MODE_ENCODER_DSI, NULL);
	if (ret) {
		DRM_ERROR("failed to init dsi encoder\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &spacemit_encoder_helper_funcs);

	return 0;
}

static int spacemit_dsi_find_panel(struct spacemit_dsi *dsi)
{
	struct device *dev = dsi->host.dev;
	struct device_node *child, *lcds_node;
	struct drm_panel *panel;

	/* search /lcds child node first */
	lcds_node = of_find_node_by_path("/lcds");
	for_each_child_of_node(lcds_node, child) {
		panel = of_drm_find_panel(child);
		if (!IS_ERR(panel)) {
			dsi->panel = panel;
			of_node_put(child);
			return 0;
		}
	}

	/*
	 * If /lcds child node search failed, we search
	 * the child of dsi host node.
	 */
	for_each_child_of_node(dev->of_node, child) {
		panel = of_drm_find_panel(child);
		if (panel) {
			dsi->panel = panel;
			of_node_put(child);
			return 0;
		}
	}

	DRM_ERROR("of_drm_find_panel() failed\n");
	return -ENODEV;
}

static int spacemit_dsi_phy_attach(struct spacemit_dsi *dsi)
{
	struct device *dev;

	dev = spacemit_disp_pipe_get_output(&dsi->dev);
	if (!dev)
		return -ENODEV;

	dsi->ctx.phy = dev_get_drvdata(dev);
	if (!dsi->ctx.phy) {
		DRM_ERROR("dsi attach phy failed\n");
		return -EINVAL;
	}

	return 0;
}

static int spacemit_dsi_get_dsc_info(struct device_node *lcd_node, struct spacemit_mipi_info *mipi_info)
{
	int ret;
	u32 val;

	mipi_info->dsc_enable = 0;
	ret = of_property_read_u32(lcd_node, "dsc-enable", &val);
	if (ret)
		return 0;
	mipi_info->dsc_enable = val;

	if (!mipi_info->dsc_enable)
		return 0;

	ret = of_property_read_u32(lcd_node, "dsc-bpc", &val);
	if (ret || (val != DSC_BPC_8 && val != DSC_BPC_10)) {
		DRM_ERROR("of get mipi dsc-bpc failed\n");
		return -1;
	}
	mipi_info->dsc_bpc = val;

	ret = of_property_read_u32(lcd_node, "dsc-bpp", &val);
	if (ret || val < DSC_MIN_BPP || val > DSC_MAX_BPP) {
		DRM_ERROR("of get mipi dsc-bpp failed\n");
		return -1;
	}
	mipi_info->dsc_bpp = val;

	return 0;
}

static void spacemit_dsi_get_mipi_info(struct device_node *lcd_node, struct spacemit_mipi_info *mipi_info)
{
	int ret;
	u32 val;

	ret = of_property_read_u32(lcd_node, "height", &val);
	if (ret)
		DRM_ERROR("of get mipi height failed\n");
	else
		mipi_info->height = val;

	ret = of_property_read_u32(lcd_node, "width", &val);
	if (ret)
		DRM_ERROR("of get mipi width failed\n");
	else
		mipi_info->width = val;

	ret = of_property_read_u32(lcd_node, "hfp", &val);
	if (ret)
		DRM_ERROR("of get mipi hfp failed\n");
	else
		mipi_info->hfp = val;

	ret = of_property_read_u32(lcd_node, "hbp", &val);
	if (ret)
		DRM_ERROR("of get mipi hbp failed\n");
	else
		mipi_info->hbp = val;

	ret = of_property_read_u32(lcd_node, "hsync", &val);
	if (ret)
		DRM_ERROR("of get mipi hsync failed\n");
	else
		mipi_info->hsync = val;

	ret = of_property_read_u32(lcd_node, "vfp", &val);
	if (ret)
		DRM_ERROR("of get mipi vfp failed\n");
	else
		mipi_info->vfp = val;

	ret = of_property_read_u32(lcd_node, "vbp", &val);
	if (ret)
		DRM_ERROR("of get mipi vbp failed\n");
	else
		mipi_info->vbp = val;

	ret = of_property_read_u32(lcd_node, "vsync", &val);
	if (ret)
		DRM_ERROR("of get mipi vsync failed\n");
	else
		mipi_info->vsync = val;

	ret = of_property_read_u32(lcd_node, "fps", &val);
	if (ret)
		DRM_ERROR("of get mipi fps failed\n");
	else
		mipi_info->fps = val;

	ret = of_property_read_u32(lcd_node, "work-mode", &val);
	if (ret)
		DRM_ERROR("of get mipi work-mode failed\n");
	else
		mipi_info->work_mode = val;

	ret = of_property_read_u32(lcd_node, "rgb-mode", &val);
	if (ret)
		DRM_ERROR("of get mipi rgb-mode failed\n");
	else
		mipi_info->rgb_mode = val;

	ret = of_property_read_u32(lcd_node, "lane-number", &val);
	if (ret)
		DRM_ERROR("of get mipi lane-number failed\n");
	else
		mipi_info->lane_number = val;

	ret = of_property_read_u32(lcd_node, "phy-freq", &val);
	if (ret)
		DRM_ERROR("of get mipi phy-freq failed\n");
	else
		mipi_info->phy_freq = val;

	ret = of_property_read_u32(lcd_node, "split-enable", &val);
	if (ret)
		DRM_ERROR("of get mipi split-enable failed\n");
	else
		mipi_info->split_enable = val;

	ret = of_property_read_u32(lcd_node, "eotp-enable", &val);
	if (ret)
		DRM_ERROR("of get mipi eotp-enable failed\n");
	else
		mipi_info->eotp_enable = val;

	ret = of_property_read_u32(lcd_node, "burst-mode", &val);
	if (ret)
		DRM_ERROR("of get mipi burst-mode failed\n");
	else
		mipi_info->burst_mode = val;

	if (mipi_info->work_mode == SPACEMIT_DSI_MODE_CMD) {
		ret = of_property_read_u32(lcd_node, "ctrl-flag", &val);
		if (ret)
			mipi_info->cmd_pkg_ctrl = -1;
		else
			mipi_info->cmd_pkg_ctrl = val;
	}

	ret = of_property_read_u32(lcd_node, "vpn-tx-dly-cnt", &val);
	if (ret) {
		DRM_DEBUG("of get mipi vpn-tx-dly-cnt failed\n");
		mipi_info->vpn_tx_dly_cnt = 0;
	} else
		mipi_info->vpn_tx_dly_cnt = val;

	ret = of_property_read_u32(lcd_node, "vpn-dly-cnt", &val);
	if (ret) {
		DRM_DEBUG("of get mipi vpn-dly-cnt failed\n");
		mipi_info->vpn_dly_cnt = 0;
	} else
		mipi_info->vpn_dly_cnt = val;

	ret = spacemit_dsi_get_dsc_info(lcd_node, mipi_info);
	if (ret)
		mipi_info->dsc_enable = 0;

	ret = of_property_read_u32(lcd_node, "h_extra", &val);
	if (ret)
		mipi_info->h_extra = 0;
	else
		mipi_info->h_extra = val;
}

static void spacemit_dsi_get_phy_clock(struct device_node *np, struct spacemit_dsi *dsi)
{
	struct spacemit_dsi_device *ctx = &dsi->ctx;
	struct spacemit_dphy *spacemit_dphy = ctx->phy;
	struct spacemit_dphy_ctx *dphy_ctx = &spacemit_dphy->ctx;
	int ret = 0;

	if (np) {
		ret = of_property_read_u32(np, "phy-freq", &dphy_ctx->phy_freq);
		if (ret) {
			DRM_ERROR("of get mipi phy-freq failed\n");
			dphy_ctx->phy_freq = DSI_DEFAULT_PHY_FREQ;
		}

		ret = of_property_read_u32(np, "phy-escape-clock", &dphy_ctx->esc_clk);
		if (ret) {
			DRM_ERROR("of get mipi phy-escape-clock failed\n");
			dphy_ctx->esc_clk = DSI_DEFAULT_ESCAPE_FREQ;
		}
	}
}

static void spacemit_dsi_get_advanced_info(struct spacemit_dsi *dsi, struct spacemit_mipi_info *mipi_info)
{
	struct spacemit_dsi_device *ctx = &dsi->ctx;
	struct spacemit_dsi_advanced_setting *adv_setting = &ctx->adv_setting;
	struct device_node *np = dsi->dev.of_node;
	int ret;

	/*advanced_setting*/
	ret = of_property_read_u32(np, "lpm_frame_en", &adv_setting->lpm_frame_en);
	if (ret)
		adv_setting->lpm_frame_en = LPM_FRAME_EN_DEFAULT;

	ret = of_property_read_u32(np, "last_line_turn", &adv_setting->last_line_turn);
	if (ret)
		adv_setting->last_line_turn = LAST_LINE_TURN_DEFAULT;

	ret = of_property_read_u32(np, "hex_slot_en", &adv_setting->hex_slot_en);
	if (ret)
		adv_setting->hex_slot_en = HEX_SLOT_EN_DEFAULT;

	ret = of_property_read_u32(np, "hsa_pkt_en", &adv_setting->hsa_pkt_en);
	if (ret) {
		if (mipi_info->burst_mode == DSI_BURST_MODE_NON_BURST_SYNC_PULSE)
			adv_setting->hsa_pkt_en = HSA_PKT_EN_DEFAULT_SYNC_PULSE;
		else
			adv_setting->hsa_pkt_en = HSA_PKT_EN_DEFAULT_OTHER;
	}

	ret = of_property_read_u32(np, "hse_pkt_en", &adv_setting->hse_pkt_en);
	if (ret) {
		if (mipi_info->burst_mode == DSI_BURST_MODE_NON_BURST_SYNC_PULSE)
			adv_setting->hse_pkt_en = HSE_PKT_EN_DEFAULT_SYNC_PULSE;
		else
			adv_setting->hse_pkt_en = HSE_PKT_EN_DEFAULT_OTHER;
	}

	ret = of_property_read_u32(np, "hbp_pkt_en", &adv_setting->hbp_pkt_en);
	if (ret)
		adv_setting->hbp_pkt_en = HBP_PKT_EN_DEFAULT;

	ret = of_property_read_u32(np, "hfp_pkt_en", &adv_setting->hfp_pkt_en);
	if (ret)
		adv_setting->hfp_pkt_en = HFP_PKT_EN_DEFAULT;

	ret = of_property_read_u32(np, "hex_pkt_en", &adv_setting->hex_pkt_en);
	if (ret)
		adv_setting->hex_pkt_en = HEX_PKT_EN_DEFAULT;

	ret = of_property_read_u32(np, "hlp_pkt_en", &adv_setting->hlp_pkt_en);
	if (ret)
		adv_setting->hlp_pkt_en = HLP_PKT_EN_DEFAULT;

	ret = of_property_read_u32(np, "auto_dly_dis", &adv_setting->auto_dly_dis);
	if (ret)
		adv_setting->auto_dly_dis = AUTO_DLY_DIS_DEFAULT;

	ret = of_property_read_u32(np, "timing_check_dis", &adv_setting->timing_check_dis);
	if (ret)
		adv_setting->timing_check_dis = TIMING_CHECK_DIS_DEFAULT;

	ret = of_property_read_u32(np, "hact_wc_en", &adv_setting->hact_wc_en);
	if (ret)
		adv_setting->hact_wc_en = HACT_WC_EN_DEFAULT;

	ret = of_property_read_u32(np, "auto_wc_dis", &adv_setting->auto_wc_dis);
	if (ret)
		adv_setting->auto_wc_dis = AUTO_WC_DIS_DEFAULT;

	ret = of_property_read_u32(np, "vsync_rst_en", &adv_setting->vsync_rst_en);
	if (ret)
		adv_setting->vsync_rst_en = VSYNC_RST_EN_DEFAULT;
}


static int create_panel_name_blob(struct drm_connector *connector)
{
	struct drm_property_blob *blob;
	struct spacemit_dsi *dsi = connector_to_dsi(connector);
	struct drm_property *prop;
	char *blob_data;
	size_t blob_size = 0;
	struct device_node *node;
	int ret = 0;
	char *default_panel = "default_panel";

	node = of_find_node_by_name(NULL, "panel0");
	if (node) {
		ret = of_property_read_string(node, "force-attached", &dsi->panel_name);
		if (ret)
			DRM_ERROR("bad panel node\n");
		of_node_put(node);
	}

	prop = drm_property_create(connector->dev,
			DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_BLOB,
			"panel_name", 0);
	if (!prop)
		return -ENOMEM;

	if (!dsi->panel_name) {
		DRM_ERROR("dsi->panel_name null\n");
		memcpy((void *)dsi->panel_name, default_panel, strlen(dsi->panel_name));
	}

	blob_size = strlen(dsi->panel_name);
	blob = drm_property_create_blob(connector->dev, blob_size, NULL);
	if (IS_ERR(blob))
		return -1;

	blob_data = blob->data;
	memcpy(blob_data, dsi->panel_name, blob_size);

	drm_object_attach_property(&connector->base, prop, blob->base.id);
	dsi->panel_name_prop = prop;

	return 0;
}

static int spacemit_dsi_connector_get_modes(struct drm_connector *connector)
{
	struct spacemit_dsi *dsi = connector_to_dsi(connector);

	DRM_DEBUG("%s()\n", __func__);

	return drm_panel_get_modes(dsi->panel, connector);
}

static enum drm_mode_status
spacemit_dsi_connector_mode_valid(struct drm_connector *connector,
			 const struct drm_display_mode *mode)
{
	DRM_DEBUG("%s()\n", __func__);

	return MODE_OK;
}

static struct drm_encoder *
spacemit_dsi_connector_best_encoder(struct drm_connector *connector)
{
	struct spacemit_dsi *dsi = connector_to_dsi(connector);

	DRM_DEBUG("%s()\n", __func__);
	return &dsi->encoder;
}

static struct drm_connector_helper_funcs spacemit_dsi_connector_helper_funcs = {
	.get_modes = spacemit_dsi_connector_get_modes,
	.mode_valid = spacemit_dsi_connector_mode_valid,
	.best_encoder = spacemit_dsi_connector_best_encoder,
};

static enum drm_connector_status
spacemit_dsi_connector_detect(struct drm_connector *connector, bool force)
{
	struct spacemit_dsi *dsi = connector_to_dsi(connector);

	DRM_DEBUG("%s() dsi subconnector type %d status %d\n", __func__, dsi->ctx.dsi_subconnector, dsi->ctx.connector_status);

	if (dsi->ctx.dsi_subconnector == SPACEMIT_DSI_SUBCONNECTOR_DP)
		return dsi->ctx.connector_status;
	else
		return connector_status_connected;
}

static void spacemit_dsi_connector_destroy(struct drm_connector *connector)
{
	DRM_INFO("%s()\n", __func__);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int spacemit_conn_atomic_get_property(struct drm_connector *connector,
				   const struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t *val)
{
	struct spacemit_dsi *dsi = connector_to_dsi(connector);

	if (property == dsi->panel_name_prop)
		*val = dsi->panel_name_prop->base.id;

	return 0;
}

static const struct drm_connector_funcs spacemit_dsi_atomic_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = spacemit_dsi_connector_detect,
	.destroy = spacemit_dsi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_get_property = spacemit_conn_atomic_get_property,
};

static int spacemit_dsi_connector_init(struct drm_device *drm, struct spacemit_dsi *dsi)
{
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_connector *connector = &dsi->connector;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(drm, connector,
				 &spacemit_dsi_atomic_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		DRM_ERROR("drm_connector_init() failed\n");
		return ret;
	}

	drm_connector_helper_add(connector,
				 &spacemit_dsi_connector_helper_funcs);

	//drm_mode_connector_attach_encoder(connector, encoder);
	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static int __maybe_unused spacemit_dsi_bridge_attach(struct spacemit_dsi *dsi)
{
	struct drm_encoder *encoder = &dsi->encoder;
	struct drm_bridge *bridge = dsi->bridge;
	struct device *dev = dsi->host.dev;
	struct device_node *bridge_node;
	int ret;

	bridge_node = of_graph_get_remote_node(dev->of_node, 2, 0);
	if (!bridge_node)
		return 0;

	bridge = of_drm_find_bridge(bridge_node);
	if (!bridge) {
		DRM_ERROR("of_drm_find_bridge() failed\n");
		return -ENODEV;
	}
	dsi->bridge = bridge;

	ret = drm_bridge_attach(encoder, bridge, NULL, 0);
	if (ret) {
		DRM_ERROR("failed to attach external bridge\n");
		return ret;
	}

	return 0;
}

static irqreturn_t spacemit_dsi_isr(int irq, void *data)
{
	struct spacemit_dsi *dsi = data;

	DRM_DEBUG("%s: dsi Enter\n", __func__);

	if (dsi->core && dsi->core->isr)
		dsi->core->isr(&dsi->ctx);

	return IRQ_HANDLED;
}

static int spacemit_dsi_irq_request(struct spacemit_dsi *dsi)
{
	int ret;
	int irq;

	irq = irq_of_parse_and_map(dsi->host.dev->of_node, 0);
	if (irq) {
		DRM_DEBUG("dsi irq num = %d\n", irq);
		ret = request_irq(irq, spacemit_dsi_isr, 0, "DSI", dsi);
		if (ret) {
			DRM_ERROR("dsi failed to request irq int0!\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int spacemit_dsi_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct spacemit_dsi *dsi = dev_get_drvdata(dev);
	struct drm_connector *connector = &dsi->connector;
	int ret;

	DRM_DEBUG("%s()\n", __func__);

	ret = spacemit_dsi_encoder_init(drm, dsi);
	if (ret)
		goto cleanup_host;

	ret = spacemit_dsi_connector_init(drm, dsi);
	if (ret)
		goto cleanup_encoder;

	ret = spacemit_dsi_irq_request(dsi);
	if (ret)
		goto cleanup_connector;

	dsi->is_disabled = false;

	create_panel_name_blob(connector);

	return 0;

cleanup_connector:
	drm_connector_cleanup(&dsi->connector);
cleanup_encoder:
	drm_encoder_cleanup(&dsi->encoder);
cleanup_host:
	mipi_dsi_host_unregister(&dsi->host);
	return ret;
}

static void spacemit_dsi_unbind(struct device *dev,
			struct device *master, void *data)
{
	/* do nothing */
	DRM_INFO("%s()\n", __func__);

}

static const struct component_ops dsi_component_ops = {
	.bind	= spacemit_dsi_bind,
	.unbind = spacemit_dsi_unbind,
};

static int spacemit_dsi_host_attach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *slave)
{
	struct spacemit_dsi *dsi = host_to_dsi(host);
	struct spacemit_dsi_device *ctx = &dsi->ctx;
	struct spacemit_mipi_info *mipi_info = &ctx->mipi_info;
	struct device_node *lcd_node;
	int ret;

	DRM_INFO("%s()\n", __func__);

	dsi->slave = slave;

	ret = spacemit_dsi_phy_attach(dsi);
	if (ret)
		return ret;

	ret = spacemit_dsi_find_panel(dsi);
	if (ret)
		return ret;

	lcd_node = dsi->panel->dev->of_node;

	spacemit_dsi_get_mipi_info(lcd_node, mipi_info);
	spacemit_dsi_get_advanced_info(dsi, mipi_info);
	spacemit_dsi_get_phy_clock(lcd_node, dsi);

	ret = component_add(host->dev, &dsi_component_ops);
	if (ret)
		return ret;
	return 0;
}

static int spacemit_dsi_host_detach(struct mipi_dsi_host *host,
			   struct mipi_dsi_device *slave)
{
	DRM_INFO("%s()\n", __func__);
	component_del(host->dev, &dsi_component_ops);
	return 0;
}

static ssize_t spacemit_dsi_host_transfer(struct mipi_dsi_host *host,
				const struct mipi_dsi_msg *msg)
{
	struct spacemit_dsi *dsi = host_to_dsi(host);
	struct spacemit_dsi_cmd_desc cmd;
	struct spacemit_dsi_rx_buf dbuf = {0x0};

	cmd.cmd_type = msg->type;
	cmd.length = msg->tx_len;
	memcpy(cmd.data, msg->tx_buf, cmd.length);

	if (msg->flags & MIPI_DSI_MSG_USE_LPM)
		cmd.lp = 1;
	else
		cmd.lp = 0;

	if (msg->rx_buf && msg->rx_len) {
		if (dsi->core && dsi->core->dsi_read_cmds)
			dsi->core->dsi_read_cmds(&dsi->ctx, &dbuf, &cmd, 1);
		memcpy(msg->rx_buf, dbuf.data, 1);
		return 0;
	}

	if (msg->tx_buf && msg->tx_len) {
		if (dsi->core && dsi->core->dsi_write_cmds)
			dsi->core->dsi_write_cmds(&dsi->ctx, &cmd, 1);
	}

	return 0;
}

static const struct mipi_dsi_host_ops spacemit_dsi_host_ops = {
	.attach = spacemit_dsi_host_attach,
	.detach = spacemit_dsi_host_detach,
	.transfer = spacemit_dsi_host_transfer,
};

static int spacemit_dsi_host_init(struct device *dev, struct spacemit_dsi *dsi)
{
	int ret;

	dsi->host.dev = dev;
	dsi->host.ops = &spacemit_dsi_host_ops;

	ret = mipi_dsi_host_register(&dsi->host);
	if (ret)
		DRM_ERROR("failed to register dsi host\n");

	return ret;
}

static int spacemit_dsi_device_create(struct spacemit_dsi *dsi, struct device *parent)
{
	int ret;

	dsi->dev.class = display_class;
	dsi->dev.parent = parent;
	dsi->dev.of_node = parent->of_node;
	dev_set_name(&dsi->dev, "dsi%d", dsi->ctx.id);
	dev_set_drvdata(&dsi->dev, dsi);

	ret = device_register(&dsi->dev);
	if (ret)
		DRM_ERROR("dsi device register failed\n");

	return ret;
}

static int spacemit_dsi_context_init(struct spacemit_dsi *dsi, struct device_node *np)
{
	struct spacemit_dsi_device *ctx = &dsi->ctx;
	struct resource r;
	u32 tmp;

	if (dsi->core && dsi->core->parse_dt)
		dsi->core->parse_dt(&dsi->ctx, np);

	if (of_address_to_resource(np, 0, &r)) {
		DRM_ERROR("parse dsi ctrl reg base failed\n");
		return -ENODEV;
	}

	ctx->base_addr_dsi0 = (void __iomem *)ioremap(r.start, resource_size(&r));
	if (ctx->base_addr_dsi0 == NULL) {
		DRM_ERROR("dsi0 ctrl reg base ioremap failed\n");
		return -ENODEV;
	}
	ctx->base_addr = ctx->base_addr_dsi0;

	ctx->base_addr_dsi1 = (void __iomem *)ioremap(DSI1_BASE_ADDR, 200);
	if (ctx->base_addr_dsi1 == NULL) {
		DRM_ERROR("dsi1 ctrl reg base ioremap failed\n");
	}

	ctx->dsi_subconnector = SPACEMIT_DSI_SUBCONNECTOR_MIPI_DSI;
	ctx->previous_connector_status = connector_status_connected;
	ctx->connector_status = connector_status_connected;

	if (!of_property_read_u32(np, "dev-id", &tmp))
		ctx->id = tmp;

	ctx->version = DSI_VERSION_2;
	if (!of_property_read_u32(np, "dsi-version", &tmp))
		ctx->version = tmp;

	// if (ctx->version >= DSI_VERSION_2) {
	//	ctx->apmu_base = spacemit_syscon_regmap_lookup_by_phandle("apmu-phandle");
	//	if (IS_ERR(ctx->apmu_base))
	//	{
	//		DRM_ERROR("BUG: failed to get apmu base\n");
	//		return -ENODEV;
	//	}
	// }

	return 0;
}

static int spacemit_dsi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spacemit_dsi *dsi;
	const char *str;
	int ret;
	DRM_INFO("%s()\n", __func__);
	dsi = devm_kzalloc(&pdev->dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi) {
		DRM_ERROR("failed to allocate dsi data.\n");
		return -ENOMEM;
	}

	if (!of_property_read_string(np, "ip", &str))
		dsi->core = dsi_core_ops_attach(str);
	else
		DRM_WARN("error: 'ip' was not found\n");

	ret = spacemit_dsi_context_init(dsi, np);
	if (ret)
		return -EINVAL;

	spacemit_dsi_device_create(dsi, &pdev->dev);
	spacemit_dsi_sysfs_init(&dsi->dev);
	platform_set_drvdata(pdev, dsi);

	ret = spacemit_dsi_host_init(&pdev->dev, dsi);
	if (ret)
		return ret;

	mutex_init(&dsi->disable_lock);

	return 0;
}

static void spacemit_dsi_remove(struct platform_device *pdev)
{
}

static const struct of_device_id spacemit_dsi_of_match[] = {
	{ .compatible = "spacemit,dsi-host" },
	{ }
};
MODULE_DEVICE_TABLE(of, spacemit_dsi_of_match);

struct platform_driver spacemit_dsi_driver = {
	.probe = spacemit_dsi_probe,
	.remove = spacemit_dsi_remove,
	.driver = {
		.name = "spacemit-dsi-drv",
		.of_match_table = spacemit_dsi_of_match,
	},
};

MODULE_DESCRIPTION("Spacemit MIPI DSI HOST Controller Driver");
MODULE_LICENSE("GPL v2");
