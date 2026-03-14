// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2026 Spacemit Limited. All rights reserved.
 * Author: liangzhen <zhen.liang@spacemit.com>
 */

#include <linux/kernel.h>
#include <linux/coresight.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/bitfield.h>
#include <linux/rvtrace.h>

#include "coresight-priv.h"
#include "coresight-trace-id.h"

#define RVTRACE_ATBBRIDGE_CONTROL_ID_MASK	GENMASK(14, 8)
/**
 * struct atbbridge_data - specifics associated to a ATB bridge component
 * @csdev:      Component vitals needed by the framework.
 * @spinlock:   Only one at a time pls.
 * @traceid:    Value of the current ID for this component.
 */
struct atbbridge_data {
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	u8			traceid;
};

DEFINE_CORESIGHT_DEVLIST(atbbridge_devs, "atbbridge");

static int atbbridge_enable(struct coresight_device *csdev,
			    struct coresight_connection *in,
			    struct coresight_connection *out)
{
	u32 control;
	int ret = 0;
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);
	struct atbbridge_data *atbbridge_data = rvtrace_component_data(comp);
	unsigned long flags;

	spin_lock_irqsave(&atbbridge_data->spinlock, flags);
	if (!comp->was_reset) {
		ret = rvtrace_component_reset(comp);
		if (ret)
			goto done;
	}

	control = readl_relaxed(comp->base + RVTRACE_COMPONENT_CTRL_OFFSET);
	control |= FIELD_PREP(RVTRACE_ATBBRIDGE_CONTROL_ID_MASK, atbbridge_data->traceid);
	writel_relaxed(control, comp->base + RVTRACE_COMPONENT_CTRL_OFFSET);

	ret = rvtrace_enable_component(comp);
	if (ret)
		goto done;

	spin_unlock_irqrestore(&atbbridge_data->spinlock, flags);

	dev_dbg(&csdev->dev, "Trace ATB bridge enabled\n");
done:
	return ret;
}

static void atbbridge_disable(struct coresight_device *csdev,
			      struct coresight_connection *in,
			      struct coresight_connection *out)
{
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);
	struct atbbridge_data *atbbridge_data = rvtrace_component_data(comp);
	unsigned long flags;

	spin_lock_irqsave(&atbbridge_data->spinlock, flags);

	if (rvtrace_disable_component(comp))
		dev_err(&csdev->dev,
			"timeout while waiting for Trace ATB Bridge to be disabled\n");
	/*
	 * if (rvtrace_comp_is_empty(comp))
	 * 	dev_err(&csdev->dev,
	 * 		"timeout while waiting for Trace ATB Bridge internal buffers become empty\n");
	 */

	spin_unlock_irqrestore(&atbbridge_data->spinlock, flags);

	dev_dbg(&csdev->dev, "Trace ATB bridge disabled\n");
}

static int atbbridge_link_trace_id(struct coresight_device *csdev, __maybe_unused enum cs_mode mode,
				   __maybe_unused struct coresight_device *sink)
{
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);
	struct atbbridge_data *atbbridge_data = rvtrace_component_data(comp);

	return atbbridge_data->traceid;
}

static const struct coresight_ops_link atbbridge_link_ops = {
	.enable		= atbbridge_enable,
	.disable	= atbbridge_disable,
};

static const struct coresight_ops atbbridge_cs_ops = {
	.trace_id	= atbbridge_link_trace_id,
	.link_ops       = &atbbridge_link_ops,
};

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t size)
{
	unsigned long val;
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct atbbridge_data *atbbridge_data = rvtrace_component_data(comp);
	unsigned long flags;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	if (val) {
		if (rvtrace_component_reset(comp)) {
			comp->was_reset = false;
			spin_unlock_irqrestore(&atbbridge_data->spinlock, flags);
			return -EINVAL;
		}
	}

	spin_unlock_irqrestore(&atbbridge_data->spinlock, flags);

	return size;
}
static DEVICE_ATTR_WO(reset);

static ssize_t cpu_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);

	val = comp->cpu;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(cpu);

static ssize_t traceid_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct atbbridge_data *atbbridge_data = rvtrace_component_data(comp);

	val = atbbridge_data->traceid;
	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}
static DEVICE_ATTR_RO(traceid);

static struct attribute *trace_atbbridge_attrs[] = {
	coresight_simple_reg32(control, RVTRACE_COMPONENT_CTRL_OFFSET),
	coresight_simple_reg32(impl, RVTRACE_COMPONENT_IMPL_OFFSET),
        &dev_attr_reset.attr,
        &dev_attr_cpu.attr,
	&dev_attr_traceid.attr,
        NULL,
};
ATTRIBUTE_GROUPS(trace_atbbridge);

static int atbbridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct coresight_platform_data *pdata;
	struct atbbridge_data *atbbridge_data;
	struct rvtrace_component *comp;
	struct coresight_desc desc = { 0 };
	int trace_id = 0;

	comp = rvtrace_register_component(pdev);
	if (IS_ERR(comp))
		return PTR_ERR(comp);

	atbbridge_data = devm_kzalloc(dev, sizeof(*atbbridge_data), GFP_KERNEL);
	if (!atbbridge_data)
		return -ENOMEM;

	spin_lock_init(&atbbridge_data->spinlock);

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	pdev->dev.platform_data = pdata;

	platform_set_drvdata(pdev, comp);

	desc.name = coresight_alloc_device_name(&atbbridge_devs, dev);
	desc.access = CSDEV_ACCESS_IOMEM(comp->base);
        desc.type = CORESIGHT_DEV_TYPE_LINK;
        desc.subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_FIFO;
	desc.ops = &atbbridge_cs_ops;
        desc.pdata = pdata;
        desc.dev = dev;
        desc.groups = trace_atbbridge_groups;
        atbbridge_data->csdev = coresight_register(&desc);
        if (IS_ERR(atbbridge_data->csdev))
                return PTR_ERR(atbbridge_data->csdev);

	trace_id = coresight_trace_id_get_system_id();
	if (trace_id < 0)
		return -EINVAL;
	atbbridge_data->traceid = (u8)trace_id;

	comp->id.data = atbbridge_data;

	pm_runtime_enable(dev);
	dev_dbg(dev, "Trace ATB Bridge initialized\n");

	return 0;
}

static void atbbridge_remove(struct platform_device *pdev)
{
	struct rvtrace_component *comp = platform_get_drvdata(pdev);
	struct atbbridge_data *atbbridge_data = rvtrace_component_data(comp);

	pm_runtime_disable(&pdev->dev);
	coresight_trace_id_put_system_id(atbbridge_data->traceid);
	coresight_unregister(atbbridge_data->csdev);
}

static const struct of_device_id atbbridge_match[] = {
	{.compatible = "riscv,trace-atbbridge"},
	{},
};

static struct platform_driver atbbridge_driver = {
	.probe = atbbridge_probe,
	.remove = atbbridge_remove,
	.driver = {
		.name	= "trace-atbbridge",
		.of_match_table = atbbridge_match,
	},
};

module_platform_driver(atbbridge_driver);

MODULE_DESCRIPTION("RISC-V Trace ATB Bridge driver");
MODULE_LICENSE("GPL");
