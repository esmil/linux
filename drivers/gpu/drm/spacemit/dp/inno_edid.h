/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 */

#ifndef __INNO_EDID_H__
#define __INNO_EDID_H__

#include "inno_utils.h"
#include "inno_modes.h"

#define EDID_FEATURE_DEFAULT_GTF	(1 << 0)
#define EDID_FEATURE_PREFERRED_TIMING	(1 << 1)
#define EDID_FEATURE_STANDARD_COLOR	(1 << 2)

#define EDID_DETAIL_EST_TIMINGS		0xf7
#define EDID_DETAIL_CVT_3BYTE		0xf8
#define EDID_DETAIL_COLOR_MGMT_DATA	0xf9
#define EDID_DETAIL_STD_MODES		0xfa
#define EDID_DETAIL_MONITOR_CPDATA	0xfb
#define EDID_DETAIL_MONITOR_NAME	0xfc
#define EDID_DETAIL_MONITOR_RANGE	0xfd
#define EDID_DETAIL_MONITOR_STRING	0xfe
#define EDID_DETAIL_MONITOR_SERIAL	0xff

#define CEA_EXT				0x02
#define VTB_EXT				0x10
#define DI_EXT				0x40
#define LS_EXT				0x50
#define MI_EXT				0x60
#define DISPLAYID_EXT			0x70

/* 00=16:10, 01=4:3, 10=5:4, 11=16:9 */
#define EDID_TIMING_ASPECT_SHIFT	6
#define EDID_TIMING_ASPECT_MASK		(0x3 << EDID_TIMING_ASPECT_SHIFT)

/* need to add 60 */
#define EDID_TIMING_VFREQ_SHIFT		0
#define EDID_TIMING_VFREQ_MASK		(0x3f << EDID_TIMING_VFREQ_SHIFT)

#define EDID_EST_TIMINGS		16
#define EDID_STD_TIMINGS		8
#define EDID_DETAILED_TIMINGS		4
#define EDID_LENGTH			128
#define BLOCK0_DTD_START		0x36
#define BLOCK0_TOTAL_DTD		4
#define DTD_SIZE			18

bool inno_edid_is_valid(struct edid *edid);
int inno_edid_mode_add_list(const unsigned char *edid, struct list_head *modes);
int inno_edid_mode_free_list(struct list_head *modes);
bool inno_detect_hdmi_monitor(struct edid *edid);

uint8_t inno_mode_match_cea_mode(const struct drm_display_mode *to_match);
void inno_mode_copy_cea(struct drm_display_mode *dst, uint8_t vic);

#endif /* __INNO_EDID_H__ */

