// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Spacemit K3 Adma Driver
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/genalloc.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/bitfield.h>
#include "dmaengine.h"

#define BCR	0x00  /* channel byte count register */
#define SAR	0x10  /* channel source address register */
#define DAR	0x20  /* channel destination address register */
#define NDR	0x30  /* channel next descriptor address register */
#define DCR	0x40  /* channel control register */
#define CDR	0x70  /* channel current descriptor address register */
#define IER	0x80  /* channel interrupt mask register */
#define ISR	0xA0  /* channel interrupt status register */

#define ADMA_SAMPLE_BITS_MASK		GENMASK(24, 22)
#define ADMA_SAMPLE_BITS(x)		FIELD_PREP(ADMA_SAMPLE_BITS_MASK, x)
#define ADMA_CH_ABORT			BIT(20)
#define ADMA_CLOSE_DESC_EN		BIT(17)
#define ADMA_UNPACK_SAMPLES		BIT(16)
#define ADMA_CH_ACTIVE			BIT(14)
#define ADMA_FETCH_NEXT_DESC		BIT(13)
#define ADMA_CH_EN			BIT(12)
#define ADMA_INTRRUPT_MODE		BIT(10)

#define ADMA_BURST_LIMIT_MASK		GENMASK(8, 6)
#define ADMA_BURST_1BYTE		FIELD_PREP(ADMA_BURST_LIMIT_MASK, 0x5)
#define ADMA_BURST_2BYTE		FIELD_PREP(ADMA_BURST_LIMIT_MASK, 0x6)
#define ADMA_BURST_4BYTE		FIELD_PREP(ADMA_BURST_LIMIT_MASK, 0x0)
#define ADMA_BURST_8BYTE		FIELD_PREP(ADMA_BURST_LIMIT_MASK, 0x1)
#define ADMA_BURST_16BYTE		FIELD_PREP(ADMA_BURST_LIMIT_MASK, 0x3)
#define ADMA_BURST_32BYTE		FIELD_PREP(ADMA_BURST_LIMIT_MASK, 0x7)

#define ADMA_DEST_ADDR_DIR_MASK		GENMASK(5, 4)
#define ADMA_DEST_ADDR_INCREMENT	FIELD_PREP(ADMA_DEST_ADDR_DIR_MASK, 0x0)
#define ADMA_DEST_ADDR_DECREMENT	FIELD_PREP(ADMA_DEST_ADDR_DIR_MASK, 0x1)
#define ADMA_DEST_ADDR_HOLD		FIELD_PREP(ADMA_DEST_ADDR_DIR_MASK, 0x2)

#define ADMA_SRC_ADDR_DIR_MASK		GENMASK(3, 2)
#define ADMA_SRC_ADDR_INCREMENT		FIELD_PREP(ADMA_SRC_ADDR_DIR_MASK, 0x0)
#define ADMA_SRC_ADDR_DECREMENT		FIELD_PREP(ADMA_SRC_ADDR_DIR_MASK, 0x1)
#define ADMA_SRC_ADDR_HOLD		FIELD_PREP(ADMA_SRC_ADDR_DIR_MASK, 0x2)

#define tx_to_adma_desc(tx)	\
		container_of(tx, struct adma_desc_sw, async_tx)
#define to_adma_chan(dchan)	\
		container_of(dchan, struct adma_ch, chan)
#define to_adma_dev(dmadev)	\
		container_of(dmadev, struct adma_dev, device)

enum {
	AUDIO_SAMPLE_WORD_8BITS = 0x0,
	AUDIO_SAMPLE_WORD_12BITS,
	AUDIO_SAMPLE_WORD_16BITS,
	AUDIO_SAMPLE_WORD_20BITS,
	AUDIO_SAMPLE_WORD_24BITS,
	AUDIO_SAMPLE_WORD_32BITS,
};

struct adma_desc_hw {
	u32 byte_cnt;
	u32 src_addr;
	u32 dst_addr;
	u32 nxt_desc;
};

struct adma_desc_sw {
	struct adma_desc_hw *desc;
	struct list_head node;
	struct list_head tx_list;
	struct dma_async_tx_descriptor async_tx;
};

struct adma_pchan;

struct adma_ch {
	struct device	*dev;
	struct dma_chan	chan;
	struct dma_async_tx_descriptor	desc;
	struct adma_pchan	*phy;
	struct dma_slave_config	slave_config;
	enum dma_transfer_direction	dir;
	struct adma_desc_sw *cyclic_first;

	struct tasklet_struct	tasklet;
	u32	dev_addr;

	spinlock_t desc_lock;
	struct list_head chain_pending;
	struct list_head chain_running;
	enum dma_status	status;

	struct dma_pool *sw_desc_pool;
	struct gen_pool *hw_desc_pool;
};

struct adma_pchan {
	int idx;
	int irq;
	void __iomem *base;
	struct adma_ch *vchan;
};

struct adma_dev {
	int		dma_channels;
	void __iomem	*base;
	void __iomem	*desc_base;
	dma_addr_t	desc_addr;
	unsigned long	desc_size;

	struct dma_device	device;
	struct device		*dev;
	struct adma_pchan	*phy;
};

static void adma_ch_write_reg(struct adma_pchan *phy, u32 reg_offset, u32 value)
{
	writel(value, phy->base + reg_offset + (phy->idx * 4));
}

static u32 adma_ch_read_reg(struct adma_pchan *phy, u32 reg_offset)
{
	return readl(phy->base + reg_offset + (phy->idx * 4));
}

static dma_cookie_t adma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct adma_ch *achan = to_adma_chan(tx->chan);
	struct adma_desc_sw *desc = tx_to_adma_desc(tx);
	struct adma_desc_sw *child;
	unsigned long flags;
	dma_cookie_t cookie = -EBUSY;

	spin_lock_irqsave(&achan->desc_lock, flags);
	list_for_each_entry(child, &desc->tx_list, node) {
		cookie = dma_cookie_assign(&child->async_tx);
	}

	list_splice_tail_init(&desc->tx_list, &achan->chain_pending);
	spin_unlock_irqrestore(&achan->desc_lock, flags);

	return cookie;
}

static int adma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct adma_ch *achan = to_adma_chan(dchan);
	struct adma_dev *adev = to_adma_dev(achan->chan.device);
	u32 buf_offset = 0;

	achan->sw_desc_pool = dma_pool_create(dev_name(&dchan->dev->device),
					      achan->dev,
					      sizeof(struct adma_desc_sw),
					      __alignof__(struct adma_desc_sw),
					      0);
	if (!achan->sw_desc_pool) {
		dev_err(achan->dev, "unable to allocate sw desc pool\n");
		return -ENOMEM;
	}

	achan->hw_desc_pool = gen_pool_create(4, -1);
	if (!achan->hw_desc_pool) {
		pr_err("unable to allocate hw desc pool\n");
		dma_pool_destroy(achan->sw_desc_pool);
		return -ENOMEM;
	}

	buf_offset = achan->phy->idx * adev->desc_size / 2;
	if (gen_pool_add_virt(achan->hw_desc_pool,
		(long)adev->desc_base + buf_offset,
		adev->desc_addr + buf_offset,
		adev->desc_size / 2,
		-1) != 0) {
		pr_err("gen_pool_add mem error!\n");
		gen_pool_destroy(achan->hw_desc_pool);
		dma_pool_destroy(achan->sw_desc_pool);
		return -ENOMEM;
	}

	achan->status = DMA_COMPLETE;
	achan->dir = 0;
	achan->dev_addr = 0;

	return 1;
}

static void adma_free_desc_list(struct adma_ch *chan,
					struct list_head *list)
{
	struct adma_desc_sw *desc, *_desc;

	list_for_each_entry_safe(desc, _desc, list, node) {
		list_del(&desc->node);
		gen_pool_free(chan->hw_desc_pool, (long)desc->desc, sizeof(struct adma_desc_hw));
		dma_pool_free(chan->sw_desc_pool, desc, sizeof(struct adma_desc_sw));
	}
}

static void adma_free_chan_resources(struct dma_chan *dchan)
{
	struct adma_ch *achan = to_adma_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&achan->desc_lock, flags);
	adma_free_desc_list(achan, &achan->chain_pending);
	adma_free_desc_list(achan, &achan->chain_running);
	spin_unlock_irqrestore(&achan->desc_lock, flags);
	gen_pool_destroy(achan->hw_desc_pool);
	dma_pool_destroy(achan->sw_desc_pool);
	achan->hw_desc_pool = NULL;
	achan->sw_desc_pool = NULL;
	achan->status = DMA_COMPLETE;
	achan->dir = 0;
	achan->dev_addr = 0;
}

static struct adma_desc_sw *alloc_descriptor(struct adma_ch *achan)
{
	struct adma_desc_sw *desc;
	dma_addr_t pdesc;

	desc = dma_pool_zalloc(achan->sw_desc_pool, GFP_ATOMIC, &pdesc);
	if (!desc) {
		dev_err(achan->dev, "can't alloc for sw descriptor\n");
		return NULL;
	}

	desc->desc = (struct adma_desc_hw *)gen_pool_alloc(achan->hw_desc_pool,
				sizeof(struct adma_desc_hw));
	if (!desc->desc) {
		dev_err(achan->dev, "can't alloc for hw descriptor\n");
		dma_pool_free(achan->sw_desc_pool, desc, sizeof(struct adma_desc_sw));
		return NULL;
	}

	pdesc = (dma_addr_t)gen_pool_virt_to_phys(achan->hw_desc_pool, (long)desc->desc);

	INIT_LIST_HEAD(&desc->tx_list);
	dma_async_tx_descriptor_init(&desc->async_tx, &achan->chan);
	desc->async_tx.tx_submit = adma_tx_submit;
	desc->async_tx.phys = pdesc;

	return desc;
}

static struct dma_async_tx_descriptor *
adma_prep_cyclic(struct dma_chan *dchan, dma_addr_t buf_addr,
				size_t len, size_t period_len,
				enum dma_transfer_direction direction,
				unsigned long flags)
{
	struct adma_ch *achan;
	struct adma_desc_sw *first = NULL, *prev = NULL, *new;
	dma_addr_t adma_src, adma_dst;

	achan = to_adma_chan(dchan);

	switch (direction) {
	case DMA_MEM_TO_DEV:
		adma_src = buf_addr & 0xffffffff;
		achan->dev_addr = achan->slave_config.dst_addr;
		adma_dst = achan->dev_addr;
		break;
	case DMA_DEV_TO_MEM:
		adma_dst = buf_addr & 0xffffffff;
		achan->dev_addr = achan->slave_config.src_addr;
		adma_src = achan->dev_addr;
		break;
	default:
		dev_err(achan->dev, "Unsupported direction for cyclic DMA\n");
		return NULL;
	}
	achan->dir = direction;

	do {
		new = alloc_descriptor(achan);
		if (!new) {
			dev_err(achan->dev, "no memory for desc\n");
			goto fail;
		}
		new->desc->byte_cnt = period_len;
		new->desc->src_addr = adma_src;
		new->desc->dst_addr = adma_dst;

		dev_dbg(achan->dev, "desc:0x%llx, src:0x%llx, dst:0x%llx",
			   new->async_tx.phys, adma_src, adma_dst);

		if (!first)
			first = new;
		else
			prev->desc->nxt_desc = new->async_tx.phys;
		new->async_tx.cookie = 0;
		prev = new;
		len -= period_len;

		if (achan->dir == DMA_MEM_TO_DEV)
			adma_src += period_len;
		else
			adma_dst += period_len;

		list_add_tail(&new->node, &first->tx_list);
	} while (len);

	first->async_tx.flags = flags;
	first->async_tx.cookie = -EBUSY;
	new->desc->nxt_desc = first->async_tx.phys;
	achan->cyclic_first = first;

	return &first->async_tx;

fail:
	if (first)
		adma_free_desc_list(achan, &first->tx_list);

	return NULL;
}

static int adma_config(struct dma_chan *dchan,
						struct dma_slave_config *cfg)
{
	struct adma_ch *achan = to_adma_chan(dchan);

	memcpy(&achan->slave_config, cfg, sizeof(*cfg));
	return 0;
}

static void set_desc(struct adma_pchan *phy, dma_addr_t addr)
{
	adma_ch_write_reg(phy, NDR, addr);
}

static void set_ctrl_reg(struct adma_pchan *phy)
{
	u32 ctrl_reg_val = 0;
	u32 maxburst = 0, sample_bits = 0;
	enum dma_slave_buswidth width = DMA_SLAVE_BUSWIDTH_UNDEFINED;
	struct adma_ch *achan = phy->vchan;

	if (achan->dir == DMA_MEM_TO_DEV) {
		maxburst = achan->slave_config.dst_maxburst;
		width = achan->slave_config.dst_addr_width;
		ctrl_reg_val |= ADMA_DEST_ADDR_HOLD | ADMA_SRC_ADDR_INCREMENT;
	} else if (achan->dir == DMA_DEV_TO_MEM) {
		maxburst = achan->slave_config.src_maxburst;
		width = achan->slave_config.src_addr_width;
		ctrl_reg_val |= ADMA_SRC_ADDR_HOLD | ADMA_DEST_ADDR_INCREMENT;
	} else
		ctrl_reg_val |= ADMA_SRC_ADDR_HOLD | ADMA_DEST_ADDR_HOLD;

	if (width == DMA_SLAVE_BUSWIDTH_1_BYTE)
		sample_bits = AUDIO_SAMPLE_WORD_8BITS;
	else if (width == DMA_SLAVE_BUSWIDTH_2_BYTES)
		sample_bits = AUDIO_SAMPLE_WORD_16BITS;
	else if (width == DMA_SLAVE_BUSWIDTH_3_BYTES)
		sample_bits = AUDIO_SAMPLE_WORD_24BITS;
	else if (width == DMA_SLAVE_BUSWIDTH_4_BYTES)
		sample_bits = AUDIO_SAMPLE_WORD_32BITS;

	ctrl_reg_val |= ADMA_SAMPLE_BITS(sample_bits);

	switch (maxburst) {
	case 8:
		ctrl_reg_val |= ADMA_BURST_1BYTE;
		break;
	case 16:
		ctrl_reg_val |= ADMA_BURST_2BYTE;
		break;
	case 32:
		ctrl_reg_val |= ADMA_BURST_4BYTE;
		break;
	case 64:
		ctrl_reg_val |= ADMA_BURST_8BYTE;
		break;
	case 128:
		ctrl_reg_val |= ADMA_BURST_16BYTE;
		break;
	case 256:
		ctrl_reg_val |= ADMA_BURST_32BYTE;
		break;
	default:
		ctrl_reg_val |= ADMA_BURST_4BYTE;
		break;
	}

	ctrl_reg_val |= ADMA_UNPACK_SAMPLES;
	ctrl_reg_val |= ADMA_CH_ABORT;

	adma_ch_write_reg(phy, DCR, ctrl_reg_val);
}

static void enable_chan(struct adma_pchan *phy)
{
	u32 ctrl_val;
	struct adma_ch *achan = phy->vchan;

	if (adma_ch_read_reg(phy, DCR) & BIT(14)) {
		dev_err(achan->dev, "chan%d is not idle!!!!", phy->idx);
		return;
	}

	enable_irq(phy->irq);
	if (achan->dir == DMA_MEM_TO_DEV)
		adma_ch_write_reg(phy, DAR, achan->dev_addr);
	else if (achan->dir == DMA_DEV_TO_MEM)
		adma_ch_write_reg(phy, SAR, achan->dev_addr);

	adma_ch_write_reg(phy, IER, 1);
	ctrl_val = adma_ch_read_reg(phy, DCR);
	ctrl_val |= ADMA_FETCH_NEXT_DESC;
	ctrl_val |= ADMA_CH_EN;
	adma_ch_write_reg(phy, DCR, ctrl_val);
}

static void adma_issue_pending(struct dma_chan *dchan)
{
	struct adma_ch *achan = to_adma_chan(dchan);
	struct adma_pchan *phy;
	struct adma_desc_sw *desc;
	unsigned long flags;

	spin_lock_irqsave(&achan->desc_lock, flags);
	if (achan->status == DMA_IN_PROGRESS) {
		dev_dbg(achan->dev, "DMA controller still busy\n");
		return;
	}
	phy = achan->phy;
	desc = list_first_entry(&achan->chain_pending,
							struct adma_desc_sw, node);
	list_splice_tail_init(&achan->chain_pending, &achan->chain_running);
	set_desc(phy, desc->async_tx.phys);
	set_ctrl_reg(phy);
	enable_chan(phy);
	achan->status = DMA_IN_PROGRESS;
	spin_unlock_irqrestore(&achan->desc_lock, flags);
}

static unsigned int adma_residue(struct adma_ch *chan,
				dma_cookie_t cookie)
{
	struct adma_desc_sw *sw;
	u32 curr, residue = 0;
	bool passed = false;
	bool cyclic = chan->cyclic_first != NULL;

	/*
	 * If the channel does not have a phy pointer anymore, it has already
	 * been completed. Therefore, its residue is 0.
	 */
	if (!chan->phy)
		return 0;

	if (chan->dir == DMA_DEV_TO_MEM)
		curr = adma_ch_read_reg(chan->phy, DAR);
	else
		curr = adma_ch_read_reg(chan->phy, SAR);

	list_for_each_entry(sw, &chan->chain_running, node) {
		u32 start, end, len;

		if (chan->dir == DMA_DEV_TO_MEM)
			start = sw->desc->dst_addr;
		else
			start = sw->desc->src_addr;

		len = sw->desc->byte_cnt;
		end = start + len;

		/*
		 * 'passed' will be latched once we found the descriptor which
		 * lies inside the boundaries of the curr pointer. All
		 * descriptors that occur in the list _after_ we found that
		 * partially handled descriptor are still to be processed and
		 * are hence added to the residual bytes counter.
		 */

		if (passed) {
			residue += len;
		} else if (curr >= start && curr <= end) {
			residue += end - curr;
			passed = true;
		}

		if (cyclic)
			continue;

		if (sw->async_tx.cookie == cookie) {
			return residue;
		} else {
			residue = 0;
			passed = false;
		}
	}

	/* We should only get here in case of cyclic transactions */
	return residue;
}

static enum dma_status adma_tx_status(struct dma_chan *dchan,
							dma_cookie_t cookie,
							struct dma_tx_state *txstate)
{
	struct adma_ch *chan = to_adma_chan(dchan);
	enum dma_status ret;
	unsigned long flags;

	spin_lock_irqsave(&chan->desc_lock, flags);
	ret = dma_cookie_status(dchan, cookie, txstate);
	if (likely(ret != DMA_ERROR))
		dma_set_residue(txstate, adma_residue(chan, cookie));
	spin_unlock_irqrestore(&chan->desc_lock, flags);

	if (ret == DMA_COMPLETE)
		return ret;
	else
		return chan->status;
}

static void disable_chan(struct adma_pchan *phy)
{
	u32 reg_val = adma_ch_read_reg(phy, DCR);

	reg_val |= ADMA_CH_ABORT;
	adma_ch_write_reg(phy, DCR, reg_val);

	udelay(500);
	reg_val = adma_ch_read_reg(phy, DCR);
	reg_val &= ~ADMA_CH_EN;
	adma_ch_write_reg(phy, DCR, reg_val);
	adma_ch_write_reg(phy, IER, 0);
	disable_irq_nosync(phy->irq);
}

static int adma_terminate_all(struct dma_chan *dchan)
{
	struct adma_ch *achan = to_adma_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&achan->desc_lock, flags);
	disable_chan(achan->phy);
	achan->status = DMA_COMPLETE;
	adma_free_desc_list(achan, &achan->chain_pending);
	adma_free_desc_list(achan, &achan->chain_running);
	spin_unlock_irqrestore(&achan->desc_lock, flags);

	return 0;
}

static void dma_do_tasklet(struct tasklet_struct *t)
{
	struct adma_ch *chan = from_tasklet(chan, t, tasklet);
	struct adma_desc_sw *desc;
	LIST_HEAD(chain_cleanup);
	unsigned long flags;
	struct dmaengine_desc_callback cb;

	spin_lock_irqsave(&chan->desc_lock, flags);
	if (chan->status == DMA_COMPLETE) {
		spin_unlock_irqrestore(&chan->desc_lock, flags);
		return;
	}
	desc = chan->cyclic_first;
	dmaengine_desc_get_callback(&desc->async_tx, &cb);
	spin_unlock_irqrestore(&chan->desc_lock, flags);

	dmaengine_desc_callback_invoke(&cb, NULL);
}

static struct dma_chan *adma_dma_xlate(struct of_phandle_args *dma_spec,
				struct of_dma *ofdma)
{
	struct adma_dev *adev = ofdma->of_dma_data;
	struct dma_chan *chan;
	u32 idx = dma_spec->args[0];

	chan = &adev->phy[idx].vchan->chan;
	if (!chan)
		return NULL;

	chan = dma_get_slave_channel(chan);
	if (!chan)
		return NULL;

	return chan;
}

static int clear_chan_irq(struct adma_pchan *phy)
{
	u32 status = adma_ch_read_reg(phy, ISR);

	if (!(status & BIT(0)))
		return -EAGAIN;

	/* clear irq */
	adma_ch_write_reg(phy, ISR, 0);

	return 0;
}

static irqreturn_t adma_chan_handler(int irq, void *dev_id)
{
	struct adma_pchan *phy = dev_id;

	if (clear_chan_irq(phy) != 0)
		return IRQ_NONE;

	tasklet_schedule(&phy->vchan->tasklet);
	return IRQ_HANDLED;
}

const char *irq_names[] = { "tx", "rx" };
static int adma_chan_init(struct adma_dev *adev, int idx, int irq)
{
	struct adma_pchan *phy  = &adev->phy[idx];
	struct adma_ch *chan;
	char *irq_name;
	int ret;

	chan = devm_kzalloc(adev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	phy->idx = idx;
	phy->base = adev->base;
	phy->vchan = chan;
	chan->phy = phy;

	if (irq) {
		irq_name = devm_kasprintf(adev->dev, GFP_KERNEL, "%s-%s",
					  dev_name(adev->dev), irq_names[idx]);
		if (!irq_name)
			return -ENOMEM;

		ret = devm_request_irq(adev->dev, irq, adma_chan_handler,
				       0, irq_name, phy);
		if (ret) {
			dev_err(adev->dev, "channel request irq fail!\n");
			devm_kfree(adev->dev, chan);
			return ret;
		}
		phy->irq = irq;
		disable_irq(phy->irq);
	}

	spin_lock_init(&chan->desc_lock);
	chan->dev = adev->dev;
	chan->chan.device = &adev->device;
	tasklet_setup(&chan->tasklet, dma_do_tasklet);
	INIT_LIST_HEAD(&chan->chain_pending);
	INIT_LIST_HEAD(&chan->chain_running);

	/* register virt channel to dma engine */
	list_add_tail(&chan->chan.device_node, &adev->device.channels);

	return 0;
}

static const struct of_device_id adma_id_table[] = {
	{ .compatible = "spacemit,k3-adma"},
	{},
};

static int adma_probe(struct platform_device *pdev)
{
	struct adma_dev *adev;
	struct device *dev;
	const struct of_device_id *of_id;
	int dma_channels = 0;
	int i, ret, irq = 0;
	struct resource *res;
	const enum dma_slave_buswidth widths =
		DMA_SLAVE_BUSWIDTH_1_BYTE | DMA_SLAVE_BUSWIDTH_2_BYTES |
		DMA_SLAVE_BUSWIDTH_3_BYTES | DMA_SLAVE_BUSWIDTH_4_BYTES;

	of_id = of_match_device(adma_id_table, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "Unable to match OF ID\n");
		return -ENODEV;
	}

	dev = &pdev->dev;
	adev = devm_kzalloc(dev, sizeof(*adev), GFP_KERNEL);
	if (!adev) {
		dev_err(&pdev->dev, "can't alloc memory for adma_dev\n");
		return -ENOMEM;
	}
	adev->dev = dev;
	adev->base = devm_platform_ioremap_resource_byname(pdev, "adma_reg");
	if (IS_ERR(adev->base))
		return PTR_ERR(adev->base);

	ret = of_property_match_string(dev->of_node, "reg-names", "buf_addr");
	if (ret < 0) {
		dev_err(&pdev->dev, "can't find buf_addr\n");
		return -EINVAL;
	}
	adev->desc_base = devm_platform_get_and_ioremap_resource(pdev, ret, &res);
	if (IS_ERR(adev->desc_base))
		return dev_err_probe(&pdev->dev, PTR_ERR(adev->desc_base), "failed to map desc buf addr\n");
	adev->desc_addr = res->start;
	adev->desc_size = res->end - res->start + 1;

	if (dev->of_node) {
		/* Parse dma-channels properties */
		if (of_property_read_u32(dev->of_node, "dma-channels",
					&dma_channels))
			of_property_read_u32(dev->of_node, "#dma-channels",
					&dma_channels);
	} else {
		dma_channels = 2;
	}
	adev->dma_channels = dma_channels;

	adev->phy = devm_kcalloc(dev, dma_channels, sizeof(*adev->phy),
				 GFP_KERNEL);
	if (adev->phy == NULL)
		return -ENOMEM;

	/*init adma-chan*/
	INIT_LIST_HEAD(&adev->device.channels);

	for (i = 0; i < dma_channels; i++) {
		irq = platform_get_irq(pdev, i);
		ret = adma_chan_init(adev, i, irq);
		if (ret)
			return ret;
	}

	dma_cap_set(DMA_SLAVE, adev->device.cap_mask);
	dma_cap_set(DMA_CYCLIC, adev->device.cap_mask);
	adev->device.dev = dev;
	adev->device.device_tx_status = adma_tx_status;
	adev->device.device_alloc_chan_resources = adma_alloc_chan_resources;
	adev->device.device_free_chan_resources = adma_free_chan_resources;
	adev->device.device_prep_dma_cyclic = adma_prep_cyclic;
	adev->device.device_issue_pending = adma_issue_pending;
	adev->device.device_config = adma_config;
	adev->device.device_terminate_all = adma_terminate_all;
	adev->device.copy_align = DMAENGINE_ALIGN_8_BYTES;
	adev->device.src_addr_widths = widths;
	adev->device.dst_addr_widths = widths;
	adev->device.directions = BIT(DMA_MEM_TO_DEV) | BIT(DMA_DEV_TO_MEM);
	dma_set_mask(adev->dev, adev->dev->coherent_dma_mask);

	ret = dma_async_device_register(&adev->device);
	if (ret) {
		dev_err(adev->device.dev, "unable to register\n");
		return ret;
	}

	if (pdev->dev.of_node) {
		ret = of_dma_controller_register(pdev->dev.of_node,
						adma_dma_xlate, adev);
		if (ret < 0) {
			dev_err(dev, "of_dma_controller_register failed\n");
			dma_async_device_unregister(&adev->device);
			return ret;
		}
	}

	platform_set_drvdata(pdev, adev);
	return 0;
}

static void adma_remove(struct platform_device *pdev)
{
	struct adma_dev *adev = platform_get_drvdata(pdev);

	if (pdev->dev.of_node)
		of_dma_controller_free(pdev->dev.of_node);

	dma_async_device_unregister(&adev->device);
	platform_set_drvdata(pdev, NULL);
}

static struct platform_driver adma_driver = {
	.driver	= {
		.name	= "k3-adma",
		.of_match_table	= adma_id_table,
	},
	.probe	= adma_probe,
	.remove	= adma_remove,
};

static int __init adma_init(void)
{
	return platform_driver_register(&adma_driver);
}

static void __exit adma_exit(void)
{
	platform_driver_unregister(&adma_driver);
}

subsys_initcall(adma_init);
module_exit(adma_exit);

MODULE_DESCRIPTION("ADMA Driver for SpacemiT K3 SoC");
MODULE_LICENSE("GPL v2");
