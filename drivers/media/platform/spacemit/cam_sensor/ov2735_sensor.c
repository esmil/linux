// SPDX-License-Identifier: GPL-2.0
/*
 * Simplified I2C driver for OmniVision OV2735
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
 * Sensor Configuration: 1920x1080 @ 30fps, 1-lane MIPI
 *
 * MCLK:           24MHz
 * Resolution:     1920x1080
 * Bit Depth:      10bit
 * FPS:            30fps
 * HTS:            1053 - registers P1:0x8C 0x8D
 * VTS:            1329 - registers P1:0x4E 0x4F
 * MIPI Data Rate: 105Mbps/Lane
 */

/* IOCTL interface for user-space control */
#define OV2735_IOC_MAGIC		'I'
#define OV2735_IOCTL_POWER_ON		_IO(OV2735_IOC_MAGIC, 1)
#define OV2735_IOCTL_POWER_OFF		_IO(OV2735_IOC_MAGIC, 2)
#define OV2735_IOCTL_INIT_REGS		_IO(OV2735_IOC_MAGIC, 3)
#define OV2735_IOCTL_STREAM_ON		_IO(OV2735_IOC_MAGIC, 4)
#define OV2735_IOCTL_STREAM_OFF		_IO(OV2735_IOC_MAGIC, 5)
#define OV2735_IOCTL_DETECT		_IO(OV2735_IOC_MAGIC, 6)

static struct ov2735 *global_ov2735;

struct ov2735 {
	struct i2c_client *client;
	struct gpio_desc *pwdn;
	struct clk *mclk;
	struct mutex lock;
	struct regulator *vdd;
	struct miscdevice miscdev;
};

struct regval_list {
	u8 addr;
	u8 data;
};
struct regval_list ov2735_spm_1920x1080_10bit_30fps_1lane_tab[] = {
	{0xfd, 0x00}, /* page flag */
	/* {0x20, 0x00}, soft reset */
	/* {0xfd, 0x00}, page flag */
	{0x2f, 0x10}, /* PLL_CTRL_BUF */
	{0x34, 0x00},
	{0x30, 0x15},
	{0x33, 0x01},
	{0x35, 0x20},

	{0xfd, 0x01},
	{0x0d, 0x00},
	{0x30, 0x00},
	{0x03, 0x01},
	{0x04, 0x8f},
	{0x01, 0x01},
	{0x09, 0x00},
	{0x0a, 0x20},
	{0x06, 0x0a},
	{0x24, 0x10},
	{0x01, 0x01},
	{0xfb, 0x73},
	{0x01, 0x01},

	{0xfd, 0x01},
	{0x1a, 0x6b},
	{0x1c, 0xea},
	{0x16, 0x0c},
	{0x21, 0x00},
	{0x11, 0xe8}, /* RST_NUM_8LSB dac */
	{0x19, 0xc3},
	{0x26, 0xda}, /* ANALOG CTRL4 reference voltage */
	{0x29, 0x01},
	{0x33, 0x6f},
	{0x2a, 0xd2},
	{0x2c, 0x40},
	{0xd0, 0x02},
	{0xd1, 0x01},
	{0xd2, 0x20},
	{0xd3, 0x03}, /* B4_NUM_3MSB */
	{0xd4, 0xa4}, /* B4_NUM_8LSB */
	{0x50, 0x00},
	{0x51, 0x2c},
	{0x52, 0x29},
	{0x53, 0x00},
	{0x55, 0x44},
	{0x58, 0x29},
	{0x5a, 0x00},
	{0x5b, 0x00},
	{0x5d, 0x00},
	{0x64, 0x2f},
	{0x66, 0x62},
	{0x68, 0x5b},
	{0x75, 0x46},
	{0x76, 0xf0}, /* P34 Cycle for Pixel Timing */
	{0x77, 0x4f},
	{0x78, 0xef},
	{0x72, 0xcf},
	{0x73, 0x36},
	{0x7d, 0x0d},
	{0x7e, 0x0d},
	{0x8a, 0x77},
	{0x8b, 0x77},

	{0xfd, 0x01},
	{0xb1, 0x83},
	{0xb3, 0x0b},
	{0xb4, 0x14},
	{0x9d, 0x40},
	{0xa1, 0x05},
	{0x94, 0x44},
	{0x95, 0x33},
	{0x96, 0x1f},
	{0x98, 0x45},
	{0x9c, 0x10},
	{0xb5, 0x70},
	/* {0xa0, 0x01}, MIPI EN BUF */
	{0x25, 0xe0},
	{0x20, 0x7b},
	{0x8f, 0x88},
	{0x91, 0x40},

	{0xfd, 0x02}, /* PAGE FLAG */
	{0x5e, 0x03},
	{0xa1, 0x04},
	{0xa3, 0x40},
	{0xa5, 0x02},
	{0xa7, 0xc4},

	{0xfd, 0x01},
	{0x86, 0x77},
	{0x89, 0x77},
	{0x87, 0x74},
	{0x88, 0x74},
	{0xfc, 0xe0},
	{0xfe, 0xe0},
	{0xf0, 0x40},
	{0xf1, 0x40},
	{0xf2, 0x40},
	{0xf3, 0x40},
	{0xb2, 0x00}, /* MIPI CTRL4 single lane mipi LP mode */

	/* crop to 1920x1080 */
	{0xfd, 0x02},
	{0xa0, 0x00},
	{0xa1, 0x08},
	{0xa2, 0x04},
	{0xa3, 0x38}, /* image vertical size 1088 */
	{0xa4, 0x00},
	{0xa5, 0x04},
	{0xa6, 0x03},
	{0xa7, 0xc0}, /* image half horizontal size 964 */

	{0xfd, 0x01},
	{0x8e, 0x07},
	{0x8f, 0x80},
	{0x90, 0x04},
	{0x91, 0x38},

	{0xfd, 0x03},
	{0xc0, 0x01},
	{0xfd, 0x04},
	{0x21, 0x14},
	{0x22, 0x14},
	{0x23, 0x14},

	{0xfd, 0x01},
	{0x06, 0xe0},
	{0x01, 0x01},
	/* {0xa0, 0x01}, MIPI enable, stream on */
};

static int ov2735_write(struct ov2735 *sensor, u8 reg, u8 val)
{
	struct i2c_adapter *adapter = sensor->client->adapter;
	struct i2c_msg msg;
	u8 data[2];
	int ret;

	data[0] = reg;
	data[1] = val;

	msg.addr = sensor->client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	mutex_lock(&sensor->lock);
	ret = i2c_transfer(adapter, &msg, 1);
	mutex_unlock(&sensor->lock);

	if (ret != 1) {
		dev_err(&sensor->client->dev,
			"ov2735-test: I2C write failed, reg=0x%x, val=0x%x, ret=%d\n",
			reg, val, ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int ov2735_read(struct ov2735 *sensor, u8 reg, u8 *val)
{
	struct i2c_adapter *adapter = sensor->client->adapter;
	struct i2c_msg msgs[2];
	u8 reg_buf[1];
	u8 data_buf[1];
	int ret;

	reg_buf[0] = reg;

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
			"ov2735-test: I2C read failed, reg=0x%x, ret=%d\n", reg,
			ret);
		return ret < 0 ? ret : -EIO;
	}

	*val = data_buf[0];
	return 0;
}

static int ov2735_stream_on(struct ov2735 *sensor)
{
	int ret;

	ret = ov2735_write(sensor, 0xfd, 0x01); /* page 1 */
	if (ret < 0)
		return ret;
	return ov2735_write(sensor, 0xa0, 0x01); /* MIPI enable, stream on */
}

static int ov2735_stream_off(struct ov2735 *sensor)
{
	int ret;

	ret = ov2735_write(sensor, 0xfd, 0x01); /* page 1 */
	if (ret < 0)
		return ret;
	return ov2735_write(sensor, 0xa0, 0x00); /* MIPI disable, stream off */
}

static int ov2735_write_init_regs(struct ov2735 *sensor);
static int ov2735_power_on(struct ov2735 *sensor);
static void ov2735_power_off(struct ov2735 *sensor);
static int ov2735_detect(struct ov2735 *sensor);

static long ov2735_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ov2735 *sensor = file->private_data;
	int ret = 0;

	if (!sensor)
		return -ENODEV;

	switch (cmd) {
	case OV2735_IOCTL_POWER_ON:
		ret = ov2735_power_on(sensor);
		break;
	case OV2735_IOCTL_POWER_OFF:
		ov2735_power_off(sensor);
		ret = 0;
		break;
	case OV2735_IOCTL_INIT_REGS:
		ret = ov2735_write_init_regs(sensor);
		break;
	case OV2735_IOCTL_STREAM_ON:
		ret = ov2735_stream_on(sensor);
		break;
	case OV2735_IOCTL_STREAM_OFF:
		ret = ov2735_stream_off(sensor);
		break;
	case OV2735_IOCTL_DETECT:
		ret = ov2735_detect(sensor);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int ov2735_dev_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct ov2735 *sensor;

	if (!misc)
		return -ENODEV;

	sensor = container_of(misc, struct ov2735, miscdev);
	/* Replace private_data with sensor pointer for use in ioctl */
	file->private_data = sensor;
	return 0;
}

static const struct file_operations ov2735_fops = {
	.owner = THIS_MODULE,
	.open = ov2735_dev_open,
	.unlocked_ioctl = ov2735_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ov2735_ioctl,
#endif
};

static int ov2735_write_init_regs(struct ov2735 *sensor)
{
	int i, ret;

	dev_info(&sensor->client->dev,
		 "ov2735-test: write init regs, total=%zu\n",
		 ARRAY_SIZE(ov2735_spm_1920x1080_10bit_30fps_1lane_tab));
	for (i = 0; i < ARRAY_SIZE(ov2735_spm_1920x1080_10bit_30fps_1lane_tab); i++) {
		const struct regval_list *reg = &ov2735_spm_1920x1080_10bit_30fps_1lane_tab[i];

		ret = ov2735_write(sensor, reg->addr, reg->data);
		if (ret < 0) {
			dev_err(&sensor->client->dev,
				"ov2735-test: write init reg failed, idx=%d, reg=0x%x, val=0x%x, ret=%d\n",
				i, reg->addr, reg->data, ret);
			return ret;
		}
	}

	dev_info(&sensor->client->dev, "ov2735-test: init regs written\n");
	return 0;
}

static int ov2735_detect(struct ov2735 *sensor)
{
	u8 hi, lo;
	int ret;

	dev_info(&sensor->client->dev, "ov2735-test: detect start\n");
	ret = ov2735_write(sensor, 0xfd, 0x00);
	if (ret < 0)
		return ret;
	ret = ov2735_read(sensor, 0x02, &hi);
	if (ret < 0)
		return ret;
	if (hi != 0x27) {
		dev_err(&sensor->client->dev,
			"ov2735-test: ID high mismatch: 0x%x\n", hi);
		return -ENODEV;
	}
	ret = ov2735_read(sensor, 0x03, &lo);
	if (ret < 0)
		return ret;
	if (lo != 0x35) {
		dev_err(&sensor->client->dev,
			"ov2735-test: ID low mismatch: 0x%x\n", lo);
		return -ENODEV;
	}

	dev_info(&sensor->client->dev,
		 "ov2735-test: detected OV2735 (id %02x%02x)\n", hi, lo);
	return 0;
}

static int ov2735_power_on(struct ov2735 *sensor)
{
	int ret;

	sensor->mclk = devm_clk_get(&sensor->client->dev, "cam_mclk");
	if (IS_ERR(sensor->mclk)) {
		dev_err(&sensor->client->dev,
			"ov2735-test: Failed to get cam_mclk clock: %ld\n",
			PTR_ERR(sensor->mclk));
		return PTR_ERR(sensor->mclk);
	}

	/* Set cam_mclk frequency to 24MHz */
	ret = clk_set_rate(sensor->mclk, 24000000);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"ov2735-test: Failed to set cam_mclk rate to 24MHz: %d\n",
			ret);
		return ret;
	}

	/* Enable cam_mclk clock */
	ret = clk_prepare_enable(sensor->mclk);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"ov2735-test: Failed to enable cam_mclk: %d\n", ret);
		return ret;
	}
	dev_info(&sensor->client->dev,
		 "ov2735-test: cam_mclk enabled, rate=%lu Hz\n",
		 clk_get_rate(sensor->mclk));

	dev_info(&sensor->client->dev, "ov2735-test: power_on enter\n");

	dev_info(&sensor->client->dev, "ov2735-test: get vdd regulator\n");
	sensor->vdd = devm_regulator_get(&sensor->client->dev, "vdd");
	if (IS_ERR(sensor->vdd)) {
		dev_err(&sensor->client->dev, "ov2735-test: Failed to get vdd regulator: %ld\n",
			PTR_ERR(sensor->vdd));
		return PTR_ERR(sensor->vdd);
	}

	if (sensor->vdd) {
		ret = regulator_enable(sensor->vdd);
		if (ret < 0) {
			dev_err(&sensor->client->dev,
				"ov2735-test: failed to enable vdd: %d\n", ret);
			return ret;
		}
		dev_info(&sensor->client->dev, "ov2735-test: enable vdd\n");

		ret = regulator_set_voltage(sensor->vdd, 3300000, 3300000);
		if (ret < 0) {
			dev_err(&sensor->client->dev, "ov2735-test: failed to set vdd voltage: %d\n",
				ret);
			return ret;
		}
		dev_info(&sensor->client->dev, "ov2735-test: vdd set to 3.3V\n");
	}

	if (sensor->pwdn) {
		dev_info(&sensor->client->dev, "ov2735-test: drive PWDN low\n");
		gpiod_set_value_cansleep(sensor->pwdn, 0);
	}

	usleep_range(10000, 12000);

	if (sensor->pwdn) {
		dev_info(&sensor->client->dev,
			 "ov2735-test: drive PWDN high\n");
		gpiod_set_value_cansleep(sensor->pwdn, 1);
	}

	usleep_range(30000, 31000);

	dev_info(&sensor->client->dev, "ov2735-test: power_on done\n");

	return 0;
}

static void ov2735_power_off(struct ov2735 *sensor)
{
	dev_info(&sensor->client->dev, "ov2735-test: power_off enter\n");

	if (sensor->pwdn) {
		dev_info(&sensor->client->dev, "ov2735-test: drive PWDN low\n");
		gpiod_set_value_cansleep(sensor->pwdn, 0);
	}

	if (sensor->vdd) {
		dev_info(&sensor->client->dev, "ov2735-test: disable vdd\n");
		regulator_disable(sensor->vdd);
	}

	dev_info(&sensor->client->dev, "ov2735-test: power_off done\n");
}

static int ov2735_probe(struct i2c_client *client)
{
	struct ov2735 *sensor;
	struct device *dev = &client->dev;
	int ret;

	dev_info(dev, "ov2735-test: probe enter, client addr=0x%x\n",
		 client->addr);

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->client = client;
	mutex_init(&sensor->lock);
	i2c_set_clientdata(client, sensor);

	dev_info(dev, "ov2735-test: get PWDN gpio\n");
	sensor->pwdn = devm_gpiod_get_optional(
		dev, "pwdn", GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(sensor->pwdn)) {
		dev_err(dev, "ov2735-test: Failed to get PWDN GPIO\n");
		goto err_pwdn;
	}

	/* Power on and detect sensor before creating device node */
	ret = ov2735_power_on(sensor);
	if (ret) {
		dev_err(dev, "ov2735-test: power on failed: %d\n", ret);
		goto err_power_on;
	}

	ret = ov2735_detect(sensor);
	if (ret) {
		dev_err(dev, "ov2735-test: sensor detect failed: %d\n", ret);
		goto err_detect;
	}

	ov2735_power_off(sensor);

	sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
	sensor->miscdev.fops = &ov2735_fops;
	sensor->miscdev.parent = dev;
	if (client->dev.of_node) {
		u32 csi_id;
		if (of_property_read_u32(client->dev.of_node, "csi-id",
					 &csi_id) == 0) {
			sensor->miscdev.name = devm_kasprintf(
				dev, GFP_KERNEL, "ov2735-%u", csi_id);
			dev_info(dev, "ov2735-test: ov2735-%u \n", csi_id);
		} else {
			sensor->miscdev.name = devm_kasprintf(
				dev, GFP_KERNEL, "ov2735-%02x", client->addr);
			dev_info(dev, "ov2735-test: ov2735-%02x \n",
				 client->addr);
		}
	} else {
		sensor->miscdev.name = devm_kasprintf(
			dev, GFP_KERNEL, "ov2735-%02x", client->addr);
	}

	ret = misc_register(&sensor->miscdev);
	if (ret) {
		dev_err(dev,
			"ov2735-test: failed to register misc device: %d\n",
			ret);
		goto err_misc_register;
	}

	global_ov2735 = sensor;
	dev_info(dev, "ov2735-test: probe successful, ioctl device /dev/%s\n",
		 sensor->miscdev.name);
	return 0;

err_misc_register:
	ov2735_power_off(sensor);
	mutex_destroy(&sensor->lock);
	return ret;

err_detect:
	ov2735_power_off(sensor);
err_power_on:
err_pwdn:
	mutex_destroy(&sensor->lock);
	return ret;
}

static void ov2735_remove(struct i2c_client *client)
{
	struct ov2735 *sensor = i2c_get_clientdata(client);

	dev_info(&client->dev, "ov2735-test: remove\n");
	if (global_ov2735 == sensor)
		global_ov2735 = NULL;

	misc_deregister(&sensor->miscdev);
	ov2735_power_off(sensor);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id ov2735_of_match[] = {
	{ .compatible = "ovti,ov2735" },
	{}
};
MODULE_DEVICE_TABLE(of, ov2735_of_match);

static struct i2c_driver ov2735_driver = {
	.driver = {
		.name		= "ov2735-simple",
		.of_match_table	= of_match_ptr(ov2735_of_match),
	},
	.probe		= ov2735_probe,
	.remove		= ov2735_remove,
};

module_i2c_driver(ov2735_driver)

MODULE_DESCRIPTION("Simplified I2C driver for OV2735");
MODULE_LICENSE("GPL v2");
