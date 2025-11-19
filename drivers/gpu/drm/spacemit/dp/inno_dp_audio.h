/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef __INNO_DP_AUDIO_H__
#define __INNO_DP_AUDIO_H__

#include <sound/pcm_params.h>
#include <sound/soc.h>

int inno_dp_audio_register(struct device *dev);
void inno_dp_audio_unregister(struct device *dev);

#endif /* __INNO_DP_AUDIO_H__ */
