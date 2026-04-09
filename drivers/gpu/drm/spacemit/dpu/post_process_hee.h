/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SATURN_hee_POST_H_
#define _SATURN_hee_POST_H_
#include "../spacemit_crtc.h"

int saturn_hee_check_end_matrix(struct drm_crtc_state *state);
void saturn_hee_conf_dpuctrl_pp_gamma(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
void saturn_hee_conf_dpuctrl_color_matrix(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
void saturn_hee_conf_dpuctrl_acad(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
void saturn_hee_conf_dpuctrl_ee(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
void saturn_hee_dpuctrl_color_temp(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
#endif
