// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Spacemit IPE MODULE
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/media-bus-format.h>
#include <media/spacemit/ccic_uapi.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include <linux/clk.h>
#include "ccic_drv.h"
#include "ccic_hwreg.h"
#include "csiphy.h"
#include "ccic_vdev.h"
#include "ccic_dma.h"

#define DEBUG 0 /* for pr_debug() */

#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
#include "ccic_iommu.h"
#include <linux/dma-mapping.h>
#define MMU_RESERVED_MEM_SIZE (4096)
#define MMU_REG_BASE (0xd420fc00)
#endif

#define CCIC_DRV_NAME "spacemit_ccic"

#define CAM_ALIGN(a, b)                                   \
	({                                                \
		unsigned int ___tmp1 = (a);               \
		unsigned int ___tmp2 = (b);               \
		unsigned int ___tmp3 = ___tmp1 % ___tmp2; \
		___tmp1 /= ___tmp2;                       \
		if (___tmp3)                              \
			___tmp1++;                        \
		___tmp1 *= ___tmp2;                       \
		___tmp1;                                  \
	})

#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
struct ccic_iommu_device *mmu_dev = NULL;
dma_addr_t trans_tab_dma_addr = 0;
void *trans_tab_cpu_addr = NULL;
size_t total_trans_tab_sz = 0;
dma_addr_t rsvd_phy_addr = 0;
unsigned char *rsvd_vaddr = NULL;
struct mmu_ctx mmu_ctx[CCIC_IOMMU_CH_NUM];
#endif

static LIST_HEAD(ccic_devices);
static DEFINE_MUTEX(list_lock);
static void ccic_dma_bh_handler(struct ccic_dma_work_struct *ccic_dma_work);

static void ccic_irqmask(struct ccic_ctrl *ctrl, int on)
{
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	if (on) {
		//ccic_reg_write(ccic_dev, REG_IRQSTAT, ALLIRQS);
		ccic_reg_set_bit(ccic_dev, REG_IRQMASK, ALLIRQS);
	}
}

static int ccic_config_csi2(struct ccic_dev *ccic_dev, struct mipi_csi2 *csi,
			    int enable)
{
	unsigned int ctrl0_val = 0;
	int lanes = csi->dphy_desc.nr_lane;

	if (!ccic_dev->csiphy)
		return -EINVAL;
	if (enable) {
		csiphy_start(ccic_dev->csiphy, csi);
		ctrl0_val = ccic_reg_read(ccic_dev, REG_CSI2_CTRL0);
		ctrl0_val &= ~(CSI2_C0_LANE_NUM_MASK);
		ctrl0_val |= CSI2_C0_LANE_NUM(lanes);
		ctrl0_val |= CSI2_C0_ENABLE;
		ctrl0_val &= ~(CSI2_C0_VLEN_MASK);
		ctrl0_val |= CSI2_C0_VLEN;
		ccic_reg_write(ccic_dev, REG_CSI2_CTRL0, ctrl0_val);
	} else {
		csiphy_stop(ccic_dev->csiphy);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL0,
				   CSI2_C0_ENABLE); //csi off
	}
	return 0;
}

static int ccic_config_csi2_dphy(struct ccic_ctrl *ctrl, struct mipi_csi2 *csi,
				 int enable)
{
	int ret = 0;
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	ret = ccic_config_csi2(ccic_dev, csi, enable);
	if (ret) {
		dev_err(ccic_dev->dev, "csi2 config failed\n");
		goto out;
	}

out:
	return ret;
}

static int ccic_config_csi2_vc(struct ccic_ctrl *ctrl, int md,
			       unsigned int dt_en)
{
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	return ccic_csi2_vc_ctrl(ccic_dev, md, dt_en);
}

static int __maybe_unused ccic_enable_csi2idi(struct ccic_ctrl *ctrl)
{
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	ccic_csi2idi_reset(ccic_dev, 0);

	return 0;
}

static int ccic_clk_set_rate(struct ccic_ctrl *ctrl_dev, int mode)
{
	unsigned long clk_val;
	struct ccic_dev *ccic_dev = ctrl_dev->ccic_dev;

	//clk_val = clk_round_rate(ccic_dev->csi_clk, 1000000000);
	clk_val = clk_round_rate(ccic_dev->csi_clk, 500000000);
	clk_set_rate(ccic_dev->csi_clk, clk_val);
	clk_val = clk_get_rate(ccic_dev->csi_clk);
	pr_info("cam clk[csi_func]: %ld\n", clk_val);

	//clk_val = clk_round_rate(ccic_dev->clk4x, 1000000000);
	clk_val = clk_round_rate(ccic_dev->clk4x, 500000000);
	clk_set_rate(ccic_dev->clk4x, clk_val);
	clk_val = clk_get_rate(ccic_dev->clk4x);
	pr_info("cam clk[ccic_func]: %ld\n", clk_val);

	clk_val = clk_round_rate(ccic_dev->ahb_clk, 307200000);
	clk_set_rate(ccic_dev->ahb_clk, clk_val);
	clk_val = clk_get_rate(ccic_dev->ahb_clk);
	pr_info("cam clk[ahb_func]: %ld\n", clk_val);

	clk_val = clk_round_rate(ccic_dev->axi_clk, 409600000);
	clk_set_rate(ccic_dev->axi_clk, clk_val);
	clk_val = clk_get_rate(ccic_dev->axi_clk);
	pr_info("cam clk[axi_func]: %ld\n", clk_val);
	return 0;
}

static int ccic_clk_enable(struct ccic_ctrl *ctrl, int en)
{
	int ret = 0, v = 0;
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;
	struct device *dev = &ccic_dev->pdev->dev;

	if (en) {
		mutex_lock(&ctrl->lock);
		if (atomic_inc_return(&ctrl->usr_cnt) == 1) {
			reset_control_deassert(ccic_dev->sc2_hclk_reset);
			reset_control_deassert(ccic_dev->ccic_4x_reset);
			reset_control_deassert(ccic_dev->csi_reset);
			reset_control_deassert(ccic_dev->isp_cibus_reset);

			clk_prepare_enable(ccic_dev->ahb_clk);
			clk_prepare_enable(ccic_dev->clk4x);
			clk_prepare_enable(ccic_dev->csi_clk);
			clk_prepare_enable(ccic_dev->axi_clk);

			ret = ccic_clk_set_rate(ctrl, SC2_MODE_ISP);
			if (ret < 0) {
				atomic_dec(&ctrl->usr_cnt);
				return ret;
			}

			ccic_csi2idi_reset(ccic_dev, 0);
#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
			//set mmu timeout default addr
			mmu_dev->ops->set_timeout_default_addr(mmu_dev, (uint64_t)rsvd_phy_addr);
#endif
			dev_info(dev, "power on\n");
		}
		mutex_unlock(&ctrl->lock);
	} else {
		mutex_lock(&ctrl->lock);
		v = atomic_dec_return(&ctrl->usr_cnt);
		if (v == 0) {
			ccic_csi2idi_reset(ccic_dev, 1);
			clk_disable_unprepare(ccic_dev->axi_clk);
			reset_control_assert(ccic_dev->isp_cibus_reset);
			clk_disable_unprepare(ccic_dev->csi_clk);
			reset_control_assert(ccic_dev->csi_reset);
			clk_disable_unprepare(ccic_dev->clk4x);
			reset_control_assert(ccic_dev->ccic_4x_reset);
			clk_disable_unprepare(ccic_dev->ahb_clk);
			reset_control_assert(ccic_dev->sc2_hclk_reset);
			dev_info(dev, "power off\n");
		} else if (v < 0) {
			atomic_inc(&ctrl->usr_cnt);
			dev_err(dev, "invalid power off\n");
		}
		mutex_unlock(&ctrl->lock);
	}

	return ret;
}

static int ccic_config_csi2_mbus(struct ccic_ctrl *ctrl, int lanes,
				 int dphy_freq)
{
	int ret;
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;
	struct mipi_csi2 csi2para;

	csi2para.calc_dphy = 0;
	csi2para.dphy[0] = 0x00000001;
	csi2para.dphy[1] = 0x5ada5ada; //for lark
	//csi2para.dphy[2] = 0x0000201a;//0x0000201a: 1.0G ~ 1.5G
	csi2para.dphy[2] = 0x00001500;
	csi2para.dphy[3] = 0x000000ff;
	csi2para.dphy[4] = 0x1000;
	csi2para.dphy_desc.nr_lane = lanes;
	ret = ccic_config_csi2_dphy(ctrl, &csi2para, !!lanes);
	if (ret)
		return ret;

	ret = ccic_dphy_hssettle_set(ccic_dev->index, dphy_freq);
	if (ret)
		return ret;
	pr_debug("ccic%d csi2 %s", ccic_dev->index,
		 lanes ? "enabled" : "disabled");

	return ret;
}

static int ccic_config_csi_path_dt_filter(struct ccic_ctrl *ctrl, int path_id,
					  int filter0_en, u32 filter0,
					  int filter1_en, u32 filter1)
{
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	if (path_id == 0) {
		ccic_set_path0_dt_filter(ccic_dev, filter0_en, filter0,
					 filter1_en, filter1);
	} else if (path_id == 1) {
		ccic_set_path1_dt_filter(ccic_dev, filter0_en, filter0,
					 filter1_en, filter1);
	} else if (path_id == 2) {
		ccic_set_path2_dt_filter(ccic_dev, filter0_en, filter0,
					 filter1_en, filter1);
	} else if (path_id == 3) {
		ccic_set_path3_dt_filter(ccic_dev, filter0_en, filter0,
					 filter1_en, filter1);
	} else {
		dev_err(ccic_dev->dev, "%s invalid path_id %d\n", __func__,
			path_id);
		return -EINVAL;
	}

	return 0;
}

static int ccic_config_csi_path_vc(struct ccic_ctrl *ctrl, int path_id, u32 vc)
{
	struct ccic_dev *ccic_dev = ctrl->ccic_dev;

	if (path_id < 0 || path_id >= 4) {
		dev_err(ccic_dev->dev, "%s invalid path_id %d\n", __func__,
			path_id);
		return -EINVAL;
	}
	ccic_set_path_vc(ccic_dev, path_id, vc);
	return 0;
}

static struct ccic_ctrl_ops ccic_ctrl_ops = {
	.irq_mask = ccic_irqmask,
	.clk_enable = ccic_clk_enable,
	.config_csi2_mbus = ccic_config_csi2_mbus,
	.config_csi2_mode = ccic_config_csi2_vc,
	.config_csi_path_dt_filter = ccic_config_csi_path_dt_filter,
	.config_csi_path_vc = ccic_config_csi_path_vc,
};

static int ccic_init_clk(struct ccic_dev *dev)
{
	dev->csi_reset = devm_reset_control_get_optional_shared(&dev->pdev->dev,
								"csi_reset");
	if (IS_ERR(dev->csi_reset))
		return PTR_ERR(dev->csi_reset);

	dev->ccic_4x_reset = devm_reset_control_get_optional_shared(
		&dev->pdev->dev, "ccic_4x_reset");
	if (IS_ERR(dev->ccic_4x_reset))
		return PTR_ERR(dev->ccic_4x_reset);

	dev->sc2_hclk_reset = devm_reset_control_get_optional_shared(
		&dev->pdev->dev, "sc2_hclk_reset");
	if (IS_ERR(dev->sc2_hclk_reset))
		return PTR_ERR(dev->sc2_hclk_reset);

	dev->isp_cibus_reset = devm_reset_control_get_optional_shared(
		&dev->pdev->dev, "isp_cibus_reset");
	if (IS_ERR(dev->isp_cibus_reset))
		return PTR_ERR(dev->isp_cibus_reset);

	dev->axi_clk = devm_clk_get(&dev->pdev->dev, "sc2_axi");
	if (IS_ERR(dev->axi_clk))
		return PTR_ERR(dev->axi_clk);

	dev->ahb_clk = devm_clk_get(&dev->pdev->dev, "sc2_ahb");
	if (IS_ERR(dev->ahb_clk))
		return PTR_ERR(dev->ahb_clk);

	dev->csi_clk = devm_clk_get(&dev->pdev->dev, "csi_func");
	if (IS_ERR(dev->csi_clk))
		return PTR_ERR(dev->csi_clk);

	dev->clk4x = devm_clk_get(&dev->pdev->dev, "ccic_func");
	return PTR_ERR_OR_ZERO(dev->clk4x);
}

static int ccic_device_register(struct ccic_dev *ccic_dev)
{
	struct ccic_dev *other;

	mutex_lock(&list_lock);
	list_for_each_entry(other, &ccic_devices, list) {
		if (other->index == ccic_dev->index) {
			dev_warn(ccic_dev->dev, "ccic%d already registered\n",
				 ccic_dev->index);
			mutex_unlock(&list_lock);
			return -EBUSY;
		}
	}

	list_add_tail(&ccic_dev->list, &ccic_devices);
	mutex_unlock(&list_lock);
	return 0;
}

static int ccic_device_unregister(struct ccic_dev *ccic_dev)
{
	mutex_lock(&list_lock);
	list_del(&ccic_dev->list);
	mutex_unlock(&list_lock);
	return 0;
}

int ccic_dphy_hssettle_set(unsigned int ccic_id, unsigned int dphy_freq)
{
	u32 reg_settle = 0x00002b00;
	struct ccic_dev *ccic_dev = NULL;
	struct ccic_dev *tmp;

	if (dphy_freq < 80) //dphy_clock uint: MHZ
		return -EINVAL;
	// RX_Tsettle > TX_HSprepare; reg uint: (1 / (dphy_clock / 2))
	reg_settle =
		(HS_PREP_ZERO_MIN + HS_PREP_MAX) * dphy_freq / (2 * 8 * 1000);
	reg_settle = reg_settle << CSI2_DPHY3_HS_SETTLE_SHIFT;

	mutex_lock(&list_lock);
	list_for_each_entry(tmp, &ccic_devices, list) {
		if (tmp->index == ccic_id) {
			ccic_dev = tmp;
			break;
		}
	}
	if (!ccic_dev) {
		pr_err("ccic%d not found", ccic_id);
		mutex_unlock(&list_lock);
		return -ENODEV;
	}

	csiphy_timming_setting(ccic_dev->csiphy, reg_settle);
	mutex_unlock(&list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(ccic_dphy_hssettle_set);

int ccic_ctrl_get(struct ccic_ctrl **ctrl_host, int id)
{
	struct ccic_dev *ccic_dev = NULL;
	struct ccic_dev *tmp;
	struct ccic_ctrl *ctrl = NULL;

	list_for_each_entry(tmp, &ccic_devices, list) {
		if (tmp->index == id) {
			ccic_dev = tmp;
			break;
		}
	}
	if (!ccic_dev) {
		pr_err("ccic%d not found", id);
		return -ENODEV;
	}

	ctrl = ccic_dev->ctrl;
	*ctrl_host = ctrl;
	pr_debug("acquire ccic%d ctrl dev succeed\n", id);

	return 0;
}
EXPORT_SYMBOL(ccic_ctrl_get);

static void ipe_error_irq_handler(struct ccic_dev *ccic, u32 ipestatus,
				  u32 csi2status)
{
	static DEFINE_RATELIMIT_STATE(rs, 5 * HZ, 20);

	if (__ratelimit(&rs)) {
		pr_err("CCIC%d: interrupt status 0x%08x, csi2 status 0x%08x\n",
		       ccic->index, ipestatus, csi2status);
	} else {
		/* aovid soft lockup due to high frequency interrupt */
		ccic_reg_clear_bit(ccic, REG_IRQMASK, CSI2PHYERRS);
		pr_err("CCIC%d: too many interrupt errors, mask\n", ccic->index);
	}
}

static int ccic_put_dma_work(struct ccic_dma_context *dma_ctx,
			     struct ccic_dma_work_struct *ccic_dma_work)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&dma_ctx->slock, flags);
	list_del_init(&ccic_dma_work->busy_list_entry);
	list_add(&ccic_dma_work->idle_list_entry, &dma_ctx->dma_work_idle_list);
	spin_unlock_irqrestore(&dma_ctx->slock, flags);

	return 0;
}

static int ccic_get_dma_work(struct ccic_dma_context *dma_ctx,
			     struct ccic_dma_work_struct **ccic_dma_work)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&dma_ctx->slock, flags);
	*ccic_dma_work = list_first_entry_or_null(&dma_ctx->dma_work_idle_list,
						  struct ccic_dma_work_struct,
						  idle_list_entry);
	if (NULL == *ccic_dma_work) {
		spin_unlock_irqrestore(&dma_ctx->slock, flags);
		return -1;
	}
	list_del_init(&((*ccic_dma_work)->idle_list_entry));
	list_add(&((*ccic_dma_work)->busy_list_entry),
		 &dma_ctx->dma_work_busy_list);
	spin_unlock_irqrestore(&dma_ctx->slock, flags);

	return 0;
}

static void ccic_dma_bh_handler(struct ccic_dma_work_struct *ccic_dma_work)
{
	struct ccic_vnode *vnode = ccic_dma_work->sc_vnode;
	struct device *dev = vnode->ccic_dev->dev;
	struct ccic_dma_context *dma_ctx = &vnode->dma_ctx;
	struct ccic_vbuffer *n = NULL, *pos = NULL;
	//unsigned int irq_status = ccic_dma_work->irq_status;
	LIST_HEAD(export_list);
	unsigned long flags = 0;
	unsigned long wq_flags = 0;

	spin_lock_irqsave(&vnode->waitq_head.lock, wq_flags);
	vnode->in_tasklet = 1;
	if (vnode->in_streamoff || !vnode->is_streaming) {
		wake_up_locked(&vnode->waitq_head);
		spin_unlock_irqrestore(&vnode->waitq_head.lock, wq_flags);
		goto dma_tasklet_finish;
	}
	wake_up_locked(&vnode->waitq_head);
	spin_unlock_irqrestore(&vnode->waitq_head.lock, wq_flags);
	spin_lock_irqsave(&vnode->slock, flags);
	list_for_each_entry_safe(pos, n, &vnode->busy_list, list_entry) {
		if (pos->flags &
		    (BUF_FLAG_HW_ERR | BUF_FLAG_SW_ERR | BUF_FLAG_DONE_TOUCH)) {
			list_del_init(&(pos->list_entry));
			atomic_dec(&vnode->busy_buf_cnt);
			list_add_tail(&(pos->list_entry), &export_list);
		}
	}
	spin_unlock_irqrestore(&vnode->slock, flags);
	list_for_each_entry_safe(pos, n, &export_list, list_entry) {
		if (!(pos->flags & BUF_FLAG_SOF_TOUCH)) {
			dev_warn(
				dev,
				"%s export buf index=%u frameid=%u without sof touch\n",
				vnode->name, pos->vb2_v4l2_buf.vb2_buf.index,
				pos->vb2_v4l2_buf.sequence);
		}
		if (pos->flags & BUF_FLAG_HW_ERR) {
			//pos->vb2_v4l2_buf.flags |= V4L2_BUF_FLAG_ERROR_HW;
			dev_warn(
				dev,
				"%s export buf index=%u frameid=%u with hw error\n",
				vnode->name, pos->vb2_v4l2_buf.vb2_buf.index,
				pos->vb2_v4l2_buf.sequence);
			cvdev_export_ccic_vbuffer(pos, 1);
			vnode->hw_err_frm++;
		} else if (pos->flags & BUF_FLAG_SW_ERR) {
			//pos->vb2_v4l2_buf.flags |= V4L2_BUF_FLAG_ERROR_SW;
			dev_warn(
				dev,
				"%s export buf index=%u frameid=%u with sw error\n",
				vnode->name, pos->vb2_v4l2_buf.vb2_buf.index,
				pos->vb2_v4l2_buf.sequence);
			cvdev_export_ccic_vbuffer(pos, 1);
			vnode->sw_err_frm++;
		} else if (pos->flags & BUF_FLAG_DONE_TOUCH) {
			cvdev_export_ccic_vbuffer(pos, 0);
			vnode->ok_frm++;
		}
	}
dma_tasklet_finish:
	if (vnode) {
		spin_lock_irqsave(&vnode->waitq_head.lock, wq_flags);
		vnode->in_tasklet = 0;
		wake_up_locked(&vnode->waitq_head);
		spin_unlock_irqrestore(&vnode->waitq_head.lock, wq_flags);
	}
	ccic_put_dma_work(dma_ctx, ccic_dma_work);
}

static void ccic_dma_tasklet_handler(unsigned long param)
{
	struct ccic_dma_work_struct *ccic_dma_work =
		(struct ccic_dma_work_struct *)param;
	ccic_dma_bh_handler(ccic_dma_work);
}

#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
irqreturn_t ccic_mmu_irq_handler(int irq, void *data)
{
	unsigned int mmu_irq_status = 0;
	int i = 0;

	mmu_irq_status = ccic_mmu_call(mmu_dev, irq_status);
	if (mmu_irq_status & MMU_RD_TIMEOUT) {
		pr_err("iommu RD_Timeout_error_IRQ\n");
		ccic_mmu_call(mmu_dev, dump_channel_regs, 0);
	}
	if (mmu_irq_status & MMU_WR_TIMEOUT) {
		pr_err("iommu WR_Timeout_error_IRQ\n");
		ccic_mmu_call(mmu_dev, dump_channel_regs, 0);
	}
	for (i = 0; i < 16; i++) {
		if (mmu_irq_status & (0x1 << i)) {
			pr_err("iommu tbu%d/%d dma err\n", 2 * i, 2 * i + 1);
			ccic_mmu_call(mmu_dev, dump_channel_regs, 2 * i);
			ccic_mmu_call(mmu_dev, dump_channel_regs, 2 * i + 1);
		}
	}

	return IRQ_HANDLED;
}
#endif

irqreturn_t ccic_dma_irq_handler(int irq, void *data)
{
	struct ccic_dma *ccic_dma = (struct ccic_dma *)data;
	struct ccic_dev *ccic_dev = NULL;
	struct ccic_vnode *vnode = NULL;
	struct ccic_vbuffer *pos = NULL, *vb = NULL;
	struct ccic_dma_work_struct *ccic_dma_work = NULL;
	struct ccic_dma_context *dma_ctx = NULL;
	unsigned long wq_flags = 0;
	unsigned int irq0 = 0, irq1 = 0, dma_ch = 0, irq_status = 0;
	unsigned int tmp = 0;
	int i = 0, ret = 0;

	irq0 = ccic_dma_get_irq0(ccic_dma);
	if (irq0) {
		ccic_dma_clear_irq0(ccic_dma, irq0);
	}
	irq1 = ccic_dma_get_irq1(ccic_dma);
	if (irq1) {
		ccic_dma_clear_irq1(ccic_dma, irq1);
	}

	if (irq1 & BIT(31)) {
		pr_err("dma_vsync_overflow0\n");
	}
	if (irq1 & BIT(30)) {
		pr_err("dma_data_overflow0\n");
	}
	if (irq1 & BIT(29)) {
		pr_err("txpath_snr1_overflow\n");
	}
	if (irq1 & BIT(28)) {
		pr_err("txpath_snr0_overflow\n");
	}

	list_for_each_entry(ccic_dev, &ccic_devices, list) {
		for (i = 0; i < PATH_NUM_PER_DEV; i++) {
			vnode = (struct ccic_vnode *)ccic_dev->path_vnode[i];
			dma_ch = vnode->dma_ctx.dma_ch;
			dma_ctx = &vnode->dma_ctx;
			if (dma_ch >= MAX_CCIC_DMA_CNT) {
				continue;
			}
			spin_lock_irqsave(&vnode->waitq_head.lock, wq_flags);
			vnode->in_irq = 1;
			if (vnode->in_streamoff) {
				vnode->in_irq = 0;
				wake_up_locked(&vnode->waitq_head);
				spin_unlock_irqrestore(&vnode->waitq_head.lock, wq_flags);
				continue;
			}
			spin_unlock_irqrestore(&vnode->waitq_head.lock, wq_flags);
			do {
				irq_status = ccic_dma_ch_irq_analyze(dma_ch, irq0, irq1);
				if (!irq_status) {
					break;
				}
				if (irq_status & DMA_IRQ_SOF) {
					vnode->frame_id++;
					vnode->total_frm++;
				}
				if (!vnode->is_streaming)
					break;
				tmp = irq_status;
				spin_lock(&(vnode->slock));
				list_for_each_entry(pos, &(vnode->busy_list),
						    list_entry) {
					if (!tmp)
						break;
					if (tmp & DMA_IRQ_ERR) {
						if (!(pos->flags &
						      BUF_FLAG_SOF_TOUCH)) {
							dev_info(
								ccic_dev->dev,
								"path %d dma_ch %u dma err without sof, drop it\n",
								i, dma_ch);
							tmp &= ~DMA_IRQ_ERR;
						} else if (!(pos->flags &
							     BUF_FLAG_HW_ERR)) {
							pos->flags |=
								BUF_FLAG_HW_ERR;
							dev_info(
								ccic_dev->dev,
								"path %d dma_ch %u dma err\n",
								i, dma_ch);
							tmp &= ~DMA_IRQ_ERR;
						}
					}
					if (tmp & DMA_IRQ_DONE) {
						if (!(pos->flags &
						      BUF_FLAG_SOF_TOUCH)) {
							dev_info(
								ccic_dev->dev,
								"path %d dma_ch %u dma done without sof, drop it\n",
								i, dma_ch);
							tmp &= ~DMA_IRQ_DONE;
						} else if (!(pos->flags &
							     BUF_FLAG_DONE_TOUCH)) {
							pos->flags |=
								BUF_FLAG_DONE_TOUCH;
							pos->vb2_v4l2_buf
								.sequence =
								vnode->frame_id -
								1;
							pos->vb2_v4l2_buf
								.vb2_buf
								.timestamp =
								ktime_get_ns();
							pos->vb2_v4l2_buf
								.vb2_buf
								.planes[0]
								.bytesused =
								ccic_reg_read(
									ccic_dev,
									REG_FRAME_CNT);
							tmp &= ~DMA_IRQ_DONE;
							dev_dbg(ccic_dev->dev,
								"path %d dma_ch %u dma done\n",
								i, dma_ch);
						}
						if (vnode->wait_done_flush) {
							complete(
								&vnode->flush_complete);
							vnode->wait_done_flush =
								0;
						}
					}
					if (tmp & DMA_IRQ_SOF) {
						if (pos->flags &
						    BUF_FLAG_SOF_TOUCH) {
							if (!(pos->flags &
							      (BUF_FLAG_DONE_TOUCH |
							       BUF_FLAG_HW_ERR |
							       BUF_FLAG_SW_ERR))) {
								dev_warn(
									ccic_dev->dev,
									"path %d dma_ch %u next sof arrived without dma done or err\n",
									i,
									dma_ch);
								pos->flags |=
									BUF_FLAG_SW_ERR;
							}
						} else {
							pos->flags |=
								(BUF_FLAG_SOF_TOUCH |
								 BUF_FLAG_TIMESTAMPED);
							tmp &= ~DMA_IRQ_SOF;
						}
					}
				}
				spin_unlock(&(vnode->slock));
				ret = ccic_get_dma_work(dma_ctx,
							&ccic_dma_work);
				if (ret) {
					dev_warn(
						ccic_dev->dev,
						"dma work idle list was null\n");
				} else {
					ccic_dma_work->irq_status = irq_status;
					tasklet_schedule(
						&(ccic_dma_work->dma_tasklet));
				}
				if (irq_status & DMA_IRQ_SOF) {
					cvdev_dq_idle_vbuffer(vnode, &vb);
					if (vb) {
						cvdev_q_busy_vbuffer(vnode, vb);
						ccic_update_dma_addr(vnode, vb,
								     0);
						ccic_dma_ch_shadow_ready(
							ccic_dma, dma_ch, 1);
					}
				}
			} while (0);
			spin_lock_irqsave(&vnode->waitq_head.lock, wq_flags);
			vnode->in_irq = 0;
			wake_up_locked(&vnode->waitq_head);
			spin_unlock_irqrestore(&vnode->waitq_head.lock, wq_flags);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t ccic_isr(int irq, void *data)
{
	struct ccic_dev *ccic_dev = data;
	uint32_t csi2status = 0, irqs = 0;

	irqs = ccic_reg_read(ccic_dev, REG_IRQSTAT);
	if (irqs) {
		ccic_reg_write(ccic_dev, REG_IRQSTAT, irqs);
	}

	csi2status = ccic_reg_read(ccic_dev, 0x108);
	if (irqs & CSI2PHYERRS)
		ipe_error_irq_handler(ccic_dev, irqs, csi2status);

	if (irqs & IRQ_DMA_PRO_LINE)
		pr_debug("CCIC%d: IRQ_DMA_PRO_LINE\n", ccic_dev->index);

	//if (irqs & IRQ_IDI_PRO_LINE)
	//	pr_debug("CCIC%d: IRQ_IDI_PRO_LINE\n", ccic_dev->index);

	if (irqs & IRQ_CSI2IDI_FLUSH)
		pr_debug("CCIC%d: IRQ_CSI2IDI_FLUSH\n", ccic_dev->index);

	if (irqs & IRQ_CSI2IDI_HBLK2HSYNC)
		pr_debug("CCIC%d: IRQ_CSI2IDI_HBLK2HSYNC\n", ccic_dev->index);

	if (irqs & IRQ_DPHY_RX_CLKULPS_ACTIVE)
		pr_debug("CCIC%d: IRQ_DPHY_RX_CLKULPS_ACTIVE\n",
			 ccic_dev->index);

	if (irqs & IRQ_DPHY_RX_CLKULPS)
		pr_debug("CCIC%d: IRQ_DPHY_RX_CLKULPS\n", ccic_dev->index);

	if (irqs & IRQ_DPHY_LN_ULPS_ACTIVE)
		pr_debug("CCIC%d: IRQ_DPHY_LN_ULPS_ACTIVE\n", ccic_dev->index);

	if (irqs & IRQ_CSI_SOF) {
		pr_debug("CCIC%d: IRQ_CSI_SOF\n", ccic_dev->index);
	}
	if (irqs & IRQ_CSI_EOF) {
		pr_debug("CCIC%d: IRQ_CSI_EOF\n", ccic_dev->index);
	}
	return IRQ_HANDLED;
}

static int ccic_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ccic_dev *ccic_dev;
	struct ccic_ctrl *ccic_ctrl;
	struct device *dev = &pdev->dev;
	int ret = 0, i = 0;
	char buf[32];
#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
	size_t tabs_size = 0, tab_offset = 0;
	int j = 0;
	void __iomem *mmu_reg_base = NULL;
#endif

	ret = of_property_read_u32(np, "cell-index", &pdev->id);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", ret);
		return ret;
	}

	ccic_dev = devm_kzalloc(&pdev->dev, sizeof(*ccic_dev), GFP_KERNEL);
	if (!ccic_dev) {
		dev_err(&pdev->dev, "camera: Could not allocate ccic dev\n");
		return -ENOMEM;
	}

	ccic_ctrl = devm_kzalloc(&pdev->dev, sizeof(*ccic_ctrl), GFP_KERNEL);
	if (!ccic_ctrl) {
		dev_err(&pdev->dev, "camera: Could not allocate ctrl dev\n");
		return -ENOMEM;
	}

	/* get mem */
	ccic_dev->mem =
		platform_get_resource_byname(pdev, IORESOURCE_MEM, "ccic-regs");
	if (!ccic_dev->mem) {
		dev_err(&pdev->dev, "no mem resource");
		return -ENODEV;
	}
	ccic_dev->base = devm_ioremap(&pdev->dev, ccic_dev->mem->start,
				      resource_size(ccic_dev->mem));
	if (IS_ERR(ccic_dev->base)) {
		dev_err(&pdev->dev, "fail to remap iomem\n");
		return PTR_ERR(ccic_dev->base);
	}

	/* get irqs */
	ccic_dev->irq = platform_get_irq_byname(pdev, "ccic-irq");
	if (ccic_dev->irq < 0)
		return ccic_dev->irq;
	ret = devm_request_irq(&pdev->dev, ccic_dev->irq, ccic_isr,
			       IRQF_SHARED, CCIC_DRV_NAME, ccic_dev);
	if (ret) {
		dev_err(&pdev->dev, "fail to request irq\n");
		return ret;
	}

	/* ccic device and ctrl init */
	ccic_ctrl->ccic_dev = ccic_dev;
	ccic_ctrl->index = pdev->id;
	ccic_ctrl->ops = &ccic_ctrl_ops;
	atomic_set(&ccic_ctrl->usr_cnt, 0);
	mutex_init(&ccic_ctrl->lock);

	ccic_dev->csiphy =
		csiphy_lookup_by_phandle(&pdev->dev, "spacemit,csiphy");
	if (!ccic_dev->csiphy) {
		dev_err(&pdev->dev, "fail to acquire csiphy\n");
		return -EPROBE_DEFER;
	}

	ccic_dev->index = pdev->id;
	ccic_dev->pdev = pdev;
	ccic_dev->dev = &pdev->dev;
	ccic_dev->ctrl = ccic_ctrl;
	ccic_dev->interrupt_mask_value = CSI2PHYERRS | FRAMEIRQS;
	dev_set_drvdata(dev, ccic_dev);

	ccic_init_clk(ccic_dev);

	ccic_device_register(ccic_dev);

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(33));
	ret = v4l2_device_register(&pdev->dev, &ccic_dev->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register v4l2 dev\n");
		return ret;
	}
	snprintf(ccic_dev->name, 32, "csi%d", ccic_ctrl->index);
	for (i = 0; i < 4; i++) {
		snprintf(buf, 32, "csi%d_path%d", ccic_ctrl->index, i);
		ccic_dev->path_vnode[i] = cvdev_create_vnode(buf, ccic_dev->index * 4 + i,
							  &ccic_dev->v4l2_dev,
							  &pdev->dev,
							  ccic_dev,
							  ccic_dma_tasklet_handler,
							  0);
		if (NULL == ccic_dev->path_vnode[i]) {
			dev_err(&pdev->dev,
				"failed to create ccic path vnode %d\n", i);
			return -EPROBE_DEFER;
		}
	}

#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
	if (!mmu_dev) {
		rsvd_vaddr =
			devm_kmalloc(dev, MMU_RESERVED_MEM_SIZE, GFP_KERNEL);
		if (!rsvd_vaddr) {
			pr_err("failed to alloc mem for mmu reserved");
			return -EPROBE_DEFER;
		}
		rsvd_phy_addr = virt_to_phys(rsvd_vaddr);
		pr_info("rsvd_phy_addr=0x%llx size=%d", (uint64_t)rsvd_phy_addr,
			MMU_RESERVED_MEM_SIZE);
		memset(rsvd_vaddr, 0xff, MMU_RESERVED_MEM_SIZE);
		mmu_reg_base = ioremap(MMU_REG_BASE, PAGE_SIZE);
		if (!mmu_reg_base) {
			pr_err("failed to ioremap mmu reg base");
			return -EPROBE_DEFER;
		}
		mmu_dev = ccic_iommu_create(dev, (unsigned long)mmu_reg_base);
		if (!mmu_dev) {
			pr_err("failed to create iommu device");
			return -EPROBE_DEFER;
		}
		tabs_size = 2 * IOMMU_TRANS_TAB_MAX_NUM * sizeof(uint32_t) * CCIC_IOMMU_TBU_NUM;
		trans_tab_cpu_addr = dmam_alloc_coherent(dev,
							tabs_size,
							&trans_tab_dma_addr, GFP_KERNEL);
		if (!trans_tab_cpu_addr) {
			pr_err("%s alloc page tables failed", __func__);
			return -EPROBE_DEFER;
		}
		total_trans_tab_sz = tabs_size;
		tab_offset = 0;
		for (i = 0; i < CCIC_IOMMU_CH_NUM; i++) {
			for (j = 0; j < 2; j++) {
				mmu_ctx[i].tt_addr[j][0] = trans_tab_dma_addr + tab_offset;
				mmu_ctx[i].tt_base[j][0] = trans_tab_cpu_addr + tab_offset;
				tab_offset += IOMMU_TRANS_TAB_MAX_NUM * sizeof(uint32_t);
			}
		}
		pr_info("ccic mmu tbl size alloc:%lu needed:%lu", tabs_size,
			tab_offset);
	}
#endif

	atomic_set(&ccic_dev->open_cnt, 0);

	pr_info("%s probed", dev_name(&pdev->dev));

	return ret;
}

static void ccic_remove(struct platform_device *pdev)
{
	struct ccic_dev *ccic_dev;
	int i = 0;

	ccic_dev = dev_get_drvdata(&pdev->dev);

	for (i = 0; i < 4; i++) {
		cvdev_destroy_vnode(
			(struct ccic_vnode *)ccic_dev->path_vnode[i]);
	}

	v4l2_device_unregister(&ccic_dev->v4l2_dev);
	ccic_device_unregister(ccic_dev);
}

static const struct of_device_id ccic_dt_match[] = {
	{
		.compatible = "spacemit,ccic",
		.data = NULL,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ccic_dt_match);

struct platform_driver ccic_driver = {
	.driver = {
		.name = CCIC_DRV_NAME,
		.of_match_table = of_match_ptr(ccic_dt_match),
	},
	.probe = ccic_probe,
	.remove = ccic_remove,
};

extern struct platform_driver ccic_dma_driver;

static int __init ccic_driver_init(void)
{
	int ret;

	ret = ccic_csiphy_register();
	if (ret < 0)
		return ret;

	ret = platform_driver_register(&ccic_driver);
	if (ret == 0) {
		ret = platform_driver_register(&ccic_dma_driver);
	}

	if (ret < 0)
		ccic_csiphy_unregister();

	return ret;
}

static void __exit ccic_driver_exit(void)
{
	platform_driver_unregister(&ccic_dma_driver);
	platform_driver_unregister(&ccic_driver);

	ccic_csiphy_unregister();
}

module_init(ccic_driver_init);
module_exit(ccic_driver_exit);

MODULE_DESCRIPTION("SPACEMIT CCIC Driver");
MODULE_LICENSE("GPL");
