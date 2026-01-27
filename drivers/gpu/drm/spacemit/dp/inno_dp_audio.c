// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include "inno_conn.h"
#include "inno_utils.h"
#include "inno_dp_audio.h"

#if IS_ENABLED(CONFIG_SND_SOC)
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
	conn->aud_mode = mode;
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
	osal_write32(0x300, (osal_read32(0x300, conn) & ~GENMASK(24, 23))
			    | (conn->aud_mode << 23), conn);
	osal_write32(0x01c, osal_read32(0x01c, conn) | BIT(28), conn);
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

static int inno_dp_dai_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct inno_conn_t *conn = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		osal_write32(0x01c, osal_read32(0x01c, conn) & ~BIT(28), conn);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		osal_write32(0x01c, osal_read32(0x01c, conn) | BIT(28), conn);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

const struct snd_soc_dai_ops inno_dp_dai_ops = {
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
#endif

