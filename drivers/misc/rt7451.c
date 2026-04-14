// SPDX-License-Identifier: GPL-2.0-only
/*
 * RT7451 retimer driver
 *
 * Copyright (C) 2025 SPACEMIT Micro Limited
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/property.h>

#define RT7451_PRIMARY_ADDR_DEFAULT	0x13
#define RT7451_SECONDARY_ADDR_DEFAULT	0x29

#define RT7451_REG_CONFIG1		0xf8
#define RT7451_REG_CONFIG2		0xa4

#define RT7451_CONFIG1_INIT_VAL		0x16
#define RT7451_CONFIG2_INIT_VAL		0x28

struct rt7451_data {
	u8 primary_addr;
	u8 secondary_addr;
};

static int rt7451_write_reg(struct i2c_adapter *adap, u8 chip_addr, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2] = { reg, val };
	int ret;

	msg.addr = chip_addr;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	ret = i2c_transfer(adap, &msg, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	return 0;
}

static int rt7451_read_reg(struct i2c_adapter *adap, u8 chip_addr, u8 reg, u8 *val)
{
	struct i2c_msg msgs[2];
	u8 reg_buf = reg;
	int ret;

	msgs[0].addr = chip_addr;
	msgs[0].flags = 0;
	msgs[0].len = 1;
	msgs[0].buf = &reg_buf;

	msgs[1].addr = chip_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = val;

	ret = i2c_transfer(adap, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	return 0;
}

static int rt7451_write_and_verify(struct device *dev, struct i2c_adapter *adap,
				   u8 chip_addr, u8 reg, u8 val)
{
	u8 readback;
	int ret;

	ret = rt7451_write_reg(adap, chip_addr, reg, val);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to write chip 0x%02x reg 0x%02x\n",
				     chip_addr, reg);

	usleep_range(100, 200);

	ret = rt7451_read_reg(adap, chip_addr, reg, &readback);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to verify chip 0x%02x reg 0x%02x\n",
				     chip_addr, reg);

	if (readback != val) {
		dev_err(dev,
			"verify failed for chip 0x%02x reg 0x%02x: wrote 0x%02x, read 0x%02x\n",
			chip_addr, reg, val, readback);
		return -EIO;
	}

	return 0;
}

static int rt7451_reg_matches(struct device *dev, struct i2c_adapter *adap,
			      u8 chip_addr, u8 reg, u8 expected, bool *matches)
{
	u8 val;
	int ret;

	ret = rt7451_read_reg(adap, chip_addr, reg, &val);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to read chip 0x%02x reg 0x%02x\n",
				     chip_addr, reg);

	*matches = val == expected;
	if (!*matches)
		dev_dbg(dev,
			"chip 0x%02x reg 0x%02x is 0x%02x, expected 0x%02x\n",
			chip_addr, reg, val, expected);

	return 0;
}

static int rt7451_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct i2c_adapter *adap = client->adapter;
	struct rt7451_data *data;
	u32 prop;
	bool primary_ok;
	bool secondary_ok;
	bool wrote_primary = false;
	int ret;

	if (!i2c_check_functionality(adap, I2C_FUNC_I2C))
		return dev_err_probe(dev, -EOPNOTSUPP, "I2C transfers are not supported\n");

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->primary_addr = client->addr;
	if (!device_property_read_u32(dev, "primary-addr", &prop))
		data->primary_addr = prop;

	data->secondary_addr = RT7451_SECONDARY_ADDR_DEFAULT;
	if (!device_property_read_u32(dev, "secondary-addr", &prop))
		data->secondary_addr = prop;

	i2c_set_clientdata(client, data);

	ret = rt7451_reg_matches(dev, adap, data->primary_addr, RT7451_REG_CONFIG1,
				 RT7451_CONFIG1_INIT_VAL, &primary_ok);
	if (ret)
		return ret;

	if (!primary_ok) {
		ret = rt7451_write_and_verify(dev, adap, data->primary_addr,
					      RT7451_REG_CONFIG1,
					      RT7451_CONFIG1_INIT_VAL);
		if (ret)
			return ret;
		wrote_primary = true;
	}

	ret = rt7451_reg_matches(dev, adap, data->secondary_addr, RT7451_REG_CONFIG2,
				 RT7451_CONFIG2_INIT_VAL, &secondary_ok);
	if (ret)
		return ret;

	if (primary_ok && secondary_ok) {
		dev_info(dev, "already initialized by firmware\n");
		return 0;
	}

	if (!secondary_ok) {
		if (wrote_primary)
			usleep_range(1000, 2000);

		ret = rt7451_write_and_verify(dev, adap, data->secondary_addr,
					      RT7451_REG_CONFIG2,
					      RT7451_CONFIG2_INIT_VAL);
		if (ret)
			return ret;
	}

	dev_info(dev, "initialized\n");

	return 0;
}

static const struct of_device_id rt7451_of_match[] = {
	{ .compatible = "retimer,rt7451" },
	{ }
};
MODULE_DEVICE_TABLE(of, rt7451_of_match);

static const struct i2c_device_id rt7451_i2c_id[] = {
	{ "rt7451" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt7451_i2c_id);

static struct i2c_driver rt7451_driver = {
	.probe = rt7451_probe,
	.driver = {
		.name = "rt7451",
		.of_match_table = rt7451_of_match,
	},
	.id_table = rt7451_i2c_id,
};

module_i2c_driver(rt7451_driver);

MODULE_AUTHOR("SPACEMIT");
MODULE_DESCRIPTION("RT7451 retimer driver");
MODULE_LICENSE("GPL");
