// SPDX-License-Identifier: GPL-2.0
/*
 * Simplified I2C driver for Sony IMX219
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/regulator/consumer.h>

/*
 * Sensor Configuration: 1920x1080 @ 30fps, 2-lane MIPI
 *
 * MCLK:           24MHz
 * Resolution:     1920x1080
 * Bit Depth:      10bit
 * FPS:            30fps
 * HTS:            3448 (0x0d78) - registers 0x0162:0x0163
 * VTS:            1766 (0x06e6) - registers 0x0160:0x0161
 * PCLK:           182.675MHz
 * MIPI Data Rate: 913.375Mbps/Lane
 * MIPI Clock:     914MHz
 * Htime:          18.875us
 */

/* IOCTL interface for user-space control */
#define IMX219_IOC_MAGIC		'I'
#define IMX219_IOCTL_POWER_ON		_IO(IMX219_IOC_MAGIC, 1)
#define IMX219_IOCTL_POWER_OFF		_IO(IMX219_IOC_MAGIC, 2)
#define IMX219_IOCTL_INIT_REGS		_IO(IMX219_IOC_MAGIC, 3)
#define IMX219_IOCTL_STREAM_ON		_IO(IMX219_IOC_MAGIC, 4)
#define IMX219_IOCTL_STREAM_OFF		_IO(IMX219_IOC_MAGIC, 5)
#define IMX219_IOCTL_DETECT		_IO(IMX219_IOC_MAGIC, 6)

static struct imx219 *global_imx219;

struct imx219 {
	struct i2c_client *client;
	struct gpio_desc *pwdn;
	struct mutex lock;
	bool power_on;
	struct regulator *vdd;
	struct miscdevice miscdev;
};

struct regval_list {
	u16 addr;
	u8 data;
};

static struct regval_list imx219_1080p_regs[] = {
	{0x30EB, 0x05},
	{0x30EB, 0x0C},
	{0x300A, 0xFF},
	{0x300B, 0xFF},
	{0x30EB, 0x05},
	{0x30EB, 0x09},
	{0x0114, 0x01},
	{0x0128, 0x00},
	{0x012A, 0x18},
	{0x012B, 0x00},
	{0x0160, 0x06},
	{0x0161, 0xE6},
	{0x0162, 0x0D},
	{0x0163, 0x78},
	{0x0164, 0x02},
	{0x0165, 0xA8},
	{0x0166, 0x0A},
	{0x0167, 0x27},
	{0x0168, 0x02},
	{0x0169, 0xB4},
	{0x016A, 0x06},
	{0x016B, 0xEB},
	{0x016C, 0x07},
	{0x016D, 0x80},
	{0x016E, 0x04},
	{0x016F, 0x38},
	{0x0170, 0x01},
	{0x0171, 0x01},
	{0x0174, 0x00},
	{0x0175, 0x00},
	{0x018C, 0x0A},
	{0x018D, 0x0A},
	{0x0301, 0x05},
	{0x0303, 0x01},
	{0x0304, 0x03},
	{0x0305, 0x03},
	{0x0306, 0x00},
	{0x0307, 0x39},
	{0x0309, 0x0A},
	{0x030B, 0x01},
	{0x030C, 0x00},
	{0x030D, 0x72},
	{0x455E, 0x00},
	{0x471E, 0x4B},
	{0x4767, 0x0F},
	{0x4750, 0x14},
	{0x4540, 0x00},
	{0x47B4, 0x14},
	{0x0100, 0x00},
};

static int imx219_write(struct imx219 *sensor, u16 reg, u8 val)
{
	struct i2c_adapter *adapter = sensor->client->adapter;
	struct i2c_msg msg;
	u8 data[3];
	int ret;

	data[0] = (reg >> 8) & 0xff;
	data[1] = reg & 0xff;
	data[2] = val & 0xff;

	msg.addr = sensor->client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	mutex_lock(&sensor->lock);
	ret = i2c_transfer(adapter, &msg, 1);
	mutex_unlock(&sensor->lock);

	if (ret != 1) {
		dev_err(&sensor->client->dev,
			"imx219-test: I2C write failed, reg=0x%x, val=0x%x, ret=%d\n",
			reg, val, ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int imx219_read(struct imx219 *sensor, u16 reg, u8 *val)
{
	struct i2c_adapter *adapter = sensor->client->adapter;
	struct i2c_msg msgs[2];
	u8 reg_buf[2];
	u8 data_buf[1];
	int ret;

	reg_buf[0] = (reg >> 8) & 0xff;
	reg_buf[1] = reg & 0xff;

	msgs[0].addr = sensor->client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(reg_buf);
	msgs[0].buf = reg_buf;

	msgs[1].addr = sensor->client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(data_buf);
	msgs[1].buf = data_buf;

	mutex_lock(&sensor->lock);
	ret = i2c_transfer(adapter, msgs, 2);
	mutex_unlock(&sensor->lock);

	if (ret != 2) {
		dev_err(&sensor->client->dev,
			"imx219-test: I2C read failed, reg=0x%x, ret=%d\n", reg,
			ret);
		return ret < 0 ? ret : -EIO;
	}

	*val = data_buf[0];
	return 0;
}

static int imx219_stream_on(struct imx219 *sensor)
{
	return imx219_write(sensor, 0x0100, 0x01);
}

static int imx219_stream_off(struct imx219 *sensor)
{
	return imx219_write(sensor, 0x0100, 0x00);
}

static int imx219_write_init_regs(struct imx219 *sensor);
static int imx219_power_on(struct imx219 *sensor);
static void imx219_power_off(struct imx219 *sensor);
static int imx219_detect(struct imx219 *sensor);

static long imx219_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct imx219 *sensor = file->private_data;
	int ret = 0;

	if (!sensor)
		return -ENODEV;

	switch (cmd) {
	case IMX219_IOCTL_POWER_ON:
		ret = imx219_power_on(sensor);
		break;
	case IMX219_IOCTL_POWER_OFF:
		imx219_power_off(sensor);
		ret = 0;
		break;
	case IMX219_IOCTL_INIT_REGS:
		ret = imx219_write_init_regs(sensor);
		break;
	case IMX219_IOCTL_STREAM_ON:
		ret = imx219_stream_on(sensor);
		break;
	case IMX219_IOCTL_STREAM_OFF:
		ret = imx219_stream_off(sensor);
		break;
	case IMX219_IOCTL_DETECT:
		ret = imx219_detect(sensor);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int imx219_dev_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct imx219 *sensor;

	if (!misc)
		return -ENODEV;

	sensor = container_of(misc, struct imx219, miscdev);
	/* replace private_data with sensor pointer for use in ioctl */
	file->private_data = sensor;
	return 0;
}

static const struct file_operations imx219_fops = {
	.owner = THIS_MODULE,
	.open = imx219_dev_open,
	.unlocked_ioctl = imx219_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = imx219_ioctl,
#endif
};

static int imx219_write_init_regs(struct imx219 *sensor)
{
	int i, ret;

	dev_info(&sensor->client->dev,
		 "imx219-test: write init regs, total=%ld\n",
		 ARRAY_SIZE(imx219_1080p_regs));
	for (i = 0; i < ARRAY_SIZE(imx219_1080p_regs); i++) {
		const struct regval_list *reg = &imx219_1080p_regs[i];

		ret = imx219_write(sensor, reg->addr, reg->data);
		if (ret < 0) {
			dev_err(&sensor->client->dev,
				"imx219-test: write init reg failed, idx=%d, reg=0x%x, val=0x%x, ret=%d\n",
				i, reg->addr, reg->data, ret);
			return ret;
		}
	}

	dev_info(&sensor->client->dev, "imx219-test: init regs written\n");
	return 0;
}

static int imx219_detect(struct imx219 *sensor)
{
	u8 hi, lo;
	int ret;

	dev_info(&sensor->client->dev, "imx219-test: detect start\n");

	ret = imx219_read(sensor, 0x0000, &hi);
	if (ret < 0)
		return ret;
	if ((hi & 0x0f) != 0x02) {
		dev_err(&sensor->client->dev,
			"imx219-test: ID high mismatch: 0x%x\n", hi);
		return -ENODEV;
	}
	ret = imx219_read(sensor, 0x0001, &lo);
	if (ret < 0)
		return ret;
	if (lo != 0x19) {
		dev_err(&sensor->client->dev,
			"imx219-test: ID low mismatch: 0x%x\n", lo);
		return -ENODEV;
	}

	dev_info(&sensor->client->dev,
		 "imx219-test: detected IMX219 (id %02x%02x)\n", hi, lo);
	return 0;
}

static int imx219_power_on(struct imx219 *sensor)
{
	int ret;

	dev_info(&sensor->client->dev,
		 "imx219-test: power_on enter, power_on=%d\n",
		 sensor->power_on);
	if (sensor->power_on) {
		dev_info(&sensor->client->dev,
			 "imx219-test: already powered on\n");
		return 0;
	}

	dev_info(&sensor->client->dev, "imx219-test: get vdd regulator\n");
	sensor->vdd = devm_regulator_get(&sensor->client->dev, "vdd");
	if (IS_ERR(sensor->vdd)) {
		dev_err(&sensor->client->dev, "imx219-test: Failed to get vdd regulator: %ld\n",
			PTR_ERR(sensor->vdd));
		return PTR_ERR(sensor->vdd);
	}

	if (sensor->vdd) {
		ret = regulator_enable(sensor->vdd);
		if (ret < 0) {
			dev_err(&sensor->client->dev,
				"imx219-test: failed to enable vdd: %d\n", ret);
			return ret;
		}
		dev_info(&sensor->client->dev, "imx219-test: enable vdd\n");

		ret = regulator_set_voltage(sensor->vdd, 3300000, 3300000);
		if (ret < 0) {
			dev_err(&sensor->client->dev, "imx219-test: failed to set vdd voltage: %d\n",
				ret);
			return ret;
		}
		dev_info(&sensor->client->dev, "imx219-test: vdd set to 3.3V\n");
	}

	if (sensor->pwdn) {
		dev_info(&sensor->client->dev, "imx219-test: drive PWDN low\n");
		gpiod_set_value_cansleep(sensor->pwdn, 0);
	}

	usleep_range(10000, 12000);

	if (sensor->pwdn) {
		dev_info(&sensor->client->dev,
			 "imx219-test: drive PWDN high\n");
		gpiod_set_value_cansleep(sensor->pwdn, 1);
	}

	usleep_range(30000, 31000);

	sensor->power_on = true;
	dev_info(&sensor->client->dev, "imx219-test: power_on done\n");

	return 0;
}

static void imx219_power_off(struct imx219 *sensor)
{
	dev_info(&sensor->client->dev, "imx219-test: power_off enter\n");
	if (!sensor->power_on) {
		dev_info(&sensor->client->dev,
			 "imx219-test: already powered off\n");
		return;
	}

	if (sensor->pwdn) {
		dev_info(&sensor->client->dev, "imx219-test: drive PWDN low\n");
		gpiod_set_value_cansleep(sensor->pwdn, 0);
	}

	if (sensor->vdd) {
		dev_info(&sensor->client->dev, "imx219-test: disable vdd\n");
		regulator_disable(sensor->vdd);
	}

	sensor->power_on = false;
	dev_info(&sensor->client->dev, "imx219-test: power_off done\n");
}

static int imx219_probe(struct i2c_client *client)
{
	struct imx219 *sensor;
	struct device *dev = &client->dev;
	int ret;

	dev_info(dev, "imx219-test: probe enter, client addr=0x%x\n",
		 client->addr);

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->client = client;
	mutex_init(&sensor->lock);
	i2c_set_clientdata(client, sensor);

	dev_info(dev, "imx219-test: get PWDN gpio\n");
	sensor->pwdn = devm_gpiod_get_optional(
		dev, "pwdn", GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(sensor->pwdn)) {
		dev_err(dev, "imx219-test: Failed to get PWDN GPIO\n");
		return PTR_ERR(sensor->pwdn);
	}

	sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
	sensor->miscdev.fops = &imx219_fops;
	sensor->miscdev.parent = dev;
	if (client->dev.of_node) {
		u32 csi_id;
		if (of_property_read_u32(client->dev.of_node, "csi-id",
					 &csi_id) == 0) {
			sensor->miscdev.name = devm_kasprintf(
				dev, GFP_KERNEL, "imx219-%u", csi_id);
			dev_info(dev, "imx219-test: imx219-%u \n", csi_id);
		} else {
			sensor->miscdev.name = devm_kasprintf(
				dev, GFP_KERNEL, "imx219-%02x", client->addr);
			dev_info(dev, "imx219-test: imx219-%02x \n",
				 client->addr);
		}
	} else {
		sensor->miscdev.name = devm_kasprintf(
			dev, GFP_KERNEL, "imx219-%02x", client->addr);
	}

	ret = misc_register(&sensor->miscdev);
	if (ret) {
		dev_err(dev,
			"imx219-test: failed to register misc device: %d\n",
			ret);
		return ret;
	}

	global_imx219 = sensor;
	dev_info(dev, "imx219-test: probe successful, ioctl device /dev/%s\n",
		 sensor->miscdev.name);
	return 0;
}

static void imx219_remove(struct i2c_client *client)
{
	struct imx219 *sensor = i2c_get_clientdata(client);

	dev_info(&client->dev, "imx219-test: remove\n");
	if (global_imx219 == sensor)
		global_imx219 = NULL;

	misc_deregister(&sensor->miscdev);
	imx219_power_off(sensor);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id imx219_of_match[] = {
	{ .compatible = "sony,imx219" },
	{}
};
MODULE_DEVICE_TABLE(of, imx219_of_match);

static struct i2c_driver imx219_driver = {
	.driver = {
		.name		= "imx219-simple",
		.of_match_table	= of_match_ptr(imx219_of_match),
	},
	.probe		= imx219_probe,
	.remove		= imx219_remove,
};

module_i2c_driver(imx219_driver)

MODULE_DESCRIPTION("Simplified I2C driver for IMX219");
MODULE_LICENSE("GPL v2");
