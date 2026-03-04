/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2026 Spacemit Limited. All rights reserved.
 * Author: liangzhen <zhen.liang@spacemit.com>
 */

#ifndef _RVTRACE_FUNNEL_H
#define _RVTRACE_FUNNEL_H

#include <linux/spinlock.h>
#include <linux/coresight.h>
#include "rvtrace-timestamp.h"

/* Disable Individual Funnel Inputs */
#define RVTRACE_FUNNEL_DISINPUT_OFFSET			0x008
#define RVTRACE_FUNNEL_DISINPUT_MASK			0xffff

/**
 * struct funnel_data - specifics associated to a Trace Funnel component
 * @csdev:        Component vitals needed by the framework.
 * @spinlock:     Only one at a time pls.
 * @was_enabled:  Flag showing whether the Trace Funnel was enabled.
 * @input_refcnt: Record the number of funnel inputs
 * @has_timestamp: True if this funnel has timestamp component.
 * @ts_ctrl:      Controls the insertion of global timestamps in the trace streams.
 * @ts_config:    Timestamp configuration.
 */
struct funnel_data {
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	bool			was_enabled;
	u32			input_refcnt;
	u32			disintput;
	bool			has_timestamp;
	bool			ts_ctrl;
	struct timestamp_config	ts_config;
};

#endif
