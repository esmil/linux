/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2025 Spacemit Limited
 * All Rights Reserved.
 */
#ifndef __VCAM_CCIC_DMA_UAPI_H__
#define __VCAM_CCIC_DMA_UAPI_H__

#include <linux/types.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define CCIC_DMA_DRV_NAME "vcam_ccic_dma"
#define CCIC_DMA_MAX_DEV_NUM 1

#define VCAM_CCIC_DMA_IOC_MAGIC 'I'

typedef enum CCIC_DMA_HW_VERSION_ID {
	CCIC_DMA_HW_VERSION_ID_SPACEMIT_K3 = 1,
	CCIC_HW_VERSION_ID_INVALID,
} CCIC_HW_DMA_VERSION_ID_E;

typedef enum VCAM_CCIC_DMA_IOC {
	CCIC_DMA_IOC_SET_REG = 1,
	CCIC_DMA_IOC_GET_REG,
	CCIC_DMA_IOC_GET_IRQ_STATUS,
	CCIC_DMA_IOC_SET_HW_VERSION,
} VCAM_ccic_dma_IOC_E;

typedef struct CCIC_DMA_REG_INFO {
	__u32 phyAddr;
	__u32 val;
} CCIC_DMA_REG_INFO_S;

typedef struct CCIC_DMA_IRQ_STATUS_INFO {
	__u32 irq0_status;
	__u32 irq1_status;
} CCIC_DMA_IRQ_STATUS_INFO_S;

typedef struct CCIC_DMA_MMU_IRQ_STATUS_INFO {
	__u32 mmu_irq_status;
} CCIC_DMA_MMU_IRQ_STATUS_INFO_S;

typedef struct CCIC_DMA_IRQ_INFO {
	union {
		CCIC_DMA_IRQ_STATUS_INFO_S ccic_dma_irq_status;
		CCIC_DMA_MMU_IRQ_STATUS_INFO_S ccic_dma_mmu_irq_status;
	};
	/* 0:DMA, 1:MMU	*/
	__u8 irq_type;
} CCIC_DMA_IRQ_INFO_S;

#define VCAM_CCIC_DMA_REG_SET \
	_IOW(VCAM_CCIC_DMA_IOC_MAGIC, CCIC_DMA_IOC_SET_REG, CCIC_DMA_REG_INFO_S)
#define VCAM_CCIC_DMA_REG_GET \
	_IOWR(VCAM_CCIC_DMA_IOC_MAGIC, CCIC_DMA_IOC_GET_REG, CCIC_DMA_REG_INFO_S)
#define VCAM_CCIC_DMA_GET_IRQ_STATUS \
	_IOWR(VCAM_CCIC_DMA_IOC_MAGIC, CCIC_DMA_IOC_GET_IRQ_STATUS, CCIC_DMA_IRQ_INFO_S)
#define VCAM_CCIC_DMA_SET_HW_VERSION \
	_IOW(VCAM_CCIC_DMA_IOC_MAGIC, CCIC_DMA_IOC_SET_HW_VERSION, __u32)

#if defined(__cplusplus)
}
#endif

#endif /* __VCAM_CCIC_DMA_UAPI_H__ */
