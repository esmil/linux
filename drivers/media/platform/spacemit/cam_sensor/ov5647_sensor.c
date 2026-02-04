// SPDX-License-Identifier: GPL-2.0
/*
 * Simplified I2C driver for OmniVision OV5647
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
#include <linux/gpio.h>

/*
 * Sensor Configuration: 1920x1080 @ 30fps, 2-lane MIPI
 *
 * MCLK:           24MHz
 * Resolution:     1920x1080
 * Bit Depth:      10bit
 * FPS:            30fps
 * HTS:            2416 (0x0970) - registers 0x380c:0x380d
 * VTS:            1104 (0x0450) - registers 0x380e:0x380f
 * PCLK:           81.667MHz
 * MIPI Data Rate: 408.334Mbps/Lane
 * MIPI Clock:     409MHz
 * Htime:          29.59us
 */

/* IOCTL interface for user-space control */
#define OV5647_IOC_MAGIC		'O'
#define OV5647_IOCTL_POWER_ON		_IO(OV5647_IOC_MAGIC, 1)
#define OV5647_IOCTL_POWER_OFF		_IO(OV5647_IOC_MAGIC, 2)
#define OV5647_IOCTL_INIT_REGS		_IO(OV5647_IOC_MAGIC, 3)
#define OV5647_IOCTL_STREAM_ON		_IO(OV5647_IOC_MAGIC, 4)
#define OV5647_IOCTL_STREAM_OFF		_IO(OV5647_IOC_MAGIC, 5)
#define OV5647_IOCTL_DETECT		_IO(OV5647_IOC_MAGIC, 6)
#define OV5647_IOCTL_INIT_REGS_1LANE	_IO(OV5647_IOC_MAGIC, 7)

static struct ov5647 *global_ov5647;

struct regval_list {
	u16 addr;
	u8 data;
};

/* 1080p register (bringup) */
static struct regval_list ov5647_1080p_2lane_regs[] = {
	{0x0100, 0x00},
	{0x0103, 0x01},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x62},
	{0x303c, 0x11},
	{0x3106, 0xf5},
	{0x3821, 0x06},
	{0x3820, 0x00},
	{0x3827, 0xec},
	{0x370c, 0x03},
	{0x3612, 0x5b},
	{0x3618, 0x04},
	{0x5000, 0x06},
	{0x5002, 0x41},
	{0x5003, 0x08},
	{0x5a00, 0x08},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	/* 2x io drive */
	{0x3011, 0x42},
	/* end */
	{0x3016, 0x08},
	{0x3017, 0xe0},
	{0x3018, 0x44},
	{0x301c, 0xf8},
	{0x301d, 0xf0},
	{0x3a18, 0x00},
	{0x3a19, 0xf8},
	{0x3c01, 0x80},
	{0x3b07, 0x0c},
	{0x380c, 0x09},
	{0x380d, 0x70},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3708, 0x64},
	{0x3709, 0x12},
	{0x3808, 0x07},
	{0x3809, 0x80},
	{0x380a, 0x04},
	{0x380b, 0x38},
	{0x3800, 0x01},
	{0x3801, 0x5c},
	{0x3802, 0x01},
	{0x3803, 0xb2},
	{0x3804, 0x08},
	{0x3805, 0xe3},
	{0x3806, 0x05},
	{0x3807, 0xf1},
	{0x3811, 0x04},
	{0x3813, 0x02},
	{0x3630, 0x2e},
	{0x3632, 0xe2},
	{0x3633, 0x23},
	{0x3634, 0x44},
	{0x3636, 0x06},
	{0x3620, 0x64},
	{0x3621, 0xe0},
	{0x3600, 0x37},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x3731, 0x02},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3f05, 0x02},
	{0x3f06, 0x10},
	{0x3f01, 0x0a},
	{0x3a08, 0x01},
	{0x3a09, 0x4b},
	{0x3a0a, 0x01},
	{0x3a0b, 0x13},
	{0x3a0d, 0x04},
	{0x3a0e, 0x03},
	{0x3a0f, 0x58},
	{0x3a10, 0x50},
	{0x3a1b, 0x58},
	{0x3a1e, 0x50},
	{0x3a11, 0x60},
	{0x3a1f, 0x28},
	{0x4001, 0x02},
	{0x4004, 0x04},
	{0x4000, 0x09},
	{0x4837, 0x19},
	{0x4800, 0x34},
	{0x3503, 0x03},
	{0x0100, 0x00},
};

static struct regval_list ov5647_1080p_1lane_regs[] = {
	{0x0100, 0x00},
	{0x0103, 0x01},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x62},
	{0x303c, 0x11},
	{0x3106, 0xf5},
	{0x3821, 0x06},
	{0x3820, 0x00},
	{0x3827, 0xec},
	{0x370c, 0x03},
	{0x3612, 0x5b},
	{0x3618, 0x04},
	{0x5000, 0x06},
	{0x5002, 0x41},
	{0x5003, 0x08},
	{0x5a00, 0x08},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	/* 2x io drive */
	{0x3011, 0x42},
	/* end */
	{0x3016, 0x08},
	{0x3017, 0xe0},
	{0x3018, 0x24},
	{0x301c, 0xf8},
	{0x301d, 0xf0},
	{0x3a18, 0x00},
	{0x3a19, 0xf8},
	{0x3c01, 0x80},
	{0x3b07, 0x0c},
	{0x380c, 0x09},
	{0x380d, 0x70},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3708, 0x64},
	{0x3709, 0x12},
	{0x3808, 0x07},
	{0x3809, 0x80},
	{0x380a, 0x04},
	{0x380b, 0x38},
	{0x3800, 0x01},
	{0x3801, 0x5c},
	{0x3802, 0x01},
	{0x3803, 0xb2},
	{0x3804, 0x08},
	{0x3805, 0xe3},
	{0x3806, 0x05},
	{0x3807, 0xf1},
	{0x3811, 0x04},
	{0x3813, 0x02},
	{0x3630, 0x2e},
	{0x3632, 0xe2},
	{0x3633, 0x23},
	{0x3634, 0x44},
	{0x3636, 0x06},
	{0x3620, 0x64},
	{0x3621, 0xe0},
	{0x3600, 0x37},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x3731, 0x02},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3f05, 0x02},
	{0x3f06, 0x10},
	{0x3f01, 0x0a},
	{0x3a08, 0x01},
	{0x3a09, 0x4b},
	{0x3a0a, 0x01},
	{0x3a0b, 0x13},
	{0x3a0d, 0x04},
	{0x3a0e, 0x03},
	{0x3a0f, 0x58},
	{0x3a10, 0x50},
	{0x3a1b, 0x58},
	{0x3a1e, 0x50},
	{0x3a11, 0x60},
	{0x3a1f, 0x28},
	{0x4001, 0x02},
	{0x4004, 0x04},
	{0x4000, 0x09},
	{0x4837, 0x19},
	{0x4800, 0x34},
	{0x3503, 0x03},
	{0x0100, 0x00},
};

static struct regval_list ov5647_640x480_2lane_10bpp[] __maybe_unused = {
	{0x0100, 0x00},
	{0x0103, 0x01},
	{0x3035, 0x11},
	{0x3036, 0x46},
	{0x303c, 0x11},
	{0x3821, 0x07},
	{0x3820, 0x41},
	{0x370c, 0x03},
	{0x3612, 0x59},
	{0x3618, 0x00},
	{0x5000, 0x06},
	{0x5003, 0x08},
	{0x5a00, 0x08},
	{0x3000, 0xff},
	{0x3001, 0xff},
	{0x3002, 0xff},
	{0x301d, 0xf0},
	{0x3a18, 0x00},
	{0x3a19, 0xf8},
	{0x3c01, 0x80},
	{0x3b07, 0x0c},
	{0x380c, 0x07},
	{0x380d, 0x3c},
	{0x3814, 0x35},
	{0x3815, 0x35},
	{0x3708, 0x64},
	{0x3709, 0x52},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0xe0},
	{0x3800, 0x00},
	{0x3801, 0x10},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x2f},
	{0x3806, 0x07},
	{0x3807, 0x9f},
	{0x3630, 0x2e},
	{0x3632, 0xe2},
	{0x3633, 0x23},
	{0x3634, 0x44},
	{0x3620, 0x64},
	{0x3621, 0xe0},
	{0x3600, 0x37},
	{0x3704, 0xa0},
	{0x3703, 0x5a},
	{0x3715, 0x78},
	{0x3717, 0x01},
	{0x3731, 0x02},
	{0x370b, 0x60},
	{0x3705, 0x1a},
	{0x3f05, 0x02},
	{0x3f06, 0x10},
	{0x3f01, 0x0a},
	{0x3a08, 0x01},
	{0x3a09, 0x2e},
	{0x3a0a, 0x00},
	{0x3a0b, 0xfb},
	{0x3a0d, 0x02},
	{0x3a0e, 0x01},
	{0x3a0f, 0x58},
	{0x3a10, 0x50},
	{0x3a1b, 0x58},
	{0x3a1e, 0x50},
	{0x3a11, 0x60},
	{0x3a1f, 0x28},
	{0x4001, 0x02},
	{0x4004, 0x02},
	{0x4000, 0x09},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3017, 0xe0},
	{0x301c, 0xfc},
	{0x3636, 0x06},
	{0x3016, 0x08},
	{0x3827, 0xec},
	{0x3018, 0x44},
	{0x3035, 0x21},
	{0x3106, 0xf5},
	{0x3034, 0x1a},
	{0x301c, 0xf8},
	{0x4800, 0x34},
	{0x3503, 0x03},
	{0x0100, 0x00},
};

struct ov5647 {
	struct i2c_client *client;
	struct gpio_desc *pwdn;
	struct mutex lock;
	bool power_on;
	struct miscdevice miscdev;
};

static int ov5647_write(struct ov5647 *sensor, u16 reg, u8 val)
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
			"ov5647-test: I2C write failed, reg=0x%x, val=0x%x, ret=%d\n",
			reg, val, ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int ov5647_read(struct ov5647 *sensor, u16 reg, u8 *val)
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
			"ov5647-test: I2C read failed, reg=0x%x, ret=%d\n", reg,
			ret);
		return ret < 0 ? ret : -EIO;
	}

	*val = data_buf[0];
	return 0;
}

static int ov5647_stream_on(struct ov5647 *sensor)
{
	return ov5647_write(sensor, 0x0100, 0x01);
}

static int ov5647_stream_off(struct ov5647 *sensor)
{
	return ov5647_write(sensor, 0x0100, 0x00);
}

static int ov5647_set_virtual_channel(struct ov5647 *sensor, int channel)
{
	u8 channel_id;
	int ret;

	ret = ov5647_read(sensor, 0x4814, &channel_id);
	if (ret < 0)
		return ret;

	channel_id &= ~(3 << 6);

	return ov5647_write(sensor, 0x4814, channel_id | (channel << 6));
}

static int ov5647_write_init_regs(struct ov5647 *sensor, int lane_num);
static int ov5647_power_on(struct ov5647 *sensor);
static void ov5647_power_off(struct ov5647 *sensor);
static int ov5647_detect(struct ov5647 *sensor);
static int ov5647_set_virtual_channel(struct ov5647 *sensor, int channel);

static long ov5647_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ov5647 *sensor = file->private_data;
	int ret = 0;

	if (!sensor)
		return -ENODEV;

	switch (cmd) {
	case OV5647_IOCTL_POWER_ON:
		ret = ov5647_power_on(sensor);
		break;
	case OV5647_IOCTL_POWER_OFF:
		ov5647_power_off(sensor);
		ret = 0;
		break;
	case OV5647_IOCTL_INIT_REGS:
		ret = ov5647_write_init_regs(sensor, 2);
		break;
	case OV5647_IOCTL_INIT_REGS_1LANE:
		ret = ov5647_write_init_regs(sensor, 1);
		break;
	case OV5647_IOCTL_STREAM_ON:
		ret = ov5647_stream_on(sensor);
		break;
	case OV5647_IOCTL_STREAM_OFF:
		ret = ov5647_stream_off(sensor);
		break;
	case OV5647_IOCTL_DETECT:
		ret = ov5647_detect(sensor);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}


static int ov5647_dev_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct ov5647 *sensor;

	if (!misc)
		return -ENODEV;

	sensor = container_of(misc, struct ov5647, miscdev);
	/* replace private_data with sensor pointer for use in ioctl */
	file->private_data = sensor;
	return 0;
}

static const struct file_operations ov5647_fops = {
	.owner = THIS_MODULE,
	.open = ov5647_dev_open,
	.unlocked_ioctl = ov5647_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ov5647_ioctl,
#endif
};

static int ov5647_write_init_regs(struct ov5647 *sensor, int lane_num)
{
	int i, ret;

	if (lane_num == 2) {
		dev_info(&sensor->client->dev,
			 "ov5647-test: write 2lane init regs, total=%ld\n",
			 ARRAY_SIZE(ov5647_1080p_2lane_regs));
		for (i = 0; i < ARRAY_SIZE(ov5647_1080p_2lane_regs); i++) {
			const struct regval_list *reg =
				&ov5647_1080p_2lane_regs[i];

			ret = ov5647_write(sensor, reg->addr, reg->data);
			if (ret < 0) {
				dev_err(&sensor->client->dev,
					"ov5647-test: write 2lane init reg failed, idx=%d, reg=0x%x, val=0x%x, ret=%d\n",
					i, reg->addr, reg->data, ret);
				return ret;
			}
		}
	} else {
		dev_info(&sensor->client->dev,
			 "ov5647-test: write 1lane init regs, total=%ld\n",
			 ARRAY_SIZE(ov5647_1080p_1lane_regs));
		for (i = 0; i < ARRAY_SIZE(ov5647_1080p_1lane_regs); i++) {
			const struct regval_list *reg = &ov5647_1080p_1lane_regs[i];

			ret = ov5647_write(sensor, reg->addr, reg->data);
			if (ret < 0) {
				dev_err(&sensor->client->dev,
					"ov5647-test: write 1lane init reg failed, idx=%d, reg=0x%x, val=0x%x, ret=%d\n",
					i, reg->addr, reg->data, ret);
				return ret;
			}
		}
	}

	ret = ov5647_set_virtual_channel(sensor, 0);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"ov5647-test: set virtual channel failed, ret=%d\n",
			ret);
		return ret;
	}

	dev_info(&sensor->client->dev, "ov5647-test: init regs written\n");
	return 0;
}

static int ov5647_detect(struct ov5647 *sensor)
{
	u8 hi, lo;
	int ret;

	dev_info(&sensor->client->dev, "ov5647-test: detect start\n");

	/* Read sensor ID: registers 0x300a and 0x300b (as in ov5647.c) */
	ret = ov5647_read(sensor, 0x300a, &hi);
	if (ret < 0)
		return ret;
	if (hi != 0x56) {
		dev_err(&sensor->client->dev,
			"ov5647-test: ID high mismatch: 0x%x\n", hi);
		return -ENODEV;
	}
	ret = ov5647_read(sensor, 0x300b, &lo);
	if (ret < 0)
		return ret;
	if (lo != 0x47) {
		dev_err(&sensor->client->dev,
			"ov5647-test: ID low mismatch: 0x%x\n", lo);
		return -ENODEV;
	}

	dev_info(&sensor->client->dev,
		 "ov5647-test: detected OV5647 (id %02x%02x)\n", hi, lo);
	return 0;
}

static int ov5647_power_on(struct ov5647 *sensor)
{
	dev_info(&sensor->client->dev,
		 "ov5647-test: power_on enter, power_on=%d\n",
		 sensor->power_on);
	if (sensor->power_on) {
		dev_info(&sensor->client->dev,
			 "ov5647-test: already powered on\n");
		return 0;
	}

	if (sensor->pwdn) {
		dev_info(&sensor->client->dev, "ov5647-test: drive PWDN low\n");
		gpiod_set_value_cansleep(sensor->pwdn, 0);
	}

	usleep_range(20000, 21000); /* OV5647 needs 20ms after PWDN goes low */

	if (sensor->pwdn) {
		dev_info(&sensor->client->dev,
			 "ov5647-test: drive PWDN high\n");
		gpiod_set_value_cansleep(sensor->pwdn, 1);
	}

	usleep_range(20000, 21000);  /* OV5647 needs another 20ms after RESETB goes high */

	sensor->power_on = true;
	dev_info(&sensor->client->dev, "ov5647-test: power_on done\n");

	return 0;
}

static void ov5647_power_off(struct ov5647 *sensor)
{
	dev_info(&sensor->client->dev, "ov5647-test: power_off enter\n");
	if (!sensor->power_on) {
		dev_info(&sensor->client->dev,
			 "ov5647-test: already powered off\n");
		return;
	}

	if (sensor->pwdn) {
		dev_info(&sensor->client->dev, "ov5647-test: drive PWDN low\n");
		gpiod_set_value_cansleep(sensor->pwdn, 0);
	}

	sensor->power_on = false;
	dev_info(&sensor->client->dev, "ov5647-test: power_off done\n");
}

static int ov5647_probe(struct i2c_client *client)
{
	struct ov5647 *sensor;
	struct device *dev = &client->dev;
	int ret;

	dev_info(dev, "ov5647-test: probe enter, client addr=0x%x\n",
		 client->addr);

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->client = client;
	mutex_init(&sensor->lock);
	i2c_set_clientdata(client, sensor);

	dev_info(dev, "ov5647-test: get PWDN gpio\n");
	sensor->pwdn = devm_gpiod_get_optional(dev, "pwdn", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->pwdn)) {
		dev_err(dev, "ov5647-test: Failed to get PWDN GPIO\n");
		return PTR_ERR(sensor->pwdn);
	}

	sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
	sensor->miscdev.fops = &ov5647_fops;
	sensor->miscdev.parent = dev;
	if (client->dev.of_node) {
		u32 csi_id;
		if (of_property_read_u32(client->dev.of_node, "csi-id",
					 &csi_id) == 0) {
			sensor->miscdev.name = devm_kasprintf(
				dev, GFP_KERNEL, "ov5647-%u", csi_id);
			dev_info(dev, "ov5647-test: ov5647-%u \n", csi_id);
		} else {
			sensor->miscdev.name = devm_kasprintf(
				dev, GFP_KERNEL, "ov5647-%02x", client->addr);
			dev_info(dev, "ov5647-test: ov5647-%02x \n",
				 client->addr);
		}
	} else {
		sensor->miscdev.name = devm_kasprintf(
			dev, GFP_KERNEL, "ov5647-%02x", client->addr);
	}

	ret = misc_register(&sensor->miscdev);
	if (ret) {
		dev_err(dev,
			"ov5647-test: failed to register misc device: %d\n",
			ret);
		return ret;
	}

	global_ov5647 = sensor;
	dev_info(dev, "ov5647-test: probe successful, ioctl device /dev/%s\n",
		 sensor->miscdev.name);
	return 0;
}

static void ov5647_remove(struct i2c_client *client)
{
	struct ov5647 *sensor = i2c_get_clientdata(client);

	dev_info(&client->dev, "ov5647-test: remove\n");
	if (global_ov5647 == sensor)
		global_ov5647 = NULL;

	misc_deregister(&sensor->miscdev);
	ov5647_power_off(sensor);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id ov5647_of_match[] = {
	{ .compatible = "ovti,ov5647" },
	{}
};
MODULE_DEVICE_TABLE(of, ov5647_of_match);

static struct i2c_driver ov5647_driver = {
	.driver = {
		.name		= "ov5647-simple",
		.of_match_table	= of_match_ptr(ov5647_of_match),
	},
	.probe		= ov5647_probe,
	.remove		= ov5647_remove,
};

module_i2c_driver(ov5647_driver)

MODULE_DESCRIPTION("Simplified I2C driver for OV5647");
MODULE_LICENSE("GPL v2");
