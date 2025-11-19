// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include "inno_conn.h"
#include "inno_utils.h"
#include "inno_dp_audio.h"

static int inno_dp_dai_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct inno_conn_t *conn = snd_soc_dai_get_drvdata(dai);
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
	osal_write32(0x300, (osal_read32(0x300, conn) & ~GENMASK(24, 23))
			    | (mode << 23), conn);
	return 0;
}

static int inno_dp_dai_pcm_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct inno_conn_t *conn = snd_soc_dai_get_drvdata(dai);
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
	osal_write32(0x300, (osal_read32(0x300, conn) & ~GENMASK(21, 17))
			    | (data_bits << 17), conn);
	return 0;
}

static int inno_dp_dai_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct inno_conn_t *conn = snd_soc_dai_get_drvdata(dai);

	if (mute)
		osal_write32(0x300, osal_read32(0x300, conn) | BIT(30), conn);
	else
		osal_write32(0x300, osal_read32(0x300, conn) & ~BIT(30), conn);

	return 0;
}

const struct snd_soc_dai_ops inno_dp_dai_ops = {
	.hw_params = inno_dp_dai_pcm_hw_params,
	.set_fmt = inno_dp_dai_set_dai_fmt,
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

static int inno_dp_init_audio(struct inno_conn_t *conn)
{
	/* init dp audio */
	osal_write32(0x424, osal_read32(0x424, conn) | BIT(13), conn);
	osal_write32(0x420, osal_read32(0x420, conn) | BIT(13), conn);
	osal_write32(0x424, osal_read32(0x424, conn) | BIT(12), conn);
	osal_write32(0x420, osal_read32(0x420, conn) | BIT(12), conn);
	osal_write32(0x300, osal_read32(0x300, conn) & ~BIT(31), conn);
	osal_write32(0x300, osal_read32(0x300, conn) | BIT(22), conn);
	osal_write32(0x300, (osal_read32(0x300, conn) & ~GENMASK(16, 14))
			    | (0x01 << 14), conn);
	osal_write32(0x300, (osal_read32(0x300, conn) & ~GENMASK(28, 25))
			    | (0x01 << 25), conn);
	osal_write32(0x300, (osal_read32(0x300, conn) & ~GENMASK(21, 17))
			    | (0x10 << 17), conn);
	osal_write32(0x300, osal_read32(0x300, conn) & ~GENMASK(24, 23), conn);
	osal_write32(0x300, osal_read32(0x300, conn) & ~BIT(30), conn);
	osal_write32(0x01c, osal_read32(0x01c, conn) | BIT(28), conn);
	udelay(1000);
	osal_write32(0x01c, osal_read32(0x01c, conn) & ~BIT(28), conn);

	return 0;
}

static int inno_dp_dai_probe(struct snd_soc_component *component)
{
	struct inno_conn_t *conn = snd_soc_component_get_drvdata(component);

	inno_dp_init_audio(conn);
	return 0;
};

const struct snd_soc_component_driver soc_component_inno_dp = {
	.name = "inno-dp-audio",
	.probe = inno_dp_dai_probe,
};

int inno_dp_audio_register(struct device *dev)
{
	return snd_soc_register_component(dev,
					  &soc_component_inno_dp,
					  &inno_dp_dai_driver, 1);
}

void inno_dp_audio_unregister(struct device *dev)
{
	snd_soc_unregister_component(dev);
}
