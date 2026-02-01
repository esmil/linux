/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __LINUX_RVTRACE_H__
#define __LINUX_RVTRACE_H__

#include <linux/device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/coresight.h>
#include <linux/types.h>

/* Control register common across all RISC-V trace components */
#define RVTRACE_COMPONENT_CTRL_OFFSET		0x000
#define RVTRACE_COMPONENT_CTRL_ACTIVE_MASK	0x1
#define RVTRACE_COMPONENT_CTRL_ACTIVE_SHIFT	0
#define RVTRACE_COMPONENT_CTRL_ENABLE_MASK	0x1
#define RVTRACE_COMPONENT_CTRL_ENABLE_SHIFT	1
#define RVTRACE_COMPONENT_CTRL_EMPTY_SHIFT	3

/* Implementation register common across all RISC-V trace components */
#define RVTRACE_COMPONENT_IMPL_OFFSET		0x004
#define RVTRACE_COMPONENT_IMPL_VERMAJOR_MASK	0xf
#define RVTRACE_COMPONENT_IMPL_VERMAJOR_SHIFT	0
#define RVTRACE_COMPONENT_IMPL_VERMINOR_MASK	0xf
#define RVTRACE_COMPONENT_IMPL_VERMINOR_SHIFT	4
#define RVTRACE_COMPONENT_IMPL_TYPE_MASK	0xf
#define RVTRACE_COMPONENT_IMPL_TYPE_SHIFT	8

#define RVTRACE_TIMEOUT_US			100

/* Possible component types defined by the RISC-V Trace Control Interface */
enum rvtrace_component_type {
	RVTRACE_COMPONENT_TYPE_RESV0,
	RVTRACE_COMPONENT_TYPE_ENCODER, /* 0x1 */
	RVTRACE_COMPONENT_TYPE_RESV2,
	RVTRACE_COMPONENT_TYPE_RESV3,
	RVTRACE_COMPONENT_TYPE_RESV4,
	RVTRACE_COMPONENT_TYPE_RESV5,
	RVTRACE_COMPONENT_TYPE_RESV6,
	RVTRACE_COMPONENT_TYPE_RESV7,
	RVTRACE_COMPONENT_TYPE_FUNNEL, /* 0x8 */
	RVTRACE_COMPONENT_TYPE_RAMSINK, /* 0x9 */
	RVTRACE_COMPONENT_TYPE_PIBSINK, /* 0xA */
	RVTRACE_COMPONENT_TYPE_RESV11,
	RVTRACE_COMPONENT_TYPE_RESV12,
	RVTRACE_COMPONENT_TYPE_RESV13,
	RVTRACE_COMPONENT_TYPE_ATBBRIDGE, /* 0xE */
	RVTRACE_COMPONENT_TYPE_RESV15,
	RVTRACE_COMPONENT_TYPE_MAX
};

/* Encoding/decoding macros for RISC-V trace component version */
#define rvtrace_component_version_major(__version)	\
	(((__version) >> 16) & 0xffff)
#define rvtrace_component_version_minor(__version)	\
	((__version) & 0xffff)
#define rvtrace_component_mkversion(__major, __minor)	\
	((((__major) & 0xffff) << 16) |	((__minor) & 0xffff))

/**
 * struct rvtrace_component_id - Details to identify or match a RISC-V trace component
 * @type:      Type of the component
 * @version:   Version of the component
 * @data:      Data pointer for driver use
 */
struct rvtrace_component_id {
	enum rvtrace_component_type type;
	u32 version;
	void *data;
};

/**
 * struct rvtrace_component - Representation of a RISC-V trace component
 * base:             Memory mapped base address for the component
 * id:               Details to match the component
 * dev:              Device instance
 * cpu:              The cpu this component is affined to
 * was_reset:        Flag showing whether RISC-V trace driver was reset successfully
 */
struct rvtrace_component {
	void __iomem *base;
	struct rvtrace_component_id id;
	struct device *dev;
	int cpu;
	bool was_reset;
};

struct component_enable_arg {
	struct rvtrace_component *comp;
	int rc;
};

struct rvtrace_component *rvtrace_register_component(struct platform_device *pdev);

int rvtrace_poll_bit(struct rvtrace_component *comp, int offset,
		     int bit, int bitval);

int rvtrace_enable_component(struct rvtrace_component *comp);
int rvtrace_disable_component(struct rvtrace_component *comp);
int rvtrace_component_reset(struct rvtrace_component *comp);

static inline void *rvtrace_component_data(struct rvtrace_component * comp) {
	return comp->id.data;
}

static inline int rvtrace_comp_is_empty(struct rvtrace_component *comp)
{
	return rvtrace_poll_bit(comp, RVTRACE_COMPONENT_CTRL_OFFSET,
				RVTRACE_COMPONENT_CTRL_EMPTY_SHIFT, 1);
}

#endif
