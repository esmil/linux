// SPDX-License-Identifier: GPL-2.0
/*
 * ccic_iommu.c - Driver for CCIC IOMMU
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */

#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/io.h>
#include <asm/io.h>
#include <linux/printk.h>
#include "ccic_reg_iommu.h"
#include "ccic_iommu.h"

#define DEBUG 0

#define read32(a) readl((volatile void __iomem *)(a))
#define write32(a, v) writel((v), (volatile void __iomem *)(a))

static void ccic_iommu_set_sva(struct ccic_iommu_device *mmu_dev);

static inline uint32_t iommu_reg_read(struct ccic_iommu_device *mmu_dev,
				      uint32_t reg)
{
	return read32(mmu_dev->regs_base + reg);
}

static inline void iommu_reg_write(struct ccic_iommu_device *mmu_dev,
				   uint32_t reg, uint32_t val)
{
	write32(mmu_dev->regs_base + reg, val);
}

static inline void iommu_reg_write_mask(struct ccic_iommu_device *mmu_dev,
					uint32_t reg, uint32_t val,
					uint32_t mask)
{
	uint32_t v;

	v = iommu_reg_read(mmu_dev, reg);
	v = (v & ~mask) | (val & mask);
	iommu_reg_write(mmu_dev, reg, v);
}

static inline void iommu_reg_set_bit(struct ccic_iommu_device *mmu_dev,
				     uint32_t reg, uint32_t val)
{
	iommu_reg_write_mask(mmu_dev, reg, val, val);
}

static inline void iommu_reg_clr_bit(struct ccic_iommu_device *mmu_dev,
				     uint32_t reg, uint32_t val)
{
	iommu_reg_write_mask(mmu_dev, reg, 0, val);
}

static void iommu_enable_tbu(struct ccic_iommu_device *mmu_dev, int tbu)
{
	iommu_reg_set_bit(mmu_dev, REG_IOMMU_TCR0(tbu), 0x1);
}

static void iommu_disable_tbu(struct ccic_iommu_device *mmu_dev, int tbu)
{
	iommu_reg_clr_bit(mmu_dev, REG_IOMMU_TCR0(tbu), 0x1);
}

static void iommu_set_tbu_ttaddr(struct ccic_iommu_device *mmu_dev, int tbu,
				 uint64_t addr)
{
	iommu_reg_write(mmu_dev, REG_IOMMU_TTBL(tbu), addr & 0xffffffff);
	iommu_reg_write(mmu_dev, REG_IOMMU_TTBH(tbu), (addr >> 32) & 0x1);
}

static void iommu_set_tbu_ttsize(struct ccic_iommu_device *mmu_dev, int tbu,
				 int size)
{
	/* version 2 */
#if 0
	iommu_reg_write_mask(mmu_dev, REG_IOMMU_TCR0(tbu),
			     ((size - 1) & 0x1fff) << 16, 0x1fff << 16);
#else
	iommu_reg_write_mask(mmu_dev, REG_IOMMU_TCR0(tbu),
			     ((size - 1) & 0xffff) << 16, 0xffff << 16);
#endif
}

static void __maybe_unused iommu_set_tbu_qos(struct ccic_iommu_device *mmu_dev, int tbu,
			      int qos)
{
	iommu_reg_write_mask(mmu_dev, REG_IOMMU_TCR0(tbu), (qos & 0xf) << 4,
			     0xf << 4);
}

static void iommu_enable_irqs(struct ccic_iommu_device *mmu_dev)
{
	iommu_reg_write_mask(mmu_dev, REG_IOMMU_GIRQ_ENA, 0xffffffff, 0xffffffff);
}

static inline uint32_t iommu_bva_low(struct ccic_iommu_device *mmu_dev)
{
	return iommu_reg_read(mmu_dev, REG_IOMMU_BVAL);
}

static inline uint32_t iommu_bva_high(struct ccic_iommu_device *mmu_dev)
{
	return iommu_reg_read(mmu_dev, REG_IOMMU_BVAH);
}

static int tid_to_tbu(struct ccic_iommu_device *mmu_dev, uint32_t tid)
{
	int i;

	for (i = 0; i < CCIC_IOMMU_CH_NUM; ++i) {
		if (mmu_dev->ch_matrix[i] == tid)
			return i;
	}

	return -1;
}

static int ccic_iommu_acquire_channel(struct ccic_iommu_device *mmu_dev,
				     uint32_t tid)
{
	int tbu;
	unsigned long flags;

	tbu = tid_to_tbu(mmu_dev, tid);
	if (tbu < 0) {
		pr_debug("no such channel %x to acquire\n", tid);
		return -ENODEV;
	}

	spin_lock_irqsave(&mmu_dev->ops_lock, flags);
	if (test_bit(tbu, &mmu_dev->ch_map)) {
		spin_unlock_irqrestore(&mmu_dev->ops_lock, flags);
		pr_err("channel %x not free\n", tid);
		return -EBUSY;
	}
	set_bit(tbu, &mmu_dev->ch_map);
	spin_unlock_irqrestore(&mmu_dev->ops_lock, flags);

	return 0;
}

static int ccic_iommu_release_channel(struct ccic_iommu_device *mmu_dev,
				     uint32_t tid)
{
	int tbu;
	unsigned long flags;

	tbu = tid_to_tbu(mmu_dev, tid);
	if (tbu < 0) {
		pr_err("no such channel %x to release\n", tid);
		return -ENODEV;
	}

	spin_lock_irqsave(&mmu_dev->ops_lock, flags);
	clear_bit(tbu, &mmu_dev->ch_map);
	spin_unlock_irqrestore(&mmu_dev->ops_lock, flags);

	return 0;
}

static int ccic_iommu_enable_channel(struct ccic_iommu_device *mmu_dev,
				    uint32_t tid)
{
	int tbu;
	unsigned long flags;

	ccic_iommu_set_sva(mmu_dev);
	tbu = tid_to_tbu(mmu_dev, tid);
	if (tbu < 0) {
		pr_err("no such channel %x to enable\n", tid);
		return -ENODEV;
	}

	spin_lock_irqsave(&mmu_dev->ops_lock, flags);
	/* if (!test_bit(tbu, &mmu_dev->ch_map)) {
	 *	spin_unlock_irqrestore(&mmu_dev->ops_lock, flags);
	 *	return -EPERM;
	 * }
	 */

	iommu_enable_tbu(mmu_dev, tbu);
	spin_unlock_irqrestore(&mmu_dev->ops_lock, flags);

	return 0;
}

static int ccic_iommu_disable_channel(struct ccic_iommu_device *mmu_dev,
				     uint32_t tid)
{
	int tbu;
	unsigned long flags;

	tbu = tid_to_tbu(mmu_dev, tid);
	if (tbu < 0) {
		pr_err("no such channel %x to disable\n", tid);
		return -ENODEV;
	}

	spin_lock_irqsave(&mmu_dev->ops_lock, flags);
	/* if (!test_bit(tbu, &mmu_dev->ch_map)) {
	 *	spin_unlock_irqrestore(&mmu_dev->ops_lock, flags);
	 *	return -EPERM;
	 * }
	 */

	iommu_disable_tbu(mmu_dev, tbu);
	spin_unlock_irqrestore(&mmu_dev->ops_lock, flags);

	return 0;
}

static int ccic_iommu_config_channel(struct ccic_iommu_device *mmu_dev,
				    uint32_t tid, uint64_t ttAddr,
				    uint32_t ttSize)
{
	int tbu;
	unsigned long flags;

	tbu = tid_to_tbu(mmu_dev, tid);
	if (tbu < 0) {
		pr_err("no such channel %x to configure\n", tid);
		return -ENODEV;
	}

	spin_lock_irqsave(&mmu_dev->ops_lock, flags);
	/* if (!test_bit(tbu, &mmu_dev->ch_map)) {
	 *	spin_unlock_irqrestore(&mmu_dev->ops_lock, flags);
	 *	return -EPERM;
	 * }
	 */

	/* iommu_set_tbu_qos(mmu_dev, tbu, 4); */
	iommu_set_tbu_ttaddr(mmu_dev, tbu, ttAddr);
	iommu_set_tbu_ttsize(mmu_dev, tbu, ttSize);
	/* iommu_update_trans_table(mmu_dev, tbu); */
	iommu_enable_irqs(mmu_dev);
	spin_unlock_irqrestore(&mmu_dev->ops_lock, flags);

	return 0;
}

static const uint64_t IOMMU_VADDR_BASE = 0x00000000;
static uint64_t ccic_iommu_get_sva(struct ccic_iommu_device *mmu_dev,
				  uint32_t tid, uint32_t offset)
{
	int tbu;
	uint64_t svAddr = 0, bva = 0;

	tbu = tid_to_tbu(mmu_dev, tid);
	if (tbu < 0) {
		pr_err("no such channel %x to get sva\n", tid);
		return -ENODEV;
	}
	bva = iommu_bva_high(mmu_dev);
	bva <<= 32;
	bva += iommu_bva_low(mmu_dev);
	/* version 2 */
#if 0
	svAddr = bva + 0x2000000 * (uint64_t)tbu + (offset & 0xfff);
#else
	svAddr = bva + 0x10000000 * (uint64_t)tbu + (offset & 0xfff);
#endif
	return svAddr;
}

static void ccic_iommu_set_sva(struct ccic_iommu_device *mmu_dev)
{
	iommu_reg_write(mmu_dev, REG_IOMMU_BVAL, IOMMU_VADDR_BASE);
	iommu_reg_write(mmu_dev, REG_IOMMU_BVAH, 0x0);
}

static unsigned int ccic_iommu_irq_status(struct ccic_iommu_device *mmu_dev)
{
	unsigned int status = 0;
	status = iommu_reg_read(mmu_dev, REG_IOMMU_GIRQ_STAT);
	if (status) {
		iommu_reg_write(mmu_dev, REG_IOMMU_GIRQ_STAT, status);
	}
	return status;
}

static int ccic_iommu_dump_regs(struct ccic_iommu_device *mmu_dev,
				uint32_t ch_id)
{
#if 0
	int ret = 0;
	unsigned int status = 0, tlb_size = 0;
	uint64_t addr1 = 0, addr2 = 0, addr3 = 0;

	pr_info("**************start dump ccic iommu ch%d regs:\n", ch_id);
	addr1 = iommu_reg_read(mmu_dev, REG_IOMMU_LVAL);
	status = iommu_reg_read(mmu_dev, REG_IOMMU_LVAH);
	if (status)
		addr1 = addr1 | (1ULL << 32);

	addr2 = iommu_reg_read(mmu_dev, REG_IOMMU_LPAL);
	status = iommu_reg_read(mmu_dev, REG_IOMMU_LPAH);
	if (status)
		addr2 |= (1ULL << 32);

	addr3 = iommu_reg_read(mmu_dev, REG_IOMMU_TVAL);
	status = iommu_reg_read(mmu_dev, REG_IOMMU_TVAH);
	if (status)
		addr3 = addr3 | (1ULL << 32);
	pr_info("ccic mmu: last virtual addr=0x%llx,last phy addr=0x%llx, timeout addr=0x%llx\n", addr1, addr2, addr3);

	addr1 = iommu_reg_read(mmu_dev, REG_IOMMU_TTBL(ch_id));
	status = iommu_reg_read(mmu_dev, REG_IOMMU_TTBH(ch_id));
	if (status)
		addr1 |= (1ULL << 32);
	status = iommu_reg_read(mmu_dev, REG_IOMMU_TCR0(ch_id));
	tlb_size = (status & 0x1fff0000) >> 16;
	pr_info("ccic mmu ch%d: tlb addr=0x%llx,tcr0=0x%x, tlb size=%d\n", ch_id, addr1, status, tlb_size);

	return ret;
#else
	return 0;
#endif
}

static void ccic_iommu_set_timeout_default_addr(struct ccic_iommu_device *mmu_dev, uint64_t timeout_default_addr)
{
	unsigned int high = 0, low = 0;

	low = timeout_default_addr & 0xffffffffULL;
	high = (timeout_default_addr >> 32) & 0xffffffffULL;
	iommu_reg_write(mmu_dev, REG_IOMMU_TIMEOUT_ADDR_LOW, low);
	iommu_reg_write(mmu_dev, REG_IOMMU_TIMEOUT_ADDR_HIGH, high);
}

static struct ccic_iommu_ops mmu_ops = {
	.acquire_channel = ccic_iommu_acquire_channel,
	.release_channel = ccic_iommu_release_channel,
	.enable_channel = ccic_iommu_enable_channel,
	.disable_channel = ccic_iommu_disable_channel,
	.config_channel = ccic_iommu_config_channel,
	.get_sva = ccic_iommu_get_sva,
	.irq_status = ccic_iommu_irq_status,
	.dump_channel_regs = ccic_iommu_dump_regs,
	.set_timeout_default_addr = ccic_iommu_set_timeout_default_addr,
};

static const uint32_t iommu_ch_dmac_mapping[CCIC_IOMMU_CH_NUM] = {
	MMU_TID(0), /* dma0 TBU0 */
	MMU_TID(1), /* dma1 TBU1 */
	MMU_TID(2), /* dma2 TBU2 */
	MMU_TID(3), /* dma3 TBU3 */
	MMU_TID(4), /* dma4 TBU4 */
	MMU_TID(5), /* dma5 TBU5 */
	MMU_TID(6), /* dma6 TBU6 */
	MMU_TID(7), /* dma7 TBU7 */
	MMU_TID(8), /* dma8 TBU8 */
	MMU_TID(9), /* dma9 TBU9 */
	MMU_TID(10), /* dma10 TBU10 */
	MMU_TID(11), /* dma11 TBU11 */
};

struct ccic_iommu_device *ccic_iommu_create(struct device *dev,
										unsigned long regs_base)
{
	struct ccic_iommu_device *mmu_dev = NULL;

	mmu_dev = devm_kzalloc(dev,
			       sizeof(struct ccic_iommu_device), GFP_KERNEL);
	if (!mmu_dev)
		return NULL;

	mmu_dev->regs_base = regs_base;
	mmu_dev->ops = &mmu_ops;
	memcpy(mmu_dev->ch_matrix, iommu_ch_dmac_mapping,
	       sizeof(iommu_ch_dmac_mapping));

	spin_lock_init(&mmu_dev->ops_lock);
	mmu_dev->dev = dev;

	pr_debug("%s X\n", __func__);

	return mmu_dev;
}

void ccic_iommu_unregister(struct ccic_iommu_device *mmu_dev)
{
	struct device *dev = mmu_dev->dev;
	devm_kfree(dev, mmu_dev);

	pr_debug("%s X\n", __func__);
}

#endif
