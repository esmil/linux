/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SPACEMIT Camera Verification System - CCIC
 *
 * Copyright (C) 2021 SPACEMIT Micro Limited
 * All Rights Reserved.
 */

#ifndef __VCAM_CCIC_H__
#define __VCAM_CCIC_H__

#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <media/vericam/vcam_ccic_uapi.h>
#include "vcam_clk.h"

#define IRQ_MSG_MAX_MSG 100

struct ccic_irq_msg {
	struct list_head list;
	CCIC_IRQ_INFO_S ccic_irq_status;
	uint8_t used;
};

struct ccic_device {
	struct device *dev;
	struct cdev cdev;
	spinlock_t dev_lock;
	int index;
	int hw_version;
	atomic_t open_cnt;

	int ccic_irq;
	unsigned long mem_start;
	unsigned long mem_end;
	void __iomem *regs_base;

	struct list_head ccic_irq_msg_list;
	struct ccic_irq_msg irq_msg[IRQ_MSG_MAX_MSG];
	int ccic_irq_msg_idx;
	spinlock_t ccic_irq_msg_lock;

	struct vcam_clk_info dphy_clk;
	struct vcam_clk_info csi_clk;
	struct vcam_clk_info clk4x;
	struct vcam_clk_info ahb_clk;
	struct vcam_clk_info axi_clk;

	struct reset_control *csi_dphy_reset;
	struct reset_control *csi_reset;
	struct reset_control *ccic_4x_reset;
	struct reset_control *sc2_hclk_reset;
	struct reset_control *isp_cibus_reset;
};
#endif
