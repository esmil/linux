// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Jinmei Wei <weijinmei@linux.spacemit.com> */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

/*
 * Registers
 */
#define RXD		(0x00)
#define RXID		(0x04)
#define RXCTL		(0x08)
#define RXSP		(0x0c)
#define RXFIFO_UL	(0x10)
#define RXINT_MASK	(0x14)
#define RXC		(0x18)
#define RXFIFO_NOFS	(0x1c)
#define RXFIFO_SIZE	(0x20)

#define TXD		(0x80)
#define TXID		(0x84)
#define TXCTL		(0x88)
#define TXSP		(0x8c)
#define TXFIFO_LL	(0x90)
#define TXINT_MASK	(0x94)
#define TXC		(0x98)
#define TXFIFO_NOFS	(0x9c)
#define TXFIFO_SIZE	(0xa0)

/* Control Register 0x08/0x88 */
#define CTL_XPH			BIT(31)          /* Read Phase */
#define CTL_XFRLEN2_MASK	GENMASK(30, 24)
#define CTL_XFRLEN2(x)		((x) << 24)      /* Transmit Frame Length in Phase 2 */
#define CTL_XWDLEN2_MASK	GENMASK(23, 21)
#define CTL_XWDLEN2(x)		((x) << 21)      /* Transmit Word Length in Phase 2 */
#define CTL_XDATDLY_MASK	GENMASK(20, 19)
#define CTL_XDATDLY(x)		((x) << 19)      /* Tansmit Data Delay */
#define CTL_XSSZ2_MASK		GENMASK(18, 16)
#define CTL_XSSZ2(x)		((x) << 16)      /* Transmit Sample Audio Size in Phase 2*/
#define CTL_XFIG		BIT(15)          /* Transmit Zeros when FIFO Empty */
#define CTL_XFRLEN1_MASK	GENMASK(14, 8)
#define CTL_XFRLEN1(x)		((x) << 8)       /* Transmit Frame Length(slot number) in Phase 1 */
#define CTL_XWDLEN1_MASK	GENMASK(7, 5)
#define CTL_XWDLEN1(x)		((x) << 5)       /* Transmit Word Length(slot width) in Phase 1 */
#define CTL_JST			BIT(3)           /* Audio Sample Justification */
#define CTL_XSSZ1_MASK		GENMASK(2, 0)
#define CTL_XSSZ1(x)		((x) << 0)       /* Transmit Sample Audio Data Size in Phase 1 */
#define CTL_8_BITS		(0x0)            /* Sample Audio Data Size */
#define CTL_12_BITS		(0x1)
#define CTL_16_BITS		(0x2)
#define CTL_20_BITS		(0x3)
#define CTL_24_BITS		(0x4)
#define CTL_32_BITS		(0x5)

/* Serial Port Register 0x0c/0x8c */
#define SP_WEN			BIT(31)          /* Write Configuration Enable */
#define SP_FWID_MASK		GENMASK(27, 20)
#define SP_FWID(x)		((x) << 20)      /* Frame-Sync Width */
#define SP_MSL			BIT(18)          /* Master Slave Configuration */
#define SP_CLKP			BIT(17)          /* CLKP Polarity Clock Edge Select */
#define SP_FSP			BIT(16)          /* FSP Polarity Clock Edge Select */
#define SP_FPER_MASK		GENMASK(15, 4)
#define SP_FPER(x)		((x) << 4)       /* Frame-Sync Active */
#define SP_FIX			BIT(3)           /* fsync fix */
#define SP_FFLUSH		BIT(2)           /* FIFO Flush */
#define SP_S_RST		BIT(1)           /* Active High Reset Signal */
#define SP_S_EN			BIT(0)           /* Serial Clock Domain Enable */

/* FIFO Register 0x10/0x90 */
#define TXRX_FIFO_DEPTH		(64)
#define TX_FIFO_THRESHOLD	(0xF)
#define RX_FIFO_THRESHOLD	(0xE)

#define SPACEMIT_PCM_BUFFER_BYTES_MAX	(16 * 1024)
#define SPACEMIT_PCM_PERIOD_BYTES_MAX	(4 * 1024)
#define SPACEMIT_PCM_PEROID_BYTES_MIN	(1 * 1024)

#define SPACEMIT_PCM_RATES	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | \
				SNDRV_PCM_RATE_48000)
#define SPACEMIT_PCM_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE)

struct spacemit_i2s_dev {
	struct device *dev;
	void __iomem *base;

	dma_addr_t alloc_dma_addr;
	unsigned char *alloc_dma_area;
	unsigned long alloc_dma_size;

	dma_addr_t buf_addr;
	unsigned long buf_size;
	void __iomem *buf_base;

	struct reset_control *reset;
	struct reset_control *reset_sys;

	struct clk *sysclk;
	struct clk *bus;
	struct clk *clk;

	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct snd_dmaengine_dai_dma_data playback_dma_data;

	bool has_capture;
	bool has_playback;

	int dai_fmt;
	int started_count;
};

static const struct snd_pcm_hardware spacemit_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_BATCH,
	.formats		= SPACEMIT_PCM_FORMATS,
	.rates			= SPACEMIT_PCM_RATES,
	.rate_min		= SNDRV_PCM_RATE_8000,
	.rate_max		= SNDRV_PCM_RATE_192000,
	.channels_min		= 1,
	.channels_max		= 4,
	.buffer_bytes_max	= SPACEMIT_PCM_BUFFER_BYTES_MAX,
	.period_bytes_min	= SPACEMIT_PCM_PEROID_BYTES_MIN,
	.period_bytes_max	= SPACEMIT_PCM_PERIOD_BYTES_MAX,
	.periods_min		= 4,
	.periods_max		= 4,
};

static const struct snd_dmaengine_pcm_config spacemit_dmaengine_pcm_config = {
	.pcm_hardware = &spacemit_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.chan_names = {"tx", "rx"},
	.prealloc_buffer_size = SPACEMIT_PCM_BUFFER_BYTES_MAX,
};

static int spacemit_i2s_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	u32 buf_size = 0;

	switch (i2s->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		dev_dbg(i2s->dev, "standard i2s/left-justified/right-justified");
		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_CHANNELS,
					     2, 2);
		snd_pcm_hw_constraint_mask64(substream->runtime,
					     SNDRV_PCM_HW_PARAM_FORMAT,
					     SNDRV_PCM_FMTBIT_S16_LE);
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		dev_dbg(i2s->dev, "mode A/B");
		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_CHANNELS,
					     1, 4);
		snd_pcm_hw_constraint_mask64(substream->runtime,
					     SNDRV_PCM_HW_PARAM_FORMAT,
					     SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S16_LE);
		break;
	default:
		dev_err(i2s->dev, "unexpected format type");
		return -EINVAL;
	}

	if (rtd->dai_link->playback_only || rtd->dai_link->capture_only)
		buf_size = i2s->buf_size;
	else
		buf_size = i2s->buf_size / 2;

	snd_pcm_hw_constraint_minmax(substream->runtime,
				     SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				     buf_size / 8, buf_size / 4);

	return 0;
}

static int spacemit_i2s_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);
	struct snd_dmaengine_dai_dma_data *dma_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	u32 data_width, data_bits;
	u32 slot_width = 0, slot_bits = 0;
	u32 sp_reg_offset = 0, sp_reg_val = 0;
	u32 ctrl_reg_offset = 0, ctrl_reg_val = 0;
	u32 buf_size = 0;
	unsigned long bclk_rate;
	int ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dma_data = &i2s->playback_dma_data;
		ctrl_reg_offset = TXCTL;
		sp_reg_offset = TXSP;
	} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		dma_data = &i2s->capture_dma_data;
		ctrl_reg_offset = RXCTL;
		sp_reg_offset = RXSP;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		data_bits = 8;
		data_width = CTL_8_BITS;
		dma_data->maxburst = 8;
		dma_data->addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		data_bits = 16;
		data_width = CTL_16_BITS;
		dma_data->maxburst = 16;
		dma_data->addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		data_bits = 32;
		data_width = CTL_32_BITS;
		dma_data->maxburst = 32;
		dma_data->addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	default:
		dev_err(i2s->dev, "unexpected data width type");
		return -EINVAL;
	}

	ctrl_reg_val = readl(i2s->base + ctrl_reg_offset);
	ctrl_reg_val &= ~(CTL_XDATDLY_MASK | CTL_JST);

	sp_reg_val = readl(i2s->base + sp_reg_offset);
	sp_reg_val |= SP_WEN | SP_FFLUSH;
	sp_reg_val &= ~(SP_FWID_MASK | SP_FSP);

	switch (i2s->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dev_dbg(i2s->dev, "standard-i2s");
		slot_bits = data_bits;
		slot_width = data_width;
		sp_reg_val |= SP_FWID(data_bits - 1);
		sp_reg_val |= SP_FSP;
		ctrl_reg_val |= CTL_XDATDLY(1);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dev_dbg(i2s->dev, "left-justified");
		slot_bits = 32;
		slot_width = CTL_32_BITS;
		sp_reg_val |= SP_FWID(slot_bits - 1);
		ctrl_reg_val |= CTL_XDATDLY(0);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		dev_dbg(i2s->dev, "right-justified");
		slot_bits = 32;
		slot_width = CTL_32_BITS;
		sp_reg_val |= SP_FWID(slot_bits - 1);
		ctrl_reg_val |= CTL_XDATDLY(0);
		ctrl_reg_val |= CTL_JST;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		dev_dbg(i2s->dev, "mode A");
		slot_bits = data_bits;
		slot_width = data_width;
		sp_reg_val |= SP_FWID(0);
		ctrl_reg_val |= CTL_XDATDLY(1);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		dev_dbg(i2s->dev, "mode B");
		slot_bits = data_bits;
		slot_width = data_width;
		sp_reg_val |= SP_FWID(0);
		ctrl_reg_val |= CTL_XDATDLY(0);
		break;
	default:
		dev_err(i2s->dev, "unexpected format type");
		return -EINVAL;
	}

	ctrl_reg_val &= ~CTL_XFRLEN1_MASK;
	ctrl_reg_val |= CTL_XFRLEN1(params_channels(params) - 1);
	ctrl_reg_val &= ~CTL_XWDLEN1_MASK;
	ctrl_reg_val |= CTL_XWDLEN1(slot_width);
	ctrl_reg_val &= ~CTL_XSSZ1_MASK;
	ctrl_reg_val |= CTL_XSSZ1(data_width);

	sp_reg_val &= ~SP_FPER_MASK;
	sp_reg_val |= SP_FPER((params_channels(params) * slot_bits) - 1);

	writel(sp_reg_val, i2s->base + sp_reg_offset);
	writel(ctrl_reg_val, i2s->base + ctrl_reg_offset);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		writel(TX_FIFO_THRESHOLD, i2s->base + TXFIFO_LL);
	else
		writel(RX_FIFO_THRESHOLD, i2s->base + RXFIFO_UL);

	bclk_rate = params_channels(params) *
		    params_rate(params) *
		    slot_bits;

	ret = clk_set_rate(i2s->clk, bclk_rate);
	if (ret)
		return ret;

	i2s->alloc_dma_addr = substream->dma_buffer.addr;
	i2s->alloc_dma_area = substream->dma_buffer.area;
	i2s->alloc_dma_size = substream->dma_buffer.bytes;

	if (rtd->dai_link->playback_only || rtd->dai_link->capture_only) {
		buf_size = i2s->buf_size;
		substream->dma_buffer.addr = (dma_addr_t)(i2s->buf_addr);
		substream->dma_buffer.area = (void *)(i2s->buf_base);
	} else {
		buf_size = i2s->buf_size / 2;
		substream->dma_buffer.addr = (dma_addr_t)(i2s->buf_addr + substream->stream * buf_size);
		substream->dma_buffer.area = (void *)(i2s->buf_base + substream->stream * buf_size);
	}
	substream->dma_buffer.bytes = buf_size;
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int spacemit_i2s_hw_free(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);

	substream->dma_buffer.addr = i2s->alloc_dma_addr;
	substream->dma_buffer.area = i2s->alloc_dma_area;
	substream->dma_buffer.bytes = i2s->alloc_dma_size;
	return 0;
}

static int spacemit_i2s_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
				   unsigned int freq, int dir)
{
	struct spacemit_i2s_dev *i2s = dev_get_drvdata(cpu_dai->dev);

	if (freq == 0)
		return 0;

	return clk_set_rate(i2s->sysclk, freq);
}

static int spacemit_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
				unsigned int fmt)
{
	struct spacemit_i2s_dev *i2s = dev_get_drvdata(cpu_dai->dev);

	i2s->dai_fmt = fmt;
	return 0;
}

static int spacemit_i2s_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);
	u32 sp_reg_offset = 0, sp_reg_val = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (!i2s->started_count) {
			if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
				writel(readl(i2s->base + RXCTL), i2s->base + TXCTL);
				writel(TX_FIFO_THRESHOLD, i2s->base + TXFIFO_LL);
			}

			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				sp_reg_offset = TXSP;
			else
				sp_reg_offset = RXSP;

			sp_reg_val = readl(i2s->base + sp_reg_offset);
			sp_reg_val &= ~SP_S_RST;
			sp_reg_val &= ~SP_FIX;
			sp_reg_val &= ~SP_CLKP;
			sp_reg_val |= SP_S_EN;
			sp_reg_val |= SP_WEN | SP_FFLUSH; /*must config this bit*/

			if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
				writel(sp_reg_val | SP_CLKP, i2s->base + RXSP);
				writel(sp_reg_val | SP_MSL, i2s->base + TXSP);
			} else {
				writel(sp_reg_val | SP_MSL, i2s->base + TXSP);
			}
		} else {
			if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
				sp_reg_val = readl(i2s->base + TXSP);
				sp_reg_val &= ~SP_MSL;
				sp_reg_val |= SP_CLKP;
				sp_reg_val |= SP_WEN;
				writel(sp_reg_val, i2s->base + RXSP);
			}
		}
		i2s->started_count++;
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXD), readl(i2s->base + RXD));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXID), readl(i2s->base + RXID));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXCTL), readl(i2s->base + RXCTL));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXSP), readl(i2s->base + RXSP));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXFIFO_LL), readl(i2s->base + RXFIFO_UL));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXINT_MASK), readl(i2s->base + RXINT_MASK));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXC), readl(i2s->base + RXC));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXFIFO_NOFS), readl(i2s->base + RXFIFO_NOFS));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXFIFO_SIZE), readl(i2s->base + RXFIFO_SIZE));
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (i2s->started_count)
			i2s->started_count--;

		if (!i2s->started_count) {
			if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				sp_reg_offset = TXSP;
			else
				sp_reg_offset = RXSP;

			sp_reg_val = readl(i2s->base + sp_reg_offset);
			sp_reg_val &= ~SP_S_EN;
			sp_reg_val |= SP_S_RST;
			sp_reg_val &= ~(SP_CLKP | SP_MSL);
			sp_reg_val |= SP_WEN | SP_FFLUSH;

			writel(sp_reg_val, i2s->base + RXSP);
			writel(sp_reg_val, i2s->base + TXSP);
		} else {
			if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
				sp_reg_val = readl(i2s->base + RXSP);
				sp_reg_val &= ~SP_S_EN;
				sp_reg_val |= SP_S_RST;
				sp_reg_val |= SP_WEN;
				writel(sp_reg_val, i2s->base + RXSP);
			}
		}
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXD), readl(i2s->base + RXD));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXID), readl(i2s->base + RXID));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXCTL), readl(i2s->base + RXCTL));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXSP), readl(i2s->base + RXSP));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXFIFO_LL), readl(i2s->base + RXFIFO_UL));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXINT_MASK), readl(i2s->base + RXINT_MASK));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXC), readl(i2s->base + RXC));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXFIFO_NOFS), readl(i2s->base + RXFIFO_NOFS));
		dev_dbg(i2s->dev, "0x%08x, 0x%08x", readl(i2s->base + TXFIFO_SIZE), readl(i2s->base + RXFIFO_SIZE));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int spacemit_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai,
				  i2s->has_playback ? &i2s->playback_dma_data : NULL,
				  i2s->has_capture ? &i2s->capture_dma_data : NULL);
	reset_control_deassert(i2s->reset_sys);
	reset_control_deassert(i2s->reset);
	return 0;
}

static int spacemit_i2s_dai_remove(struct snd_soc_dai *dai)
{
	struct spacemit_i2s_dev *i2s = snd_soc_dai_get_drvdata(dai);

	reset_control_assert(i2s->reset);
	reset_control_assert(i2s->reset_sys);
	return 0;
}

static const struct snd_soc_dai_ops spacemit_i2s_dai_ops = {
	.probe = spacemit_i2s_dai_probe,
	.remove = spacemit_i2s_dai_remove,
	.startup = spacemit_i2s_startup,
	.hw_params = spacemit_i2s_hw_params,
	.hw_free = spacemit_i2s_hw_free,
	.set_sysclk = spacemit_i2s_set_sysclk,
	.set_fmt = spacemit_i2s_set_fmt,
	.trigger = spacemit_i2s_trigger,
};

static struct snd_soc_dai_driver spacemit_i2s_dai = {
	.ops = &spacemit_i2s_dai_ops,
	.playback = {
		.channels_min = 1,
		.channels_max = 4,
		.rates = SPACEMIT_PCM_RATES,
		.rate_min = SNDRV_PCM_RATE_8000,
		.rate_max = SNDRV_PCM_RATE_48000,
		.formats = SPACEMIT_PCM_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 4,
		.rates = SPACEMIT_PCM_RATES,
		.rate_min = SNDRV_PCM_RATE_8000,
		.rate_max = SNDRV_PCM_RATE_48000,
		.formats = SPACEMIT_PCM_FORMATS,
	},
	.symmetric_rate = 1,
};

static int spacemit_i2s_init_dai(struct spacemit_i2s_dev *i2s,
				 struct snd_soc_dai_driver **dp,
				 dma_addr_t addr)
{
	struct device_node *node = i2s->dev->of_node;
	struct snd_soc_dai_driver *dai;
	struct property *dma_names;
	const char *dma_name;

	of_property_for_each_string(node, "dma-names", dma_names, dma_name) {
		if (!strcmp(dma_name, "tx"))
			i2s->has_playback = true;
		if (!strcmp(dma_name, "rx"))
			i2s->has_capture = true;
	}

	dai = devm_kmemdup(i2s->dev, &spacemit_i2s_dai,
			   sizeof(*dai), GFP_KERNEL);
	if (!dai) {
		dev_err(i2s->dev, "failed to allocate dai driver\n");
		return -ENOMEM;
	}

	if (i2s->has_playback) {
		dai->playback.stream_name = "Playback";
		dai->playback.channels_min = 1;
		dai->playback.channels_max = 4;
		dai->playback.rates = SPACEMIT_PCM_RATES;
		dai->playback.formats = SPACEMIT_PCM_FORMATS;
		i2s->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		i2s->playback_dma_data.maxburst = 32;
		i2s->playback_dma_data.addr = addr + TXD;
	}

	if (i2s->has_capture) {
		dai->capture.stream_name = "Capture";
		dai->capture.channels_min = 1;
		dai->capture.channels_max = 4;
		dai->capture.rates = SPACEMIT_PCM_RATES;
		dai->capture.formats = SPACEMIT_PCM_FORMATS;
		i2s->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		i2s->capture_dma_data.maxburst = 32;
		i2s->capture_dma_data.addr = addr + RXD;
	}

	if (dp)
		*dp = dai;

	return 0;
}

static const struct snd_soc_component_driver spacemit_i2s_component = {
	.name = "k3-ri2s",
	.legacy_dai_naming = 1,
};

static int spacemit_i2s_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_driver *dai;
	struct spacemit_i2s_dev *i2s;
	struct resource *res;
	int ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s->dev = &pdev->dev;

	i2s->sysclk = devm_clk_get_enabled(i2s->dev, "sysclk");
	if (IS_ERR(i2s->sysclk))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->sysclk),
				     "failed to enable sysclk\n");

	i2s->bus = devm_clk_get_enabled(i2s->dev, "bus");
	if (IS_ERR(i2s->bus))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->bus), "failed to enable bus clock\n");

	i2s->clk = devm_clk_get_enabled(i2s->dev, "func");
	if (IS_ERR(i2s->clk))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->clk), "failed to enable func clock\n");

	i2s->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(i2s->base))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->base), "failed to map registers\n");

	i2s->reset = devm_reset_control_get_exclusive(&pdev->dev, "reset");
	if (IS_ERR(i2s->reset))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->reset),
				     "failed to get reset control");

	i2s->reset_sys = devm_reset_control_get_exclusive(&pdev->dev, "reset-sys");
	if (IS_ERR(i2s->reset_sys))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->reset_sys),
				     "failed to get reset_sys control");

	dev_set_drvdata(i2s->dev, i2s);

	spacemit_i2s_init_dai(i2s, &dai, res->start);

	i2s->buf_base = devm_platform_get_and_ioremap_resource(pdev, 1, &res);
	if (IS_ERR(i2s->buf_base))
		return dev_err_probe(i2s->dev, PTR_ERR(i2s->buf_base),
				    "failed to map DMA buffer addr\n");

	i2s->buf_addr = res->start;
	i2s->buf_size = res->end - res->start + 1;

	ret = devm_snd_soc_register_component(i2s->dev,
					      &spacemit_i2s_component,
					      dai, 1);
	if (ret)
		return dev_err_probe(i2s->dev, ret, "failed to register component");

	return devm_snd_dmaengine_pcm_register(&pdev->dev, &spacemit_dmaengine_pcm_config, 0);
}

static const struct of_device_id spacemit_i2s_of_match[] = {
	{ .compatible = "spacemit,k3-ri2s", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_i2s_of_match);

static struct platform_driver spacemit_i2s_driver = {
	.probe = spacemit_i2s_probe,
	.driver = {
		.name = "k3-ri2s",
		.of_match_table = spacemit_i2s_of_match,
	},
};
module_platform_driver(spacemit_i2s_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RI2S bus driver for SpacemiT K3 SoC");
