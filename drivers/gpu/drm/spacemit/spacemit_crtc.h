/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_CRTC_H_
#define _SPACEMIT_CRTC_H_

#include <linux/delay.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <video/videomode.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_uapi.h>
#include <drm/drm_print.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_vblank.h>
#include <drm/drm_writeback.h>
#include <dt-bindings/display/spacemit_dpu.h>
#include "dpu/saturn_regs/reg_map.h"
#include "spacemit_cmdlist.h"
#include "spacemit_drm.h"
#include "spacemit_lib.h"
#include "../drm_crtc_internal.h"
#include <linux/spinlock.h>
#include "spacemit_dsi.h"

#define SPACEMIT_DPU_LPM_PERIOD_MIN_MS	(50)
#define SPACEMIT_DPU_LPM_PERIOD_MAX_MS	(5000)

#define DMA_TOP_ARB_DEBUG_INFO0_DFT	0x20003000
#define CMDLIST_CH_DBG_CH_CS_STS	0x11
#define CMDLIST_CH_DBG_CH_CS_MASK	0xff
#define CMDLIST_CH_DBG_SIZE		0x7

#define N_SCALER_MAX			5
#define N_DMA_CHANNEL_MAX		12
#define N_DMA_LAYER_MAX			16
#define N_COMPOSER_MAX			4
#define N_COMPOSER_LAYER_MAX		16
#define N_PANEL_MAX			3
#define N_OUTCTRL_MAX			3
#define N_WRITEBACK_MAX			2
#define N_DISPLAY_MAX			5
#define N_LUT3D_MAX			3
#define N_CMDLIST_MAX			14

#define MHZ2HZ	1000000ULL
#define MHZ2KHZ	1000ULL
#define DPU_QOS_REQ			1100000
#define DPU_MIN_QOS_REQ			331070976
#define DPU_MAX_QOS_REQ			(3900000ULL)
#define RDMA_INVALID_ID			(~0)
#define SCALER_INVALID_ID		((u8)(~0))

#define DPU_STOP_TIMEOUT		(2000)
#define DPU_STOP_REBOOT_TIMEOUT		(150000)
#define DPU_CTRL_MAX_TIMING_INTER1	(0xf)

#define DPU_DSCCLK_DEFAULT	307200000
#define DPU_PXCLK_DEFAULT	98000000
#define DPU_MCLK_DEFAULT	307200000
#define DPU_MCLK_MIN		40960000
#define DPU_AXICLK_DEFAULT	409000000
#define DPU_ESCCLK_DEFAULT	52000000
#define DPU_BITCLK_DEFAULT	624000000
#define DPU_DSIPLL_REG0_DEFAULT	0x8010c563
#define DPU_DSIPLL_REG1_DEFAULT	0x0bcec4ec
#define DPU_DSIPLL_REG2_DEFAULT	0x800078a0

#define CMDLIST_ADDRL_ALIGN_BITS		(4) //From cmdlist_reg_0[] in CMDLIST_REG
#define CMDLIST_ADDRL_ALIGN_MASK		((u32)(~(BIT(CMDLIST_ADDRL_ALIGN_BITS) - 1)))

#define MAX_SCALER_NUMS		4
struct spacemit_crtc_scaler {
	u32 rdma_id;
	u32 in_use;
};

enum spacemit_dpu_irq {
	INT_UNDERRUN,
	INT_CFG_RDY,
	INT_VSYNC,
	INT_EOF,
	INT_WB_DONE,
	INT_REST,
	OFF_CFG_RDY,
	OFF_WB_DONE,
	OFF_REST,
	INT_VSYNC_UPDATE,
	INT_DEBUG,
};

enum rdma_mode {
	UP_DOWN = BIT(0),
	LEFT_RIGHT = BIT(1),
};

struct spacemit_crtc_fbcmem {
	u32 start;
	u32 size;
	bool map;
};

struct spacemit_crtc_rdma {
	enum rdma_mode mode;
	struct spacemit_crtc_fbcmem fbcmem;
	bool in_use;
	bool is_offline_mode;
	u32 use_cnt;
};

struct dpu_mmu_tbl {
	u32 size;
	void *va;
	dma_addr_t pa;
};

struct dpu_clk_context {
	struct clk *pxclk;
	struct clk *mclk;
	struct clk *hclk;
	struct clk *escclk;
	struct clk *bitclk;
	struct clk *aclk;
	struct clk *dscclk;
};

#define DEFAULT_SLICE_WIDTH	512
#define MAX_WIDTH		4096
#define MAX_SLICE	(MAX_WIDTH / DEFAULT_SLICE_WIDTH)
#define DPU_NO_SLICE (0xFF)
struct slice_cfg {
	u32 wb_in_w;
	u32 wb_in_h;
	u32 wb_out_x;
	u32 wb_out_y;
	u32 wb_out_w;
	u32 wb_out_h;
	u32 rdma_w;
	u32 rdma_h;
	u32 rdma_x;
	u32 rdma_y;
	u32 crtc_w;
	u32 crtc_h;
	u32 crtc_x;
	u32 crtc_y;
};

struct spacemit_crtc {
	spinlock_t irq_msk_lock;
	struct device *dev;
	struct drm_crtc crtc;
	struct dpu_core_ops *core;
	struct workqueue_struct *dpu_trace_wq;
	struct work_struct work_dpu_trace;
	struct work_struct work_update_clk;
	struct work_struct work_update_bw;
	struct dpu_mmu_tbl mmu_tbl;
#ifdef CONFIG_PM
#if IS_ENABLED(CONFIG_SPACEMIT_DDR_FC)
	struct spacemit_bw_con *ddr_qos_cons;
#endif
	s32 lpm_commit_qos;
	u32 lpm_period;
	struct delayed_work lpm_qos_work;
	bool lpm_work_pending;
	s32 lpm_bl_qos;
#endif
	int dev_id;
	bool is_edp;
	int dpu_id;
	struct timer_list cfg_rdy_timer;
	int wb_id;
	struct slice_cfg slice_wb[MAX_SLICE];
	bool enable_dump_reg;
	bool enable_dump_fps;
	bool enable_auto_fc;
	struct timespec64 last_tm;

	bool is_1st_f;
	bool logo_booton;
	uint32_t vrr_vfp;
	struct dpu_clk_context clk_ctx;
	uint64_t new_mclk;		/* new frame mclk */
	uint64_t cur_mclk;		/* current frame mclk */
	unsigned int min_mclk;		/* min_mclk of board panel resolution */
	unsigned int max_mclk;		/* max_mclk of board panel resolution */
	uint64_t bw_margin;		/* bandwidth margin */
	uint64_t max_bw;		/* max bandwidth */
	bool fix_max_mclk;
	uint64_t new_bw;
	uint64_t cur_bw;
	struct drm_property *color_matrix_property;
	struct drm_property *gamma_table_property;
	struct drm_property *end_tone_mapping_property;
	struct drm_property *acad_property;
	struct drm_property *ee_property;

	struct drm_property *hw_info_property;
	struct drm_property *wb_property;
	uint32_t bitclk;
	uint32_t dsipll_reg0;
	uint32_t dsipll_reg1;
	uint32_t dsipll_reg2;
	void __iomem *dsipll_base;
	void __iomem *dsi1pll_base;
	bool dsipll_valid;
	uint32_t aclk;
	uint32_t escclk;
	uint32_t out_format;
	uint32_t out_mode; /* cmd or video */
	struct notifier_block max_mclk_nb;
	uint32_t stop_to;
	bool is_stopped;		/* skip dpu key regs when dpu_stopped */
	uint32_t outctrl_reg;
	uint32_t dpu_online_nml_rch_en;
	uint32_t dpu_online_nml_scl_en;
	uint32_t dpu_online_nml_outctl_en;
	uint32_t dpuctrl_ctl_nml_cmdlist_rch_en[N_CMDLIST_MAX];

	struct reset_control *mclk_reset;
	struct reset_control *lcd_reset;
	struct reset_control *esc_reset;
	struct reset_control *aclk_reset;
	struct reset_control *dsc_reset;

#ifdef CONFIG_SPACEMIT_DEBUG
	bool (*is_dpu_running)(struct spacemit_crtc *a_crtc);
	struct notifier_block nb;
	bool is_working;
#endif
    // Added for Android14(HWC3.x)
	struct drm_property *expected_present_time;
	/* add for cmdlist_v1 (led) */
	struct cmdlist_regs *cl_rdma;
	struct cmdlist_regs *cl_tbu;
	struct cmdlist_regs *cl_pq;

	unsigned int wb_pos;
	unsigned int wb_format;
	unsigned int is_offline_mode;
	unsigned int is_slice_mode;
	unsigned int slice_num;
	unsigned int split_en;
	struct drm_property *offline_mode_property;
	struct drm_property *post_scaler_property;
	struct drm_property *pp_color_temperature_property;
	struct drm_property *acad_status_property;
	struct drm_property *bl_save_status_property;

	/* dsc parameters */
	unsigned int dsc_pxclk;
	unsigned int dsc_clk;
	unsigned int dsc_x_slice;
	u32 *dsc_regs;
	int dsc_regs_len;

	u32 dither_mode;

	bool flip_done;
	bool is_oled;

	bool rpm_status;
	bool power_on;
	struct device_node *panel_node;
	struct device_node *dsi_node;
	struct spacemit_dsi *dsi;
};

extern struct list_head dpu_core_head;

static inline struct spacemit_crtc *to_spacemit_crtc(struct drm_crtc *crtc)
{
	return crtc ? container_of(crtc, struct spacemit_crtc, crtc) : NULL;
}

struct spacemit_hw_device;
struct dpu_core_ops {
	int (*parse_dt)(struct spacemit_crtc *a_crtc, struct device_node *np);
	u32 (*version)(struct spacemit_crtc *a_crtc);
	int (*init)(struct spacemit_crtc *a_crtc);
	void (*uninit)(struct spacemit_crtc *a_crtc);
	void (*run)(struct drm_crtc *crtc,
		    struct drm_crtc_state *old_state);
	void (*stop)(struct spacemit_crtc *a_crtc);
	void (*esd_restart)(struct spacemit_crtc *a_crtc);
	void (*flip)(struct spacemit_crtc *a_crtc);
	void (*disable_vsync)(struct spacemit_crtc *a_crtc);
	void (*enable_vsync)(struct spacemit_crtc *a_crtc);
	u32 (*online_isr)(struct spacemit_crtc *a_crtc);
	u32 (*offline_isr)(struct spacemit_crtc *a_crtc);
	int (*modeset)(struct spacemit_crtc *a_crtc, struct drm_mode_modeinfo *mode);
	int (*enable_clk)(struct spacemit_crtc *a_crtc);
	int (*disable_clk)(struct spacemit_crtc *a_crtc);
	int (*cal_layer_fbcmem_size)(struct drm_plane *plane,
				     struct drm_plane_state *state);
	int (*adjust_rdma_fbcmem)(struct spacemit_hw_device *hwdev,
				 struct spacemit_crtc_rdma *rdmas);
	int (*calc_plane_mclk_bw)(struct drm_plane *plane,
			struct drm_plane_state *state);
	int (*update_clk)(struct spacemit_crtc *a_crtc, uint64_t mclk);
	int (*update_bw)(struct spacemit_crtc *a_crtc, uint64_t bw);
	void (*begin)(struct spacemit_crtc *a_crtc);
};

#define dpu_core_ops_register(entry) \
	disp_ops_register(entry, &dpu_core_head)
#define dpu_core_ops_attach(str) \
	disp_ops_attach(str, &dpu_core_head)

int spacemit_crtc_run(struct drm_crtc *crtc,
		struct drm_crtc_state *old_state);
int spacemit_crtc_stop(struct spacemit_crtc *a_crtc);
int spacemit_dpu_esd_restart(struct spacemit_crtc *a_crtc);
bool dpu_mclk_exclusive_get(void);
void dpu_mclk_exclusive_put(void);
int dpu_max_mclk_notifier_register(struct notifier_block *nb);
int dpu_max_mclk_notifier_unregister(struct notifier_block *nb);
int dpu_max_mclk_notifier_call_chain(unsigned int mclk_rate);
void spacemit_crtc_fill_slice(struct spacemit_crtc *a_crtc, u32 width, u32 height);
int spacemit_crtc_calc_slices(struct spacemit_crtc *a_crtc, u32 width, u32 height);

struct spacemit_plane {
	struct drm_plane plane;
	struct spacemit_hw_device *hwdev;
	struct drm_property *rdma_id_property;
	struct drm_property *solid_color_property;
	struct drm_property *dec_lines_property;
	struct drm_property *hdr_coef_property;
	struct drm_property *scale_coef_property;
	u32 hw_pid;
};

#define MAX_CL_NUM	MAX_SLICE

struct spacemit_plane_state {
	struct drm_plane_state state;
	u32 rdma_id;
	u32 solid_color;
	u32 dec_lines;
	/*
	 * scaler id. As long as its rdma channel is identical with any
	 * one uses the scaler, it's set to the scaler's id, even if it
	 * doesn't really use the scaler.
	 */
	u8 scaler_id;
	/* hw format */
	u8 format;
	bool use_scl;
	bool is_offline; //to indicate rdma is offline
	bool right_image;
	u32 fbcmem_size;
	uint64_t mclk;	//DPU MCLK = MAX(Mclk, Aclk) of all planes
	uint64_t bw;	//BandWidth = SUM(BW_single) * 1.08
	struct dpu_mmu_tbl mmu_tbl;
	struct cmdlist cl[MAX_CL_NUM];
	u8 cur_cl;
	uint64_t afbc_effc;
	struct drm_property_blob *hdr_coefs_blob_prop;
	struct drm_property_blob *scale_coefs_blob_prop;
	struct spacemit_afbc_state *afbc_state;
};

struct spacemit_plane *to_spacemit_plane(struct drm_plane *plane);

static inline struct
spacemit_plane_state *to_spacemit_plane_state(const struct drm_plane_state *state)
{
	return container_of(state, struct spacemit_plane_state, state);
}

struct spacemit_crtc_state {
	struct drm_crtc_state base;
	uint32_t post_scaler_w;
	uint32_t post_scaler_h;
	bool post_scl_on;
	struct spacemit_crtc_scaler scalers[MAX_SCALER_NUMS];
	struct spacemit_crtc_rdma *rdmas;
	uint64_t mclk;	/* max of all rdma mclk */
	uint64_t bw;	/* sum of all rdma bw*/
	uint64_t aclk;	/* based on bw */
	uint64_t real_mclk; /* real_mclk = max(mclk, aclk) */
	uint64_t expected_present_time;
	struct drm_property_blob *color_matrix_blob_prop;
	bool scl_rdma_reuse[MAX_SCALER_NUMS];
	u32 scl_rdma_id[MAX_SCALER_NUMS];
	struct dpu_mmu_tbl wb_mmu_tbl;
	struct cmdlist cl[MAX_CL_NUM];
	int cur_cl;
	struct drm_property_blob *gamma_table_blob_prop;
	struct drm_property_blob *pp_acad_blob_prop;
	struct drm_property_blob *end_tone_mapping_blob_prop;
	struct drm_property_blob *ee_blob_prop;
	struct drm_property_blob *pp_color_temperature_blob_property;
};

#define to_spacemit_crtc_state(x) container_of(x, struct spacemit_crtc_state, base)

struct drm_plane *spacemit_plane_init(struct drm_device *drm,
					struct spacemit_crtc *a_crtc);

struct spacemit_plane_state;
struct spacemit_crtc;
struct tbu_instance;
struct dpu_mmu_tbl;
struct spacemit_wb;
enum spacemit_dpu_irq;
struct spacemit_hw_device {
	void __iomem *base;
	phys_addr_t phy_addr;
	/* lcd top reg, used for cmd mode */
	void __iomem *top_base;
	phys_addr_t top_phy_addr;
	u8 plane_nums;
	u8 offline_plane_nums;
	u8 rdma_nums;
	u8 crtc_nums;
	const struct spacemit_hw_rdma *rdmas;
	u8 n_formats;
	const struct dpu_format_id *formats;
	u8 n_fbcmems;
	const u32 *fbcmem_sizes;
	u32 solid_color_shift;
	int hdr_coef_size;
	int hor_scale_coef_size;
	int ver_scale_coef_size;
	u8 reboot_flag;
	u16 scaler_num;
	u16 acad_num;
	u16 dpu_version;
	u8 crtc_num;
	u16 *gamma_table;
	int gamma_size;
	enum drm_color_encoding color_encoding;
	enum drm_color_range color_range;
	int etm_size;
	bool is_edp;
	bool is_acad_on;
	bool is_bl_save_on;
	void (*conf_ee)(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
	void (*conf_dpuctrl_color_matrix)(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
	void (*update_prepipe_gamma)(struct drm_plane *plane, struct spacemit_plane_state *spacemit_pstate);
	void (*conf_gamma_table)(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
	int (*check_end_matrix)(struct drm_crtc_state *state);
	void (*conf_end_tone_mapping)(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
	void (*conf_matrix)(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
	void (*update_hdr_matrix)(struct drm_plane *plane, struct spacemit_plane_state *spacemit_pstate);
	void (*update_csc_matrix)(struct drm_plane *plane, struct drm_plane_state *old_state);
	void (*conf_dpuctrl_acad)(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
	void (*conf_scaler_x)(struct drm_plane_state *state, struct cmdlist_regs *cl_scl);
	void (*conf_scaler_coefs)(struct drm_plane *plane, struct spacemit_plane_state *spacemit_pstate, struct cmdlist_regs *cl_scl);
	void (*enable_vsync)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev, bool enable);
	void (*enable_cfg_irq)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev, bool enable);
	void (*cfg_ready)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
	void (*sw_start)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
	void (*cmd_update)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
	void (*dpu_init)(struct spacemit_crtc *a_crtc);
	void (*irq_enable)(struct spacemit_crtc *a_crtc, bool enable);
	void (*plane_update_hw_channel)(struct drm_plane *plane, int slice_id);
	void (*plane_disable_hw_channel)(struct drm_plane *plane, struct drm_plane_state *old_state);
	void (*conf_dpuctrl)(struct drm_crtc *crtc, struct drm_crtc_state *old_state);
	void (*wb_config)(struct spacemit_crtc *a_crtc, struct spacemit_wb *wb, struct drm_framebuffer *fb, struct cmdlist_regs *cl_wb, int slice_id);
	void (*wb_disable)(struct spacemit_crtc *a_crtc);
	int (*is_wb_en)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
	uint32_t (*get_cfg_rdy)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
	uint32_t (*get_irq_bit)(enum spacemit_dpu_irq irq_id, int dev_id);
	uint32_t (*get_int_sts)(struct spacemit_hw_device *hwdev, int dev_id);
	uint32_t (*get_rdma_dbg_sts)(struct spacemit_crtc *a_crtc, int dev_id);
	void (*clr_int_sts)(struct spacemit_crtc *a_crtc, u32 data, int dev_id);
	void (*dpu_disable)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
	void (*dpu_restart)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
	int (*dpu_stop_check)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
	void (*enable_cmdlist)(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev, int id, bool enable);
	void (*cfg_cmdlist)(struct spacemit_hw_device *hwdev, int id, u32 chy, u32 addrl, u32 addrh);
	void (*rdma_contig_mem)(struct spacemit_hw_device *hwdev, u8 tbu_id, phys_addr_t contig_pa, struct drm_framebuffer *fb, struct cmdlist_regs *cl_rdma, struct drm_plane *plane);
	void (*rdma_dmmu)(struct spacemit_hw_device *hwdev, u8 tbu_id, struct tbu_instance *tbu, struct drm_framebuffer *fb, u32 val, struct cmdlist_regs *cl_rdma, struct drm_plane *plane);
	void (*wb_dmmu)(struct spacemit_hw_device *hwdev, u8 tbu_id, struct tbu_instance *tbu, struct drm_framebuffer *fb, u32 val, u8 fbc_mode, int header_size, struct spacemit_wb *wb);
	int (*get_cl_rdma_buf)(struct spacemit_crtc *a_crtc);
	void (*cmdlist_fill_data_row)(struct cmdlist *cl, u32 strobe, u32 offset, u32 value[]);
	void (*cmdlist_fill_conf_row)(struct cmdlist *cl, struct spacemit_hw_device *hwdev, u8 dev_id);
	void (*wb_cmdlist)(struct cmdlist *cl, struct spacemit_hw_device *hwdev, struct spacemit_drm_private *priv, u8 crtc_id, u8 dev_id);
#ifdef CONFIG_SPACEMIT_DEBUG
	void (*dpu_dump_reg)(struct spacemit_crtc *a_crtc);
	void (*dpu_dump_rdma_status)(struct spacemit_drm_private *priv);
#endif
	void (*cmdlist_dump_node)(struct cmdlist *cl);
};
#endif
