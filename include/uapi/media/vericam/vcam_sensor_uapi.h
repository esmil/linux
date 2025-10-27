/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2025 Spacemit  Limited
 * All Rights Reserved.
 */
#ifndef __VCAM_SENSOR_UAPI_H__
#define __VCAM_SENSOR_UAPI_H__

#include <linux/types.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define SNR_DRV_NAME "vcam_sensor"
#define VCAM_SNS_MAX_DEV_NUM 4

#define VCAM_SENSOR_IOC_MAGIC 'I'

typedef enum VCAM_SENSOR_IOC {
	SENSOR_IOC_RESET = 1,
	SENSOR_IOC_UNRESET,
	SENSOR_IOC_I2C_WRITE,
	SENSOR_IOC_I2C_READ,
	SENSOR_IOC_PHY_I2C_WRITE,
	SENSOR_IOC_PHY_I2C_READ,
	SENSOR_IOC_POWER_CFG,
} VCAM_SENSOR_IOC_E;

typedef unsigned int sns_rst_source_t;

struct regval_tab {
	__u16 reg;
	__u16 val;
};

enum sensor_i2c_len {
	I2C_8BIT = 1,
	I2C_16BIT = 2,
	I2C_24BIT = 3,
	I2C_32BIT = 4,
};

struct vcam_cmd_i2c_data {
	__u8 twsi_no;
	enum sensor_i2c_len reg_len;
	enum sensor_i2c_len val_len;
	__u8 addr; /* 7 bit i2c address*/
	struct regval_tab tab;
};

struct vcam_twsi_data {
	__u8 twsi_no;
	__u8 reg_len; /* byte num*/
	__u8 val_len; /* byte num*/
	__u8 addr; /* 7 bit i2c address*/
	__u16 reg;
	__u32 val;
};

#ifndef __SENSOR_POWER_ENUM__
#define __SENSOR_POWER_ENUM__
enum sensor_power_seq_type_t {
	SENSOR_SEQ_CLK,
	SENSOR_SEQ_GPIO,
	SENSOR_SEQ_VREG,
	SENSOR_SEQ_MAX,
};

enum sensor_gpio_type_t {
	SENSOR_GPIO_RESET,
	SENSOR_GPIO_PWDN,
	SENSOR_GPIO_AFVDD,
	SENSOR_GPIO_AVDD,
	SENSOR_GPIO_DVDD,
	SENSOR_GPIO_DPTC,
	SENSOR_GPIO_CUSTOM1,
	SENSOR_GPIO_CUSTOM2,
	SENSOR_GPIO_MAX,
};

enum sensor_vreg_type_t {
	SENSOR_VREG_AFVDD,
	SENSOR_VREG_AVDD,
	SENSOR_VREG_DOVDD,
	SENSOR_VREG_DVDD,
	SENSOR_VREG_CUSTOM1,
	SENSOR_VREG_CUSTOM2,
	SENSOR_VREG_MAX,
};

enum sensor_clk_type_t {
	SENSOR_CLK_MCLK,
	SENSOR_CLK_CUSTOM1,
	SENSOR_CLK_CUSTOM2,
	SENSOR_CLK_MAX,
};
#endif

struct sensor_power_setting {
	enum sensor_power_seq_type_t seq_type;
	__u32 seq_val;
	__u32 config_val;
	__u32 delay;
};

#define VCAM_SENSOR_RESET \
	_IOW(VCAM_SENSOR_IOC_MAGIC, SENSOR_IOC_RESET, sns_rst_source_t)
#define VCAM_SENSOR_UNRESET \
	_IOW(VCAM_SENSOR_IOC_MAGIC, SENSOR_IOC_UNRESET, sns_rst_source_t)
#define VCAM_SENSOR_I2C_WRITE                             \
	_IOW(VCAM_SENSOR_IOC_MAGIC, SENSOR_IOC_I2C_WRITE, \
	     struct vcam_cmd_i2c_data)
#define VCAM_SENSOR_I2C_READ                             \
	_IOW(VCAM_SENSOR_IOC_MAGIC, SENSOR_IOC_I2C_READ, \
	     struct vcam_cmd_i2c_data)
#define VCAM_SENSOR_PHY_I2C_WRITE                             \
	_IOW(VCAM_SENSOR_IOC_MAGIC, SENSOR_IOC_PHY_I2C_WRITE, \
	     struct vcam_twsi_data)
#define VCAM_SENSOR_PHY_I2C_READ                             \
	_IOW(VCAM_SENSOR_IOC_MAGIC, SENSOR_IOC_PHY_I2C_READ, \
	     struct vcam_twsi_data)
#define VCAM_SENSOR_POWER_CFG                             \
	_IOW(VCAM_SENSOR_IOC_MAGIC, SENSOR_IOC_POWER_CFG, \
	     struct sensor_power_setting)

#if defined(__cplusplus)
}
#endif

#endif /* __VCAM_SENSOR_UAPI_H__ */
