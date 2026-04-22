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
#include "k3-pm_domain.h"

#define MAX_REGULATOR_PER_DOMAIN	5
#define PRIFIX_OF_PM_QOS		2000000000	

#define DEV_PM_QOS_CLK_GATE		(PRIFIX_OF_PM_QOS | 1)
#define DEV_PM_QOS_REGULATOR_GATE	(PRIFIX_OF_PM_QOS | 2)
#define DEV_PM_QOS_PM_DOMAIN_GATE	(PRIFIX_OF_PM_QOS | 4)
#define DEV_PM_QOS_DEFAULT		(PRIFIX_OF_PM_QOS | 7)

struct per_device_qos {
	struct notifier_block notifier;
	struct list_head qos_node;
	struct dev_pm_qos_request req;
	int level;
	struct device *dev;
	struct regulator *rgr[MAX_REGULATOR_PER_DOMAIN];
	int rgr_count;

	bool handle_clk;
	bool handle_regulator;
	bool handle_pm_domain;
};

struct spacemit_pm_domain {
	struct generic_pm_domain genpd;
	struct rpmi_domain_context *context;
	int pm_index;
	struct device *gdev;
	int rgr_count;
	struct regulator *rgr[MAX_REGULATOR_PER_DOMAIN];
	struct rpmi_domain_set_state_rx rx;
	struct rpmi_domain_set_state_tx tx;
	struct rpmi_mbox_message msg;

	/**
	 * manageing the device-drivers power qos
	 */
	struct list_head qos_head;
};

struct spacemit_pmu {
	struct device *dev;
	int number_domains;
	struct genpd_onecell_data genpd_data;
	struct spacemit_pm_domain **domains;
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

static int rpmi_domain_get_attrs(struct spacemit_pm_domain *spd)
{
	struct rpmi_domain_context *context = spd->context;
	struct rpmi_mbox_message msg;
	struct rpmi_domain_get_attr_tx tx;
	struct rpmi_domain_get_attr_rx rx;
	int ret;

	tx.domain_id = cpu_to_le32(spd->pm_index);
	rpmi_mbox_init_send_with_response(&msg, RPMI_DOMAIN_SRV_GET_ATTRIBUTES,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return 0;
}

static int rpmi_domain_handle_state(struct spacemit_pm_domain *spd, bool enable)
{
	struct rpmi_domain_context *context = spd->context;
	int ret;

	if (enable)
		spd->tx.state = cpu_to_le32(RPMI_DEVICE_POWER_STATE_ON);
	else
		spd->tx.state = cpu_to_le32(RPMI_DEVICE_POWER_STATE_OFF);
	spd->tx.domain_id = cpu_to_le32(spd->pm_index);

	rpmi_mbox_init_send_with_response(&spd->msg, RPMI_DOMAIN_SRV_SET_STATE,
					  &spd->tx, sizeof(spd->tx), &spd->rx, sizeof(spd->rx));
	ret = rpmi_mbox_send_message(context->chan, &spd->msg);
	if (ret)
		return ret;

	if (spd->rx.status && (spd->rx.status != RPMI_ERR_ALREADY))
		return rpmi_to_linux_error(spd->rx.status);

	return 0;
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
		pr_err("%s: index:%d, domain handle state failed\n", __func__, spd->pm_index);
		return ret;
	}

	/* disable the supply */
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

	/* force to disable the power-switch */
	ret = rpmi_domain_handle_state(spd, false);
	if (ret) {
		pr_err("%s: index:%d, domain handle state failed\n", __func__, spd->pm_index);
		return ret;
	}

	ret = rpmi_domain_handle_state(spd, true);
	if (ret) {
		pr_err("%s: index:%d, domain handle state failed\n", __func__, spd->pm_index);
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

	if ((pos->level & DEV_PM_QOS_CLK_GATE) && pos->handle_clk)
		pm_clk_resume(dev);

	return 0;
}

static int spacemit_pm_add_one_domain(struct spacemit_pmu *pmu, struct device_node *node, int num)
{
	int err, count, i;
	struct spacemit_pm_domain *pd;
	struct rpmi_domain_context *context = dev_get_drvdata(pmu->dev);
	const char *strings[MAX_REGULATOR_PER_DOMAIN];

	pd = (struct spacemit_pm_domain *)devm_kzalloc(pmu->dev, sizeof(struct spacemit_pm_domain), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->pm_index = num;

	pd->gdev = pmu->dev;
	pd->context = context;


	err = rpmi_domain_get_attrs(pd);
	if (err) {
		dev_err(pmu->dev, "Failed to get domain-%u attributes, %d\n", num, err);
		return err;
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

	pm_genpd_init(&pd->genpd, NULL, true);

	pmu->domains[num] = pd;

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
	int i = 0;
	struct device *dev = &pdev->dev;
	struct device_node *node;
	struct device_node *np = dev->of_node;
	struct rpmi_domain_context *context;
	struct spacemit_pmu *pmu = NULL;
	int err = -ENODEV;


	pmu = (struct spacemit_pmu *)devm_kzalloc(dev, sizeof(struct spacemit_pmu), GFP_KERNEL);
	if (pmu == NULL) {
		pr_err("%s:%d, err\n", __func__, __LINE__);
		return -ENOMEM;
	}

	pmu->dev = dev;
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

	/* get number power domains */
	pmu->number_domains = rpmi_domain_get_num(context);
	pmu->domains = devm_kzalloc(dev, sizeof(struct spacemit_pm_domain *) * pmu->number_domains,
			GFP_KERNEL);
	if (!pmu->domains) {
		pr_err("%s:%d, err\n", __func__, __LINE__);
		return -ENOMEM;
	}

	for_each_available_child_of_node(np, node) {
		err = spacemit_pm_add_one_domain(pmu, node, i);
		if (err) {
			pr_err("%s:%d, failed to handle node %pOFn: %d\n", __func__, __LINE__,
					node, err);
			of_node_put(node);
			goto err_out;
		}

		++i;
	}

	pmu->genpd_data.domains = (struct generic_pm_domain **)pmu->domains;
	pmu->genpd_data.num_domains = pmu->number_domains;

	err = of_genpd_add_provider_onecell(np, &pmu->genpd_data);
	if (err) {
		pr_err("failed to add provider: %d\n", err);
		goto err_out;
	}

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
device_initcall(spacemit_pm_domain_drv_register);
