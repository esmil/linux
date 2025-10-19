// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit Generic power domain support over rpmi.
 *
 * Copyright (c) 2025 SPACEMIT, Co. Ltd.
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/mfd/syscon.h>
#include <linux/spinlock_types.h>
#include <linux/regulator/consumer.h>
#include <linux/syscore_ops.h>
#include <linux/mailbox/riscv-rpmi-message.h>
#include <linux/mailbox_client.h>
#include <dt-bindings/pmu/k3_pmu.h>
#include "atomic_qos.h"
#include "k3-pm_domain.h"

#define MAX_REGMAP		5
#define MAX_REGULATOR_PER_DOMAIN	5

#define MPMU_REGMAP_INDEX	0
#define APMU_REGMAP_INDEX	1

#define APMU_POWER_STATUS_REG	0xf0
#define MPMU_APCR_PER_REG	0x1098
#define MPMU_AWUCRM_REG		0x104c

#define APMU_AUDIO_CLK_RES_CTRL	0x14c
#define AP_POWER_CTRL_AUDIO_AUTH_OFFSET	28
#define FORCE_AUDIO_POWER_ON_OFFSET	13

/* wakeup set */
/* pmic */
#define WAKEUP_SOURCE_WAKEUP_7	7
/* gpio */
#define WAKEUP_SOURCE_WAKEUP_2	2

/* usb & others */
#define WAKEUP_SOURCE_WAKEUP_5	5
static bool pmu_support_wakeup5 = false;

#define PM_QOS_BLOCK_C1		0x0 /* core wfi */
#define PM_QOS_BLOCK_C2		0x2 /* core power off */
#define PM_QOS_BLOCK_M2		0x6 /* core l2 off */
#define PM_QOS_BLOCK_AXI        0x7 /* d1p */
#define PM_QOS_BLOCK_DDR        12 /* d1 */
#define PM_QOS_BLOCK_UDR_VCTCXO 13 /* d2 */
#define PM_QOS_BLOCK_UDR        14 /* d2pp */
#define PM_QOS_BLOCK_DEFAULT_VALUE	15

#define PM_QOS_AXISDD_OFFSET	31
#define PM_QOS_DDRCORSD_OFFSET	27
#define PM_QOS_APBSD_OFFSET	26
#define PM_QOS_VCTCXOSD_OFFSET	19
#define PM_QOS_STBYEN_OFFSET	13
#define PM_QOS_PE_VOTE_AP_SLPEN_OFFSET	3

#define DEV_PM_QOS_CLK_GATE		1
#define DEV_PM_QOS_REGULATOR_GATE	2
#define DEV_PM_QOS_PM_DOMAIN_GATE	4
#define DEV_PM_QOS_DEFAULT		7

struct spacemit_pm_domain_param {
	int pm_qos;
};

struct per_device_qos {
	struct notifier_block notifier;
	struct list_head qos_node;
	struct dev_pm_qos_request req;
	int level;
	struct device *dev;
	struct regulator *rgr[MAX_REGULATOR_PER_DOMAIN];
	int rgr_count;
	/**
	 * manageing the cpuidle-qos, should be per-device
	 */
	struct atomic_freq_qos_request qos;

	bool handle_clk;
	bool handle_regulator;
	bool handle_pm_domain;
	bool handle_cpuidle_qos;
};

struct spacemit_pm_domain {
	struct generic_pm_domain genpd;
	struct rpmi_domain *domain;
	int pm_index;
	struct device *gdev;
	int rgr_count;
	struct regulator *rgr[MAX_REGULATOR_PER_DOMAIN];
	/**
	 * manageing the cpuidle-qos
	 */
	struct spacemit_pm_domain_param param;

	/**
	 * manageing the device-drivers power qos
	 */
	struct list_head qos_head;
};

struct spacemit_pmu {
	struct device *dev;
	int number_domains;
	struct genpd_onecell_data genpd_data;
	struct regmap *regmap[MAX_REGMAP];
	struct spacemit_pm_domain **domains;
	/**
	 * manageing the cpuidle-qos
	 */
	struct notifier_block notifier;
};

static struct spacemit_pmu *gpmu;

static struct atomic_freq_constraints afreq_constraints;

static const struct of_device_id spacemit_regmap_dt_match[] = {
	{ .compatible = "spacemit,spacemit-mpmu", },
	{ .compatible = "spacemit,spacemit-apmu", },
};

static int rpmi_domain_get_num(struct rpmi_domain_context *context)
{
	struct rpmi_mbox_message msg;
	struct rpmi_get_num_pdomain_rx rx;
	int ret;

	rpmi_mbox_init_send_with_response(&msg, RPMI_DOMAIN_SRV_GET_NUM_DOMAINS,
					  NULL, 0, &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return rx.num_domains;
}

static int rpmi_domain_get_attrs(u32 id, struct rpmi_domain *domain)
{
	struct rpmi_domain_context *context = domain->context;
	struct rpmi_mbox_message msg;
	struct rpmi_domain_get_attr_tx tx;
	struct rpmi_domain_get_attr_rx rx;
	int ret;

	tx.domain_id = cpu_to_le32(id);
	rpmi_mbox_init_send_with_response(&msg, RPMI_DOMAIN_SRV_GET_ATTRIBUTES,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);
	domain->id = id;
	strscpy(domain->name, rx.name, RPMI_DOMAIN_NAME_LEN);

	return 0;
}

static int rpmi_domain_handle_state(struct spacemit_pm_domain *spd, bool enable)
{
	struct rpmi_domain *domain = spd->domain;
	struct rpmi_domain_context *context = domain->context;
	struct rpmi_mbox_message msg;
	struct rpmi_domain_set_state_rx rx;
	struct rpmi_domain_set_state_tx tx;
	int ret;

	if (enable)
		tx.state = cpu_to_le32(RPMI_DEVICE_POWER_STATE_ON);
	else
		tx.state = cpu_to_le32(RPMI_DEVICE_POWER_STATE_OFF);
	tx.domain_id = cpu_to_le32(domain->id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_DOMAIN_SRV_SET_STATE,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return 0;
}

static u32 rpmi_domain_get_state(struct spacemit_pm_domain *spd)
{
	struct rpmi_domain *domain = spd->domain;
	struct rpmi_domain_context *context = domain->context;
	struct rpmi_mbox_message msg;
	struct rpmi_domain_get_state_rx rx;
	struct rpmi_domain_get_state_tx tx;
	int ret;

	tx.domain_id = cpu_to_le32(domain->id);
	rpmi_mbox_init_send_with_response(&msg, RPMI_DOMAIN_SRV_GET_STATE,
					   &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return rx.state;
}

static int spacemit_pd_power_off(struct generic_pm_domain *domain)
{
	int loop, ret;
	struct per_device_qos *pos;
	struct spacemit_pm_domain *spd = container_of(domain, struct spacemit_pm_domain, genpd);

	/**
	 * if all the devices in this power domain don't want the pm-domain driver taker over
	 * the power-domian' on/off, return directly.
	 */
	list_for_each_entry(pos, &spd->qos_head, qos_node) {
		if (!pos->handle_pm_domain)
			return 0;
	}

	/**
	 * as long as there is one device don't want to on/off this power-domain, just return
	 */
	list_for_each_entry(pos, &spd->qos_head, qos_node) {
		if ((pos->level & DEV_PM_QOS_PM_DOMAIN_GATE) == 0)
			return 0;
	}

	ret = rpmi_domain_handle_state(spd, false);
	if (ret) {
		pr_err("%s: domain handle state failed\n", __func__);
		return ret;
	}

	/* enable the supply */
	for (loop = 0; loop < spd->rgr_count; ++loop) {
		ret = regulator_disable(spd->rgr[loop]);
		if (ret < 0) {
			pr_err("%s: regulator disable failed\n", __func__);
			return ret;
		}
	}

	return 0;
}

static int spacemit_pd_power_on(struct generic_pm_domain *domain)
{
	int loop, ret;
	unsigned int val;
	struct per_device_qos *pos;
	struct spacemit_pm_domain *spd = container_of(domain, struct spacemit_pm_domain, genpd);

	/**
	 * if all the devices in this power domain don't want the pm-domain driver taker over
	 * the power-domian' on/off, return directly.
	 * */
	list_for_each_entry(pos, &spd->qos_head, qos_node) {
		if (!pos->handle_pm_domain)
			return 0;
	}

	/**
	 * as long as there is one device don't want to on/off this power-domain, just return
	 */
	list_for_each_entry(pos, &spd->qos_head, qos_node) {
		if ((pos->level & DEV_PM_QOS_PM_DOMAIN_GATE) == 0)
			return 0;
	}

	/* enable the supply */
	for (loop = 0; loop < spd->rgr_count; ++loop) {
		ret = regulator_enable(spd->rgr[loop]);
		if (ret < 0) {
			pr_err("%s: regulator disable failed\n", __func__);
			return ret;
		}
	}

	val = rpmi_domain_get_state(spd);
	if (val == 1) {
		if (spd->pm_index == K3_PMU_LCD_PWR_DOMAIN)
			return 0;

		ret = rpmi_domain_handle_state(spd, false);
		if (ret) {
			pr_err("%s: domain handle state failed\n", __func__);
			return ret;
		}
	} else if (val < 0) {
		pr_err("%s: domain get state failed\n", __func__);
		return val;
	}
	ret = rpmi_domain_handle_state(spd, true);
	if (ret) {
		pr_err("%s: domain handle state failed\n", __func__);
		return ret;
	}

	return 0;
}

static int spacemit_handle_level_notfier_call(struct notifier_block *nb, unsigned long action, void *data)
{
	struct per_device_qos *per_qos = container_of(nb, struct per_device_qos, notifier);

	per_qos->level = action;

	return 0;
}

static int spacemit_pd_attach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	int err, i = 0, count;
	struct clk *clk;
	struct per_device_qos *per_qos, *pos;
	const char *strings[MAX_REGULATOR_PER_DOMAIN];
	struct spacemit_pm_domain *spd = container_of(genpd, struct spacemit_pm_domain, genpd);

	/**
	 * per-device qos set
	 * this feature enable the device drivers to dynamically modify the power
	 * module taken over by PM domain driver
	 */
	per_qos = (struct per_device_qos *)devm_kzalloc(dev, sizeof(struct per_device_qos), GFP_KERNEL);
	if (!per_qos) {
		pr_err(" allocate per device qos error\n");
		return -ENOMEM;
	}

	per_qos->dev = dev;
	INIT_LIST_HEAD(&per_qos->qos_node);
	list_add(&per_qos->qos_node, &spd->qos_head);
	per_qos->notifier.notifier_call = spacemit_handle_level_notfier_call;

	dev_pm_qos_add_notifier(dev, &per_qos->notifier, DEV_PM_QOS_MAX_FREQUENCY);

	dev_pm_qos_add_request(dev, &per_qos->req, DEV_PM_QOS_MAX_FREQUENCY, DEV_PM_QOS_DEFAULT);

	if (!of_property_read_bool(dev->of_node, "clk,pm-runtime,no-sleep")) {
		err = pm_clk_create(dev);
		if (err) {
			 dev_err(dev, "pm_clk_create failed %d\n", err);
			 return err;
		}

		while ((clk = of_clk_get(dev->of_node, i++)) && !IS_ERR(clk)) {
			err = pm_clk_add_clk(dev, clk);
			if (err) {
				 dev_err(dev, "pm_clk_add_clk failed %d\n", err);
				 clk_put(clk);
				 pm_clk_destroy(dev);
				 return err;
			}
		}

		per_qos->handle_clk = true;
	}

	/* parse the regulator */
	if (!of_property_read_bool(dev->of_node, "regulator,pm-runtime,no-sleep")) {
		count = of_property_count_strings(dev->of_node, "vin-supply-names");
		if (count < 0)
			pr_debug("no vin-suppuly-names found\n");
		else {
			err = of_property_read_string_array(dev->of_node, "vin-supply-names",
				strings, count);
			if (err < 0) {
				pr_info("read string array vin-supplu-names error\n");
				return err;
			}

			for (i = 0; i < count; ++i) {
				per_qos->rgr[i] = devm_regulator_get(dev, strings[i]);
				if (IS_ERR(per_qos->rgr[i])) {
					pr_err("regulator supply %s, get failed\n", strings[i]);
					return PTR_ERR(per_qos->rgr[i]);
				}
			}

			per_qos->rgr_count = count;
		}

		per_qos->handle_regulator = true;
	}

	/* dealing with the cpuidle-qos */
	if (of_property_read_bool(dev->of_node, "cpuidle,pm-runtime,sleep")) {
		atomic_freq_qos_add_request(&afreq_constraints, &per_qos->qos, FREQ_QOS_MAX, PM_QOS_BLOCK_DEFAULT_VALUE);
		per_qos->handle_cpuidle_qos = true;
	}

	if (!of_property_read_bool(dev->of_node, "pwr-domain,pm-runtime,no-sleep"))
		per_qos->handle_pm_domain = true;

	list_for_each_entry(pos, &spd->qos_head, qos_node) {
		if (per_qos->handle_pm_domain != pos->handle_pm_domain) {
			pr_err("all the devices in this power domain must has the same 'pwr-domain,pm-runtime,no-sleep' perporty\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void spacemit_pd_detach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	struct per_device_qos *pos;
	struct spacemit_pm_domain *spd = container_of(genpd, struct spacemit_pm_domain, genpd);

	list_for_each_entry(pos, &spd->qos_head, qos_node) {
		if (pos->dev == dev)
			break;
	}

	if (pos->handle_clk)
		pm_clk_destroy(dev);

	if (pos->handle_regulator) {
		while (--pos->rgr_count >= 0)
			devm_regulator_put(pos->rgr[pos->rgr_count]);
	}

	if (pos->handle_pm_domain) {
		if (pos->qos.qos)
			atomic_freq_qos_remove_request(&pos->qos);
	}

	dev_pm_qos_remove_request(&pos->req);
	dev_pm_qos_remove_notifier(dev, &pos->notifier, DEV_PM_QOS_MAX_FREQUENCY);
	list_del(&pos->qos_node);
	devm_kfree(dev, pos);
}

static int spacemit_genpd_stop(struct device *dev)
{
	int loop, ret;
	struct per_device_qos *pos;
	struct generic_pm_domain *pd = pd_to_genpd(dev->pm_domain);
	struct spacemit_pm_domain *spd = container_of(pd, struct spacemit_pm_domain, genpd);

	list_for_each_entry(pos, &spd->qos_head, qos_node) {
		if (pos->dev == dev)
			break;
	}

	/* disable the clk */
	if ((pos->level & DEV_PM_QOS_CLK_GATE) && pos->handle_clk)
		pm_clk_suspend(dev);

	/* dealing with the pm_qos */
	if (pos->handle_cpuidle_qos)
		atomic_freq_qos_update_request(&pos->qos, PM_QOS_BLOCK_DEFAULT_VALUE);

	if (pos->handle_regulator && (pos->level & DEV_PM_QOS_REGULATOR_GATE)) {
		for (loop = 0; loop < pos->rgr_count; ++loop) {
			ret = regulator_disable(pos->rgr[loop]);
			if (ret < 0) {
				pr_err("%s: regulator disable failed\n", __func__);
				return ret;
			}
		}
	}

	return 0;
}

static int spacemit_genpd_start(struct device *dev)
{
	int loop, ret;
	struct per_device_qos *pos;
	struct generic_pm_domain *pd = pd_to_genpd(dev->pm_domain);
	struct spacemit_pm_domain *spd = container_of(pd, struct spacemit_pm_domain, genpd);

	list_for_each_entry(pos, &spd->qos_head, qos_node) {
		if (pos->dev == dev)
			break;
	}

	if (pos->handle_regulator && (pos->level & DEV_PM_QOS_REGULATOR_GATE)) {
		for (loop = 0; loop < pos->rgr_count; ++loop) {
			ret = regulator_enable(pos->rgr[loop]);
			if (ret < 0) {
				pr_err("%s: regulator disable failed\n", __func__);
				return ret;
			}
		}
	}

	/* dealing with the pm_qos */
	if (pos->handle_cpuidle_qos)
		atomic_freq_qos_update_request(&pos->qos, spd->param.pm_qos);

	if ((pos->level & DEV_PM_QOS_CLK_GATE) && pos->handle_clk)
		pm_clk_resume(dev);

	return 0;
}

static int spacemit_pm_add_one_domain(struct spacemit_pmu *pmu, struct device_node *node)
{
	int err, ret;
	int id, count, i;
	struct spacemit_pm_domain *pd;
	struct rpmi_domain_context *context = dev_get_drvdata(pmu->dev);
	const char *strings[MAX_REGULATOR_PER_DOMAIN];

	err = of_property_read_u32(node, "reg", &id);
	if (err) {
		pr_err("%s:%d, failed to retrive the domain id\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (id >= pmu->number_domains) {
		pr_err("%pOFn: invalid domain id %d\n", node, id);
		return -EINVAL;
	}

	pd = (struct spacemit_pm_domain *)devm_kzalloc(pmu->dev, sizeof(struct spacemit_pm_domain), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;
	pd->domain = devm_kzalloc(pmu->dev, sizeof(struct rpmi_domain), GFP_KERNEL);
	if (!pd->domain)
		return -ENOMEM;

	pd->pm_index = id;

	/* we will add all the notifiers to this device */
	pd->gdev = pmu->dev;
	pd->domain->context = context;

	ret = rpmi_domain_get_attrs(id, pd->domain);
	if (ret) {
		dev_err(pmu->dev, "Failed to get domain-%u attributes, %d\n", pd->domain->id, ret);
		return ret;
	}

	err = of_property_read_u32(node, "pm_qos", &pd->param.pm_qos);
	if (err) {
		pr_err("%s:%d, failed to retrive the domain pm_qos\n",
				__func__, __LINE__);
		return -EINVAL;
	}

	/* get the power supply of the power-domain */
	count = of_property_count_strings(node, "vin-supply-names");
	if (count < 0)
		pr_debug("no vin-suppuly-names found\n");
	else {
		err = of_property_read_string_array(node, "vin-supply-names",
			strings, count);
		if (err < 0) {
			pr_info("read string array vin-supplu-names error\n");
			return err;
		}

		for (i = 0; i < count; ++i) {
			pd->rgr[i] = regulator_get(NULL, strings[i]);
			if (IS_ERR(pd->rgr[i])) {
				pr_err("regulator supply %s, get failed\n", strings[i]);
				return PTR_ERR(pd->rgr[i]);
			}
		}

		pd->rgr_count = count;
	}

	INIT_LIST_HEAD(&pd->qos_head);

	pd->genpd.name = kbasename(node->full_name);
	pd->genpd.power_off = spacemit_pd_power_off;
	pd->genpd.power_on = spacemit_pd_power_on;
	pd->genpd.attach_dev = spacemit_pd_attach_dev;
	pd->genpd.detach_dev = spacemit_pd_detach_dev;

	pd->genpd.dev_ops.stop = spacemit_genpd_stop;
	pd->genpd.dev_ops.start = spacemit_genpd_start;

	/* audio power-domain is power-on by default */
	if (id == K3_PMU_AUD_PWR_DOMAIN) {
		pm_genpd_init(&pd->genpd, NULL, false);
	} else
		pm_genpd_init(&pd->genpd, NULL, true);

	pmu->domains[id] = pd;

	return 0;
}

static void spacemit_pm_remove_one_domain(struct spacemit_pm_domain *pd)
{
	int ret;

	ret = pm_genpd_remove(&pd->genpd);
	if (ret < 0) {
		pr_err("failed to remove domain '%s' : %d\n", pd->genpd.name, ret);
	}
}

static int spacemit_pm_add_subdomain(struct spacemit_pmu *pmu, struct device_node *parent)
{
	struct device_node *np;
	struct generic_pm_domain *child_domain, *parent_domain;
	int err, idx;

	for_each_child_of_node(parent, np) {
		err = of_property_read_u32(parent, "reg", &idx);
		if (err) {
			pr_err("%pOFn: failed to retrive domain id (reg): %d\n",
					parent, err);
			goto err_out;
		}

		parent_domain = &pmu->domains[idx]->genpd;

		err = spacemit_pm_add_one_domain(pmu, np);
		if (err) {
			pr_err("failed to handle node %pOFn: %d\n", np, err);
			goto err_out;
		}

		err = of_property_read_u32(np, "reg", &idx);
		if (err) {
			pr_err("%pOFn: failed to retrive domain id (reg): %d\n",
					parent, err);
			goto err_out;
		}

		child_domain = &pmu->domains[idx]->genpd;

		err = pm_genpd_add_subdomain(parent_domain, child_domain);
		if (err) {
			pr_err("%s failed to add subdomain %s: %d\n",
					parent_domain->name, child_domain->name, err);
			goto err_out;
		} else {
			pr_info("%s add subdomain: %s\n",
					parent_domain->name, child_domain->name);
		}

		spacemit_pm_add_subdomain(pmu, np);
	}

	return 0;

err_out:
	of_node_put(np);
	return err;
}

static void spacemit_pm_domain_cleanup(struct spacemit_pmu *pmu)
{
	struct spacemit_pm_domain *pd;
	int i;

	for (i = 0; i < pmu->number_domains; i++) {
		pd = pmu->domains[i];
		if (pd)
			spacemit_pm_remove_one_domain(pd);
	}

	/* devm will free our memory */
}

#ifdef CONFIG_PM_SLEEP
static int acpr_per_suspend(void)
{
	unsigned int apcr_per;

	/* enable pmic wakeup */
	regmap_read(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_AWUCRM_REG, &apcr_per);
	apcr_per |= (1 << WAKEUP_SOURCE_WAKEUP_7);
	regmap_write(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_AWUCRM_REG, apcr_per);

	/* enable pinctrl edge detect wakeup */
	regmap_read(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_AWUCRM_REG, &apcr_per);
	apcr_per |= (1 << WAKEUP_SOURCE_WAKEUP_2);
	regmap_write(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_AWUCRM_REG, apcr_per);

	/* enable usb/rcpu/ap2audio */
	if (pmu_support_wakeup5) {
		regmap_read(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_AWUCRM_REG, &apcr_per);
		apcr_per |= (1 << WAKEUP_SOURCE_WAKEUP_5);
		regmap_write(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_AWUCRM_REG, apcr_per);
	}

	return 0;
}

static void acpr_per_resume(void)
{
	unsigned int apcr_per;

	/* disable pmic wakeup */
	regmap_read(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_AWUCRM_REG, &apcr_per);
	apcr_per &= ~(1 << WAKEUP_SOURCE_WAKEUP_7);
	regmap_write(gpmu->regmap[MPMU_REGMAP_INDEX], MPMU_AWUCRM_REG, apcr_per);
}

static struct syscore_ops acpr_per_syscore_ops = {
	.suspend = acpr_per_suspend,
	.resume = acpr_per_resume,
};
#endif

static int pm_domain_rpmi_init(struct rpmi_domain_context *context)
{
	struct rpmi_mbox_message msg;
	struct device *dev = context->dev;
	int ret;

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SPEC_VERSION);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err(dev, "Failed to get spec version, %d\n", ret);
		return ret;
	}
	if (msg.attr.value < RPMI_MKVER(1, 0)) {
		dev_err(dev, "msg protocol version mismatch, expected 0x%x, found 0x%x, errno: %d\n",
				    RPMI_MKVER(1, 0), msg.attr.value, -EINVAL);
		return -EINVAL;
	}

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SERVICEGROUP_ID);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err(dev, "Failed to get service group ID, errno: %d\n", ret);
		return ret;
	}
	if (msg.attr.value != RPMI_SRVGRP_DEVICE_POWER) {
		dev_err(dev, "service group match failed, expected 0x%x, found 0x%x, errno: %d\n",
				    RPMI_SRVGRP_DEVICE_POWER, msg.attr.value, -EINVAL);
		return -EINVAL;
	}

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SERVICEGROUP_VERSION);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err(dev, "Failed to get service group version, errno: %d\n", ret);
		return ret;
	}
	if (msg.attr.value < RPMI_MKVER(1, 0)) {
		dev_err(dev, "service group version failed, expected 0x%x, found 0x%x errno: %d\n",
				    RPMI_MKVER(1, 0), msg.attr.value, -EINVAL);
		return -EINVAL;
	}

	/* Save the maximum message data size of mailbox channel */
	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_MAX_MSG_DATA_SIZE);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err(dev, "Failed to get max message data size, errno: %d\n", ret);
		return ret;
	}
	context->max_msg_data_size = msg.attr.value;

	return 0;
}

static int spacemit_pm_domain_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node;
	struct device_node *np = dev->of_node;
	struct rpmi_domain_context *context;
	struct spacemit_pmu *pmu = NULL;
	int err = 0;

	pmu = (struct spacemit_pmu *)devm_kzalloc(dev, sizeof(struct spacemit_pmu), GFP_KERNEL);
	if (pmu == NULL) {
		pr_err("%s:%d, err\n", __func__, __LINE__);
		return -ENOMEM;
	}

	pmu->dev = dev;
	pmu->regmap[0] = syscon_regmap_lookup_by_phandle(np, "spacemit,mpmu");
	if (IS_ERR(pmu->regmap[0])) {
		pr_err("%s:%d err\n", __func__, __LINE__);
		return PTR_ERR(pmu->regmap[0]);
	}

	context = devm_kzalloc(dev, sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;
	context->dev = dev;
	platform_set_drvdata(pdev, context);

	context->client.dev		= context->dev;
	context->client.rx_callback	= NULL;
	context->client.tx_block	= false;
	context->client.knows_txdone	= true;
	context->client.tx_tout		= 0;
	context->chan = mbox_request_channel(&context->client, 0);
	if (IS_ERR(context->chan))
		return PTR_ERR(context->chan);
	err = pm_domain_rpmi_init(context);
	if (err)
		return err;

	pmu_support_wakeup5 = of_property_read_bool(np, "pmu_wakeup5");

	/* get number power domains */
	pmu->number_domains = rpmi_domain_get_num(context);
	pmu->domains = devm_kzalloc(dev, sizeof(struct spacemit_pm_domain *) * pmu->number_domains,
			GFP_KERNEL);
	if (!pmu->domains) {
		pr_err("%s:%d, err\n", __func__, __LINE__);
		return -ENOMEM;
	}

	err = -ENODEV;

	for_each_available_child_of_node(np, node) {
		err = spacemit_pm_add_one_domain(pmu, node);
		if (err) {
			pr_err("%s:%d, failed to handle node %pOFn: %d\n", __func__, __LINE__,
					node, err);
			of_node_put(node);
			goto err_out;
		}

		err = spacemit_pm_add_subdomain(pmu, node);
		if (err) {
			pr_err("%s:%d, failed to handle subdomain node %pOFn: %d\n",
					__func__, __LINE__, node, err);
			of_node_put(node);
			goto err_out;
		}
	}

	if(err) {
		pr_err("no power domains defined\n");
		goto err_out;
	}

	pmu->genpd_data.domains = (struct generic_pm_domain **)pmu->domains;
	pmu->genpd_data.num_domains = pmu->number_domains;

	err = of_genpd_add_provider_onecell(np, &pmu->genpd_data);
	if (err) {
		pr_err("failed to add provider: %d\n", err);
		goto err_out;
	}

	/**
	 * dealing with the cpuidle qos
	 */
	atomic_freq_constraints_init(&afreq_constraints);

	gpmu = pmu;

#ifdef CONFIG_PM_SLEEP
	register_syscore_ops(&acpr_per_syscore_ops);
#endif
	return 0;

err_out:
	spacemit_pm_domain_cleanup(pmu);
	return err;
}

static const struct of_device_id spacemit_pm_domain_dt_match[] = {
	{ .compatible = "spacemit,power-controller", },
	{ },
};

static struct platform_driver spacemit_pm_domain_driver = {
	.probe = spacemit_pm_domain_probe,
	.driver = {
		.name   = "spacemit-pm-domain",
		.of_match_table = spacemit_pm_domain_dt_match,
	},
};

static int __init spacemit_pm_domain_drv_register(void)
{
	return platform_driver_register(&spacemit_pm_domain_driver);
}
core_initcall(spacemit_pm_domain_drv_register);
