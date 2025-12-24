// SPDX-License-Identifier: GPL-2.0-only

#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/clk/clk-conf.h>
#include <linux/pm_qos.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <linux/pm_opp.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include "../opp/opp.h"
#include "cpufreq-dt.h"

struct private_data {
	struct list_head node;

	cpumask_var_t cpus;
	struct device *cpu_dev;
	struct cpufreq_frequency_table *freq_table;
	bool have_static_opps;
	int opp_token;
};

static int spacemit_policy_notifier(struct notifier_block *nb,
                                  unsigned long event, void *data)
{
	int cpu;
	u64 rates;
	struct clk *cci_clk;
	struct device *cpu_dev;
	struct cpufreq_policy *policy = data;
	struct opp_table *opp_table;

	cpu = cpumask_first(policy->related_cpus);
	cpu_dev = get_cpu_device(cpu);
	opp_table = _find_opp_table(cpu_dev);

	cci_clk = of_clk_get_by_name(opp_table->np, "cci");
	if (!IS_ERR(cci_clk)) {
		of_property_read_u64_array(opp_table->np, "cci-hz", &rates, 1);
		clk_enable(cci_clk);
		clk_set_rate(cci_clk, rates);
		clk_put(cci_clk);
	}

	if (policy->clk)
		clk_put(policy->clk);

	/* cover the policy->clk & opp_table->clk which has been set before */
	policy->clk = opp_table->clks[0];
	opp_table->clk = opp_table->clks[0];

	return 0;
}

static struct notifier_block spacemit_policy_notifier_block = {
       .notifier_call = spacemit_policy_notifier,
};

extern struct private_data *cpufreq_dt_find_data(int cpu);
extern void cpufreq_dt_add_data(struct private_data *priv);

static int spacemit_dt_cpufreq_pre_early_init(struct device *dev, int cpu)
{
	struct private_data *priv;
	struct device *cpu_dev;
	const char *reg_name[] = { "clst", NULL };
	const char *clk_name[] = { "cls0", "cls1", NULL };
	struct dev_pm_opp_config config = {
		.regulator_names = reg_name,
		.clk_names = clk_name,
		.config_clks = dev_pm_opp_config_clks_simple,
	};
	int ret;

	/* Check if this CPU is already covered by some other policy */
	if (cpufreq_dt_find_data(cpu))
		return 0;

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev)
		return -EPROBE_DEFER;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (!alloc_cpumask_var(&priv->cpus, GFP_KERNEL))
		return -ENOMEM;

	cpumask_set_cpu(cpu, priv->cpus);
	priv->cpu_dev = cpu_dev;

	if (cpu >= 8)
		config.regulator_names = NULL;
	/*
	 * OPP layer will be taking care of regulators now, but it needs to know
	 * the name of the regulator first.
	 */
	priv->opp_token = dev_pm_opp_set_config(cpu_dev, &config);
	if (priv->opp_token < 0) {
		ret = -EPROBE_DEFER;
		goto free_cpumask;
	}

	/* Get OPP-sharing information from "operating-points-v2" bindings */
	ret = dev_pm_opp_of_get_sharing_cpus(cpu_dev, priv->cpus);
	if (ret)
		goto out;

	/*
	 * Initialize OPP tables for all priv->cpus. They will be shared by
	 * all CPUs which have marked their CPUs shared with OPP bindings.
	 *
	 * For platforms not using operating-points-v2 bindings, we do this
	 * before updating priv->cpus. Otherwise, we will end up creating
	 * duplicate OPPs for the CPUs.
	 *
	 * OPPs might be populated at runtime, don't fail for error here unless
	 * it is -EPROBE_DEFER.
	 */
	ret = dev_pm_opp_of_cpumask_add_table(priv->cpus);
	if (!ret) {
		priv->have_static_opps = true;
	} else if (ret == -EPROBE_DEFER) {
		goto out;
	}

	/*
	 * The OPP table must be initialized, statically or dynamically, by this
	 * point.
	 */
	ret = dev_pm_opp_get_opp_count(cpu_dev);
	if (ret <= 0) {
		dev_err(cpu_dev, "OPP table can't be empty\n");
		ret = -ENODEV;
		goto out;
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &priv->freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out;
	}

	cpufreq_dt_add_data(priv);

	return 0;

out:
	if (priv->have_static_opps)
		dev_pm_opp_of_cpumask_remove_table(priv->cpus);
	dev_pm_opp_put_regulators(priv->opp_token);
free_cpumask:
	free_cpumask_var(priv->cpus);
	return ret;
}

static int spacemit_dt_cpufreq_pre_probe(struct platform_device *pdev)
{
	int cpu;

	if (strncmp(pdev->name, "cpufreq-dt", 10) != 0)
		return 0;

	for_each_possible_cpu(cpu)
		spacemit_dt_cpufreq_pre_early_init(&pdev->dev, cpu);

	return 0;
}

static int __device_notifier_call(struct notifier_block *nb,
				      unsigned long event, void *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	switch (event) {
	case BUS_NOTIFY_REMOVED_DEVICE:
		break;
	case BUS_NOTIFY_UNBOUND_DRIVER:
		break;
	case BUS_NOTIFY_BIND_DRIVER:
		/* here */
		spacemit_dt_cpufreq_pre_probe(pdev);
		break;
	case BUS_NOTIFY_ADD_DEVICE:
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block spacemit_platform_nb = {
	.notifier_call = __device_notifier_call,
};

static int __init spacemit_processor_driver_init(void)
{
       int ret;

       ret = cpufreq_register_notifier(&spacemit_policy_notifier_block, CPUFREQ_POLICY_NOTIFIER);
       if (ret) {
               pr_err("register cpufreq notifier failed\n");
               return -EINVAL;
       }

	bus_register_notifier(&platform_bus_type, &spacemit_platform_nb);

       return 0;
}
arch_initcall(spacemit_processor_driver_init);
