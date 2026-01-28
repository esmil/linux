/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ccic_drv.h - Driver for ccic
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */

#ifndef _CCIC_DRV_H_
#define _CCIC_DRV_H_
#include <linux/types.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <linux/reset.h>
//#include "cam_dbg.h"

#define SC2_MODE_CCIC 1
#define SC2_MODE_ISP 2

#define MHZ 1000000
/*
 * the min/max is for calcuting DPHY, the unit is ns.
 */
#define D_TERMEN_MAX (35)
#define HS_PREP_MIN (40)
#define HS_PREP_MAX (85)
#define HS_PREP_ZERO_MIN (145)
#define NS_TO_PS(nsec) ((nsec) * 1000)
#define CCIC_CSI_DEV_MAX (4)
#define PATH_NUM_PER_DEV (4)
#define PATH_NUM_MAX (CCIC_CSI_DEV_MAX * PATH_NUM_PER_DEV)

/* MIPI related */
/* Sensor MIPI behavior descriptor, sensor driver should pass it to controller
 * driver, and let controller driver decide how to config its PHY registers */
struct csi_dphy_desc {
	u32 clk_mul;
	u32 clk_div; /* clock_lane_freq = input_clock * clk_mul / clk_div */
	u32 clk_freq;
	u32 cl_prepare; /* cl_* describes clock lane timing in the unit of ns */
	u32 cl_zero;
	u32 hs_prepare; /* hs_* describes data LP to HS transition timing */
	u32 hs_zero; /* in the unit of clock lane period(DDR period) */
	u32 nr_lane; /* When set to 0, S/W will try to figure out a value */
};

struct mipi_csi2 {
	int dphy_type; /* 0: DPHY on chip, 1: DPTC off chip */
	u32 dphy[5]; /* DPHY:  CSI2_DPHY1, CSI2_DPHY2, CSI2_DPHY3, CSI2_DPHY5, CSI2_DPHY6 */
	int calc_dphy;
	struct csi_dphy_desc dphy_desc;
	int enable_dpcm;
};

#define HS_SETTLE_POS_MAX (100)
struct csi_dphy_calc {
	char name[16];
	int hs_termen_pos;
	int hs_settle_pos; /* 0~100 */
};

struct csi_dphy_reg {
	u16 cl_termen;
	u16 cl_settle;
	u16 cl_miss;
	u16 hs_termen;
	u16 hs_settle;
	u16 hs_rx_to;
	u16 lane; /* When set to 0, S/W will try to figure out a value */
	u16 vc; /* Virtual channel */
	u16 dt1; /* Data type 1: For video or main data type */
	u16 dt2; /* Data type 2: For thumbnail or auxiliry data type */
};

struct ccic_ctrl {
	int index;
	atomic_t usr_cnt;
	struct mipi_csi2 csi;
	struct ccic_dev *ccic_dev;
	struct ccic_ctrl_ops *ops;
	//irqreturn_t (*handler)(struct ccic_ctrl *, u32);
};

enum ccic_idi {
	CCIC_CSI2IDI0 = 0,
	CCIC_CSI2IDI1,
};

enum ccic_idi_sel {
	CCIC_IDI_SEL_NONE = 0,
	CCIC_IDI_SEL_DPCM,
	CCIC_IDI_SEL_REPACK,
	CCIC_IDI_SEL_PARALLEL,
	CCIC_IDI_SEL_AHB,
	CCIC_IDI_RELEASE_RESET,
};

enum ccic_idi_mux {
	CCIC_IDI_MUX_LOCAL_MAIN = 0,
	CCIC_IDI_MUX_IPE2_VCDT,
	CCIC_IDI_MUX_IPE2_MAIN,
	CCIC_IDI_MUX_REMOTE_VCDT,
};

enum ccic_csi2vc_mode {
	CCIC_CSI2VC_NM = 0,
	CCIC_CSI2VC_VC,
	CCIC_CSI2VC_DT,
};

enum ccic_csi2vc_chnl {
	CCIC_CSI2VC_MAIN = 0,
	CCIC_CSI2VC_VCDT,
};

enum csi_dma_dump_format {
	CSI_DUMP_FMT_YUV422 = 0,
	CSI_DUMP_FMT_RAW8,
	CSI_DUMP_FMT_RAW10,
	CSI_DUMP_FMT_RAW12,
	CSI_DUMP_FMT_MAX,
};

struct ccic_ctrl_ops {
	void (*irq_mask)(struct ccic_ctrl *ctrl, int on);
	int (*clk_enable)(struct ccic_ctrl *ctrl, int en);
	int (*config_csi2_mode)(struct ccic_ctrl *ctrl, int mode,
				unsigned int dt_en);
	int (*config_csi2_mbus)(struct ccic_ctrl *ctrl, int lanes,
				int mipi_m_bps);
	int (*config_csi_path_dt_filter)(struct ccic_ctrl *ctrl, int path_id,
					 int filter0_en, u32 filter0,
					 int filter1_en, u32 filter1);
	int (*config_csi_path_vc)(struct ccic_ctrl *ctrl, int path_id, u32 vc);
};

//todo: to delete
enum ccic_dma_sel {
	CCIC_DMA_SEL_LOCAL_MAIN = 0,
	CCIC_DMA_SEL_LOCAL_VCDT,
	CCIC_DMA_SEL_REMOTE_MAIN,
	CCIC_DMA_SEL_REMOTE_VCDT,
};

struct ccic_dev {
	//struct miscdevice mdev;
	char name[32];
	int index;
	struct device *dev;
	struct platform_device *pdev;
	struct list_head list;
	int irq;
	struct resource *mem;
	void __iomem *base;
	struct clk *csi_clk;
	struct clk *clk4x;
	struct clk *ahb_clk;
	struct clk *axi_clk;

	struct reset_control *csi_reset;
	struct reset_control *ccic_4x_reset;
	struct reset_control *sc2_hclk_reset;
	struct reset_control *isp_cibus_reset;
	int dma_burst;
	spinlock_t ccic_lock; /* protect the struct members and HW */
	u32 interrupt_mask_value;

	/* object for ccic csi part */
	struct ccic_ctrl *ctrl;
	/* object for csiphy part */
	struct csiphy_device *csiphy;
	struct v4l2_device v4l2_dev;
	void *vnode;
	void *path_vnode[PATH_NUM_PER_DEV];

	atomic_t open_cnt;
};

/*
 * Device register I/O
 */
static inline u32 ccic_reg_read(struct ccic_dev *ccic_dev, unsigned int reg)
{
	return ioread32(ccic_dev->base + reg);
}

static inline void ccic_reg_write(struct ccic_dev *ccic_dev, unsigned int reg,
				  u32 val)
{
	iowrite32(val, ccic_dev->base + reg);
}

static inline void ccic_reg_write_mask(struct ccic_dev *ccic_dev,
				       unsigned int reg, u32 val, u32 mask)
{
	u32 v = ccic_reg_read(ccic_dev, reg);

	v = (v & ~mask) | (val & mask);
	ccic_reg_write(ccic_dev, reg, v);
}

static inline void ccic_reg_set_bit(struct ccic_dev *ccic_dev, unsigned int reg,
				    u32 val)
{
	ccic_reg_write_mask(ccic_dev, reg, val, val);
}

static inline void ccic_reg_clear_bit(struct ccic_dev *ccic_dev,
				      unsigned int reg, u32 val)
{
	ccic_reg_write_mask(ccic_dev, reg, 0, val);
}

int ccic_ctrl_get(struct ccic_ctrl **ctrl_host, int id);
int ccic_dphy_hssettle_set(unsigned int ccic_id, unsigned int dphy_freg);
irqreturn_t ccic_mmu_irq_handler(int irq, void *data);
irqreturn_t ccic_dma_irq_handler(int irq, void *data);
#endif
