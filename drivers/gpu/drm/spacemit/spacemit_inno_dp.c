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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/backlight.h>
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
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "spacemit_inno_dp.h"

#define INVALID_GPIO	0xFFFFFFFF

#define ACTIVATE_DO_DIV	1
#define HPD_BYPASS	0

#define HOT_PLUG_THREAD_ENABLED 1
#define HPD_POLL_INTERVAL_MS    200

#define SOC_DP_SWING_MAX  2
#define SOC_DP_PREEMP_MAX 2
#define SOC_DP_AUX_MAX_RETRIES 3

#define SOC_DP_EDID_CHUNK_SIZE 16
#define SOC_DP_DDC_SEGMENT_ADDR 0x30
#define SOC_DP_SINK_READY_DELAY_MS 120
#define SOC_DP_SINK_READY_RETRIES 3

#define SOC_DP_APMU_CLK_CTRL	0x23c
#define SOC_DP_QOS_BASE		0xd4282c00
#define SOC_DP_QOS_SIZE		0x200
#define SOC_DP_QOS_MUX_CTRL	0x12c

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
	{ 551040, 2760000, 0x05, 574,  0x3, 0x0, 0x1, 0x0, 0x00, 0x0, 551040, true },
	{ 533250, 2130000, 0x08, 711,  0x3, 0x0, 0x0, 0x1, 0x01, 0x1, 533250, true },
	{ 443250, 1770000, 0x08, 591,  0x3, 0x0, 0x0, 0x1, 0x01, 0x1, 443250, true },
	{ 375000, 3000000, 0x01, 125,  0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 375000, true },
	{ 372000, 2980000, 0x01, 124,  0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 372000, true },
	{ 348500, 2790000, 0x06, 697,  0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 348500, true },
	{ 307200, 1540000, 0x01, 64,   0x3, 0x0, 0x1, 0x0, 0x00, 0x0, 307200, true },
	{ 297000, 2376000, 0x01, 99,   0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 297000, true },
	{ 280000, 1676000, 0x01, 70,   0x3, 0x0, 0x0, 0xa, 0x01, 0x1, 280000, true },
	{ 277440, 2770000, 0x05, 578,  0x3, 0x0, 0x0, 0x3, 0x01, 0x1, 277440, true },
	{ 245760, 2457600, 0x05, 512,  0x3, 0x0, 0x0, 0x3, 0x01, 0x1, 245760, true },
	{ 241500, 1932000, 0x02, 161,  0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 241500, true },
	{ 236000, 2830000, 0x01, 118,  0x3, 0x0, 0x0, 0x0, 0x06, 0x1, 236000, true },
	{ 204800, 2048000, 0x03, 256,  0x3, 0x0, 0x0, 0x3, 0x01, 0x1, 204800, true },
	{ 193250, 2320000, 0x08, 773,  0x3, 0x0, 0x0, 0x0, 0x06, 0x1, 193250, true },
	{ 189000, 1510000, 0x01,  63,  0x3, 0x0, 0x0, 0x0, 0x04, 0x1, 189000, true },
	{ 187500, 3000000, 0x01, 125,  0x3, 0x0, 0x0, 0x0, 0x08, 0x1, 187500, true },
	{ 162000, 2592000, 0x01, 108,  0x3, 0x0, 0x0, 0x0, 0x08, 0x1, 162000, true },
	{ 156000, 2810000, 0x01, 117,  0x3, 0x0, 0x0, 0x0, 0x09, 0x1, 156000, true },
	{ 150000, 3000000, 0x01, 125,  0x3, 0x0, 0x0, 0x0, 0x0a, 0x1, 150000, true },
	{ 148500, 2376000, 0x01, 99,   0x3, 0x0, 0x0, 0x0, 0x08, 0x1, 148500, true },
	{ 146000, 1750000, 0x01, 73,   0x3, 0x0, 0x0, 0x0, 0x06, 0x1, 146000, true },
	{ 142860, 2860000, 0x14, 2381, 0x3, 0x0, 0x0, 0x0, 0x0a, 0x1, 142860, true },
	{ 140000, 2520000, 0x01, 105,  0x3, 0x0, 0x0, 0x0, 0x09, 0x1, 140000, true },
	{ 138500, 2220000, 0x03, 277,  0x3, 0x0, 0x0, 0x0, 0x08, 0x1, 138500, true },
	{ 122000, 2930000, 0x01, 122,  0x3, 0x0, 0x0, 0x0, 0x0c, 0x1, 122000, true },
	{ 121750, 2920000, 0x04, 487,  0x3, 0x0, 0x0, 0x0, 0x0c, 0x1, 121750, true },
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
	struct backlight_device *backlight;

	void __iomem *regs;
	struct regmap *apmu;
	struct regmap *qos;
	struct drm_dp_aux aux;

	/* Buffer to store raw DPCD data */
	uint8_t dpcd[DP_RECEIVER_CAP_SIZE];
	int lane_count;
	uint32_t link_rate;

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
	int dpu_id;
	bool use_ext_pixel_clock;
	int pixel_clock;
	struct mutex mode_lock;
	bool suspended;

	uint32_t ref_clk;
	uint32_t color_format;

#ifdef CONFIG_SOC_DP_DRIVER_QEMU
	struct proc_dir_entry *proc_irq;
#endif

#if HOT_PLUG_THREAD_ENABLED
	struct delayed_work hpd_work;
#else
	int irq;
#endif
#if IS_ENABLED(CONFIG_SND_SOC)
	uint32_t aud_mode;
	bool card_instantiated;
	bool aud_registered;
#endif
};

static const struct regmap_config soc_dp_qos_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = SOC_DP_QOS_SIZE - 4,
};

static struct regmap *soc_dp_qos_init_regmap(struct device *dev,
					      resource_size_t base,
					      resource_size_t size,
					      const struct regmap_config *config)
{
	void __iomem *regs;

	regs = devm_ioremap(dev, base, size);
	if (!regs)
		return ERR_PTR(-ENOMEM);

	return devm_regmap_init_mmio(dev, regs, config);
}

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

static int soc_dp_reg_only_write_range(struct soc_dp_dev *dp,
		uint32_t offset, uint32_t high, uint32_t low, uint32_t val)
	{
	uint32_t mask;

	mask = (uint32_t)(((((uint64_t)1) << (high - low + 1)) - 1) << low);
	writel((val << low) & mask, (char *)dp->regs + offset);
	return 0;
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

#if IS_ENABLED(CONFIG_SND_SOC)
static int inno_dp_dai_probe(struct snd_soc_dai *dai)
{
	struct soc_dp_dev *dp = snd_soc_dai_get_drvdata(dai);

	if (!dp) {
		dev_err(dp->dev, "Failed to get soc_dp_dev from dai probe\n");
		return -EINVAL;
	}
	dp->card_instantiated = true;

	return 0;
}

static int inno_dp_dai_remove(struct snd_soc_dai *dai)
{
	struct soc_dp_dev *dp = snd_soc_dai_get_drvdata(dai);

	if (!dp) {
		dev_err(dp->dev, "Failed to get soc_dp_dev from dai remove\n");
		return -EINVAL;
	}
	dp->card_instantiated = false;

	return 0;
}

static int inno_dp_dai_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct soc_dp_dev *dp = snd_soc_dai_get_drvdata(dai);
	uint32_t mode = 0x00;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		mode = 0x00;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		mode = 0x01;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		mode = 0x02;
		break;
	default:
		return -EINVAL;
	}
	dp->aud_mode = mode;
	soc_dp_reg_write_range(dp, SOC_DPTX_I2S_AUDIO_MODE, mode);
	return 0;
}

static int inno_dp_dai_pcm_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct soc_dp_dev *dp = snd_soc_dai_get_drvdata(dai);
	unsigned int data_bits = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		data_bits = 0x10;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		data_bits = 0x14;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		data_bits = 0x18;
		break;
	default:
		return -EINVAL;
	}

	soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_DATA_WIDTH, data_bits);
	soc_dp_reg_write_range(dp, SOC_DPTX_I2S_AUDIO_MODE, dp->aud_mode);
	soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_RESET, 1);

	return 0;
}

static int inno_dp_dai_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct soc_dp_dev *dp = snd_soc_dai_get_drvdata(dai);

	if (mute)
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_MUTE, 1);
	else
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_MUTE, 0);

	return 0;
}

static int inno_dp_dai_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct soc_dp_dev *dp = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_RESET, 0);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_RESET, 1);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

const struct snd_soc_dai_ops inno_dp_dai_ops = {
	.probe = inno_dp_dai_probe,
	.remove = inno_dp_dai_remove,
	.hw_params = inno_dp_dai_pcm_hw_params,
	.set_fmt = inno_dp_dai_set_dai_fmt,
	.trigger = inno_dp_dai_trigger,
	.mute_stream = inno_dp_dai_mute,
	.no_capture_mute = 0,
};

struct snd_soc_dai_driver inno_dp_dai_driver = {
	.name = "dp audio",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE
			   | SNDRV_PCM_FMTBIT_S20_3LE
			   | SNDRV_PCM_FMTBIT_S24_LE,
		},
	.ops = &inno_dp_dai_ops,
};

const struct snd_soc_component_driver soc_component_inno_dp = {
	.name = "inno-dp-audio",
};

static int inno_dp_audio_register(struct device *dev)
{
	return snd_soc_register_component(dev,
					  &soc_component_inno_dp,
					  &inno_dp_dai_driver, 1);
}

static void inno_dp_audio_unregister(struct device *dev)
{
	snd_soc_unregister_component(dev);
}
#endif

static uint32_t soc_dp_aux_get_cmd(struct drm_dp_aux_msg *msg)
{
	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		return msg->request;
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		return msg->request;
	default:
		return 0;
	}

	return 0;
}

static void soc_dp_aux_hw_reset(struct soc_dp_dev *dp)
{
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_RESET, 0x1);
	usleep_range(2000, 2500);
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_RESET, 0x0);
	usleep_range(2000, 2500);
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_REPLY_EVENT_INT_STA, 1);
}

static bool soc_dp_dpcd_caps_valid(const u8 *dpcd)
{
	u8 max_bw = dpcd[DP_MAX_LINK_RATE];
	u8 max_lanes = dpcd[DP_MAX_LANE_COUNT] & DP_MAX_LANE_COUNT_MASK;

	if (!dpcd[DP_DPCD_REV])
		return false;

	switch (max_bw) {
	case DP_LINK_BW_1_62:
	case DP_LINK_BW_2_7:
	case DP_LINK_BW_5_4:
	case DP_LINK_BW_8_1:
		break;
	default:
		return false;
	}

	return max_lanes == 1 || max_lanes == 2 || max_lanes == 4;
}

static ssize_t soc_dp_aux_transfer(struct drm_dp_aux *aux,
				   struct drm_dp_aux_msg *msg)
{
	int ret, i, retries = 0;
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

retry_eio:
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
		if (retries < SOC_DP_AUX_MAX_RETRIES) {
			retries++;
			soc_dp_aux_hw_reset(dp);
			dev_dbg(dp->dev,
				"AUX timeout retry, cmd: 0x%x, addr: 0x%x, size %zu, retries %d/%d\n",
				cmd, msg->address, msg->size, retries, SOC_DP_AUX_MAX_RETRIES);
			goto retry_eio;
		}

		dev_err(dp->dev,
			"AUX transfer timeout, req: 0x%x, cmd: 0x%x, addr: 0x%x, size %zu\n",
			msg->request, cmd, msg->address, msg->size);
		return ret;
	}

	/* 6. Read Status */
	soc_dp_reg_read_range(dp, SOC_DPTX_AUX_STATUS, &status);

	/* 7. Clear Interrupt Status (W1C) */
	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_REPLY_EVENT_INT_STA, 1);

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
		if (retries < SOC_DP_AUX_MAX_RETRIES) {
			retries++;
			dev_dbg(dp->dev,
				"AUX status retry, cmd: 0x%x, addr: 0x%x, size %zu, retries %d/%d\n",
				cmd, msg->address, msg->size, retries, SOC_DP_AUX_MAX_RETRIES);
			usleep_range(1000, 1100);
			goto retry_eio;
		}
		dev_err(dp->dev, "AUX status error, cmd: 0x%x, address: 0x%x, size %zu, status: 0x%x, code: 0x%x\n",
			cmd, msg->address, msg->size, status, val);
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
	dp->aux.no_zero_sized = true;

	drm_dp_aux_register(&dp->aux);
}

static uint64_t soc_dp_abs_diff(uint64_t a, uint64_t b)
{
	return (a > b) ? (a - b) : (b - a);
}

static uint32_t soc_dp_div64(uint64_t *n, uint32_t base)
{
#if ACTIVATE_DO_DIV
	return do_div(*n, base);
#else
	uint32_t rem = *n % base;
	*n = *n / base;
	return rem;
#endif
}

static uint32_t soc_dp_get_rate_khz(uint8_t pre, uint16_t fb, uint32_t frac,
				    uint32_t ref_clk_khz, uint32_t total_div)
{
	uint64_t vco_hz;
	uint64_t ref_hz = (uint64_t)ref_clk_khz * 1000;
	uint64_t int_part;
	uint64_t frac_part;

	/* Integer part: (Ref * FB) / Pre */
	int_part = ref_hz * fb;
	int_part += (pre / 2);
	soc_dp_div64(&int_part, pre);

	/* Fractional part: (Ref * Frac) / (Pre * 2^24) */
	frac_part = ref_hz * frac;

	frac_part += (pre / 2);
	soc_dp_div64(&frac_part, pre);

	frac_part += (SOC_DP_PLL_FRAC_MOD / 2);
	soc_dp_div64(&frac_part, SOC_DP_PLL_FRAC_MOD);

	vco_hz = int_part + frac_part;

	/* Final Rate = VCO / total_div */
	vco_hz += (total_div / 2); // Rounding before final division
	soc_dp_div64(&vco_hz, total_div);

	vco_hz += 500; // Rounding for 1000
	soc_dp_div64(&vco_hz, 1000);

	return (uint32_t)vco_hz;
}

static int soc_dp_get_pixel_pll_div_total(const struct soc_dp_pixel_pll_cfg *cfg,
					  uint32_t *div_total)
{
	static const uint8_t divm_factors[] = { 1, 2, 3, 5 };

	if (!cfg || !div_total || !cfg->valid || !cfg->divp)
		return -EINVAL;

	if (cfg->div5_en) {
		*div_total = 5;
		return 0;
	}

	if (cfg->divaux == 1) {
		if (cfg->divm >= ARRAY_SIZE(divm_factors))
			return -EINVAL;

		*div_total = 2 * divm_factors[cfg->divm] * cfg->divp;
		return 0;
	}

	if (cfg->divaux < 2)
		return -EINVAL;

	*div_total = 2 * cfg->divaux * cfg->divp;
	return 0;
}

static bool soc_dp_pixel_pll_cfg_matches(const struct soc_dp_pixel_pll_cfg *cfg,
					 uint32_t target_pclk_khz,
					 uint32_t ref_clk_khz)
{
	uint32_t div_total;
	uint32_t actual_pclk_khz;

	if (soc_dp_get_pixel_pll_div_total(cfg, &div_total))
		return false;

	actual_pclk_khz = soc_dp_get_rate_khz(cfg->prediv, cfg->fbdiv, cfg->frac,
					      ref_clk_khz, div_total);

	if (cfg->actual_pclk_khz &&
	    soc_dp_abs_diff(actual_pclk_khz, cfg->actual_pclk_khz) > SOC_DP_PLL_ERR_TOLERANCE)
		return false;

	return soc_dp_abs_diff(actual_pclk_khz, target_pclk_khz) <= SOC_DP_PLL_ERR_TOLERANCE;
}

static const struct soc_dp_pixel_pll_cfg* find_pixel_pll_cfg(uint32_t pclk_khz,
							      uint32_t ref_clk_khz) {
	const struct soc_dp_pixel_pll_cfg *best_match = NULL;
	uint32_t min_diff = 0xFFFFFFFF;
	int num_configs = sizeof(pixel_pll_cfg_table) / sizeof(pixel_pll_cfg_table[0]);

	for (int i = 0; i < num_configs; i++) {
		uint32_t current_target = pixel_pll_cfg_table[i].target_pclk_khz;
		uint32_t diff = (pclk_khz > current_target) ? (pclk_khz - current_target) : (current_target - pclk_khz);

		if (diff == 0 && soc_dp_pixel_pll_cfg_matches(&pixel_pll_cfg_table[i],
							      pclk_khz, ref_clk_khz)) {
			return &pixel_pll_cfg_table[i];
		}

		if ((pclk_khz / 100) == (current_target / 100) &&
		    soc_dp_pixel_pll_cfg_matches(&pixel_pll_cfg_table[i],
						 pclk_khz, ref_clk_khz)) {
			return &pixel_pll_cfg_table[i];
		}

		if (diff < min_diff && diff < 500 &&
		    soc_dp_pixel_pll_cfg_matches(&pixel_pll_cfg_table[i],
						 pclk_khz, ref_clk_khz)) {
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
	int retry;

	for (retry = 0; retry < SOC_DP_SINK_READY_RETRIES; retry++) {
		if (retry) {
			soc_dp_aux_hw_reset(dp);
			msleep(SOC_DP_SINK_READY_DELAY_MS);
		}

		/* Read DPCD Receiver Capability fields (0x00000 - 0x0000F) */
		ret = drm_dp_dpcd_read(&dp->aux, DP_DPCD_REV, dp->dpcd,
				       DP_RECEIVER_CAP_SIZE);
		if (ret < 0)
			continue;

		if (ret != DP_RECEIVER_CAP_SIZE || !soc_dp_dpcd_caps_valid(dp->dpcd)) {
			dev_dbg(dp->dev,
				"DPCD caps not ready: rev=0x%02x bw=0x%02x lanes=0x%02x\n",
				dp->dpcd[DP_DPCD_REV], dp->dpcd[DP_MAX_LINK_RATE], dp->dpcd[DP_MAX_LANE_COUNT]);
			ret = -EAGAIN;
			continue;
		}

		break;
	}

	if (ret < 0) {
		dev_err(dp->dev, "Failed to read DPCD after %d retries: %zd\n",
			SOC_DP_SINK_READY_RETRIES, ret);
		dp->link.revision = 0x14;
		dp->link.max_rate = SOC_DP_LINK_RATE_5_40;
		dp->link.max_num_lanes = SOC_DP_LANE_2;
		dp->link.enhanced_framing = 1;
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
		dev_warn(dp->dev, "Unknown DPCD Max Rate: 0x%x, defaulting to 5.40G\n", max_bw);
		dp->link.revision = 0x14;
		dp->link.max_rate = SOC_DP_LINK_RATE_5_40;
		dp->link.max_num_lanes = SOC_DP_LANE_2;
		dp->link.enhanced_framing = 1;
		return -1;
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

static int soc_dp_is_better_config(bool new_valid, bool new_is_int, uint8_t new_pre, uint32_t new_vco,
				   bool best_valid, bool best_is_int, uint8_t best_pre, uint32_t best_vco)
{
	if (!new_valid) return 0;
	if (!best_valid) return 1;

	if (new_is_int && !best_is_int) return 1;
	if (!new_is_int && best_is_int) return 0;

	if (new_pre < best_pre) return 1;
	if (new_pre > best_pre) return 0;

	if (new_vco > best_vco) return 1;

	return 0;
}

static int soc_dp_solve_pll_frac(uint32_t target_vco_khz, uint32_t ref_clk_khz,
				 uint8_t *best_pre, uint16_t *best_fb, uint32_t *best_frac)
{
	uint64_t min_err = ~0ULL;
	int found = 0;
	bool best_is_int = false;
	int pre;

	/* Iterate pre-divider 1 to 63 to find best PFD frequency */
	for (pre = 1; pre <= 63; pre++) {
		uint64_t ref_clk_hz = (uint64_t)ref_clk_khz * 1000;
		uint64_t target_vco_hz = (uint64_t)target_vco_khz * 1000;

		/* Calculate required multiplier: Mult = (TargetVCO * Pre) / Ref */
		uint64_t num = target_vco_hz * pre;
		uint64_t den = ref_clk_hz;
		uint64_t remainder;
		uint64_t fb_val;
		uint64_t frac_val;
		uint64_t actual_vco;
		uint64_t diff;
		bool current_is_int;

		fb_val = num;
		remainder = soc_dp_div64(&fb_val, (uint32_t)den);

		if (fb_val > 4095) continue;

		/* Frac = (Remainder * 2^24 + Ref/2) / Ref */
		frac_val = remainder * SOC_DP_PLL_FRAC_MOD;
		frac_val += (den / 2);
		soc_dp_div64(&frac_val, (uint32_t)den);

		if (frac_val > 0xFFFFFF)
			frac_val = 0xFFFFFF;

		/*
		* Calculate actual VCO for error checking.
		* VCO = (Ref * FB / Pre) + (Ref * Frac / (Pre * 2^24))
		*/
		{
			uint64_t vco_int, vco_frac;
			/* Integer part: (Ref * FB) / Pre */
			vco_int = ref_clk_hz * fb_val;
			soc_dp_div64(&vco_int, pre);
			/* Fractional part: (Ref * Frac) / (Pre * 2^24) */
			vco_frac = ref_clk_hz * frac_val;
			soc_dp_div64(&vco_frac, pre);
			soc_dp_div64(&vco_frac, SOC_DP_PLL_FRAC_MOD);

			actual_vco = vco_int + vco_frac;
		}

		diff = soc_dp_abs_diff(actual_vco, target_vco_hz);
		current_is_int = (frac_val == 0);

		if (diff < min_err) {
			min_err = diff;
			*best_pre = pre;
			*best_fb = (uint16_t)fb_val;
			*best_frac = (uint32_t)frac_val;
			best_is_int = current_is_int;
			found = 1;
		} else if (diff == min_err) {
			if (current_is_int && !best_is_int) {
			*best_pre = pre;
			*best_fb = (uint16_t)fb_val;
			*best_frac = (uint32_t)frac_val;
			best_is_int = true;
			found = 1;
			}
		}
	}

	return found ? 0 : -1;
}

static int soc_dp_calc_pixel_pll(uint32_t target_pclk_khz, uint32_t ref_clk_khz, struct soc_dp_pixel_pll_cfg *cfg)
{
	struct soc_dp_pixel_pll_cfg best = {0};
	int pclk_div;

	/* Strategy 1: Div5 Path (VCO = PCLK * 5) */
	{
		struct soc_dp_pixel_pll_cfg curr = {0};
		uint32_t div_total = 5;
		uint32_t target_vco = target_pclk_khz * div_total;

		if (target_vco >= SOC_DP_VCO_MIN_KHZ && target_vco <= SOC_DP_VCO_MAX_KHZ) {
			uint8_t pre;
			uint16_t fb;
			uint32_t frac;

			if (soc_dp_solve_pll_frac(target_vco, ref_clk_khz, &pre, &fb, &frac) == 0) {
			uint32_t actual_pclk = soc_dp_get_rate_khz(pre, fb, frac, ref_clk_khz, div_total);

			if (soc_dp_abs_diff(actual_pclk, target_pclk_khz) <= SOC_DP_PLL_ERR_TOLERANCE) {
				curr.valid = true;
				curr.vco_freq_khz = target_vco;
				curr.actual_pclk_khz = actual_pclk;
				curr.prediv = pre; curr.fbdiv = fb; curr.frac = frac;

				curr.frac_pd = (frac == 0) ? 3 : 0;

				curr.div5_en = 1;
				curr.divaux = 0; curr.divm = 0; curr.divp = 0;

				if (soc_dp_is_better_config(curr.valid, (curr.frac==0), curr.prediv, curr.vco_freq_khz,
						best.valid, (best.frac==0), best.prediv, best.vco_freq_khz)) {
					best = curr;
				}
			}
			}
		}
	}

	/* Strategy 2 & 3: Iterate PclkDiv (1 to 31) */
	for (pclk_div = 1; pclk_div <= 31; pclk_div++) {

		/* Strategy 2: DivM Path (DivAux = 1) */
		int divm_factors[] = {1, 2, 3, 5};
		int divm_regs[]    = {0, 1, 2, 3};
		int i;

		for (i = 0; i < 4; i++) {
			struct soc_dp_pixel_pll_cfg curr = {0};
			int m_val = divm_factors[i];
			uint32_t div_total = 2 * m_val * pclk_div;
			uint32_t target_vco = target_pclk_khz * div_total;
			uint8_t pre;
			uint16_t fb;
			uint32_t frac;

			if (target_vco < SOC_DP_VCO_MIN_KHZ || target_vco > SOC_DP_VCO_MAX_KHZ) continue;

			if (soc_dp_solve_pll_frac(target_vco, ref_clk_khz, &pre, &fb, &frac) == 0) {
			uint32_t actual_pclk = soc_dp_get_rate_khz(pre, fb, frac, ref_clk_khz, div_total);

			if (soc_dp_abs_diff(actual_pclk, target_pclk_khz) > SOC_DP_PLL_ERR_TOLERANCE) continue;

			curr.valid = true;
			curr.vco_freq_khz = target_vco;
			curr.actual_pclk_khz = actual_pclk;
			curr.prediv = pre; curr.fbdiv = fb; curr.frac = frac;

			curr.frac_pd = (frac == 0) ? 3 : 0;

			curr.div5_en = 0;
			curr.divaux = 1;         /* Must be 1 to enable DivM logic */
			curr.divm = divm_regs[i];
			curr.divp = pclk_div;

			if (soc_dp_is_better_config(curr.valid, (curr.frac==0), curr.prediv, curr.vco_freq_khz,
					best.valid, (best.frac==0), best.prediv, best.vco_freq_khz)) {
				best = curr;
			}
			}
		}

		/* Strategy 3: DivAux Path (DivAux > 1) */
		{
			int aux;
			for (aux = 2; aux <= 31; aux++) {
			struct soc_dp_pixel_pll_cfg curr = {0};
			uint32_t div_total = 2 * aux * pclk_div;
			uint32_t target_vco = target_pclk_khz * div_total;
			uint8_t pre;
			uint16_t fb;
			uint32_t frac;

			if (target_vco < SOC_DP_VCO_MIN_KHZ || target_vco > SOC_DP_VCO_MAX_KHZ) continue;

			if (soc_dp_solve_pll_frac(target_vco, ref_clk_khz, &pre, &fb, &frac) == 0) {
				uint32_t actual_pclk = soc_dp_get_rate_khz(pre, fb, frac, ref_clk_khz, div_total);

				if (soc_dp_abs_diff(actual_pclk, target_pclk_khz) > SOC_DP_PLL_ERR_TOLERANCE) continue;

				curr.valid = true;
				curr.vco_freq_khz = target_vco;
				curr.actual_pclk_khz = actual_pclk;
				curr.prediv = pre; curr.fbdiv = fb; curr.frac = frac;

				curr.frac_pd = (frac == 0) ? 3 : 0;

				curr.div5_en = 0;
				curr.divaux = aux;
				curr.divm = 0; /* Ignored when divaux != 1 */
				curr.divp = pclk_div;

				if (soc_dp_is_better_config(curr.valid, (curr.frac==0), curr.prediv, curr.vco_freq_khz,
						best.valid, (best.frac==0), best.prediv, best.vco_freq_khz)) {
				best = curr;
				}
			}
			}
		}
	}

	if (!best.valid)
		return -EINVAL;

	*cfg = best;
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
	if (!pll_locked) {
		dev_err(dp->dev, "Pre_pll unlocked.\n");
		return -EINVAL;
	}

#ifndef CONFIG_SOC_DP_DRIVER_QEMU
	soc_dp_reg_read_range(dp, SOC_DPTX_AD_LOCK_COREPLL, &pll_locked);
#else
	pll_locked = 1;
#endif
	if (!pll_locked) {
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
	int ret;

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

	pixel_pll_cfg = find_pixel_pll_cfg(pclk, dp->ref_clk);
	if (pixel_pll_cfg) {
		soc_dp_calc_pixel_pll_to_reg(dp, pixel_pll_cfg);
	} else {
		struct soc_dp_pixel_pll_cfg pll_cfg;

		memset(&pll_cfg, 0, sizeof(pll_cfg));
		ret = soc_dp_calc_pixel_pll(pclk, dp->ref_clk, &pll_cfg);
		if (ret) {
			dev_err(dp->dev, "Unsupported pixel clock %d\n", pclk);
			return ret;
		}
		dev_info(dp->dev, "pixel pll config: prediv %d, fbdiv %d, frac_pd %d, frac %d, div5_en %d, divm %d, divaux %d, divp %d\n",
			 pll_cfg.prediv, pll_cfg.fbdiv, pll_cfg.frac_pd, pll_cfg.frac, pll_cfg.div5_en, pll_cfg.divm, pll_cfg.divaux, pll_cfg.divp);
		soc_dp_calc_pixel_pll_to_reg(dp, &pll_cfg);
	}

	return 0;
}

static void soc_dp_phy_config_lanes(struct soc_dp_dev *dp, enum soc_dp_lane_count lanes)
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

	dev_dbg(dp->dev, "Configuring PHY Lane Count: %d (Reg: %d)\n",
		 lanes, phy_lanes_val);

	/* Set the number of active lanes */
	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_NUM_LANES, phy_lanes_val);
	dp->lane_count = lanes;
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
		soc_dp_reg_only_write_range(dp, SOC_DPTX_HOT_PLUG_EVENT, 0x1);

	if (unplug_event)
		soc_dp_reg_only_write_range(dp, SOC_DPTX_HOT_UNPLUG_EVENT, 0x1);
#endif
}

static int soc_dp_phy_power_on(struct soc_dp_dev *dp)
{
	int ret, retry;
	uint32_t lane_en;

	switch (dp->lane_count) {
	case SOC_DP_LANE_1:
		lane_en = 0x1;
		break;
	case SOC_DP_LANE_2:
		lane_en = 0x3;
		break;
	case SOC_DP_LANE_4:
		default:
		lane_en = 0xF;
		break;
	}

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_PD, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PD, 0);
	mdelay(2);

	soc_dp_reg_write_range(dp, SOC_DPTX_XMIT_ENABLE, lane_en);
	mdelay(2);

	for (retry = 0; retry < 3; retry++) {
		ret = soc_dp_check_pll_lock(dp);
		if (!ret)
			break;
		mdelay(2);
	}

	return ret;
}

static int soc_dp_phy_power_off(struct soc_dp_dev *dp)
{
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_STREAM_ENABLE, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_XMIT_ENABLE, 0);
	mdelay(2);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PD, 1);
	mdelay(2);

	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_RESET, 0x1);
	mdelay(5);

	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_RESET, 0x0);
	mdelay(2);

	return 0;
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
static void soc_dp_phy_config_rate(struct soc_dp_dev *dp, enum soc_dp_link_rate rate)
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
	dp->link_rate = rate;
}

static int soc_dp_link_train_clock_recovery(struct soc_dp_dev *dp, enum soc_dp_link_rate rate, enum soc_dp_lane_count lanes)
{
	uint8_t link_status[DP_LINK_STATUS_SIZE];
	uint8_t training_set[4] = {0};
	int retries = 0;
	int i, ret;

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
	uint8_t training_pattern = DP_TRAINING_PATTERN_2;
	int retries = 0;
	int i, ret;

	if (dp->dpcd[DP_MAX_LANE_COUNT] & DP_TPS3_SUPPORTED) {
		training_pattern = DP_TRAINING_PATTERN_3;
		dev_info(dp->dev, "Link Training: Using TPS3\n");
	} else {
		dev_info(dp->dev, "Link Training: Using TPS2\n");
	}

	ret = soc_dp_set_training_pattern(dp, training_pattern);
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
	dev_dbg(dp->dev, "Enabling Video Stream...\n");
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_STREAM_ENABLE, 1);
}

static void soc_dp_hw_disable(struct soc_dp_dev *dp)
{
	dev_info(dp->dev, "Disabling Video & PHY\n");

	/* 1. Disable Video Stream */
	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_STREAM_ENABLE, 0);

	/* 2. Disable Transmitters */
	soc_dp_reg_write_range(dp, SOC_DPTX_XMIT_ENABLE, 0);

	/* Disable LDO */
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_LDO_D0, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_LDO_D1, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_LDO_D2, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_LDO_D3, 0);

	/* Disable Driver */
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_DRV_D0, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_DRV_D1, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_DRV_D2, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_DRV_D3, 0);

	/* Disable P2S */
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_P2S_D0, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_P2S_D1, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_P2S_D2, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_TX_EN_P2S_D3, 0);

	/*Disable BG_EN */
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_BG_EN, 0);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_BG_EN_RCAL, 0);

	/* 3. Power Down PLLs (MPLL and PREPLL) */
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

	enum drm_connector_status status;

	mutex_lock(&dp->mode_lock);
	status = dp->connector_status;
	mutex_unlock(&dp->mode_lock);

	return status;
}

static int
soc_dp_conn_probe_single_connector_modes(struct drm_connector *connector,
				       uint32_t maxX, uint32_t maxY)
{
	return drm_helper_probe_single_connector_modes(connector, 3840, 2160);
}

static const struct drm_connector_funcs soc_dp_connector_funcs = {
	.fill_modes = soc_dp_conn_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.detect = soc_dp_conn_detect,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int soc_dp_aux_i2c_write(struct soc_dp_dev *dp, u32 address,
				const u8 *buf, size_t len)
{
	struct drm_dp_aux_msg msg = {
		.address = address,
		.request = DP_AUX_I2C_WRITE,
		.buffer = (u8 *)buf,
		.size = len,
	};
	int ret;

	ret = soc_dp_aux_transfer(&dp->aux, &msg);
	if (ret < 0)
		return ret;

	return ret == len ? 0 : -EIO;
}

static int soc_dp_aux_i2c_read(struct soc_dp_dev *dp, u32 address,
			       u8 *buf, size_t len)
{
	struct drm_dp_aux_msg msg = {
		.address = address,
		.request = DP_AUX_I2C_READ,
		.buffer = buf,
		.size = len,
	};
	int ret;

	ret = soc_dp_aux_transfer(&dp->aux, &msg);
	if (ret < 0)
		return ret;

	return ret == len ? 0 : -EIO;
}

static int soc_dp_conn_get_edid_block(void *data, u8 *buf,
				      unsigned int block, size_t len)
{
	struct soc_dp_dev *dp = data;
	unsigned int start = block * EDID_LENGTH;
	u8 segment = block >> 1;
	int ret, retry;
	size_t offset;

	if (segment) {
		for (retry = 0; retry < 3; retry++) {
			ret = soc_dp_aux_i2c_write(dp, SOC_DP_DDC_SEGMENT_ADDR,
						   &segment, 1);
			if (!ret)
				break;
		}

		if (ret) {
			dev_err(dp->dev,
				"[EDID] segment write failed, block %u segment %u ret %d\n",
				block, segment, ret);
			return ret;
		}
	}

	for (offset = 0; offset < len; offset += SOC_DP_EDID_CHUNK_SIZE) {
		u8 edid_offset = (start + offset) & 0xff;
		size_t chunk = min_t(size_t, SOC_DP_EDID_CHUNK_SIZE, len - offset);

		for (retry = 0; retry < 3; retry++) {
			ret = soc_dp_aux_i2c_write(dp, DDC_ADDR, &edid_offset, 1);
			if (ret)
				continue;

			ret = soc_dp_aux_i2c_read(dp, DDC_ADDR, buf + offset, chunk);
			if (!ret)
				break;
		}

		if (ret) {
			dev_err(dp->dev,
				"[EDID] read failed, block %u offset 0x%02x len %zu ret %d\n",
				block, edid_offset, chunk, ret);
			return ret;
		}
	}

	return 0;
}

static int soc_dp_conn_get_modes(struct drm_connector *connector)
{
	int count;
	uint32_t max_mode_pixels = 0;
	const struct drm_edid *edid;
	struct drm_display_mode *mode, *tmp;
	struct drm_device *dev = connector->dev;
	struct soc_dp_dev *dp = container_of(connector, struct soc_dp_dev, connector);
	struct drm_display_mode *preferred_mode = NULL;
	struct drm_display_mode *edid_preferred_mode = NULL;
	int retry;

	mutex_lock(&dp->mode_lock);

	for (retry = 0; retry < SOC_DP_SINK_READY_RETRIES; retry++) {
		if (retry) {
			soc_dp_aux_hw_reset(dp);
			msleep(SOC_DP_SINK_READY_DELAY_MS);
		}

		edid = drm_edid_read_custom(connector, soc_dp_conn_get_edid_block, dp);
		if (edid)
			break;
	}

	if (!edid) {
		mutex_unlock(&dp->mode_lock);
		dev_err(dp->dev, "Failed to read EDID\n");
		return drm_add_modes_noedid(connector, 1920, 1080);
	}

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

	if (dp->link.max_num_lanes > SOC_DP_LANE_2) {
		list_for_each_entry_safe(mode, tmp, &connector->probed_modes, head) {
			if (mode->hdisplay == 3840) {
				if (drm_mode_vrefresh(mode) > 60) {
					list_del(&mode->head);
					drm_mode_destroy(dev, mode);
					count--;
				}
			}
		}
	} else {
		list_for_each_entry_safe(mode, tmp, &connector->probed_modes, head) {
			if (mode->hdisplay == 3840) {
				if (drm_mode_vrefresh(mode) > 30) {
					list_del(&mode->head);
					drm_mode_destroy(dev, mode);
					count--;
				}
			}
		}
	}

	if (count > 1) {
		list_for_each_entry(mode, &connector->probed_modes, head) {
			u32 mode_pixels = mode->hdisplay * mode->vdisplay;

			if (mode_pixels > max_mode_pixels)
				max_mode_pixels = mode_pixels;

			if (!edid_preferred_mode &&
			    (mode->type & DRM_MODE_TYPE_PREFERRED)) {
				edid_preferred_mode = mode;
			}
		}

		if (!dp->edp_mode && max_mode_pixels > 1920 * 1080 &&
		    (!edid_preferred_mode ||
		     edid_preferred_mode->hdisplay != 1920 ||
		     edid_preferred_mode->vdisplay != 1080)) {
			list_for_each_entry_safe(mode, tmp, &connector->probed_modes, head) {
				mode->type &= ~DRM_MODE_TYPE_PREFERRED;

				if (!preferred_mode && mode->hdisplay == 1920 &&
				    mode->vdisplay == 1080 &&
				    drm_mode_vrefresh(mode) == 60) {
					preferred_mode = mode;
				}
			}

			if (preferred_mode) {
				preferred_mode->type |= DRM_MODE_TYPE_PREFERRED;
				list_move(&preferred_mode->head, &connector->probed_modes);
			} else {
				mode = list_first_entry(&connector->probed_modes,
							struct drm_display_mode, head);
				mode->type |= DRM_MODE_TYPE_PREFERRED;
			}
		}
	}

	drm_edid_free(edid);

	mutex_unlock(&dp->mode_lock);

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
	int ret;
	int i;
	struct soc_dp_dev *dp = container_of(encoder, struct soc_dp_dev, encoder);

	uint32_t req_bw;
	int bpp;
	const struct soc_dp_link_config *cfg;
	struct drm_display_mode *adjusted_mode = &dp->mode;
	uint64_t clk_val;
	uint64_t set_clk_val;

	DRM_INFO("%s()\n", __func__);

	mutex_lock(&dp->mode_lock);

	if (dp->use_ext_pixel_clock && dp->pxclk) {
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
	}

	/* use DP pixel clock */
	if (!dp->use_ext_pixel_clock && dp->dpu_id == 0) {
		ret = regmap_update_bits(dp->apmu, SOC_DP_APMU_CLK_CTRL,
					 BIT(2), BIT(2));
		if (ret)
			dev_err(dp->dev, "Failed to enable dpu0 DP/eDP pixel clock mux: %d\n", ret);
	} else if (!dp->use_ext_pixel_clock && dp->dpu_id == 1) {
		ret = regmap_update_bits(dp->apmu, SOC_DP_APMU_CLK_CTRL,
					 BIT(18), BIT(18));
		if (ret)
			dev_err(dp->dev, "Failed to enable dpu1 DP/eDP pixel clock mux: %d\n", ret);
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

		soc_dp_phy_power_off(dp);

		/* Apply Hardware Settings */
		if (dp->use_ext_pixel_clock) {
			if (soc_dp_hw_set_pll(dp, cfg->rate, dp->pixel_clock))
				continue;
		} else {
			if (soc_dp_hw_set_pll(dp, cfg->rate, adjusted_mode->clock))
				continue;
		}

		soc_dp_phy_config_lanes(dp, cfg->lanes);
		soc_dp_phy_config_rate(dp, cfg->rate);

		if (soc_dp_phy_power_on(dp))
			continue;

		if (soc_dp_hw_detect_hpd(dp) == connector_status_disconnected) {
			mutex_unlock(&dp->mode_lock);
			dev_warn(dp->dev, "DP: Training failed for the connector is disconnected\n");
			return;
		}

		if (dp->edp_mode) {
			soc_dp_reg_write_range(dp, SOC_DPTX_ENABLE_EDP, 0x1);
			soc_dp_reg_write_range(dp, SOC_DPTX_STREAM_ENC_EN, 0x1);
			update_edp_config(dp, true);
		}

		/* Execute Link Training */
		if (soc_dp_link_train(dp, cfg->rate, cfg->lanes) == 0) {
			dev_info(dp->dev, "DP: Training successful for R:%d L:%d.\n",
					cfg->rate, cfg->lanes);
			break;
		}

		dev_warn(dp->dev, "DP: Training failed for R:%d L:%d. Upgrading...\n",
			cfg->rate, cfg->lanes);
	}

	soc_dp_hw_set_msa_and_enable_video(dp, adjusted_mode, dp->link_rate, dp->lane_count);

	mutex_unlock(&dp->mode_lock);
	if (dp->backlight)
		backlight_enable(dp->backlight);
}

static void soc_dp_encoder_disable(struct drm_encoder *encoder)
{
	struct soc_dp_dev *dp = container_of(encoder, struct soc_dp_dev, encoder);

	DRM_INFO("%s()\n", __func__);

	if (dp->backlight)
		backlight_disable(dp->backlight);

	mutex_lock(&dp->mode_lock);

	/* Disable Video Stream */
	soc_dp_phy_power_off(dp);

	mutex_unlock(&dp->mode_lock);
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
	struct soc_dp_dev *dp = container_of(encoder, struct soc_dp_dev, encoder);

	mutex_lock(&dp->mode_lock);
	drm_mode_copy(&dp->mode, adjusted_mode);
	dev_dbg(dp->dev, "DP: Mode Set %dx%d (PCLK: %d kHz) flags 0x%x\n",
		adjusted_mode->hdisplay, adjusted_mode->vdisplay, adjusted_mode->clock, adjusted_mode->flags);
	mutex_unlock(&dp->mode_lock);
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
	int interval_ms = HPD_POLL_INTERVAL_MS;

	mutex_lock(&dp->mode_lock);

	old_status = dp->connector_status;

	if (!dp->suspended) {
		new_status = soc_dp_hw_detect_hpd(dp);
		soc_dp_hw_clean_hpd(dp);
	} else {
		mutex_unlock(&dp->mode_lock);
		return;
	}

	if (new_status != old_status) {
		dp->connector_status = new_status;

		if (dp->connector_status == connector_status_connected)
			soc_dp_hw_read_sink_caps(dp);
		else
			interval_ms = 3000;

		mutex_unlock(&dp->mode_lock);
		DRM_INFO("%s() hot plug event\n", __func__);
		drm_kms_helper_hotplug_event(dp->drm);

#if IS_ENABLED(CONFIG_SND_SOC)
		if (!dp->edp_mode) {
			if (dp->connector_status == connector_status_connected) {
				if (inno_dp_audio_register(dp->dev))
					DRM_INFO("%s() failed to register dp audio component\n", __func__);
				else
					dp->aud_registered = true;
			} else {
				inno_dp_audio_unregister(dp->dev);
				dp->aud_registered = false;
			}
		}
#endif
	} else {
		mutex_unlock(&dp->mode_lock);
#if IS_ENABLED(CONFIG_SND_SOC)
		if (!dp->edp_mode) {
			if (dp->aud_registered && new_status == connector_status_disconnected) {
				if (dp->card_instantiated) {
					inno_dp_audio_unregister(dp->dev);
					dp->aud_registered = false;
				}
			} else if (!dp->aud_registered && new_status == connector_status_connected) {
				if (inno_dp_audio_register(dp->dev))
					DRM_INFO("%s() failed to register dp audio component\n", __func__);
				else
					dp->aud_registered = true;
			}
		}
	}
#endif

	schedule_delayed_work(&dp->hpd_work, msecs_to_jiffies(interval_ms));
}
#else
static irqreturn_t soc_dp_irq_handler(int irq, void *data)
{
	struct soc_dp_dev *dp = data;
	enum drm_connector_status old_status, new_status;
	uint32_t hpd_status;

	old_status = dp->connector_status;

	if (!dp->suspended) {
		new_status = soc_dp_hw_detect_hpd(dp);
		soc_dp_hw_clean_hpd(dp);
	} else {
		new_status = connector_status_disconnected;
	}

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

	drm_kms_helper_hotplug_event(dp->drm);

	return IRQ_HANDLED;
}
#endif

static int soc_dp_resource_init(struct soc_dp_dev *dp, struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	uint32_t dp_id, edp_id;
	int ret;

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

	if (dp_id == 0 || edp_id == 0) {
		dp->dpu_id = 0;
		dp->qos = soc_dp_qos_init_regmap(dev, SOC_DP_QOS_BASE,
						SOC_DP_QOS_SIZE,
						&soc_dp_qos_regmap_config);
		if (IS_ERR(dp->qos)) {
			dev_err(dev, "Failed to regmap QoS\n");
			return PTR_ERR(dp->qos);
		}

		ret = regmap_update_bits(dp->qos, SOC_DP_QOS_MUX_CTRL,
					 BIT(8), BIT(8));
		if (ret) {
			dev_err(dev, "Failed to mux dp0/edp0 %d\n", ret);
			return ret;
		}
	} else
		dp->dpu_id = 1;

	dp->apmu = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "spacemit,apmu");
	if (IS_ERR(dp->apmu)) {
		dev_err(dev, "Failed to lookup APMU\n");
		return PTR_ERR(dp->apmu);
	}

	/* use external pixel clock */
	dp->use_ext_pixel_clock = true;

	/* use DP pixel clock */
	if (dp->dpu_id == 0 ) {
		ret = regmap_update_bits(dp->apmu, SOC_DP_APMU_CLK_CTRL,
					 BIT(2), BIT(2));
		if (ret) {
			dev_err(dev, "Failed to select dp0 internal pixel clock mux: %d\n", ret);
			return ret;
		}
		dp->use_ext_pixel_clock = false;
	} else if (dp->dpu_id == 1) {
		ret = regmap_update_bits(dp->apmu, SOC_DP_APMU_CLK_CTRL,
					 BIT(18), BIT(18));
		if (ret) {
			dev_err(dev, "Failed to select dp1 internal pixel clock mux: %d\n", ret);
			return ret;
		}
		dp->use_ext_pixel_clock = false;
	}

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
	uint32_t m_isel = 0x5, m_mainsel = 0x19;
	uint32_t m_pre = 0x0, m_post = 0x2;
	uint32_t tx_mode = 0x1, tx_pre = 0x0;
	uint32_t clk_div = 24 * 1000 / 100;

	dp->ref_clk = SOC_DP_REF_CLK_24M;
	dp->color_format = SOC_VIDEO_RGB_8BIT;

	soc_dp_reg_write_range(dp, SOC_DPTX_XMIT_ENABLE, 0);
	mdelay(2);

	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_PD, 1);
	soc_dp_reg_write_range(dp, SOC_DPTX_ANA_PREPLL_PD, 1);
	mdelay(2);

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

	soc_dp_reg_write_range(dp, SOC_DPTX_AUX_REPLY_EVENT_INT_STA, 1);

	soc_dp_reg_write_range(dp, SOC_DPTX_DEFAULT_FAST_LINK_TRAIN_EN, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_SCRAMBLER_DISABLE, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_SCALE_DOWN_MODE, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_XMIT_ENABLE, 0);

	soc_dp_reg_write_range(dp, SOC_DPTX_VIDEO_STREAM_ENABLE, 0);

	// Unmask Interrupts
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
	soc_dp_reg_write_range(dp, SOC_DPTX_HPD_INT_STA_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_HOT_PLUG_EVENT_MSK, 0x0);
	soc_dp_reg_write_range(dp, SOC_DPTX_HOT_UNPLUG_EVENT_MSK, 0x0);
#else
	soc_dp_reg_write_range(dp, SOC_DPTX_HPD_INT_STA_MSK, 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_HOT_PLUG_EVENT_MSK, 0x1);
	soc_dp_reg_write_range(dp, SOC_DPTX_HOT_UNPLUG_EVENT_MSK, 0x1);
#endif
	soc_dp_reg_write_range(dp, SOC_DPTX_SINK_UNPLUG_ERROR_EVENT_MSK, 0x0);
	mdelay(2);

	// Disable PHY SSC (Spread Spectrum Clocking)
	// soc_dp_reg_write_range(dp, SOC_DPTX_ANA_MPLL_DISABLE_SSCG, 0x1);

	// Bypass PHY busy state
	soc_dp_reg_write_range(dp, SOC_DPTX_PHY_BUSY_BYP, 0x1);

	// Enable Enhance Framing and Scale Down Mode
	soc_dp_reg_write_range(dp, SOC_DPTX_ENHANCE_FRAMING_EN, 0x1);

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

	soc_dp_phy_config_lanes(dp, SOC_DP_LANE_2);
	soc_dp_phy_config_rate(dp, SOC_DP_LINK_RATE_2_70);

	ret = soc_dp_phy_power_on(dp);
	if (ret)
		return ret;

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

#if IS_ENABLED(CONFIG_SND_SOC)
	// init Audio
	if (!dp->edp_mode) {
		dev_info(dp->dev, "init audio\n");
		soc_dp_reg_write_range(dp, SOC_DPTX_AUD_STREAM_VERTICAL_EN, 1);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUD_STREAM_HORIZONTAL_EN, 1);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUD_TIMESTAMP_VERTICAL_EN, 1);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUD_TIMESTAMP_HORIZONTAL_EN, 1);

		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_INF_SELECT, 0);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUD_ADJUST_SEL, 1);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_NUM_CHANNELS, 0x1);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_DATA_IN_EN, 0x1);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_DATA_WIDTH, 0x10);
		soc_dp_reg_write_range(dp, SOC_DPTX_I2S_AUDIO_MODE, 0x01);

		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_PACKET_ID, 0);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_TIMESTAMP_VERSION_NUM, 0x12);

		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_MUTE, 0);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_RESET, 1);
		udelay(1000);
		soc_dp_reg_write_range(dp, SOC_DPTX_AUDIO_RESET, 0);
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
	uint32_t dp_id;

	DRM_INFO("%s()\n", __func__);

	dp = devm_kmalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;
	memset(dp, 0, sizeof(*dp));

	dp->dev = dev;
	dp->drm = drm;
	dp->connector_status = connector_status_disconnected;
	mutex_init(&dp->mode_lock);
	dp->suspended = false;

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
		dev_dbg(dev, "missing dt property: gpios-bl\n");
		dp->gpio_bl = INVALID_GPIO;
	} else {
		ret = gpio_request(dp->gpio_bl, NULL);
		if (ret) {
			pr_err("gpio_bl request fail\n");
		}
	}

	ret = of_property_read_u32(dev->of_node, "gpios-enable", &dp->gpio_enable);
	if (ret || !gpio_is_valid(dp->gpio_enable)) {
		dev_dbg(dev, "missing dt property: gpios-enable\n");
		dp->gpio_enable = INVALID_GPIO;
	} else {
		ret = gpio_request(dp->gpio_enable, NULL);
		if (ret) {
			pr_err("gpio_enable request fail\n");
		}
	}

	ret = of_property_read_u32(dev->of_node, "gpios-power", &dp->gpio_power);
	if (ret || !gpio_is_valid(dp->gpio_power)) {
		dev_dbg(dev, "missing dt property: gpios-power\n");
		dp->gpio_power = INVALID_GPIO;
	} else {
		ret = gpio_request(dp->gpio_power, NULL);
		if (ret) {
			pr_err("gpio_power request fail\n");
		}
	}

	if (of_property_read_u32(pdev->dev.of_node, "dp-id", &dp_id))
		dp->edp_mode = true;
	else
		dp->edp_mode = false;

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
	if (dp->edp_mode) {
		ret = drm_connector_init(drm, &dp->connector,
				&soc_dp_connector_funcs, DRM_MODE_CONNECTOR_eDP);
		if (ret) {
			dev_err(dev, "Failed to init connector\n");
			return ret;
		}
	} else {
		ret = drm_connector_init(drm, &dp->connector,
				&soc_dp_connector_funcs, DRM_MODE_CONNECTOR_DisplayPort);
		if (ret) {
			dev_err(dev, "Failed to init connector\n");
			return ret;
		}
	}
	drm_connector_helper_add(&dp->connector, &soc_dp_conn_helper_funcs);

	if (dp->edp_mode) {
		dp->backlight = devm_of_find_backlight(dp->dev);
		if (IS_ERR(dp->backlight))
			dev_err(dev, "Failed to find backlight\n");
	} else {
		dp->backlight = NULL;
	}

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
	dp->connector.ddc = &dp->aux.ddc;

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

	soc_dp_phy_power_off(dp);

#if IS_ENABLED(CONFIG_SND_SOC)
	if (!dp->edp_mode) {
		ret = inno_dp_audio_register(dp->dev);
		if (ret)
			dev_err(dev, "failed to register dp audio component\n");
		else
			dp->aud_registered = true;
	}
#endif

#if HPD_BYPASS
	soc_dp_reg_write_range(dp, SOC_DPTX_FORCE_HPD, 0x1);
	mdelay(5);
#endif
	dp->connector_status = soc_dp_hw_detect_hpd(dp);
	soc_dp_hw_clean_hpd(dp);

	if (dp->connector_status == connector_status_connected)
		soc_dp_hw_read_sink_caps(dp);

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

static void soc_dp_unbind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct soc_dp_dev *dp = platform_get_drvdata(pdev);
	int ret;

	DRM_INFO("%s()\n", __func__);

#if IS_ENABLED(CONFIG_SND_SOC)
	if (!dp->edp_mode) {
		inno_dp_audio_unregister(dp->dev);
		dp->aud_registered = false;
	}
#endif

#if HOT_PLUG_THREAD_ENABLED
	cancel_delayed_work_sync(&dp->hpd_work);
#endif

	drm_dp_aux_unregister(&dp->aux);

	soc_dp_hw_disable(dp);

#ifdef CONFIG_SOC_DP_DRIVER_QEMU
	soc_dp_proc_irq_debug_exit(dp);
#endif
	drm_encoder_cleanup(&dp->encoder);
	drm_connector_cleanup(&dp->connector);

	mutex_destroy(&dp->mode_lock);

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

static void inno_dp_shutdown(struct platform_device *pdev)
{
	struct soc_dp_dev *dp = platform_get_drvdata(pdev);

#if HOT_PLUG_THREAD_ENABLED
	cancel_delayed_work_sync(&dp->hpd_work);
#endif
	soc_dp_hw_disable(dp);
}

#ifdef CONFIG_PM_SLEEP

static int inno_dp_drv_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct soc_dp_dev *dp = platform_get_drvdata(pdev);

	DRM_INFO("%s()\n", __func__);

#if HOT_PLUG_THREAD_ENABLED
	cancel_delayed_work_sync(&dp->hpd_work);
#endif

	mutex_lock(&dp->mode_lock);
	dp->suspended = true;
	mutex_unlock(&dp->mode_lock);

	return 0;
}

static int inno_dp_drv_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct soc_dp_dev *dp = platform_get_drvdata(pdev);

	DRM_INFO("%s()\n", __func__);

	mutex_lock(&dp->mode_lock);
	dp->suspended = false;
	mutex_unlock(&dp->mode_lock);

#if HPD_BYPASS
	soc_dp_reg_write_range(dp, SOC_DPTX_FORCE_HPD, 0x1);
	mdelay(5);
#endif
	dp->connector_status = soc_dp_hw_detect_hpd(dp);
	soc_dp_hw_clean_hpd(dp);

	if (dp->connector_status == connector_status_connected)
		soc_dp_hw_read_sink_caps(dp);

	drm_kms_helper_hotplug_event(dp->drm);

#if HOT_PLUG_THREAD_ENABLED
	schedule_delayed_work(&dp->hpd_work, msecs_to_jiffies(HPD_POLL_INTERVAL_MS));
#endif

	return 0;
}

static int inno_dp_drv_pm_suspend_late(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct soc_dp_dev *dp = platform_get_drvdata(pdev);
	int ret;

	DRM_INFO("%s()\n", __func__);

	mutex_lock(&dp->mode_lock);

	soc_dp_hw_disable(dp);

	if (dp->pxclk)
		clk_disable_unprepare(dp->pxclk);

	if (!IS_ERR_OR_NULL(dp->reset)) {
		ret = reset_control_assert(dp->reset);
		if (ret < 0) {
			DRM_INFO("Failed to assert reset\n");
		}
	}

	mutex_unlock(&dp->mode_lock);

	return 0;
}

static int inno_dp_drv_pm_resume_early(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct soc_dp_dev *dp = platform_get_drvdata(pdev);
	int ret;

	DRM_INFO("%s()\n", __func__);

	mutex_lock(&dp->mode_lock);

	if (!IS_ERR_OR_NULL(dp->reset)) {
		ret = reset_control_deassert(dp->reset);
		if (ret < 0) {
			DRM_INFO("Failed to deassert reset\n");
		}
	}
	if (dp->pxclk)
		clk_prepare_enable(dp->pxclk);

	soc_dp_dev_init(dp);

	mutex_unlock(&dp->mode_lock);

	return 0;
}

#endif

static const struct dev_pm_ops inno_dp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(inno_dp_drv_pm_suspend,
				inno_dp_drv_pm_resume)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(inno_dp_drv_pm_suspend_late,
				     inno_dp_drv_pm_resume_early)
};

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
	.shutdown = inno_dp_shutdown,
	.driver = {
		.name = "spacemit-inno-dp-drv",
		.of_match_table = soc_dp_match,
		.pm = &inno_dp_pm_ops,
	},
};

// module_platform_driver(inno_dp_driver);
static int inno_dp_driver_init(void)
{
	return platform_driver_register(&inno_dp_driver);
}
late_initcall(inno_dp_driver_init);

MODULE_LICENSE("GPL");
