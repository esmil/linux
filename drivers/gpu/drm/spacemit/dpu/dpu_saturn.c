// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/types.h>
#include <linux/math64.h>
#include <drm/drm_atomic.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_writeback.h>
#include "dpu_saturn.h"
#include "saturn_fbcmem.h"
#include "../spacemit_cmdlist.h"
#include "../spacemit_dmmu.h"
#include "../spacemit_dpu_reg.h"
#include "../spacemit_drm.h"
#include "../spacemit_wb.h"
#include "post_process_hee.h"
#include <video/display_timing.h>
#include <dt-bindings/display/spacemit_dpu.h>
#include "saturn_regs/ops_hee.h"
#if IS_ENABLED(CONFIG_SPACEMIT_NI700)
#include <soc/spacemit/ni700.h>
#endif

#define CREATE_TRACE_POINTS
#include "dpu_trace.h"
#include "dpu_debug.h"
#include "../spacemit_bootloader.h"

/* DPU MCLK */
#define DPU_MCLK_MAX				409600000

static void dpu_rpm_resume(struct spacemit_crtc *a_crtc);
static void dpu_rpm_suspend(struct spacemit_crtc *a_crtc);

enum {
	DITHER_MODE_ARM = 0,
	DITHER_MODE_PATTERN,
	DITHER_MODE_MAX_LIMITED
};

#define TOTAL_RDMA_MEMSIZE	(68 * 1024) /* 68KB */
#define DSC_COMP_COEF	(3) /* dsc compression coefficient */
#define DSI_PHY_ANA_CTRL1 0x1E8
//RDMA_FMT_YUV_420_P1_8, RDMA_FMT_YUV_420_P1_10 not support by hardware, has checked with asic
//rdma hardware support RDMA_FMT_BGRA_16161616/RDMA_FMT_RGBA_16161616 formats are not  support by fourcc
static const struct dpu_format_id primary_fmts[] = {
	{ DRM_FORMAT_ABGR2101010, 1, 32 }, //RDMA_FMT_ABGR_2101010
	{ DRM_FORMAT_ARGB8888, 4, 32 }, //RDMA_FMT_ARGB_8888
	{ DRM_FORMAT_ABGR8888, 5, 32 }, //RDMA_FMT_ABGR_8888
	{ DRM_FORMAT_RGBA8888, 6, 32 }, //RDMA_FMT_RGBA_8888
	{ DRM_FORMAT_BGRA8888, 7, 32 }, //RDMA_FMT_BGRA_8888
	{ DRM_FORMAT_XRGB8888, 8, 32 }, //RDMA_FMT_XRGB_8888
	{ DRM_FORMAT_XBGR8888, 9, 32 }, //RDMA_FMT_XBGR_8888
	{ DRM_FORMAT_RGBX8888, 10, 32 }, //RDMA_FMT_RGBX_8888
	{ DRM_FORMAT_BGRX8888, 11, 32 }, //RDMA_FMT_BGRX_8888
	{ DRM_FORMAT_RGB565, 22, 16 }, //RDMA_FMT_RGB_565
	{ DRM_FORMAT_BGR565, 23, 16 }, //RDMA_FMT_BGR_565
	{ DRM_FORMAT_RGB888, 12, 24 }, //RDMA_FMT_RGB_888
	{ DRM_FORMAT_BGR888, 13, 24 }, //RDMA_FMT_BGR_888
	/*
	{ DRM_FORMAT_ARGB2101010,	 0 }, //RDMA_FMT_ARGB_2101010
	{ DRM_FORMAT_RGBA1010102,	 2 }, //RDMA_FMT_RGBA_2101010
	{ DRM_FORMAT_BGRA1010102,	 3 }, //RDMA_FMT_BGRA_2101010
	{ DRM_FORMAT_RGB888,		12 }, //RDMA_FMT_RGB_888
	{ DRM_FORMAT_BGR888,        13 }, //RDMA_FMT_BGR_888
	{ DRM_FORMAT_RGBA5551,      14 }, //RDMA_FMT_RGBA_5551
	{ DRM_FORMAT_BGRA5551,      15 }, //RDMA_FMT_BGRA_5551
	{ DRM_FORMAT_ABGR1555,      16 }, //RDMA_FMT_ABGR_1555
	{ DRM_FORMAT_ARGB1555,      17 }, //RDMA_FMT_ARGB_1555
	{ DRM_FORMAT_RGBX5551,      18 }, //RDMA_FMT_RGBX_5551
	{ DRM_FORMAT_BGRX5551,      19 }, //RDMA_FMT_BGRX_5551
	{ DRM_FORMAT_XBGR1555,      20 }, //RDMA_FMT_XBGR_1555
	{ DRM_FORMAT_XRGB1555,      21 }, //RDMA_FMT_XRGB_1555
	{ DRM_FORMAT_ARGB16161616F, 24 }, //RDMA_FMT_ARGB_16161616
	{ DRM_FORMAT_ABGR16161616F, 25 }, //RDMA_FMT_ABGR_16161616
	{ DRM_FORMAT_XYUV8888,      32 }, //RDMA_FMT_XYUV_444_P1_8, uv_swap has no corresponding fourcc format
	{ DRM_FORMAT_Y410,          33 }, //RDMA_FMT_XYUV_444_P1_10, uv_swap has no corresponding fourcc format
	{ DRM_FORMAT_YUYV,          34 }, //RDMA_FMT_VYUY_422_P1_8
	{ DRM_FORMAT_YVYU,          34 }, //RDMA_FMT_VYUY_422_P1_8, uv_swap = 1
	{ DRM_FORMAT_UYVY,          35 }, //RDMA_FMT_YVYU_422_P1_8
	{ DRM_FORMAT_VYUY,          35 }, //RDMA_FMT_YVYU_422_P1_8, uv_swap = 1
	*/
	{ DRM_FORMAT_YUV420_8BIT,   37, 12 }, //DRM_FORMAT_YUV420_8BIT for AFBC
	{ DRM_FORMAT_NV12,          37, 12 }, //RDMA_FMT_YUV_420_P2_8
	{ DRM_FORMAT_YVU420,        38, 12 }, //4CC: YV12

	/*
	{ DRM_FORMAT_NV21,          37 }, //RDMA_FMT_YUV_420_P2_8, uv_swap = 1
	{ DRM_FORMAT_YUV420,        38 }, //RDMA_FMT_YUV_420_P3_8
	{ DRM_FORMAT_YVU420,        38 }, //RDMA_FMT_YUV_420_P3_8, uv_swap = 1
	{ DRM_FORMAT_YUV420_10BIT,  39 }, //RDMA_FMT_YUV_420_P1_10 //DPU not support, DO NOT use
	*/
	{ DRM_FORMAT_P010,          40, 24 }, //RDMA_FMT_YUV_420_P2_10
	/*
	{ DRM_FORMAT_Q410,          41 }, //DPU not support
	{ DRM_FORMAT_Q401,          41 }, //DPU not support
	*/
};

/* hee */
const struct spacemit_hw_rdma saturn_hee_rdmas[] = {
	/* TODO: set max_yuv_height to 1088 instead of 1080 due to vpu fw issue */
	{FORMAT_RGB | FORMAT_AFBC, ROTATE_COMMON},
	{FORMAT_RGB | FORMAT_AFBC | FORMAT_RAW_YUV | FORMAT_AFBC, ROTATE_COMMON | ROTATE_AFBC_90_270},
	{FORMAT_RGB | FORMAT_AFBC, ROTATE_COMMON},
	{FORMAT_RGB | FORMAT_AFBC, ROTATE_COMMON},
};

const u32 saturn_hee_fbcmem_sizes[] = {
	89600,	//87.5k
	15360,	//16k
};
EXPORT_SYMBOL(saturn_hee_fbcmem_sizes);

struct spacemit_hw_device spacemit_dp_devices[SPACEMIT_DP_MAX_DEVICES] = {
	[SATURN_HEE] = {
		.base = NULL,		/* Parsed by dts */
		.phy_addr = 0x0,	/* Parsed by dts */
		.plane_nums = 16,
		.offline_plane_nums = 1,
		.crtc_nums = 2,
		.rdma_nums = ARRAY_SIZE(saturn_hee_rdmas),
		.rdmas = saturn_hee_rdmas,
		.n_formats = ARRAY_SIZE(primary_fmts),
		.formats = primary_fmts,
		.n_fbcmems = ARRAY_SIZE(saturn_hee_fbcmem_sizes),
		.fbcmem_sizes = saturn_hee_fbcmem_sizes,
		.solid_color_shift = 0,
		.hdr_coef_size = 135,
		.hor_scale_coef_size = 48,
		.ver_scale_coef_size = 48,
		.scaler_num = 1,
		.gamma_size = 257,
		.etm_size = 65,
		.acad_num = 149,
		.is_acad_on = false,
		.is_bl_save_on = false,
		.dpu_version = SATURN_HEE,
		.conf_dpuctrl_color_matrix = saturn_hee_conf_dpuctrl_color_matrix,
		.check_end_matrix = saturn_hee_check_end_matrix,
		.conf_dpuctrl_acad = saturn_hee_conf_dpuctrl_acad,
		.conf_ee = saturn_hee_conf_dpuctrl_ee,
		.update_csc_matrix = saturn_hee_update_csc_matrix,
		.update_hdr_matrix = saturn_hee_update_hdr_matrix,
		.conf_scaler_coefs = saturn_hee_conf_scaler_coefs,
		.conf_scaler_x = saturn_hee_conf_scaler_x,
		.enable_vsync = saturn_hee_enable_vsync,
		.enable_cfg_irq = saturn_hee_enable_cfg_irq,
		.cfg_ready = saturn_hee_cfg_ready,
		.sw_start = saturn_hee_sw_start,
		.irq_enable = saturn_hee_irq_enable,
		.dpu_init = saturn_hee_dpu_init,
		.conf_gamma_table = saturn_hee_conf_dpuctrl_pp_gamma,
		.conf_matrix = saturn_hee_dpuctrl_color_temp,
		.plane_update_hw_channel = saturn_hee_plane_update_hw_channel,
		.plane_disable_hw_channel = saturn_hee_plane_disable_hw_channel,
		.conf_dpuctrl = saturn_hee_conf_dpuctrl,
		.wb_config = saturn_hee_wb_config,
		.wb_disable = saturn_hee_wb_disable,
		.is_wb_en = saturn_hee_is_wb_en,
		.get_cfg_rdy = saturn_hee_get_cfg_rdy,
		.get_int_sts = saturn_hee_get_int_sts,
		.get_irq_bit = saturn_hee_get_irq_bit,
		.clr_int_sts = saturn_hee_clr_int_sts,
		.dpu_disable = saturn_hee_dpu_disable,
		.dpu_restart = saturn_hee_dpu_restart,
		.dpu_stop_check = saturn_hee_dpu_stop_check,
		.enable_cmdlist = saturn_hee_enable_cmdlist,
		.cfg_cmdlist = saturn_hee_cfg_cmdlist,
		.rdma_contig_mem = saturn_hee_rdma_contig_mem,
		.rdma_dmmu = saturn_hee_rdma_dmmu,
		.wb_dmmu = saturn_hee_wb_dmmu,
		.get_cl_rdma_buf = saturn_hee_get_cl_rdma_buf,
		.cmdlist_fill_data_row = saturn_hee_cmdlist_fill_data_row,
		.cmdlist_fill_conf_row = saturn_hee_cmdlist_fill_conf_row,
		.wb_cmdlist = saturn_hee_wb_cmdlist,
		.cmdlist_dump_node = saturn_hee_cmdlist_dump_node,
#ifdef CONFIG_SPACEMIT_DEBUG
		.dpu_dump_rdma_status = hee_dpu_dump_rdma_status,
		.dpu_dump_reg = hee_dpu_dump_reg,
#endif
	},
	[SATURN_EDP] = {
		.base = NULL,		/* Parsed by dts */
		.phy_addr = 0x0,	/* Parsed by dts */
		.plane_nums = 16,
		.offline_plane_nums = 1,
		.crtc_nums = 2,
		.rdma_nums = ARRAY_SIZE(saturn_hee_rdmas),
		.rdmas = saturn_hee_rdmas,
		.n_formats = ARRAY_SIZE(primary_fmts),
		.formats = primary_fmts,
		.n_fbcmems = ARRAY_SIZE(saturn_hee_fbcmem_sizes),
		.fbcmem_sizes = saturn_hee_fbcmem_sizes,
		.solid_color_shift = 0,
		.hdr_coef_size = 135,
		.hor_scale_coef_size = 48,
		.ver_scale_coef_size = 48,
		.scaler_num = 1,
		.gamma_size = 257,
		.etm_size = 65,
		.acad_num = 149,
		.is_acad_on = false,
		.is_edp = true,
		.is_bl_save_on = false,
		.dpu_version = SATURN_HEE,
		.conf_dpuctrl_color_matrix = saturn_hee_conf_dpuctrl_color_matrix,
		.check_end_matrix = saturn_hee_check_end_matrix,
		.conf_dpuctrl_acad = saturn_hee_conf_dpuctrl_acad,
		.conf_ee = saturn_hee_conf_dpuctrl_ee,
		.update_csc_matrix = saturn_hee_update_csc_matrix,
		.update_hdr_matrix = saturn_hee_update_hdr_matrix,
		.conf_scaler_coefs = saturn_hee_conf_scaler_coefs,
		.conf_scaler_x = saturn_hee_conf_scaler_x,
		.enable_vsync = saturn_hee_enable_vsync,
		.enable_cfg_irq = saturn_hee_enable_cfg_irq,
		.cfg_ready = saturn_hee_cfg_ready,
		.sw_start = saturn_hee_sw_start,
		.irq_enable = saturn_hee_irq_enable,
		.dpu_init = saturn_hee_dpu_init,
		.conf_gamma_table = saturn_hee_conf_dpuctrl_pp_gamma,
		.conf_matrix = saturn_hee_dpuctrl_color_temp,
		.plane_update_hw_channel = saturn_hee_plane_update_hw_channel,
		.plane_disable_hw_channel = saturn_hee_plane_disable_hw_channel,
		.conf_dpuctrl = saturn_hee_conf_dpuctrl,
		.wb_config = saturn_hee_wb_config,
		.wb_disable = saturn_hee_wb_disable,
		.is_wb_en = saturn_hee_is_wb_en,
		.get_cfg_rdy = saturn_hee_get_cfg_rdy,
		.get_int_sts = saturn_hee_get_int_sts,
		.get_irq_bit = saturn_hee_get_irq_bit,
		.clr_int_sts = saturn_hee_clr_int_sts,
		.dpu_disable = saturn_hee_dpu_disable,
		.dpu_restart = saturn_hee_dpu_restart,
		.dpu_stop_check = saturn_hee_dpu_stop_check,
		.enable_cmdlist = saturn_hee_enable_cmdlist,
		.cfg_cmdlist = saturn_hee_cfg_cmdlist,
		.rdma_contig_mem = saturn_hee_rdma_contig_mem,
		.rdma_dmmu = saturn_hee_rdma_dmmu,
		.wb_dmmu = saturn_hee_wb_dmmu,
		.get_cl_rdma_buf = saturn_hee_get_cl_rdma_buf,
		.cmdlist_fill_data_row = saturn_hee_cmdlist_fill_data_row,
		.cmdlist_fill_conf_row = saturn_hee_cmdlist_fill_conf_row,
		.wb_cmdlist = saturn_hee_wb_cmdlist,
		.cmdlist_dump_node = saturn_hee_cmdlist_dump_node,
#ifdef CONFIG_SPACEMIT_DEBUG
		.dpu_dump_rdma_status = hee_dpu_dump_rdma_status,
		.dpu_dump_reg = hee_dpu_dump_reg,
#endif
	},
};
EXPORT_SYMBOL(spacemit_dp_devices);

void saturn_enable_irq_mask(struct spacemit_crtc *a_crtc, bool enable, u32 offset, u32 mask)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	//unsigned long flags;
	u32 irq_bit = hwdev->get_irq_bit(INT_VSYNC, a_crtc->dev_id) |
		hwdev->get_irq_bit(INT_CFG_RDY, a_crtc->dev_id);

	if (!hwdev->base) {
		DRM_ERROR("hwdev->base NULL %s\n", __func__);
		return;
	}

	if (!a_crtc->power_on)
		return;

	//spin_lock_irqsave(&a_crtc->irq_msk_lock, flags);
	if (enable) {
		//uboot -> kernel, clear online vsync status first
		if ((a_crtc->is_offline_mode == 0) && (unlikely(a_crtc->logo_booton))) {
			//write 1 to clr irq status
			hwdev->clr_int_sts(a_crtc, irq_bit, a_crtc->dev_id);
		}
		dpu_set_bit(hwdev->base, offset, mask);
	} else {
		dpu_clr_bit(hwdev->base, offset, mask);
	}

	//spin_unlock_irqrestore(&a_crtc->irq_msk_lock, flags);
}
EXPORT_SYMBOL(saturn_enable_irq_mask);

static void dpu_get_dsc_pxclk(struct spacemit_crtc *a_crtc, unsigned int dsc_bpc,
	unsigned int dsc_bpp, struct drm_display_mode *mode, unsigned int fps)
{
	unsigned int width;

	if (!mode || !dsc_bpc)
		return;

	width = mode->hdisplay;
	width = width * dsc_bpp / DSC_COMP_COEF / dsc_bpc;  /* dsc_hout */
	a_crtc->dsc_pxclk = (mode->htotal - mode->hdisplay + width) * mode->vtotal * fps;

}

static void dpu_get_dsc_info(struct spacemit_crtc *a_crtc, struct device_node *np)
{
	int bytes, ret, i;
	unsigned int dsc_bpc;
	unsigned int dsc_bpp;
	unsigned int val;
	const void *p;
	struct drm_display_mode mode;

	ret = of_property_read_u32(np, "dsc-enable", &val);
	if (ret || !val)
		return;

	ret = of_property_read_u32(np, "dsc-bpc", &dsc_bpc);
	if (ret) {
		DRM_ERROR("of get mipi dsc-bpc failed\n");
		return;
	}

	ret = of_property_read_u32(np, "dsc-bpp", &dsc_bpp);
	if (ret) {
		DRM_ERROR("of get mipi dsc-bpp failed\n");
		return;
	}

	of_get_drm_display_mode(np, &mode, 0, 0);
	val = mode.clock * 1000 / mode.htotal / mode.vtotal;
	dpu_get_dsc_pxclk(a_crtc, dsc_bpc, dsc_bpp, &mode, val);

	a_crtc->dsc_clk = 2 * mode.hdisplay * mode.vtotal * val;
	a_crtc->dsc_x_slice = SPACEMIT_DSC_DEFALUT_X_SLICE;
	p = of_get_property(np, "dsc-regs", &bytes);
	if (p) {
		a_crtc->dsc_regs = devm_kmalloc(a_crtc->dev, bytes, GFP_KERNEL);
		if (IS_ERR(a_crtc->dsc_regs)) {
			DRM_ERROR("alloc dsc reg buffer(size:0x%x) failed\n", bytes);
			return;
		}
		a_crtc->dsc_regs_len = bytes / sizeof(u32);
		for (i = 0; i < a_crtc->dsc_regs_len; i++)
			a_crtc->dsc_regs[i] = be32_to_cpup((u32 *)p + i);
	} else {
		DRM_ERROR("can't find dsc-regs property\n");
		a_crtc->dsc_regs = NULL;
		a_crtc->dsc_regs_len = 0;
		return;
	}

	ret = of_property_read_u32(np, "dsc-x-slice", &a_crtc->dsc_x_slice);
	if (ret || a_crtc->dsc_x_slice > SPACEMIT_DSC_MAX_X_SLICE || !a_crtc->dsc_x_slice) {
		DRM_ERROR("of get dsc-x-slice failed\n");
		a_crtc->dsc_x_slice = SPACEMIT_DSC_DEFALUT_X_SLICE;
		return;
	}

	if (a_crtc->dsc_regs && a_crtc->dsc_x_slice)
		a_crtc->dsc_clk /= a_crtc->dsc_x_slice;
}

void spacemit_dpu_power_enable(struct spacemit_crtc *a_crtc, bool enable)
{
	if (enable) {
		if (a_crtc->power_on)
			return;

		pm_runtime_get_sync(a_crtc->dev);
		a_crtc->power_on = true;
	} else {
		if (!a_crtc->power_on)
			return;

		a_crtc->power_on = false;
		pm_runtime_put_sync(a_crtc->dev);

	}

}

#ifdef CONFIG_PM
static void dpu_lpm_work_func(struct work_struct *work)
{
	struct spacemit_crtc *a_crtc = container_of(work, struct spacemit_crtc,
						lpm_qos_work.work);
	dpu_rpm_suspend(a_crtc);
}
#endif

static void dpu_get_pipe_out_node(struct spacemit_crtc *a_crtc)
{
	struct device_node *ports, *port, *ep;
	struct device_node *remote; /* dsi */
	struct device_node *child;  /* panel candidate */
	u32 reg;

	a_crtc->dsi_node = NULL;
	a_crtc->panel_node = NULL;

	ports = of_get_child_by_name(a_crtc->dev->of_node, "ports");
	if (!ports) {
		DRM_ERROR("no ports under %pOF\n", a_crtc->dev->of_node);
		return;
	}

	for_each_child_of_node(ports, port) {
		if (of_property_read_u32(port, "reg", &reg))
			continue;
		if (reg != 0)
			continue;

		ep = of_graph_get_endpoint_by_regs(
				a_crtc->dev->of_node, reg, 0);
		if (!ep)
			continue;

		remote = of_graph_get_remote_port_parent(ep);
		if (!remote || !of_device_is_available(remote)) {
			of_node_put(ep);
			of_node_put(remote);
			continue;
		}

		a_crtc->dsi_node = remote;

		DRM_DEBUG(" found dsi node: %pOF\n", remote);

		for_each_child_of_node(remote, child) {

			if (of_node_name_eq(child, "ports"))
				continue;

			if (!of_device_is_available(child))
				continue;

			if (!of_property_read_bool(child, "compatible"))
				continue;

			DRM_DEBUG(" found panel node: %pOF\n", child);
			a_crtc->panel_node = child;
			break;
		}

		of_node_put(ep);

		if (!a_crtc->panel_node) {
			DRM_ERROR("no panel found under dsi %pOF\n", remote);
		} else {
			DRM_DEBUG("SUCCESS: crtc %pOF -> dsi %pOF -> panel %pOF\n",
				 a_crtc->dev->of_node,
				 a_crtc->dsi_node,
				 a_crtc->panel_node);
		}
		return;
	}

	DRM_ERROR("failed to get dsi/panel node\n");
}

static void dpu_parse_panel_dt(struct spacemit_crtc *a_crtc)
{
	struct device_node *lcd_node = NULL;
	const char *str;
	char lcd_path[60];
	uint32_t value;
	int rc;

	dpu_get_pipe_out_node(a_crtc);
	if (!a_crtc->panel_node)
		return;

	rc = of_property_read_string(a_crtc->panel_node, "force-attached", &str);
	if (rc) {
		DRM_WARN("get panel string failed.\n");
		goto node_put;
	}

	sprintf(lcd_path, "/lcds/%s", str);
	lcd_node = of_find_node_by_path(lcd_path);
	if (!lcd_node) {
		DRM_WARN("find lcd node failed.\n");
		goto node_put;
	}

	a_crtc->dsc_clk = DPU_DSCCLK_DEFAULT;
	dpu_get_dsc_info(a_crtc, lcd_node);

	if (of_property_read_u32(lcd_node, "work-mode", &a_crtc->out_mode))
		a_crtc->out_mode = DPU_OUT_MODE_VIDEO; /* default is video mode */

	if (of_property_read_bool(lcd_node, "oled"))
		a_crtc->is_oled = true;
	else
		a_crtc->is_oled = false;

	DRM_INFO("is_oled: %d\n", a_crtc->is_oled);

	rc = of_property_read_string(lcd_node, "dsi-color-format", &str);
	if (rc)
		a_crtc->out_format = OUTFMT_RGB888;
	else if (!strcmp(str, "rgb888"))
		a_crtc->out_format = OUTFMT_RGB888;
	else if (!strcmp(str, "rgb666"))
		a_crtc->out_format = OUTFMT_RGB666;
	else if (!strcmp(str, "rgb666_packed"))
		a_crtc->out_format = OUTFMT_RGB666;
	else if (!strcmp(str, "rgb565"))
		a_crtc->out_format = OUTFMT_RGB565;
	else
		DRM_ERROR("dsi-color-format (%s) is not supported\n", str);

	rc = of_property_read_u32(lcd_node, "dither-mode", &value);
	if (!rc && value < DITHER_MODE_MAX_LIMITED)
		a_crtc->dither_mode = value;
	else
		a_crtc->dither_mode = DITHER_MODE_ARM;

	if (!of_property_read_u32(lcd_node, "spacemit-dpu-min-mclk", &value))
		a_crtc->min_mclk = value;

	if (!of_property_read_u32(lcd_node, "spacemit-dsi-escclk", &value))
		a_crtc->escclk = value;

	if (!of_property_read_u32(lcd_node, "split-enable", &value))
		a_crtc->split_en = value;

	if (a_crtc->dsipll_valid) {
		if (of_property_read_u32(lcd_node, "spacemit-dpu-dsipll-reg0", &a_crtc->dsipll_reg0))
			a_crtc->dsipll_reg0 = DPU_DSIPLL_REG0_DEFAULT;
		if (of_property_read_u32(lcd_node, "spacemit-dpu-dsipll-reg1", &a_crtc->dsipll_reg1))
			a_crtc->dsipll_reg1 = DPU_DSIPLL_REG1_DEFAULT;
		if (of_property_read_u32(lcd_node, "spacemit-dpu-dsipll-reg2", &a_crtc->dsipll_reg2))
			a_crtc->dsipll_reg2 = DPU_DSIPLL_REG2_DEFAULT;
	}

node_put:
	of_node_put(a_crtc->panel_node);
}

static int dpu_parse_dt(struct spacemit_crtc *a_crtc, struct device_node *np)
{
	struct dpu_clk_context *clk_ctx = &a_crtc->clk_ctx;
	struct resource *r;
	struct platform_device *pdev = to_platform_device(a_crtc->dev);

#ifdef CONFIG_SOC_SPACEMIT_K3_FPGA
	return 0;
#endif
	clk_ctx->pxclk = of_clk_get_by_name(np, "pxclk");
	if (IS_ERR(clk_ctx->pxclk)) {
		pr_err("%s, read pxclk failed from dts!\n", __func__);
		return PTR_ERR(clk_ctx->pxclk);
	}

	clk_ctx->mclk = of_clk_get_by_name(np, "mclk");
	if (IS_ERR(clk_ctx->mclk)) {
		pr_err("%s, read mclk failed from dts!\n", __func__);
		return PTR_ERR(clk_ctx->mclk);
	}

	clk_ctx->hclk = of_clk_get_by_name(np, "hclk");
	if (IS_ERR(clk_ctx->hclk)) {
		clk_ctx->hclk = NULL;
		pr_err("%s, read hclk failed from dts!\n", __func__);
		// return PTR_ERR(clk_ctx->hclk);
	}

	if (!a_crtc->is_edp) {
		clk_ctx->escclk = of_clk_get_by_name(np, "escclk");
		if (IS_ERR(clk_ctx->escclk)) {
			pr_err("%s, read escclk failed from dts!\n", __func__);
			return PTR_ERR(clk_ctx->escclk);
		}
		clk_ctx->bitclk = of_clk_get_by_name(np, "bitclk");
		if (IS_ERR(clk_ctx->bitclk)) {
			clk_ctx->bitclk = NULL;
			DRM_INFO("%s, read bitclk failed from dts!\n", __func__);
		}
		if (of_property_read_u32(np, "spacemit-dpu-bitclk", &a_crtc->bitclk))
			a_crtc->bitclk = DPU_BITCLK_DEFAULT;
		if (of_property_read_u32(np, "spacemit-dsi-escclk", &a_crtc->escclk))
			a_crtc->escclk = DPU_ESCCLK_DEFAULT;
	} else {
		clk_ctx->escclk = of_clk_get_by_name(np, "escclk");
		if (IS_ERR(clk_ctx->escclk)) {
			pr_err("%s, read escclk failed from dts!\n", __func__);
			clk_ctx->escclk = NULL;
			// return PTR_ERR(clk_ctx->escclk);
		}
		clk_ctx->bitclk = NULL;
	}

	clk_ctx->aclk = of_clk_get_by_name(np, "aclk");
	if (IS_ERR(clk_ctx->aclk)) {
		clk_ctx->aclk = NULL;
		DRM_INFO("%s, read aclk failed from dts!\n", __func__);
	}

	clk_ctx->dscclk = of_clk_get_by_name(np, "dscclk");
	if (IS_ERR(clk_ctx->dscclk)) {
		clk_ctx->dscclk = NULL;
		DRM_INFO("%s, read dscclk failed from dts!\n", __func__);
	}

	if (of_property_read_u32(np, "spacemit-dpu-min-mclk", &a_crtc->min_mclk))
		a_crtc->min_mclk = DPU_MCLK_DEFAULT;

	if (of_property_read_u32(np, "spacemit-dpu-max-mclk", &a_crtc->max_mclk) ||
			(a_crtc->max_mclk > DPU_MCLK_MAX))
		a_crtc->max_mclk = DPU_MCLK_MAX;

	// if (of_property_read_bool(np, "spacemit-dpu-auto-fc"))
	// 	a_crtc->enable_auto_fc = 1;

	if (of_property_read_bool(np, "spacemit-dpu-dsipll"))
		a_crtc->dsipll_valid = true;

	//crtc is_offline_mode dts
	if (a_crtc->dsipll_valid || a_crtc->is_edp)
		a_crtc->is_offline_mode = 0;
	else
		a_crtc->is_offline_mode = 1;

	if (of_property_read_u32(np, "spacemit-dpu-aclk", &a_crtc->aclk))
		a_crtc->aclk = DPU_AXICLK_DEFAULT;

	if (of_property_read_u64(np, "spacemit-dpu-bw-margin", &a_crtc->bw_margin))
		a_crtc->bw_margin = 0;
	a_crtc->max_bw = DPU_MAX_QOS_REQ - a_crtc->bw_margin;

#ifdef CONFIG_PM
	if (of_property_read_u32(np, "lpm-commit-qos", &a_crtc->lpm_commit_qos)) {
		DRM_INFO("can not get lcd lpm commit qos value\n");
		a_crtc->lpm_commit_qos = 0;
	}

	if (!a_crtc->is_offline_mode) {
		if (of_property_read_u32(np, "lpm-bl-qos", &a_crtc->lpm_bl_qos)) {
			DRM_INFO("can not get lcd bl lpm qos value\n");
			a_crtc->lpm_bl_qos = 0;
		}
	}

	if (of_property_read_u32(np, "lpm-period", &a_crtc->lpm_period)) {
		DRM_INFO("can not get lcd lpm period value\n");
		a_crtc->lpm_period = SPACEMIT_DPU_LPM_PERIOD_MIN_MS;
	}

	if (a_crtc->lpm_period) {
		INIT_DELAYED_WORK(&a_crtc->lpm_qos_work, dpu_lpm_work_func);
		a_crtc->lpm_work_pending = false;
	}
#endif

	if (!a_crtc->is_offline_mode && !a_crtc->is_edp)
		/* must be the last in parse process */
		dpu_parse_panel_dt(a_crtc);

	if (a_crtc->dsipll_valid) {
		r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		a_crtc->dsipll_base = devm_ioremap_resource(&pdev->dev, r);
		if (IS_ERR(a_crtc->dsipll_base))
			return PTR_ERR(a_crtc->dsipll_base);

		if (a_crtc->split_en) {
			r = platform_get_resource(pdev, IORESOURCE_MEM, 1);
			a_crtc->dsi1pll_base = devm_ioremap_resource(&pdev->dev, r);
			if (IS_ERR(a_crtc->dsi1pll_base))
				return PTR_ERR(a_crtc->dsi1pll_base);

		}
	}

	return 0;
}

static unsigned int dpu_get_bpp(u32 format)
{
	unsigned int i = 0;

	for (i = 0; i < ARRAY_SIZE(primary_fmts); i++) {
		if (format == primary_fmts[i].format)
			return primary_fmts[i].bpp;
	}

	DRM_ERROR("format 0x%x is not supported!\n", format);
	return SPACEMIT_DPU_INVALID_FORMAT_ID;
}


static int dpu_calc_plane_mclk_bw(struct drm_plane *plane,
		struct drm_plane_state *new_state)
{
	/* For some platform without aclk, mclk = max(aclk, mclk) */
	uint64_t calc_mclk = 0;
	uint64_t Fpixclk_hblk = 0;
	struct drm_plane_state *state = NULL;
	struct drm_crtc *crtc = new_state->crtc;
	struct drm_display_mode *mode = NULL;
	struct spacemit_plane_state *spacemit_plane_state = NULL;
	uint64_t tmp;
	uint64_t hact = 0;
	uint64_t fps = 0;
	bool scl_en = 0;
	unsigned int C = 0;
	uint64_t width_ratio = 0;	/* (scl_in_width/hact) * 1000000 */
	uint64_t height_ratio = 0;	/* roundup(scl_in_height/scl_out_height) */
	uint64_t scl_in_width, scl_in_height, scl_out_width, scl_out_height;
	uint64_t fscl_in = 0;

	struct drm_framebuffer *fb = new_state->fb;
	const struct drm_format_info *format = fb->format;
	unsigned int bpp = dpu_get_bpp(format->format);
	uint64_t calc_bandwidth = 0;
	unsigned long img_width = 0;

	struct spacemit_crtc *a_crtc = NULL;

	state = new_state;
	if (!crtc)
		return 0;

	a_crtc = to_spacemit_crtc(crtc);
	if (!a_crtc->enable_auto_fc)
		return 0;

	/* prepare calc mclk and bw */
	mode = &crtc->mode;
	hact = mode->hdisplay;
	fps = mode->clock * 1000 / (mode->htotal * (u16)mode->vtotal);
	spacemit_plane_state = to_spacemit_plane_state(state);
	scl_en = spacemit_plane_state->use_scl;
	Fpixclk_hblk = hact * (mode->vtotal) * fps; /* MHZ */

	trace_dpu_fpixclk_hblk(hact, fps, (uint64_t)mode->vtotal, Fpixclk_hblk);

	/* calculate no_scl and scl_up mclk and bw */
	if (is_rot_90_270(state->rotation))
		img_width = state->src_h >> 16;
	else
		img_width = state->src_w >> 16;

	trace_u64_data("img_width", img_width);
	//calc_mclk = ( hact + 32 ) * MHZ2HZ / hact * Fpixclk_hblk / 2 / MHZ2HZ * 1.1;
	tmp = (hact + 32) * MHZ2HZ * 110;
	do_div(tmp, hact);
	tmp = tmp * Fpixclk_hblk;
	tmp = tmp >> 1;
	do_div(tmp, MHZ2HZ);
	do_div(tmp, 100);
	calc_mclk = tmp;
	if (calc_mclk > a_crtc->max_mclk) {
		DRM_INFO("plane:%d mclk too large %lld\n", state->zpos, calc_mclk);
		DRM_INFO("img_width = %ld\n", img_width);
		return -EINVAL;
	}
	//calc_bandwidth = Fpixclk_hblk * bpp * img_width / hact / 8;
	tmp = Fpixclk_hblk * bpp * img_width;
	do_div(tmp, hact);
	do_div(tmp, 8);
	calc_bandwidth = tmp;
	if (calc_bandwidth > (a_crtc->max_bw * MHZ2KHZ)) {
		DRM_INFO("plane:%d bandwidth too large %lld\n", state->zpos, calc_bandwidth);
		DRM_INFO("img_width = %ld\n", img_width);
		return -EINVAL;
	}

	/* calculate scl_en mclk and bandwidth */
	if (scl_en) {
		if (is_rot_90_270(state->rotation)) {
			scl_in_width  = state->src_h >> 16;
			scl_in_height = state->src_w >> 16;
		} else {
			scl_in_width  = state->src_w >> 16;
			scl_in_height = state->src_h >> 16;
		}

		scl_out_width = state->crtc_w;
		scl_out_height = state->crtc_h;

		C = min(2 * scl_in_width / scl_out_width, 4ULL);	/* sclaer after rdma */
		spacemit_plane_state->afbc_effc = 4 / C;

		//width_ratio = scl_in_width * MHZ2HZ / hact;
		tmp = scl_in_width * MHZ2HZ;
		do_div(tmp, hact);
		width_ratio = tmp;
		height_ratio = DIV_ROUND_UP_ULL(scl_in_height, scl_out_height) * MHZ2HZ;

		/* if scale down */
		if ((width_ratio > MHZ2HZ) || (height_ratio > MHZ2HZ)) {
			/*
			 * fscl_in = Fpixclk_hblk * max(width_ratio, MHZ2HZ) / MHZ2HZ * max(height_ratio, MHZ2HZ) / MHZ2HZ / C * 1.16
			 * fscl_out = (hact+32)/hact * Fpixclk_hblk/2*1.1, same as current calc_mclk.
			 * calc_mclk = max(fscl_in, fscl_out);
			 */
			tmp = Fpixclk_hblk * max(width_ratio, MHZ2HZ) * 116;
			do_div(tmp, MHZ2HZ);
			tmp = tmp * max(height_ratio, MHZ2HZ);
			do_div(tmp, MHZ2HZ);
			do_div(tmp, C);
			do_div(tmp, 100);
			fscl_in = tmp;
			calc_mclk = max(fscl_in, calc_mclk);

			if (calc_mclk > a_crtc->max_mclk) {
				DRM_INFO("plane:%d mclk too large %lld\n", state->zpos, calc_mclk);
				DRM_INFO("hact = %lld, fps = %lld, vtotal = %d, Fpixclk_hblk = %lld\n", hact, fps, (u16)mode->vtotal, Fpixclk_hblk);
				DRM_INFO("scl_in_width = %lld, scl_in_height = %lld, scl_out_width = %lld, scl_out_height = %lld, bpp = %d, C = %d\n", scl_in_width, scl_in_height, scl_out_width, scl_out_height, bpp, C);
				return -EINVAL;
			}

			//calc_bandwidth = Fpixclk_hblk * bpp * width_ratio / MHZ2HZ * height_ratio / MHZ2HZ / 8;
			tmp = Fpixclk_hblk * bpp * width_ratio;
			do_div(tmp, MHZ2HZ);
			tmp = tmp * height_ratio;
			do_div(tmp, MHZ2HZ);
			tmp = tmp >> 3;
			calc_bandwidth = tmp;
			if (calc_bandwidth > (a_crtc->max_bw * MHZ2KHZ)) {
				DRM_INFO("plane:%d bandwidth too large %lld\n", state->zpos, calc_bandwidth);
				DRM_INFO("hact = %lld, fps = %lld, vtotal = %d, Fpixclk_hblk = %lld\n", hact, fps, (u16)mode->vtotal, Fpixclk_hblk);
				DRM_INFO("scl_in_width = %lld, scl_in_height = %lld, scl_out_width = %lld, scl_out_height = %lld, bpp = %d, C = %d\n", scl_in_width, scl_in_height, scl_out_width, scl_out_height, bpp, C);
				return -EINVAL;
			}
			trace_dpu_mclk_scl(scl_in_width, scl_in_height, scl_out_width, scl_out_height, bpp, C, width_ratio, height_ratio);
		}
	}

	a_crtc = to_spacemit_crtc(crtc);
	if (calc_mclk < a_crtc->min_mclk)
		calc_mclk = a_crtc->min_mclk;

	if (calc_bandwidth < DPU_MIN_QOS_REQ)
		calc_bandwidth = DPU_MIN_QOS_REQ;

	spacemit_plane_state->mclk = calc_mclk;

	/* add some buffer for MMU and AFBC Header */
	//calc_bandwidth = calc_bandwidth * 115 / 100;
	tmp = calc_bandwidth * 115;
	do_div(tmp, 100);
	calc_bandwidth = tmp;
	spacemit_plane_state->bw = calc_bandwidth;

	trace_u64_data("plane calc_mclk", calc_mclk);
	trace_u64_data("plane calc_bw", calc_bandwidth);
	return 0;
}

static int dpu_update_clocks(struct spacemit_crtc *a_crtc, uint64_t mclk)
{
	struct dpu_clk_context *clk_ctx = &a_crtc->clk_ctx;
	uint64_t cur_mclk = 0;
	int ret = 0;

#ifdef CONFIG_SOC_SPACEMIT_K3_FPGA
	return 0;
#endif
	//offline does not change clk
	if (a_crtc->is_offline_mode || a_crtc->is_edp || !clk_ctx->mclk)
		return 0;

	trace_u64_data("update mclk", mclk);
	cur_mclk = clk_get_rate(clk_ctx->mclk);
	if (cur_mclk == mclk)
		return 0;

	ret = clk_set_rate(clk_ctx->mclk, mclk);
	if (unlikely(ret)) {
		trace_u64_data("Failed to set mclk", mclk);
		DRM_ERROR("Failed to set DPU MCLK %llu %d\n", mclk, ret);
		WARN_ON_ONCE(ret);
	} else {
		a_crtc->cur_mclk = clk_get_rate(clk_ctx->mclk);
		trace_u64_data("Pass to set mclk", mclk);
	}

	return ret;
}

static int dpu_update_bw(struct spacemit_crtc *a_crtc, uint64_t bw)
{
	uint64_t __maybe_unused tmp;

#ifdef CONFIG_SOC_SPACEMIT_K3_FPGA
	return 0;
#endif
	trace_u64_data("update bw", bw);
	if (a_crtc->cur_bw == bw)
		return 0;
#ifdef CONFIG_PM
#if IS_ENABLED(CONFIG_SPACEMIT_DDR_FC)
	tmp = bw;
	do_div(tmp, MHZ2KHZ);
	update_spacemit_ddr_bw_read_req(a_crtc->ddr_qos_cons, tmp);
	a_crtc->cur_bw = bw;
#endif
#endif

	return 0;
}

static const struct pll_freq_range_t pll_freq_table[32] = {
	{780,  1010, 1090},
	{1090, 1170, 1250},
	{1250, 1330, 1410},
	{1410, 1490, 1570},
	{1570, 1650, 1730},
	{1730, 1810, 1890},
	{1890, 1970, 2050},
	{2050, 2130, 2210},
	{2210, 2290, 2370},
	{2370, 2450, 2530},
	{2530, 2610, 2695},
	{2695, 2780, 2860},
	{2860, 2940, 3020},
	{3020, 3100, 3185},
	{3185, 3270, 3350},
	{3350, 3430, 3510},

	{3510, 3590, 3675},
	{3675, 3760, 3840},
	{3840, 3920, 4000},
	{4000, 4080, 4165},
	{4165, 4250, 4330},
	{4330, 4410, 4485},
	{4485, 4560, 4650},
	{4650, 4740, 4825},
	{4825, 4910, 4990},
	{4990, 5070, 5150},
	{5150, 5230, 5315},
	{5315, 5400, 5480},
	{5480, 5560, 5640},
	{5640, 5720, 5805},
	{5805, 5890, 5970},
	{5970, 6050, 6500}
};

static int pll_get_rate_sel(uint32_t vco_freq)
{
	if (vco_freq < 2000)
		return 0;
	else if (vco_freq < 4000)
		return 1;
	else if (vco_freq <= 6500)
		return 2;
	else
		return -1;
}

static int pll_get_range_index(uint32_t vco_freq)
{
	for (int i = 0; i < 32; i++) {
		if (vco_freq > pll_freq_table[i].low && vco_freq <= pll_freq_table[i].high)
			return i;
	}
	return -1;
}

/* pll_reg5 / pll_reg6 */
static uint8_t pll_make_range_reg(int range_idx)
{
	return (uint8_t)((0b100 << 5) | (range_idx & 0x1F));
}

/* pll_reg7 */
static uint8_t pll_make_rate_reg(int rate_sel)
{
	if (rate_sel < 0)
		return 0xFF;

	/* 01 | rate_sel | 0101 */
	return (uint8_t)((0x01 << 6) |
			(rate_sel << 4) |
			0x05);
}

/* ===================== ctrl_reg0 ===================== */
static u32 pll_make_ctrl_reg0(u32 vco_freq, int rate_sel)
{
	u32 denom = PLL_REF_FREQ_MHZ;
	u32 div_int;
	u64 frac22;
	u32 remainder;

	if (rate_sel >= 0)
		denom = PLL_REF_FREQ_MHZ  * (rate_sel + 1);

	/* integer part */
	div_int = vco_freq / denom;

	/* fractional part: round((vco % denom) / denom * 2^22) */
	remainder = vco_freq % denom;
	frac22 = (u64)remainder << 22;
	frac22 += denom / 2;
	do_div(frac22, denom);   /* frac22 is 22-bit */

	return ((div_int & 0xff) << 24) | ((u32)frac22 & 0x00ffffff);
}

/* ===================== ctrl_reg1 ===================== */
static uint32_t pll_make_ctrl_reg1(uint32_t vco_freq, int pllmode, int rate_sel)
{
	uint8_t pll_reg4, pll_reg5, pll_reg6, pll_reg7;

	int range_idx = pll_get_range_index(vco_freq);
	if (range_idx < 0)
		return 0xFFFFFFFF;

	pll_reg4 = (uint8_t)(3 + pllmode * 8);
	pll_reg5 = pll_make_range_reg(range_idx);
	pll_reg6 = pll_reg5;
	pll_reg7 = pll_make_rate_reg(rate_sel);

	return (pll_reg7 << 24) | (pll_reg6 << 16) |
		(pll_reg5 << 8) | pll_reg4;
}

/*
 * PLL_CTRL_REG2[30:29] div_sel encoding:
 *   0b00: pll_div2  → VCO / 2  (VCO = bitclock × 2)
 *   0b01: pll_div2  → VCO / 2  (VCO = bitclock × 2, same as 0b00)
 *   0b10: pll_div8  → VCO / 8  (VCO = bitclock × 8)
 *   0b11: pll_divm  → VCO / m  (not used)
 */
static const struct {
	uint32_t mult;     /* VCO = bitclock * mult */
	uint32_t div_sel;  /* PLL_CTRL_REG2[30:29] */
} pll_vco_candidates[] = {
	{  2, 0 }, /* pll_div2 (0b00) */
	{  2, 1 }, /* pll_div2 (0b01) */
	{  8, 2 }, /* pll_div8 (0b10) */
};

static int spacemit_calc_pll_regs(uint32_t bitclock, uint32_t *pll_ctrl_reg0,
					uint32_t *pll_ctrl_reg1, uint32_t *div_sel_out)
{
	const int pllmode = PLL_MODE_5G_PLL;
	uint32_t vco_freq = 0;
	uint32_t div_sel = 0;
	int rate_sel;

	/* If bitclock itself is already within the PLL VCO range,
	 * use it directly without any post-divider multiplication.
	 * NOTE: div_sel for "no division" case needs hardware clarification.
	 */
	if (pll_get_rate_sel(bitclock) >= 0 && pll_get_range_index(bitclock) >= 0) {
		vco_freq = bitclock;
		div_sel  = 0;
		DRM_DEBUG("pll vco: bitclock=%u MHz already in range, no division needed\n",
			 bitclock);
	} else {
		/* Try each multiplier in order until we find a VCO that falls in range */
		for (int i = 0; i < ARRAY_SIZE(pll_vco_candidates); i++) {
			uint32_t vco = bitclock * pll_vco_candidates[i].mult;

			if (pll_get_rate_sel(vco) >= 0 && pll_get_range_index(vco) >= 0) {
				vco_freq = vco;
				div_sel  = pll_vco_candidates[i].div_sel;
				DRM_INFO("pll vco selected: bitclock=%u MHz, mult=%u, vco=%u MHz, div_sel=%u\n",
					 bitclock, pll_vco_candidates[i].mult, vco_freq, div_sel);
				break;
			}
		}
	}

	if (!vco_freq) {
		DRM_ERROR("no valid VCO config found for bitclock=%u MHz\n", bitclock);
		return -EINVAL;
	}

	rate_sel = pll_get_rate_sel(vco_freq);

	*pll_ctrl_reg0 = pll_make_ctrl_reg0(vco_freq, rate_sel);
	*pll_ctrl_reg1 = pll_make_ctrl_reg1(vco_freq, pllmode, rate_sel);
	*div_sel_out = div_sel;

	return 0;
}

static void dpu_disable_dsipll(struct spacemit_crtc *a_crtc)
{
	if (a_crtc->split_en)
		return;

	/* disable pu */
	writel(0, a_crtc->dsipll_base + PLL_CTRL_REG2);
}

static void dpu_enable_dsipll(struct spacemit_crtc *a_crtc)
{
	u32 pll_ctrl_reg0;
	u32 pll_ctrl_reg1;
	u32 div_sel;
	u32 value;
	unsigned int timeout = 100;

	void __iomem *dsi1_base = NULL;

	if (a_crtc->split_en) {
		dsi1_base = ioremap(DSI1_BASE_ADDR, 0x200);
		if (!dsi1_base) {
			DRM_ERROR("%s: failed to ioremap dsi1 base\n", __func__);
			return;
		}
	}

	int ret;
	u32 bitclock = a_crtc->bitclk/1000000; /* MHz */

	/* not touch reg if running */
	if (readl(a_crtc->dsipll_base + PLL_CTRL_REG2) & PLL_UP)
		return;

	ret = spacemit_calc_pll_regs(bitclock,
				     &pll_ctrl_reg0,
				     &pll_ctrl_reg1,
				     &div_sel);
	if (ret) {
		DRM_ERROR("DSI PLL calc failed, bitclock=%u\n", bitclock);
		return;
	}

	a_crtc->dsipll_reg0 = pll_ctrl_reg0;
	a_crtc->dsipll_reg1 = pll_ctrl_reg1;

	writel(pll_ctrl_reg0, a_crtc->dsipll_base + PLL_CTRL_REG0);

	writel(pll_ctrl_reg1, a_crtc->dsipll_base + PLL_CTRL_REG1);

	/* Apply div_sel to bits[30:29], keep other bits from dsipll_reg2 */
	value = (a_crtc->dsipll_reg2 & ~(3U << 29)) | (div_sel << 29);
	writel(value, a_crtc->dsipll_base + PLL_CTRL_REG2);
	if (a_crtc->split_en) {
		writel(pll_ctrl_reg0, a_crtc->dsi1pll_base + PLL_CTRL_REG0);
		writel(pll_ctrl_reg1, a_crtc->dsi1pll_base + PLL_CTRL_REG1);
		writel(value, a_crtc->dsi1pll_base + PLL_CTRL_REG2);
	}

	if ((a_crtc->dsipll_reg1 & (BIT(29) | BIT(30))) != 0)
		dpu_set_bit(a_crtc->dsipll_base, DSI_PHY_ANA_CTRL1, BIT(28));
	else
		dpu_clr_bit(a_crtc->dsipll_base, DSI_PHY_ANA_CTRL1, BIT(28));

	/* pu */
	value |= PLL_UP;
	writel(value, a_crtc->dsipll_base + PLL_CTRL_REG2);
	if (a_crtc->split_en)
		writel(value, a_crtc->dsi1pll_base + PLL_CTRL_REG2);

	/* wait pll lock */
	while (true) {
		value = readl(a_crtc->dsipll_base + PLL_CTRL_REG3);

		if (value & PLL_LK) {
			DRM_DEBUG("bitclk pll locked\n");
			break;
		}

		if (timeout == 0) {
			DRM_ERROR("failed to got bitclk pll lock 0x%x\n", value);
			break;
		}

		timeout--;

		udelay(10);
	}
}

/* dove: hclk, pxclk, mclk, escclk, bitclk
 * lark: hclk, pxclk, mclk, escclk, dsipll, aclk
 * dovenr/larkpro: hclk, pxclk, mclk, escclk, dsipll, aclk, dscclk
 */
static int dpu_enable_clocks(struct spacemit_crtc *a_crtc)
{
	struct dpu_clk_context *clk_ctx = &a_crtc->clk_ctx;
	struct drm_crtc *crtc = &a_crtc->crtc;
	struct drm_display_mode *mode = &crtc->mode;
	uint64_t clk_val;
	uint64_t set_clk_val;
	bool dpu_online_enabled = false;

#ifdef CONFIG_SOC_SPACEMIT_K3_FPGA
	return 0;
#endif
	if (a_crtc->is_offline_mode)
		dpu_online_enabled = __clk_is_enabled(clk_ctx->pxclk);

	if (clk_ctx->hclk)
		clk_prepare_enable(clk_ctx->hclk);

	if (clk_ctx->pxclk) {
		clk_prepare_enable(clk_ctx->pxclk);

		if (dpu_online_enabled == false) {
			if (a_crtc->dsc_regs)
				set_clk_val = a_crtc->dsc_pxclk ;  /* dsc mode pxclk */
			else
				set_clk_val = mode->clock * 1000;

			if (set_clk_val) {
				set_clk_val = clk_round_rate(clk_ctx->pxclk, set_clk_val);
				clk_val = clk_get_rate(clk_ctx->pxclk);
				if (clk_val != set_clk_val) {
					clk_set_rate(clk_ctx->pxclk, set_clk_val);
					DRM_DEBUG("pxclk=%lld\n", clk_val);
				}
			}
		}
	}

	if (clk_ctx->mclk) {
		clk_prepare_enable(clk_ctx->mclk);

		clk_val = clk_get_rate(clk_ctx->mclk);
		if (clk_val != DPU_MCLK_DEFAULT) {
			clk_val = clk_round_rate(clk_ctx->mclk, DPU_MCLK_DEFAULT);
			clk_set_rate(clk_ctx->mclk, clk_val);
			DRM_DEBUG("mclk=%lld\n", clk_val);
		}
	}

	if (clk_ctx->escclk) {
		clk_prepare_enable(clk_ctx->escclk);

		clk_val = clk_get_rate(clk_ctx->escclk);
		set_clk_val = a_crtc->escclk;
		if (clk_val != set_clk_val) {
			clk_val = clk_round_rate(clk_ctx->escclk, set_clk_val);
			clk_set_rate(clk_ctx->escclk, clk_val);
			DRM_DEBUG("escclk=%lld\n", clk_val);
		}
	}

	if (clk_ctx->bitclk) {
		clk_prepare_enable(clk_ctx->bitclk);

		clk_val = clk_get_rate(clk_ctx->bitclk);
		set_clk_val = a_crtc->bitclk;
		if (clk_val != set_clk_val) {
			clk_val = clk_round_rate(clk_ctx->bitclk, set_clk_val);
			clk_set_rate(clk_ctx->bitclk, clk_val);
			DRM_DEBUG("bitclk=%lld\n", clk_val);
		}
	}

	if (clk_ctx->aclk) {
		clk_prepare_enable(clk_ctx->aclk);

		clk_val = clk_get_rate(clk_ctx->aclk);
		set_clk_val = a_crtc->aclk;
		DRM_INFO("Current aclk rate: %llu, Target: %llu\n", clk_val, set_clk_val);

		if (clk_val != set_clk_val) {
			clk_val = clk_round_rate(clk_ctx->aclk, set_clk_val);
			clk_set_rate(clk_ctx->aclk, clk_val);
			DRM_DEBUG("aclk=%lld\n", clk_val);
		}
	}

	if (a_crtc->dsipll_valid)
		dpu_enable_dsipll(a_crtc);

	if (clk_ctx->dscclk) {
		clk_prepare_enable(clk_ctx->dscclk);

		if (a_crtc->dsc_regs && a_crtc->dsc_x_slice && a_crtc->dsc_clk) {
			clk_val = clk_get_rate(clk_ctx->dscclk);
			if (clk_val != a_crtc->dsc_clk) {
				clk_val = clk_round_rate(clk_ctx->dscclk, a_crtc->dsc_clk);
				clk_set_rate(clk_ctx->dscclk, clk_val);
				DRM_DEBUG("dscclk=%lld\n", clk_val);
			}
		}
	}

#if IS_ENABLED(CONFIG_SPACEMIT_NI700)
	ni700_power_on(SPACEMIT_NI700_LCD);
#endif

	trace_dpu_enable_clocks(a_crtc->dev_id);
	return 0;
}

static int dpu_disable_clocks(struct spacemit_crtc *a_crtc)
{
	struct dpu_clk_context *clk_ctx = &a_crtc->clk_ctx;

#ifdef CONFIG_SOC_SPACEMIT_K3_FPGA
	return 0;
#endif
	trace_dpu_disable_clocks(a_crtc->dev_id);

#if IS_ENABLED(CONFIG_SPACEMIT_NI700)
	ni700_power_off(SPACEMIT_NI700_LCD);
#endif

	if (clk_ctx->hclk)
		clk_disable_unprepare(clk_ctx->hclk);

	if (clk_ctx->pxclk)
		clk_disable_unprepare(clk_ctx->pxclk);

	if (clk_ctx->mclk)
		clk_disable_unprepare(clk_ctx->mclk);

	if (clk_ctx->escclk)
		clk_disable_unprepare(clk_ctx->escclk);

	if (clk_ctx->bitclk)
		clk_disable_unprepare(clk_ctx->bitclk);

	if (a_crtc->dsipll_valid)
		dpu_disable_dsipll(a_crtc);

	if (clk_ctx->aclk)
		clk_disable_unprepare(clk_ctx->aclk);

	if (clk_ctx->dscclk)
		clk_disable_unprepare(clk_ctx->dscclk);

	return 0;
}

u8 spacemit_plane_hw_get_format_id(u32 format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(primary_fmts); i++) {
		if (primary_fmts[i].format == format)
			return primary_fmts[i].id;
	}

	return SPACEMIT_DPU_INVALID_FORMAT_ID;
}

void spacemit_get_afbc_modifier(uint64_t modifier, struct spacemit_afbc_state *afbc_state)
{
	uint64_t super_block_size = modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK;

	if (super_block_size == AFBC_FORMAT_MOD_BLOCK_SIZE_16x16)
		afbc_state->block_size = 0;
	else if (super_block_size == AFBC_FORMAT_MOD_BLOCK_SIZE_32x8)
		afbc_state->block_size = 1;

	if (modifier & AFBC_FORMAT_MOD_TILED)
		afbc_state->tile_type = 1;
	else
		afbc_state->tile_type = 0;

	if (modifier & AFBC_FORMAT_MOD_YTR)
		afbc_state->yuv_transform = 1;
	else
		afbc_state->yuv_transform = 0;

	if (modifier & AFBC_FORMAT_MOD_SPLIT)
		afbc_state->split_mode = 1;
	else
		afbc_state->split_mode = 0;

	if (modifier & AFBC_FORMAT_MOD_CBR)
		afbc_state->copy_mode = 1;
	else
		afbc_state->copy_mode = 0;

}

static void saturn_enable_vsync(struct spacemit_crtc *a_crtc, bool enable)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	trace_saturn_enable_vsync("vsync", enable);
	hwdev->enable_vsync(a_crtc, hwdev, enable);
}

void spacemit_cfg_rdy_timer_handler(struct timer_list *t)
{
	struct spacemit_crtc *a_crtc = timer_container_of(a_crtc, t, cfg_rdy_timer);
#ifdef CONFIG_SPACEMIT_DEBUG
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
#endif

	DRM_ERROR("CFG_RDY ERROR:%s\n", __func__);
	trace_saturn_ctrl_cfg_ready_timer(a_crtc->dev_id);
	//flip_done is used for online
	if (!a_crtc->is_offline_mode)
		a_crtc->flip_done = false;
#ifdef CONFIG_SPACEMIT_DEBUG
	hwdev->dpu_dump_reg(a_crtc);
	//logger_noti_helper(3);
#endif
}

static void saturn_ctrl_cfg_ready(struct spacemit_crtc *a_crtc, bool enable)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	if (a_crtc->is_stopped)
		return;

	trace_saturn_ctrl_cfg_ready(a_crtc->dev_id, enable);
	mod_timer(&a_crtc->cfg_rdy_timer, jiffies + msecs_to_jiffies(3000));
	hwdev->cfg_ready(a_crtc, hwdev);

}

static void saturn_ctrl_sw_start(struct spacemit_crtc *a_crtc, bool enable)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	if (a_crtc->is_offline_mode == 1)
		return;

	trace_saturn_ctrl_sw_start(a_crtc->dev_id, enable);

	hwdev->sw_start(a_crtc, hwdev);
}

static u32 dpu_get_version(struct spacemit_crtc *a_crtc)
{
	return 0;
}

static void saturn_ctrl_cmd_update(struct spacemit_crtc *a_crtc, bool enable)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	if (a_crtc->out_mode == DPU_OUT_MODE_CMD) {
		trace_saturn_ctl_cmd_update(a_crtc->dev_id, enable);
		if (hwdev->cmd_update)
			hwdev->cmd_update(a_crtc, hwdev);
	}
}

static int dpu_init(struct spacemit_crtc *a_crtc)
{
	unsigned int timeout = 1000;
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	if (!a_crtc->power_on)
		return 0;

	trace_dpu_init(a_crtc->dev_id);

	while (timeout) {
		if (hwdev->get_cfg_rdy(a_crtc, hwdev) == 0)
			break;
		udelay(100);
		timeout--;
	};
	if (timeout == 0)
		DRM_ERROR("%s wait cfg ready done timeout\n", __func__);

	hwdev->dpu_init(a_crtc);

	return 0;
}

static void dpu_uninit(struct spacemit_crtc *a_crtc)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	if (!a_crtc->power_on)
		return;

	trace_dpu_uninit(a_crtc->dev_id);
	hwdev->irq_enable(a_crtc, false);
}

static void dpu_pm_update_commit_qos(struct spacemit_crtc *a_crtc)
{
#ifdef CONFIG_PM
	if (a_crtc->out_mode == DPU_OUT_MODE_CMD && a_crtc->lpm_commit_qos) {
		if (a_crtc->lpm_period) {
			/* put lpm qos in delay a_crtc->lpm_period time */
			schedule_delayed_work(&a_crtc->lpm_qos_work,
					msecs_to_jiffies(a_crtc->lpm_period));
			a_crtc->lpm_work_pending = true;
		} else {
			dpu_rpm_suspend(a_crtc);
		}
	}
#endif
}

static inline void dpu_isr_vblank(struct spacemit_crtc *a_crtc, bool *flip)
{
	struct drm_crtc *crtc = &a_crtc->crtc;

	drm_crtc_handle_vblank(crtc);
	if (!*flip) {
		struct drm_device *drm = a_crtc->crtc.dev;
		struct drm_pending_vblank_event *event = crtc->state->event;

		*flip = true;
		spin_lock(&drm->event_lock);
		if (crtc->state->event) {
			/*
			 * Set event to NULL first to ensure event is consumed
			 * before drm_atomic_helper_commit_hw_done.
			 */
			crtc->state->event = NULL;
			drm_crtc_send_vblank_event(crtc, event);
		}
		spin_unlock(&drm->event_lock);
		drm_crtc_vblank_put(crtc);
		dpu_pm_update_commit_qos(a_crtc);
	}
}

u32 saturn_conf_dpuctrl_rdma(struct spacemit_crtc *a_crtc)
{
	struct drm_crtc *crtc = &a_crtc->crtc;
	struct drm_plane *plane;
	u32 rdma_en = 0;

	/* Find out the active rdmas */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		u32 rdma_id = to_spacemit_plane_state(plane->state)->rdma_id;

		if (rdma_id != RDMA_INVALID_ID)
			rdma_en |= (1 << rdma_id);
	}

	trace_dpuctrl("rdma_en", rdma_en);

	return rdma_en;
}

static uint32_t dpu_online_isr(struct spacemit_crtc *a_crtc)
{
	uint32_t irq_raw, irq_bit, irq_ur_bit, irq_dbg_sts;
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	static bool ur_done;
	struct drm_writeback_connector *wb_conn = priv->wb_connector[a_crtc->wb_id];
	int dev_id = a_crtc->dev_id;

#ifdef CONFIG_SPACEMIT_DEBUG
	unsigned long flags;
#endif

	trace_dpu_isr(a_crtc->dev_id);

	irq_raw = hwdev->get_int_sts(hwdev, dev_id);
	trace_dpu_isr_status("ONLINE", irq_raw);
	/* underrun */
	irq_bit = hwdev->get_irq_bit(INT_UNDERRUN, dev_id);
	irq_ur_bit = irq_raw & irq_bit;
	if (irq_ur_bit && !ur_done) {
		hwdev->clr_int_sts(a_crtc, irq_ur_bit, dev_id);
		trace_dpu_isr_status("Under Run!", irq_ur_bit);
		trace_dpu_isr_ul_data("DPU Mclk", a_crtc->cur_mclk);
		trace_dpu_isr_ul_data("DPU BW", a_crtc->cur_bw);
		queue_work(a_crtc->dpu_trace_wq, &a_crtc->work_dpu_trace);
#ifdef CONFIG_SPACEMIT_DEBUG
		hwdev->dpu_dump_reg(a_crtc);
#endif
		DRM_ERROR_RATELIMITED("Under Run! DPU_Mclk = %ld, DPU BW = %ld\n", (unsigned long)a_crtc->cur_mclk, (unsigned long)a_crtc->cur_bw);
#if IS_ENABLED(CONFIG_SPACEMIT_CORE_CLK_DOVE) || IS_ENABLED(CONFIG_SPACEMIT_CORE_CLK_LARK)
		DRM_ERROR_RATELIMITED("DDR Freq = %d\n", ddr_get_freq_lv());
#endif
#ifdef CONFIG_SPACEMIT_DEBUG
		hwdev->dpu_dump_rdma_status(priv);
#endif
		ur_done = true;
	}
	/* cfg ready clear */
	irq_bit = hwdev->get_irq_bit(INT_CFG_RDY, dev_id);
	if (irq_raw & irq_bit) {
		hwdev->clr_int_sts(a_crtc, irq_bit, dev_id);
		trace_dpu_isr_status("cfg_rdy_clr", irq_raw & irq_bit);
		a_crtc->flip_done = false;
		timer_delete(&a_crtc->cfg_rdy_timer);
		ur_done = false;
		if (hwdev->enable_cfg_irq)
			hwdev->enable_cfg_irq(a_crtc, hwdev, false);
		trace_u64_data("irq crtc mclk cur", a_crtc->cur_mclk);
		trace_u64_data("irq crtc mclk new", a_crtc->new_mclk);
		trace_u64_data("irq crtc bw cur", a_crtc->cur_bw);
		trace_u64_data("irq crtc bw new", a_crtc->new_bw);
		if (a_crtc->enable_auto_fc && a_crtc->new_mclk < a_crtc->cur_mclk && a_crtc->fix_max_mclk == false) {
			trace_u64_data("run wq to set mclk", a_crtc->new_mclk);
			queue_work(system_wq, &a_crtc->work_update_clk);
		}
		if (a_crtc->enable_auto_fc && a_crtc->new_bw < a_crtc->cur_bw) {
			trace_u64_data("run wq to set bw", a_crtc->new_bw);
			queue_work(system_wq, &a_crtc->work_update_bw);
		}
	}
	/* vsync */
	irq_bit = hwdev->get_irq_bit(INT_VSYNC, dev_id);
	if (irq_raw & irq_bit) {
		hwdev->clr_int_sts(a_crtc, irq_bit, dev_id);
		trace_dpu_isr_status("vsync", irq_raw & irq_bit);
		dpu_isr_vblank(a_crtc, &a_crtc->flip_done);
		dpu_dump_fps(a_crtc);
#ifdef CONFIG_SPACEMIT_DEBUG
		spin_lock_irqsave(&priv->ur_dump_lock, flags);
		priv->underrun_debug = false;
		priv->old_state = NULL;
		spin_unlock_irqrestore(&priv->ur_dump_lock, flags);
#endif
	}
	/* wb done */
	irq_bit = hwdev->get_irq_bit(INT_WB_DONE, dev_id);
	if (irq_raw & irq_bit) {
		hwdev->clr_int_sts(a_crtc, irq_bit, dev_id);

		if (hwdev->is_wb_en(a_crtc, hwdev)) {
			trace_dpu_isr_status("wb", irq_raw & irq_bit);
			hwdev->wb_disable(a_crtc);
			drm_writeback_signal_completion(wb_conn, 0);
		}
	}
	/* rest irq status */
	irq_bit = hwdev->get_irq_bit(INT_REST, dev_id) | irq_ur_bit;
	if (irq_raw & irq_bit)
		hwdev->clr_int_sts(a_crtc, irq_bit, dev_id);

	/* vsync update clear */
	irq_bit = hwdev->get_irq_bit(INT_VSYNC_UPDATE, dev_id);
	if (irq_raw & irq_bit)
		hwdev->clr_int_sts(a_crtc, irq_bit, dev_id);

	if (hwdev->get_rdma_dbg_sts) {
		irq_dbg_sts = hwdev->get_rdma_dbg_sts(a_crtc, dev_id);
		if (irq_dbg_sts) {
			trace_dpu_isr_status("ONLINE INT DBG STS", irq_dbg_sts);
			queue_work(a_crtc->dpu_trace_wq, &a_crtc->work_dpu_trace);
		}
	}

	return 0;
}

static uint32_t dpu_offline_isr(struct spacemit_crtc *a_crtc)
{
	uint32_t irq_raw, irq_bit;
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	static bool wb_flip_done;
	struct drm_writeback_connector *wb_conn = priv->wb_connector[a_crtc->wb_id];
	int dev_id = a_crtc->dev_id;

	trace_dpu_isr(a_crtc->dev_id);
	irq_raw = hwdev->get_int_sts(hwdev, dev_id);
	trace_dpu_isr_status("OFFLINE", irq_raw);
	/* cfg ready clear */
	irq_bit = hwdev->get_irq_bit(OFF_CFG_RDY, dev_id);
	if (irq_raw & irq_bit) {
		hwdev->clr_int_sts(a_crtc, irq_bit, dev_id);
		trace_dpu_isr_status("cfg_rdy_clr", irq_raw & irq_bit);
		wb_flip_done = false;
		timer_delete(&a_crtc->cfg_rdy_timer);

		if (hwdev->enable_cfg_irq)
			hwdev->enable_cfg_irq(a_crtc, hwdev, false);
		if (saturn_conf_dpuctrl_rdma(a_crtc) == 0)
			dpu_isr_vblank(a_crtc, &wb_flip_done);
		trace_u64_data("irq crtc mclk cur", a_crtc->cur_mclk);
		trace_u64_data("irq crtc mclk new", a_crtc->new_mclk);
		trace_u64_data("irq crtc bw cur", a_crtc->cur_bw);
		trace_u64_data("irq crtc bw new", a_crtc->new_bw);
		if (a_crtc->enable_auto_fc && a_crtc->new_mclk < a_crtc->cur_mclk && a_crtc->fix_max_mclk == false) {
			trace_u64_data("run wq to set mclk", a_crtc->new_mclk);
			queue_work(system_wq, &a_crtc->work_update_clk);
		}
		if (a_crtc->enable_auto_fc && a_crtc->new_bw < a_crtc->cur_bw) {
			trace_u64_data("run wq to set bw", a_crtc->new_bw);
			queue_work(system_wq, &a_crtc->work_update_bw);
		}
	}
	/* wb done */
	irq_bit = hwdev->get_irq_bit(OFF_WB_DONE, dev_id);
	if (irq_raw & irq_bit) {
		trace_dpu_isr_status("wb", irq_raw & irq_bit);
		hwdev->clr_int_sts(a_crtc, irq_bit, dev_id);
		dpu_isr_vblank(a_crtc, &wb_flip_done);
		drm_writeback_signal_completion(wb_conn, 0);
	}
	/* rest irq status */
	irq_bit = hwdev->get_irq_bit(OFF_REST, dev_id);
	if (irq_raw & irq_bit)
		hwdev->clr_int_sts(a_crtc, irq_bit, dev_id);
	return 0;
}

static void dpu_run(struct drm_crtc *crtc,
		    struct drm_crtc_state *old_state)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct spacemit_dsi_vrr_param vrr_param = {0};

	trace_dpu_run(a_crtc->dev_id);

	/* config dpuctrl modules */
	if (hwdev->conf_dpuctrl)
		hwdev->conf_dpuctrl(crtc, old_state);

	if (!a_crtc->is_offline_mode &&
		a_crtc->dsi && a_crtc->dsi->core) {
		if (a_crtc->dsi->core->dsi_update_vrr) {
			vrr_param.vrr_vfp = a_crtc->vrr_vfp;
			a_crtc->dsi->core->dsi_update_vrr(&a_crtc->dsi->ctx, &vrr_param);
		}
	}

	//dsb(sy);
	mb();

	drm_crtc_vblank_get(crtc);
	if (hwdev->enable_cfg_irq)
		hwdev->enable_cfg_irq(a_crtc, hwdev, true);

	saturn_ctrl_cfg_ready(a_crtc, true);

	saturn_ctrl_cmd_update(a_crtc, true);

	if (unlikely(a_crtc->is_1st_f)) {
		a_crtc->is_1st_f = false;
		saturn_ctrl_sw_start(a_crtc, true);
		DRM_INFO("DPU %d Start!\n", a_crtc->dpu_id);
	}
#ifdef CONFIG_ARM64
	__iomb();
#else
	dma_rmb();
#endif
}

static void dpu_esd_restart(struct spacemit_crtc *a_crtc)
{
	unsigned int timeout = DPU_STOP_TIMEOUT;
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct drm_crtc *crtc = &a_crtc->crtc;

	DRM_INFO("%s()\n", __func__);

	while (timeout) {
		if (hwdev->get_cfg_rdy(a_crtc, hwdev) == 0)
			break;
		udelay(10);
		timeout--;
	};
	if (timeout == 0)
		DRM_ERROR("%s dpu %d wait cfg ready done timeout\n", __func__, a_crtc->dev_id);
	else
		DRM_DEBUG("%s dpu %d wait cfg ready done %d\n", __func__, timeout, a_crtc->dev_id);

	hwdev->dpu_restart(a_crtc, hwdev);
	drm_crtc_vblank_get(crtc);
	if (hwdev->enable_cfg_irq)
		hwdev->enable_cfg_irq(a_crtc, hwdev, true);
	//dsb(sy);
	mb();
	hwdev->cfg_ready(a_crtc, hwdev);
	hwdev->sw_start(a_crtc, hwdev);
#ifdef CONFIG_ARM64
	__iomb();
#else
	dma_rmb();
#endif

	timeout = hwdev->reboot_flag ? DPU_STOP_REBOOT_TIMEOUT : DPU_STOP_TIMEOUT;
	while (timeout) {
		if (hwdev->get_cfg_rdy(a_crtc, hwdev) & 1) {
			udelay(10);
			timeout--;
			continue;
		} else
			break;
	};
	if (timeout == 0)
		DRM_ERROR("%s esd_restart timeout!!!\n", __func__);
	else
		DRM_DEBUG("%s esd_restart done %d\n", __func__, timeout);
	hwdev->irq_enable(a_crtc, 1);
}

static void dpu_stop(struct spacemit_crtc *a_crtc)
{
	unsigned int timeout = DPU_STOP_TIMEOUT;
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	int ret = 0;

	if (!a_crtc->power_on)
		return;

	DRM_INFO("%s()\n", __func__);

	trace_dpu_stop(a_crtc->dev_id);

	hwdev->irq_enable(a_crtc, 0);
	while (timeout) {
		if (hwdev->get_cfg_rdy(a_crtc, hwdev) == 0)
			break;
		udelay(10);
		timeout--;
	};
	if (timeout == 0) {
		DRM_ERROR("%s dpu %d wait cfg ready done timeout\n", __func__, a_crtc->dev_id);
		a_crtc->stop_to++;
	} else
		DRM_DEBUG("%s dpu %d wait cfg ready done %d\n", __func__, timeout, a_crtc->dev_id);

	hwdev->dpu_disable(a_crtc, hwdev);
	//dsb(sy);
	mb();
	hwdev->cfg_ready(a_crtc, hwdev);
#ifdef CONFIG_ARM64
	__iomb();
#else
	dma_rmb();
#endif

	timeout = hwdev->reboot_flag ? DPU_STOP_REBOOT_TIMEOUT : DPU_STOP_TIMEOUT;
	while (timeout) {
		if (hwdev->get_cfg_rdy(a_crtc, hwdev) & 1) {
			udelay(10);
			timeout--;
			continue;
		} else
			break;
	};
	ret = hwdev->dpu_stop_check(a_crtc, hwdev);
	if (ret) {
		DRM_ERROR("dpu %d stop check failed!!!\n", a_crtc->dev_id);
		a_crtc->stop_to++;
	}
	if (timeout == 0) {
		DRM_ERROR("%s dpu %d stop timeout!!!\n", __func__, a_crtc->dev_id);
		a_crtc->stop_to++;
		pm_stay_awake(a_crtc->dev);
	} else {
		DRM_DEBUG("%s dpu %d stop Done %d\n", __func__, a_crtc->dev_id, timeout);
		if (a_crtc->stop_to) {
			pm_relax(a_crtc->dev);
			a_crtc->stop_to = 0;
		}
	}

}

static int dpu_modeset(struct spacemit_crtc *a_crtc, struct drm_mode_modeinfo *mode)
{
	return 0;
}

static void dpu_enable_vsync(struct spacemit_crtc *a_crtc)
{
	saturn_enable_vsync(a_crtc, true);
}

static void dpu_disable_vsync(struct spacemit_crtc *a_crtc)
{
	saturn_enable_vsync(a_crtc, false);
}

static void __maybe_unused dpu_rpm_suspend(struct spacemit_crtc *a_crtc)
{
	if (!a_crtc->rpm_status)
		return;

	DRM_DEBUG("%s\n", __func__);

	dpu_stop(a_crtc);

	if (!a_crtc->is_offline_mode &&
		a_crtc->dsi && a_crtc->dsi->core) {
		mutex_lock(&a_crtc->dsi->disable_lock);
		if (a_crtc->dsi->core->dsi_close_datatx)
			a_crtc->dsi->core->dsi_close_datatx(&a_crtc->dsi->ctx);

		if (a_crtc->dsi->core->dsi_close)
			a_crtc->dsi->core->dsi_close(&a_crtc->dsi->ctx);
		mutex_unlock(&a_crtc->dsi->disable_lock);
	}

	dpu_uninit(a_crtc);

	drm_crtc_vblank_off(&a_crtc->crtc);

	if (!a_crtc->is_offline_mode)
		spacemit_dpu_power_enable(a_crtc, false);

	a_crtc->rpm_status = false;
}

static void __maybe_unused dpu_rpm_resume(struct spacemit_crtc *a_crtc)
{
#ifdef CONFIG_PM
	if (a_crtc->lpm_work_pending && a_crtc->lpm_period) {
		cancel_delayed_work_sync(&a_crtc->lpm_qos_work);
		a_crtc->lpm_work_pending = false;
	}
#endif
	if (a_crtc->rpm_status)
		return;

	DRM_DEBUG("%s\n", __func__);

	if (!a_crtc->is_offline_mode)
		spacemit_dpu_power_enable(a_crtc, true);

	drm_crtc_vblank_on(&a_crtc->crtc);

	dpu_init(a_crtc);
	a_crtc->is_1st_f = true;

	if (!a_crtc->is_offline_mode &&
		a_crtc->dsi && a_crtc->dsi->core) {
		if (a_crtc->dsi->core->dsi_open)
			a_crtc->dsi->core->dsi_open(&a_crtc->dsi->ctx, false);

		if (a_crtc->dsi->core->dsi_ready_for_datatx)
			a_crtc->dsi->core->dsi_ready_for_datatx(&a_crtc->dsi->ctx);
	}

	a_crtc->rpm_status = true;
}

static void dpu_begin(struct spacemit_crtc *a_crtc)
{
#ifdef CONFIG_PM
	if (a_crtc->out_mode == DPU_OUT_MODE_CMD && a_crtc->lpm_commit_qos) {
		dpu_rpm_resume(a_crtc);
	}
#endif
}

static struct dpu_core_ops dpu_saturn_ops = {
	.parse_dt = dpu_parse_dt,
	.version = dpu_get_version,
	.init = dpu_init,
	.uninit = dpu_uninit,
	.run = dpu_run,
	.stop = dpu_stop,
	.esd_restart = dpu_esd_restart,
	.online_isr = dpu_online_isr,
	.offline_isr = dpu_offline_isr,
	.modeset = dpu_modeset,
	.enable_clk = dpu_enable_clocks,
	.disable_clk = dpu_disable_clocks,
	.update_clk = dpu_update_clocks,
	.update_bw = dpu_update_bw,
	.enable_vsync = dpu_enable_vsync,
	.disable_vsync = dpu_disable_vsync,
	.cal_layer_fbcmem_size = saturn_cal_layer_fbcmem_size,
	.calc_plane_mclk_bw = dpu_calc_plane_mclk_bw,
	.adjust_rdma_fbcmem = saturn_adjust_rdma_fbcmem,
	.begin = dpu_begin,
};

static struct ops_entry entry = {
	.ver = "spacemit-saturn",
	.ops = &dpu_saturn_ops,
};

#ifndef MODULE
static int __init dpu_core_register(void)
#else
int dpu_core_register(void)
#endif
{
	return dpu_core_ops_register(&entry);
}
#ifndef MODULE
subsys_initcall(dpu_core_register);
#endif
MODULE_LICENSE("GPL v2");
