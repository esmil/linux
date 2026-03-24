// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit hdma controller driver support
 *
 * Copyright (c) 2025 SPACEMIT, Co. Ltd.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/adi-axi-common.h>

#include "ai_dma.h"

#define AXI_DMAC_REG_INTERFACE_DESC	0x10
#define AXI_DMAC_DMA_SRC_TYPE_MSK	GENMASK(13, 12)
#define AXI_DMAC_DMA_SRC_TYPE_GET(x)	FIELD_GET(AXI_DMAC_DMA_SRC_TYPE_MSK, x)
#define AXI_DMAC_DMA_SRC_WIDTH_MSK	GENMASK(11, 8)
#define AXI_DMAC_DMA_SRC_WIDTH_GET(x)	FIELD_GET(AXI_DMAC_DMA_SRC_WIDTH_MSK, x)
#define AXI_DMAC_DMA_DST_TYPE_MSK	GENMASK(5, 4)
#define AXI_DMAC_DMA_DST_TYPE_GET(x)	FIELD_GET(AXI_DMAC_DMA_DST_TYPE_MSK, x)
#define AXI_DMAC_DMA_DST_WIDTH_MSK	GENMASK(3, 0)
#define AXI_DMAC_DMA_DST_WIDTH_GET(x)	FIELD_GET(AXI_DMAC_DMA_DST_WIDTH_MSK, x)
#define AXI_DMAC_REG_COHERENCY_DESC	0x14
#define AXI_DMAC_DST_COHERENT_MSK	BIT(0)
#define AXI_DMAC_DST_COHERENT_GET(x)	FIELD_GET(AXI_DMAC_DST_COHERENT_MSK, x)

#define AXI_DMAC_REG_IRQ_MASK		0x80
#define AXI_DMAC_REG_IRQ_PENDING	0x84
#define AXI_DMAC_REG_IRQ_SOURCE		0x88

#define AXI_DMAC_REG_CTRL		0x400
#define AXI_DMAC_REG_TRANSFER_ID	0x404
#define AXI_DMAC_REG_START_TRANSFER	0x408
#define AXI_DMAC_REG_FLAGS		0x40c
#define AXI_DMAC_REG_DEST_ADDRESS	0x410
#define AXI_DMAC_REG_SRC_ADDRESS	0x414
#define AXI_DMAC_REG_X_LENGTH		0x418
#define AXI_DMAC_REG_Y_LENGTH		0x41c
#define AXI_DMAC_REG_DEST_STRIDE	0x420
#define AXI_DMAC_REG_SRC_STRIDE		0x424
#define AXI_DMAC_REG_TRANSFER_DONE	0x428
#define AXI_DMAC_REG_ACTIVE_TRANSFER_ID 0x42c
#define AXI_DMAC_REG_STATUS		0x430
#define AXI_DMAC_REG_CURRENT_SRC_ADDR	0x434
#define AXI_DMAC_REG_CURRENT_DEST_ADDR	0x438
#define AXI_DMAC_REG_PARTIAL_XFER_LEN	0x44c
#define AXI_DMAC_REG_PARTIAL_XFER_ID	0x450
#define AXI_DMAC_REG_CURRENT_SG_ID	0x454
#define AXI_DMAC_REG_SG_ADDRESS		0x47c
#define AXI_DMAC_REG_DEST_ADDRESS_HIGH	0x490
#define AXI_DMAC_REG_SRC_ADDRESS_HIGH	0x494
#define AXI_DMAC_REG_CUR_DEST_ADDR_HIGH	0x498
#define AXI_DMAC_REG_CUR_SRC_ADDR_HIGH	0x498
#define AXI_DMAC_REG_SG_ADDRESS_HIGH	0x4bc
#define AXI_DMAC_REG_SGDG_CFG		0x600
#define AXI_DMAC_REG_PAD_VALUE		0x604
#define AXI_DMAC_REG_SGDG_M_SIZE	0x608
#define AXI_DMAC_REG_SGDG_K_SIZE	0x60c
#define AXI_DMAC_REG_SGDG_MR_SIZE	0x610
#define AXI_DMAC_REG_SGDG_KR_SIZE	0x614
#define AXI_DMAC_REG_SGDG_MP_SIZE	0x618
#define AXI_DMAC_REG_SGDG_KP_SIZE	0x61c
#define AXI_DMAC_REG_SGDG_MB_SIZE	0x624

#define AXI_DMAC_REG_SGDG_MODE		BIT(0)
#define AXI_DMAC_REG_PACK_MODE		BIT(1)
#define AXI_DMAC_REG_TRANSPOSE		BIT(2)
#define AXI_DMAC_REG_PAD_MODE		BIT(3) | BIT(8)
#define AXI_DMAC_REG_ELE_SIZE_OFFSET	4

#define AXI_DMAC_CTRL_ENABLE		BIT(0)
#define AXI_DMAC_CTRL_PAUSE		BIT(1)
#define AXI_DMAC_CTRL_ENABLE_SG		BIT(2)

#define AXI_DMAC_IRQ_SOT		BIT(0)
#define AXI_DMAC_IRQ_EOT		BIT(1)

#define AXI_DMAC_FLAG_CYCLIC		BIT(0)
#define AXI_DMAC_FLAG_LAST		BIT(1)
#define AXI_DMAC_FLAG_PARTIAL_REPORT	BIT(2)

#define AXI_DMAC_FLAG_PARTIAL_XFER_DONE BIT(31)

/* The maximum ID allocated by the hardware is 31 */
#define AXI_DMAC_SG_UNUSED 32U

/* Flags for axi_dmac_hw_desc.flags */
#define AXI_DMAC_HW_FLAG_LAST		BIT(0)
#define AXI_DMAC_HW_FLAG_IRQ		BIT(1)

int dma_id = 0;
struct axi_dmac_info aidma_info[AXI_DMAC_NUM];
extern spinlock_t aidma_lock;

struct ai_dmac_hw_desc {
	u32 flags;
	u32 id;
	u64 dest_addr;
	u64 src_addr;
	u64 next_sg_addr;
	u32 y_len;
	u32 x_len;
	u32 src_stride;
	u32 dst_stride;
	u64 __pad[2];
};

struct ai_dmac_sg {
	struct ai_dmac_hw_desc *hw;
	dma_addr_t hw_phys;
};

struct ai_dmac_desc {
	unsigned int num_sgs;
	struct ai_dmac_sg sg[] __counted_by(num_sgs);
};

static void ai_dmac_write(struct ai_dmac *ai_dmac, unsigned int reg, unsigned int val)
{
	writel(val, ai_dmac->base + reg);
}

static int ai_dmac_read(struct ai_dmac *ai_dmac, unsigned int reg)
{
	return readl(ai_dmac->base + reg);
}

static struct ai_dmac_desc *axi_dmac_alloc_desc(struct ai_dmac *c, unsigned int num_sgs)
{
	struct ai_dmac_desc *desc;
	struct ai_dmac_hw_desc *hws;
	struct device *dev = c->dev;
	dma_addr_t hw_phys;
	unsigned int i;

	desc = kzalloc(struct_size(desc, sg, num_sgs), GFP_NOWAIT);
	if (!desc)
		return NULL;

	desc->num_sgs = num_sgs;

	hws = dma_alloc_coherent(dev, PAGE_ALIGN(num_sgs * sizeof(*hws)),
				&hw_phys, GFP_ATOMIC);
	if (!hws) {
		kfree(desc);
		return NULL;
	}

	for (i = 0; i < num_sgs; i++) {
		desc->sg[i].hw = &hws[i];
		desc->sg[i].hw_phys = hw_phys + i * sizeof(*hws);
		hws[i].id = AXI_DMAC_SG_UNUSED;
		hws[i].flags = 0;
		/* Link hardware descriptors */
		hws[i].next_sg_addr = hw_phys + (i + 1) * sizeof(*hws);
	}

	desc->sg[num_sgs - 1].hw->flags = AXI_DMAC_HW_FLAG_LAST | AXI_DMAC_HW_FLAG_IRQ;
	return desc;
}

int ai_dmac_memcpy(struct ai_dmac *c, dma_addr_t dma_dst,
		   dma_addr_t dma_src, size_t len)
{
	struct ai_dmac_desc *desc;
	struct ai_dmac_sg *dsg;
	int num_sgs = DIV_ROUND_UP(len, c->max_length);
	unsigned int i;
	unsigned int segment_size = DIV_ROUND_UP(len, num_sgs);
	unsigned int flags = 0;
	segment_size = ((segment_size - 1) | c->length_align_mask) + 1;

	ai_dmac_write(c, AXI_DMAC_REG_SGDG_CFG, 0);
	if (!c || !len)
		return -1;

	desc = axi_dmac_alloc_desc(c, num_sgs);
	if (!desc)
		return -1;
	dsg = desc->sg;
	for (i = 0; i < num_sgs; i++) {
		dsg->hw->x_len = len > segment_size ? segment_size : len - 1;
		dsg->hw->y_len = 0;
		dsg->hw->src_addr = dma_src;
		dsg->hw->dest_addr = dma_dst;
		dma_src += segment_size;
		dma_dst += segment_size;
		dsg++;
		len -= segment_size;
	}

	dsg = desc->sg;
	dsg->hw->id = ai_dmac_read(c, AXI_DMAC_REG_TRANSFER_ID);
	ai_dmac_write(c, AXI_DMAC_REG_CTRL, AXI_DMAC_CTRL_ENABLE | AXI_DMAC_CTRL_ENABLE_SG);

	ai_dmac_write(c, AXI_DMAC_REG_SG_ADDRESS, (u32)dsg->hw_phys);
	ai_dmac_write(c, AXI_DMAC_REG_SG_ADDRESS_HIGH, (u64)dsg->hw_phys >> 32);
	ai_dmac_write(c, AXI_DMAC_REG_IRQ_PENDING, 3);
	ai_dmac_write(c, AXI_DMAC_REG_FLAGS, flags);
	ai_dmac_write(c, AXI_DMAC_REG_START_TRANSFER, 1);
	return 0;
}

int ai_dmac_memcpy_by_2d(struct ai_dmac *c, dma_addr_t dma_dst, dma_addr_t dma_src, size_t len)
{
	struct ai_dmac_desc *desc;
	struct ai_dmac_sg *dsg;
	u32 y_length = len / 512;
	u32 residue = len % 512;
	int num_sgs = residue == 0 ? 2 : 3;
	unsigned int i;
	unsigned int flags = 0;

	desc = axi_dmac_alloc_desc(c, num_sgs);
	if (!desc)
		return -1;
	dsg = desc->sg;
	for (i = 0; i < 2; i++) {
		dsg->hw->x_len = 255;
		dsg->hw->y_len = y_length - 1;
		dsg->hw->src_stride = 512;
		dsg->hw->dst_stride = 512;
		if (c->id % 2) {
			if (i == 0) {
				dsg->hw->src_addr = dma_src;
				dsg->hw->dest_addr = dma_dst;
			} else {
				dsg->hw->src_addr = dma_src + 256;
				dsg->hw->dest_addr = dma_dst + 256;
			}
		} else {
			if (i == 0) {
				dsg->hw->src_addr = dma_src + 256;
				dsg->hw->dest_addr = dma_dst + 256;
			} else {
				dsg->hw->src_addr = dma_src;
				dsg->hw->dest_addr = dma_dst;
			}
		}
		dsg++;
	}

	if (num_sgs == 3) {
		dsg->hw->x_len = residue;
		dsg->hw->y_len = 0;
		dsg->hw->src_addr = dma_src + len - residue;
		dsg->hw->dest_addr = dma_dst + len - residue;
	}

	dsg = desc->sg;
	dsg->hw->id = ai_dmac_read(c, AXI_DMAC_REG_TRANSFER_ID);
	ai_dmac_write(c, AXI_DMAC_REG_SGDG_CFG, 0);
	ai_dmac_write(c, AXI_DMAC_REG_CTRL, AXI_DMAC_CTRL_ENABLE | AXI_DMAC_CTRL_ENABLE_SG);

	ai_dmac_write(c, AXI_DMAC_REG_SG_ADDRESS, (u32)dsg->hw_phys);
	ai_dmac_write(c, AXI_DMAC_REG_SG_ADDRESS_HIGH, (u64)dsg->hw_phys >> 32);
	ai_dmac_write(c, AXI_DMAC_REG_IRQ_PENDING, 3);
	ai_dmac_write(c, AXI_DMAC_REG_FLAGS, flags);
	ai_dmac_write(c, AXI_DMAC_REG_START_TRANSFER, 1);

	return 0;
}

int ai_dmac_pack_start(struct ai_dmac *c, struct ai_pack_param *param,
		       dma_addr_t dma_dst, dma_addr_t dma_src)
{
	u32 kp, mp;
	u32 reg = 0;

	kp = (param->k_size % param->kr_size) ? (param->kr_size - (param->k_size % param->kr_size)) : 0;
	mp = (param->m_size % param->mr_size) ? (param->mr_size - (param->m_size % param->mr_size)) : 0;

	ai_dmac_write(c, AXI_DMAC_REG_SRC_ADDRESS, dma_src & 0xffffffff);
	ai_dmac_write(c, AXI_DMAC_REG_DEST_ADDRESS, dma_dst & 0xffffffff);
	ai_dmac_write(c, AXI_DMAC_REG_SRC_ADDRESS_HIGH, dma_src >> 32);
	ai_dmac_write(c, AXI_DMAC_REG_DEST_ADDRESS_HIGH, dma_dst >> 32);
	if (param->sgdg == true)
		reg |= AXI_DMAC_REG_SGDG_MODE;
	if (param->pack == false)
		reg |= AXI_DMAC_REG_PACK_MODE;
	if (param->transpose == true)
		reg |= AXI_DMAC_REG_TRANSPOSE;
	if (kp || mp)
		reg |= AXI_DMAC_REG_PAD_MODE;
	reg |= param->ele_size << AXI_DMAC_REG_ELE_SIZE_OFFSET;
	ai_dmac_write(c, AXI_DMAC_REG_SGDG_CFG, reg);
	ai_dmac_write(c, AXI_DMAC_REG_CTRL, AXI_DMAC_CTRL_ENABLE | AXI_DMAC_CTRL_ENABLE_SG);

	ai_dmac_write(c, AXI_DMAC_REG_PAD_VALUE, param->pad_value);
	ai_dmac_write(c, AXI_DMAC_REG_SGDG_M_SIZE, param->m_size);
	ai_dmac_write(c, AXI_DMAC_REG_SGDG_K_SIZE, param->k_size);
	ai_dmac_write(c, AXI_DMAC_REG_SGDG_MR_SIZE, param->mr_size);
	ai_dmac_write(c, AXI_DMAC_REG_SGDG_KR_SIZE, param->kr_size);
	ai_dmac_write(c, AXI_DMAC_REG_SGDG_MP_SIZE, mp);
	ai_dmac_write(c, AXI_DMAC_REG_SGDG_KP_SIZE, kp);
	if ((param->pack == false) && (mp == 0))
		ai_dmac_write(c, AXI_DMAC_REG_SGDG_MB_SIZE, param->m_size / param->mr_size);
	else if ((param->pack == false) && (mp != 0))
		ai_dmac_write(c, AXI_DMAC_REG_SGDG_MB_SIZE, (param->m_size / param->mr_size) + 1);

	ai_dmac_write(c, AXI_DMAC_REG_FLAGS, 0);
	ai_dmac_write(c, AXI_DMAC_REG_IRQ_PENDING, 3);
	ai_dmac_write(c, AXI_DMAC_REG_SRC_STRIDE, 0);
	ai_dmac_write(c, AXI_DMAC_REG_DEST_STRIDE, 0);
	ai_dmac_write(c, AXI_DMAC_REG_X_LENGTH, 0);
	ai_dmac_write(c, AXI_DMAC_REG_Y_LENGTH, 0);
	ai_dmac_write(c, AXI_DMAC_REG_START_TRANSFER, 1);

	return 0;
}

static irqreturn_t axi_dmac_start_next_handler(int irq, void *devid)
{
	struct ai_dmac *dma = (struct ai_dmac *)devid;
	struct aidma_req *req;
	unsigned long flags;
	unsigned int pending;
	int id;

	spin_lock_irqsave(&aidma_lock, flags);
	pending = ai_dmac_read(dma, AXI_DMAC_REG_IRQ_PENDING);
	if (!pending) {
		spin_unlock_irqrestore(&aidma_lock, flags);
		return IRQ_NONE;
	}

	ai_dmac_write(dma, AXI_DMAC_REG_IRQ_PENDING, pending);
	req = aidma_info[dma->id].req;
	id = aidma_info[dma->id].work_id;
	req[id].status = DMA_REQ_DONE;
	aidma_info[dma->id].work = false;
	spin_unlock_irqrestore(&aidma_lock, flags);
	start_transfer();

	return IRQ_HANDLED;
}

static int axi_dmac_get_config(struct device *dev, struct ai_dmac *dma)
{
	unsigned int val, reg;

	reg = ai_dmac_read(dma, AXI_DMAC_REG_INTERFACE_DESC);
	if (reg == 0) {
		dev_err(dev, "DMA interface register reads zero\n");
		return -EFAULT;
	}

	val = AXI_DMAC_DMA_SRC_WIDTH_GET(reg);
	if (val == 0) {
		dev_err(dev, "Source bus width is zero\n");
		return -EINVAL;
	}
	dma->src_width = 1 << val;

	val = AXI_DMAC_DMA_DST_WIDTH_GET(reg);
        if (val == 0) {
		dev_err(dev, "Destination bus width is zero\n");
		return -EINVAL;
	}
	dma->dest_width = 1 << val;

	ai_dmac_write(dma, AXI_DMAC_REG_X_LENGTH, 0xffffffff);
	dma->max_length = ai_dmac_read(dma, AXI_DMAC_REG_X_LENGTH);
	if (dma->max_length != UINT_MAX)
		dma->max_length++;

	ai_dmac_write(dma, AXI_DMAC_REG_X_LENGTH, 0x00);
	dma->length_align_mask =
		ai_dmac_read(dma, AXI_DMAC_REG_X_LENGTH);

	return 0;
}

static int ai_dmac_probe(struct platform_device *pdev)
{
	struct ai_dmac *dma_dev;
	u32 irq_mask = 0;
	int ret;

	dma_dev = devm_kzalloc(&pdev->dev, sizeof(struct ai_dmac), GFP_KERNEL);
	if (!dma_dev)
		return -ENOMEM;

	dma_dev->irq = platform_get_irq(pdev, 0);
	if (dma_dev->irq < 0)
		return dma_dev->irq;

	dma_dev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dma_dev->base))
		return PTR_ERR(dma_dev->base);

	dma_dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dma_dev->clk))
		return PTR_ERR(dma_dev->clk);
	clk_prepare_enable(dma_dev->clk);

	dma_dev->resets = devm_reset_control_array_get_optional_shared(&pdev->dev);
	if (IS_ERR(dma_dev->resets))
		return PTR_ERR(dma_dev->resets);
	ret = reset_control_deassert(dma_dev->resets);
	if (ret)
		return ret;

	dma_dev->dev = &pdev->dev;
	ret = axi_dmac_get_config(&pdev->dev, dma_dev);
	if (ret < 0)
		return ret;

	dma_set_max_seg_size(&pdev->dev, UINT_MAX);

	irq_mask |= AXI_DMAC_IRQ_SOT;
	ai_dmac_write(dma_dev, AXI_DMAC_REG_IRQ_MASK, irq_mask);

	ret = devm_request_irq(&pdev->dev, dma_dev->irq, axi_dmac_start_next_handler,
				IRQF_SHARED, dev_name(&pdev->dev), dma_dev);
	if (ret)
		return ret;

	aidma_info[dma_id].dma = dma_dev;
	aidma_info[dma_id].work_id = -1;
	aidma_info[dma_id].req = NULL;
	aidma_info[dma_id].work = false;
	dma_dev->id = dma_id;
	dma_id++;

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(38));
	return 0;
}

static const struct of_device_id ai_dmac_of_match_table[] = {
	{ .compatible = "spacemit-ai-dmac" },
	{ },
};
MODULE_DEVICE_TABLE(of, ai_dmac_of_match_table);

static struct platform_driver ai_dmac_driver = {
	.driver = {
		.name = "ai-dmac",
		.of_match_table = ai_dmac_of_match_table,
	},
	.probe = ai_dmac_probe,
};
module_platform_driver(ai_dmac_driver);
