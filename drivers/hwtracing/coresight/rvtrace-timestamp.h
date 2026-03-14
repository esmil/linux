/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2026 Spacemit Limited. All rights reserved.
 * Author: liangzhen <zhen.liang@spacemit.com>
 */

#ifndef _RVTRACE_TIMESTAMP_H
#define _RVTRACE_TIMESTAMP_H

#include <linux/types.h>

/* Timestamp Control Register */
#define RVTRACE_TIMESTAMP_CTRL_OFFSET			0x040
#define RVTRACE_TIMESTAMP_ACTIVE			BIT(0)
#define RVTRACE_TIMESTAMP_COUNT				BIT(1)
#define RVTRACE_TIMESTAMP_RESET				BIT(2)
#define RVTRACE_TIMESTAMP_RUN_IN_DEBUG			BIT(3)
#define RVTRACE_TIMESTAMP_MODE				GENMASK(6, 4)
#define RVTRACE_TIMESTAMP_PRESCALE			GENMASK(9, 8)
#define RVTRACE_TIMESTAMP_ENABLE			BIT(15)
#define RVTRACE_TIMESTAMP_WIDTH				GENMASK(29, 24)

#define RVTRACE_TIMESTAMP_ACTIVE_SHIFT			0
#define RVTRACE_TIMESTAMP_ENABLE_SHIFT			15

/* Timestamp Counter Lower Bits */
#define RVTRACE_TIMESTAMP_COUNTER_LOW			0x048
/* Timestamp Counter Upper Bits */
#define RVTRACE_TIMESTAMP_COUNTER_HIGH			0x04C

enum timestamp_mode {
	TIMESTAMP_MODE_NONE = 0,
	TIMESTAMP_MODE_EXTERNAL = 1,
	TIMESTAMP_MODE_INTERNAL_SYSTEM = 2,
	TIMESTAMP_MODE_INTERNAL_CORE = 3,
	TIMESTAMP_MODE_SHARED = 4
};

/**
 * struct timestamp_config - timestamp configuration for encoder/funnel
 * @run_in_debug:       Continue timestamp counting in debug mode
 * @mode:		Timestamp generation mode (periodic, event-triggered)
 * @prescale:		Clock prescale factor (1, 4, 16, 64)
 * @width:		Timestamp counter width in bits (0-63)
 */
struct timestamp_config {
	bool				run_in_debug;
	u8				mode;
	u8				prescale;
	u8				width;
};

struct rvtrace_component;

/* Timestamp control functions */
int timestamp_enable(struct rvtrace_component *comp);
void timestamp_disable(struct rvtrace_component *comp);
struct timestamp_config *timestamp_get_config(struct rvtrace_component *comp);
void timestamp_set_config(struct rvtrace_component *comp, struct device *dev,
			  struct timestamp_config *config);
int rvtrace_init_timestamp(struct rvtrace_component *comp,
			   struct timestamp_config *config);

#endif
