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

#include <media/vericam/vcam_ccic_uapi.h>
#include "pwrctrl.h"
#include "vcam_clk.h"
#include "vcam_ccic.h"

/*********************************************************************************/
static int vcamccic_major;
static struct class *vcamccic_class;
//static struct cdev vcamccic_cdev;

#define read32(a) readl((volatile void __iomem *)(a))
#define write32(a, v) writel((v), (volatile void __iomem *)(a))

#define CCIC_REG(n) (ccic_local->regs_base + (n))
#define CCIC_REG_PHY_ADDR(n) (ccic_local->mem_start + (n))

#ifdef CONFIG_TIME_CALC_DEBUG
u64 ts_frm_trigger;
u64 ts_frm_finish;
#endif

static DECLARE_WAIT_QUEUE_HEAD(irq_waitq);
static int g_csi_clk = 0;

static inline u32 ccic_reg_read(struct ccic_device *ccic_dev, u32 reg)
{
	return ioread32(ccic_dev->regs_base + reg);
}

static inline void ccic_reg_write(struct ccic_device *ccic_dev, u32 reg,
				  u32 val)
{
	iowrite32(val, ccic_dev->regs_base + reg);
}

static inline void ccic_reg_write_mask(struct ccic_device *ccic_dev, u32 reg,
				       u32 val, u32 mask)
{
	u32 v;

	if (0xffff8000 & reg) { /* block reg violation */
		pr_err("reg write mask violation, 0x%x", reg);
		return;
	}
	v = ccic_reg_read(ccic_dev, reg);
	v = (v & ~mask) | (val & mask);
	ccic_reg_write(ccic_dev, reg, v);
}

static inline void ccic_reg_set_bit(struct ccic_device *ccic_dev, u32 reg,
				    u32 val)
{
	ccic_reg_write_mask(ccic_dev, reg, val, val);
}

static inline void ccic_reg_clr_bit(struct ccic_device *ccic_dev, u32 reg,
				    u32 val)
{
	ccic_reg_write_mask(ccic_dev, reg, 0, val);
}

static void vcamccic_local_reset(struct ccic_device *ccic_local)
{
	struct ccic_irq_msg *irq_msg, *tmp_irq_msg;

	list_for_each_entry_safe (irq_msg, tmp_irq_msg,
				  &ccic_local->ccic_irq_msg_list, list) {
		list_del(&irq_msg->list);
		irq_msg->used = 0;
	}
	ccic_local->ccic_irq_msg_idx = 0;

	return;
}

__maybe_unused static int vcam_ccic_init_clk(struct ccic_device *ccic_dev)
{
#ifdef CONFIG_ARCH_SPACEMIT
	dev_info(ccic_dev->dev, "----------to init clk\n");
	vcam_get_dt_clk_info(ccic_dev->dev, "sc2_axi", &ccic_dev->axi_clk);
	vcam_get_dt_clk_info(ccic_dev->dev, "sc2_ahb", &ccic_dev->ahb_clk);
	vcam_get_dt_clk_info(ccic_dev->dev, "csi_dphy", &ccic_dev->dphy_clk);
	vcam_get_dt_clk_info(ccic_dev->dev, "csi_func", &ccic_dev->csi_clk);
	vcam_get_dt_clk_info(ccic_dev->dev, "ccic_func", &ccic_dev->clk4x);
#endif

	return 0;
}

static int ccic_set_clk_rates(struct ccic_device *ccic)
{
	if (g_csi_clk)
		vcam_update_clock_rate(&ccic->csi_clk, g_csi_clk * 1000000);
	else
		vcam_update_clock_rate(&ccic->csi_clk, ccic->csi_clk.clk_rate);

	if (g_csi_clk)
		vcam_update_clock_rate(&ccic->clk4x, g_csi_clk * 1000000);
	else
		vcam_update_clock_rate(&ccic->clk4x, ccic->clk4x.clk_rate);

	vcam_update_clock_rate(&ccic->dphy_clk, ccic->dphy_clk.clk_rate);
	vcam_update_clock_rate(&ccic->ahb_clk, ccic->ahb_clk.clk_rate);
	vcam_update_clock_rate(&ccic->axi_clk, ccic->axi_clk.clk_rate);

	return 0;
}

__maybe_unused static int vcam_ccic_power_on(struct ccic_device *ccic_dev)
{
	int ret = 0;

	/* get runtime pm */
	ret = pm_runtime_get_sync(ccic_dev->dev);
	if (ret < 0) {
		pr_err("rpm get failed\n");
		return ret;
	}

	ret = ccic_set_clk_rates(ccic_dev);
	if (ret)
		return ret;

	clk_prepare_enable(ccic_dev->ahb_clk.clk);
	clk_prepare_enable(ccic_dev->clk4x.clk);
	clk_prepare_enable(ccic_dev->csi_clk.clk);
	clk_prepare_enable(ccic_dev->dphy_clk.clk);
	clk_prepare_enable(ccic_dev->axi_clk.clk);

	return ret;
}

__maybe_unused static int vcam_ccic_power_off(struct ccic_device *ccic_dev)
{
	clk_disable_unprepare(ccic_dev->axi_clk.clk);
	clk_disable_unprepare(ccic_dev->dphy_clk.clk);
	clk_disable_unprepare(ccic_dev->csi_clk.clk);
	clk_disable_unprepare(ccic_dev->clk4x.clk);
	clk_disable_unprepare(ccic_dev->ahb_clk.clk);

	pm_runtime_put_sync(ccic_dev->dev);

	return 0;
}

static int vcamccic_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	struct ccic_device *ccic_local =
		container_of(inode->i_cdev, struct ccic_device, cdev);
	struct device *ccic_dev;

	if (ccic_local) {
		atomic_inc(&ccic_local->open_cnt);
		if (1 == atomic_read(&ccic_local->open_cnt)) {
			ret = vcam_ccic_power_on(ccic_local);
			if (ret)
			    goto err_dec;
			ccic_dev = ccic_local->dev;
			dev_info(ccic_dev, "open %s success, regs_base 0x%px\n",
				 CCIC_DRV_NAME, ccic_local->regs_base);
			vcamccic_local_reset(ccic_local);
			enable_irq(ccic_local->ccic_irq);
			ccic_local->hw_version = CCIC_HW_VERSION_ID_NORMAL;
		}
	}

	file->private_data = ccic_local;

err_dec:
	return ret;
}

static int vcamccic_release(struct inode *inode, struct file *file)
{
	struct ccic_device *ccic_local =
		container_of(inode->i_cdev, struct ccic_device, cdev);
	struct device *ccic_dev;

	if (ccic_local) {
		atomic_dec(&ccic_local->open_cnt);
		if (0 == atomic_read(&ccic_local->open_cnt)) {
			ccic_dev = ccic_local->dev;
			write32(CCIC_REG(0x2c), 0); //clear mask
			disable_irq(ccic_local->ccic_irq);
			vcamccic_local_reset(ccic_local);
			vcam_ccic_power_off(ccic_local);
			dev_dbg(ccic_dev, "close %s success\n", CCIC_DRV_NAME);
		}
	}

	return 0;
}

static long vcamccic_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct ccic_device *ccic_local;
	struct device *ccic_dev;
	void __user *argp = (void __user *)arg;
	int ret = 0;

	ccic_local = (struct ccic_device *)file->private_data;
	ccic_dev = ccic_local->dev;

	if (_IOC_TYPE(cmd) != VCAM_CCIC_IOC_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case VCAM_CCIC_REG_SET: {
		CCIC_REG_INFO_S ccic_reg;
		if (copy_from_user(&ccic_reg, argp, sizeof(CCIC_REG_INFO_S))) {
			dev_err(ccic_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
		write32(CCIC_REG(ccic_reg.phyAddr), ccic_reg.val);
	} break;
	case VCAM_CCIC_REG_GET: {
		CCIC_REG_INFO_S ccic_reg;
		if (copy_from_user(&ccic_reg, argp, sizeof(CCIC_REG_INFO_S))) {
			dev_err(ccic_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
		//printk("cpp: read sensor 0x%08x\n", CPP_REG_PHY_ADDR(cpp_reg.phyAddr));
		ccic_reg.val = read32(CCIC_REG(ccic_reg.phyAddr));
		//printk("cpp: read sensor 0x%08x val 0x%08x\n", CPP_REG_PHY_ADDR(cpp_reg.phyAddr), cpp_reg.val);
		if (copy_to_user(argp, &ccic_reg, sizeof(CCIC_REG_INFO_S))) {
			dev_err(ccic_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
	} break;
	case VCAM_CCIC_GET_ISP_IRQ_STATUS: {
		struct ccic_irq_msg *msg;
		unsigned long flags;

		spin_lock_irqsave(&ccic_local->ccic_irq_msg_lock, flags);
		msg = list_first_entry_or_null(&ccic_local->ccic_irq_msg_list,
					       struct ccic_irq_msg, list);
		if (unlikely(!msg)) {
			spin_unlock_irqrestore(&ccic_local->ccic_irq_msg_lock,
					       flags);
			dev_info(ccic_dev, "no pending interrupt\n");
			return -EFAULT;
		}
		list_del(&msg->list);
		msg->used = 0;
		spin_unlock_irqrestore(&ccic_local->ccic_irq_msg_lock, flags);
		if (copy_to_user(argp, &msg->ccic_irq_status,
				 sizeof(CCIC_IRQ_INFO_S))) {
			dev_err(ccic_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
	} break;
	case VCAM_CCIC_SET_HW_VERSION: {
		uint32_t ver;
		if (copy_from_user(&ver, argp, sizeof(ver))) {
			dev_err(ccic_dev, "Failed to copy args from user\n");
			return -EFAULT;
		}
		ccic_local->hw_version = ver;
	} break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static unsigned int vcamccic_poll(struct file *file, poll_table *wait)
{
	unsigned int ret = 0;
	struct ccic_device *ccic_local =
		(struct ccic_device *)file->private_data;
	unsigned long flags;

	poll_wait(file, &irq_waitq, wait);

	spin_lock_irqsave(&ccic_local->ccic_irq_msg_lock, flags);
	if (!list_empty(&ccic_local->ccic_irq_msg_list))
		ret |= POLLIN;
	spin_unlock_irqrestore(&ccic_local->ccic_irq_msg_lock, flags);

	return ret;
}

static const struct file_operations vcamccic_fops = {
	.owner = THIS_MODULE,
	.open = vcamccic_open,
	.release = vcamccic_release,
	.unlocked_ioctl = vcamccic_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vcamccic_ioctl,
#endif
	.poll = vcamccic_poll,
};

static void vcam_ccic_drv_deinit(void)
{
	dev_t dev_id = MKDEV(vcamccic_major, 0);

	//device_destroy(vcamccic_class, MKDEV(vcamccic_major, index));
	//cdev_del(&vcamccic_cdev);
	class_destroy(vcamccic_class);
	unregister_chrdev_region(dev_id, CCIC_MAX_DEV_NUM);
}

static int vcam_ccic_drv_init(void)
{
	int ret = 0;
	dev_t dev_id;

	ret = alloc_chrdev_region(&dev_id, 0, CCIC_MAX_DEV_NUM, CCIC_DRV_NAME);
	if (ret) {
		pr_err("can't get major number\n");
		goto out;
	}

	vcamccic_major = MAJOR(dev_id);

	vcamccic_class = class_create(CCIC_DRV_NAME);
	if (IS_ERR(vcamccic_class)) {
		ret = PTR_ERR(vcamccic_class);
		goto error_cdev;
	}

out:
	return ret;

error_cdev:
	unregister_chrdev_region(dev_id, CCIC_MAX_DEV_NUM);
	return ret;
}

static void vcam_ccic_dev_destroy(struct cdev *cdev, int index)
{
	if (!cdev) {
		pr_err("parameter cdev is NULL\n");
		return;
	}
	device_destroy(vcamccic_class, MKDEV(vcamccic_major, index));
	cdev_del(cdev);
}

static int vcam_ccic_dev_create(struct cdev *cdev, int index)
{
	int ret = 0;

	if (!cdev) {
		pr_err("parameter cdev is NULL\n");
		return -1;
	}

	cdev_init(cdev, &vcamccic_fops);
	ret = cdev_add(cdev, MKDEV(vcamccic_major, index), 1);
	if (ret < 0) {
		pr_err("add device %d cdev fail\n", index);
		return -1;
	}

	/* create device node */
	device_create(vcamccic_class, NULL, MKDEV(vcamccic_major, index), NULL,
		      "%s%d", CCIC_DRV_NAME, index);

	return ret;
}

static irqreturn_t vcam_ccic_irq(int irq, void *lp)
{
	struct ccic_device *ccic_local = (struct ccic_device *)lp;
	uint32_t irqs = 0, irqs1 = 0;
	struct ccic_irq_msg *msg;
	unsigned long flags;

#if 0
	static const char *const ccic_err_msg[] = {
		"End of Frame IRQ",
		"Start of Frame IRQ",
		"CSI EOF IRQ",
		"CSI SOF IRQ",
		"DMA not done at frame start IRQ",
		"Shadow bit not ready at frame start IRQ",
		"FIFO Full IRQ",
		"CCIC Programmable Line IRQ",
		"IDI Programmable Line IRQ",
		"CSI2IDI DATA FLUSH IRQ",
		"HBLK_TO_HSYNC IRQ",
		"AXI Write Error IRQ",
		"DPHY Rx CLKULPS Active IRQ",
		"DPHY Rx CLKULPS IRQ",
		"DPHY Lane ULPS Active IRQ",
		"DPHY Lane Error Control IRQ",
		"DPHY Lane Start of Transmission Sync Error IRQ",
		"DPHY Lane Start of Transmission Error IRQ",
		"DPHY receiver Line Error IRQ",
		"DPCM/Repack IRQ",
		"End of Frame DMA.",
		"End of Frame with no data IRQon DMA side",
		"ISP CLK DFC IRQ",
		"CSI2 Packet Error IRQ",
		"CSI2 CRC Error IRQ",
		"CSI2 ECC 2-bit Error IRQ",
		"CSI2 Patiry Error IRQ",
		"CSI2 ECC Correctable Error IRQ",
		"CSI2 Lane FIFO Overrun Error IRQ",
		"CSI2 Parse Error IRQ",
		"CSI2 Generic Short Packet Valid IRQ",
		"CSI2 Generic Short Packet Error IRQ",
	};
#endif

	if (ccic_local->hw_version == CCIC_HW_VERSION_ID_ARASAN_RX_CONTRL_TX_CONTRL) {
		if (ccic_local->index == 1) {
			//ccic dma and tx path
			irqs = read32(CCIC_REG(0x114));
			write32(CCIC_REG(0x114), irqs);
			irqs = irqs & 0xfff;
			irqs1 = read32(CCIC_REG(0x118));
			write32(CCIC_REG(0x118), irqs1);
			irqs |= (irqs1 & 0xf0000000);
		} else {
			irqs = read32(CCIC_REG(0x188));
			write32(CCIC_REG(0x188), irqs);
		}
	} else if (ccic_local->hw_version == CCIC_HW_VERSION_ID_ARASAN_RX_CONTRL) {
		irqs = read32(CCIC_REG(0x188));
		write32(CCIC_REG(0x188), irqs);
	} else {
		irqs = read32(CCIC_REG(0x30));
		write32(CCIC_REG(0x30), irqs);
	}

	//pr_debug("irq-ccic: 0x%08x\n", irqs);

	spin_lock_irqsave(&ccic_local->ccic_irq_msg_lock, flags);
	msg = &ccic_local->irq_msg[ccic_local->ccic_irq_msg_idx];
	if (msg->used == 1) {
		pr_warn_ratelimited(
			"isp-ccic%d: pending pipeline irq number is out of %d, disable irq mask,cur=0x%x\n",
			ccic_local->index, IRQ_MSG_MAX_MSG, irqs);
		if (ccic_local->hw_version ==
		    CCIC_HW_VERSION_ID_ARASAN_RX_CONTRL_TX_CONTRL) {
			if (ccic_local->index == 1) {
				//ccic dma and tx path
				write32(CCIC_REG(0x124), 0);
				write32(CCIC_REG(0x128), 0);
			} else
				write32(CCIC_REG(0x18c), 0);
		} else if (ccic_local->hw_version == CCIC_HW_VERSION_ID_ARASAN_RX_CONTRL) {
			write32(CCIC_REG(0x18c), 0);
		} else {
			write32(CCIC_REG(0x2c), 0);
		}
		spin_unlock_irqrestore(&ccic_local->ccic_irq_msg_lock, flags);
		return IRQ_HANDLED;
	}
	msg->ccic_irq_status.ccic_irq_status = irqs;
	msg->used = 1;
	ccic_local->ccic_irq_msg_idx =
		(ccic_local->ccic_irq_msg_idx + 1) % IRQ_MSG_MAX_MSG;
	list_add_tail(&msg->list, &ccic_local->ccic_irq_msg_list);
	spin_unlock_irqrestore(&ccic_local->ccic_irq_msg_lock, flags);
	wake_up_interruptible(&irq_waitq);

	return IRQ_HANDLED;
}

static int vcam_ccic_probe(struct platform_device *pdev)
{
	struct resource *r_mem; /* IO mem resources */
	struct device *dev = &pdev->dev;
	struct ccic_device *lp = NULL;
	int rc;

	lp = (struct ccic_device *)devm_kzalloc(dev, sizeof(struct ccic_device),
						GFP_KERNEL);
	if (!lp) {
		dev_err(dev, "Cound not allocate vcam-ccic device\n");
		return -ENOMEM;
	}
	dev_set_drvdata(dev, lp);
	lp->dev = dev;
	spin_lock_init(&lp->dev_lock);
	spin_lock_init(&lp->ccic_irq_msg_lock);
	INIT_LIST_HEAD(&lp->ccic_irq_msg_list);
	atomic_set(&lp->open_cnt, 0);

	rc = of_property_read_u32(pdev->dev.of_node, "cell-index", &pdev->id);
	if (rc < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", rc);
		return rc;
	}
	lp->index = pdev->id;
	vcam_ccic_dev_create(&lp->cdev, lp->index);

	/* Get iospace for the device */
	r_mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ccic-regs");
	if (!r_mem) {
		dev_err(dev, "invalid address\n");
		return -ENODEV;
	}
	lp->mem_start = r_mem->start;
	lp->mem_end = r_mem->end;

	lp->regs_base =
		devm_ioremap(&pdev->dev, r_mem->start, resource_size(r_mem));
	if (!lp->regs_base) {
		dev_err(dev, "vcam-ccic: Could not allocate iomem\n");
		rc = -EIO;
		goto error2;
	}

	/* Get IRQ for the device */
	lp->ccic_irq = platform_get_irq(pdev, 0);
	if (lp->ccic_irq < 0) {
		dev_info(dev, "no IRQ found\n");
		dev_info(dev, "vcam-ccic%d at 0x%lx mapped to 0x%px\n",
			 lp->index, lp->mem_start, lp->regs_base);
		return 0;
	}
	rc = devm_request_irq(&pdev->dev, lp->ccic_irq, vcam_ccic_irq,
			      IRQF_SHARED, pdev->name, lp);
	if (rc) {
		dev_err(dev, "testmodule: Could not allocate interrupt %d.\n",
			lp->ccic_irq);
		goto error3;
	}
	disable_irq(lp->ccic_irq);

	/* enable runtime pm */
	pm_runtime_enable(&pdev->dev);

	vcam_ccic_init_clk(lp);

	dev_info(dev, "ccic%d(%px) at 0x%lx mapped to 0x%px, ccic_irq=%d\n",
		 lp->index, lp, lp->mem_start, lp->regs_base, lp->ccic_irq);

	return 0;
error3:
	// free_irq(lp->ccic_irq, lp);
error2:
	devm_kfree(dev, lp);
	dev_set_drvdata(dev, NULL);

	return rc;
}

static void vcam_ccic_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ccic_device *lp = dev_get_drvdata(dev);
	vcam_ccic_dev_destroy(&lp->cdev, lp->index);
	//device_destroy(vcamccic_class, MKDEV(vcamccic_major, lp->index));
	// free_irq(lp->ccic_irq, lp);
	// iounmap(lp->regs_base);
	// release_mem_region(lp->mem_start, lp->mem_end - lp->mem_start + 1);
	devm_kfree(dev, lp);
	dev_set_drvdata(dev, NULL);
}

#ifdef CONFIG_OF
static struct of_device_id vcam_ccic_of_match[] = {
	{
		.compatible = "spacemit,vcam-ccic",
	},
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, vcam_ccic_of_match);
#else
#define vcam_ccic_of_match
#endif

static struct platform_driver vcam_ccic_driver = {
	.driver = {
		.name = CCIC_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= vcam_ccic_of_match,
	},
	.probe		= vcam_ccic_probe,
	.remove		= vcam_ccic_remove,
};

static int __init vcam_ccic_init(void)
{
	int ret;

	ret = vcam_ccic_drv_init();
	if (ret < 0) {
		printk("vcamccic cdev create failed\n");
		return ret;
	}
	return platform_driver_register(&vcam_ccic_driver);
}

static void __exit vcam_ccic_exit(void)
{
	platform_driver_unregister(&vcam_ccic_driver);
	vcam_ccic_drv_deinit();
}

module_init(vcam_ccic_init);
module_exit(vcam_ccic_exit);

module_param(g_csi_clk, int, 0644);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("SPACEMIT Inc.");
MODULE_DESCRIPTION("SPACEMIT Camera CCIC Driver");
