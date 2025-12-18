// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_writeback.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_of.h>
#include <drm/drm_edid.h>
#include <linux/component.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include "spacemit_lib.h"
#include "spacemit_wb.h"
//#include "spacemit_drm.h"
#include "spacemit_dmmu.h"
#include "sysfs/sysfs_display.h"
#include "dpu/dpu_saturn.h"
#include "dpu/saturn_regs/wb.h"

static const u32 spacemit_wb_formats[] = {
	DRM_FORMAT_XRGB8888,
};

int spacemit_wb_get_afbc_header_size(struct spacemit_wb *wb, struct drm_framebuffer *fb, int width, int height)
{
	int raw_width = 0;
	int raw_heght = 0;
	int fbc_out_width = 0;
	int fbc_out_height = 0;
	int w_block_num = 0;
	int h_block_num = 0;
	int fbc_block_size = 0;
	int header_size = 0;
	int payload_size = 0;

	int bpp = 0;
	int divide = 1;

	if (fb->format->format == DRM_FORMAT_YUV420_8BIT) {
		DRM_DEBUG("fb->format: DRM_FORMAT_YU08\n\r");
		bpp = 3;
		divide = 2;
	} else if (fb->format->format == DRM_FORMAT_YUV420_10BIT) {
		DRM_DEBUG("fb->format: DRM_FORMAT_YUV420_10BIT\n\r");
		bpp = 3;
	} else if (fb->format->format == DRM_FORMAT_ABGR8888) {
		DRM_DEBUG("fb->format: DRM_FORMAT_ABGR8888\n\r");
		bpp = 4;
	} else if (fb->format->format == DRM_FORMAT_BGR888) {
		DRM_DEBUG("fb->format: DRM_FORMAT_BGR8888\n\r");
		bpp = 3;
	} else if (fb->format->format == DRM_FORMAT_BGR565) {
		DRM_DEBUG("fb->format: DRM_FORMAT_BGR565\n\r");
		bpp = 2;
	}

	wb->afbc_state = kzalloc(sizeof(struct spacemit_afbc_state), GFP_KERNEL);
	if (!wb->afbc_state) {
		DRM_ERROR("Faild to set wb afbc\n");
		return 0;
	}
	spacemit_get_afbc_modifier(fb->modifier, wb->afbc_state);

	raw_width = width;
	raw_heght = height;
	if (wb->afbc_state->block_size == 1) {
		if (wb->afbc_state->tile_type) {
			fbc_out_width = ((raw_width + 255) >> 8) << 8;
			fbc_out_height = ((raw_heght + 63) >> 6) << 6;
		} else {
			fbc_out_width = ((raw_width + 31) >> 5) << 5;
			fbc_out_height = ((raw_heght + 7) >> 3) << 3;
		}
		w_block_num = fbc_out_width >> 5;
		h_block_num = fbc_out_height >> 3;
	} else {
		if (wb->afbc_state->tile_type) {
			fbc_out_width = ((raw_width + 127) >> 7) << 7;
			fbc_out_height = ((raw_heght + 127) >> 7) << 7;
		} else {
			fbc_out_width = ((raw_width + 15) >> 4) << 4;
			fbc_out_height = ((raw_heght + 15) >> 4) << 4;
		}
		w_block_num = fbc_out_width >> 4;
		h_block_num = fbc_out_height >> 4;
	}
	kfree(wb->afbc_state);

	fbc_block_size = w_block_num * h_block_num;
	header_size = fbc_block_size * 16;// each block 16 byte header
	payload_size = w_block_num * h_block_num * 256 * bpp/divide;
	if (fbc_block_size == 0 || payload_size == 0) {
		DRM_ERROR("Failed to cal afbc head/payload size, w_block_num = %d, h_block_num = %d, bpp = %d, divide = %d, payload_size = %d\n",
				w_block_num, h_block_num, bpp, divide, payload_size);
	}
	return header_size;
}

static const struct spacemit_format_fb2wb spacemit_wb_format_fb2wb[] = {
	{ DRM_FORMAT_ABGR2101010, SPACEMIT_WB_FORMAT_ABGR2101010},
	{ DRM_FORMAT_ARGB8888, SPACEMIT_WB_FORMAT_ARGB8888},
	{ DRM_FORMAT_ABGR8888, SPACEMIT_WB_FORMAT_ABGR8888},
	{ DRM_FORMAT_RGBA8888, SPACEMIT_WB_FORMAT_RGBA8888},
	{ DRM_FORMAT_BGRA8888, SPACEMIT_WB_FORMAT_BGRA8888},
	{ DRM_FORMAT_XRGB8888, SPACEMIT_WB_FORMAT_XRGB8888},
	{ DRM_FORMAT_XBGR8888, SPACEMIT_WB_FORMAT_XBGR8888},
	{ DRM_FORMAT_RGBX8888, SPACEMIT_WB_FORMAT_RGBX8888},
	{ DRM_FORMAT_BGRX8888, SPACEMIT_WB_FORMAT_BGRX8888},
	{ DRM_FORMAT_RGB565, SPACEMIT_WB_FORMAT_RGB565},
	{ DRM_FORMAT_BGR565, SPACEMIT_WB_FORMAT_BGR565},
	{ DRM_FORMAT_RGB888, SPACEMIT_WB_FORMAT_RGB888},
	{ DRM_FORMAT_BGR888, SPACEMIT_WB_FORMAT_BGR888},
	{ DRM_FORMAT_YUV420_8BIT, SPACEMIT_WB_FORMAT_YUV420_P2_8_VU},
	{ DRM_FORMAT_NV12, SPACEMIT_WB_FORMAT_YUV420_P2_8_VU},
	{ DRM_FORMAT_P010, SPACEMIT_WB_FORMAT_YUV420_P2_10_VU},
};

static int spacemit_wb_get_format(u32 format)
{
	unsigned int i = 0;

	for (i = 0; i < ARRAY_SIZE(spacemit_wb_format_fb2wb); i++) {
		if (format == spacemit_wb_format_fb2wb[i].format)
			return spacemit_wb_format_fb2wb[i].wb_id;
	}

	DRM_ERROR("format 0x%x is not supported in wb!\n", format);
	return SPACEMIT_WB_INVALID_FORMAT_ID;
}

static void spacemit_wb_connector_atomic_commit(struct drm_connector *conn, struct drm_atomic_state *state)
{
	struct drm_device *drm = conn->dev;
	struct spacemit_drm_private *priv = drm->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct drm_writeback_connector *wb_conn;
	struct drm_framebuffer *fb;
	struct spacemit_wb *wb = container_of(conn, struct spacemit_wb, wb_connector.base);
	u8 tbu_id = hwdev->rdma_nums * 2 + wb->ctx.id;
	struct drm_connector_state *conn_state = NULL;

	struct cmdlist_regs *cl_wb;
	cmdlist_mode_type_t cl_mode_type;
	int ret;

	struct drm_crtc *crtc = NULL;
	struct spacemit_crtc *a_crtc = NULL;
	struct drm_crtc_state *crtc_state;
	struct spacemit_crtc_state *spacemit_crtc_state;
	struct cmdlist *cl = NULL;
	int i = 0;

	cl_wb = NULL;
	cl_mode_type = CMDLIST_MOD_WB0;
	ret = 0;

	wb_conn = priv->wb_connector[wb->ctx.id];
	conn_state = wb_conn->base.state;
	if (WARN_ON(!conn_state->writeback_job))
		return;
	cl_wb = alloc_cmdlist_regs(WB_REG);

	crtc = conn_state->crtc;
	a_crtc = to_spacemit_crtc(crtc);

	crtc_state = a_crtc->crtc.state;
	spacemit_crtc_state = to_spacemit_crtc_state(crtc_state);

	fb = conn_state->writeback_job->fb;
	DRM_DEBUG("WB: %s w %d, h %d, stride %d", __func__, fb->width, fb->height, fb->pitches[0]);
	wb->format = spacemit_wb_get_format(fb->format->format);
	/* init mmu_tbl */
	spacemit_crtc_state->wb_mmu_tbl.size = ((PAGE_ALIGN(fb->obj[0]->size) >> PAGE_SHIFT) +
			HW_ALIGN_TTB_NUM) * 4;
	spacemit_crtc_state->wb_mmu_tbl.va = dma_alloc_coherent(a_crtc->dev, spacemit_crtc_state->wb_mmu_tbl.size,
			&spacemit_crtc_state->wb_mmu_tbl.pa, GFP_KERNEL | __GFP_ZERO);
	if (spacemit_crtc_state->wb_mmu_tbl.va == NULL) {
		DRM_ERROR("Failed to allocate %d bytes for wb mmu table\n", spacemit_crtc_state->wb_mmu_tbl.size);
		free_cmdlist_regs(cl_wb);
		return;
	}

	wb_conn = priv->wb_connector[wb->ctx.id];
	drm_writeback_queue_job(wb_conn, wb_conn->base.state);

	wb->a_crtc = a_crtc;
	wb->cl_wb = cl_wb;
	a_crtc->wb_id = wb->ctx.id;
	if (a_crtc->wb_id == 1)
		cl_mode_type = CMDLIST_MOD_WB1;

	ret = spacemit_dmmu_map(fb, &spacemit_crtc_state->wb_mmu_tbl, tbu_id, wb, NULL, NULL);
	if (ret) {
		DRM_ERROR("%s failed to map wb with ret = %d\n", __func__, ret);
		free_cmdlist_regs(cl_wb);
		if (spacemit_crtc_state->wb_mmu_tbl.va)
			dma_free_coherent(a_crtc->dev, spacemit_crtc_state->wb_mmu_tbl.size,
				spacemit_crtc_state->wb_mmu_tbl.va, spacemit_crtc_state->wb_mmu_tbl.pa);
		return;
	}

	if (a_crtc->is_slice_mode) {
		for (i = 0; i < a_crtc->slice_num; i++) {
			hwdev->wb_config(a_crtc, wb, fb, wb->cl_wb, i);
			cmdlist_regs_packing(crtc_to_cl(crtc), cl_mode_type, cl_wb);
			cl = &spacemit_crtc_state->cl[spacemit_crtc_state->cur_cl];
			cl->rch_start_cmps_y = 0xDEADBEEF;
			cl->cmdlist_ch_y_other = a_crtc->slice_wb[0].crtc_h * (i + 1);
			spacemit_crtc_state->cur_cl++;
		}
	} else {
		hwdev->wb_config(a_crtc, wb, fb, wb->cl_wb, i);
		cmdlist_regs_packing(crtc_to_cl(crtc), cl_mode_type, cl_wb);
		cl = &spacemit_crtc_state->cl[spacemit_crtc_state->cur_cl];
		cl->rch_start_cmps_y = 0xDEADBEEF;
		cl->cmdlist_ch_y_other = 0xDEADBEEF;
		spacemit_crtc_state->cur_cl++;
	}


	free_cmdlist_regs(cl_wb);
}

static int spacemit_wb_encoder_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	DRM_DEBUG("%s()\n", __func__);

	return 0;
}

static const struct drm_encoder_helper_funcs spacemit_wb_encoder_helper_funcs = {
	.atomic_check = spacemit_wb_encoder_atomic_check,
};

static int spacemit_wb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	int cnt = 0;
	struct drm_display_mode *mode_dynamic;
	struct drm_display_mode mode = {
			DRM_MODE("256x600", DRM_MODE_TYPE_DRIVER, 20276, 256, 360,
			364, 474, 0, 600, 650, 654, 704, 0,
			DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC) };

	DRM_DEBUG("%s()\n", __func__);
	cnt = drm_add_modes_noedid(connector, dev->mode_config.max_width,
				    dev->mode_config.max_height);

	mode_dynamic = drm_mode_duplicate(connector->dev, &mode);
	if (!mode_dynamic) {
		DRM_ERROR("allocatre mode_dynamic failed\n");
		return cnt;
	}

	drm_mode_set_name(mode_dynamic);

	drm_mode_probed_add(connector, mode_dynamic);
	cnt++;

	return cnt;
}

static enum drm_mode_status
spacemit_wb_connector_mode_valid(struct drm_connector *connector,
			 struct drm_display_mode *mode)
{
	enum drm_mode_status mode_status = MODE_OK;

	DRM_DEBUG("%s(%s)\n", __func__, mode->name);

	return mode_status;
}

static enum drm_connector_status
spacemit_wb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void spacemit_wb_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_helper_funcs spacemit_wb_connector_helper_funcs = {
	.get_modes = spacemit_wb_connector_get_modes,
	.mode_valid = spacemit_wb_connector_mode_valid,
	.atomic_commit = spacemit_wb_connector_atomic_commit,
};

static int spacemit_wb_atomic_get_property(struct drm_connector *connector,
				   const struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t *val)
{
	struct spacemit_wb *wb = container_of(connector, struct spacemit_wb, wb_connector.base);

	DRM_DEBUG("%s() name = %s\n", __func__, property->name);

	if (property == wb->rotation_property) {
		*val = wb->rotation;
	} else if (property == wb->in_x_property) {
		*val = wb->in_x;
	} else if (property == wb->in_y_property) {
		*val = wb->in_y;
	} else if (property == wb->in_w_property) {
		*val = wb->in_w;
	} else if (property == wb->in_h_property) {
		*val = wb->in_h;
	} else if (property == wb->out_x_property) {
		*val = wb->out_x;
	} else if (property == wb->out_y_property) {
		*val = wb->out_y;
	} else if (property == wb->out_w_property) {
		*val = wb->out_w;
	} else if (property == wb->out_h_property) {
		*val = wb->out_h;
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static int spacemit_wb_atomic_set_property(struct drm_connector *connector,
				   struct drm_connector_state *state,
				   struct drm_property *property,
				   uint64_t val)
{
	struct spacemit_wb *wb = container_of(connector, struct spacemit_wb, wb_connector.base);

	DRM_DEBUG("%s() name = %s, val = %llu\n",
		__func__, property->name, val);

	if (property == wb->rotation_property) {
		if (!is_power_of_2(val & DRM_MODE_ROTATE_MASK)) {
			DRM_DEBUG_ATOMIC("[WB:%d] bad rotation bitmask: 0x%llx\n",
					wb->ctx.id, val);
			return -EINVAL;
		}
		wb->rotation = val;
	} else if (property == wb->in_x_property) {
		wb->in_x = val;
	} else if (property == wb->in_y_property) {
		wb->in_y = val;
	} else if (property == wb->in_w_property) {
		wb->in_w = val;
	} else if (property == wb->in_h_property) {
		wb->in_h = val;
	} else if (property == wb->out_x_property) {
		wb->out_x = val;
	} else if (property == wb->out_y_property) {
		wb->out_y = val;
	} else if (property == wb->out_w_property) {
		wb->out_w = val;
	} else if (property == wb->out_h_property) {
		wb->out_h = val;
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_connector_funcs spacemit_wb_connector_funcs = {
	.atomic_set_property = spacemit_wb_atomic_set_property,
	.atomic_get_property = spacemit_wb_atomic_get_property,
	.reset = drm_atomic_helper_connector_reset,
	.detect = spacemit_wb_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = spacemit_wb_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int spacemit_wb_device_create(struct spacemit_wb *wb, struct device *parent)
{
	int ret;

	wb->dev.class = display_class;
	wb->dev.parent = parent;
	wb->dev.of_node = parent->of_node;
	dev_set_name(&wb->dev, "wb%d", wb->ctx.id);
	dev_set_drvdata(&wb->dev, wb);

	ret = device_register(&wb->dev);
	if (ret)
		DRM_ERROR("wb device register failed\n");

	return ret;
}

static int spacemit_wb_context_init(struct spacemit_wb *wb, struct device_node *np)
{
	struct spacemit_wb_device *ctx = &wb->ctx;
	u32 tmp;
	u32 crtc_mask = drm_of_find_possible_crtcs(wb->ddev, np);

	if (!crtc_mask) {
		DRM_ERROR("failed to find crtc mask\n");
		return -EINVAL;
	}
	DRM_INFO("find possible crtcs: 0x%08x\n", crtc_mask);

	wb->wb_connector.encoder.possible_crtcs = crtc_mask;

	if (!of_property_read_u32(np, "dev-id", &tmp))
		ctx->id = tmp;

	if (ctx->id >= N_WRITEBACK_MAX) {
		DRM_ERROR("Current id = %d, Max id = %d\n", ctx->id, N_WRITEBACK_MAX - 1);
		return -EINVAL;
	}

	return 0;
}

static int spacemit_wb_create_properties(struct drm_connector *conn)
{
	struct drm_property *prop;
	struct drm_device *drm = conn->dev;
	struct spacemit_wb *wb = container_of(conn, struct spacemit_wb, wb_connector.base);

	static const struct drm_prop_enum_list props[] = {
		{ __builtin_ffs(DRM_MODE_ROTATE_0) - 1,   "rotate-0" },
		{ __builtin_ffs(DRM_MODE_ROTATE_90) - 1,  "rotate-90" },
		{ __builtin_ffs(DRM_MODE_ROTATE_180) - 1, "rotate-180" },
		{ __builtin_ffs(DRM_MODE_ROTATE_270) - 1, "rotate-270" },
	};
	unsigned int rotation = DRM_MODE_ROTATE_0;
	unsigned int supported_rotations = DRM_MODE_ROTATE_MASK;

	DRM_DEBUG("%s()\n", __func__);
	WARN_ON((supported_rotations & DRM_MODE_ROTATE_MASK) == 0);
	WARN_ON(!is_power_of_2(rotation & DRM_MODE_ROTATE_MASK));
	WARN_ON(rotation & ~supported_rotations);

	prop = drm_property_create_bitmask(drm, 0, "wb_rotation",
					props, ARRAY_SIZE(props),

					supported_rotations);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&conn->base, prop, rotation);
	wb->rotation_property = prop;
	wb->rotation = rotation;

	prop = drm_property_create_range(drm, DRM_MODE_PROP_ATOMIC,
			"WB_IN_X", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&conn->base, prop, 0);
	wb->in_x_property = prop;

	prop = drm_property_create_range(drm, DRM_MODE_PROP_ATOMIC,
			"WB_IN_Y", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&conn->base, prop, 0);
	wb->in_y_property = prop;

	prop = drm_property_create_range(drm, DRM_MODE_PROP_ATOMIC,
			"WB_IN_W", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&conn->base, prop, 0);
	wb->in_w_property = prop;

	prop = drm_property_create_range(drm, DRM_MODE_PROP_ATOMIC,
			"WB_IN_H", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&conn->base, prop, 0);
	wb->in_h_property = prop;

	prop = drm_property_create_range(drm, DRM_MODE_PROP_ATOMIC,
			"WB_OUT_X", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&conn->base, prop, 0);
	wb->out_x_property = prop;

	prop = drm_property_create_range(drm, DRM_MODE_PROP_ATOMIC,
			"WB_OUT_Y", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&conn->base, prop, 0);
	wb->out_y_property = prop;

	prop = drm_property_create_range(drm, DRM_MODE_PROP_ATOMIC,
			"WB_OUT_W", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&conn->base, prop, 0);
	wb->out_w_property = prop;

	prop = drm_property_create_range(drm, DRM_MODE_PROP_ATOMIC,
			"WB_OUT_H", 0, UINT_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&conn->base, prop, 0);
	wb->out_h_property = prop;

	return 0;
}

static int spacemit_wb_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct spacemit_drm_private *priv = drm->dev_private;
	struct spacemit_wb *wb = dev_get_drvdata(dev);
	struct device_node *np = wb->pdev->dev.of_node;
	int id = -1;
	int ret;

	wb->ddev = drm;
	ret = spacemit_wb_context_init(wb, np);
	if (ret)
		return -EINVAL;

	id = wb->ctx.id;
	if (id < 0)
		return -EINVAL;

	spacemit_wb_device_create(wb, &wb->pdev->dev);

	priv->wb_connector[id] = &wb->wb_connector;

	drm_connector_helper_add(&priv->wb_connector[id]->base,
				 &spacemit_wb_connector_helper_funcs);

	ret = drm_writeback_connector_init(drm, priv->wb_connector[id],
					   &spacemit_wb_connector_funcs,
					   &spacemit_wb_encoder_helper_funcs,
					   spacemit_wb_formats,
					   ARRAY_SIZE(spacemit_wb_formats),
					   wb->wb_connector.encoder.possible_crtcs);
	if (ret) {
		DRM_ERROR("drm_connector_init() failed\n");
		return ret;
	}

	spacemit_wb_create_properties(&wb->wb_connector.base);

	return 0;
}

static void spacemit_wb_unbind(struct device *dev,
		       struct device *master, void *data)
{
	/* do nothing */
	DRM_INFO("%s()\n", __func__);
}

static const struct component_ops spacemit_wb_component_ops = {
	.bind = spacemit_wb_bind,
	.unbind = spacemit_wb_unbind,
};

static int spacemit_wb_probe(struct platform_device *pdev)
{
	struct spacemit_wb *wb;

	wb = devm_kzalloc(&pdev->dev, sizeof(*wb), GFP_KERNEL);
	if (!wb) {
		DRM_ERROR("failed to allocate wb data.\n");
		return -ENOMEM;
	}

	wb->pdev = pdev;

	platform_set_drvdata(pdev, wb);

	return component_add(&pdev->dev, &spacemit_wb_component_ops);
}

static void spacemit_wb_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &spacemit_wb_component_ops);
}

static const struct of_device_id spacemit_wb_of_match[] = {
	{.compatible = "spacemit,wb0"},
	{.compatible = "spacemit,wb1"},
	{ }
};
MODULE_DEVICE_TABLE(of, spacemit_wb_of_match);

struct platform_driver spacemit_wb_driver = {
	.probe = spacemit_wb_probe,
	.remove = spacemit_wb_remove,
	.driver = {
		.name = "spacemit-wb-drv",
		.of_match_table = spacemit_wb_of_match,
	},
};

MODULE_DESCRIPTION("Spacemit WB Driver");
MODULE_LICENSE("GPL v2");
