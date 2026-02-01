#include <linux/kernel.h>
#include <linux/coresight.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/rvtrace.h>

#include "coresight-priv.h"

/* Disable Individual Funnel Inputs */
#define RVTRACE_FUNNEL_DISINPUT_OFFSET			0x008
#define RVTRACE_FUNNEL_DISINPUT_MASK			0xffff

/**
 * struct funnel_data - specifics associated to a Trace Funnel component
 * @csdev:        Component vitals needed by the framework.
 * @spinlock:     Only one at a time pls.
 * @was_enabled:  Flag showing whether the Trace Funnel was enabled.
 * @input_refcnt: Record the number of funnel inputs
 */
struct funnel_data {
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	bool			was_enabled;
	u32			input_refcnt;
	u32			disintput;
};

DEFINE_CORESIGHT_DEVLIST(funnel_devs, "rvtrace_funnel");

static int funnel_enable_hw(struct rvtrace_component *comp, int port)
{
	u32 disinput;
	int ret = 0;
	struct funnel_data *funnel_data = rvtrace_component_data(comp);

	if (!comp->was_reset) {
		ret = rvtrace_component_reset(comp);
		if (ret)
			goto done;
	}

	disinput = readl_relaxed(comp->base + RVTRACE_FUNNEL_DISINPUT_OFFSET);
	disinput &= ~(1 << port);
	writel_relaxed(disinput, comp->base + RVTRACE_FUNNEL_DISINPUT_OFFSET);
	ret = rvtrace_poll_bit(comp, RVTRACE_FUNNEL_DISINPUT_OFFSET, port, 0);
	if (ret)
		goto done;

	if (!funnel_data->was_enabled) {
		ret = rvtrace_enable_component(comp);
		if (ret)
			goto done;
	}

done:
	return ret;
}

static int funnel_enable(struct coresight_device *csdev,
			 struct coresight_connection *in,
			 struct coresight_connection *out)
{
	int ret = 0;
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);
	struct funnel_data *funnel_data = rvtrace_component_data(comp);
	unsigned long flags;
	bool first_enable = false;

	spin_lock_irqsave(&funnel_data->spinlock, flags);
	if (in->dest_refcnt == 0) {
		ret = funnel_enable_hw(comp, in->dest_port);
		if (!ret)
			first_enable = true;
	}
	if (!ret) {
		in->dest_refcnt++;
		funnel_data->input_refcnt++;
	}

	if (first_enable)
		dev_dbg(&csdev->dev, "Trace funnel inport %d enabled\n",
			in->dest_port);

	spin_unlock_irqrestore(&funnel_data->spinlock, flags);

	return ret;
}

static void funnel_disable_hw(struct rvtrace_component *comp, int inport)
{
	struct funnel_data *funnel_data = rvtrace_component_data(comp);
	if (--funnel_data->input_refcnt != 0)
		return;

	writel_relaxed(RVTRACE_FUNNEL_DISINPUT_MASK, comp->base + RVTRACE_FUNNEL_DISINPUT_OFFSET);

	if (rvtrace_disable_component(comp))
		dev_err(&funnel_data->csdev->dev,
			"timeout while waiting for Trace Funnel to be disabled\n");

	if (rvtrace_comp_is_empty(comp))
		dev_err(&funnel_data->csdev->dev,
			"timeout while waiting for Trace Funnel internal buffers become empty\n");
}

static void funnel_disable(struct coresight_device *csdev,
			   struct coresight_connection *in,
			   struct coresight_connection *out)
{
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);
	struct funnel_data *funnel_data = rvtrace_component_data(comp);
	unsigned long flags;
	bool last_disable = false;

	spin_lock_irqsave(&funnel_data->spinlock, flags);
	if (--in->dest_refcnt == 0) {
		funnel_disable_hw(comp, in->dest_port);
		last_disable = true;
	}
	spin_unlock_irqrestore(&funnel_data->spinlock, flags);

	if (last_disable)
		dev_dbg(&csdev->dev, "Trace funnel inport %d disabled\n",
			in->dest_port);

	if (funnel_data->input_refcnt == 0)
		dev_dbg(&csdev->dev, "Trace funnel disabled\n");
}

static const struct coresight_ops_link funnel_link_ops = {
	.enable		= funnel_enable,
	.disable	= funnel_disable,
};

static const struct coresight_ops funnel_cs_ops = {
        .link_ops       = &funnel_link_ops,
};

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t size)
{
	unsigned long val;
	struct rvtrace_component *comp = dev_get_drvdata(dev->parent);
	struct funnel_data *funnel_data = rvtrace_component_data(comp);
	unsigned long flags;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	spin_lock_irqsave(&funnel_data->spinlock, flags);

	if (val) {
		if (rvtrace_component_reset(comp)) {
			comp->was_reset = false;
			spin_unlock_irqrestore(&funnel_data->spinlock, flags);
			return -EINVAL;
		}
	}

	spin_unlock_irqrestore(&funnel_data->spinlock, flags);

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

static struct attribute *trace_funnel_attrs[] = {
	coresight_simple_reg32(control, RVTRACE_COMPONENT_CTRL_OFFSET),
	coresight_simple_reg32(impl, RVTRACE_COMPONENT_IMPL_OFFSET),
	coresight_simple_reg32(disinput, RVTRACE_FUNNEL_DISINPUT_OFFSET),
	&dev_attr_reset.attr,
	&dev_attr_cpu.attr,
        NULL,
};
ATTRIBUTE_GROUPS(trace_funnel);

static int funnel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct coresight_platform_data *pdata;
	struct funnel_data *funnel_data;
	struct rvtrace_component *comp;
	struct coresight_desc desc = { 0 };

	comp = rvtrace_register_component(pdev);
	if (IS_ERR(comp))
		return PTR_ERR(comp);

	funnel_data = devm_kzalloc(dev, sizeof(*funnel_data), GFP_KERNEL);
	if (!funnel_data)
		return -ENOMEM;

	spin_lock_init(&funnel_data->spinlock);

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	pdev->dev.platform_data = pdata;

	platform_set_drvdata(pdev, comp);

	desc.name = coresight_alloc_device_name(&funnel_devs, dev);
	desc.access = CSDEV_ACCESS_IOMEM(comp->base);
	desc.type = CORESIGHT_DEV_TYPE_LINK;
	desc.subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_MERG;
	desc.ops = &funnel_cs_ops;
	desc.pdata = pdata;
	desc.dev = dev;
	desc.groups = trace_funnel_groups;
	funnel_data->csdev = coresight_register(&desc);
	if (IS_ERR(funnel_data->csdev))
		return PTR_ERR(funnel_data->csdev);

	comp->id.data = funnel_data;

	pm_runtime_enable(dev);
	dev_dbg(dev, "Trace Funnel initialized\n");

	return 0;
}

static void funnel_remove(struct platform_device *pdev)
{
	struct rvtrace_component *comp = platform_get_drvdata(pdev);
	struct funnel_data *funnel_data = rvtrace_component_data(comp);

	pm_runtime_disable(&pdev->dev);
	coresight_unregister(funnel_data->csdev);
}

static const struct of_device_id funnel_match[] = {
	{.compatible = "riscv,trace-funnel"},
	{},
};

static struct platform_driver funnel_driver = {
	.probe = funnel_probe,
	.remove = funnel_remove,
	.driver = {
		.name	= "trace-funnel",
		.of_match_table = funnel_match,
	},
};

module_platform_driver(funnel_driver);

MODULE_DESCRIPTION("RISC-V Trace funnel driver");
MODULE_LICENSE("GPL");
