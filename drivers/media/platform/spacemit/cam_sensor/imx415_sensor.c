// SPDX-License-Identifier: GPL-2.0
/*
 * Simplified I2C driver for Sony IMX415
 *
 * Copyright (C) 2025 Spacemit Ltd.
 *
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
#include <linux/regulator/consumer.h>

/*
 * IMX415 Configuration: 1920x1080 @ 60fps, 4-lane MIPI
 *
 * MCLK:           24MHz
 * Resolution:     1920x1080
 * Bit Depth:      12bit
 * FPS:            60fps
 * HTS:            365 (0x16D)
 * VTS:            2892
 * PCLK:           72MHz
 * MIPI Data Rate: 1485Mbps/Lane
 * MIPI Clock:     742.5MHz
 */

/* IOCTL interface for user-space control */
#define IMX415_IOC_MAGIC		'I'
#define IMX415_IOCTL_POWER_ON		_IO(IMX415_IOC_MAGIC, 1)
#define IMX415_IOCTL_POWER_OFF		_IO(IMX415_IOC_MAGIC, 2)
#define IMX415_IOCTL_INIT_REGS		_IO(IMX415_IOC_MAGIC, 3)
#define IMX415_IOCTL_STREAM_ON		_IO(IMX415_IOC_MAGIC, 4)
#define IMX415_IOCTL_STREAM_OFF		_IO(IMX415_IOC_MAGIC, 5)
#define IMX415_IOCTL_DETECT		_IO(IMX415_IOC_MAGIC, 6)


#define SENSOR_REG_END 0xFFFF
#define SENSOR_REG_DELAY 0xFFFE

static struct imx415 *global_imx415;

struct imx415 {
	struct i2c_client *client;
	struct gpio_desc *pwdn;
	struct gpio_desc *i2c_mux;
	struct mutex lock;
	struct regulator *vdd;
	struct miscdevice miscdev;
};

struct regval_list {
	u16 addr;
	u8 data;
};

static struct regval_list imx415_1920x1080_10bit_112fps_tab[] = {
	// @@1920x1080 crop 112fps 1485Mbps/Lane
	// MCLK: 37.125Mhz
	// resolution: 1920x1080
	// Mipi : 4 lane
	// Mipi data rate: 1485Mbps/Lane
	// FPS      :112fps
	// HTS      :538 -> 0x021A(0x3028:0x3029)
	// VTS      :1232 -> 0x04d0(0x3024:0x3025)
	// PCLK/system clk     : 74235392
	// Htime= 7.201 us; Vblank= ? ms
	{0x3000, 0x01},  // STANDBY
	{0x3001, 0x00},  // REGHOLD
	{0x3002, 0x01},  // XMSTA
	{0x3003, 0x00},  // XMASTER
	{0x3008, 0x7F},  // BCWAIT_TIME[9:0]
	{0x3009, 0x00},  // BCWAIT_TIME[9:0]
	{0x300A, 0x5B},  // CPWAIT_TIME[9:0]
	{0x300B, 0xA0},  // CPWAIT_TIME[9:0]
	{0x301C, 0x04},  // WINMODE[3:0]
	{0x301D, 0x08},  // -
	{0x3020, 0x00},  // HADD
	{0x3021, 0x00},  // VADD
	{0x3022, 0x00},  // ADDMODE[1:0]
	{0x3023, 0x01},  // VCMODE
	{0x3024, 0xD0},  // VMAX[19:0]
	{0x3025, 0x04},  // VMAX[19:0]
	{0x3026, 0x00},  // VMAX[19:0]
	{0x3028, 0x1A},  // HMAX[15:0]
	{0x3029, 0x02},  // HMAX[15:0]
	{0x302C, 0x00},  // WDMODE[1:0]
	{0x302D, 0x00},  // WDSEL[1:0]
	{0x3030, 0x00},  // HREVERSE
	{0x3031, 0x00},  // ADBIT[1:0]
	{0x3032, 0x00},  // MDBIT
	{0x3033, 0x08},  // SYS_MODE[3:0]
	{0x3040, 0xCC},  // PIX_HST[12:0]
	{0x3041, 0x03},  // PIX_HST[12:0]
	{0x3042, 0x80},  // PIX_HWIDTH[12:0]
	{0x3043, 0x07},  // PIX_HWIDTH[12:0]
	{0x3044, 0x58},  // PIX_VST[12:0]
	{0x3045, 0x04},  // PIX_VST[12:0]
	{0x3046, 0x70},  // PIX_VWIDTH[12:0]
	{0x3047, 0x08},  // PIX_VWIDTH[12:0]
	{0x3050, 0x08},  // SHR0[19:0]
	{0x3051, 0x00},  // SHR0[19:0]
	{0x3052, 0x00},  // SHR0[19:0]
	{0x3054, 0x19},  // SHR1[19:0]
	{0x3055, 0x00},  // SHR1[19:0]
	{0x3056, 0x00},  // SHR1[19:0]
	{0x3058, 0x3E},  // SHR2[19:0]
	{0x3059, 0x00},  // SHR2[19:0]
	{0x305A, 0x00},  // SHR2[19:0]
	{0x305C, 0x66},  // SHR3[19:0]
	{0x305D, 0x00},  // SHR3[19:0]
	{0x305E, 0x00},  // SHR3[19:0]
	{0x3060, 0x25},  // RHS1[19:0]
	{0x3061, 0x00},  // RHS1[19:0]
	{0x3062, 0x00},  // RHS1[19:0]
	{0x3064, 0x4A},  // RHS2[19:0]
	{0x3065, 0x00},  // RHS2[19:0]
	{0x3066, 0x00},  // RHS2[19:0]
	{0x3090, 0x00},  // GAIN_PGC_0[8:0]
	{0x3091, 0x00},  // GAIN_PGC_0[8:0]
	{0x3092, 0x00},  // GAIN_PGC_1[8:0]
	{0x3093, 0x00},  // GAIN_PGC_1[8:0]
	{0x3094, 0x00},  // GAIN_PGC_2[8:0]
	{0x3095, 0x00},  // GAIN_PGC_2[8:0]
	{0x3096, 0x00},  // GAIN_PGC_3[8:0]
	{0x3097, 0x00},  // GAIN_PGC_3[8:0]
	{0x30C0, 0x2A},  // XVSOUTSEL[1:0]
	{0x30C1, 0x00},  // XVS_DRV[1:0]
	{0x30CC, 0x00},  // -
	{0x30CD, 0x00},  // -
	{0x30CF, 0x00},  // XVSMSKCNT_INT[1:0]
	{0x30D9, 0x06},  // DIG_CLP_VSTART[4:0]
	{0x30DA, 0x02},  // DIG_CLP_VNUM[1:0]
	{0x30E2, 0x32},  // BLKELVEL[9:0]
	{0x30E3, 0x00},  // BLKELVEL[9:0]
	{0x3115, 0x00},  // INCKSEL1[7:0]
	{0x3116, 0x24},  // INCKSEL2[7:0]
	{0x3118, 0xA0},  // INCKSEL3[10:0]
	{0x3119, 0x00},  // INCKSEL3[10:0]
	{0x311A, 0xE0},  // INCKSEL4[10:0]
	{0x311B, 0x00},  // INCKSEL4[10:0]
	{0x311E, 0x24},  // INCKSEL5[7:0]
	{0x3260, 0x01},  // GAIN_PGC_FIDMD
	{0x32C8, 0x01},  // -
	{0x32D4, 0x21},  // -
	{0x32EC, 0xA1},  // -
	{0x344C, 0x2B},  // -
	{0x344D, 0x01},  // -
	{0x344E, 0xED},  // -
	{0x344F, 0x01},  // -
	{0x3450, 0xF6},  // -
	{0x3451, 0x02},  // -
	{0x3452, 0x7F},  // -
	{0x3453, 0x03},  // -
	{0x358A, 0x04},  // -
	{0x35A1, 0x02},  // -
	{0x35EC, 0x27},  // -
	{0x35EE, 0x8D},  // -
	{0x35F0, 0x8D},  // -
	{0x35F2, 0x29},  // -
	{0x36BC, 0x0C},  // -
	{0x36CC, 0x53},  // -
	{0x36CD, 0x00},  // -
	{0x36CE, 0x3C},  // -
	{0x36D0, 0x8C},  // -
	{0x36D1, 0x00},  // -
	{0x36D2, 0x71},  // -
	{0x36D4, 0x3C},  // -
	{0x36D6, 0x53},  // -
	{0x36D7, 0x00},  // -
	{0x36D8, 0x71},  // -
	{0x36DA, 0x8C},  // -
	{0x36DB, 0x00},  // -
	{0x3701, 0x00},  // ADBIT1[7:0]
	{0x3720, 0x00},  // -
	{0x3724, 0x02},  // -
	{0x3726, 0x02},  // -
	{0x3732, 0x02},  // -
	{0x3734, 0x03},  // -
	{0x3736, 0x03},  // -
	{0x3742, 0x03},  // -
	{0x3862, 0xE0},  // -
	{0x38CC, 0x30},  // -
	{0x38CD, 0x2F},  // -
	{0x395C, 0x0C},  // -
	{0x39A4, 0x07},  // -
	{0x39A8, 0x32},  // -
	{0x39AA, 0x32},  // -
	{0x39AC, 0x32},  // -
	{0x39AE, 0x32},  // -
	{0x39B0, 0x32},  // -
	{0x39B2, 0x2F},  // -
	{0x39B4, 0x2D},  // -
	{0x39B6, 0x28},  // -
	{0x39B8, 0x30},  // -
	{0x39BA, 0x30},  // -
	{0x39BC, 0x30},  // -
	{0x39BE, 0x30},  // -
	{0x39C0, 0x30},  // -
	{0x39C2, 0x2E},  // -
	{0x39C4, 0x2B},  // -
	{0x39C6, 0x25},  // -
	{0x3A42, 0xD1},  // -
	{0x3A4C, 0x77},  // -
	{0x3AE0, 0x02},  // -
	{0x3AEC, 0x0C},  // -
	{0x3B00, 0x2E},  // -
	{0x3B06, 0x29},  // -
	{0x3B98, 0x25},  // -
	{0x3B99, 0x21},  // -
	{0x3B9B, 0x13},  // -
	{0x3B9C, 0x13},  // -
	{0x3B9D, 0x13},  // -
	{0x3B9E, 0x13},  // -
	{0x3BA1, 0x00},  // -
	{0x3BA2, 0x06},  // -
	{0x3BA3, 0x0B},  // -
	{0x3BA4, 0x10},  // -
	{0x3BA5, 0x14},  // -
	{0x3BA6, 0x18},  // -
	{0x3BA7, 0x1A},  // -
	{0x3BA8, 0x1A},  // -
	{0x3BA9, 0x1A},  // -
	{0x3BAC, 0xED},  // -
	{0x3BAD, 0x01},  // -
	{0x3BAE, 0xF6},  // -
	{0x3BAF, 0x02},  // -
	{0x3BB0, 0xA2},  // -
	{0x3BB1, 0x03},  // -
	{0x3BB2, 0xE0},  // -
	{0x3BB3, 0x03},  // -
	{0x3BB4, 0xE0},  // -
	{0x3BB5, 0x03},  // -
	{0x3BB6, 0xE0},  // -
	{0x3BB7, 0x03},  // -
	{0x3BB8, 0xE0},  // -
	{0x3BBA, 0xE0},  // -
	{0x3BBC, 0xDA},  // -
	{0x3BBE, 0x88},  // -
	{0x3BC0, 0x44},  // -
	{0x3BC2, 0x7B},  // -
	{0x3BC4, 0xA2},  // -
	{0x3BC8, 0xBD},  // -
	{0x3BCA, 0xBD},  // -
	{0x4000, 0x10},  // -
	{0x4001, 0x03},  // LANEMODE[2:0]
	{0x4004, 0x48},  // TXCLKESC_FREQ[15:0]
	{0x4005, 0x09},  // TXCLKESC_FREQ[15:0]
	{0x400C, 0x01},  // INCKSEL6
	{0x4018, 0xA7},  // TCLKPOST[15:0]
	{0x4019, 0x00},  // TCLKPOST[15:0]
	{0x401A, 0x57},  // TCLKPREPARE[15:0]
	{0x401B, 0x00},  // TCLKPREPARE[15:0]
	{0x401C, 0x5F},  // TCLKTRAIL[15:0]
	{0x401D, 0x00},  // TCLKTRAIL[15:0]
	{0x401E, 0x97},  // TCLKZERO[15:0]
	{0x401F, 0x01},  // TCLKZERO[15:0]
	{0x4020, 0x5F},  // THSPREPARE[15:0] smaller
	{0x4021, 0x00},  // THSPREPARE[15:0]
	{0x4022, 0xAF},  // THSZERO[15:0]	bigger
	{0x4023, 0x00},  // THSZERO[15:0]
	{0x4024, 0x5F},  // THSTRAIL[15:0]	bigger
	{0x4025, 0x00},  // THSTRAIL[15:0]
	{0x4026, 0x9F},  // THSEXIT[15:0]
	{0x4027, 0x00},  // THSEXIT[15:0]
	{0x4028, 0x4F},  // TLPX[15:0]
	{0x4029, 0x00},  // TLPX[15:0]
	{0x4074, 0x00},  // INCKSEL7[2:0]
};

static struct regval_list imx415_1920x1080_12bit_112fps_tab[] = {
	// @@1920x1080 crop 112fps 1485Mbps/Lane
	// MCLK: 37.125Mhz
	// resolution: 1920x1080
	// Mipi : 4 lane
	// Mipi data rate: 1485Mbps/Lane
	// FPS      :112fps
	// HTS      :538 -> 0x021A(0x3028:0x3029)
	// VTS      :1232 -> 0x04d0(0x3024:0x3025)
	// PCLK/system clk     : 74235392
	// Htime= 7.201 us; Vblank= ? ms
	{0x3000, 0x01},  // STANDBY
	{0x3001, 0x00},  // REGHOLD
	{0x3002, 0x01},  // XMSTA
	{0x3003, 0x00},  // XMASTER
	{0x3008, 0x7F},  // BCWAIT_TIME[9:0]
	{0x3009, 0x00},  // BCWAIT_TIME[9:0]
	{0x300A, 0x5B},  // CPWAIT_TIME[9:0]
	{0x300B, 0xA0},  // CPWAIT_TIME[9:0]
	{0x301C, 0x04},  // WINMODE[3:0]
	{0x301D, 0x08},  // -
	{0x3020, 0x00},  // HADD
	{0x3021, 0x00},  // VADD
	{0x3022, 0x00},  // ADDMODE[1:0]
	{0x3023, 0x01},  // VCMODE
	{0x3024, 0xD0},  // VMAX[19:0]
	{0x3025, 0x04},  // VMAX[19:0]
	{0x3026, 0x00},  // VMAX[19:0]
	{0x3028, 0x1A},  // HMAX[15:0]
	{0x3029, 0x02},  // HMAX[15:0]
	{0x302C, 0x00},  // WDMODE[1:0]
	{0x302D, 0x00},  // WDSEL[1:0]
	{0x3030, 0x00},  // HREVERSE
	{0x3031, 0x01},  // ADBIT[1:0]
	{0x3032, 0x01},  // MDBIT
	{0x3033, 0x08},  // SYS_MODE[3:0]
	{0x3040, 0xCC},  // PIX_HST[12:0]
	{0x3041, 0x03},  // PIX_HST[12:0]
	{0x3042, 0x80},  // PIX_HWIDTH[12:0]
	{0x3043, 0x07},  // PIX_HWIDTH[12:0]
	{0x3044, 0x58},  // PIX_VST[12:0]
	{0x3045, 0x04},  // PIX_VST[12:0]
	{0x3046, 0x70},  // PIX_VWIDTH[12:0]
	{0x3047, 0x08},  // PIX_VWIDTH[12:0]
	{0x3050, 0x08},  // SHR0[19:0]
	{0x3051, 0x00},  // SHR0[19:0]
	{0x3052, 0x00},  // SHR0[19:0]
	{0x3054, 0x19},  // SHR1[19:0]
	{0x3055, 0x00},  // SHR1[19:0]
	{0x3056, 0x00},  // SHR1[19:0]
	{0x3058, 0x3E},  // SHR2[19:0]
	{0x3059, 0x00},  // SHR2[19:0]
	{0x305A, 0x00},  // SHR2[19:0]
	{0x305C, 0x66},  // SHR3[19:0]
	{0x305D, 0x00},  // SHR3[19:0]
	{0x305E, 0x00},  // SHR3[19:0]
	{0x3060, 0x25},  // RHS1[19:0]
	{0x3061, 0x00},  // RHS1[19:0]
	{0x3062, 0x00},  // RHS1[19:0]
	{0x3064, 0x4A},  // RHS2[19:0]
	{0x3065, 0x00},  // RHS2[19:0]
	{0x3066, 0x00},  // RHS2[19:0]
	{0x3090, 0x00},  // GAIN_PGC_0[8:0]
	{0x3091, 0x00},  // GAIN_PGC_0[8:0]
	{0x3092, 0x00},  // GAIN_PGC_1[8:0]
	{0x3093, 0x00},  // GAIN_PGC_1[8:0]
	{0x3094, 0x00},  // GAIN_PGC_2[8:0]
	{0x3095, 0x00},  // GAIN_PGC_2[8:0]
	{0x3096, 0x00},  // GAIN_PGC_3[8:0]
	{0x3097, 0x00},  // GAIN_PGC_3[8:0]
	{0x30C0, 0x2A},  // XVSOUTSEL[1:0]
	{0x30C1, 0x00},  // XVS_DRV[1:0]
	{0x30CC, 0x00},  // -
	{0x30CD, 0x00},  // -
	{0x30CF, 0x00},  // XVSMSKCNT_INT[1:0]
	{0x30D9, 0x06},  // DIG_CLP_VSTART[4:0]
	{0x30DA, 0x02},  // DIG_CLP_VNUM[1:0]
	{0x30E2, 0x32},  // BLKELVEL[9:0]
	{0x30E3, 0x00},  // BLKELVEL[9:0]
	{0x3115, 0x00},  // INCKSEL1[7:0]
	{0x3116, 0x24},  // INCKSEL2[7:0]
	{0x3118, 0xA0},  // INCKSEL3[10:0]
	{0x3119, 0x00},  // INCKSEL3[10:0]
	{0x311A, 0xE0},  // INCKSEL4[10:0]
	{0x311B, 0x00},  // INCKSEL4[10:0]
	{0x311E, 0x24},  // INCKSEL5[7:0]
	{0x3260, 0x01},  // GAIN_PGC_FIDMD
	{0x32C8, 0x01},  // -
	{0x32D4, 0x21},  // -
	{0x32EC, 0xA1},  // -
	{0x344C, 0x2B},  // -
	{0x344D, 0x01},  // -
	{0x344E, 0xED},  // -
	{0x344F, 0x01},  // -
	{0x3450, 0xF6},  // -
	{0x3451, 0x02},  // -
	{0x3452, 0x7F},  // -
	{0x3453, 0x03},  // -
	{0x358A, 0x04},  // -
	{0x35A1, 0x02},  // -
	{0x35EC, 0x27},  // -
	{0x35EE, 0x8D},  // -
	{0x35F0, 0x8D},  // -
	{0x35F2, 0x29},  // -
	{0x36BC, 0x0C},  // -
	{0x36CC, 0x53},  // -
	{0x36CD, 0x00},  // -
	{0x36CE, 0x3C},  // -
	{0x36D0, 0x8C},  // -
	{0x36D1, 0x00},  // -
	{0x36D2, 0x71},  // -
	{0x36D4, 0x3C},  // -
	{0x36D6, 0x53},  // -
	{0x36D7, 0x00},  // -
	{0x36D8, 0x71},  // -
	{0x36DA, 0x8C},  // -
	{0x36DB, 0x00},  // -
	{0x3701, 0x00},  // ADBIT1[7:0]
	{0x3720, 0x00},  // -
	{0x3724, 0x02},  // -
	{0x3726, 0x02},  // -
	{0x3732, 0x02},  // -
	{0x3734, 0x03},  // -
	{0x3736, 0x03},  // -
	{0x3742, 0x03},  // -
	{0x3862, 0xE0},  // -
	{0x38CC, 0x30},  // -
	{0x38CD, 0x2F},  // -
	{0x395C, 0x0C},  // -
	{0x39A4, 0x07},  // -
	{0x39A8, 0x32},  // -
	{0x39AA, 0x32},  // -
	{0x39AC, 0x32},  // -
	{0x39AE, 0x32},  // -
	{0x39B0, 0x32},  // -
	{0x39B2, 0x2F},  // -
	{0x39B4, 0x2D},  // -
	{0x39B6, 0x28},  // -
	{0x39B8, 0x30},  // -
	{0x39BA, 0x30},  // -
	{0x39BC, 0x30},  // -
	{0x39BE, 0x30},  // -
	{0x39C0, 0x30},  // -
	{0x39C2, 0x2E},  // -
	{0x39C4, 0x2B},  // -
	{0x39C6, 0x25},  // -
	{0x3A42, 0xD1},  // -
	{0x3A4C, 0x77},  // -
	{0x3AE0, 0x02},  // -
	{0x3AEC, 0x0C},  // -
	{0x3B00, 0x2E},  // -
	{0x3B06, 0x29},  // -
	{0x3B98, 0x25},  // -
	{0x3B99, 0x21},  // -
	{0x3B9B, 0x13},  // -
	{0x3B9C, 0x13},  // -
	{0x3B9D, 0x13},  // -
	{0x3B9E, 0x13},  // -
	{0x3BA1, 0x00},  // -
	{0x3BA2, 0x06},  // -
	{0x3BA3, 0x0B},  // -
	{0x3BA4, 0x10},  // -
	{0x3BA5, 0x14},  // -
	{0x3BA6, 0x18},  // -
	{0x3BA7, 0x1A},  // -
	{0x3BA8, 0x1A},  // -
	{0x3BA9, 0x1A},  // -
	{0x3BAC, 0xED},  // -
	{0x3BAD, 0x01},  // -
	{0x3BAE, 0xF6},  // -
	{0x3BAF, 0x02},  // -
	{0x3BB0, 0xA2},  // -
	{0x3BB1, 0x03},  // -
	{0x3BB2, 0xE0},  // -
	{0x3BB3, 0x03},  // -
	{0x3BB4, 0xE0},  // -
	{0x3BB5, 0x03},  // -
	{0x3BB6, 0xE0},  // -
	{0x3BB7, 0x03},  // -
	{0x3BB8, 0xE0},  // -
	{0x3BBA, 0xE0},  // -
	{0x3BBC, 0xDA},  // -
	{0x3BBE, 0x88},  // -
	{0x3BC0, 0x44},  // -
	{0x3BC2, 0x7B},  // -
	{0x3BC4, 0xA2},  // -
	{0x3BC8, 0xBD},  // -
	{0x3BCA, 0xBD},  // -
	{0x4000, 0x10},  // -
	{0x4001, 0x03},  // LANEMODE[2:0]
	{0x4004, 0x48},  // TXCLKESC_FREQ[15:0]
	{0x4005, 0x09},  // TXCLKESC_FREQ[15:0]
	{0x400C, 0x01},  // INCKSEL6
	{0x4018, 0xA7},  // TCLKPOST[15:0]
	{0x4019, 0x00},  // TCLKPOST[15:0]
	{0x401A, 0x57},  // TCLKPREPARE[15:0]
	{0x401B, 0x00},  // TCLKPREPARE[15:0]
	{0x401C, 0x5F},  // TCLKTRAIL[15:0]
	{0x401D, 0x00},  // TCLKTRAIL[15:0]
	{0x401E, 0x97},  // TCLKZERO[15:0]
	{0x401F, 0x01},  // TCLKZERO[15:0]
	{0x4020, 0x5F},  // THSPREPARE[15:0] smaller
	{0x4021, 0x00},  // THSPREPARE[15:0]
	{0x4022, 0xAF},  // THSZERO[15:0]	bigger
	{0x4023, 0x00},  // THSZERO[15:0]
	{0x4024, 0x5F},  // THSTRAIL[15:0]	bigger
	{0x4025, 0x00},  // THSTRAIL[15:0]
	{0x4026, 0x9F},  // THSEXIT[15:0]
	{0x4027, 0x00},  // THSEXIT[15:0]
	{0x4028, 0x4F},  // TLPX[15:0]
	{0x4029, 0x00},  // TLPX[15:0]
	{0x4074, 0x00},  // INCKSEL7[2:0]
    };
    
/*
 * 1920x1080@60fps 12bit 4-lane MIPI configuration
 * From sensor_init_regs_1920_1080_60fps_mipi[]
 *
 * Key registers:
 * - 0x3033: 0x08 (SYS_MODE[3:0])
 * - 0x3024: VMAX[19:0] = 0x0B4C (2892)
 * - 0x3028: HMAX[15:0] = 0x016D (365)
 */
static struct regval_list imx415_1920x1080_60fps_12bpp_4lane_regs[] = {
	{0x3008, 0x7F},	/* BCWAIT_TIME[9:0] */
	{0x300A, 0x5B},	/* CPWAIT_TIME[9:0] */
	{0x3020, 0x01},	/* HADD */
	{0x3021, 0x01},	/* VADD */
	{0x3022, 0x01},	/* ADDMODE[1:0] */
	{0x3024, 0x4C},	/* VMAX[19:0] LSB */
	{0x3025, 0x0B},	/* VMAX[19:0] MSB */
	{0x3028, 0x6D},	/* HMAX[15:0] LSB */
	{0x3029, 0x01},	/* HMAX[15:0] MSB */
	{0x3031, 0x00},	/* ADBIT[1:0] */
	{0x3033, 0x08},	/* SYS_MODE[3:0] - MIPI mode */
	{0x3050, 0x08},	/* SHR0[19:0] LSB */
	{0x30C1, 0x00},	/* XVS_DRV[1:0] */
	{0x30D9, 0x02},	/* DIG_CLP_VSTART[4:0] */
	{0x30DA, 0x01},	/* DIG_CLP_VNUM[1:0] */
	{0x3116, 0x24},	/* INCKSEL2[7:0] */
	{0x3118, 0xA0},	/* INCKSEL3[10:0] LSB */
	{0x311E, 0x24},	/* INCKSEL5[7:0] */
	{0x32D4, 0x21},	/* */
	{0x32EC, 0xA1},	/* */
	{0x344C, 0x2B},	/* */
	{0x344D, 0x01},	/* */
	{0x344E, 0xED},	/* */
	{0x344F, 0x01},	/* */
	{0x3450, 0xF6},	/* */
	{0x3451, 0x02},	/* */
	{0x3452, 0x7F},	/* */
	{0x3453, 0x03},	/* */
	{0x358A, 0x04},	/* */
	{0x35A1, 0x02},	/* */
	{0x35EC, 0x27},	/* */
	{0x35EE, 0x8D},	/* */
	{0x35F0, 0x8D},	/* */
	{0x35F2, 0x29},	/* */
	{0x36BC, 0x0C},	/* */
	{0x36CC, 0x53},	/* */
	{0x36CD, 0x00},	/* */
	{0x36CE, 0x3C},	/* */
	{0x36D0, 0x8C},	/* */
	{0x36D1, 0x00},	/* */
	{0x36D2, 0x71},	/* */
	{0x36D4, 0x3C},	/* */
	{0x36D6, 0x53},	/* */
	{0x36D7, 0x00},	/* */
	{0x36D8, 0x71},	/* */
	{0x36DA, 0x8C},	/* */
	{0x36DB, 0x00},	/* */
	{0x3701, 0x00},	/* ADBIT1[7:0] */
	{0x3720, 0x00},	/* */
	{0x3724, 0x02},	/* */
	{0x3726, 0x02},	/* */
	{0x3732, 0x02},	/* */
	{0x3734, 0x03},	/* */
	{0x3736, 0x03},	/* */
	{0x3742, 0x03},	/* */
	{0x3862, 0xE0},	/* */
	{0x38CC, 0x30},	/* */
	{0x38CD, 0x2F},	/* */
	{0x395C, 0x0C},	/* */
	{0x39A4, 0x07},	/* */
	{0x39A8, 0x32},	/* */
	{0x39AA, 0x32},	/* */
	{0x39AC, 0x32},	/* */
	{0x39AE, 0x32},	/* */
	{0x39B0, 0x32},	/* */
	{0x39B2, 0x2F},	/* */
	{0x39B4, 0x2D},	/* */
	{0x39B6, 0x28},	/* */
	{0x39B8, 0x30},	/* */
	{0x39BA, 0x30},	/* */
	{0x39BC, 0x30},	/* */
	{0x39BE, 0x30},	/* */
	{0x39C0, 0x30},	/* */
	{0x39C2, 0x2E},	/* */
	{0x39C4, 0x2B},	/* */
	{0x39C6, 0x25},	/* */
	{0x3A42, 0xD1},	/* */
	{0x3A4C, 0x77},	/* */
	{0x3AE0, 0x02},	/* */
	{0x3AEC, 0x0C},	/* */
	{0x3B00, 0x2E},	/* */
	{0x3B06, 0x29},	/* */
	{0x3B98, 0x25},	/* */
	{0x3B99, 0x21},	/* */
	{0x3B9B, 0x13},	/* */
	{0x3B9C, 0x13},	/* */
	{0x3B9D, 0x13},	/* */
	{0x3B9E, 0x13},	/* */
	{0x3BA1, 0x00},	/* */
	{0x3BA2, 0x06},	/* */
	{0x3BA3, 0x0B},	/* */
	{0x3BA4, 0x10},	/* */
	{0x3BA5, 0x14},	/* */
	{0x3BA6, 0x18},	/* */
	{0x3BA7, 0x1A},	/* */
	{0x3BA8, 0x1A},	/* */
	{0x3BA9, 0x1A},	/* */
	{0x3BAC, 0xED},	/* */
	{0x3BAD, 0x01},	/* */
	{0x3BAE, 0xF6},	/* */
	{0x3BAF, 0x02},	/* */
	{0x3BB0, 0xA2},	/* */
	{0x3BB1, 0x03},	/* */
	{0x3BB2, 0xE0},	/* */
	{0x3BB3, 0x03},	/* */
	{0x3BB4, 0xE0},	/* */
	{0x3BB5, 0x03},	/* */
	{0x3BB6, 0xE0},	/* */
	{0x3BB7, 0x03},	/* */
	{0x3BB8, 0xE0},	/* */
	{0x3BBA, 0xE0},	/* */
	{0x3BBC, 0xDA},	/* */
	{0x3BBE, 0x88},	/* */
	{0x3BC0, 0x44},	/* */
	{0x3BC2, 0x7B},	/* */
	{0x3BC4, 0xA2},	/* */
	{0x3BC8, 0xBD},	/* */
	{0x3BCA, 0xBD},	/* */
	{0x4004, 0x48},	/* TXCLKESC_FREQ[15:0] */
	{0x4005, 0x09},	/* */
	{0x4018, 0xA7},	/* TCLKPOST[15:0] */
	{0x401A, 0x57},	/* TCLKPREPARE[15:0] */
	{0x401C, 0x5F},	/* TCLKTRAIL[15:0] */
	{0x401E, 0x97},	/* TCLKZERO[15:0] */
	{0x4020, 0x5F},	/* THSPREPARE[15:0] */
	{0x4022, 0xAF},	/* THSZERO[15:0] */
	{0x4024, 0x5F},	/* THSTRAIL[15:0] */
	{0x4026, 0x9F},	/* THSEXIT[15:0] */
	{0x4028, 0x4F},	/* TLPX[15:0] */
	{0x3000, 0x00},	/* STANDBY */
};

static int imx415_write(struct imx415 *sensor, u16 reg, u8 val)
{
	struct i2c_adapter *adapter = sensor->client->adapter;
	struct i2c_msg msg;
	u8 data[3];
	int ret;

	data[0] = (reg >> 8) & 0xFF;
	data[1] = reg & 0xFF;
	data[2] = val & 0xFF;

	msg.addr = sensor->client->addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = data;

	mutex_lock(&sensor->lock);
	ret = i2c_transfer(adapter, &msg, 1);
	mutex_unlock(&sensor->lock);

	if (ret != 1) {
		dev_err(&sensor->client->dev,
			"imx415-test: I2C write failed, reg=0x%04x, val=0x%02x, ret=%d\n",
			reg, val, ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int imx415_read(struct imx415 *sensor, u16 reg, u8 *val)
{
	struct i2c_adapter *adapter = sensor->client->adapter;
	struct i2c_msg msgs[2];
	u8 reg_buf[2];
	u8 data_buf[1];
	int ret;

	reg_buf[0] = (reg >> 8) & 0xFF;
	reg_buf[1] = reg & 0xFF;

	msgs[0].addr = sensor->client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = reg_buf;

	msgs[1].addr = sensor->client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = data_buf;

	mutex_lock(&sensor->lock);
	ret = i2c_transfer(adapter, msgs, 2);
	mutex_unlock(&sensor->lock);

	if (ret != 2) {
		dev_err(&sensor->client->dev,
			"imx415-test: I2C read failed, reg=0x%04x, ret=%d\n",
			reg, ret);
		return ret < 0 ? ret : -EIO;
	}

	*val = data_buf[0];
	return 0;
}

static int imx415_write_array(struct imx415 *sensor, struct regval_list *vals)
{
	int ret;

	while (vals->addr != SENSOR_REG_END) {
		if (vals->addr == SENSOR_REG_DELAY) {
			msleep(vals->data);
		} else {
			ret = imx415_write(sensor, vals->addr, vals->data);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int imx415_stream_on(struct imx415 *sensor)
{
	imx415_write(sensor, 0x3000, 0x00);
	imx415_write(sensor, 0x3002, 0x00);
	return 0;
}

static int imx415_stream_off(struct imx415 *sensor)
{
	return imx415_write(sensor, 0x3000, 0x01);
}

static int imx415_detect(struct imx415 *sensor)
{
	u8 lo, hi;
	u16 chip_id;
	int ret;

	dev_info(&sensor->client->dev, "imx415-test: detect start\n");

	/*
	 * The sensor info register (0x3F12, 16-bit LE) can only be read when
	 * the sensor is NOT in standby mode. Wake the sensor up first.
	 */
	ret = imx415_write(sensor, 0x3000, 0x00); /* STANDBY -> Operating */
	if (ret < 0)
		return ret;
	/* Wait for sensor to wake up */
	usleep_range(30000, 40000);

	/* Read SENSOR_INFO register at 0x3F12 (16-bit, little-endian) */
	ret = imx415_read(sensor, 0x3f12, &lo);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"imx415-test: failed to read sensor info low byte\n");
		goto done;
	}

	ret = imx415_read(sensor, 0x3f13, &hi);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"imx415-test: failed to read sensor info high byte\n");
		goto done;
	}

	chip_id = ((u16)hi << 8) | lo;
	chip_id &= 0x0FFF; /* IMX415_SENSOR_INFO_MASK */

	if (chip_id != 0x0514) { /* IMX415_CHIP_ID */
		dev_err(&sensor->client->dev,
			"imx415-test: chip ID mismatch: expected 0x0514, got 0x%04x\n",
			chip_id);
		ret = -ENODEV;
		goto done;
	}

	dev_info(&sensor->client->dev,
		 "imx415-test: Detected IMX415 image sensor (chip_id=0x%04x)\n",
		 chip_id);
	usleep_range(10000, 20000);
	ret = 0;

done:
	/* Put sensor back to standby */
	imx415_write(sensor, 0x3000, 0x01);
	return ret;
}

static int imx415_power_on(struct imx415 *sensor)
{
	int ret = 0;

	dev_info(&sensor->client->dev, "imx415-test: power_on enter\n");

	/* Set I2C mux to select this sensor */
	if (sensor->i2c_mux) {
		dev_info(&sensor->client->dev, "imx415-test: set i2c-mux high\n");
		gpiod_set_value_cansleep(sensor->i2c_mux, 1);
	}

	dev_info(&sensor->client->dev, "imx415-test: get vdd regulator\n");
	sensor->vdd = devm_regulator_get(&sensor->client->dev, "vdd");
	if (IS_ERR(sensor->vdd)) {
		dev_err(&sensor->client->dev, "imx415-test: Failed to get vdd regulator: %ld\n",
			PTR_ERR(sensor->vdd));
		return PTR_ERR(sensor->vdd);
	}

	if (sensor->vdd) {
		ret = regulator_enable(sensor->vdd);
		if (ret < 0) {
			dev_err(&sensor->client->dev,
				"imx415-test: failed to enable vdd: %d\n", ret);
			return ret;
		}
		dev_info(&sensor->client->dev, "imx415-test: enable vdd\n");

		ret = regulator_set_voltage(sensor->vdd, 3300000, 3300000);
		if (ret < 0) {
			dev_err(&sensor->client->dev, "imx415-test: failed to set vdd voltage: %d\n",
				ret);
			return ret;
		}
		dev_info(&sensor->client->dev, "imx415-test: vdd set to 3.3V\n");
	}

	if (sensor->pwdn) {
		gpiod_set_value_cansleep(sensor->pwdn, 0);
		usleep_range(10000, 20000);
		gpiod_set_value_cansleep(sensor->pwdn, 1);
		usleep_range(10000, 20000);
	}

	dev_info(&sensor->client->dev, "imx415-test: power_on done\n");

	return 0;
}

static void imx415_power_off(struct imx415 *sensor)
{
	dev_info(&sensor->client->dev, "imx415-test: power_off enter\n");

	if (sensor->pwdn) {
		gpiod_set_value_cansleep(sensor->pwdn, 0);
	}

	if (sensor->vdd) {
		dev_info(&sensor->client->dev, "imx415-test: disable vdd\n");
		regulator_disable(sensor->vdd);
	}

	/* Clear I2C mux */
	if (sensor->i2c_mux) {
		dev_info(&sensor->client->dev, "imx415-test: set i2c-mux low\n");
		gpiod_set_value_cansleep(sensor->i2c_mux, 0);
	}

	dev_info(&sensor->client->dev, "imx415-test: power_off done\n");
}

static long imx415_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct imx415 *sensor = file->private_data;
	int ret = 0;

	if (!sensor)
		return -ENODEV;

	switch (cmd) {
	case IMX415_IOCTL_POWER_ON:
		ret = imx415_power_on(sensor);
		break;
	case IMX415_IOCTL_POWER_OFF:
		imx415_power_off(sensor);
		ret = 0;
		break;
	case IMX415_IOCTL_INIT_REGS:
		ret = imx415_write_array(
			sensor, imx415_1920x1080_12bit_112fps_tab);
		break;
	case IMX415_IOCTL_STREAM_ON:
		ret = imx415_stream_on(sensor);
		break;
	case IMX415_IOCTL_STREAM_OFF:
		ret = imx415_stream_off(sensor);
		break;
	case IMX415_IOCTL_DETECT:
		ret = imx415_detect(sensor);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int imx415_dev_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct imx415 *sensor;

	if (!misc)
		return -ENODEV;

	sensor = container_of(misc, struct imx415, miscdev);
	file->private_data = sensor;
	return 0;
}

static const struct file_operations imx415_fops = {
	.owner = THIS_MODULE,
	.open = imx415_dev_open,
	.unlocked_ioctl = imx415_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = imx415_ioctl,
#endif
};

static int imx415_probe(struct i2c_client *client)
{
	struct imx415 *sensor;
	struct device *dev = &client->dev;
	int ret;

	dev_info(dev, "imx415-test: probe enter, client addr=0x%02x\n",
		 client->addr);

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->client = client;
	mutex_init(&sensor->lock);
	i2c_set_clientdata(client, sensor);

	/* Get power-down GPIO */
	sensor->pwdn = devm_gpiod_get_optional(
		dev, "pwdn", GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(sensor->pwdn)) {
		dev_err(dev, "imx415-test: Failed to get pwdn GPIO\n");
		goto err_pwdn;
	}

	/* Get I2C mux GPIO (optional) */
	sensor->i2c_mux = devm_gpiod_get_optional(dev, "i2c-mux",
		GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	if (IS_ERR(sensor->i2c_mux)) {
		dev_warn(dev, "imx415-test: Failed to get i2c-mux GPIO, continuing without it\n");
		sensor->i2c_mux = NULL;
	}

	/* Power on and detect sensor before creating device node */
	ret = imx415_power_on(sensor);
	if (ret) {
		dev_err(dev, "imx415-test: power on failed: %d\n", ret);
		goto err_pwdn;
	}

	ret = imx415_detect(sensor);
	if (ret) {
		dev_err(dev, "imx415-test: sensor detect failed: %d\n", ret);
		goto err_detect;
	}

	imx415_power_off(sensor);

	sensor->miscdev.minor = MISC_DYNAMIC_MINOR;
	sensor->miscdev.fops = &imx415_fops;
	sensor->miscdev.parent = dev;

	/* Generate device name from CSI ID or I2C address */
	if (client->dev.of_node) {
		u32 csi_id;
		if (of_property_read_u32(client->dev.of_node, "csi-id",
					 &csi_id) == 0) {
			sensor->miscdev.name = devm_kasprintf(
				dev, GFP_KERNEL, "imx415-%u", csi_id);
			dev_info(dev, "imx415-test: imx415-%u\n", csi_id);
		} else {
			sensor->miscdev.name = devm_kasprintf(
				dev, GFP_KERNEL, "imx415-%02x", client->addr);
			dev_info(dev, "imx415-test: imx415-%02x\n",
				 client->addr);
		}
	} else {
		sensor->miscdev.name = devm_kasprintf(
			dev, GFP_KERNEL, "imx415-%02x", client->addr);
	}

	ret = misc_register(&sensor->miscdev);
	if (ret) {
		dev_err(dev,
			"imx415-test: failed to register misc device: %d\n",
			ret);
		goto err_misc_register;
	}

	global_imx415 = sensor;
	dev_info(dev, "imx415-test: probe successful, ioctl device /dev/%s\n",
		 sensor->miscdev.name);
	return 0;

err_misc_register:
	imx415_power_off(sensor);
	mutex_destroy(&sensor->lock);
	return ret;

err_detect:
	imx415_power_off(sensor);
err_pwdn:
	mutex_destroy(&sensor->lock);
	return ret;
}

static void imx415_remove(struct i2c_client *client)
{
	struct imx415 *sensor = i2c_get_clientdata(client);

	dev_info(&client->dev, "imx415-test: remove\n");
	if (global_imx415 == sensor)
		global_imx415 = NULL;

	misc_deregister(&sensor->miscdev);
	imx415_power_off(sensor);
	mutex_destroy(&sensor->lock);
}

static const struct of_device_id imx415_of_match[] = {
	{ .compatible = "sony,imx415" },
	{}
};
MODULE_DEVICE_TABLE(of, imx415_of_match);

static struct i2c_driver imx415_driver = {
	.driver = {
		.name		= "imx415-simple",
		.of_match_table	= of_match_ptr(imx415_of_match),
	},
	.probe		= imx415_probe,
	.remove		= imx415_remove,
};

module_i2c_driver(imx415_driver)

MODULE_DESCRIPTION("Simplified I2C driver for IMX415");
MODULE_LICENSE("GPL v2");

