/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ccic_iommu.h - Driver for CCIC IOMMU
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */

#ifndef _CCIC_IOMMU_H_
#define _CCIC_IOMMU_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include "ccic_reg_iommu.h"

#define CCIC_IOMMU_TBU_NUM (12)
#define CCIC_IOMMU_CH_NUM (CCIC_IOMMU_TBU_NUM)
//#define IOMMU_TRANS_TAB_MAX_NUM (8192)
//#define IOMMU_TRANS_TAB_MAX_NUM (65536)
#define IOMMU_TRANS_TAB_MAX_NUM (32768)
#define MMU_TID(dma_ch) (dma_ch)

#define ccic_mmu_call(mmu_dev, f, args...)             \
    ({                              \
        struct ccic_iommu_device *__mmu_dev = (mmu_dev);            \
        int __result;                       \
        if (!__mmu_dev)                      \
            __result = -ENODEV;             \
        else if (!(__mmu_dev->ops && __mmu_dev->ops->f))        \
            __result = -ENOIOCTLCMD;            \
        else                            \
            __result = __mmu_dev->ops->f(__mmu_dev, ##args);   \
        __result;                       \
    })

struct mmu_ctx {
	dma_addr_t tt_addr[2][2];
	unsigned int *tt_base[2][2];
	unsigned int tbu_update_cnt[2];
};

struct iommu_ch_info {
	uint32_t tid;
	uint64_t ttAddr;
	uint32_t ttSize;
};

struct ccic_iommu_device {
	struct device *dev;
	unsigned long regs_base;
	unsigned long ch_map;
	uint32_t ch_matrix[CCIC_IOMMU_CH_NUM];
	struct iommu_ch_info info[CCIC_IOMMU_CH_NUM];
	spinlock_t ops_lock;
	struct ccic_iommu_ops *ops;
};

struct ccic_iommu_ops {
	int (*acquire_channel)(struct ccic_iommu_device *mmu_dev, uint32_t tid);
	int (*release_channel)(struct ccic_iommu_device *mmu_dev, uint32_t tid);
	int (*enable_channel)(struct ccic_iommu_device *mmu_dev, uint32_t tid);
	int (*disable_channel)(struct ccic_iommu_device *mmu_dev, uint32_t tid);
	int (*config_channel)(struct ccic_iommu_device *mmu_dev, uint32_t tid,
			      uint64_t ttAddr, uint32_t ttSize);
	uint64_t (*get_sva)(struct ccic_iommu_device *mmu_dev, uint32_t tid,
			    uint32_t offset);
	unsigned int (*irq_status)(struct ccic_iommu_device *mmu_dev);
	int (*dump_channel_regs)(struct ccic_iommu_device *mmu_dev, uint32_t tid);
	void (*set_timeout_default_addr)(struct ccic_iommu_device *mmu_dev, uint64_t timeout_default_addr);
};

struct ccic_iommu_device* ccic_iommu_create(struct device *dev, unsigned long regs_base);
void ccic_iommu_unregister(struct ccic_iommu_device *mmu_dev);
#endif /* ifndef __CCIC_IOMMU_H__ */
