/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef __INNO_MODE_H__
#define __INNO_MODE_H__

#include <linux/i2c.h>
#include <drm/drm.h>
#include <drm/drm_modes.h>
#include <drm/drm_edid.h>

#include "inno_utils.h"

#define INNO_MODE_LEN		32

#define for_each_displaymode(mode, mode_head) \
	list_for_each_entry(mode, mode_head, head)
#define for_each_displaymode_safe(mode, mode2, mode_head) \
	list_for_each_entry_safe(mode, mode2, mode_head, head)

extern bool inno_mode_equal_no_flags(const struct drm_display_mode *mode1,
				     const struct drm_display_mode *mode2);
extern void inno_mode_set_name(struct drm_display_mode *mode);
extern int inno_mode_vrefresh(const struct drm_display_mode *mode);
extern int inno_mode_hsync(const struct drm_display_mode *mode);
extern struct drm_display_mode *inno_mode_create(void);
extern void inno_mode_copy(struct drm_display_mode *dst, const struct drm_display_mode *src);
extern struct drm_display_mode *inno_mode_duplicate(
	const struct drm_display_mode *mode);
extern struct drm_display_mode *inno_gtf_mode_complex(int hdisplay, int vdisplay,
						      int vrefresh, bool interlaced,
						      int margins, int GTF_M,
						      int GTF_2C, int GTF_K,
						      int GTF_2J);
extern struct drm_display_mode *inno_gtf_mode(int hdisplay, int vdisplay, int vrefresh,
					      bool interlaced, int margins);
extern struct drm_display_mode *inno_cvt_mode(int hdisplay,
					      int vdisplay, int vrefresh,
					      bool reduced, bool interlaced,
					      bool margins);
enum drm_mode_status
inno_mode_validate_basic(const struct drm_display_mode *mode);
enum drm_mode_status
inno_mode_validate_size(const struct drm_display_mode *mode,
			int maxX, int maxY);
enum drm_mode_status
inno_mode_validate_flag(const struct drm_display_mode *mode,
			int flags);
void inno_mode_prune_invalid(struct list_head *mode_list);
struct drm_display_mode *inno_mode_find_out_mode(int perfect_w,
						 int perfect_h, struct list_head *mode_list);
int inno_modes_replace_timing(struct drm_display_mode *mode);
void inno_mode_sort(struct list_head *mode_list);

#endif /* __INNO_MODE_H__ */
