// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit Generic regulator support over rpmi.
 *
 * Copyright (c) 2025 SPACEMIT, Co. Ltd.
 */

#include <linux/err.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/platform_device.h>
#include <linux/mailbox/riscv-rpmi-message.h>

#define RPMI_REGULATOR_NAME_LEN			16
#define RPMI_REGULATOR_DISCRETE_MAX_NUM		16
#define RPMI_REGULATOR_LINEAR_MAX_NUM		2

enum rpmi_regulator_config {
	RPMI_REGULATOR_DISABLE = 0,
	RPMI_REGULATOR_ENABLE,
};

enum rpmi_regulator_type {
	RPMI_REGULATOR_DISCRETE = 0,
	RPMI_REGULATOR_LINEAR,
	RPMI_REGULATOR_TYPE_MAX_IDX,
};

struct rpmi_regulator_context {
	struct device *dev;
	struct mbox_chan *chan;
	struct mbox_client client;
	u32 max_msg_data_size;
};

struct rpmi_regulator {
	struct rpmi_regulator_context *context;
	u32 id;
	u32 type;
	u32 num_levels;
	char name[RPMI_REGULATOR_NAME_LEN];
	struct regulator_desc *desc;
};

struct rpmi_get_num_domain_rx {
	u32 status;
	u32 num_domains;
};

struct rpmi_volt_get_attr_tx {
	u32 domain_id;
};

struct rpmi_volt_get_attr_rx {
	u32 status;
	u32 flags;
	u32 num_levels;
	u32 trans_latency;
	char name[RPMI_REGULATOR_NAME_LEN];
};

struct rpmi_volt_get_sup_tx {
	u32 domain_id;
	u32 volt_level_index;
};

struct rpmi_volt_get_sup_rx {
	u32 status;
	u32 flags;
	u32 remaining;
	u32 returned;
	u32 volt_level[];
};

struct rpmi_volt_set_config_tx {
	u32 domain_id;
	u32 config;
};

struct rpmi_volt_set_config_rx {
	u32 status;
};

struct rpmi_volt_get_config_tx {
	u32 domain_id;
};

struct rpmi_volt_get_config_rx {
	u32 status;
	u32 config;
};

struct rpmi_volt_set_level_tx {
	u32 domain_id;
	u32 volt_level;
};

struct rpmi_volt_set_level_rx {
	u32 status;
};

struct rpmi_volt_get_level_tx {
	u32 domain_id;
};

struct rpmi_volt_get_level_rx {
	u32 status;
	u32 volt_level;
};

static int regulator_rpmi_get_num(struct rpmi_regulator_context *context)
{
	struct rpmi_mbox_message msg;
	struct rpmi_get_num_domain_rx rx;
	int ret;

	rpmi_mbox_init_send_with_response(&msg, RPMI_REGULATOR_SRV_GET_NUM_DOMAINS,
					  NULL, 0, &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return rx.num_domains;
}

static int regulator_rpmi_get_attrs(u32 id, struct rpmi_regulator *reg)
{
	struct rpmi_regulator_context *context = reg->context;
	struct rpmi_mbox_message msg;
	struct rpmi_volt_get_attr_tx tx;
	struct rpmi_volt_get_attr_rx rx;
	u8 format;
	int ret;

	tx.domain_id = cpu_to_le32(id);
	rpmi_mbox_init_send_with_response(&msg, RPMI_REGULATOR_SRV_GET_ATTRIBUTES,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	reg->id = id;
	strscpy(reg->name, rx.name, RPMI_REGULATOR_NAME_LEN);
	format = rx.flags & 7U;
	if ((format >> 1) >= RPMI_REGULATOR_TYPE_MAX_IDX)
		return -EINVAL;
	reg->type = format >> 1;
	reg->num_levels = rx.num_levels;

	return 0;
}

static int regulator_rpmi_get_supported_level(u32 id, struct rpmi_regulator *reg)
{
	struct rpmi_regulator_context *context = reg->context;
	struct rpmi_mbox_message msg;
	struct rpmi_volt_get_sup_tx tx;
	struct rpmi_volt_get_sup_rx rx;
	struct regulator_desc *desc = reg->desc;
	struct linear_range ranges[RPMI_REGULATOR_LINEAR_MAX_NUM];
	int i, ret;

	tx.domain_id = cpu_to_le32(id);
	tx.volt_level_index = 0;
	if (reg->type == RPMI_REGULATOR_LINEAR) {
		rpmi_mbox_init_send_with_response(&msg, RPMI_REGULATOR_SRV_GET_SUPPORTED_LEVELS,
						  &tx, sizeof(tx), &rx, context->max_msg_data_size);
		ret = rpmi_mbox_send_message(context->chan, &msg);
		if (ret)
			return ret;
		if (rx.status)
			return rpmi_to_linux_error(rx.status);

		for (i = 0; i < rx.returned; i++) {
			ranges[i].min = rx.volt_level[3 * i];
			ranges[i].step = rx.volt_level[3 * i + 2];
			if (i == 0)
				ranges[i].min_sel = 0;
			else
				ranges[i].min_sel = ranges[i - 1].max_sel;
			ranges[i].max_sel = (rx.volt_level[3 * i + 1] - rx.volt_level[3 * i])
					    / rx.volt_level[3 * i + 2]
					    + ranges[i].min_sel;
		}
		desc->n_voltages = ranges[i].max_sel;
		desc->linear_ranges = &ranges[0];
		desc->n_linear_ranges = reg->num_levels;
	} else
		return -EINVAL;

	return 0;
}

static int regulator_rpmi_get_voltage_sel(struct regulator_dev *reg)
{
	struct device *dev = reg->dev.parent;
	struct rpmi_regulator_context *context = dev_get_drvdata(dev);
	const struct regulator_desc *desc = reg->desc;
	struct rpmi_mbox_message msg;
	struct rpmi_volt_get_level_tx tx;
	struct rpmi_volt_get_level_rx rx;
	int ret;
	unsigned int uV;

	tx.domain_id = cpu_to_le32(desc->id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_REGULATOR_SRV_GET_LEVEL,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	uV = rx.volt_level * 1000;

	return regulator_map_voltage_linear_range(reg, uV, uV);
}

static int regulator_rpmi_set_voltage_sel(struct regulator_dev *reg, unsigned sel)
{
	struct device *dev = reg->dev.parent;
	struct rpmi_regulator_context *context = dev_get_drvdata(dev);
	const struct regulator_desc *desc = reg->desc;
	struct rpmi_mbox_message msg;
	struct rpmi_volt_set_level_tx tx;
	struct rpmi_volt_set_level_rx rx;
	int ret;
	unsigned int uV;

	tx.domain_id = cpu_to_le32(desc->id);
	uV = regulator_list_voltage_linear_range(reg, sel);
	tx.volt_level = uV / 1000;

	rpmi_mbox_init_send_with_response(&msg, RPMI_REGULATOR_SRV_SET_LEVEL,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return 0;
}

static int regulator_rpmi_enable(struct regulator_dev *reg)
{
	struct device *dev = reg->dev.parent;
	struct rpmi_regulator_context *context = dev_get_drvdata(dev);
	const struct regulator_desc *desc = reg->desc;
	struct rpmi_mbox_message msg;
	struct rpmi_volt_set_config_tx tx;
	struct rpmi_volt_set_config_rx rx;
	int ret;

	tx.config = cpu_to_le32(RPMI_REGULATOR_ENABLE);
	tx.domain_id = cpu_to_le32(desc->id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_REGULATOR_SRV_SET_CONFIG,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return 0;
}

static int regulator_rpmi_disable(struct regulator_dev *reg)
{
	struct device *dev = reg->dev.parent;
	struct rpmi_regulator_context *context = dev_get_drvdata(dev);
	const struct regulator_desc *desc = reg->desc;
	struct rpmi_mbox_message msg;
	struct rpmi_volt_set_config_tx tx;
	struct rpmi_volt_set_config_rx rx;
	int ret;

	tx.config = cpu_to_le32(RPMI_REGULATOR_DISABLE);
	tx.domain_id = cpu_to_le32(desc->id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_REGULATOR_SRV_SET_CONFIG,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return 0;
}

static int regulator_rpmi_is_enabled(struct regulator_dev *reg)
{
	struct device *dev = reg->dev.parent;
	struct rpmi_regulator_context *context = dev_get_drvdata(dev);
	const struct regulator_desc *desc = reg->desc;
	struct rpmi_mbox_message msg;
	struct rpmi_volt_get_config_tx tx;
	struct rpmi_volt_get_config_rx rx;
	int ret;

	tx.domain_id = cpu_to_le32(desc->id);

	rpmi_mbox_init_send_with_response(&msg, RPMI_REGULATOR_SRV_GET_CONFIG,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return rx.config;
}

static const struct regulator_ops regulator_rpmi_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_rpmi_get_voltage_sel,
	.set_voltage_sel	= regulator_rpmi_set_voltage_sel,
	.enable			= regulator_rpmi_enable,
	.disable		= regulator_rpmi_disable,
	.is_enabled		= regulator_rpmi_is_enabled,
};

static struct regulator_desc *rpmi_regulator_enumerate(struct rpmi_regulator_context *context, u32 id)
{
	struct device *dev = context->dev;
	struct regulator_desc *desc;
	struct rpmi_regulator *reg;
	int ret;

	reg = devm_kzalloc(dev, sizeof(*reg), GFP_KERNEL);
	if (!reg)
		return ERR_PTR(-ENOMEM);
	reg->context = context;

	reg->desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!reg->desc)
		return ERR_PTR(-ENOMEM);
	desc = reg->desc;

	ret = regulator_rpmi_get_attrs(id, reg);
	if (ret)
		return dev_err_ptr_probe(dev, ret,
				"Failed to get domain-%u attrs, %d\n", id, ret);

	ret = regulator_rpmi_get_supported_level(id, reg);
	if (ret)
		return dev_err_ptr_probe(dev, ret,
				"Failed to get domain-%u levels, %d\n", id, ret);

	desc->ops = &regulator_rpmi_ops;
	desc->owner = THIS_MODULE;
	desc->name = reg->name;

	return desc;
}

static int regulator_rpmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rpmi_regulator_context *context;
	struct rpmi_mbox_message msg;
	struct regulator_dev *regulator_dev;
	struct regulator_desc *desc;
	struct regulator_config config = {};
	int ret, num_domains, i;

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

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SPEC_VERSION);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to get spec version\n");
		goto fail_free_channel;
	}
	if (msg.attr.value < RPMI_MKVER(1, 0)) {
		ret = dev_err_probe(dev, -EINVAL,
				    "msg protocol version mismatch, expected 0x%x, found 0x%x\n",
				    RPMI_MKVER(1, 0), msg.attr.value);
		goto fail_free_channel;
	}

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SERVICEGROUP_ID);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to get service group ID\n");
		goto fail_free_channel;
	}
	if (msg.attr.value != RPMI_SRVGRP_REGULATOR) {
		ret = dev_err_probe(dev, EINVAL,
				    "service group match failed, expected 0x%x, found 0x%x\n",
				    RPMI_SRVGRP_REGULATOR, msg.attr.value);
		goto fail_free_channel;
	}

	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SERVICEGROUP_VERSION);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to get service group version\n");
		goto fail_free_channel;
	}
	if (msg.attr.value < RPMI_MKVER(1, 0)) {
		ret = dev_err_probe(dev, -EINVAL,
				    "service group version failed, expected 0x%x, found 0x%x\n",
				    RPMI_MKVER(1, 0), msg.attr.value);
		goto fail_free_channel;
	}

	/* Save the maximum message data size of mailbox channel */
	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_MAX_MSG_DATA_SIZE);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to get max message data size\n");
		goto fail_free_channel;
	}
	context->max_msg_data_size = msg.attr.value;

	num_domains = regulator_rpmi_get_num(context);
	if (num_domains < 1) {
		ret = dev_err_probe(dev, -ENODEV, "No regulator found\n");
		goto fail_free_channel;
	}
	desc = devm_kzalloc(dev, sizeof(*desc) * num_domains, GFP_KERNEL);
	if (desc == NULL) {
		ret = -ENOMEM;
		goto fail_free_channel;
	}

	config.dev = &pdev->dev;
	for (i = 0; i < num_domains; i++) {
		desc = rpmi_regulator_enumerate(context, i);
		regulator_dev = devm_regulator_register(&pdev->dev,
					desc, &config);
		if (IS_ERR(regulator_dev)) {
			pr_err("failed to register %d regulator\n", i);
			return PTR_ERR(regulator_dev);
		}
	}

	return 0;

fail_free_channel:
	mbox_free_channel(context->chan);
	return ret;
}

static void regulator_rpmi_remove(struct platform_device *pdev)
{
	struct rpmi_regulator_context *context = platform_get_drvdata(pdev);

	mbox_free_channel(context->chan);
}

static const struct of_device_id regulator_rpmi_of_match[] = {
	{ .compatible = "riscv,rpmi-regulator" },
	{}
};
MODULE_DEVICE_TABLE(of, regulator_rpmi_of_match);

static struct platform_driver regulator_rpmi_driver = {
	.driver = {
		.name = "riscv-rpmi-regulator",
		.of_match_table = regulator_rpmi_of_match,
	},
	.probe = regulator_rpmi_probe,
	.remove = regulator_rpmi_remove,
};
module_platform_driver(regulator_rpmi_driver);
