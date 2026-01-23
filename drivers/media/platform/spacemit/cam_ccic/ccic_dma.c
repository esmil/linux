// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for CCIC DMA
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
#include <linux/delay.h>
#include <linux/atomic.h>
#include "ccic_drv.h"
#include "ccic_hwreg.h"
#include "csiphy.h"
#include "ccic_vdev.h"
#include "ccic_dma.h"

#define DEBUG 0 /* for pr_debug() */

#define CCIC_DMA_DRV_NAME "spacemit_ccic_dma"
extern irqreturn_t ccic_dma_irq_handler(int irq, void *data);
#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
extern irqreturn_t ccic_mmu_irq_handler(int irq, void *data);
#endif
struct ccic_dma *g_ccic_dma = NULL;

/*
 * Device register I/O
 */
static inline u32 dma_reg_read(struct ccic_dma *ccic_dma, unsigned int reg)
{
	return ioread32(ccic_dma->base + reg);
}

static inline void dma_reg_write(struct ccic_dma *ccic_dma, unsigned int reg,
				 u32 val)
{
	iowrite32(val, ccic_dma->base + reg);
}

static inline void dma_reg_write_mask(struct ccic_dma *ccic_dma,
				      unsigned int reg, u32 val, u32 mask)
{
	u32 v = dma_reg_read(ccic_dma, reg);

	v = (v & ~mask) | (val & mask);
	dma_reg_write(ccic_dma, reg, v);
}

static inline void dma_reg_set_bit(struct ccic_dma *ccic_dma, unsigned int reg,
				   u32 val)
{
	dma_reg_write_mask(ccic_dma, reg, val, val);
}

static inline void dma_reg_clear_bit(struct ccic_dma *ccic_dma,
				     unsigned int reg, u32 val)
{
	dma_reg_write_mask(ccic_dma, reg, 0, val);
}

static int ccic_dma_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct ccic_dma *ccic_dma = NULL;
	int ret = 0;

	ret = of_property_read_u32(np, "cell-index", &pdev->id);
	if (ret < 0) {
		dev_err(dev, "failed to get alias id, errno %d\n", ret);
		return ret;
	}

	ccic_dma = devm_kzalloc(dev, sizeof(*ccic_dma), GFP_KERNEL);
	if (!ccic_dma) {
		dev_err(dev, "could not allocate ccic dma\n");
		return -ENOMEM;
	}

	/* get mem */
	ccic_dma->mem = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						     "ccic-dma-regs");
	if (!ccic_dma->mem) {
		dev_err(dev, "no mem resource");
		return -ENODEV;
	}
	ccic_dma->base = devm_ioremap(dev, ccic_dma->mem->start,
				      resource_size(ccic_dma->mem));
	if (!ccic_dma->base) {
		dev_err(dev, "fail to remap iomem\n");
		return -ENOMEM;
	}

	/* get irqs */
	ccic_dma->irq = platform_get_irq_byname(pdev, "ccic-dma-irq");
	if (ccic_dma->irq < 0) {
		dev_err(dev, "failed to get ccic dma irq\n");
		return ccic_dma->irq;
	}
	ret = devm_request_irq(dev, ccic_dma->irq, ccic_dma_irq_handler,
			       IRQF_SHARED, CCIC_DMA_DRV_NAME, ccic_dma);
	if (ret) {
		dev_err(dev, "fail to request ccic dma irq\n");
		return ret;
	}
#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
	ccic_dma->mmu_irq = platform_get_irq_byname(pdev, "ccic-dma-mmu-irq");
	if (ccic_dma->mmu_irq < 0) {
		dev_err(dev, "failed to get ccic mmu irq\n");
		return ccic_dma->mmu_irq;
	}
	ret = devm_request_irq(dev, ccic_dma->mmu_irq, ccic_mmu_irq_handler,
			       IRQF_SHARED, CCIC_DMA_DRV_NAME, ccic_dma);
	if (ret) {
		dev_err(dev, "fail to request ccic dma irq\n");
		return ret;
	}
#endif
	ccic_dma->dev = &pdev->dev;
	atomic_set(&ccic_dma->busy_cnt, 0);
	/* initialize lock protecting open/release and register writes */
	spin_lock_init(&ccic_dma->dev_lock);
	g_ccic_dma = ccic_dma;
	pr_info("%s probed", dev_name(&pdev->dev));
	return 0;
}

static void ccic_dma_remove(struct platform_device *pdev)
{
	g_ccic_dma = NULL;
}

static const struct of_device_id ccic_dt_match[] = {
	{
		.compatible = "spacemit,ccic-dma",
		.data = NULL,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ccic_dt_match);

struct platform_driver ccic_dma_driver = {
    .driver = {
        .name = CCIC_DMA_DRV_NAME,
        .of_match_table = of_match_ptr(ccic_dt_match),
    },
    .probe = ccic_dma_probe,
    .remove = ccic_dma_remove,
};

void ccic_dma_open(struct ccic_dma *ccic_dma)
{
	struct device *dev = ccic_dma->dev;
	unsigned long flags;

	spin_lock_irqsave(&ccic_dma->dev_lock, flags);
	if (atomic_inc_return(&ccic_dma->busy_cnt) == 1) {
		dev_info(dev, "ccic dma open\n");
		dma_reg_write(ccic_dma, 0x80, 0x00);
		dma_reg_write(ccic_dma, 0x7c, 0x00);
		/* dma overflow irq */
		dma_reg_write(ccic_dma, 0x128, 0xf0000000);
	}
	spin_unlock_irqrestore(&ccic_dma->dev_lock, flags);
}

void ccic_dma_release(struct ccic_dma *ccic_dma)
{
	struct device *dev = ccic_dma->dev;
	int v = 0;
	unsigned long flags;

	spin_lock_irqsave(&ccic_dma->dev_lock, flags);
	v = atomic_dec_return(&ccic_dma->busy_cnt);
	if (v == 0) {
		dev_info(dev, "ccic dma release\n");
		dma_reg_write(ccic_dma, 0x128, 0x00000000);
	} else if (v < 0) {
		dev_err(dev, "invalid ccic dma release\n");
		atomic_inc(&ccic_dma->busy_cnt);
	}
	spin_unlock_irqrestore(&ccic_dma->dev_lock, flags);
}

int ccic_dma_ch_set_fmt(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			unsigned int width, unsigned int height,
			unsigned int h_offset, unsigned int v_offset,
			unsigned int pix_fmt)
{
	struct device *dev = NULL;
	unsigned int data_fmt = C0_DF_BAYER, imgsz_w = 0, imgsz_h = 0;
	unsigned int stride = 0;
	unsigned int reg_addr = 0, reg_val = 0;

	if (!ccic_dma) {
		pr_err("ccic_dma is null\n");
		return -EINVAL;
	}
	dev = ccic_dma->dev;
	if (dma_ch >= MAX_CCIC_DMA_CNT) {
		dev_err(dev, "invalid dma_ch %u\n", dma_ch);
		return -EINVAL;
	}
	switch (pix_fmt) {
	case CSI_DUMP_FMT_YUV422:
		data_fmt = C0_DF_YUV;
		imgsz_w = width * 2;
		imgsz_h = height;
		break;
	case CSI_DUMP_FMT_RAW8:
		data_fmt = C0_DF_BAYER;
		imgsz_w = width;
		imgsz_h = height;
		break;
	case CSI_DUMP_FMT_RAW10:
		data_fmt = C0_DF_BAYER;
		imgsz_w = CAM_ALIGN(width, 4);
		imgsz_w = imgsz_w * 5 / 4;
		imgsz_h = height;
		h_offset = CAM_ALIGN(h_offset, 4);
		h_offset = h_offset * 5 / 4;
		break;
	case CSI_DUMP_FMT_RAW12:
		data_fmt = C0_DF_BAYER;
		imgsz_w = CAM_ALIGN(width, 2);
		imgsz_w = imgsz_w * 3 / 2;
		imgsz_h = height;
		h_offset = CAM_ALIGN(h_offset, 2);
		h_offset = h_offset * 3 / 2;
		break;
	default:
		pr_err("%s failed: invalid pixfmt %d\n", __func__, pix_fmt);
		return -1;
	}

	stride = CAM_ALIGN(imgsz_w, 4);

	dev_info(dev, "dma_ch=%u stride=0x%x, width=%u height=%u\n", dma_ch,
		 stride, width, height);

	//pitch
	reg_addr = 0x98 + dma_ch * 0x4;
	reg_val = stride & 0xffff;
	reg_val |= (stride << 16);
	dma_reg_write(ccic_dma, reg_addr, reg_val);

	//ch size
	reg_addr = 0xd0 + dma_ch * 0x4;
	reg_val = imgsz_w & 0xffff;
	reg_val |= (height << 16);
	dma_reg_write_mask(ccic_dma, reg_addr, reg_val, 0x7fffffff);
	return 0;
}

int ccic_dma_ch_src_sel(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			unsigned int src_sel)
{
	struct device *dev = NULL;
	unsigned int reg_val = 0, reg_mask = 0;

	if (!ccic_dma) {
		pr_err("ccic_dma is null\n");
		return -EINVAL;
	}
	dev = ccic_dma->dev;
	if (dma_ch >= MAX_CCIC_DMA_CNT) {
		dev_err(dev, "invalid dma_ch %u\n", dma_ch);
		return -EINVAL;
	}
	if (src_sel >= PATH_NUM_MAX) {
		dev_err(dev, "invalid src_sel %u\n", src_sel);
		return -EINVAL;
	}
	if (dma_ch < 4) {
		reg_mask = 0xf0000 << (dma_ch * 4);
		reg_val = (src_sel << 16) << (dma_ch * 4);
		dma_reg_write_mask(ccic_dma, 0x7c, reg_val, reg_mask);
	} else {
		reg_mask = 0xf << ((dma_ch - 4) * 4);
		reg_val = src_sel << ((dma_ch - 4) * 4);
		dma_reg_write_mask(ccic_dma, 0x80, reg_val, reg_mask);
	}

	return 0;
}

int ccic_dma_ch_enable(struct ccic_dma *ccic_dma, unsigned int dma_ch,
		       unsigned int enable)
{
	struct device *dev = NULL;
	unsigned int reg_val = 0, reg_mask = 0;

	if (!ccic_dma) {
		pr_err("ccic_dma is null\n");
		return -EINVAL;
	}
	dev = ccic_dma->dev;
	if (dma_ch >= MAX_CCIC_DMA_CNT) {
		dev_err(dev, "invalid dma_ch %u\n", dma_ch);
		return -EINVAL;
	}
	reg_mask = 1 << (dma_ch * 1);
	if (enable) {
		reg_val = reg_mask;
	} else {
		reg_val = 0;
	}
	dma_reg_write_mask(ccic_dma, 0x7c, reg_val, reg_mask);

	return 0;
}

int ccic_dma_ch_set_addr(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			 unsigned long phy_addr)
{
	struct device *dev = NULL;
	unsigned int reg_addr = 0;

	if (!ccic_dma) {
		pr_err("ccic_dma is null\n");
		return -EINVAL;
	}
	dev = ccic_dma->dev;
	if (dma_ch >= MAX_CCIC_DMA_CNT) {
		dev_err(dev, "invalid dma_ch %u\n", dma_ch);
		return -EINVAL;
	}
	reg_addr = dma_ch * 0x8;
	dma_reg_write(ccic_dma, reg_addr, phy_addr);
	if (dma_ch > 9) {
		dma_reg_write(ccic_dma, reg_addr + 0x04, phy_addr);
	}

	return 0;
}

int ccic_dma_ch_shadow_ready(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			     unsigned int ready)
{
	struct device *dev = NULL;
	unsigned int reg_addr = 0, reg_val = 0;

	if (!ccic_dma) {
		pr_err("ccic_dma is null\n");
		return -EINVAL;
	}
	dev = ccic_dma->dev;
	if (dma_ch >= MAX_CCIC_DMA_CNT) {
		dev_err(dev, "invalid dma_ch %u\n", dma_ch);
		return -EINVAL;
	}
	reg_addr = 0xd0 + dma_ch * 0x4;
	if (ready) {
		reg_val = BIT(31);
	} else {
		reg_val = 0;
	}
	dma_reg_write_mask(ccic_dma, reg_addr, reg_val, BIT(31));

	return 0;
}

int ccic_dma_ch_irq_enable(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			   unsigned int enable)
{
	struct device *dev = NULL;
	unsigned int reg_val = 0, reg_mask = 0;

	if (!ccic_dma) {
		pr_err("ccic_dma is null\n");
		return -EINVAL;
	}
	dev = ccic_dma->dev;
	if (dma_ch >= MAX_CCIC_DMA_CNT) {
		dev_err(dev, "invalid dma_ch %u\n", dma_ch);
		return -EINVAL;
	}

	if (dma_ch < 10) {
		reg_mask = 0x7 << (dma_ch * 3);
		if (enable) {
			reg_val = reg_mask;
		} else {
			reg_val = 0;
		}
		dma_reg_write_mask(ccic_dma, 0x124, reg_val, reg_mask);
	} else if (dma_ch == 10) {
		reg_mask = 0x3 << 30;
		if (enable) {
			reg_val = reg_mask;
		} else {
			reg_val = 0;
		}
		dma_reg_write_mask(ccic_dma, 0x124, reg_val, reg_mask);

		reg_mask = 0x1 << 0;
		if (enable) {
			reg_val = reg_mask;
		} else {
			reg_val = 0;
		}
		dma_reg_write_mask(ccic_dma, 0x128, reg_val, reg_mask);
	} else {
		reg_mask = 0x7 << 1;
		if (enable) {
			reg_val = reg_mask;
		} else {
			reg_val = 0;
		}
		dma_reg_write_mask(ccic_dma, 0x128, reg_val, reg_mask);
	}
	return 0;
}

unsigned int ccic_dma_get_irq0(struct ccic_dma *ccic_dma)
{
	BUG_ON(!ccic_dma);
	return dma_reg_read(ccic_dma, 0x114);
}

void ccic_dma_clear_irq0(struct ccic_dma *ccic_dma, unsigned int clear)
{
	BUG_ON(!ccic_dma);
	dma_reg_write(ccic_dma, 0x114, clear);
}

unsigned int ccic_dma_get_irq1(struct ccic_dma *ccic_dma)
{
	BUG_ON(!ccic_dma);
	return dma_reg_read(ccic_dma, 0x118);
}

void ccic_dma_clear_irq1(struct ccic_dma *ccic_dma, unsigned int clear)
{
	BUG_ON(!ccic_dma);
	dma_reg_write(ccic_dma, 0x118, clear);
}

unsigned int ccic_dma_ch_irq_analyze(unsigned int dma_ch, unsigned int irq0,
				     unsigned int irq1)
{
	unsigned int ret = 0;

	if (dma_ch < 10) {
		ret = irq0 >> (dma_ch * 3);
	} else if (dma_ch == 10) {
		ret = irq0 >> (dma_ch * 3);
		if (irq1 & BIT(0)) {
			ret |= BIT(2);
		}
	} else {
		ret = irq1 >> 1;
	}
	ret &= 0x07;

	return ret;
}

struct ccic_dma *get_ccic_dma(void)
{
	return g_ccic_dma;
}
