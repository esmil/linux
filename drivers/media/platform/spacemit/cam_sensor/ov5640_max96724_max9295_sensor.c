// SPDX-License-Identifier: GPL-2.0
/*
 * ov5640_max96724_max9295_sensor.c - OV5640 GMSL sensor driver
 *
 * Copyright (C) 2026 Spacemit Ltd.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define OV5640_IOC_MAGIC			'V'
#define OV5640_IOCTL_POWER_ON			_IO(OV5640_IOC_MAGIC, 1)
#define OV5640_IOCTL_POWER_OFF			_IO(OV5640_IOC_MAGIC, 2)
#define OV5640_IOCTL_INIT_REGS			_IO(OV5640_IOC_MAGIC, 3)
#define OV5640_IOCTL_STREAM_ON			_IO(OV5640_IOC_MAGIC, 4)
#define OV5640_IOCTL_STREAM_OFF			_IO(OV5640_IOC_MAGIC, 5)
#define OV5640_IOCTL_DETECT			_IO(OV5640_IOC_MAGIC, 6)
#define OV5640_IOCTL_SETUP_GMSL			_IO(OV5640_IOC_MAGIC, 7)

#define MAX96724_DEF_ADDR		0x27
#define MAX9295_BASE_ADDR		0x41
#define MAX9295_BROADCAST_ADDR		0x40

#define MAX96724_PHY_CLK		(0x20 | 12)
#define MAX96724_TX11_PIPE_X_EN_ADDR	0x090b
#define MAX96724_TX45_PIPE_X_DST_CTRL_ADDR	0x092d
#define MAX96724_PIPE_X_SRC_0_MAP_ADDR	0x090d
#define MAX9295_GPIO_FRAME_TRIGGER	0x02d3
#define OV5640_XCLK_FREQ		24000000

struct regval_list {
	u16 addr;
	u8 data;
	u8 mask;
	u16 delay_ms;
};

struct ov5640_gmsl {
	struct i2c_client *client;
	struct mutex lock;
	struct gpio_desc *pwdn;
	struct gpio_desc *reset;
	struct gpio_desc *i2c_mux;
	struct clk *xclk;
	struct regulator *vdd;
	struct miscdevice miscdev;
	u8 des_addr;
	u8 ser_base_addr;
	u8 link_lock_status;
	bool gmsl_inited;
	bool powered;
	bool streaming;
};

static struct ov5640_gmsl *global_ov5640_gmsl;

static const struct regval_list ov5640_init_setting[] = {
	{0x3103, 0x11, 0, 0},
	{0x3008, 0x82, 0, 10},
	{0x3008, 0x42, 0, 0},
	{0x3103, 0x03, 0, 0},
	{0x3017, 0x00, 0, 0},
	{0x3018, 0x00, 0, 0},
	{0x3034, 0x18, 0, 0},
	{0x3035, 0x11, 0, 0},
	{0x3036, 0x54, 0, 0},
	{0x3037, 0x13, 0, 0},
	{0x3108, 0x01, 0, 0},
	{0x3630, 0x36, 0, 0},
	{0x3631, 0x0e, 0, 0},
	{0x3632, 0xe2, 0, 0},
	{0x3633, 0x12, 0, 0},
	{0x3621, 0xe0, 0, 0},
	{0x3704, 0xa0, 0, 0},
	{0x3703, 0x5a, 0, 0},
	{0x3715, 0x78, 0, 0},
	{0x3717, 0x01, 0, 0},
	{0x370b, 0x60, 0, 0},
	{0x3705, 0x1a, 0, 0},
	{0x3905, 0x02, 0, 0},
	{0x3906, 0x10, 0, 0},
	{0x3901, 0x0a, 0, 0},
	{0x3731, 0x12, 0, 0},
	{0x3600, 0x08, 0, 0},
	{0x3601, 0x33, 0, 0},
	{0x302d, 0x60, 0, 0},
	{0x3620, 0x52, 0, 0},
	{0x371b, 0x20, 0, 0},
	{0x471c, 0x50, 0, 0},
	{0x3a13, 0x43, 0, 0},
	{0x3a18, 0x00, 0, 0},
	{0x3a19, 0xf8, 0, 0},
	{0x3635, 0x13, 0, 0},
	{0x3636, 0x03, 0, 0},
	{0x3634, 0x40, 0, 0},
	{0x3622, 0x01, 0, 0},
	{0x3c01, 0x34, 0, 0},
	{0x3c04, 0x28, 0, 0},
	{0x3c05, 0x98, 0, 0},
	{0x3c06, 0x00, 0, 0},
	{0x3c07, 0x07, 0, 0},
	{0x3c08, 0x00, 0, 0},
	{0x3c09, 0x1c, 0, 0},
	{0x3c0a, 0x9c, 0, 0},
	{0x3c0b, 0x40, 0, 0},
	{0x3820, 0x40, 0, 0},
	{0x3821, 0x06, 0, 0},
	{0x3814, 0x11, 0, 0},
	{0x3815, 0x11, 0, 0},
	{0x3800, 0x01, 0, 0},
	{0x3801, 0x50, 0, 0},
	{0x3802, 0x01, 0, 0},
	{0x3803, 0xb2, 0, 0},
	{0x3804, 0x08, 0, 0},
	{0x3805, 0xef, 0, 0},
	{0x3806, 0x05, 0, 0},
	{0x3807, 0xf1, 0, 0},
	{0x3808, 0x07, 0, 0},
	{0x3809, 0x80, 0, 0},
	{0x380a, 0x04, 0, 0},
	{0x380b, 0x38, 0, 0},
	{0x380c, 0x09, 0, 0},
	{0x380d, 0xc4, 0, 0},
	{0x380e, 0x04, 0, 0},
	{0x380f, 0x60, 0, 0},
	{0x3810, 0x00, 0, 0},
	{0x3811, 0x10, 0, 0},
	{0x3812, 0x00, 0, 0},
	{0x3813, 0x04, 0, 0},
	{0x3618, 0x04, 0, 0},
	{0x3612, 0x2b, 0, 0},
	{0x3708, 0x64, 0, 0},
	{0x3709, 0x12, 0, 0},
	{0x370c, 0x00, 0, 0},
	{0x3a02, 0x04, 0, 0},
	{0x3a03, 0x60, 0, 0},
	{0x3a08, 0x01, 0, 0},
	{0x3a09, 0x50, 0, 0},
	{0x3a0a, 0x01, 0, 0},
	{0x3a0b, 0x18, 0, 0},
	{0x3a0e, 0x03, 0, 0},
	{0x3a0d, 0x04, 0, 0},
	{0x3a14, 0x04, 0, 0},
	{0x3a15, 0x60, 0, 0},
	{0x4001, 0x02, 0, 0},
	{0x4004, 0x06, 0, 0},
	{0x3000, 0x00, 0, 0},
	{0x3002, 0x1c, 0, 0},
	{0x3004, 0xff, 0, 0},
	{0x3006, 0xc3, 0, 0},
	{0x300e, 0x45, 0, 0},
	{0x302e, 0x08, 0, 0},
	{0x4300, 0x32, 0, 0},
	{0x501f, 0x00, 0, 0},
	{0x4713, 0x02, 0, 0},
	{0x4407, 0x04, 0, 0},
	{0x440e, 0x00, 0, 0},
	{0x460b, 0x37, 0, 0},
	{0x460c, 0x20, 0, 0},
	{0x4837, 0x0a, 0, 0},
	{0x3824, 0x04, 0, 0},
	{0x5000, 0xa7, 0, 0},
	{0x5001, 0x83, 0, 0},
	{0x5180, 0xff, 0, 0},
	{0x5181, 0xf2, 0, 0},
	{0x5182, 0x00, 0, 0},
	{0x5183, 0x14, 0, 0},
	{0x5184, 0x25, 0, 0},
	{0x5185, 0x24, 0, 0},
	{0x5186, 0x09, 0, 0},
	{0x5187, 0x09, 0, 0},
	{0x5188, 0x09, 0, 0},
	{0x5189, 0x75, 0, 0},
	{0x518a, 0x54, 0, 0},
	{0x518b, 0xe0, 0, 0},
	{0x518c, 0xb2, 0, 0},
	{0x518d, 0x42, 0, 0},
	{0x518e, 0x3d, 0, 0},
	{0x518f, 0x56, 0, 0},
	{0x5190, 0x46, 0, 0},
	{0x5191, 0xf8, 0, 0},
	{0x5192, 0x04, 0, 0},
	{0x5193, 0x70, 0, 0},
	{0x5194, 0xf0, 0, 0},
	{0x5195, 0xf0, 0, 0},
	{0x5196, 0x03, 0, 0},
	{0x5197, 0x01, 0, 0},
	{0x5198, 0x04, 0, 0},
	{0x5199, 0x12, 0, 0},
	{0x519a, 0x04, 0, 0},
	{0x519b, 0x00, 0, 0},
	{0x519c, 0x06, 0, 0},
	{0x519d, 0x82, 0, 0},
	{0x519e, 0x38, 0, 0},
	{0x5381, 0x1e, 0, 0},
	{0x5382, 0x5b, 0, 0},
	{0x5383, 0x08, 0, 0},
	{0x5384, 0x0a, 0, 0},
	{0x5385, 0x7e, 0, 0},
	{0x5386, 0x88, 0, 0},
	{0x5387, 0x7c, 0, 0},
	{0x5388, 0x6c, 0, 0},
	{0x5389, 0x10, 0, 0},
	{0x538a, 0x01, 0, 0},
	{0x538b, 0x98, 0, 0},
	{0x5300, 0x08, 0, 0},
	{0x5301, 0x30, 0, 0},
	{0x5302, 0x10, 0, 0},
	{0x5303, 0x00, 0, 0},
	{0x5304, 0x08, 0, 0},
	{0x5305, 0x30, 0, 0},
	{0x5306, 0x08, 0, 0},
	{0x5307, 0x16, 0, 0},
	{0x5309, 0x08, 0, 0},
	{0x530a, 0x30, 0, 0},
	{0x530b, 0x04, 0, 0},
	{0x530c, 0x06, 0, 0},
	{0x5480, 0x01, 0, 0},
	{0x5580, 0x02, 0, 0},
	{0x5583, 0x40, 0, 0},
	{0x5584, 0x10, 0, 0},
	{0x5589, 0x10, 0, 0},
	{0x558a, 0x00, 0, 0},
	{0x558b, 0xf8, 0, 0},
	{0x5800, 0x23, 0, 0},
	{0x5801, 0x14, 0, 0},
	{0x5802, 0x0f, 0, 0},
	{0x5803, 0x0f, 0, 0},
	{0x5804, 0x12, 0, 0},
	{0x5805, 0x26, 0, 0},
	{0x5806, 0x0c, 0, 0},
	{0x5807, 0x08, 0, 0},
	{0x5808, 0x05, 0, 0},
	{0x5809, 0x05, 0, 0},
	{0x580a, 0x08, 0, 0},
	{0x580b, 0x0d, 0, 0},
	{0x580c, 0x08, 0, 0},
	{0x580d, 0x03, 0, 0},
	{0x580e, 0x00, 0, 0},
	{0x580f, 0x00, 0, 0},
	{0x5810, 0x03, 0, 0},
	{0x5811, 0x09, 0, 0},
	{0x5812, 0x07, 0, 0},
	{0x5813, 0x03, 0, 0},
	{0x5814, 0x00, 0, 0},
	{0x5815, 0x01, 0, 0},
	{0x5816, 0x03, 0, 0},
	{0x5817, 0x08, 0, 0},
	{0x5818, 0x0d, 0, 0},
	{0x5819, 0x08, 0, 0},
	{0x581a, 0x05, 0, 0},
	{0x581b, 0x06, 0, 0},
	{0x581c, 0x08, 0, 0},
	{0x581d, 0x0e, 0, 0},
	{0x581e, 0x29, 0, 0},
	{0x581f, 0x17, 0, 0},
	{0x5820, 0x11, 0, 0},
	{0x5821, 0x11, 0, 0},
	{0x5822, 0x15, 0, 0},
	{0x5823, 0x28, 0, 0},
	{0x5824, 0x46, 0, 0},
	{0x5825, 0x26, 0, 0},
	{0x5826, 0x08, 0, 0},
	{0x5827, 0x26, 0, 0},
	{0x5828, 0x64, 0, 0},
	{0x5829, 0x26, 0, 0},
	{0x582a, 0x24, 0, 0},
	{0x582b, 0x22, 0, 0},
	{0x582c, 0x24, 0, 0},
	{0x582d, 0x24, 0, 0},
	{0x582e, 0x06, 0, 0},
	{0x582f, 0x22, 0, 0},
	{0x5830, 0x40, 0, 0},
	{0x5831, 0x42, 0, 0},
	{0x5832, 0x24, 0, 0},
	{0x5833, 0x26, 0, 0},
	{0x5834, 0x24, 0, 0},
	{0x5835, 0x22, 0, 0},
	{0x5836, 0x22, 0, 0},
	{0x5837, 0x26, 0, 0},
	{0x5838, 0x44, 0, 0},
	{0x5839, 0x24, 0, 0},
	{0x583a, 0x26, 0, 0},
	{0x583b, 0x28, 0, 0},
	{0x583c, 0x42, 0, 0},
	{0x583d, 0xce, 0, 0},
	{0x5025, 0x00, 0, 0},
	{0x3a0f, 0x30, 0, 0},
	{0x3a10, 0x28, 0, 0},
	{0x3a1b, 0x30, 0, 0},
	{0x3a1e, 0x26, 0, 0},
	{0x3a11, 0x60, 0, 0},
	{0x3a1f, 0x14, 0, 5},
};

static const struct regval_list ov5640_start_stream[] = {
	{0x3008, 0x02, 0, 10},
};

static const struct regval_list ov5640_stop_stream[] = {
	{0x3008, 0x42, 0, 10},
};

static int ov5640_i2c_write(struct ov5640_gmsl *sensor, u16 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[3];
	int ret;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	buf[2] = val;

	msg.addr = sensor->client->addr;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	ret = i2c_transfer(sensor->client->adapter, &msg, 1);
	if (ret != 1) {
		dev_err(&sensor->client->dev,
			"ov5640-gmsl: i2c write failed slave=0x%02x reg=0x%04x val=0x%02x ret=%d\n",
			sensor->client->addr, reg, val, ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int ov5640_i2c_read(struct ov5640_gmsl *sensor, u16 reg, u8 *val)
{
	struct i2c_msg msgs[2];
	u8 reg_buf[2];
	u8 data_buf[1];
	int ret;

	reg_buf[0] = reg >> 8;
	reg_buf[1] = reg & 0xff;

	msgs[0].addr = sensor->client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(reg_buf);
	msgs[0].buf = reg_buf;

	msgs[1].addr = sensor->client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = sizeof(data_buf);
	msgs[1].buf = data_buf;

	ret = i2c_transfer(sensor->client->adapter, msgs, 2);
	if (ret != 2) {
		dev_err(&sensor->client->dev,
			"ov5640-gmsl: i2c read failed slave=0x%02x reg=0x%04x ret=%d\n",
			sensor->client->addr, reg, ret);
		return ret < 0 ? ret : -EIO;
	}

	*val = data_buf[0];
	return 0;
}

static int ov5640_i2c_update_bits(struct ov5640_gmsl *sensor, u16 reg,
				  u8 mask, u8 val)
{
	u8 tmp;
	int ret;

	ret = ov5640_i2c_read(sensor, reg, &tmp);
	if (ret)
		return ret;

	tmp = (tmp & ~mask) | (val & mask);
	return ov5640_i2c_write(sensor, reg, tmp);
}

static int ov5640_write_array(struct ov5640_gmsl *sensor,
			      const struct regval_list *regs, int num)
{
	int i;
	int ret;

	for (i = 0; i < num; i++) {
		if (regs[i].mask)
			ret = ov5640_i2c_update_bits(sensor, regs[i].addr,
						 regs[i].mask, regs[i].data);
		else
			ret = ov5640_i2c_write(sensor, regs[i].addr, regs[i].data);
		if (ret)
			return ret;
		if (regs[i].delay_ms)
			msleep(regs[i].delay_ms);
	}

	return 0;
}

static int ov5640_check_des_version(struct ov5640_gmsl *sensor)
{
	static const u16 ver_addrs[2] = { 0x0316, 0x0319 };
	u8 ver = 0;
	u8 tmp;
	int i;
	int ret;

	sensor->client->addr = sensor->des_addr;
	for (i = 0; i < ARRAY_SIZE(ver_addrs); i++) {
		ret = ov5640_i2c_read(sensor, ver_addrs[i], &tmp);
		if (ret)
			return ret;
		ver |= ((tmp & 0x08) >> 3) << i;
	}

	dev_info(&sensor->client->dev,
		 "ov5640-gmsl: %s des version bits=0x%02x\n", __func__, ver);

	return ver == 0x01 ? 0 : -ENODEV;
}

static int ov5640_get_link_lock_status(struct ov5640_gmsl *sensor)
{
	u8 val;
	int ret;

	dev_dbg(&sensor->client->dev, "ov5640-gmsl: enter %s\n", __func__);

	sensor->client->addr = sensor->des_addr;
	sensor->link_lock_status = 0;
	ret = ov5640_i2c_read(sensor, 0x001a, &val);
	if (ret)
		return ret;
	sensor->link_lock_status |= (val & 0x08) >> 3;

	ret = ov5640_i2c_read(sensor, 0x000a, &val);
	if (ret)
		return ret;
	sensor->link_lock_status |= (val & 0x08) >> 2;

	ret = ov5640_i2c_read(sensor, 0x000b, &val);
	if (ret)
		return ret;
	sensor->link_lock_status |= (val & 0x08) >> 1;

	ret = ov5640_i2c_read(sensor, 0x000c, &val);
	if (ret)
		return ret;
	sensor->link_lock_status |= (val & 0x08) >> 0;

	dev_dbg(&sensor->client->dev,
		 "ov5640-gmsl: %s link_lock_status=0x%02x\n",
		 __func__, sensor->link_lock_status);

	return 0;
}

static int ov5640_monopolize_link(struct ov5640_gmsl *sensor, int link)
{
	u8 value;
	int ret;

	sensor->client->addr = sensor->des_addr;
	ret = ov5640_i2c_read(sensor, 0x0006, &value);
	if (ret)
		return ret;

	value = (value & 0xf0) | BIT(link);
	ret = ov5640_i2c_write(sensor, 0x0006, value);
	if (!ret)
		msleep(150);

	if (!ret)
		dev_dbg(&sensor->client->dev,
			 "ov5640-gmsl: monopolize link%d success, reg0x0006=0x%02x\n",
			 link, value);

	return ret;
}

static int ov5640_restore_links(struct ov5640_gmsl *sensor)
{
	int ret;

	dev_dbg(&sensor->client->dev, "ov5640-gmsl: enter %s lock_status=0x%02x\n",
		 __func__, sensor->link_lock_status);

	sensor->client->addr = sensor->des_addr;
	ret = ov5640_i2c_write(sensor, 0x0006,
			      0xf0 | sensor->link_lock_status);
	if (!ret)
		msleep(150);

	if (!ret)
		dev_dbg(&sensor->client->dev,
			 "ov5640-gmsl: restore links success reg0x0006=0x%02x\n",
			 0xf0 | sensor->link_lock_status);

	return ret;
}

static int ov5640_setup_gmsl(struct ov5640_gmsl *sensor)
{
	u16 addrbackup = sensor->client->addr;
	u8 val;
	int i;
	int ret;

	dev_dbg(&sensor->client->dev,
		 "ov5640-gmsl: enter %s des=0x%02x ser_base=0x%02x\n",
		 __func__, sensor->des_addr, sensor->ser_base_addr);

	sensor->client->addr = sensor->des_addr;
	ret = ov5640_i2c_read(sensor, 0x0000, &val);
	if (ret)
		goto out_restore_addr;
	if (val != 0x4e) {
		dev_err(&sensor->client->dev,
			"ov5640-gmsl: max96724 not detected, id=0x%02x\n", val);
		ret = -ENODEV;
		goto out_restore_addr;
	}

	ret = ov5640_check_des_version(sensor);
	if (ret)
		goto out_restore_addr;

	ov5640_i2c_write(sensor, 0x0013, 0x40);
	msleep(100);
	ov5640_i2c_write(sensor, 0x0013, 0x00);

	ov5640_i2c_write(sensor, 0x0005, 0x88);
	ov5640_i2c_write(sensor, 0x0310, 0x90);
	ov5640_i2c_write(sensor, 0x0309, 0x90);

	ret = ov5640_get_link_lock_status(sensor);
	if (ret)
		goto out_restore_addr;
	if (!sensor->link_lock_status) {
		dev_err(&sensor->client->dev, "ov5640-gmsl: no locked GMSL links\n");
		ret = -ENOLINK;
		goto out_restore_addr;
	}

	dev_dbg(&sensor->client->dev,
		 "ov5640-gmsl: detected link lock bitmap=0x%02x\n",
		 sensor->link_lock_status);

	ret = ov5640_i2c_read(sensor, 0x0006, &val);
	if (ret)
		goto out_restore_addr;
	ov5640_i2c_write(sensor, 0x0006, (val & 0xf0) | (sensor->link_lock_status & 0x0f));

	ov5640_i2c_write(sensor, 0x090a, 0xc0);
	ov5640_i2c_write(sensor, 0x094a, 0xc0);
	ov5640_i2c_write(sensor, 0x098a, 0xc0);
	ov5640_i2c_write(sensor, 0x09ca, 0xc0);

	ov5640_i2c_write(sensor, 0x08a3, 0xe4);
	ov5640_i2c_write(sensor, 0x08a4, 0xe4);
	ov5640_i2c_write(sensor, 0x08a0, 0x24);

	ov5640_i2c_write(sensor, 0x0415, MAX96724_PHY_CLK);
	ov5640_i2c_write(sensor, 0x0418, MAX96724_PHY_CLK);
	ov5640_i2c_write(sensor, 0x041b, MAX96724_PHY_CLK);
	ov5640_i2c_write(sensor, 0x041e, MAX96724_PHY_CLK);

	ov5640_i2c_write(sensor, 0x00f0, 0x62);
	ov5640_i2c_write(sensor, 0x00f1, 0xea);
	ov5640_i2c_write(sensor, 0x00f4, 0x00);

	ov5640_i2c_write(sensor, 0x0300, 0x83);
	ov5640_i2c_write(sensor, 0x0301, 0xaa);
	ov5640_i2c_write(sensor, 0x0337, 0x2a);
	ov5640_i2c_write(sensor, 0x036d, 0x2a);
	ov5640_i2c_write(sensor, 0x03a4, 0x2a);

	ov5640_i2c_write(sensor, 0x0027, 0x00);
	ov5640_i2c_write(sensor, 0x0029, 0xf0);

	for (i = 0; i < 4; i++) {
		u8 ser_addr;

		if (!(sensor->link_lock_status & BIT(i)))
			continue;

		dev_dbg(&sensor->client->dev,
			 "ov5640-gmsl: start setup link%d\n", i);

		sensor->client->addr = sensor->des_addr;
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_TX11_PIPE_X_EN_ADDR, 0x1f);
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_TX45_PIPE_X_DST_CTRL_ADDR, 0xaa);
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_TX45_PIPE_X_DST_CTRL_ADDR + 1, 0x02);
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 0, 0x1e);
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 1, 0x1e | (i << 6));
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 2, 0x00);
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 3, 0x00 | (i << 6));
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 4, 0x01);
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 5, 0x01 | (i << 6));
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 6, 0x02);
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 7, 0x02 | (i << 6));
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 8, 0x03);
		ov5640_i2c_write(sensor, (0x40 * i) + MAX96724_PIPE_X_SRC_0_MAP_ADDR + 9, 0x03 | (i << 6));

		ret = ov5640_monopolize_link(sensor, i);
		if (ret)
			goto out_restore_addr;

		sensor->client->addr = MAX9295_BROADCAST_ADDR;
		ov5640_i2c_write(sensor, 0x0000, (sensor->ser_base_addr + i) << 1);
		msleep(75);

		ser_addr = sensor->ser_base_addr + i;
		sensor->client->addr = ser_addr;
		dev_dbg(&sensor->client->dev,
			 "ov5640-gmsl: link%d serializer remapped to 0x%02x\n",
			 i, ser_addr);
		ret = ov5640_i2c_read(sensor, 0x000d, &val);
		if (ret) {
			sensor->link_lock_status &= ~BIT(i);
			dev_err(&sensor->client->dev,
				"ov5640-gmsl: read serializer id failed on link %d addr=0x%02x ret=%d\n",
				i, ser_addr, ret);
			goto restore_links;
		}
		if (val != 0x91) {
			dev_err(&sensor->client->dev,
				"ov5640-gmsl: max9295 on link %d not detected, id=0x%02x\n",
				i, val);
			sensor->link_lock_status &= ~BIT(i);
			ret = -ENODEV;
			goto restore_links;
		}

		dev_dbg(&sensor->client->dev,
			 "ov5640-gmsl: link%d serializer detected, addr=0x%02x id=0x%02x\n",
			 i, ser_addr, val);

		ov5640_i2c_write(sensor, 0x02bf, 0x60);
		ov5640_i2c_write(sensor, 0x02be, 0x10);
		ov5640_i2c_write(sensor, 0x0311, 0x40);
		ov5640_i2c_write(sensor, 0x0308, 0x64);
		ov5640_i2c_write(sensor, 0x0002, 0x43);
		ov5640_i2c_write(sensor, 0x0331, 0x11);
		ov5640_i2c_write(sensor, 0x0334, 0x70);
		ov5640_i2c_write(sensor, 0x0335, 0x07);

		ov5640_i2c_write(sensor, 0x007b, 0x30 + i);
		ov5640_i2c_write(sensor, 0x0083, 0x30 + i);
		ov5640_i2c_write(sensor, 0x0093, 0x30 + i);
		ov5640_i2c_write(sensor, 0x009b, 0x30 + i);
		ov5640_i2c_write(sensor, 0x00a3, 0x30 + i);
		ov5640_i2c_write(sensor, 0x00ab, 0x30 + i);
		ov5640_i2c_write(sensor, 0x008b, 0x30 + i);

		dev_dbg(&sensor->client->dev,
			 "ov5640-gmsl: link%d setup done serializer=0x%02x\n",
			 i, ser_addr);
	}

	restore_links:
	ret = ov5640_restore_links(sensor);
	if (ret)
		goto out_restore_addr;

	if (!sensor->link_lock_status)
		ret = -ENOLINK;
	else
		sensor->gmsl_inited = true;

	out_restore_addr:
	sensor->client->addr = addrbackup;
	if (!ret)
		dev_dbg(&sensor->client->dev, "ov5640-gmsl: %s done\n", __func__);
	return ret;
}

static int __maybe_unused ov5640_sensor_detect(struct ov5640_gmsl *sensor)
{
	u8 hi;
	u8 lo;
	int ret;

	ret = ov5640_i2c_read(sensor, 0x300a, &hi);
	if (ret)
		return ret;
	ret = ov5640_i2c_read(sensor, 0x300b, &lo);
	if (ret)
		return ret;

	if (hi != 0x56 || lo != 0x40) {
		dev_err(&sensor->client->dev,
			"ov5640-gmsl: ov5640 id mismatch %02x%02x\n", hi, lo);
		return -ENODEV;
	}

	dev_dbg(&sensor->client->dev,
		 "ov5640-gmsl: sensor detected id=%02x%02x\n", hi, lo);

	return 0;
}

static int ov5640_power_on(struct ov5640_gmsl *sensor)
{
	int i;
	int ret;

	if (sensor->powered)
		return 0;

	if (sensor->i2c_mux)
		gpiod_set_value_cansleep(sensor->i2c_mux, 1);

	if (sensor->xclk) {
		ret = clk_prepare_enable(sensor->xclk);
		if (ret)
			return ret;
	}

	sensor->vdd = devm_regulator_get(&sensor->client->dev, "vdd");
	if (IS_ERR(sensor->vdd)) {
		ret = PTR_ERR(sensor->vdd);
		goto err_clk;
	}

	ret = regulator_enable(sensor->vdd);
	if (ret)
		goto err_clk;

	ret = regulator_set_voltage(sensor->vdd, 3300000, 3300000);
	if (ret)
		goto err_regulator;

	if (sensor->reset)
		gpiod_set_value_cansleep(sensor->reset, 0);
	if (sensor->pwdn)
		gpiod_set_value_cansleep(sensor->pwdn, 1);
	usleep_range(5000, 10000);
	if (sensor->pwdn)
		gpiod_set_value_cansleep(sensor->pwdn, 0);
	usleep_range(5000, 10000);
	if (sensor->reset)
		gpiod_set_value_cansleep(sensor->reset, 1);
	usleep_range(1000, 2000);
	if (sensor->reset)
		gpiod_set_value_cansleep(sensor->reset, 0);
	usleep_range(20000, 25000);

	for (i = 0; i < 3; i++)
		dev_dbg(&sensor->client->dev, "ov5640-gmsl: power sequence step %d done\n", i);

	sensor->powered = true;
	dev_info(&sensor->client->dev, "ov5640-gmsl: %s done\n", __func__);
	return 0;

err_clk:
	if (sensor->xclk)
		clk_disable_unprepare(sensor->xclk);
	if (sensor->i2c_mux)
		gpiod_set_value_cansleep(sensor->i2c_mux, 0);
	return ret;

err_regulator:
	regulator_disable(sensor->vdd);
	goto err_clk;
}

static void ov5640_power_off(struct ov5640_gmsl *sensor)
{
	if (!sensor->powered)
		return;

	if (sensor->pwdn)
		gpiod_set_value_cansleep(sensor->pwdn, 1);
	if (sensor->reset)
		gpiod_set_value_cansleep(sensor->reset, 0);
	if (sensor->vdd)
		regulator_disable(sensor->vdd);
	if (sensor->xclk)
		clk_disable_unprepare(sensor->xclk);
	if (sensor->i2c_mux)
		gpiod_set_value_cansleep(sensor->i2c_mux, 0);

	sensor->powered = false;
	sensor->streaming = false;
	dev_info(&sensor->client->dev, "ov5640-gmsl: %s done\n", __func__);
}

static int ov5640_write_init_regs(struct ov5640_gmsl *sensor)
{
	int ret;

	dev_dbg(&sensor->client->dev, "ov5640-gmsl: enter %s\n", __func__);

	if (!sensor->gmsl_inited) {
		ret = ov5640_setup_gmsl(sensor);
		if (ret)
			return ret;
	}

	ret = ov5640_write_array(sensor,
				 ov5640_init_setting,
				 ARRAY_SIZE(ov5640_init_setting));
	if (!ret)
		dev_dbg(&sensor->client->dev, "ov5640-gmsl: sensor init registers written\n");

	return ret;
}

static int ov5640_stream_on(struct ov5640_gmsl *sensor)
{
	int i;
	u16 addrbackup = sensor->client->addr;

	dev_dbg(&sensor->client->dev, "ov5640-gmsl: enter %s link_status=0x%02x\n",
		 __func__, sensor->link_lock_status);

	if (sensor->streaming)
		return 0;

	ov5640_write_array(sensor,
				 ov5640_start_stream,
				 ARRAY_SIZE(ov5640_start_stream));

	sensor->client->addr = sensor->des_addr;

	ov5640_i2c_write(sensor, 0x00f4, sensor->link_lock_status);
	ov5640_i2c_write(sensor, 0x0018, sensor->link_lock_status);
	ov5640_i2c_write(sensor, 0x0943, 0x10);
	ov5640_i2c_write(sensor, 0x0943, 0x33);
	ov5640_i2c_write(sensor, 0x0983, 0x10);
	ov5640_i2c_write(sensor, 0x0983, 0x33);

	msleep(100);

	for (i = 0; i < 4; i++) {
		u8 ser_addr;

		if (!(sensor->link_lock_status & BIT(i)))
			continue;

		ser_addr = sensor->ser_base_addr + i;
		sensor->client->addr = ser_addr;
		dev_dbg(&sensor->client->dev,
			 "ov5640-gmsl: stream on trigger link%d serializer=0x%02x\n",
			 i, ser_addr);
		ov5640_i2c_write(sensor, MAX9295_GPIO_FRAME_TRIGGER + 1, 0xa0);
		ov5640_i2c_write(sensor, MAX9295_GPIO_FRAME_TRIGGER, 0x00);
		msleep(50);
		ov5640_i2c_write(sensor, MAX9295_GPIO_FRAME_TRIGGER, 0x10);
		msleep(50);
		ov5640_i2c_write(sensor, MAX9295_GPIO_FRAME_TRIGGER, 0x00);
		msleep(50);
		ov5640_i2c_write(sensor, MAX9295_GPIO_FRAME_TRIGGER, 0x10);
		msleep(50);
		ov5640_i2c_write(sensor, MAX9295_GPIO_FRAME_TRIGGER, 0x04);
		ov5640_i2c_write(sensor, MAX9295_GPIO_FRAME_TRIGGER + 1, 0x6a);
		ov5640_i2c_write(sensor, MAX9295_GPIO_FRAME_TRIGGER + 2, 0x0a);
	}

	sensor->client->addr = addrbackup;

	sensor->streaming = true;
	dev_info(&sensor->client->dev, "ov5640-gmsl: %s done\n", __func__);
	return 0;
}

static int ov5640_stream_off(struct ov5640_gmsl *sensor)
{
	int ret;

	if (!sensor->streaming)
		return 0;

	ret = ov5640_write_array(sensor,
				 ov5640_stop_stream,
				 ARRAY_SIZE(ov5640_stop_stream));
	if (!ret)
		sensor->streaming = false;

	if (!ret)
		dev_info(&sensor->client->dev, "ov5640-gmsl: %s done\n", __func__);

	return ret;
}

static long ov5640_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ov5640_gmsl *sensor = file->private_data;
	long ret = 0;

	if (!sensor)
		return -ENODEV;

	mutex_lock(&sensor->lock);
	switch (cmd) {
	case OV5640_IOCTL_POWER_ON:
		/* ret = ov5640_power_on(sensor); */
		break;
	case OV5640_IOCTL_POWER_OFF:
		ov5640_stream_off(sensor);
		ov5640_power_off(sensor);
		break;
	case OV5640_IOCTL_INIT_REGS:
		/* ret = ov5640_write_init_regs(sensor); */
		break;
	case OV5640_IOCTL_STREAM_ON:
		ret = ov5640_stream_on(sensor);
		break;
	case OV5640_IOCTL_STREAM_OFF:
		ret = ov5640_stream_off(sensor);
		break;
	case OV5640_IOCTL_DETECT:
		/* ret = ov5640_sensor_detect(sensor); */
		break;
	case OV5640_IOCTL_SETUP_GMSL:
		/* ret = ov5640_setup_gmsl(sensor); */
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&sensor->lock);

	return ret;
}

static int ov5640_dev_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct ov5640_gmsl *sensor;

	if (!misc)
		return -ENODEV;

	sensor = container_of(misc, struct ov5640_gmsl, miscdev);
	file->private_data = sensor;

	return 0;
}

static const struct file_operations ov5640_fops = {
	.owner = THIS_MODULE,
	.open = ov5640_dev_open,
	.unlocked_ioctl = ov5640_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ov5640_ioctl,
#endif
};

static int ov5640_probe(struct i2c_client *client)
{
	struct ov5640_gmsl *sensor;
	struct device *dev = &client->dev;
	int ret;
	u32 val;

	dev_dbg(dev, "ov5640-gmsl: enter %s client=0x%02x\n", __func__, client->addr);

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->client = client;
	sensor->des_addr = MAX96724_DEF_ADDR;
	sensor->ser_base_addr = MAX9295_BASE_ADDR;
	mutex_init(&sensor->lock);
	i2c_set_clientdata(client, sensor);

	sensor->pwdn = devm_gpiod_get_optional(dev, "pwdn",
				       GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(sensor->pwdn)) {
		dev_warn(dev, "ov5640-gmsl: failed to get pwdn gpio: %ld\n",
			 PTR_ERR(sensor->pwdn));
		sensor->pwdn = NULL;
	}

	sensor->reset = devm_gpiod_get_optional(dev, "reset",
				        GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(sensor->reset)) {
		dev_warn(dev, "ov5640-gmsl: failed to get reset gpio: %ld\n",
			 PTR_ERR(sensor->reset));
		sensor->reset = NULL;
	}

	sensor->i2c_mux = devm_gpiod_get_optional(dev, "i2c-mux",
				          GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(sensor->i2c_mux)) {
		dev_warn(dev, "ov5640-gmsl: failed to get i2c-mux gpio: %ld\n",
			 PTR_ERR(sensor->i2c_mux));
		sensor->i2c_mux = NULL;
	}

	sensor->xclk = devm_clk_get_optional(dev, "xclk");
	if (IS_ERR(sensor->xclk)) {
		ret = PTR_ERR(sensor->xclk);
		goto err_mutex;
	}

	if (sensor->xclk) {
		ret = clk_set_rate(sensor->xclk, OV5640_XCLK_FREQ);
		if (ret) {
			dev_err(dev, "ov5640-gmsl: failed to set xclk to %u Hz: %d\n",
				OV5640_XCLK_FREQ, ret);
			goto err_mutex;
		}
	}

	if (!of_property_read_u32(dev->of_node, "deserializer-addr", &val))
		sensor->des_addr = val;
	if (!of_property_read_u32(dev->of_node, "serializer-base-addr", &val))
		sensor->ser_base_addr = val;


	ret = ov5640_setup_gmsl(sensor);
	if (ret)
		goto err_power;

	ret = ov5640_power_on(sensor);
	if (ret)
		goto err_mutex;

        ret = ov5640_write_init_regs(sensor);
        if (ret) 
                goto err_power;

	sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
	sensor->miscdev.fops = &ov5640_fops;
	sensor->miscdev.parent = dev;
	if (client->dev.of_node &&
	    of_property_read_u32(client->dev.of_node, "csi-id", &val) == 0)
		sensor->miscdev.name = devm_kasprintf(dev, GFP_KERNEL,
					     "ov5640-gmsl-%u", val);
	else
		sensor->miscdev.name = devm_kasprintf(dev, GFP_KERNEL,
					     "ov5640-gmsl-%02x", client->addr);
	if (!sensor->miscdev.name) {
		ret = -ENOMEM;
		goto err_mutex;
	}

	ret = misc_register(&sensor->miscdev);
	if (ret)
		goto err_mutex;

	global_ov5640_gmsl = sensor;
	dev_info(dev, "ov5640-gmsl: probe ok, /dev/%s\n", sensor->miscdev.name);

        return 0;

err_power:
	ov5640_power_off(sensor);
err_mutex:
	mutex_destroy(&sensor->lock);
	return ret;
}

static void ov5640_remove(struct i2c_client *client)
{
	struct ov5640_gmsl *sensor = i2c_get_clientdata(client);

	if (!sensor)
		return;

	if (global_ov5640_gmsl == sensor)
		global_ov5640_gmsl = NULL;

	misc_deregister(&sensor->miscdev);
	ov5640_stream_off(sensor);
	ov5640_power_off(sensor);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id ov5640_of_match[] = {
	{ .compatible = "spacemit,ov5640-max96724-max9295" },
	{ }
};
MODULE_DEVICE_TABLE(of, ov5640_of_match);

static struct i2c_driver ov5640_driver = {
	.driver = {
		.name = "ov5640-max96724-max9295",
		.of_match_table = of_match_ptr(ov5640_of_match),
	},
	.probe = ov5640_probe,
	.remove = ov5640_remove,
};

module_i2c_driver(ov5640_driver);

MODULE_DESCRIPTION("Simplified OV5640 MAX96724/MAX9295 driver");
MODULE_LICENSE("GPL v2");
