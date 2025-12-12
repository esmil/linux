/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _DPU_DEBUG_H_
#define _DPU_DEBUG_H_

#include <linux/types.h>
//#include "saturn_regs/reg_map.h"
#include "../spacemit_crtc.h"

void dpu_dump_fps(struct spacemit_crtc *a_crtc);
int dpu_buffer_dump(struct drm_plane *plane);
void dpu_trace(struct work_struct *work);

#endif
