/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _DPU_SATURN_H_
#define _DPU_SATURN_H_

//#include "saturn_regs/reg_map.h"
#include "../spacemit_crtc.h"

#ifndef min
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif

#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif

#ifndef clip
#define clip(x, a, b) (max(a, min(x, b)))
#endif

#ifndef is_rot_90_270
#define is_rot_90_270(x) (x & (DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270))
#endif

#define DOVE_SCALER_MAX			4
#define DOVE_DMA_CHANNEL_MAX		4
#define DOVE_DMA_LAYER_MAX		16
#define DOVE_COMPOSER_MAX		2
#define DOVE_COMPOSER_LAYER_MAX	16
#define DOVE_PANEL_MAX			1
#define DOVE_OUTCTRL_MAX			4
#define DOVE_WRITEBACK_MAX		1
#define DOVE_DISPLAY_MAX			2
#define DOVE_LUT3D_MAX			0
#define DOVE_CMDLIST_MAX			5

#define SPACEMIT_DSC_MAX_X_SLICE	(2)
#define SPACEMIT_DSC_DEFALUT_X_SLICE	(1)

/* Supported variants of the hardware
 * saturn_hw_version must match with HWC
 * so can NOT update current IP id
 */
enum saturn_hw_version {
	SATURN_LEC = 2,			//lark L
	SATURN_LED,			//lark M
	SATURN_LEE,			//dove NR
	SATURN_MEA,			//lark pro
	SATURN_HEE,			//1st dpu
	SATURN_EDP,			//2rd dpu
	/* keep the next entry last */
	SPACEMIT_DP_MAX_DEVICES
};

struct dpu_format_id {
	u32 format;		/* DRM fourcc */
	u8 id;			/* used internally */
	u8 bpp;			/* bit per pixel */
};
#define SPACEMIT_DPU_INVALID_FORMAT_ID	0xff

enum format_features {
	FORMAT_RGB  = BIT(0),
	FORMAT_RAW_YUV  = BIT(1),
	FORMAT_AFBC = BIT(2),
};

enum out_mode {
	DPU_OUT_MODE_VIDEO,
	DPU_OUT_MODE_CMD,
	DPU_OUT_MODE_MAX
};

enum rotation_features {
	ROTATE_COMMON = BIT(0),		/* supports rotation on raw and afbc 0/180, x and y */
	ROTATE_RAW_90_270 = BIT(1),	/* supports rotation on raw 90/270 */
	ROTATE_AFBC_90_270 = BIT(2),	/* supports rotation on afbc 90/270 */
};

struct spacemit_hw_rdma {
	u16 formats;
	u16 rots;
};

struct drm_dpu_hw_version_blob {
	u16 dpu_version;
	u16 rdma_nums;
	u16 scaler_num;
	u16 ver_scaler_num;
	u16 hor_scaler_num;
	u16 acad_num;
	u16 rdma_offset;
	u64 dpu_cfg;
	u64 reserved;
} __attribute__((packed));

struct spacemit_afbc_state {
	uint8_t block_size;
	uint8_t tile_type;
	uint8_t yuv_transform;
	uint8_t split_mode;
	uint8_t copy_mode;
};

struct pll_freq_range_t{
	u16 low;
	u16 mid;
	u16 high;
};

#define PLL_MODE_LWG_PLL 0
#define PLL_MODE_5G_PLL  1
#define PLL_REF_FREQ_MHZ 24
/* DSI bitclk pll control regs */
#define PLL_CTRL_REG0   0xc0
#define PLL_CTRL_REG1   0xc4
#define PLL_CTRL_REG2   0xc8
#define PLL_CTRL_REG3   0xcc
#define PLL_CTRL_STATUS 0x230

#define PLL_LK          BIT(29)
#define PLL_UP          BIT(31)
#define PLL_DIV_EN      (0xf << 4)

#define SPACEMIT_MAX_SCALE_FACTOR 4

extern const u32 saturn_fbcmem_sizes[2];
extern const u32 saturn_le_fbcmem_sizes[2];

u8 spacemit_plane_hw_get_format_id(u32 format);
u32 saturn_conf_dpuctrl_rdma(struct spacemit_crtc *a_crtc);
extern struct spacemit_hw_device spacemit_dp_devices[SPACEMIT_DP_MAX_DEVICES];
void spacemit_get_afbc_modifier(uint64_t modifier, struct spacemit_afbc_state *afbc_state);
void spacemit_dpu_power_enable(struct spacemit_crtc *a_crtc, bool enable);
void saturn_enable_irq_mask(struct spacemit_crtc *a_crtc, bool enable, u32 offset, u32 mask);
void spacemit_cfg_rdy_timer_handler(struct timer_list *t);
#endif
