// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/of.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/component.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <drm/drm_of.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/display/drm_dp_aux_bus.h>
#include <drm/display/drm_dp.h>
#include <drm/display/drm_dp_helper.h>

#include "spacemit_inno_dp.h"

#define INVALID_GPIO	0xFFFFFFFF

#define ACTIVATE_DO_DIV	0
#define HPD_BYPASS	0

#define HOT_PLUG_THREAD_ENABLED 1
#define HPD_POLL_INTERVAL_MS    200

#define SOC_DP_SWING_MAX  2
#define SOC_DP_PREEMP_MAX 2

#ifdef CONFIG_SOC_DP_DRIVER_QEMU
#include <linux/proc_fs.h>
#else
#include <linux/clk.h>
#include <linux/reset.h>
#endif

#if HOT_PLUG_THREAD_ENABLED
#include <linux/workqueue.h>
#endif

#if ACTIVATE_DO_DIV
#include <linux/math64.h>
#endif

/*
 * Local definitions for Link Configuration.
 * Decoupled from <drm/drm_dp_helper.h> to facilitate bare-metal porting.
 */
enum soc_dp_link_rate {
	SOC_DP_LINK_RATE_1_62 = 1620000, /* 1.62 Gbps */
	SOC_DP_LINK_RATE_2_70 = 2700000, /* 2.70 Gbps */
	SOC_DP_LINK_RATE_5_40 = 5400000, /* 5.40 Gbps */
	SOC_DP_LINK_RATE_8_10 = 8100000, /* 8.10 Gbps */
};

enum soc_dp_lane_count {
	SOC_DP_LANE_1 = 1,
	SOC_DP_LANE_2 = 2,
	SOC_DP_LANE_4 = 4,
};

enum soc_video_format {
	SOC_VIDEO_RGB_6BIT = 0,
	SOC_VIDEO_RGB_8BIT = 1,
	SOC_VIDEO_RGB_10BIT = 2,
	SOC_VIDEO_RGB_12BIT = 3,
	SOC_VIDEO_RGB_16BIT = 4,
	SOC_VIDEO_YUV444_8BIT = 5,
	SOC_VIDEO_YUV444_10BIT = 6,
	SOC_VIDEO_YUV444_12BIT = 7,
	SOC_VIDEO_YUV444_16BIT = 8,
	SOC_VIDEO_YUV422_8BIT = 9,
	SOC_VIDEO_YUV422_10BIT = 10,
	SOC_VIDEO_YUV422_12BIT = 11,
	SOC_VIDEO_YUV422_16BIT = 12,
};

enum soc_dp_ref_clk {
	SOC_DP_REF_CLK_24M = 24000,
	SOC_DP_REF_CLK_50M = 50000,
};

static const struct soc_dp_link_config {
	enum soc_dp_link_rate rate;
	enum soc_dp_lane_count lanes;
} soc_dp_link_priority_table[] = {
	/* --- Tier 1: Low Bandwidth (< 4 Gbps) --- */
	{SOC_DP_LINK_RATE_1_62, SOC_DP_LANE_1}, /* 1.62 Gbps */
	{SOC_DP_LINK_RATE_2_70, SOC_DP_LANE_1}, /* 2.70 Gbps */
	{SOC_DP_LINK_RATE_1_62, SOC_DP_LANE_2}, /* 3.24 Gbps */

	/* --- Tier 2: Medium Bandwidth (~5-6 Gbps) --- */
	{SOC_DP_LINK_RATE_2_70, SOC_DP_LANE_2}, /* 5.40 Gbps */
	{SOC_DP_LINK_RATE_5_40, SOC_DP_LANE_1}, /* 5.40 Gbps */
	{SOC_DP_LINK_RATE_1_62, SOC_DP_LANE_4}, /* 6.48 Gbps */

	/* --- Tier 3: High Bandwidth (~10 Gbps) --- */
	{SOC_DP_LINK_RATE_2_70, SOC_DP_LANE_4}, /* 10.8 Gbps */
	{SOC_DP_LINK_RATE_5_40, SOC_DP_LANE_2}, /* 10.8 Gbps */

	/* --- Tier 4: Ultra High Bandwidth (> 17 Gbps) --- */
	{SOC_DP_LINK_RATE_5_40, SOC_DP_LANE_4}, /* 21.6 Gbps */
};

static const struct soc_format_info {
	uint8_t bpp; /* Bits Per Pixel */
} format_info_table[] = {
	[SOC_VIDEO_RGB_6BIT]      = { .bpp = 18 },
	[SOC_VIDEO_RGB_8BIT]      = { .bpp = 24 },
	[SOC_VIDEO_RGB_10BIT]     = { .bpp = 30 },
	[SOC_VIDEO_RGB_12BIT]     = { .bpp = 36 },
	[SOC_VIDEO_RGB_16BIT]     = { .bpp = 48 },

	[SOC_VIDEO_YUV444_8BIT]   = { .bpp = 24 },
	[SOC_VIDEO_YUV444_10BIT]  = { .bpp = 30 },
	[SOC_VIDEO_YUV444_12BIT]  = { .bpp = 36 },
	[SOC_VIDEO_YUV444_16BIT]  = { .bpp = 48 },

	[SOC_VIDEO_YUV422_8BIT]   = { .bpp = 16 },
	[SOC_VIDEO_YUV422_10BIT]  = { .bpp = 20 },
	[SOC_VIDEO_YUV422_12BIT]  = { .bpp = 24 },
	[SOC_VIDEO_YUV422_16BIT]  = { .bpp = 32 },
};

static int soc_dp_get_bpp(uint32_t format)
{
	if (format >= ARRAY_SIZE(format_info_table)) {
		pr_warn("DP: Invalid color format index %d, defaulting to RGB888\n", format);
		return 24;
	}
	return format_info_table[format].bpp;
}

#define SOC_DP_VCO_MIN_KHZ        1000000
#define SOC_DP_VCO_MAX_KHZ        3000000
#define SOC_DP_PLL_FRAC_MOD       16777216  /* 2^24 */
#define SOC_DP_PLL_ERR_TOLERANCE  10

/* Data structure for Core PLL results */
struct soc_dp_core_pll_cfg {
	uint32_t target_rate_kbps;
	uint32_t vco_freq_khz;
	uint8_t prediv;
	uint16_t fbdiv;
	uint32_t frac;
	uint8_t postdiv_reg;
	uint8_t frac_pd;
	uint8_t vcoclk_div8_en;
	uint8_t postdiv_en;
	uint8_t clk_16mdiv;
	uint32_t actual_rate_khz;
	bool valid;
};

static struct soc_dp_core_pll_cfg core_pll_cfg_table[] = {
	/* LinkRate 1.62Gbps */
	{
		.target_rate_kbps = 1620000,
		.vco_freq_khz     = 1620000,
		.prediv           = 0x02,
		.fbdiv            = 0x87,
		.frac             = 0x0,
		.postdiv_reg      = 0x0,
		.frac_pd          = 0x3,
		.vcoclk_div8_en   = 0x1,
		.postdiv_en       = 0x1,
		.clk_16mdiv       = 12,
		.actual_rate_khz  = 1620000,
		.valid            = true,
	},
	/* LinkRate 2.7Gbps */
	{
		.target_rate_kbps = 2700000,
		.vco_freq_khz     = 2700000,
		.prediv           = 0x02,
		.fbdiv            = 0xe1,
		.frac             = 0x0,
		.postdiv_reg      = 0x0,
		.frac_pd          = 0x3,
		.vcoclk_div8_en   = 0x1,
		.postdiv_en       = 0x1,
		.clk_16mdiv       = 21,
		.actual_rate_khz  = 2700000,
		.valid            = true,
	},
	/* LinkRate 5.4Gbps */
	{
		.target_rate_kbps = 5400000,
		.vco_freq_khz     = 2700000,
		.prediv           = 0x02,
		.fbdiv            = 0xe1,
		.frac             = 0x0,
		.postdiv_reg      = 0x0,
		.frac_pd          = 0x3,
		.vcoclk_div8_en   = 0x0,
		.postdiv_en       = 0x0,
		.clk_16mdiv       = 42,
		.actual_rate_khz  = 5400000,
		.valid            = true,
	},
};

/* Data structure for Pixel PLL results */
struct soc_dp_pixel_pll_cfg {
	uint32_t target_pclk_khz;
	uint32_t vco_freq_khz;
	uint8_t prediv;
	uint16_t fbdiv;
	uint32_t frac_pd;
	uint32_t frac;
	uint8_t div5_en;
	uint8_t divm;
	uint8_t divaux;
	uint8_t divp;
	uint32_t actual_pclk_khz;
	bool valid;
};

static const struct soc_dp_pixel_pll_cfg pixel_pll_cfg_table[] = {
	{ 614400, 2460000, 0x05, 512,  0x3, 0x0, 0x0, 0x1, 0x01, 0x1, 614400, true },
	{ 594000, 2376000, 0x01, 99,   0x3, 0x0, 0x0, 0x1, 0x01, 0x1, 594000, true },
	{ 443250, 1770000, 0x08, 591,  0x3, 0x0, 0x0, 0x1, 0x01, 0x1, 443250, true },
	{ 375000, 3000000, 0x01, 125,  0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 375000, true },
	{ 348500, 2790000, 0x06, 697,  0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 348500, true },
	{ 307200, 1540000, 0x01, 64,   0x3, 0x0, 0x1, 0x0, 0x00, 0x0, 307200, true },
	{ 297000, 2376000, 0x01, 99,   0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 297000, true },
	{ 280000, 1676000, 0x01, 70,   0x3, 0x0, 0x0, 0xa, 0x01, 0x1, 280000, true },
	{ 277440, 2770000, 0x05, 578,  0x3, 0x0, 0x0, 0x3, 0x01, 0x1, 277440, true },
	{ 245760, 2457600, 0x05, 512,  0x3, 0x0, 0x0, 0x3, 0x01, 0x1, 245760, true },
	{ 241500, 1932000, 0x02, 161,  0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 241500, true },
	{ 204800, 2048000, 0x03, 256,  0x3, 0x0, 0x0, 0x3, 0x01, 0x1, 204800, true },
	{ 193250, 2320000, 0x08, 773,  0x3, 0x0, 0x0, 0x0, 0x06, 0x1, 193250, true },
	{ 162000, 2592000, 0x01, 108,  0x3, 0x0, 0x0, 0x0, 0x08, 0x1, 162000, true },
	{ 156000, 2810000, 0x01, 117,  0x3, 0x0, 0x0, 0x0, 0x09, 0x1, 156000, true },
	{ 150000, 3000000, 0x01, 125,  0x3, 0x0, 0x0, 0x0, 0x0a, 0x1, 150000, true },
	{ 148500, 2376000, 0x01, 99,   0x3, 0x0, 0x0, 0x0, 0x08, 0x1, 148500, true },
	{ 146000, 1750000, 0x01, 73,   0x3, 0x0, 0x0, 0x0, 0x06, 0x1, 146000, true },
	{ 142860, 2860000, 0x14, 2381, 0x3, 0x0, 0x0, 0x0, 0x0a, 0x1, 142860, true },
	{ 140000, 2520000, 0x01, 105,  0x3, 0x0, 0x0, 0x0, 0x09, 0x1, 140000, true },
	{ 138500, 2220000, 0x03, 277,  0x3, 0x0, 0x0, 0x0, 0x08, 0x1, 138500, true },
	{ 122000, 2930000, 0x01, 122,  0x3, 0x0, 0x0, 0x0, 0x0c, 0x1, 122000, true },
	{ 108000, 2810000, 0x01, 117,  0x3, 0x0, 0x0, 0x0, 0x0d, 0x1, 108000, true },
	{ 106500, 1700000, 0x01,  71,  0x3, 0x0, 0x0, 0x0, 0x08, 0x1, 106500, true },
	{ 83500,  2000000, 0x02, 167,  0x3, 0x0, 0x0, 0x0, 0x0c, 0x1, 83500,  true },
	{ 79500,  2540000, 0x01, 106,  0x3, 0x0, 0x0, 0x0, 0x10, 0x1, 79500,  true },
	{ 75000,  3000000, 0x01, 125,  0x3, 0x0, 0x0, 0x0, 0x14, 0x1, 75000,  true },
	{ 74250,  2376000, 0x01,  99,  0x3, 0x0, 0x0, 0x0, 0x10, 0x1, 74250,  true },
	{ 65000,  1560000, 0x01,  65,  0x3, 0x0, 0x0, 0x0, 0x0c, 0x1, 65000,  true },
	{ 40000,  2880000, 0x01, 120,  0x3, 0x0, 0x0, 0x0, 0x12, 0x2, 40000,  true },
	{ 27000,  2810000, 0x01, 117,  0x3, 0x0, 0x0, 0x0, 0x1a, 0x2, 27000,  true },
	{ 25600,  2300000, 0x01,  96,  0x3, 0x0, 0x0, 0x0, 0x0f, 0x3, 25600,  true },
	{ 25200,  2520000, 0x01, 105,  0x3, 0x0, 0x0, 0x0, 0x19, 0x2, 25200,  true },
};

struct soc_dp_dev {
	struct device *dev;

	struct drm_device *drm;
	struct drm_encoder encoder;
	struct drm_connector connector;

	enum drm_connector_status connector_status;
	struct drm_display_mode mode;

	void __iomem *regs;
	struct drm_dp_aux aux;

	/* Buffer to store raw DPCD data */
	uint8_t dpcd[DP_RECEIVER_CAP_SIZE];

	/* Structure to store negotiated link parameters */
	struct {
		uint8_t revision;
		uint8_t enhanced_framing;
		uint32_t max_rate;
		uint32_t max_num_lanes;
	} link;

	struct reset_control *reset;
	struct clk *pxclk;

	u32 gpio_bl;
	u32 gpio_power;
	u32 gpio_enable;

	bool edp_mode;
	bool use_ext_pixel_clock;
	int pixel_clock;

	uint32_t ref;
	uint32_t color_format;

#ifdef CONFIG_SOC_DP_DRIVER_QEMU
	struct proc_dir_entry *proc_irq;
#endif

#if HOT_PLUG_THREAD_ENABLED
	struct delayed_work hpd_work;
#else
	int irq;
#endif
};

static int soc_dp_reg_write(struct soc_dp_dev *dp,
		uint32_t offset, uint32_t bit_wide, uint32_t mask, uint32_t val)
{
	uint32_t reg_val;

	reg_val = (uint32_t)readl((char *)dp->regs + offset);
	reg_val &= ~mask;
	reg_val |= val & mask;
	DRM_DEBUG("%s() [W] 0x%x 0x%x\n", __func__, offset, reg_val);
	writel(reg_val, (char *)dp->regs + offset);

	return 0;
}

static int soc_dp_reg_read(struct soc_dp_dev *dp,
		uint32_t offset, uint32_t bit_wide, uint32_t mask, uint32_t *val)
{
	*val = ((uint32_t)readl((char *)dp->regs + offset)) & mask;
	DRM_DEBUG("%s() [R] 0x%x 0x%x\n", __func__, offset, *val);

	return 0;
}

static int soc_dp_reg_write_range(struct soc_dp_dev *dp,
		uint32_t offset, uint32_t high, uint32_t low, uint32_t val)
{
	uint32_t mask;

	mask = (uint32_t)(((((uint64_t)1) << (high - low + 1)) - 1) << low);
	return soc_dp_reg_write(dp, offset, 32, mask, (val << low) & mask);
}

static int soc_dp_reg_read_range(struct soc_dp_dev *dp,
		uint32_t offset, uint32_t high, uint32_t low, uint32_t *val)
{
	int ret;
	uint32_t mask;

	mask = (uint32_t)(((((uint64_t)1) << (high - low + 1)) - 1) << low);
	ret = soc_dp_reg_read(dp, offset, 32, mask, val);
	*val = *val >> low;

	return ret;
}

static uint32_t soc_dp_aux_get_cmd(struct drm_dp_aux_msg *msg)
{
	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
		return msg->request;
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		return msg->request;
	default:
		return 0;
	}

	return 0;
}

static ssize_t soc_dp_aux_transfer(struct drm_dp_aux *aux,
				   struct drm_dp_aux_msg *msg)
{
	int ret, i;
	unsigned long timeout;

	struct soc_dp_dev *dp = container_of(aux, struct soc_dp_dev, aux);
	uint32_t cmd, len, val, status;
	uint32_t data[4] = {0};
	uint8_t *buf = msg->buffer;
	bool is_read = (msg->request & DP_AUX_I2C_READ) ||
		((msg->request & DP_AUX_NATIVE_READ) == DP_AUX_NATIVE_READ);

	/* 1. Check message validity */
	if (msg->size > 16)
		return -EINVAL;

	cmd = soc_dp_aux_get_cmd(msg);

	/* 2. Prepare Data for Write (if applicable) */
	if (!is_read) {
		/* Pack bytes into 32-bit words (Little Endian packing) */
		for (i = 0; i < msg->size; i++) {
			data[i / 4] |= buf[i] << ((i % 4) * 8);
		}

		/* Write data to registers: DATA1(LSB)..DATA4(MSB) */
		soc_dp_reg_write_range(dp, SOC_DPTX_AUX_DATA1, data[0]);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUX_DATA2, data[1]);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUX_DATA3, data[2]);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUX_DATA4, data[3]);
	}

	/* 3. Configure Command, Address, Length */
	/* HW expects Length - 1 */
	len = msg->size > 0 ? msg->size - 1 : 0;

	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_LENGTH, len);
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_ADDR, msg->address);
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_CMD_TYPE, cmd);

	/* 4. Trigger Transfer */
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_START, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_START, 1);

	/* 5. Wait for Completion */
	timeout = jiffies + msecs_to_jiffies(200);
	ret = -ETIMEDOUT;

	while (1) {
		soc_dp_reg_read_range(dp, SOC_DPTX_AUX_REPLY_EVENT_INT_STA, &val);
		if (val) {
			ret = 0;
			break;
		}
		if (time_after(jiffies, timeout))
			break;
		usleep_range(100, 110);
	}

	if (ret) {
		dev_err(dp->dev, "AUX transfer timeout\n");
		return ret;
	}

	/* 6. Clear Interrupt Status (W1C) */
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_REPLY_EVENT_INT_STA, 1);

	/* 7. Read Status */
	soc_dp_reg_read_range(dp, SOC_DPTX_AUX_STATUS, &status);

	/* Map HW status to DRM reply codes */
	switch (status) {
	case 0: /* ACK */
		msg->reply = DP_AUX_NATIVE_REPLY_ACK;
		break;
	case 1: /* NACK */
		msg->reply = DP_AUX_NATIVE_REPLY_NACK;
		return 0; /* Standard says return 0 on NACK for upper layer retry */
	case 2: /* DEFER */
		msg->reply = DP_AUX_NATIVE_REPLY_DEFER;
		return 0;
	default:
		/* Check error code if status is weird */
		soc_dp_reg_read_range(dp, SOC_DPTX_AUX_REPLY_ERR_CODE, &val);
		dev_err(dp->dev, "AUX error, status: 0x%x, code: 0x%x\n", status, val);
		return -EIO;
	}

	/* 8. Read Data (if Read operation and ACK) */
	if (is_read && msg->size > 0) {
		soc_dp_reg_read_range(dp, SOC_DPTX_AUX_DATA1, &data[0]);
		soc_dp_reg_read_range(dp, SOC_DPTX_AUX_DATA2, &data[1]);
		soc_dp_reg_read_range(dp, SOC_DPTX_AUX_DATA3, &data[2]);
		soc_dp_reg_read_range(dp, SOC_DPTX_AUX_DATA4, &data[3]);

		/* Unpack 32-bit words back to bytes */
		for (i = 0; i < msg->size; i++) {
			buf[i] = (data[i / 4] >> ((i % 4) * 8)) & 0xFF;
		}
	}

	return msg->size;
}

static void soc_dp_aux_init(struct soc_dp_dev *dp)
{
	DRM_INFO("%s() \n", __func__);

	dp->aux.name = "soc-dp-aux";
	dp->aux.dev = dp->dev;
	dp->aux.drm_dev = dp->drm;
	dp->aux.transfer = soc_dp_aux_transfer;

	drm_dp_aux_register(&dp->aux);
}

static const struct soc_dp_pixel_pll_cfg* find_pixel_pll_cfg(uint32_t pclk_khz) {
	const struct soc_dp_pixel_pll_cfg *best_match = NULL;
	uint32_t min_diff = 0xFFFFFFFF;
	int num_configs = sizeof(pixel_pll_cfg_table) / sizeof(pixel_pll_cfg_table[0]);

	for (int i = 0; i < num_configs; i++) {
		uint32_t current_target = pixel_pll_cfg_table[i].target_pclk_khz;
		uint32_t diff = (pclk_khz > current_target) ? (pclk_khz - current_target) : (current_target - pclk_khz);

		if (diff == 0) {
			return &pixel_pll_cfg_table[i];
		}

		if ((pclk_khz / 100) == (current_target / 100)) {
			return &pixel_pll_cfg_table[i];
		}

		if (diff < min_diff && diff < 2000) {
			min_diff = diff;
			best_match = &pixel_pll_cfg_table[i];
		}
	}

	return best_match;

}

static int update_edp_config(struct soc_dp_dev *dp, bool enable)
{
	uint8_t value;
	int ret;

	ret = drm_dp_dpcd_read(&dp->aux, DP_EDP_CONFIGURATION_SET, &value, 1);
	if (ret < 0) {
		dev_err(dp->dev, "Failed to read DP_EDP_CONFIGURATION_SET, ret: %d\n", ret);
		return ret;
	}

	if (enable)
		value |= 0x01;
	else
		value &= ~0x01;

	ret = drm_dp_dpcd_write(&dp->aux, DP_EDP_CONFIGURATION_SET, &value, 1);
	if (ret < 0) {
		dev_err(dp->dev, "Failed to write DP_EDP_CONFIGURATION_SET, ret: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * Read the Sink's DPCD capability information.
 * Note: EDID is parsed separately. This function focuses solely on
 * Link Layer capabilities (Rate, Lanes, etc.).
 */
static int soc_dp_hw_read_sink_caps(struct soc_dp_dev *dp)
{
#ifdef CONFIG_SOC_DP_DRIVER_QEMU
	dp->link.revision = 0x14; // DP 1.4
	dp->link.max_rate = SOC_DP_LINK_RATE_5_40;
	dp->link.max_num_lanes = SOC_DP_LANE_4;
	dp->link.enhanced_framing = 1;
#else
	ssize_t ret;
	uint8_t max_bw;

	/* 1. Read DPCD Receiver Capability fields (0x00000 - 0x0000F) */
	ret = drm_dp_dpcd_read(&dp->aux, DP_DPCD_REV, dp->dpcd, DP_RECEIVER_CAP_SIZE);
	if (ret < 0) {
		dev_err(dp->dev, "Failed to read DPCD: %zd\n", ret);
		return ret;
	}

	/* 2. Parse DP Revision */
	dp->link.revision = dp->dpcd[DP_DPCD_REV];

	/*
	 * 3. Parse and determine Link Rate.
	 * Get the maximum link rate supported by the Sink.
	 * Note: During link training, we usually start from min(Sink_Max, Source_Max).
	 */
	max_bw = dp->dpcd[DP_MAX_LINK_RATE];
	switch (max_bw) {
	case DP_LINK_BW_1_62:
		dp->link.max_rate = SOC_DP_LINK_RATE_1_62;
		break;
	case DP_LINK_BW_2_7:
		dp->link.max_rate = SOC_DP_LINK_RATE_2_70;
		break;
	case DP_LINK_BW_5_4:
		dp->link.max_rate = SOC_DP_LINK_RATE_5_40;
		break;
	case DP_LINK_BW_8_1:
		dp->link.max_rate = SOC_DP_LINK_RATE_8_10;
		break;
	default:
		dev_warn(dp->dev, "Unknown DPCD Max Rate: 0x%x, defaulting to 1.62G\n", max_bw);
		dp->link.max_rate = SOC_DP_LINK_RATE_1_62;
		break;
	}

	/* 4. Parse and determine Lane Count */
	dp->link.max_num_lanes = dp->dpcd[DP_MAX_LANE_COUNT] & DP_MAX_LANE_COUNT_MASK;

	/* 5. Check for Enhanced Framing support */
	dp->link.enhanced_framing =
		(dp->dpcd[DP_MAX_LANE_COUNT] & DP_ENHANCED_FRAME_CAP);
#endif

	dev_info(dp->dev, "DPCD: Rev %x.%x, MaxRate %d kHz, MaxLanes %d, EnhFrame %d\n",
		 dp->link.revision >> 4, dp->link.revision & 0xF,
		 dp->link.max_rate,
		 dp->link.max_num_lanes,
		 dp->link.enhanced_framing);

	return 0;
}

static int soc_dp_check_pll_lock(struct soc_dp_dev *dp)
{
	uint32_t pll_locked;

#ifndef CONFIG_SOC_DP_DRIVER_QEMU
	soc_dp_reg_read_range(dp, SOC_DPTX_AD_LOCK_PIXELPLL, &pll_locked);
#else
	pll_locked = 1;
#endif
	if (pll_locked) {
		dev_info(dp->dev, "Pre_pll locked.\n");
	} else {
		dev_err(dp->dev, "Pre_pll unlocked.\n");
		return -EINVAL;
	}

#ifndef CONFIG_SOC_DP_DRIVER_QEMU
	soc_dp_reg_read_range(dp, SOC_DPTX_AD_LOCK_COREPLL, &pll_locked);
#else
	pll_locked = 1;
#endif
	if (pll_locked) {
		dev_info(dp->dev, "Post_pll locked.\n");
	} else {
		dev_err(dp->dev, "Post_pll unlocked.\n");
		return -EINVAL;
	}

	return 0;
}

static void soc_dp_calc_core_pll_to_reg(struct soc_dp_dev *dp, struct soc_dp_core_pll_cfg *cfg)
{
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_PD, 1);
	mdelay(2);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_PREDIV, cfg->prediv);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_FBDIV_LBIT, cfg->fbdiv & 0xFF);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_FBDIV_HBIT, (cfg->fbdiv >> 8) & 0xF);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_DACPD, (cfg->frac_pd >> 1) & 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_DSMPD, cfg->frac_pd & 0x1);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_FRAC_LBIT, cfg->frac & 0xFF);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_FRAC_MBIT, (cfg->frac >> 8) & 0xFF);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_FRAC_HBIT, (cfg->frac >> 16) & 0xFF);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_POSTDIV, cfg->postdiv_reg);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_POSTDIVEN, cfg->postdiv_en);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_VCOCLK_DIV8_EN, cfg->vcoclk_div8_en);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_CLKDIV_16M, cfg->clk_16mdiv);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_LOCK_BYPEN, 1);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_PD, 0);
	mdelay(2);
}

static void soc_dp_calc_pixel_pll_to_reg(struct soc_dp_dev *dp, const struct soc_dp_pixel_pll_cfg *cfg)
{
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PD, 1);
	mdelay(2);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PREDIV, cfg->prediv);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_FBDIV2_LBIT, cfg->fbdiv & 0xFF);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_FBDIV2_HBIT, (cfg->fbdiv >> 8) & 0xF);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_DACPD, (cfg->frac_pd >> 1) & 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_DSMPD, cfg->frac_pd & 0x1);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_FRAC2_LBIT, cfg->frac & 0xFF);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_FRAC2_MBIT, (cfg->frac >> 8) & 0xFF);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_FRAC2_HBIT, (cfg->frac >> 16) & 0xFF);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PRECLK_DIVM, cfg->divm);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PRECLK_DIVAUX, cfg->divaux);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PCLKDIV5_EN, cfg->div5_en);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PCLK_DIVAUX, cfg->divp);

	soc_dp_reg_write_range(dp, SOC_DPTX_REG_PCLK_OUTPUT_NORMAL, 1);
	mdelay(2);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PD, 0);
	mdelay(2);
}

static int soc_dp_hw_set_pll(struct soc_dp_dev *dp, enum soc_dp_link_rate rate, uint32_t pclk)
{
	const struct soc_dp_pixel_pll_cfg *pixel_pll_cfg;

	dev_info(dp->dev, "Setting PLL to Rate %d kHz, Pclk %d kHz\n", rate, pclk);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_DP_EN, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_HDMI_EN, 0);

	if (rate == SOC_DP_LINK_RATE_1_62) {
		soc_dp_calc_core_pll_to_reg(dp, &core_pll_cfg_table[0]);
	} else if (rate == SOC_DP_LINK_RATE_2_70 ) {
		soc_dp_calc_core_pll_to_reg(dp, &core_pll_cfg_table[1]);
	} else if (rate == SOC_DP_LINK_RATE_5_40 ) {
		soc_dp_calc_core_pll_to_reg(dp, &core_pll_cfg_table[2]);
	} else {
		dev_err(dp->dev, "Unsupported link rate %d\n", rate);
		return -EINVAL;
	}

	pixel_pll_cfg = find_pixel_pll_cfg(pclk);
	if (pixel_pll_cfg) {
		soc_dp_calc_pixel_pll_to_reg(dp, pixel_pll_cfg);
	} else {
		dev_err(dp->dev, "Unsupported pixel clock %d\n", pclk);
		return -EINVAL;
	}

	return 0;
}

static void soc_dp_phy_config_lane_count(struct soc_dp_dev *dp, enum soc_dp_lane_count lanes)
{
	uint32_t phy_lanes_val;

	switch (lanes) {
	case SOC_DP_LANE_1:
		phy_lanes_val = 0;   /* Register value for 1 Lane */
		break;
	case SOC_DP_LANE_2:
		phy_lanes_val = 1;   /* Register value for 2 Lanes */
		break;
	case SOC_DP_LANE_4:
	default:
		phy_lanes_val = 2;   /* Register value for 4 Lanes */
		break;
	}

	dev_info(dp->dev, "Configuring PHY Lane Count: %d (Reg: %d)\n",
		 lanes, phy_lanes_val);

	/* Set the number of active lanes */
	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_NUM_LANES, phy_lanes_val);
}

static void soc_dp_phy_enable_lanes(struct soc_dp_dev *dp, enum soc_dp_lane_count lanes)
{
	uint32_t lane_en;

	switch (lanes) {
	case SOC_DP_LANE_1:
		lane_en = 0x1;       /* Enable Lane 0 */
		break;
	case SOC_DP_LANE_2:
		lane_en = 0x3;       /* Enable Lane 0, 1 */
		break;
	case SOC_DP_LANE_4:
	default:
		lane_en = 0xF;       /* Enable Lane 0, 1, 2, 3 */
		break;
	}

	dev_info(dp->dev, "Enabling PHY Transmitters: Mask 0x%x\n", lane_en);

	/* Enable Transmitters for the selected lanes */
	soc_dp_reg_write_range(dp, SOC_DPTX_XMIT_ENABLE, lane_en);
}

/*
 * Check Hot Plug Detect (HPD) Status
 */
static enum drm_connector_status soc_dp_hw_detect_hpd(struct soc_dp_dev *dp)
{
#if defined(CONFIG_SOC_DP_DRIVER_QEMU) || HPD_BYPASS
	/* QEMU Environment: Always simulate as Connected */
	return connector_status_connected;
#else
	uint32_t hpd_status;
	enum drm_connector_status connector_status = connector_status_disconnected;

	soc_dp_reg_read_range(dp, SOC_DPTX_HPD_IN_STATUS, &hpd_status);
	if (hpd_status)
		connector_status = connector_status_connected;
	else
		connector_status = connector_status_disconnected;

	return connector_status;
#endif
}

/*
 * Clean Hot Plug Detect (HPD) Status
 */
static void soc_dp_hw_clean_hpd(struct soc_dp_dev *dp)
{
#if defined(CONFIG_SOC_DP_DRIVER_QEMU) || HPD_BYPASS
	return;
#else
	uint32_t plug_event, unplug_event;

	soc_dp_reg_read_range(dp, SOC_DPTX_HOT_PLUG_EVENT, &plug_event);
	soc_dp_reg_read_range(dp, SOC_DPTX_HOT_UNPLUG_EVENT, &unplug_event);

	if (plug_event)
		soc_dp_reg_write_range(dp, SOC_DPTX_HOT_PLUG_EVENT, 0x1);

	if (unplug_event)
		soc_dp_reg_write_range(dp, SOC_DPTX_HOT_UNPLUG_EVENT, 0x1);
#endif
}

/* Mappings for PHY Swing/Emphasis Levels */
static const uint32_t phy_swing_map[] = { 0x0, 0x1, 0x2, 0x3 };
static const uint32_t phy_preemp_map[] = { 0x0, 0x1, 0x2, 0x3 };

static void soc_dp_phy_set_lane_settings(struct soc_dp_dev *dp,
					 uint8_t training_set[4])
{
	int i;
	uint32_t swing, preemp;

	for (i = 0; i < 4; i++) {
		swing = training_set[i] & DP_TRAIN_VOLTAGE_SWING_MASK;
		preemp = (training_set[i] & DP_TRAIN_PRE_EMPHASIS_MASK) >> DP_TRAIN_PRE_EMPHASIS_SHIFT;

		swing = phy_swing_map[swing];
		preemp = phy_preemp_map[preemp];

		switch (i) {
		case 0:
			soc_dp_reg_write_range(dp, SOC_DPTX_PHY_LANE0_TX_VSWING, swing);
			soc_dp_reg_write_range(dp, SOC_DPTX_PHY_LANE0_TX_PREEMP, preemp);
			break;
		case 1:
			soc_dp_reg_write_range(dp, SOC_DPTX_PHY_LANE1_TX_VSWING, swing);
			soc_dp_reg_write_range(dp, SOC_DPTX_PHY_LANE1_TX_PREEMP, preemp);
			break;
		case 2:
			soc_dp_reg_write_range(dp, SOC_DPTX_PHY_LANE2_TX_VSWING, swing);
			soc_dp_reg_write_range(dp, SOC_DPTX_PHY_LANE2_TX_PREEMP, preemp);
			break;
		case 3:
			soc_dp_reg_write_range(dp, SOC_DPTX_PHY_LANE3_TX_VSWING, swing);
			soc_dp_reg_write_range(dp, SOC_DPTX_PHY_LANE3_TX_PREEMP, preemp);
			break;
		}
	}
}

static int soc_dp_set_training_pattern(struct soc_dp_dev *dp, uint8_t pattern)
{
	uint32_t tps_sel = 0;
	uint8_t dpcd_pattern = pattern;
	int ret;

	if (pattern != DP_TRAINING_PATTERN_DISABLE)
		dpcd_pattern |= DP_LINK_SCRAMBLING_DISABLE;

	/* Configure PHY Pattern */
	switch (pattern) {
	case DP_TRAINING_PATTERN_DISABLE:
		tps_sel = 0;
		soc_dp_reg_write_range(dp, SOC_DPTX_SCRAMBLER_DISABLE, 0);
		break;
	case DP_TRAINING_PATTERN_1:
		tps_sel = 1;
		soc_dp_reg_write_range(dp, SOC_DPTX_SCRAMBLER_DISABLE, 1);
		break;
	case DP_TRAINING_PATTERN_2:
		tps_sel = 2;
		soc_dp_reg_write_range(dp, SOC_DPTX_SCRAMBLER_DISABLE, 1);
		break;
	case DP_TRAINING_PATTERN_3:
		tps_sel = 3;
		soc_dp_reg_write_range(dp, SOC_DPTX_SCRAMBLER_DISABLE, 1);
		break;
	default:
		dev_err(dp->dev, "Unsupported training pattern: 0x%x\n", pattern);
		return -EINVAL;
	}

	soc_dp_reg_write_range(dp, SOC_DPTX_TPS_SEL, tps_sel);

#ifndef CONFIG_SOC_DP_DRIVER_QEMU
	/* Configure DPCD Pattern */
	ret = drm_dp_dpcd_writeb(&dp->aux, DP_TRAINING_PATTERN_SET, dpcd_pattern);
	if (ret < 0) {
		dev_err(dp->dev, "Failed to set DPCD training pattern: %d\n", ret);
		return ret;
	}
#else
	ret = 0;
	return ret;
#endif

	return 0;
}

/*
 * Configure PHY Rate Register
 * This must be called before Link Training.
 */
static void soc_dp_hw_config_phy_rate(struct soc_dp_dev *dp, enum soc_dp_link_rate rate)
{
	uint32_t rate_val = 0;

	switch (rate) {
	case SOC_DP_LINK_RATE_1_62:
		rate_val = 0;
		break;
	case SOC_DP_LINK_RATE_2_70:
		rate_val = 1;
		break;
	case SOC_DP_LINK_RATE_5_40:
		rate_val = 2;
		break;
	case SOC_DP_LINK_RATE_8_10:
		rate_val = 3;
		break;
	default:
		dev_err(dp->dev, "Invalid Link Rate: %d\n", rate);
		rate_val = 0;
		break;
	}

	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_RATE, rate_val);
}

static int soc_dp_link_train_clock_recovery(struct soc_dp_dev *dp, enum soc_dp_link_rate rate, enum soc_dp_lane_count lanes)
{
	uint8_t link_status[DP_LINK_STATUS_SIZE];
	uint8_t training_set[4] = {0};
	int retries = 0;
	int i, ret;

	soc_dp_phy_set_lane_settings(dp, training_set);

#ifndef CONFIG_SOC_DP_DRIVER_QEMU
	ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET,
				training_set, lanes);
	if (ret < 0)
		return ret;
#endif

	ret = soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_1);
	if (ret < 0) {
		soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
		return ret;
	}

	while (retries < 8) {
#ifndef CONFIG_SOC_DP_DRIVER_QEMU
		drm_dp_link_train_clock_recovery_delay(&dp->aux, dp->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0) {
			soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
			return ret;
		}

		if (drm_dp_clock_recovery_ok(link_status, lanes))
#else
		if (1)
#endif
			return 0;

		/* Update settings based on Sink request */
		for (i = 0; i < lanes; i++) {
			uint8_t v = drm_dp_get_adjust_request_voltage(link_status, i);
			uint8_t p = drm_dp_get_adjust_request_pre_emphasis(link_status, i);

			if (v >= SOC_DP_SWING_MAX) {
				v = SOC_DP_SWING_MAX;
				v |= DP_TRAIN_MAX_SWING_REACHED;
			}

			if (p >= SOC_DP_PREEMP_MAX) {
				p = SOC_DP_PREEMP_MAX;
				v |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
			}

			training_set[i] = v | (p << DP_TRAIN_PRE_EMPHASIS_SHIFT);
		}

		soc_dp_phy_set_lane_settings(dp, training_set);

		ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET,
					training_set, lanes);
		if (ret < 0) {
			soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
			return ret;
		}

		retries++;
	}

	dev_err(dp->dev, "Link Training Clock Recovery Failed\n");
	soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
	return -ETIMEDOUT;
}

static int soc_dp_link_train_channel_eq(struct soc_dp_dev *dp, enum soc_dp_link_rate rate, enum soc_dp_lane_count lanes)
{
	uint8_t link_status[DP_LINK_STATUS_SIZE];
	uint8_t training_set[4] = {0};
	int retries = 0;
	int i, ret;

	/* Use TPS2 for EQ phase */
	ret = soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_2);
	if (ret < 0) {
		soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
		return ret;
	}

	while (retries < 8) {
#ifndef CONFIG_SOC_DP_DRIVER_QEMU
		drm_dp_link_train_channel_eq_delay(&dp->aux, dp->dpcd);

		ret = drm_dp_dpcd_read_link_status(&dp->aux, link_status);
		if (ret < 0) {
			soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
			return ret;
		}

		if (drm_dp_channel_eq_ok(link_status, lanes)) {
#else
		if (1) {
#endif
			soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
			return 0;
		}

		/* Update settings based on Sink request */
		for (i = 0; i < lanes; i++) {
			uint8_t v = drm_dp_get_adjust_request_voltage(link_status, i);
			uint8_t p = drm_dp_get_adjust_request_pre_emphasis(link_status, i);

			if (v >= SOC_DP_SWING_MAX) {
				v = SOC_DP_SWING_MAX;
				v |= DP_TRAIN_MAX_SWING_REACHED;
			}

			if (p >= SOC_DP_PREEMP_MAX) {
				p = SOC_DP_PREEMP_MAX;
				v |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
			}

			training_set[i] = v | (p << DP_TRAIN_PRE_EMPHASIS_SHIFT);
		}

		soc_dp_phy_set_lane_settings(dp, training_set);

		ret = drm_dp_dpcd_write(&dp->aux, DP_TRAINING_LANE0_SET,
					training_set, lanes);
		if (ret < 0) {
			soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
			return ret;
		}

		retries++;
	}

	dev_err(dp->dev, "Link Training Channel EQ Failed\n");
	soc_dp_set_training_pattern(dp, DP_TRAINING_PATTERN_DISABLE);
	return -ETIMEDOUT;
}

/*
 * Main Link Training Function
 */
static int soc_dp_link_train(struct soc_dp_dev *dp, enum soc_dp_link_rate rate, enum soc_dp_lane_count lanes)
{
	int ret;
	uint8_t link_config[2];
	uint8_t bw_code;

	/* Map Link Rate Enum to DPCD Bandwidth Code */
	switch (rate) {
	case SOC_DP_LINK_RATE_1_62:
		bw_code = DP_LINK_BW_1_62;
		break;
	case SOC_DP_LINK_RATE_2_70:
		bw_code = DP_LINK_BW_2_7;
		break;
	case SOC_DP_LINK_RATE_5_40:
		bw_code = DP_LINK_BW_5_4;
		break;
	case SOC_DP_LINK_RATE_8_10:
		bw_code = DP_LINK_BW_8_1;
		break;
	default:
		bw_code = DP_LINK_BW_1_62;
		break;
	}

	/* Configure DPCD Link Rate and Lane Count */
	link_config[0] = bw_code;
	link_config[1] = lanes;
	if (dp->link.enhanced_framing)
		link_config[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

#ifndef CONFIG_SOC_DP_DRIVER_QEMU
	ret = drm_dp_dpcd_write(&dp->aux, DP_LINK_BW_SET, link_config, 2);
	if (ret < 0) {
		dev_err(dp->dev, "Failed to configure DPCD\n");
		return ret;
	}
#endif

	ret = soc_dp_link_train_clock_recovery(dp, rate, lanes);
	if (ret)
		return ret;

	ret = soc_dp_link_train_channel_eq(dp, rate, lanes);
	if (ret)
		return ret;

	return 0;
}

static void soc_dp_hw_set_msa_and_enable_video(struct soc_dp_dev *dp, const struct drm_display_mode *mode,
		enum soc_dp_link_rate rate, enum soc_dp_lane_count lanes)
{
	uint64_t hb_num;
	uint32_t link_rate;
	uint32_t fp; // Pixel clock in MHz
	uint32_t bpp, misc0;
	uint32_t tu, tu_frac, tu_int, rd_thres;
	uint32_t hsync_len;

	// 1. Prepare basic parameters
	// mode->clock unit is kHz, fp unit is MHz
	if (dp->use_ext_pixel_clock)
		fp = dp->pixel_clock / 1000;
	else
		fp = mode->clock / 1000;

	if (fp == 0) fp = 1; // Prevent division by zero

	// Get BPP
	bpp = soc_dp_get_bpp(dp->color_format);

	// Calculate MISC0
	// bit0: 0 (Sync Clock)
	// bits1-7: Color Format (000=RGB, 001=YCbCr422, 010=YCbCr444)
	// bits5-7: BPC (001=8bpc, 010=10bpc, etc)
	switch (dp->color_format) {
	case SOC_VIDEO_RGB_6BIT:      misc0 = 0x00; break;
	case SOC_VIDEO_RGB_8BIT:      misc0 = 0x20; break;
	case SOC_VIDEO_RGB_10BIT:     misc0 = 0x40; break;
	case SOC_VIDEO_RGB_12BIT:     misc0 = 0x60; break;
	case SOC_VIDEO_RGB_16BIT:     misc0 = 0x80; break;
	case SOC_VIDEO_YUV422_8BIT:   misc0 = 0x22; break;
	case SOC_VIDEO_YUV422_10BIT:  misc0 = 0x42; break;
	case SOC_VIDEO_YUV422_12BIT:  misc0 = 0x62; break;
	case SOC_VIDEO_YUV422_16BIT:  misc0 = 0x82; break;
	case SOC_VIDEO_YUV444_8BIT:   misc0 = 0x24; break;
	case SOC_VIDEO_YUV444_10BIT:  misc0 = 0x44; break;
	case SOC_VIDEO_YUV444_12BIT:  misc0 = 0x64; break;
	case SOC_VIDEO_YUV444_16BIT:  misc0 = 0x84; break;
	default:                      misc0 = 0x20; break;
	}

	// 2. Calculate HBlank Interval (hb_num)
	// (htotal - hactive) * (LinkSymbolClock / 4) / PixelClock
	// LinkSymbolClock = LinkRate * 100 (e.g., 1.62G -> 162MHz)
	// rate unit is kHz (e.g., 1620000)
	// link_rate = rate / 10000 (e.g., 162)
	link_rate = rate / 10000;

	// Formula: hb_num = hblank * (link_rate / 4) / fp
	// To avoid floating point arithmetic, multiply first then divide
	hb_num = (uint64_t)(mode->htotal - mode->hdisplay) * link_rate;
	do_div(hb_num, 4 * fp);

	// 3. Calculate TU (Transfer Unit)
	// tu = fp * bpp * 640 / (8 * num_cnt * link_rate)
	// Here link_rate also refers to 162, 270 etc.
#if ACTIVATE_DO_DIV
	{
		uint64_t temp_tu = (uint64_t)fp * bpp * 640;
		uint32_t den = 8 * lanes * link_rate;
		do_div(temp_tu, den);
		tu = temp_tu;
	}
#else
	tu = (uint64_t)fp * bpp * 640 / (8 * lanes * link_rate);
#endif
	tu_frac = tu % 10;
	tu_int  = tu / 10;

	// 4. Calculate FIFO read threshold
	if (tu_int < 6) {
		rd_thres = 32;
	} else if ((mode->htotal - mode->hdisplay) < 80) {
		rd_thres = 12;
	} else {
		rd_thres = 16;
	}

	dev_info(dp->dev, "MSA: %dx%d, Rate:%d kHz, Lanes:%d, BPP:%d, TU:%d.%d\n",
		 mode->hdisplay, mode->vdisplay, rate, lanes, bpp, tu_int, tu_frac);

	// 5. Video mapping format
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_MAPPING, dp->color_format);

	// Polarity configuration
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		soc_dp_reg_write_range(dp, SOC_DPTX_HSYNC_IN_POLARITY, 1);
	else
		soc_dp_reg_write_range(dp, SOC_DPTX_HSYNC_IN_POLARITY, 0);

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		soc_dp_reg_write_range(dp, SOC_DPTX_VSYNC_IN_POLARITY, 1);
	else
		soc_dp_reg_write_range(dp, SOC_DPTX_VSYNC_IN_POLARITY, 0);

	DRM_INFO("%s() hdisplay %d hsync_start %d hsync_end %d htotal %d dp pixel clock %d dpu pixel clock %d flags 0x%x\n", __func__, mode->hdisplay, mode->hsync_start, mode->hsync_end, mode->htotal, mode->clock, dp->pixel_clock, mode->flags);
	DRM_INFO("%s() vdisplay %d vsync_start %d vsync_end %d vtotal %d \n", __func__, mode->vdisplay, mode->vsync_start, mode->vsync_end, mode->vtotal);

	soc_dp_reg_write_range(dp, SOC_DPTX_HSYNC_IN_POLARITY, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_VSYNC_IN_POLARITY, 1);

	// Basic timing
	soc_dp_reg_write_range(dp, SOC_DPTX_HACTIVE, mode->hdisplay);
	soc_dp_reg_write_range(dp, SOC_DPTX_VACTIVE, mode->vdisplay);
	soc_dp_reg_write_range(dp, SOC_DPTX_HBLANK, mode->htotal - mode->hdisplay);
	soc_dp_reg_write_range(dp, SOC_DPTX_VBLANK, mode->vtotal - mode->vdisplay);

	soc_dp_reg_write_range(dp, SOC_DPTX_HSTART, mode->htotal - mode->hsync_end + (mode->hsync_end - mode->hsync_start));
	soc_dp_reg_write_range(dp, SOC_DPTX_VSTART, mode->vtotal - mode->vsync_end + (mode->vsync_end - mode->vsync_start));

	hsync_len = mode->hsync_end - mode->hsync_start;

	soc_dp_reg_write_range(dp, SOC_DPTX_H_SYNC_WIDTH, hsync_len);
	soc_dp_reg_write_range(dp, SOC_DPTX_V_SYNC_WIDTH, mode->vsync_end - mode->vsync_start);
	soc_dp_reg_write_range(dp, SOC_DPTX_H_FRONT_PORCH, mode->hsync_start - mode->hdisplay);
	soc_dp_reg_write_range(dp, SOC_DPTX_V_FRONT_PORCH, mode->vsync_start - mode->vdisplay);

	// MSA and MISC
	soc_dp_reg_write_range(dp, SOC_DPTX_MISC0, misc0);
	soc_dp_reg_write_range(dp, SOC_DPTX_MISC1, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_NVID, 0);

	// Link layer parameters
	soc_dp_reg_write_range(dp, SOC_DPTX_HBLANK_INTERVAL, (uint32_t)hb_num);
	soc_dp_reg_write_range(dp, SOC_DPTX_AVERAGE_BYTES_PER_TU, tu_int);
	soc_dp_reg_write_range(dp, SOC_DPTX_AVERAGE_BYTES_PER_TU_FRAC, tu_frac);
	soc_dp_reg_write_range(dp, SOC_DPTX_INIT_THRESHOLD, rd_thres);

	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_SSC_DIS, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_REG_VID_CLK_SEL, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_VID_BIST_EN, 0);

	// 6. Enable video stream
	dev_info(dp->dev, "Enabling Video Stream...\n");
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_STREAM_ENABLE, 1);
}

static void soc_dp_hw_disable(struct soc_dp_dev *dp)
{
	dev_info(dp->dev, "Disabling Video & PHY\n");

	/* 1. Disable Video Stream */
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_STREAM_ENABLE, 0);

	/* 2. Disable Transmitters */
	soc_dp_reg_write_range(dp, SOC_DPTX_XMIT_ENABLE, 0);

	/* 3. Power Down PHY */
	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_POWERDOWN, 0xc);

	/* 4. Power Down PLLs (MPLL and PREPLL) */
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_PD, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PD, 1);
}

/* Calculate required bandwidth in kbps (Pixel Clock * Bits Per Pixel) */
static uint32_t soc_dp_calc_required_bw(const struct drm_display_mode *mode, int bpp)
{
	return mode->clock * bpp;
}

/* Calculate available link capacity in kbps (taking 8b/10b overhead into account) */
static uint32_t soc_dp_calc_link_capacity(enum soc_dp_link_rate rate, enum soc_dp_lane_count lanes)
{
	/* Capacity = Rate(kHz) * Lanes * 0.8 */
	return (rate * lanes * 8) / 10;
}

static enum drm_connector_status soc_dp_conn_detect(struct drm_connector *connector, bool force)
{
	struct soc_dp_dev *dp = container_of(connector, struct soc_dp_dev, connector);

	return dp->connector_status;
}

static int
soc_dp_conn_probe_single_connector_modes(struct drm_connector *connector,
				       uint32_t maxX, uint32_t maxY)
{
	struct soc_dp_dev *dp = container_of(connector, struct soc_dp_dev, connector);

	if (dp->edp_mode)
		return drm_helper_probe_single_connector_modes(connector, 2560, 1600);
	else
		return drm_helper_probe_single_connector_modes(connector, 1920, 1080);
}

static const struct drm_connector_funcs soc_dp_connector_funcs = {
	.fill_modes = soc_dp_conn_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.detect = soc_dp_conn_detect,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int soc_dp_conn_get_edid_block(void *data, uint8_t *buf, unsigned int block, size_t len)
{
	struct soc_dp_dev *dp = data;
	struct drm_dp_aux_msg msg;
	int ret, i, retry;
	uint8_t offset;

	offset = (block * EDID_LENGTH) & 0xFF;

	for (retry = 0; retry < 3; retry++) {
		msg.address = 0x50;
		msg.request = DP_AUX_I2C_WRITE;
		msg.buffer = &offset;
		msg.size = 1;
		msg.reply = 0;

		ret = soc_dp_aux_transfer(&dp->aux, &msg);
		if (ret >= 0)
			break;
	}

	if (ret < 0) {
		dev_err(dp->dev, "[EDID] AUX write offset failed: %d\n", ret);
		return -EIO;
	}

	for (i = 0; i < len; i += 16) {
		for (retry = 0; retry < 3; retry++) {
			msg.address = 0x50;
			msg.request = DP_AUX_I2C_READ;
			msg.buffer = buf + i;
			msg.size = min_t(size_t, 16, len - i);
			msg.reply = 0;

			ret = soc_dp_aux_transfer(&dp->aux, &msg);
			if (ret >= 0)
				break;
		}

		if (ret < 0) {
			dev_err(dp->dev, "[EDID] AUX read data failed at offset %d: %d\n", i, ret);
			return -EIO;
		}
	}

	return 0;
}

static int soc_dp_conn_get_modes(struct drm_connector *connector)
{
	int count;
	const struct drm_edid *edid;
	struct drm_display_mode *mode, *tmp;
	struct drm_device *dev = connector->dev;
	struct soc_dp_dev *dp = container_of(connector, struct soc_dp_dev, connector);
	struct drm_display_mode *preferred_mode = NULL;

	edid = drm_edid_read_custom(connector, soc_dp_conn_get_edid_block, dp);
	drm_edid_connector_update(connector, edid);
	count = drm_edid_connector_add_modes(connector);

	list_for_each_entry_safe(mode, tmp, &connector->probed_modes, head) {
		if (mode->hdisplay == 2560) {
			if (drm_mode_vrefresh(mode) > 90) {
				list_del(&mode->head);
				drm_mode_destroy(dev, mode);
				count--;
			}
		}
	}

	if (count > 1) {
		list_for_each_entry_safe(mode, tmp, &connector->probed_modes, head) {
			mode->type &= ~DRM_MODE_TYPE_PREFERRED;

			if (!preferred_mode && mode->hdisplay == 1920 && mode->vdisplay == 1080 &&
			    drm_mode_vrefresh(mode) == 60) {
				preferred_mode = mode;
			}
		}

		if (preferred_mode) {
			preferred_mode->type |= DRM_MODE_TYPE_PREFERRED;
			list_move(&preferred_mode->head, &connector->probed_modes);
		} else {
			mode = list_first_entry(&connector->probed_modes, struct drm_display_mode, head);
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		}
	}

	drm_edid_free(edid);

	return count;
}

static enum drm_mode_status soc_dp_conn_mode_valid(struct drm_connector *connector,
					       const struct drm_display_mode *mode)
{
	return MODE_OK;
}

static const struct drm_connector_helper_funcs soc_dp_conn_helper_funcs = {
	.get_modes = soc_dp_conn_get_modes,
	.mode_valid = soc_dp_conn_mode_valid,
};

static const struct drm_encoder_funcs soc_dp_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void soc_dp_encoder_enable(struct drm_encoder *encoder)
{
	int i;
	struct soc_dp_dev *dp = container_of(encoder, struct soc_dp_dev, encoder);

	uint32_t req_bw;
	int bpp;
	bool config_success = false;
	const struct soc_dp_link_config *cfg;
	struct drm_display_mode *adjusted_mode = &dp->mode;
	uint64_t clk_val;
	uint64_t set_clk_val;

	DRM_INFO("%s()\n", __func__);

	if (dp->pxclk) {
		set_clk_val = adjusted_mode->clock * 1000;
		if (set_clk_val) {
			set_clk_val = clk_round_rate(dp->pxclk, set_clk_val);
			clk_val = clk_get_rate(dp->pxclk);
			if(clk_val != set_clk_val){
				clk_set_rate(dp->pxclk, set_clk_val);
				DRM_INFO("set dp pxclk=%lld\n", set_clk_val);
			}
		}
		clk_val = clk_get_rate(dp->pxclk);
		dp->pixel_clock = clk_val / 1000;
		DRM_INFO("get dp pxclk=%lld\n", clk_val);
	}

	bpp = soc_dp_get_bpp(dp->color_format);
	req_bw = soc_dp_calc_required_bw(adjusted_mode, bpp);

	for (i = 0; i < ARRAY_SIZE(soc_dp_link_priority_table); i++) {
		uint32_t capacity;

		cfg = &soc_dp_link_priority_table[i];

		/* Filter 1: Check HW Capabilities (Source & Sink limits) */
		if (cfg->rate > dp->link.max_rate || cfg->lanes > dp->link.max_num_lanes)
			continue;

		/* Filter 2: Check Bandwidth Requirement */
		capacity = soc_dp_calc_link_capacity(cfg->rate, cfg->lanes);
		if (capacity < req_bw)
			continue;

		dev_info(dp->dev, "DP: Attempting Config: R=%d, L=%d (Cap: %d > Req: %d)\n",
			cfg->rate, cfg->lanes, capacity, req_bw);

		/* Apply Hardware Settings */
		if (dp->use_ext_pixel_clock) {
			if (soc_dp_hw_set_pll(dp, cfg->rate, dp->pixel_clock))
				continue;
		} else {
			if (soc_dp_hw_set_pll(dp, cfg->rate, adjusted_mode->clock))
				continue;
		}

		soc_dp_phy_config_lane_count(dp, cfg->lanes);
		soc_dp_hw_config_phy_rate(dp, cfg->rate);

		soc_dp_reg_write_range(dp, SOC_DPTX_PHY_POWERDOWN, 0x0);
		mdelay(2);

		soc_dp_phy_enable_lanes(dp, cfg->lanes);

		if (soc_dp_check_pll_lock(dp))
			continue;

		if (dp->edp_mode) {
			soc_dp_reg_write_range(dp, SOC_DPTX_ENABLE_EDP, 0x1);
			soc_dp_reg_write_range(dp, SOC_DPTX_STREAM_ENC_EN, 0x1);
			update_edp_config(dp, true);
		} else {
			soc_dp_reg_write_range(dp, SOC_DPTX_ENABLE_EDP, 0x0);
			soc_dp_reg_write_range(dp, SOC_DPTX_STREAM_ENC_EN, 0x0);
			update_edp_config(dp, false);
		}

		/* Execute Link Training */
		if (soc_dp_link_train(dp, cfg->rate, cfg->lanes) == 0) {
			config_success = true;
			dev_info(dp->dev, "DP: Training successful for R:%d L:%d.\n",
					cfg->rate, cfg->lanes);
			break;
		}

		dev_warn(dp->dev, "DP: Training failed for R:%d L:%d. Upgrading...\n",
			cfg->rate, cfg->lanes);
	}

	if (!config_success) {
		dev_err(dp->dev, "DP: Critical Failure - No valid link config found.\n");
		return;
	}

	soc_dp_hw_set_msa_and_enable_video(dp, adjusted_mode, cfg->rate, cfg->lanes);
	dev_info(dp->dev, "DP: Stream Active\n");
}

static void soc_dp_encoder_disable(struct drm_encoder *encoder)
{
	struct soc_dp_dev *dp = container_of(encoder, struct soc_dp_dev, encoder);

	DRM_INFO("%s()\n", __func__);

	/* Disable Video Stream */
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_STREAM_ENABLE, 0);
}

static int soc_dp_encoder_atomic_check(struct drm_encoder *encoder,
		struct drm_crtc_state *crtc_state, struct drm_connector_state *conn_state)
{
	return 0;
}

/*
 * soc_dp_mode_set - Main DP Configuration Entry Point
 * Logic:
 * 1. Read Sink Capabilities.
 * 2. Iterate through link configurations (Rate/Lane combinations).
 * 3. Strategy: Ascending Bandwidth Order (Upgrade Logic).
 * - Start with the lowest config that satisfies bandwidth.
 * - Priority: Maximize Lanes first, then increase Rate (Stability over raw speed).
 * 4. Perform Link Training. If failed, upgrade to next config.
 * 5. Enable Video Stream.
 */
static void soc_dp_mode_set(struct drm_encoder *encoder,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct soc_dp_dev *dp_dev = container_of(encoder, struct soc_dp_dev, encoder);

	drm_mode_copy(&dp_dev->mode, adjusted_mode);
	DRM_INFO("%s()\n", __func__);
	dev_info(dp_dev->dev, "DP: Mode Set %dx%d (PCLK: %d kHz) flags 0x%x\n",
		adjusted_mode->hdisplay, adjusted_mode->vdisplay, adjusted_mode->clock, adjusted_mode->flags);
}

static const struct drm_encoder_helper_funcs soc_dp_encoder_helper_funcs = {
	.enable = soc_dp_encoder_enable,
	.disable = soc_dp_encoder_disable,
	.atomic_check = soc_dp_encoder_atomic_check,
	.mode_set = soc_dp_mode_set,
};

#ifdef CONFIG_SOC_DP_DRIVER_QEMU
/* Debug interface for simulating Hotplug events in QEMU/Simulation */
static ssize_t soc_dp_irq_proc_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	struct soc_dp_dev *dp = PDE_DATA(file_inode(filp));
	char write_status[2] = {0};

	if (copy_from_user(write_status, buf, 1))
		write_status[0] = '1';

	if (write_status[0] == '1')
		dp->connector_status = connector_status_connected;
	else if (write_status[0] == '0')
		dp->connector_status = connector_status_disconnected;
	else
		return -EINVAL;

	drm_kms_helper_hotplug_event(dp->drm);
	return count;
}

static const struct proc_ops soc_dp_irq_proc_ops = {
	.proc_flags = PROC_ENTRY_PERMANENT,
	.proc_write = soc_dp_irq_proc_write,
};

static void soc_dp_proc_irq_debug_init(struct soc_dp_dev *dp)
{
	dp->proc_irq = proc_create_data(dp->connector.name,
			S_IWUSR, NULL, &soc_dp_irq_proc_ops, dp);
}

static void soc_dp_proc_irq_debug_exit(struct soc_dp_dev *dp)
{
	if (dp->proc_irq)
		proc_remove(dp->proc_irq);
	dp->proc_irq = NULL;
}
#endif

#if HOT_PLUG_THREAD_ENABLED
static void soc_dp_hpd_poll_work(struct work_struct *work)
{
	struct soc_dp_dev *dp = container_of(work, struct soc_dp_dev, hpd_work.work);
	enum drm_connector_status old_status, new_status;

	old_status = dp->connector_status;
	new_status = soc_dp_hw_detect_hpd(dp);

	soc_dp_hw_clean_hpd(dp);

	if (new_status != old_status) {
		dp->connector_status = new_status;
		if (dp->connector_status == connector_status_connected)
			soc_dp_hw_read_sink_caps(dp);
		DRM_INFO("%s() dp hpd event\n", __func__);
		drm_kms_helper_hotplug_event(dp->drm);
	}

	schedule_delayed_work(&dp->hpd_work, msecs_to_jiffies(HPD_POLL_INTERVAL_MS));
}
#else
static irqreturn_t soc_dp_irq_handler(int irq, void *data)
{
	struct soc_dp_dev *dp = data;
	enum drm_connector_status old_status, new_status;
	uint32_t hpd_status;

	old_status = dp->connector_status;
	new_status = soc_dp_hw_detect_hpd(dp);

	soc_dp_hw_clean_hpd(dp);

	if (new_status != old_status) {

		soc_dp_reg_read_range(dp, SOC_DPTX_HPD_IN_STATUS, &hpd_status);
		DRM_INFO("%s() hpd status 0x%x\n", __func__, hpd_status);

		dp->connector_status = new_status;
		return IRQ_WAKE_THREAD; // Call hotplug_event
	}

	return IRQ_NONE;
}

static irqreturn_t soc_dp_hotplug_event_handler(int irq, void *data)
{
	struct soc_dp_dev *dp = data;

	if (dp->connector_status == connector_status_connected)
		soc_dp_hw_read_sink_caps(dp);
	drm_kms_helper_hotplug_event(dp->drm);

	return IRQ_HANDLED;
}
#endif

static int soc_dp_resource_init(struct soc_dp_dev *dp, struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	uint32_t dp_id, edp_id;
	void __iomem *pmu_addr = (void __iomem *)ioremap(0xd4282800, 0x400);
	void __iomem *ciu_addr = (void __iomem *)ioremap(0xd4282c00, 0x200);
	u32 value;

	if (of_property_read_u32(pdev->dev.of_node, "dp-id", &dp_id))
		dp_id = -1;

	if (of_property_read_u32(pdev->dev.of_node, "edp-id", &edp_id))
		edp_id = -1;

#ifndef CONFIG_SOC_DP_DRIVER_QEMU
	dp->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dp->regs)) {
		dev_err(dev, "Failed to map registers\n");
		return PTR_ERR(dp->regs);
	}
#else
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to obtain dp resource.\n");
		return -EINVAL;
	}

	dp->regs = devm_kmalloc(dev, res->end - res->start + 1, GFP_KERNEL);
	if (!dp->regs) {
		dev_err(dev, "Failed to map registers\n");
		return -ENOMEM;
	}
#endif

	if (of_property_read_u32(pdev->dev.of_node, "dp-id", &dp_id)) {
		dp->edp_mode = true;
		dp_id = -1;
	} else {
		dp->edp_mode = false;
	}

	if (of_property_read_u32(pdev->dev.of_node, "edp-id", &edp_id))
		edp_id = -1;

	if (dp_id == 0 || edp_id == 0) {
		// mux dp0
		value = readl(ciu_addr + 0x12c);
		value |= BIT(8);
		writel(value, (ciu_addr + 0x12c));
	}

	dp->use_ext_pixel_clock = false;

	/* use DP pixel clock */
	if (dp_id == 0 ) {
		value = readl(pmu_addr + 0x23c);
		value |= BIT(2);
		writel(value, (pmu_addr + 0x23c));
		dp->use_ext_pixel_clock = false;
	} else if (dp_id == 1) {
		value = readl(pmu_addr + 0x23c);
		value |= BIT(18);
		writel(value, (pmu_addr + 0x23c));
		dp->use_ext_pixel_clock = false;
	}

	/* use external pixel clock */
	if (edp_id == 0 ) {
		value = readl(pmu_addr + 0x23c);
		value |= BIT(2);
		writel(value, (pmu_addr + 0x23c));
		dp->use_ext_pixel_clock = true;
	} else if (edp_id == 1) {
		value = readl(pmu_addr + 0x23c);
		value |= BIT(18);
		writel(value, (pmu_addr + 0x23c));
		dp->use_ext_pixel_clock = true;
	}

	iounmap(ciu_addr);
	iounmap(pmu_addr);

#if HOT_PLUG_THREAD_ENABLED
	INIT_DELAYED_WORK(&dp->hpd_work, soc_dp_hpd_poll_work);
#else
	dp->irq = platform_get_irq(pdev, 0);
	if (dp->irq < 0) {
		dev_err(dev, "Failed to get IRQ\n");
		return dp->irq;
	}
	dev_info(dev, "irq %d\n", dp->irq);
#endif

	return 0;
}

static int soc_dp_dev_init(struct soc_dp_dev *dp)
{
	int ret;
	uint32_t m_isel = 0x5, m_mainsel = 0xb;
	uint32_t m_pre = 0x0, m_post = 0x2;
	uint32_t tx_mode = 0x1, tx_pre = 0x0;
	uint32_t clk_div = 24 * 1000 / 100;

	if (of_property_read_u32(dp->dev->of_node, "ref_clock", &dp->ref)) {
		dev_err(dp->dev, "ref_clock attribute not found, default to use 24M.\n");
		dp->ref = SOC_DP_REF_CLK_24M;
	}

	if (of_property_read_u32(dp->dev->of_node, "color_format", &dp->color_format)) {
		dev_err(dp->dev, "color_format attribute not found, default to use rgb888.\n");
		dp->color_format = SOC_VIDEO_RGB_8BIT;
	}

	dev_info(dp->dev, "ref_clock %d color_format %d\n", dp->ref, dp->color_format);

	// Reset Controller and PHY
	soc_dp_reg_write_range(dp, SOC_DPTX_CONTROLLER_RESET, 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_RESET, 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_HDCP_RESET, 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_RESET, 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_RESET, 0x1);
	mdelay(5);

	// Clear Video Reset
	soc_dp_reg_write_range(dp, SOC_DPTX_CONTROLLER_RESET, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_RESET, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_HDCP_RESET, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_RESET, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_RESET, 0x0);
	mdelay(2);

	soc_dp_reg_write_range(dp, SOC_DPTX_DEFAULT_FAST_LINK_TRAIN_EN, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_SCRAMBLER_DISABLE, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_SCALE_DOWN_MODE, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_XMIT_ENABLE, 0);

	// Disable PHY SSC (Spread Spectrum Clocking)
	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_SSC_DIS, 0x1);

	// Bypass PHY busy state
	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_BUSY_BYP, 0x1);

	// Unmask Interrupts
	soc_dp_reg_write_range(dp, SOC_DPTX_HPD_INT_STA_MSK, 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_REPLY_EVENT_INT_STA_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_HDCP_INT_STA_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ILLEGAL_AUX_CMD_INT_STA_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_TYPE_C_EVENT_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_DSC_EVENT_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_SDP_INT_STA_S3_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_SDP_INT_STA_S2_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_SDP_INT_STA_S1_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_SDP_INT_STA_S0_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_FIFO_OVERFLOW_INT_STA_S3_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_FIFO_OVERFLOW_INT_STA_S2_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_FIFO_OVERFLOW_INT_STA_S1_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_FIFO_OVERFLOW_INT_STA_S0_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_SINK_IRQ_EVENT_MSK, 0x0);
#if HPD_BYPASS || HOT_PLUG_THREAD_ENABLED
	soc_dp_reg_write_range(dp, SOC_DPTX_HOT_PLUG_EVENT_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_HOT_UNPLUG_EVENT_MSK, 0x0);
#else
	soc_dp_reg_write_range(dp, SOC_DPTX_HOT_PLUG_EVENT_MSK, 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_HOT_UNPLUG_EVENT_MSK, 0x1);
#endif
	soc_dp_reg_write_range(dp, SOC_DPTX_SINK_UNPLUG_ERROR_EVENT_MSK, 0x0);
	mdelay(2);

	// Configure PLL and Lanes
	if (dp->use_ext_pixel_clock) {
		ret = soc_dp_hw_set_pll(dp, SOC_DP_LINK_RATE_2_70, 150000);
		if (ret)
			return ret;
	} else {
		ret = soc_dp_hw_set_pll(dp, SOC_DP_LINK_RATE_2_70, 148500);
		if (ret)
			return ret;
	}

	soc_dp_phy_config_lane_count(dp, SOC_DP_LANE_2);
	soc_dp_hw_config_phy_rate(dp, SOC_DP_LINK_RATE_2_70);

	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_POWERDOWN, 0x0);
	mdelay(2);

	soc_dp_phy_enable_lanes(dp, SOC_DP_LANE_2);

	ret = soc_dp_check_pll_lock(dp);
	if (ret)
		return ret;

	// Enable Enhance Framing and Scale Down Mode
	soc_dp_reg_write_range(dp, SOC_DPTX_ENHANCE_FRAMING_EN, 0x1);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_D0, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_D1, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_D2, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_D3, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_RTCAL_FREQDIV_HBIT, (clk_div >> 8) & 0x7f);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_RTCAL_BYPASS, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_RTCAL_FREQDIV_LBIT, clk_div & 0xff);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_BG_RCAL_SEL, 0);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_RTM_D3, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_RTM_D2, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_RTM_D1, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_RTM_D0, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_RTCAL_BYPASS, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_RTCAL_BYPASS, 0);
	msleep(100);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_PRE_D3, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_PRE_D2, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_PRE_D1, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_PRE_D0, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_DE_D3, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_DE_D2, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_DE_D1, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_DE_D0, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_POSTSEL_PRE_D3, tx_pre);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_POSTSEL_PRE_D2, tx_pre);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_POSTSEL_PRE_D1, tx_pre);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_POSTSEL_PRE_D0, tx_pre);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_ISEL_DRV_D3, m_isel);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_ISEL_DRV_D2, m_isel);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MAINSEL_D2, m_mainsel);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MAINSEL_D3, m_mainsel);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_ISEL_DRV_D1, m_isel);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_ISEL_DRV_D0, m_isel);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_POSTSEL_D1, m_post);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_POSTSEL_D0, m_post);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_POSTSEL_D3, m_post);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_POSTSEL_D2, m_post);
	soc_dp_reg_write_range(dp, SOC_DPTX_DA_TX_MAINSEL_D0_4_0, m_mainsel);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MAINSEL_D1, m_mainsel);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_PRESEL_D1, m_pre);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_PRESEL_D0, m_pre);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_PRESEL_D3, m_pre);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_PRESEL_D2, m_pre);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_D3, tx_mode);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_D2, tx_mode);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_D1, tx_mode);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_MODE_D0, tx_mode);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_AUX_RX_VSEL, 0x0);

	// Update connector status using hardware detection interface
#if HPD_BYPASS
	soc_dp_reg_write_range(dp, SOC_DPTX_FORCE_HPD, 0x1);
	mdelay(5);
#endif
	dp->connector_status = soc_dp_hw_detect_hpd(dp);
	soc_dp_hw_clean_hpd(dp);

	if (dp->connector_status == connector_status_connected)
		soc_dp_hw_read_sink_caps(dp);

	// Notify DRM core about the initial hotplug event
	drm_kms_helper_hotplug_event(dp->drm);

#if HOT_PLUG_THREAD_ENABLED
	dev_info(dp->dev, "Starting HPD Polling Thread...\n");
	schedule_delayed_work(&dp->hpd_work, msecs_to_jiffies(HPD_POLL_INTERVAL_MS));
#else
	ret = devm_request_threaded_irq(dp->dev, dp->irq, soc_dp_irq_handler,
			soc_dp_hotplug_event_handler, 0, dev_name(dp->dev), dp);
	if (ret) {
		dev_err(dp->dev, "Failure requesting irq %d: %d.\n", dp->irq, ret);
		return ret;
	}
#endif

	return 0;
}

static int soc_dp_bind(struct device *dev, struct device *master, void *data)
{
	int ret;
	struct soc_dp_dev *dp;
	struct drm_device *drm = (struct drm_device *)data;
	struct platform_device *pdev = to_platform_device(dev);

	DRM_INFO("%s()\n", __func__);

	dp = devm_kmalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;
	memset(dp, 0, sizeof(*dp));

	dp->dev = dev;
	dp->drm = drm;
	dp->connector_status = connector_status_disconnected;

#ifdef CONFIG_SOC_DP_DRIVER_QEMU
	dp->proc_irq = NULL;
#endif

	dp->reset = devm_reset_control_get_optional_shared(&pdev->dev, "reset");
	if (IS_ERR_OR_NULL(dp->reset)) {
		DRM_INFO("Failed to found reset\n");
	}

	dp->pxclk = of_clk_get_by_name(dev->of_node, "pxclk");
	if (IS_ERR(dp->pxclk)) {
		dp->pxclk = NULL;
		DRM_INFO("Failed to found pxclk\n");
	}

	ret = of_property_read_u32(dev->of_node, "gpios-bl", &dp->gpio_bl);
	if (ret || !gpio_is_valid(dp->gpio_bl)) {
		dev_info(dev, "missing dt property: gpios-bl\n");
		dp->gpio_bl = INVALID_GPIO;
	} else {
		ret = gpio_request(dp->gpio_bl, NULL);
		if (ret) {
			pr_err("gpio_bl request fail\n");
		}
	}

	ret = of_property_read_u32(dev->of_node, "gpios-enable", &dp->gpio_enable);
	if (ret || !gpio_is_valid(dp->gpio_enable)) {
		dev_info(dev, "missing dt property: gpios-enable\n");
		dp->gpio_enable = INVALID_GPIO;
	} else {
		ret = gpio_request(dp->gpio_enable, NULL);
		if (ret) {
			pr_err("gpio_enable request fail\n");
		}
	}

	ret = of_property_read_u32(dev->of_node, "gpios-power", &dp->gpio_power);
	if (ret || !gpio_is_valid(dp->gpio_power)) {
		dev_info(dev, "missing dt property: gpios-power\n");
		dp->gpio_power = INVALID_GPIO;
	} else {
		ret = gpio_request(dp->gpio_power, NULL);
		if (ret) {
			pr_err("gpio_power request fail\n");
		}
	}

	if (!IS_ERR_OR_NULL(dp->reset)) {
		ret = reset_control_deassert(dp->reset);
		if (ret < 0) {
			DRM_INFO("Failed to deassert reset\n");
		}
	}

	if (dp->pxclk)
		clk_prepare_enable(dp->pxclk);

	if(INVALID_GPIO != dp->gpio_power)
		gpio_direction_output(dp->gpio_power, 1);
	if(INVALID_GPIO != dp->gpio_enable)
		gpio_direction_output(dp->gpio_enable, 1);
	if(INVALID_GPIO != dp->gpio_bl)
		gpio_direction_output(dp->gpio_bl, 1);

	/* Init Connector */
	ret = drm_connector_init(drm, &dp->connector,
			&soc_dp_connector_funcs, DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		dev_err(dev, "Failed to init connector\n");
		return ret;
	}
	drm_connector_helper_add(&dp->connector, &soc_dp_conn_helper_funcs);

	/* Init Encoder */
	ret = drm_encoder_init(drm, &dp->encoder,
			&soc_dp_encoder_funcs, DRM_MODE_ENCODER_TMDS, NULL);
	if (ret) {
		dev_err(dev, "Failed to init encoder\n");
		drm_connector_cleanup(&dp->connector);
		return ret;
	}
	drm_encoder_helper_add(&dp->encoder, &soc_dp_encoder_helper_funcs);

	dp->encoder.possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	drm_connector_attach_encoder(&dp->connector, &dp->encoder);

	platform_set_drvdata(pdev, dp);

#ifdef CONFIG_SOC_DP_DRIVER_QEMU
	soc_dp_proc_irq_debug_init(dp);
#endif

	soc_dp_aux_init(dp);

	ret = soc_dp_resource_init(dp, pdev);
	if (ret) {
		drm_connector_cleanup(&dp->connector);
		return ret;
	}

	ret = soc_dp_dev_init(dp);
	if (ret) {
		drm_connector_cleanup(&dp->connector);
		return ret;
	}

	return 0;
}

static void soc_dp_unbind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct soc_dp_dev *dp = platform_get_drvdata(pdev);
	int ret;

	DRM_INFO("%s()\n", __func__);

	soc_dp_hw_disable(dp);

#if HOT_PLUG_THREAD_ENABLED
	cancel_delayed_work_sync(&dp->hpd_work);
#endif

	drm_dp_aux_unregister(&dp->aux);

#ifdef CONFIG_SOC_DP_DRIVER_QEMU
	soc_dp_proc_irq_debug_exit(dp);
#endif
	drm_encoder_cleanup(&dp->encoder);
	drm_connector_cleanup(&dp->connector);

	if(INVALID_GPIO != dp->gpio_bl)
		gpio_direction_output(dp->gpio_bl, 0);
	if(INVALID_GPIO != dp->gpio_enable)
		gpio_direction_output(dp->gpio_enable, 0);
	if(INVALID_GPIO != dp->gpio_power)
		gpio_direction_output(dp->gpio_power, 0);

	if (dp->pxclk)
		clk_disable_unprepare(dp->pxclk);

	if (!IS_ERR_OR_NULL(dp->reset)) {
		ret = reset_control_assert(dp->reset);
		if (ret < 0) {
			DRM_INFO("Failed to assert reset\n");
		}
	}
}

static const struct component_ops soc_dp_ops = {
	.bind = soc_dp_bind,
	.unbind = soc_dp_unbind,
};

static int inno_dp_probe(struct platform_device *pdev)
{
	DRM_INFO("%s()\n", __func__);
	return component_add(&pdev->dev, &soc_dp_ops);
}

static void inno_dp_remove(struct platform_device *pdev)
{
	DRM_INFO("%s()\n", __func__);
	component_del(&pdev->dev, &soc_dp_ops);
}

static const struct of_device_id soc_dp_match[] = {
	{ .compatible = "spacemit,inno-dp0" },
	{ .compatible = "spacemit,inno-dp1" },
	{ .compatible = "spacemit,inno-edp0" },
	{ .compatible = "spacemit,inno-edp1" },
	{}
};
MODULE_DEVICE_TABLE(of, soc_dp_match);

struct platform_driver inno_dp_driver = {
	.probe = inno_dp_probe,
	.remove = inno_dp_remove,
	.driver = {
		.name = "spacemit-inno-dp-drv",
		.of_match_table = soc_dp_match,
	},
};

// module_platform_driver(inno_dp_driver);
static int inno_dp_driver_init(void)
{
	return platform_driver_register(&inno_dp_driver);
}
late_initcall(inno_dp_driver_init);

MODULE_LICENSE("GPL");
