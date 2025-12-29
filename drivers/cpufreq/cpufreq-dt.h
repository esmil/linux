/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Linaro
 * Viresh Kumar <viresh.kumar@linaro.org>
 */

#ifndef __CPUFREQ_DT_H__
#define __CPUFREQ_DT_H__

#include <linux/types.h>

struct cpufreq_policy;

struct cpufreq_dt_platform_data {
	bool have_governor_per_policy;

	unsigned int	(*get_intermediate)(struct cpufreq_policy *policy,
					    unsigned int index);
	int		(*target_intermediate)(struct cpufreq_policy *policy,
					       unsigned int index);
	int (*suspend)(struct cpufreq_policy *policy);
	int (*resume)(struct cpufreq_policy *policy);
};

struct platform_device *cpufreq_dt_pdev_register(struct device *dev);

#ifdef CONFIG_SOC_SPACEMIT
struct private_data {
	struct list_head node;

	cpumask_var_t cpus;
	struct device *cpu_dev;
	struct cpufreq_frequency_table *freq_table;
	bool have_static_opps;
	int opp_token;
};

struct private_data *cpufreq_dt_find_data(int cpu);
void cpufreq_dt_add_data(struct private_data *priv);
#endif

#endif /* __CPUFREQ_DT_H__ */
