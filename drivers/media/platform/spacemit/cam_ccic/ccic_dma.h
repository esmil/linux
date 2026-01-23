/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ccic_dma.h - Driver for CCIC DMA
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */

#ifndef _CCIC_DMA_H_
#define _CCIC_DMA_H_

#define MAX_CCIC_DMA_CNT (12)

struct ccic_dma {
	int index;
	struct device *dev;
	struct platform_device *pdev;
	int irq;
#if defined(CONFIG_SPACEMIT_K3_CCIC_IOMMU)
	int mmu_irq;
#endif
	struct resource *mem;
	void __iomem *base;
	struct mutex ops_mutex;
	spinlock_t dev_lock;
	//struct ccic_dma_ops *ops;
	//void *vnode;
	atomic_t busy_cnt;
};

struct ccic_dma *get_ccic_dma(void);
void ccic_dma_open(struct ccic_dma *ccic_dma);
void ccic_dma_release(struct ccic_dma *ccic_dma);
int ccic_dma_clock_enable(struct ccic_dma *ccic_dma);
int ccic_dma_ch_set_fmt(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			unsigned int width, unsigned int height,
			unsigned int h_offset, unsigned int v_offset,
			unsigned int pix_fmt);
int ccic_dma_ch_src_sel(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			unsigned int src_sel);
int ccic_dma_ch_enable(struct ccic_dma *ccic_dma, unsigned int dma_ch,
		       unsigned int enable);
int ccic_dma_ch_set_addr(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			 unsigned long phy_addr);
int ccic_dma_ch_shadow_ready(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			     unsigned int ready);
int ccic_dma_ch_irq_enable(struct ccic_dma *ccic_dma, unsigned int dma_ch,
			   unsigned int enable);
unsigned int ccic_dma_get_irq0(struct ccic_dma *ccic_dma);
unsigned int ccic_dma_get_irq1(struct ccic_dma *ccic_dma);
void ccic_dma_clear_irq0(struct ccic_dma *ccic_dma, unsigned int clear);
void ccic_dma_clear_irq1(struct ccic_dma *ccic_dma, unsigned int clear);
#define DMA_IRQ_SOF BIT(0)
#define DMA_IRQ_DONE BIT(1)
#define DMA_IRQ_ERR BIT(2)
unsigned int ccic_dma_ch_irq_analyze(unsigned int dma_ch, unsigned int irq0,
				     unsigned int irq1);
#endif
