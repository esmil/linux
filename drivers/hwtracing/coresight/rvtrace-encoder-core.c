#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/bitfield.h>
#include <linux/smp.h>
#include <linux/coresight.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/rvtrace.h>

#include "rvtrace-encoder.h"
#include "rvtrace-timestamp.h"
#include "coresight-etm-perf.h"

static int boot_enable;
module_param_named(boot_enable, boot_enable, int, S_IRUGO);

static struct rvtrace_component *rvtrace_cpu_encoder[NR_CPUS];

static int encoder_cpu_id(struct coresight_device *csdev)
{
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);

	return comp->cpu;
}

static void encoder_set_config(struct rvtrace_component *comp)
{
	u32 val;
	struct encoder_data *encoder_data = rvtrace_component_data(comp);
	struct device *dev = &encoder_data->csdev->dev;
	struct encoder_config *config = &encoder_data->config;

	/* Configure Trace Encoder features register */
	val = (FIELD_PREP(RVTRACE_ENCODER_INST_NO_ADDR_DIFF, config->inst_na_diff) |
		   FIELD_PREP(RVTRACE_ENCODER_INST_NO_TRAP_ADDR, config->inst_nt_addr) |
		   FIELD_PREP(RVTRACE_ENCODER_INST_EN_SEQUENTIAL_JUMP, config->seq_jump) |
		   FIELD_PREP(RVTRACE_ENCODER_INST_EN_IMPLICIT_RETURN, config->impl_ret) |
		   FIELD_PREP(RVTRACE_ENCODER_INST_EN_BRANCH_PREDICTION, config->branch_pred) |
		   FIELD_PREP(RVTRACE_ENCODER_INST_IMPLICIT_RETURN_MODE, config->impl_ret_mode) |
		   FIELD_PREP(RVTRACE_ENCODER_INST_EN_REPEQTED_HISTORT, config->rep_hist) |
		   FIELD_PREP(RVTRACE_ENCODER_INST_EN_ALL_JUMPS, config->all_jumps) |
		   FIELD_PREP(RVTRACE_ENCODER_INST_EXTEND_ADDR_MSB, config->ext_msb) |
		   FIELD_PREP(RVTRACE_ENCODER_SRCID, config->srcid) |
		   FIELD_PREP(RVTRACE_ENCODER_SRCBITS, config->srcb));

	writel_relaxed(val, comp->base + RVTRACE_ENCODER_INST_FTRS_OFFSET);

	/* Check Trace Encoder features register WARL bits could be set */
	val = readl_relaxed(comp->base + RVTRACE_ENCODER_INST_FTRS_OFFSET);
	if (BMVAL(val, 0, 0) != config->inst_na_diff) {
		dev_warn(dev, "trTeInstNoAddrDiff %#x is not supported\n",
			 config->inst_na_diff);
		config->inst_na_diff = BMVAL(val, 0, 0);
	}

	if (BMVAL(val, 1, 1) != config->inst_nt_addr) {
		dev_warn(dev, "trTeInstNoTrapAddr %#x is not supported\n",
			 config->inst_nt_addr);
		config->inst_nt_addr = BMVAL(val, 1, 1);
	}

	if (BMVAL(val, 2, 2) != config->seq_jump) {
		dev_warn(dev, "trTeInstEnSequentialJump %#x is not supported\n",
			 config->seq_jump);
		config->seq_jump = BMVAL(val, 2, 2);
	}

	if (BMVAL(val, 3, 3) != config->impl_ret) {
		dev_warn(dev, "trTeInstEnImplicitReturn %#x is not supported\n",
			 config->impl_ret);
		config->impl_ret = BMVAL(val, 3, 3);
	}

	if (BMVAL(val, 4, 4) != config->branch_pred) {
		dev_warn(dev, "trTeInstEnBranchPrediction %#x is not supported\n",
			 config->branch_pred);
		config->branch_pred = BMVAL(val, 4, 4);
	}

	if (BMVAL(val, 5, 5) != config->jump_target_cache) {
		dev_warn(dev, "trTeInstEnJumpTargetCache %#x is not supported\n",
			 config->jump_target_cache);
		config->jump_target_cache = BMVAL(val, 5, 5);
	}

	if (BMVAL(val, 6, 7) != config->impl_ret_mode) {
		dev_warn(dev, "trTeInstImplicitReturnMode %#x is not supported\n",
			 config->impl_ret_mode);
		config->impl_ret_mode = BMVAL(val, 6, 7);
	}

	if (BMVAL(val, 8, 8) != config->rep_hist) {
		dev_warn(dev, "trTeInstEnRepeatedHistory %#x is not supported\n",
			 config->rep_hist);
		config->rep_hist = BMVAL(val, 8, 8);
	}

	if (BMVAL(val, 9, 9) != config->all_jumps) {
		dev_warn(dev, "trTeInstEnAllJumps %#x is not supported\n",
			 config->all_jumps);
		config->all_jumps = BMVAL(val, 9, 9);
	}

	if (BMVAL(val, 10, 10) != config->ext_msb) {
		dev_warn(dev, " trTeInstExtendAddrMSB %#x is not supported\n",
			 config->ext_msb);
		config->ext_msb = BMVAL(val, 10, 10);
	}

	if (BMVAL(val, 16, 27) != config->srcid) {
		dev_warn(dev, "trTeSrcID %#x is not supported\n",
			 config->srcid);
		config->srcid = BMVAL(val, 16, 27);
	}

	if (BMVAL(val, 28, 31) != config->srcb) {
		dev_warn(dev, "trTeSrcBits %#x is not supported\n",
			 config->srcb);
		config->srcb = BMVAL(val, 28, 31);
	}

	/* Configure Trace Encoder control register */
	val = readl_relaxed(comp->base + RVTRACE_COMPONENT_CTRL_OFFSET);
	val |= (FIELD_PREP(RVTRACE_ENCODER_INSTMODE, ENCODER_INSTMODE_HTM) |
		   FIELD_PREP(RVTRACE_ENCODER_CONTEXT, config->context) |
		   FIELD_PREP(RVTRACE_ENCODER_INSTTRIGEN, config->inst_trigger) |
		   FIELD_PREP(RVTRACE_ENCODER_INST_STALL_EN, config->inst_stall) |
		   FIELD_PREP(RVTRACE_ENCODER_INHBSRC, config->inhb_src) |
		   FIELD_PREP(RVTRACE_ENCODER_INSTSYNC_MODE, config->inst_syncmode) |
		   FIELD_PREP(RVTRACE_ENCODER_INSTSYNC_MAX, config->inst_syncmax));


	writel_relaxed(val, comp->base + RVTRACE_COMPONENT_CTRL_OFFSET);

	/* Check Trace Encoder control register WARL bits could be set */
	val = readl_relaxed(comp->base + RVTRACE_COMPONENT_CTRL_OFFSET);

	if (BMVAL(val, 9, 9) != config->context) {
		dev_warn(dev, "trTeContext %#x is not supported\n",
			 config->context);
		config->context = BMVAL(val, 9, 9);
	}

	if (BMVAL(val, 11, 11) != config->inst_trigger) {
		dev_warn(dev, "trTeInstTrigEnable %#x is not supported\n",
			 config->inst_trigger);
		config->inst_trigger = BMVAL(val, 11, 11);
	}

	if (BMVAL(val, 13, 13) != config->inst_stall) {
		dev_warn(dev, "trTeInstStallEna %#x is not supported\n",
			 config->inst_stall);
		config->inst_stall = BMVAL(val, 13, 13);
	}

	if (BMVAL(val, 15, 15) != config->inhb_src) {
		dev_warn(dev, "trTeInhibitSrc %#x is not supported\n",
			 config->inhb_src);
		config->inhb_src = BMVAL(val, 15, 15);
	}

	if (BMVAL(val, 20, 23) != config->inst_syncmax) {
		dev_warn(dev, "trTeInstSyncMax %#x is not supported\n",
			 config->inst_syncmax);
		config->inst_syncmax = BMVAL(val, 20, 23);
	}

	/* Configure timestamp only if encoder has timestamp component */
	if (encoder_data->has_timestamp && encoder_data->ts_ctrl)
		timestamp_set_config(comp, dev, &encoder_data->ts_config);
}

static int encoder_enable_hw(struct rvtrace_component *comp)
{
	u32 val;
	int ret;
	struct encoder_data *encoder_data = rvtrace_component_data(comp);

	if (!comp->was_reset) {
		ret = rvtrace_component_reset(comp);
		if (ret)
			goto done;
	}

	encoder_set_config(comp);

	ret = rvtrace_enable_component(comp);
	if (ret)
		goto done;

	/* Enable timestamp only if encoder has timestamp component */
	if (encoder_data->has_timestamp && encoder_data->ts_ctrl) {
		ret = timestamp_enable(comp);
		if (ret) {
			dev_warn(&encoder_data->csdev->dev,
				 "Failed to enable timestamp\n");
			ret = 0;  /* Don't fail encoder enable if timestamp fails */
		}
	}

	val = readl_relaxed(comp->base + RVTRACE_COMPONENT_CTRL_OFFSET);
	val |= RVTRACE_ENCODER_ITRACE;
	writel_relaxed(val, comp->base + RVTRACE_COMPONENT_CTRL_OFFSET);
	ret = rvtrace_poll_bit(comp, RVTRACE_COMPONENT_CTRL_OFFSET,
				RVTRACE_COMPONENT_CTRL_ITRACE_SHIFT, 1);

done:
	dev_dbg(&encoder_data->csdev->dev, "cpu: %d enable smp call done: %d\n",
		comp->cpu, ret);
	return ret;
}

static void encoder_enable_hw_smp_call(void *info)
{
	struct component_enable_arg *arg = info;

	if (WARN_ON(!arg))
		return;
	arg->rc = encoder_enable_hw(arg->comp);
}

static int encoder_enable_perf(struct coresight_device *csdev)
{
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);

	if (WARN_ON_ONCE(comp->cpu != smp_processor_id()))
		return -EINVAL;

	return encoder_enable_hw(comp);
}

static int encoder_enable_sysfs(struct coresight_device *csdev)
{
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);
	struct encoder_data *encoder_data = rvtrace_component_data(comp);
	struct component_enable_arg arg = { };
	int ret;

	spin_lock(&encoder_data->spinlock);

	/*
	 * Executing encoder_enable_hw on the cpu whose trace encoder is being
	 * enabled ensures that register writes occur when cpu is powered.
	 */
	arg.comp = comp;
	ret = smp_call_function_single(comp->cpu,
				       encoder_enable_hw_smp_call, &arg, 1);
	if (!ret)
		ret = arg.rc;
	if (!ret)
		encoder_data->sticky_enable = true;

	spin_unlock(&encoder_data->spinlock);

	if (!ret)
		dev_dbg(&csdev->dev, "Trace Encoder tracing enabled\n");
	return ret;
}

static int encoder_enable(struct coresight_device *csdev, struct perf_event *event,
			  enum cs_mode mode, __maybe_unused struct coresight_path *path)
{
	int ret;

	if (!coresight_take_mode(csdev, mode)) {
		/* Someone is already using the tracer */
		return -EBUSY;
	}

	switch (mode) {
	case CS_MODE_SYSFS:
		ret = encoder_enable_sysfs(csdev);
		break;
	case CS_MODE_PERF:
		ret = encoder_enable_perf(csdev);
		break;
	default:
		ret = -EINVAL;
	}

	/* The tracer didn't start */
	if (ret)
		coresight_set_mode(csdev, CS_MODE_DISABLED);

	return ret;
}

static void encoder_disable_hw(struct rvtrace_component *comp)
{
	struct encoder_data *encoder_data = rvtrace_component_data(comp);

	/* Disable timestamp only if encoder has timestamp component */
	if (encoder_data->has_timestamp && encoder_data->ts_ctrl)
		timestamp_disable(comp);

	if (rvtrace_disable_component(comp))
		dev_err(&encoder_data->csdev->dev,
			"timeout while waiting for Trace Encoder become disabled\n");

	if (rvtrace_comp_is_empty(comp))
		dev_err(&encoder_data->csdev->dev,
			"timeout while waiting for all generated trace have been emitted\n");

	dev_dbg(&encoder_data->csdev->dev, "cpu: %d disable smp call done\n", comp->cpu);
}

static void encoder_disable_sysfs_smp_call(void *info)
{
	struct rvtrace_component *comp = info;

	encoder_disable_hw(comp);
}

static void encoder_disable_sysfs(struct coresight_device *csdev)
{
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);
	struct encoder_data *encoder_data = rvtrace_component_data(comp);

        /*
         * Taking hotplug lock here protects from clocks getting disabled
         * with tracing being left on (crash scenario) if user disable occurs
         * after cpu online mask indicates the cpu is offline but before the
         * DYING hotplug callback is serviced by the trace encoder driver.
         */
        cpus_read_lock();
        spin_lock(&encoder_data->spinlock);

        /*
         * Executing encoder_disable_hw on the cpu whose trace encoder is being
         * disabled ensures that register writes occur when cpu is powered.
         */
	smp_call_function_single(comp->cpu, encoder_disable_sysfs_smp_call, comp, 1);

	spin_unlock(&encoder_data->spinlock);
	cpus_read_unlock();

	dev_dbg(&csdev->dev, "Trace Encoder tracing disabled\n");
}

static void encoder_disable_perf(struct coresight_device *csdev)
{
	struct rvtrace_component *comp = dev_get_drvdata(csdev->dev.parent);

	if (WARN_ON_ONCE(comp->cpu != smp_processor_id()))
		return;

	encoder_disable_hw(comp);
}

static void encoder_disable(struct coresight_device *csdev,
			    struct perf_event *event)
{
	enum cs_mode mode;

	/*
	 * For as long as the tracer isn't disabled another entity can't
	 * change its status.  As such we can read the status here without
	 * fearing it will change under us.
	 */
	mode = coresight_get_mode(csdev);

	switch (mode) {
	case CS_MODE_DISABLED:
		break;
	case CS_MODE_SYSFS:
		encoder_disable_sysfs(csdev);
		break;
	case CS_MODE_PERF:
		encoder_disable_perf(csdev);
		break;
	default:
		WARN_ON_ONCE(mode);
		return;
	}

	if (mode)
		coresight_set_mode(csdev, CS_MODE_DISABLED);
}

static const struct coresight_ops_source encoder_source_ops = {
	.cpu_id		= encoder_cpu_id,
	.enable         = encoder_enable,
	.disable        = encoder_disable,
};

static const struct coresight_ops encoder_cs_ops = {
	.source_ops     = &encoder_source_ops,
};

void encoder_set_default(struct rvtrace_component *comp)
{
	struct encoder_data *encoder_data = rvtrace_component_data(comp);
	if (WARN_ON_ONCE(!encoder_data))
		return;

	struct encoder_config *config = &encoder_data->config;

	/* Enable sending trace messages/fields with scontext/mcontext values
	 * and/or privilege levels
	 */
	config->context = true;

	/* Allows trTeInstTracing to be set or cleared by Trace-on and Trace-
	 * off signals generated by the corresponding trigger module.
	 */
	config->inst_trigger = true;

	/* Enable periodic instruction trace synchronization */
	config->inst_syncmode = ENCODER_SYNCMODE_CLOCK;
	config->inst_syncmax = 0x6;

	/* trace source ID*/
	config->srcid = encoder_cpu_id(encoder_data->csdev);
	config->srcb = 0xc;
}

static int encoder_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct coresight_platform_data *pdata;
	struct encoder_data *encoder_data;
	struct rvtrace_component *comp;
	struct coresight_desc desc = { 0 };

	comp = rvtrace_register_component(pdev);
	if (IS_ERR(comp))
		return PTR_ERR(comp);

	encoder_data = devm_kzalloc(dev, sizeof(*encoder_data), GFP_KERNEL);
	if (!encoder_data)
		return -ENOMEM;

	spin_lock_init(&encoder_data->spinlock);

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	pdev->dev.platform_data = pdata;

	platform_set_drvdata(pdev, comp);

	/* Check if encoder has timestamp component from device tree early */
	encoder_data->has_timestamp = fwnode_property_present(dev->fwnode, "riscv,timestamp-present");
	if (encoder_data->has_timestamp) {
		if (rvtrace_init_timestamp(comp, &encoder_data->ts_config)) {
			dev_err(dev, "Timestamp initialization failed\n");
			return -EINVAL;
		}

		/* TODO: Default to enabling timestamp control if present, as
		 * encoder_data->ts_ctrl can be configured via sysfs attribute,
		 * but not through perf_event at this time. Future versions may
		 * add support for configuring timestamps via perf_event.
		 */
		encoder_data->ts_ctrl = true;
	}

	/* Set component data before registration so is_visible callbacks can access it */
	comp->id.data = encoder_data;

	desc.name = devm_kasprintf(dev, GFP_KERNEL, "encoder%d", comp->cpu);
	if (!desc.name)
		return -ENOMEM;

	desc.access = CSDEV_ACCESS_IOMEM(comp->base);
	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_PROC;
	desc.ops = &encoder_cs_ops;
	desc.pdata = pdata;
	desc.dev = dev;
	desc.groups = trace_encoder_groups;
	encoder_data->csdev = coresight_register(&desc);
	if (IS_ERR(encoder_data->csdev))
		return PTR_ERR(encoder_data->csdev);

	ret = etm_perf_symlink(encoder_data->csdev, true);
	if (ret) {
		coresight_unregister(encoder_data->csdev);
		return ret;
	}

	rvtrace_cpu_encoder[comp->cpu] = comp;

	encoder_set_default(comp);

	pm_runtime_put(dev);
	dev_dbg(dev, "CPU%d: Trace Encoder initialized\n", comp->cpu);

	if (boot_enable) {
		coresight_enable_sysfs(encoder_data->csdev);
		encoder_data->boot_enable = true;
	}

	return 0;
}

static void clear_encodata(void *info)
{
	int cpu = *(int *)info;

	rvtrace_cpu_encoder[cpu]->id.data = NULL;
	rvtrace_cpu_encoder[cpu] = NULL;
}

static void encoder_remove(struct platform_device *pdev)
{
	struct rvtrace_component *comp = platform_get_drvdata(pdev);
	struct encoder_data *encoder_data = rvtrace_component_data(comp);
	struct device *dev = &pdev->dev;

	etm_perf_symlink(encoder_data->csdev, false);

	/*
	 * Taking hotplug lock here to avoid racing between encoder_remove and
	 * CPU hotplug call backs.
	 */
	cpus_read_lock();
	/*
	 * The readers for encodata[] are CPU hotplug call backs
	 * and PM notification call backs. Change encodata[i] on
	 * CPU i ensures these call backs has consistent view
	 * inside one call back function.
	 */
	if (smp_call_function_single(comp->cpu, clear_encodata, &comp->cpu, 1)) {
		rvtrace_cpu_encoder[comp->cpu]->id.data = NULL;
		rvtrace_cpu_encoder[comp->cpu] = NULL;
	}

	cpus_read_unlock();

	pm_runtime_disable(dev);
	coresight_unregister(encoder_data->csdev);
}

static const struct of_device_id encoder_match[] = {
	{.compatible = "riscv,trace-encoder"},
	{},
};

static struct platform_driver encoder_driver = {
	.probe = encoder_probe,
	.remove = encoder_remove,
	.driver = {
		.name = "trace-encoder",
		.of_match_table = encoder_match,
	},
};

module_platform_driver(encoder_driver);

MODULE_DESCRIPTION("RISC-V Trace Encoder Driver");
MODULE_LICENSE("GPL");
