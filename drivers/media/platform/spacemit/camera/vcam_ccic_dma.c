/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SPACEMIT Camera Verification System - CCIC
 *
 * Copyright (C) 2021 SPACEMIT Micro Limited
 * All Rights Reserved.
 */
/* #define DEBUG */

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/poll.h>

#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/timekeeping.h>
#include <linux/platform_device.h>

#include <media/vericam/vcam_ccic_dma_uapi.h>
#include "pwrctrl.h"
#include "vcam_clk.h"
#include "vcam_ccic_dma.h"

/*********************************************************************************/
static int vcamccic_dma_major;
static struct class *vcamccic_dma_class;

#define read32(a) readl((volatile void __iomem *)(a))
#define write32(a, v) writel((v), (volatile void __iomem *)(a))

#define CCIC_DMA_REG(n) (ccic_dma_local->regs_base + (n))
#define CCIC_DMA_REG_PHY_ADDR(n) (ccic_dma_local->mem_start + (n))

#ifdef CONFIG_TIME_CALC_DEBUG
u64 ts_frm_trigger;
u64 ts_frm_finish;
#endif

static DECLARE_WAIT_QUEUE_HEAD(irq_waitq);

static inline u32 ccic_dma_reg_read(struct ccic_dma_device *ccic_dma_dev, u32 reg)
{
	return ioread32(ccic_dma_dev->regs_base + reg);
}

static inline void ccic_dma_reg_write(struct ccic_dma_device *ccic_dma_dev, u32 reg,
				  u32 val)
{
	iowrite32(val, ccic_dma_dev->regs_base + reg);
}

static inline void ccic_dma_reg_write_mask(struct ccic_dma_device *ccic_dma_dev, u32 reg,
				       u32 val, u32 mask)
{
	u32 v;

	if (0xffff8000 & reg) { /* block reg violation */
		pr_err("reg write mask violation, 0x%x", reg);
		return;
	}
	v = ccic_dma_reg_read(ccic_dma_dev, reg);
	v = (v & ~mask) | (val & mask);
	ccic_dma_reg_write(ccic_dma_dev, reg, v);
}

static inline void ccic_dma_reg_set_bit(struct ccic_dma_device *ccic_dma_dev, u32 reg,
				    u32 val)
{
	ccic_dma_reg_write_mask(ccic_dma_dev, reg, val, val);
}

static inline void ccic_dma_reg_clr_bit(struct ccic_dma_device *ccic_dma_dev, u32 reg,
				    u32 val)
{
	ccic_dma_reg_write_mask(ccic_dma_dev, reg, 0, val);
}

static void vcamccic_dma_local_reset(struct ccic_dma_device *ccic_dma_local)
{
	struct ccic_dma_irq_msg *irq_msg, *tmp_irq_msg;

	list_for_each_entry_safe (irq_msg, tmp_irq_msg,
				  &ccic_dma_local->ccic_dma_irq_msg_list, list) {
		list_del(&irq_msg->list);
		irq_msg->used = 0;
	}
	ccic_dma_local->ccic_dma_irq_msg_idx = 0;

	return;
}

__maybe_unused static int vcam_ccic_dma_init_clk(struct ccic_dma_device *ccic_dma_dev)
{
	pr_info("ccic dma to init clk\n");

	dev_info(ccic_dma_dev->dev, "---ccic dma to init clk\n");

	vcam_get_dt_reset_info(ccic_dma_dev->dev, "csi_reset",
				     &ccic_dma_dev->csi_reset);
	if (IS_ERR_OR_NULL(ccic_dma_dev->csi_reset))
		return PTR_ERR(ccic_dma_dev->csi_reset);

	vcam_get_dt_reset_info(ccic_dma_dev->dev, "ccic_4x_reset",
				     &ccic_dma_dev->ccic_4x_reset);
	if (IS_ERR_OR_NULL(ccic_dma_dev->ccic_4x_reset))
		return PTR_ERR(ccic_dma_dev->ccic_4x_reset);

	vcam_get_dt_reset_info(ccic_dma_dev->dev, "sc2_hclk_reset",
				     &ccic_dma_dev->sc2_hclk_reset);
	if (IS_ERR_OR_NULL(ccic_dma_dev->sc2_hclk_reset))
		return PTR_ERR(ccic_dma_dev->sc2_hclk_reset);

	vcam_get_dt_reset_info(ccic_dma_dev->dev, "isp_cibus_reset",
				     &ccic_dma_dev->isp_cibus_reset);
	if (IS_ERR_OR_NULL(ccic_dma_dev->isp_cibus_reset))
		return PTR_ERR(ccic_dma_dev->isp_cibus_reset);

	vcam_get_dt_clk_info(ccic_dma_dev->dev, "sc2_axi", &ccic_dma_dev->axi_clk);
	vcam_get_dt_clk_info(ccic_dma_dev->dev, "sc2_ahb", &ccic_dma_dev->ahb_clk);
	vcam_get_dt_clk_info(ccic_dma_dev->dev, "csi_func", &ccic_dma_dev->csi_clk);
	vcam_get_dt_clk_info(ccic_dma_dev->dev, "ccic_func", &ccic_dma_dev->clk4x);

	pr_info("ccic dma to init clk retun 0\n");
	return 0;
}

static int ccic_dma_set_clk_rates(struct ccic_dma_device *ccic_dma_dev)
{
	vcam_update_clock_rate(&ccic_dma_dev->axi_clk, ccic_dma_dev->axi_clk.clk_rate);
	vcam_update_clock_rate(&ccic_dma_dev->csi_clk, ccic_dma_dev->csi_clk.clk_rate);
	vcam_update_clock_rate(&ccic_dma_dev->clk4x, ccic_dma_dev->clk4x.clk_rate);
	vcam_update_clock_rate(&ccic_dma_dev->ahb_clk, ccic_dma_dev->ahb_clk.clk_rate);
	return 0;
}

__maybe_unused static int vcam_ccic_dma_power_on(struct ccic_dma_device *ccic_dma_dev)
{
	int ret = 0;

	/* get runtime pm */
	ret = pm_runtime_get_sync(ccic_dma_dev->dev);
	if (ret < 0) {
		pr_err("rpm get failed\n");
		return ret;
	}

	ret = ccic_dma_set_clk_rates(ccic_dma_dev);
	if (ret)
		return ret;

	clk_prepare_enable(ccic_dma_dev->axi_clk.clk);
	reset_control_deassert(ccic_dma_dev->isp_cibus_reset);
	clk_prepare_enable(ccic_dma_dev->ahb_clk.clk);
	reset_control_deassert(ccic_dma_dev->sc2_hclk_reset);
	clk_prepare_enable(ccic_dma_dev->clk4x.clk);
	reset_control_deassert(ccic_dma_dev->ccic_4x_reset);
	clk_prepare_enable(ccic_dma_dev->csi_clk.clk);
	reset_control_deassert(ccic_dma_dev->csi_reset);

	return ret;
}

__maybe_unused static int vcam_ccic_dma_power_off(struct ccic_dma_device *ccic_dma_dev)
{
	clk_disable_unprepare(ccic_dma_dev->csi_clk.clk);
	reset_control_assert(ccic_dma_dev->csi_reset);
	clk_disable_unprepare(ccic_dma_dev->clk4x.clk);
	reset_control_assert(ccic_dma_dev->ccic_4x_reset);
	clk_disable_unprepare(ccic_dma_dev->ahb_clk.clk);
	reset_control_assert(ccic_dma_dev->sc2_hclk_reset);
	clk_disable_unprepare(ccic_dma_dev->axi_clk.clk);
	reset_control_assert(ccic_dma_dev->isp_cibus_reset);

	pm_runtime_put_sync(ccic_dma_dev->dev);

	return 0;
}

static int vcamccic_dma_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	struct ccic_dma_device *ccic_dma_local =
		container_of(inode->i_cdev, struct ccic_dma_device, cdev);
	struct device *ccic_dma_dev;

	if (ccic_dma_local) {
		atomic_inc(&ccic_dma_local->open_cnt);
		if (1 == atomic_read(&ccic_dma_local->open_cnt)) {
#if CONFIG_CAMERA_OF_CLOCK
			vcam_ccic_dma_power_on(ccic_dma_local);
#else
			bare_ccic_dma_power_on();
#endif
			ccic_dma_dev = ccic_dma_local->dev;
			dev_info(ccic_dma_dev, "open %s success, regs_base 0x%px\n",
				 CCIC_DMA_DRV_NAME, ccic_dma_local->regs_base);
			vcamccic_dma_local_reset(ccic_dma_local);
			enable_irq(ccic_dma_local->ccic_dma_irq);
			enable_irq(ccic_dma_local->ccic_dma_mmu_irq);
			ccic_dma_local->hw_version = CCIC_DMA_HW_VERSION_ID_SPACEMIT_K3;
		}
	}

	file->private_data = ccic_dma_local;

	return ret;
}

static int vcamccic_dma_release(struct inode *inode, struct file *file)
{
	struct ccic_dma_device *ccic_dma_local =
		container_of(inode->i_cdev, struct ccic_dma_device, cdev);
	struct device *ccic_dma_dev;

	if (ccic_dma_local) {
		atomic_dec(&ccic_dma_local->open_cnt);
		if (0 == atomic_read(&ccic_dma_local->open_cnt)) {
			ccic_dma_dev = ccic_dma_local->dev;
			write32(CCIC_DMA_REG(0x2c), 0); //clear mask
			disable_irq(ccic_dma_local->ccic_dma_irq);
			disable_irq(ccic_dma_local->ccic_dma_mmu_irq);
			vcamccic_dma_local_reset(ccic_dma_local);
#if CONFIG_CAMERA_OF_CLOCK
			vcam_ccic_dma_power_off(ccic_dma_local);
#else
			bare_ccic_dma_power_off();
#endif
			dev_dbg(ccic_dma_dev, "close %s success\n", CCIC_DMA_DRV_NAME);
		}
	}

	return 0;
}

static long vcamccic_dma_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct ccic_dma_device *ccic_dma_local;
	struct device *ccic_dma_dev;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	ccic_dma_local = (struct ccic_dma_device *)file->private_data;
	ccic_dma_dev = ccic_dma_local->dev;

	if (_IOC_TYPE(cmd) != VCAM_CCIC_DMA_IOC_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case VCAM_CCIC_DMA_REG_SET: {
		CCIC_DMA_REG_INFO_S ccic_dma_reg;
		if (copy_from_user(&ccic_dma_reg, argp, sizeof(CCIC_DMA_REG_INFO_S))) {
			dev_err(ccic_dma_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
		write32(CCIC_DMA_REG(ccic_dma_reg.phyAddr), ccic_dma_reg.val);
	} break;
	case VCAM_CCIC_DMA_REG_GET: {
		CCIC_DMA_REG_INFO_S ccic_dma_reg;
		if (copy_from_user(&ccic_dma_reg, argp, sizeof(CCIC_DMA_REG_INFO_S))) {
			dev_err(ccic_dma_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
		//printk("cpp: read sensor 0x%08x\n", CPP_REG_PHY_ADDR(cpp_reg.phyAddr));
		ccic_dma_reg.val = read32(CCIC_DMA_REG(ccic_dma_reg.phyAddr));
		//printk("cpp: read sensor 0x%08x val 0x%08x\n", CPP_REG_PHY_ADDR(cpp_reg.phyAddr), cpp_reg.val);
		if (copy_to_user(argp, &ccic_dma_reg, sizeof(CCIC_DMA_REG_INFO_S))) {
			dev_err(ccic_dma_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
	} break;
	case VCAM_CCIC_DMA_GET_IRQ_STATUS: {
		struct ccic_dma_irq_msg *msg = NULL;;
		unsigned long flags;

		spin_lock_irqsave(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);
		msg = list_first_entry_or_null(&ccic_dma_local->ccic_dma_irq_msg_list,
					       struct ccic_dma_irq_msg, list);
		if (unlikely(!msg)) {
			dev_info(ccic_dma_dev, "no pending interrupt\n");
			spin_unlock_irqrestore(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);
			return -EFAULT;
		}
		list_del(&msg->list);
		msg->used = 0;
		spin_unlock_irqrestore(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);

		if (copy_to_user(argp, &msg->ccic_dma_irq_status,
				sizeof(CCIC_DMA_IRQ_INFO_S))) {
			dev_err(ccic_dma_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
	} break;
	case VCAM_CCIC_DMA_SET_HW_VERSION: {
		uint32_t ver;
		if (copy_from_user(&ver, argp, sizeof(ver))) {
			dev_err(ccic_dma_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
		ccic_dma_local->hw_version = ver;
	} break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static unsigned int vcamccic_dma_poll(struct file *file, poll_table *wait)
{
	unsigned int ret = 0;
	struct ccic_dma_device *ccic_dma_local =
		(struct ccic_dma_device *)file->private_data;
	unsigned long flags;

	poll_wait(file, &irq_waitq, wait);

	spin_lock_irqsave(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);
	if (!list_empty(&ccic_dma_local->ccic_dma_irq_msg_list))
		ret |= POLLIN;
	spin_unlock_irqrestore(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);

	return ret;
}

static const struct file_operations vcamccic_dma_fops = {
	.owner = THIS_MODULE,
	.open = vcamccic_dma_open,
	.release = vcamccic_dma_release,
	.unlocked_ioctl = vcamccic_dma_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vcamccic_dma_ioctl,
#endif
	.poll = vcamccic_dma_poll,
};

static void vcam_ccic_dma_drv_deinit(void)
{
	dev_t dev_id = MKDEV(vcamccic_dma_major, 0);

	//device_destroy(vcamccic_dma_class, MKDEV(vcamccic_dma_major, index));
	//cdev_del(&vcamccic_dma_cdev);
	class_destroy(vcamccic_dma_class);
	unregister_chrdev_region(dev_id, CCIC_DMA_MAX_DEV_NUM);
}

static int vcam_ccic_dma_drv_init(void)
{
	int ret = 0;
	dev_t dev_id;

	ret = alloc_chrdev_region(&dev_id, 0, CCIC_DMA_MAX_DEV_NUM, CCIC_DMA_DRV_NAME);
	if (ret) {
		pr_err("can't get major number\n");
		goto out;
	}

	vcamccic_dma_major = MAJOR(dev_id);

	vcamccic_dma_class = class_create(CCIC_DMA_DRV_NAME);
	if (IS_ERR(vcamccic_dma_class)) {
		ret = PTR_ERR(vcamccic_dma_class);
		goto error_cdev;
	}

out:
	return ret;

error_cdev:
	unregister_chrdev_region(dev_id, CCIC_DMA_MAX_DEV_NUM);
	return ret;
}

static void vcam_ccic_dma_dev_destroy(struct cdev *cdev, int index)
{
	if (!cdev) {
		pr_err("parameter cdev is NULL\n");
		return;
	}
	device_destroy(vcamccic_dma_class, MKDEV(vcamccic_dma_major, index));
	cdev_del(cdev);
}

static int vcam_ccic_dma_dev_create(struct cdev *cdev, int index)
{
	int ret = 0;

	if (!cdev) {
		pr_err("parameter cdev is NULL\n");
		return -1;
	}
	pr_info("ccic dma to init cdev\n");

	cdev_init(cdev, &vcamccic_dma_fops);
	pr_info("ccic dma to add cdev %d\n", index);
	ret = cdev_add(cdev, MKDEV(vcamccic_dma_major, index), 1);
	if (ret < 0) {
		pr_err("add device %d cdev fail\n", index);
		return -1;
	}
	pr_info("ccic dma to add cdev %d success\n", index);

	/* create device node */
	device_create(vcamccic_dma_class, NULL, MKDEV(vcamccic_dma_major, index), NULL,
		      "%s%d", CCIC_DMA_DRV_NAME, index);
	pr_info("ccic dma created device node %s%d\n", CCIC_DMA_DRV_NAME, index);

	return ret;
}

static irqreturn_t vcam_ccic_dma_irq(int irq, void *lp)
{
	struct ccic_dma_device *ccic_dma_local = (struct ccic_dma_device *)lp;
	uint32_t irqs0 = 0, irqs1 = 0;
	struct ccic_dma_irq_msg *msg;
	unsigned long flags;

	if (ccic_dma_local->hw_version == CCIC_DMA_HW_VERSION_ID_SPACEMIT_K3) {
		//ccic dma and tx path
		irqs0 = read32(CCIC_DMA_REG(0x114));
		write32(CCIC_DMA_REG(0x114), irqs0);
		irqs1 = read32(CCIC_DMA_REG(0x118));
		write32(CCIC_DMA_REG(0x118), irqs1);
	}

	//pr_info("irq-dma-ccic: 0x%08x 0x%08x\n", irqs0, irqs1);

	spin_lock_irqsave(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);
	msg = &ccic_dma_local->irq_msg[ccic_dma_local->ccic_dma_irq_msg_idx];
	if (msg->used == 1) {
		pr_warn_ratelimited(
			"isp-ccic-dma%d: pending dma irq number is out of %d, disable irq mask,cur=0x%x\n",
			ccic_dma_local->index, IRQ_MSG_MAX_MSG, irqs0);
		if (ccic_dma_local->hw_version == CCIC_DMA_HW_VERSION_ID_SPACEMIT_K3) {
			write32(CCIC_DMA_REG(0x124), 0);
			write32(CCIC_DMA_REG(0x128), 0);
		}
		spin_unlock_irqrestore(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);
		return IRQ_HANDLED;
	}
	msg->ccic_dma_irq_status.ccic_dma_irq_status.irq0_status = irqs0;
	msg->ccic_dma_irq_status.ccic_dma_irq_status.irq1_status = irqs1;
	msg->ccic_dma_irq_status.irq_type = 0;
	msg->used = 1;
	ccic_dma_local->ccic_dma_irq_msg_idx =
		(ccic_dma_local->ccic_dma_irq_msg_idx + 1) % IRQ_MSG_MAX_MSG;
	list_add_tail(&msg->list, &ccic_dma_local->ccic_dma_irq_msg_list);
	spin_unlock_irqrestore(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);
	wake_up_interruptible(&irq_waitq);

	return IRQ_HANDLED;
}

static irqreturn_t vcam_ccic_dma_mmu_irq(int irq, void *lp)
{
	struct ccic_dma_device *ccic_dma_local = (struct ccic_dma_device *)lp;
	uint32_t irqs = 0;
	struct ccic_dma_irq_msg *msg;
	unsigned long flags;

	if (ccic_dma_local->hw_version == CCIC_DMA_HW_VERSION_ID_SPACEMIT_K3) {
		irqs = read32(CCIC_DMA_REG(0xC00 + 0x18));
		//pr_info("ccic dma mmu irq status: 0x%08x\n", irqs);
		write32(CCIC_DMA_REG(0x114), irqs);
	}

	//pr_info("mmu irq-ccic: 0x%08x\n", irqs);

	spin_lock_irqsave(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);
	msg = &ccic_dma_local->irq_msg[ccic_dma_local->ccic_dma_irq_msg_idx];
	if (msg->used == 1) {
		pr_warn_ratelimited(
			"isp-ccic-dma%d: pending mmu irq number is out of %d, disable irq mask,cur=0x%x\n",
			ccic_dma_local->index, IRQ_MSG_MAX_MSG, irqs);
		if (ccic_dma_local->hw_version == CCIC_DMA_HW_VERSION_ID_SPACEMIT_K3) {
			write32(CCIC_DMA_REG(0xC00 + 0x1C), 0);
		}
		spin_unlock_irqrestore(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);
		return IRQ_HANDLED;
	}
	msg->ccic_dma_irq_status.ccic_dma_mmu_irq_status.mmu_irq_status = irqs;
	msg->ccic_dma_irq_status.irq_type = 1;
	msg->used = 1;
	ccic_dma_local->ccic_dma_irq_msg_idx =
		(ccic_dma_local->ccic_dma_irq_msg_idx + 1) % IRQ_MSG_MAX_MSG;
	list_add_tail(&msg->list, &ccic_dma_local->ccic_dma_irq_msg_list);
	spin_unlock_irqrestore(&ccic_dma_local->ccic_dma_irq_msg_lock, flags);
	wake_up_interruptible(&irq_waitq);

	return IRQ_HANDLED;
}

static int vcam_ccic_dma_probe(struct platform_device *pdev)
{
	struct resource *r_mem; /* IO mem resources */
	struct device *dev = &pdev->dev;
	struct ccic_dma_device *lp = NULL;
	int rc;

	pr_info("enter vcam_ccic_dma_probe\n");
	lp = (struct ccic_dma_device *)devm_kzalloc(dev, sizeof(struct ccic_dma_device),
						GFP_KERNEL);
	if (!lp) {
		dev_err(dev, "Cound not allocate vcam-ccic device\n");
		return -ENOMEM;
	}
	dev_set_drvdata(dev, lp);
	lp->dev = dev;
	spin_lock_init(&lp->dev_lock);
	spin_lock_init(&lp->ccic_dma_irq_msg_lock);
	INIT_LIST_HEAD(&lp->ccic_dma_irq_msg_list);
	atomic_set(&lp->open_cnt, 0);
	lp->hw_version = CCIC_DMA_HW_VERSION_ID_SPACEMIT_K3;

	rc = of_property_read_u32(pdev->dev.of_node, "cell-index", &pdev->id);
	if (rc < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", rc);
		return rc;
	}
	dev_info(dev, "vcam-ccic-dma probe, cell-index=%d\n", pdev->id);
	lp->index = pdev->id;
	vcam_ccic_dma_dev_create(&lp->cdev, lp->index);
	dev_info(dev, "vcam-ccic-dma device created, index=%d\n", lp->index);

	/* Get iospace for the device */
	r_mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ccic-dma-regs");
	if (!r_mem) {
		dev_err(dev, "invalid address\n");
		return -ENODEV;
	}
	lp->mem_start = r_mem->start;
	lp->mem_end = r_mem->end;
	dev_info(dev, "vcam-ccic-dma get regs resource ok\n");

	lp->regs_base =
		devm_ioremap(&pdev->dev, r_mem->start, resource_size(r_mem));
	if (!lp->regs_base) {
		dev_err(dev, "vcam-ccic: Could not allocate iomem\n");
		rc = -EIO;
		goto error2;
	}
	dev_info(dev, "vcam-ccic-dma regs_base=0x%px\n", lp->regs_base);

	/* Get IRQ for the device */
	lp->ccic_dma_irq = platform_get_irq(pdev, 0);
	if (lp->ccic_dma_irq < 0) {
		dev_info(dev, "no IRQ found\n");
		dev_info(dev, "vcam-ccic%d at 0x%lx mapped to 0x%px\n",
			 lp->index, lp->mem_start, lp->regs_base);
		return 0;
	}
	dev_info(dev, "ccic_dma_irq=%d\n", lp->ccic_dma_irq);

	rc = devm_request_irq(&pdev->dev, lp->ccic_dma_irq, vcam_ccic_dma_irq,
			      IRQF_SHARED, pdev->name, lp);
	if (rc) {
		dev_err(dev, "testmodule: Could not allocate interrupt %d.\n",
			lp->ccic_dma_irq);
		goto error3;
	}
	disable_irq(lp->ccic_dma_irq);

	/* Get IRQ for the device */
	lp->ccic_dma_mmu_irq = platform_get_irq(pdev, 1);
	if (lp->ccic_dma_mmu_irq < 0) {
		dev_info(dev, "no IRQ found\n");
		dev_info(dev, "vcam-ccic%d at 0x%lx mapped to 0x%px\n",
			 lp->index, lp->mem_start, lp->regs_base);
		return 0;
	}

	rc = devm_request_irq(&pdev->dev, lp->ccic_dma_mmu_irq, vcam_ccic_dma_mmu_irq,
			      IRQF_SHARED, pdev->name, lp);
	if (rc) {
		dev_err(dev, "testmodule: Could not allocate interrupt %d.\n",
			lp->ccic_dma_mmu_irq);
		goto error3;
	}

	disable_irq(lp->ccic_dma_mmu_irq);

	/* enable runtime pm */
	pm_runtime_enable(&pdev->dev);

#if CONFIG_CAMERA_OF_CLOCK
	vcam_ccic_dma_init_clk(lp);
#endif

	dev_info(dev, "ccic%d(%px) at 0x%lx mapped to 0x%px, ccic_dma_irq=%d, ccic_dma_mmu_irq=%d\n",
		 lp->index, lp, lp->mem_start, lp->regs_base, lp->ccic_dma_irq, lp->ccic_dma_mmu_irq);
	pr_info("exit vcam_ccic_dma_probe return 0\n");
	return 0;
error3:
	pr_info("exit vcam_ccic_dma_probe error3\n");
	// free_irq(lp->ccic_dma_irq, lp);
error2:
	pr_info("exit vcam_ccic_dma_probe error2\n");
	devm_kfree(dev, lp);
	dev_set_drvdata(dev, NULL);

	return rc;
}

static void vcam_ccic_dma_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ccic_dma_device *lp = dev_get_drvdata(dev);
	vcam_ccic_dma_dev_destroy(&lp->cdev, lp->index);
	//device_destroy(vcamccic_dma_class, MKDEV(vcamccic_dma_major, lp->index));
	// free_irq(lp->ccic_dma_irq, lp);
	// iounmap(lp->regs_base);
	// release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	devm_kfree(dev, lp);
	dev_set_drvdata(dev, NULL);
}

#ifdef CONFIG_OF
static struct of_device_id vcam_ccic_dma_of_match[] = {
	{
		.compatible = "spacemit,vcam-ccic-dma",
	},
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, vcam_ccic_dma_of_match);
#else
#define vcam_ccic_dma_of_match
#endif

static struct platform_driver vcam_ccic_dma_driver = {
	.driver = {
		.name = CCIC_DMA_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= vcam_ccic_dma_of_match,
	},
	.probe		= vcam_ccic_dma_probe,
	.remove		= vcam_ccic_dma_remove,
};

static int __init vcam_ccic_dma_init(void)
{
	int ret;

	ret = vcam_ccic_dma_drv_init();
	if (ret < 0) {
		printk("vcamccic cdev create failed\n");
		return ret;
	}
	return platform_driver_register(&vcam_ccic_dma_driver);
}

static void __exit vcam_ccic_dma_exit(void)
{
	platform_driver_unregister(&vcam_ccic_dma_driver);
	vcam_ccic_dma_drv_deinit();
}

module_init(vcam_ccic_dma_init);
module_exit(vcam_ccic_dma_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("SPACEMIT Inc.");
MODULE_DESCRIPTION("SPACEMIT Camera CCIC Driver");
