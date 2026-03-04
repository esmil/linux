/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2026 Spacemit Limited. All rights reserved.
 * Author: liangzhen <zhen.liang@spacemit.com>
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/bitfield.h>
#include <linux/sysfs.h>
#include <linux/rvtrace.h>

#include "rvtrace-timestamp.h"
#include "rvtrace-encoder.h"
#include "rvtrace-funnel.h"
#include "coresight-priv.h"

int timestamp_enable(struct rvtrace_component *comp)
{
	u32 val;

	val = readl_relaxed(comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);
	val |= (RVTRACE_TIMESTAMP_ENABLE | RVTRACE_TIMESTAMP_COUNT);

	writel_relaxed(val, comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);

	return rvtrace_poll_bit(comp, RVTRACE_TIMESTAMP_CTRL_OFFSET,
				RVTRACE_TIMESTAMP_ENABLE_SHIFT, 1);
}
EXPORT_SYMBOL_GPL(timestamp_enable);

void timestamp_disable(struct rvtrace_component *comp)
{
	u32 val;

	val = readl_relaxed(comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);
	val &= ~(RVTRACE_TIMESTAMP_ENABLE | RVTRACE_TIMESTAMP_COUNT);
	writel_relaxed(val, comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);
}
EXPORT_SYMBOL_GPL(timestamp_disable);

struct timestamp_config *timestamp_get_config(struct rvtrace_component *comp)
{
	if (comp->id.type == RVTRACE_COMPONENT_TYPE_ENCODER) {
		struct encoder_data *encoder_data = rvtrace_component_data(comp);
		return &encoder_data->ts_config;
	} else if (comp->id.type == RVTRACE_COMPONENT_TYPE_FUNNEL) {
		struct funnel_data *funnel_data = rvtrace_component_data(comp);
		return &funnel_data->ts_config;
	} else {
		return NULL;
	}
}
EXPORT_SYMBOL_GPL(timestamp_get_config);

void timestamp_set_config(struct rvtrace_component *comp, struct device *dev,
			  struct timestamp_config *config)
{
	u32 val;

	val = readl_relaxed(comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);

	val |= (FIELD_PREP(RVTRACE_TIMESTAMP_RUN_IN_DEBUG, config->run_in_debug) |
		FIELD_PREP(RVTRACE_TIMESTAMP_MODE, config->mode) |
		FIELD_PREP(RVTRACE_TIMESTAMP_PRESCALE, config->prescale));

	writel_relaxed(val, comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);

	/* Verify configuration was applied */
	val = readl_relaxed(comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);

	if (BMVAL(val, 3, 3) != config->run_in_debug) {
		dev_warn(dev, "timestamp run_in_debug %#x not supported\n",
			 config->run_in_debug);
		config->run_in_debug = BMVAL(val, 3, 3);
	}

	if (BMVAL(val, 4, 6) != config->mode) {
		dev_warn(dev, "timestamp mode %#x not supported\n",
			 config->mode);
		config->mode = BMVAL(val, 4, 6);
	}

	if (BMVAL(val, 8, 9) != config->prescale) {
		dev_warn(dev, "timestamp prescale %#x not supported\n",
			 config->prescale);
		config->prescale = BMVAL(val, 8, 9);
	}
}
EXPORT_SYMBOL_GPL(timestamp_set_config);

static int rvtrace_timestamp_reset(struct rvtrace_component *comp)
{
	int ret;

	writel_relaxed(0, comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);
	ret = rvtrace_poll_bit(comp, RVTRACE_TIMESTAMP_CTRL_OFFSET,
			       RVTRACE_TIMESTAMP_ACTIVE_SHIFT, 0);

	if (ret)
		return ret;

	writel_relaxed(RVTRACE_TIMESTAMP_ACTIVE,
			comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);
	return rvtrace_poll_bit(comp, RVTRACE_TIMESTAMP_CTRL_OFFSET,
				RVTRACE_TIMESTAMP_ACTIVE_SHIFT, 1);
}

int rvtrace_init_timestamp(struct rvtrace_component *comp,
			   struct timestamp_config *config)
{
	u32 val;
	int ret;

	val = readl_relaxed(comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);
	if (!FIELD_GET(RVTRACE_TIMESTAMP_ACTIVE, val)) {
		ret = rvtrace_timestamp_reset(comp);
		if (ret)
			return ret;
	}

	val = readl_relaxed(comp->base + RVTRACE_TIMESTAMP_CTRL_OFFSET);

	config->run_in_debug = !!(val & RVTRACE_TIMESTAMP_RUN_IN_DEBUG);
	config->mode = BMVAL(val, 4, 6);
	config->prescale = BMVAL(val, 8, 9);
	config->width = BMVAL(val, 24, 29);

	return 0;
}
EXPORT_SYMBOL_GPL(rvtrace_init_timestamp);

static ssize_t run_in_debug_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct timestamp_config *config = timestamp_get_config(comp);

	if (!config)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%u\n", config->run_in_debug);
}

static ssize_t run_in_debug_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct timestamp_config *config = timestamp_get_config(comp);
	unsigned long val;

	if (!config)
		return -EINVAL;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	config->run_in_debug = !!val;

	return size;
}
static DEVICE_ATTR_RW(run_in_debug);

static const char * const modes_str[] = {
	[TIMESTAMP_MODE_NONE] = "none",
	[TIMESTAMP_MODE_EXTERNAL] = "external",
	[TIMESTAMP_MODE_INTERNAL_SYSTEM] = "internal_system",
	[TIMESTAMP_MODE_INTERNAL_CORE] = "internal_core",
	[TIMESTAMP_MODE_SHARED] = "shared",
};

static ssize_t modes_available_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s %s %s %s %s\n",
			    modes_str[TIMESTAMP_MODE_NONE],
			    modes_str[TIMESTAMP_MODE_EXTERNAL],
			    modes_str[TIMESTAMP_MODE_INTERNAL_SYSTEM],
			    modes_str[TIMESTAMP_MODE_INTERNAL_CORE],
			    modes_str[TIMESTAMP_MODE_SHARED]);
}
static DEVICE_ATTR_RO(modes_available);

static ssize_t mode_preferred_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct timestamp_config *config = timestamp_get_config(comp);

	if (!config)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%s\n", modes_str[config->mode]);
}

static ssize_t mode_preferred_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct timestamp_config *config = timestamp_get_config(comp);

	if (!config)
		return -EINVAL;

	if (sysfs_streq(buf, modes_str[TIMESTAMP_MODE_NONE]))
		config->mode = TIMESTAMP_MODE_NONE;
	else if (sysfs_streq(buf, modes_str[TIMESTAMP_MODE_EXTERNAL]))
		config->mode = TIMESTAMP_MODE_EXTERNAL;
	else if (sysfs_streq(buf, modes_str[TIMESTAMP_MODE_INTERNAL_SYSTEM]))
		config->mode = TIMESTAMP_MODE_INTERNAL_SYSTEM;
	else if (sysfs_streq(buf, modes_str[TIMESTAMP_MODE_INTERNAL_CORE]))
		config->mode = TIMESTAMP_MODE_INTERNAL_CORE;
	else if (sysfs_streq(buf, modes_str[TIMESTAMP_MODE_SHARED]))
		config->mode = TIMESTAMP_MODE_SHARED;
	else
		return -EINVAL;
	return size;
}
static DEVICE_ATTR_RW(mode_preferred);

static ssize_t prescale_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct timestamp_config *config = timestamp_get_config(comp);

	if (!config)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%u\n", config->prescale);
}

static ssize_t prescale_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t size)
{
	unsigned long val;
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct timestamp_config *config = timestamp_get_config(comp);

	if (!config)
		return -EINVAL;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	config->prescale = val;

	return size;
}
static DEVICE_ATTR_RW(prescale);

static ssize_t width_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct timestamp_config *config = timestamp_get_config(comp);

	if (!config)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%u\n", config->width);
}
static DEVICE_ATTR_RO(width);

const struct attribute *timestamp_attrs[] = {
	&dev_attr_run_in_debug.attr,
	&dev_attr_modes_available.attr,
	&dev_attr_mode_preferred.attr,
	&dev_attr_prescale.attr,
	&dev_attr_width.attr,
	NULL,
};
EXPORT_SYMBOL_GPL(timestamp_attrs);
