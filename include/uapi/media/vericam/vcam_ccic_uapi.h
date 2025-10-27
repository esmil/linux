/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2025 Spacemit Limited
 * All Rights Reserved.
 */
#ifndef __VCAM_CCIC_UAPI_H__
#define __VCAM_CCIC_UAPI_H__

#include <linux/types.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define CCIC_DRV_NAME "vcam_ccic"
#define CCIC_MAX_DEV_NUM 4

#define VCAM_CCIC_IOC_MAGIC 'I'

typedef enum CCIC_HW_VERSION_ID {
	CCIC_HW_VERSION_ID_NORMAL = 1,
	CCIC_HW_VERSION_ID_ARASAN_RX_CONTRL_TX_CONTRL,
	CCIC_HW_VERSION_ID_ARASAN_RX_CONTRL,
	CCIC_HW_VERSION_ID_INVALID,
} CCIC_HW_VERSION_ID_E;

typedef enum VCAM_CCIC_IOC {
	CCIC_IOC_SET_REG = 1,
	CCIC_IOC_GET_REG,
	CCIC_IOC_GET_ISP_IRQ_STATUS,
	CCIC_IOC_SET_HW_VERSION,
} VCAM_CCIC_IOC_E;

typedef struct CCIC_REG_INFO {
	__u32 phyAddr;
	__u32 val;
} CCIC_REG_INFO_S;

typedef struct CCIC_IRQ_INFO {
	__u32 ccic_irq_status;
} CCIC_IRQ_INFO_S;

#define VCAM_CCIC_REG_SET \
	_IOW(VCAM_CCIC_IOC_MAGIC, CCIC_IOC_SET_REG, CCIC_REG_INFO_S)
#define VCAM_CCIC_REG_GET \
	_IOWR(VCAM_CCIC_IOC_MAGIC, CCIC_IOC_GET_REG, CCIC_REG_INFO_S)
#define VCAM_CCIC_GET_ISP_IRQ_STATUS \
	_IOWR(VCAM_CCIC_IOC_MAGIC, CCIC_IOC_GET_ISP_IRQ_STATUS, CCIC_IRQ_INFO_S)
#define VCAM_CCIC_SET_HW_VERSION \
	_IOW(VCAM_CCIC_IOC_MAGIC, CCIC_IOC_SET_HW_VERSION, __u32)

#if defined(__cplusplus)
}
#endif

#endif /* __VCAM_CCIC_UAPI_H__ */
