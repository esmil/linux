/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SPACEMIT Camera Verification System - SENSOR Module
 *
 * Copyright (C) 2021 SPACEMIT Micro Limited
 * All Rights Reserved.
 */
/* #define DEBUG */

#include <linux/atomic.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/timekeeping.h>
#include <linux/platform_device.h>

#include <media/vericam/vcam_sensor_uapi.h>
#include "pwrctrl.h"
#include "vcam_sensor.h"

/* Simple example of how to receive command line parameters to your module.
   Delete if you don't need them */
static unsigned myint = 0xdeadbaad;
static char *mystr = "sensor";

module_param(myint, int, S_IRUGO);
module_param(mystr, charp, S_IRUGO);

//#define CONFIG_TIME_CALC_DEBUG

static struct vcam_sensor_device *g_sdev[VCAM_SNS_MAX_DEV_NUM];
static int vcamsnr_major;
//static struct cdev vcamsnr_cdev;
static struct class *vcamsnr_class;

static DEFINE_MUTEX(cmd_mutex_dev1);
static DEFINE_MUTEX(cmd_mutex_dev2);

/*********************************************************************************/
#define SENSOR_MCLK_CLK_RATE 24000000
static int vcam_sensor_clock_enable(struct vcam_sensor_device *msnr_dev,
				    u32 mclk_rate, u32 en)
{
	int ret = 0;

#ifndef CONFIG_ARCH_SPACEMIT
	pr_info("sensor%d: needn't %s clock on current platform\n", msnr_dev->id, en ? "enable" : "disable");
	return ret;
#endif

	if (IS_ERR_OR_NULL(msnr_dev->mclk))
		return -EINVAL;

	if (en) {
		if (!mclk_rate)
			mclk_rate = 24000000;
		ret = clk_set_rate(msnr_dev->mclk, mclk_rate);
		if (ret)
			return ret;
		ret = clk_prepare_enable(msnr_dev->mclk);
	} else {
		clk_disable_unprepare(msnr_dev->mclk);
	}

	return ret;
}

static int vcam_sensor_power_set(struct vcam_sensor_device *msnr_dev, u32 on)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(msnr_dev->gpio_pwdn) && IS_ERR_OR_NULL(msnr_dev->gpio_rst))
		return -EINVAL;

	if (on) {
		/* pwdn-gpios */
		if (!IS_ERR_OR_NULL(msnr_dev->gpio_pwdn))
			gpiod_set_value_cansleep(msnr_dev->gpio_pwdn, 1);

		/* rst-gpios */
		if (!IS_ERR_OR_NULL(msnr_dev->gpio_rst)) {
			gpiod_set_value_cansleep(msnr_dev->gpio_rst, 0);
			usleep_range(5 * 1000, 5 * 1000);
			gpiod_set_value_cansleep(msnr_dev->gpio_rst, 1);
			usleep_range(10 * 1000, 10 * 1000);
		}

		pr_info("sensor%d: unreset\n", msnr_dev->id);
	} else {
		/* rst-gpios */
		if (!IS_ERR_OR_NULL(msnr_dev->gpio_rst))
			gpiod_set_value_cansleep(msnr_dev->gpio_rst, 0);

		/* pwdn-gpios */
		if (!IS_ERR_OR_NULL(msnr_dev->gpio_pwdn))
			gpiod_set_value_cansleep(msnr_dev->gpio_pwdn, 0);

		pr_info("sensor%d: reset\n", msnr_dev->id);
	}

	return ret;
}

static int vcamsnr_reset_sensor(unsigned long arg)
{
	int ret = 0;
	sns_rst_source_t sns_reset_source;
	struct vcam_sensor_device *msnr_dev;

	if (copy_from_user((void *)&sns_reset_source, (void *)arg,
			   sizeof(sns_reset_source))) {
		pr_err("Failed to copy args from user\n");
		return -EFAULT;
	}

	if (sns_reset_source >= VCAM_SNS_MAX_DEV_NUM) {
		pr_err("Invalid snr reset source %d\n", sns_reset_source);
		return -EINVAL;
	}

	msnr_dev = g_sdev[sns_reset_source];
	if (IS_ERR_OR_NULL(msnr_dev))
		return -ENODEV;

	ret = vcam_sensor_power_set(msnr_dev, 0);
	if (ret) {
		pr_err("snr power disable fail\n");
		return ret;
	}

	ret = vcam_sensor_clock_enable(msnr_dev, 0, 0);

	return ret;
}

static int vcamsnr_unreset_sensor(unsigned long arg)
{
	int ret = 0;
	sns_rst_source_t sns_reset_source;
	struct vcam_sensor_device *msnr_dev;

	if (copy_from_user((void *)&sns_reset_source, (void *)arg,
			   sizeof(sns_reset_source))) {
		pr_err("Failed to copy args from user\n");
		return -EFAULT;
	}

	if (sns_reset_source >= VCAM_SNS_MAX_DEV_NUM) {
		pr_err("Invalid snr reset source %d\n", sns_reset_source);
		return -EINVAL;
	}

	msnr_dev = g_sdev[sns_reset_source];
	if (IS_ERR_OR_NULL(msnr_dev))
		return -ENODEV;

	ret = vcam_sensor_clock_enable(msnr_dev, SENSOR_MCLK_CLK_RATE, 1);
	if (ret) {
		pr_err("snr clock enable fail\n");
		return ret;
	}

	ret = vcam_sensor_power_set(msnr_dev, 1);

	return ret;
}

static int vcam_sensor_write(struct vcam_cmd_i2c_data *data)
{
	struct i2c_adapter *adapter;
	struct i2c_msg msg;
	u8 val[4];
	int ret = 0;
	struct mutex *pcmd_mutex = NULL;
	u8 twsi_no, addr;
	u16 reg_len, val_len, reg, reg_val;

	if (!data || !data->addr || !data->reg_len || !data->val_len) {
		pr_err("Error: %s, %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	twsi_no = data->twsi_no;
	addr = data->addr;
	reg_len = data->reg_len;
	val_len = data->val_len;
	reg = data->tab.reg;
	reg_val = data->tab.val;

	adapter = i2c_get_adapter(twsi_no);
	if (!adapter)
		return -ENODEV;

	if (twsi_no == 0)
		pcmd_mutex = &cmd_mutex_dev1;
	else
		pcmd_mutex = &cmd_mutex_dev2;

	msg.addr = addr;
	msg.flags = 0;
	msg.len = 1;
	msg.buf = val;

	msg.len = reg_len + val_len;

	mutex_lock(pcmd_mutex);
	if (msg.len == 2) {
		/* reg:8bit; val:8bit */
		val[0] = reg & 0xff;
		val[1] = reg_val & 0xff;
	} else if (msg.len == 3) {
		/* reg:16bit; val:8bit */
		val[0] = (reg >> 8) & 0xff;
		val[1] = reg & 0xff;
		val[2] = reg_val & 0xff;
	} else if (msg.len == 4) {
		/* reg:16bit; val:16bit */
		val[0] = (reg >> 8) & 0xff;
		val[1] = reg & 0xff;
		val[2] = (reg_val >> 8) & 0xff;
		val[3] = reg_val & 0xff;
	}
	ret = i2c_transfer(adapter, &msg, 1);
	if (ret < 0) {
		mutex_unlock(pcmd_mutex);
		return ret;
	}
	mutex_unlock(pcmd_mutex);

	return 0;
}

static int vcam_sensor_read(struct vcam_cmd_i2c_data *data)
{
	int ret;
	u8 val[4];
	struct i2c_adapter *adapter;
	struct i2c_msg msg;
	struct mutex *pcmd_mutex = NULL;
	u8 twsi_no, addr;
	u16 reg_len, val_len, reg, reg_val = 0;

	if (!data || !data->addr || !data->reg_len || !data->val_len) {
		pr_err("Error: %s, %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	twsi_no = data->twsi_no;
	addr = data->addr;
	reg_len = data->reg_len;
	val_len = data->val_len;
	reg = data->tab.reg;

	adapter = i2c_get_adapter(twsi_no);
	if (!adapter)
		return -ENODEV;

	if (twsi_no == 0)
		pcmd_mutex = &cmd_mutex_dev1;
	else
		pcmd_mutex = &cmd_mutex_dev2;

	msg.addr = addr;
	msg.flags = 0;
	msg.buf = val;

	mutex_lock(pcmd_mutex);
	if (reg_len == I2C_8BIT) {
		val[0] = reg & 0xff;
	} else if (reg_len == I2C_16BIT) {
		val[0] = (reg >> 8) & 0xff;
		val[1] = reg & 0xff;
	}
	msg.len = reg_len;
	ret = i2c_transfer(adapter, &msg, 1);
	if (ret < 0) {
		mutex_unlock(pcmd_mutex);
		goto err;
	}

	msg.len = val_len;
	msg.flags = I2C_M_RD;
	ret = i2c_transfer(adapter, &msg, 1);
	if (ret < 0) {
		mutex_unlock(pcmd_mutex);
		goto err;
	}
	if (val_len == I2C_8BIT)
		reg_val = val[0];
	else if (val_len == I2C_16BIT)
		reg_val = (val[0] << 8) + val[1];
	mutex_unlock(pcmd_mutex);

	data->tab.val = reg_val;

	return 0;

err:
	pr_debug("Failed reading register 0x%x!\n", reg);
	return ret;
}

static DEFINE_MUTEX(cmd_mutex);
static int twsi_write_i2c(struct vcam_twsi_data *data)
{
	int ret = 0;
	struct i2c_adapter *adapter;
	struct i2c_msg msg;
	u8 val[8];
	int i, j = 0;

	if (!data || !data->addr || !data->reg_len || !data->val_len) {
		pr_err("Error: %s, %d", __func__, __LINE__);
		return -EINVAL;
	}

	msg.addr = data->addr;
	msg.flags = 0;
	msg.len = data->reg_len + data->val_len;
	msg.buf = val;

	adapter = i2c_get_adapter(data->twsi_no);
	if (!adapter)
		return -1;

	mutex_lock(&cmd_mutex);
	for (i = 0; i < data->reg_len; i++)
		val[j++] = ((u8 *)(&data->reg))[i];
	for (i = 0; i < data->val_len; i++)
		val[j++] = ((u8 *)(&data->val))[i];
	ret = i2c_transfer(adapter, &msg, 1);
	if (ret < 0) {
		mutex_unlock(&cmd_mutex);
		return ret;
	}
	mutex_unlock(&cmd_mutex);

	return ret;
}

static int twsi_read_i2c(struct vcam_twsi_data *data)
{
	struct i2c_adapter *adapter;
	struct i2c_msg msg;
	int ret = 0;
	u8 val[4];

	if (!data || !data->addr || !data->reg_len || !data->val_len) {
		pr_err("%s, error param", __func__);
		return -EINVAL;
	}

	msg.addr = data->addr;
	msg.flags = 0;
	msg.len = data->reg_len;
	msg.buf = val;

	adapter = i2c_get_adapter(data->twsi_no);
	if (!adapter) {
		pr_err("Failed to get i2c adapter for twsi_no %d", data->twsi_no);
		return -1;
	}

	mutex_lock(&cmd_mutex);
	if (data->reg_len == I2C_8BIT) {
		val[0] = data->reg & 0xff;
	} else if (data->reg_len == I2C_16BIT) {
		val[0] = (data->reg >> 8) & 0xff;
		val[1] = data->reg & 0xff;
	}
	msg.len = data->reg_len;
	ret = i2c_transfer(adapter, &msg, 1);
	if (ret < 0) {
		pr_err("line%d i2c_transfer Failed! ret=%d, twsi_no=%d, addr=0x%x, reg_len=%d, val_len=%d",
		       __LINE__, ret, data->twsi_no, data->addr, data->reg_len,
		       data->val_len);
		mutex_unlock(&cmd_mutex);
		goto err;
	}

	msg.flags = I2C_M_RD;
	msg.len = data->val_len;
	ret = i2c_transfer(adapter, &msg, 1);
	if (ret < 0) {
		pr_err("line%d i2c_transfer Failed! ret=%d, twsi_no=%d, addr=0x%x, reg_len=%d, val_len=%d",
		       __LINE__, ret, data->twsi_no, data->addr, data->reg_len,
		       data->val_len);
		mutex_unlock(&cmd_mutex);
		goto err;
	}

	if (data->val_len == I2C_8BIT)
		data->val = val[0];
	else if (data->val_len == I2C_16BIT)
		data->val = (val[0] << 8) + val[1];
	else if (data->val_len == I2C_32BIT)
		data->val = (val[3] << 24) + (val[2] << 16) + (val[1] << 8) +
			    val[0];
	//pr_info("twsi_read_i2c: val[0]=0x%x,val[1]=0x%x,val[2]=0x%x,val[3]=0x%x\n",val[0],val[1],val[2],val[3]);
	mutex_unlock(&cmd_mutex);

	return 0;

err:
	pr_info("Failed reading register 0x%x!", data->reg);
	return ret;
}

static int sensor_seq_clock_set(struct vcam_sensor_device *msnr_dev,
				u32 mclk_rate)
{
	int rc;

	if (IS_ERR_OR_NULL(msnr_dev->mclk))
		return -ENOENT;

	if (mclk_rate) {
		rc = clk_set_rate(msnr_dev->mclk, mclk_rate);
		if (rc) {
			pr_err("%s: snr%d mclk=%d failed", __func__,
			       msnr_dev->id, mclk_rate);
			return rc;
		}
		rc = clk_prepare_enable(msnr_dev->mclk);
	} else {
		clk_disable_unprepare(msnr_dev->mclk);
		rc = 0;
	}

	return rc;
}

static int sensor_seq_vreg_set(struct vcam_sensor_device *msnr_dev,
				enum sensor_vreg_type_t vreg_type, u32 vreg_volt)
{
	struct regulator *regulator;
	int rc;

	switch (vreg_type) {
	case SENSOR_VREG_AVDD:
		regulator = msnr_dev->supply_avdd;
		break;
	case SENSOR_VREG_DOVDD:
		regulator = msnr_dev->supply_dovdd;
		break;
	case SENSOR_VREG_DVDD:
		regulator = msnr_dev->supply_dvdd;
		break;
	case SENSOR_VREG_AFVDD:
		regulator = msnr_dev->supply_afvdd;
		break;
	default:
		pr_err("invalid regulator type %d\n", vreg_type);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(regulator))
		return -ENOENT;

	if (vreg_volt) {
		rc = regulator_set_voltage(regulator, vreg_volt, vreg_volt);
		if (rc) {
			pr_err("%s: snr%d, vreg=%d, vlot=%d failed", __func__,
			       msnr_dev->id, vreg_type, vreg_volt);
			return rc;
		}

		rc = regulator_enable(regulator);
		if (rc)
			pr_err("%s: snr%d enable failed", __func__,
			       msnr_dev->id);
	} else {
		rc = regulator_disable(regulator);
		if (rc)
			pr_err("%s: snr%d disable failed", __func__,
			       msnr_dev->id);
	}

    return rc;
}

static int sensor_seq_gpio_set(struct vcam_sensor_device *msnr_dev,
			       enum sensor_gpio_type_t gpio_type, u32 gpio_lvl)
{
	struct gpio_desc *gpio;
	int rc;

	switch (gpio_type) {
	case SENSOR_GPIO_RESET:
		gpio = msnr_dev->gpio_rst;
		break;
	case SENSOR_GPIO_PWDN:
		gpio = msnr_dev->gpio_pwdn;
		break;
//#ifdef CONFIG_ARCH_SPACEMIT
	case SENSOR_GPIO_AVDD:
		gpio = msnr_dev->gpio_avdd;
		break;
	case SENSOR_GPIO_DVDD:
		gpio = msnr_dev->gpio_dvdd;
		break;
	case SENSOR_GPIO_AFVDD:
		gpio = msnr_dev->gpio_afvdd;
		break;
//#else
	case SENSOR_GPIO_DPTC:
		gpio = msnr_dev->gpio_dptc;
		break;
//#endif
	default:
		pr_err("invalid gpio type %d\n", gpio_type);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(gpio))
		return -ENOENT;

	if (gpio_lvl) {
		rc = gpiod_direction_output(gpio, 1);
		if (rc)
			pr_err("%s: snr%d gpio type%d up failed", __func__,
			       msnr_dev->id, gpio_type);
	} else {
		rc = gpiod_direction_output(gpio, 0);
		if (rc)
			pr_err("%s: snr%d gpio type%d down failed", __func__,
			       msnr_dev->id, gpio_type);
	}

	return rc;
}

static int vcam_sensor_config_power(struct vcam_sensor_device *msnr_dev,
				    struct sensor_power_setting *power)
{
	int rc;

	switch (power->seq_type) {
	case SENSOR_SEQ_CLK:
		rc = sensor_seq_clock_set(msnr_dev, power->config_val);
		break;
	case SENSOR_SEQ_VREG:
		rc = sensor_seq_vreg_set(msnr_dev, power->seq_val,
					 power->config_val);
		break;
	case SENSOR_SEQ_GPIO:
		rc = sensor_seq_gpio_set(msnr_dev, power->seq_val,
					 power->config_val);
		break;
	default:
		pr_err("invalid power sequence type %d\n", power->seq_type);
		rc = -EINVAL;
	}

	return rc;
}

static long vcamsnr_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct vcam_sensor_device *msnr_dev;
	int ret;

	if (_IOC_TYPE(cmd) != VCAM_SENSOR_IOC_MAGIC)
		return -ENOTTY;

	msnr_dev = (struct vcam_sensor_device *)file->private_data;

	switch (cmd) {
	case VCAM_SENSOR_RESET:
		ret = vcamsnr_reset_sensor(arg);
		break;
	case VCAM_SENSOR_UNRESET:
		ret = vcamsnr_unreset_sensor(arg);
		break;
	case VCAM_SENSOR_I2C_WRITE: {
		struct vcam_cmd_i2c_data data;
		if (copy_from_user((void *)&data, (void *)arg, sizeof(data)))
			return -EFAULT;

		ret = vcam_sensor_write(&data); 
	} break;
	case VCAM_SENSOR_I2C_READ: {
		struct vcam_cmd_i2c_data data;
		if (copy_from_user((void *)&data, (void *)arg, sizeof(data))) {
			pr_err("Failed to copy args from user\n");
			return -EFAULT;
		}

		ret = vcam_sensor_read(&data);
		if (copy_to_user((void *)arg, (void *)&data, sizeof(data))) {
			pr_err("Failed to copy args to user\n");
			return -EFAULT;
		}
	} break;
	case VCAM_SENSOR_PHY_I2C_WRITE: {
		struct vcam_twsi_data data;
		if (copy_from_user((void *)&data, (void *)arg, sizeof(data)))
			return -EFAULT;

		ret = twsi_write_i2c(&data);
	} break;
	case VCAM_SENSOR_PHY_I2C_READ: {
		struct vcam_twsi_data data;
		if (copy_from_user((void *)&data, (void *)arg, sizeof(data))) {
			pr_err("Failed to copy args from user\n");
			return -EFAULT;
		}

		ret = twsi_read_i2c(&data);
		if (copy_to_user((void *)arg, (void *)&data, sizeof(data))) {
			pr_err("Failed to copy args to user\n");
			return -EFAULT;
		}
	} break;
	case VCAM_SENSOR_POWER_CFG: {
		struct sensor_power_setting data;
		if (copy_from_user((void *)&data, (void *)arg, sizeof(data))) {
			pr_err("Failed to copy args from user\n");
			return -EFAULT;
		}
		ret = vcam_sensor_config_power(msnr_dev, &data);
	} break;
	default:
		pr_err("unknown IOCTL code 0x%x\n", cmd);
		ret = -ENOTTY;
	}

	pr_debug("%s IN, cmd %x\n", __func__, cmd);

	return ret;
}

static int vcamsnr_open(struct inode *inode, struct file *file)
{
	struct vcam_sensor_device *msnr_dev =
		container_of(inode->i_cdev, struct vcam_sensor_device, cdev);
	file->private_data = msnr_dev;
	/* pr_info("%s IN", __func__); */

	return 0;
}

static int vcamsnr_release(struct inode *inode, struct file *file)
{
	struct vcam_sensor_device *msnr_dev =
		container_of(inode->i_cdev, struct vcam_sensor_device, cdev);

	if (!msnr_dev) {
		vcam_sensor_power_set(msnr_dev, 0);
		pr_info("%s sensor%d", __func__, msnr_dev->id);
	}

	return 0;
}

static const struct file_operations vcamsnr_fops = {
	.owner = THIS_MODULE,
	.open = vcamsnr_open,
	.release = vcamsnr_release,
	.unlocked_ioctl = vcamsnr_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vcamsnr_ioctl,
#endif
};

static void vcam_snr_drv_deinit(void)
{
	dev_t dev_id = MKDEV(vcamsnr_major, 0);

	class_destroy(vcamsnr_class);
	unregister_chrdev_region(dev_id, VCAM_SNS_MAX_DEV_NUM);
	vcamsnr_class = NULL;
}

static int vcam_snr_drv_init(void)
{
	int ret = 0;
	dev_t dev_id;

	ret = alloc_chrdev_region(&dev_id, 0, VCAM_SNS_MAX_DEV_NUM,
				  SNR_DRV_NAME);
	if (ret) {
		pr_err("can't get major number\n");
		goto out;
	}

	vcamsnr_major = MAJOR(dev_id);

	vcamsnr_class = class_create(SNR_DRV_NAME);
	if (IS_ERR(vcamsnr_class)) {
		ret = PTR_ERR(vcamsnr_class);
		goto error_cdev;
	}

out:
	return ret;

error_cdev:
	unregister_chrdev_region(dev_id, VCAM_SNS_MAX_DEV_NUM);
	return ret;
}

static void vcam_snr_dev_destroy(struct cdev *cdev, int index)
{
        if(!cdev) {
            pr_err("parameter cdev is NULL\n");
            return;
        }
	device_destroy(vcamsnr_class, MKDEV(vcamsnr_major, index));
	cdev_del(cdev);
}
static int vcam_snr_dev_create(struct cdev *cdev, int index)
{
	int ret = 0;

	if (!cdev) {
		pr_err("parameter cdev is NULL\n");
		return -1;
	}

	cdev_init(cdev, &vcamsnr_fops);
	ret = cdev_add(cdev, MKDEV(vcamsnr_major, index), 1);
	if (ret < 0) {
		pr_err("add device %d cdev fail\n", index);
		return -1;
	}

	/* create device node */
	device_create(vcamsnr_class, NULL, MKDEV(vcamsnr_major, index), NULL,
		      "%s%d", SNR_DRV_NAME, index);

	return ret;
}

static int spacemit_snr_of_parse(struct vcam_sensor_device *sensor)
{
	struct device *dev = &sensor->pdev->dev;
	struct device_node *of_node = dev->of_node;
	char mclk_name[32];
	u32 cell_id;
	u32 dphy_entries;
	int ret;
	u32 i2c_addr;

	ret = of_property_read_u32(of_node, "reg", &i2c_addr); 
	if (ret) {
		pr_err("failed to get I2C address\n");
		return ret;
	}

	/* cell-index */
	ret = of_property_read_u32(of_node, "cell-index", &cell_id);
	if (ret < 0) {
		pr_err("cell-index read failed\n");
		return ret;
	}

	if (cell_id >= VCAM_SNS_MAX_DEV_NUM) {
		pr_err("invaid cell-index %d\n", cell_id);
		return -EINVAL;
	}

	sensor->id = cell_id;
	if (g_sdev[cell_id]) {
		pr_err("cell-index %d already exists\n", cell_id);
		return -EINVAL;
	}

	/* mclks */
#ifdef CONFIG_ARCH_SPACEMIT
	snprintf(mclk_name, sizeof(mclk_name), "cam_mclk%d", cell_id);
	sensor->mclk = devm_clk_get(dev, mclk_name);
	if (IS_ERR_OR_NULL(sensor->mclk)) {
		dev_info(dev, "unable to get cam_mclk%d\n", cell_id);
		ret = PTR_ERR(sensor->mclk);
		goto st_err;
	}
#endif

	/* gpios */
	sensor->gpio_pwdn = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->gpio_pwdn)) {
		dev_info(dev, "no pwdn gpio\n");
		sensor->gpio_pwdn = NULL;
	} else {
		ret = gpiod_direction_output(sensor->gpio_pwdn, 0);
		if (ret < 0) {
			pr_err("Failed to init sensor%d pwdn gpio\n", cell_id);
			goto st_err;
		}
	}

	sensor->gpio_rst = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->gpio_rst)) {
		dev_info(dev, "no reset gpio\n");
		sensor->gpio_rst = NULL;
	} else {
		ret = gpiod_direction_output(sensor->gpio_rst, 0);
		if (ret < 0) {
			pr_err("Failed to init sensor%d reset gpio\n", cell_id);
			goto st_err;
		}
	}

#ifdef CONFIG_ARCH_SPACEMIT
	/* afvdd28-gpios */
	sensor->gpio_afvdd = devm_gpiod_get(dev, "afvdd28", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->gpio_afvdd)) {
	    pr_err("unable to parse sensor%d afvdd28 gpio\n", cell_id);
	    ret = PTR_ERR(sensor->gpio_afvdd);
	} else {
		ret = gpiod_direction_output(sensor->gpio_afvdd, 1);
		if (ret < 0) {
			pr_err("Failed to init sensor%d afvdd28 gpio\n",
			       cell_id);
			goto st_err;
		}
	}

	sensor->gpio_avdd = devm_gpiod_get(dev, "avdd28", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->gpio_avdd)) {
		dev_info(dev, "unable to parse sensor%d avdd28 gpio\n", cell_id);
		sensor->gpio_avdd = NULL;
	} else {
		ret = gpiod_direction_output(sensor->gpio_avdd, 1);
		if (ret < 0) {
			pr_err("Failed to init sensor%d avdd28 gpio\n",
			       cell_id);
			goto st_err;
		}
	}

	sensor->gpio_dvdd = devm_gpiod_get(dev, "dvdd12", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->gpio_dvdd)) {
		dev_info(dev, "unable to parse sensor%d dvdd12 gpio\n", cell_id);
		sensor->gpio_dvdd = NULL;
	} else {
		ret = gpiod_direction_output(sensor->gpio_dvdd, 1);
		if (ret < 0) {
			pr_err("Failed to init sensor%d dvdd12 gpio\n",
			       cell_id);
			goto st_err;
		}
	}
#endif

	sensor->gpio_dptc = devm_gpiod_get(dev, "dptc", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->gpio_dptc)) {
		dev_info(dev, "no dptc gpio\n");
		sensor->gpio_dptc = NULL;
	} else {
		ret = gpiod_direction_output(sensor->gpio_dptc, 1);
		if (ret < 0) {
			pr_err("Failed to init sensor%d dptc gpio\n", cell_id);
			goto st_err;
		}
		gpiod_set_value_cansleep(sensor->gpio_dptc, 1);
		usleep_range(100 * 1000, 100 * 1000);
		gpiod_set_value_cansleep(sensor->gpio_dptc, 0);
		usleep_range(100 * 1000, 100 * 1000);
		gpiod_set_value_cansleep(sensor->gpio_dptc, 1);
		usleep_range(100 * 1000, 100 * 1000);
	}


	/* regulators */
#ifdef CONFIG_ARCH_SPACEMIT
	sensor->supply_afvdd = devm_regulator_get(dev, "af_2v8");
	if (IS_ERR(sensor->supply_afvdd)) {
		dev_info(dev, "no regulator af_2v8\n");
		sensor->supply_afvdd = NULL;
	}

	sensor->supply_avdd = devm_regulator_get(dev, "avdd_2v8");
	if (IS_ERR(sensor->supply_avdd)) {
		dev_info(dev, "no regulator avdd_2v8\n");
		sensor->supply_avdd = NULL;
	}

	sensor->supply_dovdd = devm_regulator_get(dev, "dovdd_1v8");
	if (IS_ERR(sensor->supply_dovdd)) {
		dev_info(dev, "no regulator dovdd_1v8\n");
		sensor->supply_dovdd = NULL;
	}

	sensor->supply_dvdd = devm_regulator_get(dev, "dvdd_1v2");
	if (IS_ERR(sensor->supply_dvdd)) {
		dev_info(dev, "no regulator dvdd_1v2\n");
		sensor->supply_dvdd = NULL;
	}
#endif

	/* dphy-settings */
	ret = of_property_read_u32(of_node, "dphy-entries", &dphy_entries);
	if (ret < 0 || !dphy_entries || dphy_entries > 5) {
		sensor->dphy[0] = 0x00000001;
		sensor->dphy[1] = 0xa2848888;
		sensor->dphy[2] = 0x00001500;
		sensor->dphy[3] = 0x000000ff;
		sensor->dphy[4] = 0x1001;
		pr_info("no dphy entries found, set to default");
	} else {
		ret = of_property_read_u32_array(of_node, "dphy-settings",
						 sensor->dphy, dphy_entries);
		if (ret < 0) {
			pr_err("Failed to get dphy setttings");
			goto st_err;
		}
	}

	return ret;

st_err:
	return ret;
}

static void vcam_sensor_remove(struct platform_device *pdev)
{
	struct vcam_sensor_device *msnr_dev;

	msnr_dev = platform_get_drvdata(pdev);
	if (!msnr_dev) {
		dev_err(&pdev->dev, "sensor device is NULL");
		return;
	}
	device_destroy(vcamsnr_class, MKDEV(vcamsnr_major, msnr_dev->id));
	cdev_del(&msnr_dev->cdev);
	mutex_destroy(&msnr_dev->lock);
	devm_kfree(&pdev->dev, msnr_dev);
    vcam_snr_dev_destroy(&msnr_dev->cdev, msnr_dev->id);
}

static int vcam_sensor_probe(struct platform_device *pdev)
{
	struct vcam_sensor_device *msnr_dev;
	int ret;
	pr_info("vcam_sensor_probe IN\n");

	msnr_dev = devm_kzalloc(&pdev->dev, sizeof(struct vcam_sensor_device),
				GFP_KERNEL);
	if (!msnr_dev) {
		dev_err(&pdev->dev, "Failed to allocate memory for sensor device");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, msnr_dev);
	msnr_dev->pdev = pdev;

	ret = spacemit_snr_of_parse(msnr_dev);
	if (ret)
		return ret;

	ret = vcam_snr_dev_create(&msnr_dev->cdev, msnr_dev->id);
	if (ret)
		return ret;

	atomic_set(&msnr_dev->usr_cnt, 0);
	mutex_init(&msnr_dev->lock);

	g_sdev[msnr_dev->id] = msnr_dev;
	dev_info(&pdev->dev, "probe successful\n");
	return ret;
}

static const struct of_device_id vcam_sensor_dt_match[] = {
	{ .compatible = "spacemit,vcam-sensor" },
	{}
};
MODULE_DEVICE_TABLE(of, vcam_sensor_dt_match);

static struct platform_driver vcamsnr_driver = {
	.probe  = vcam_sensor_probe,
	.remove = vcam_sensor_remove,
	.driver = {
		.name = SNR_DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = vcam_sensor_dt_match,
	},
};

static int __init vcam_sensor_init(void)
{
	int ret;

	ret = vcam_snr_drv_init();
	if (ret < 0) {
		printk("vcamsnr cdev create failed\n");
		return ret;
	}

	return platform_driver_register(&vcamsnr_driver);
}

static void __exit vcam_sensor_exit(void)
{
	platform_driver_unregister(&vcamsnr_driver);
	vcam_snr_drv_deinit();
}

module_init(vcam_sensor_init);
module_exit(vcam_sensor_exit);

MODULE_AUTHOR("SPACEMIT Inc.");
MODULE_DESCRIPTION("SPACEMIT Camera Sensor Driver");
MODULE_LICENSE("GPL v2");
