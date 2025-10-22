// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include "inno_modes.h"

#define GTF_PXL_CLK_GRAN	250000	/* pixel clock granularity */

#define GTF_MIN_VSYNC_BP	550	/* min time of vsync + back porch (us) */
#define GTF_V_FP		1	/* vertical front porch (lines) */
#define GTF_CELL_GRAN		8	/* character cell granularity */

/* Default */
#define GTF_D_M			600	/* blanking formula gradient */
#define GTF_D_C			40	/* blanking formula offset */
#define GTF_D_K			128	/* blanking formula scaling factor */
#define GTF_D_J			20	/* blanking formula scaling factor */
#define GTF_D_C_PRIME ((((GTF_D_C - GTF_D_J) * GTF_D_K) / 256) + GTF_D_J)
#define GTF_D_M_PRIME ((GTF_D_K * GTF_D_M) / 256)

/* Secondary */
#define GTF_S_M			3600	/* blanking formula gradient */
#define GTF_S_C			40	/* blanking formula offset */
#define GTF_S_K			128	/* blanking formula scaling factor */
#define GTF_S_J			35	/* blanking formula scaling factor */
#define GTF_S_C_PRIME ((((GTF_S_C - GTF_S_J) * GTF_S_K) / 256) + GTF_S_J)
#define GTF_S_M_PRIME ((GTF_S_K * GTF_S_M) / 256)

bool inno_mode_equal_no_flags(const struct drm_display_mode *mode1,
			      const struct drm_display_mode *mode2)
{
	if (mode1->clock == mode2->clock &&
	    mode1->hdisplay == mode2->hdisplay &&
	    mode1->hsync_start == mode2->hsync_start &&
	    mode1->hsync_end == mode2->hsync_end &&
	    mode1->htotal == mode2->htotal &&
	    mode1->vdisplay == mode2->vdisplay &&
	    mode1->vsync_start == mode2->vsync_start &&
	    mode1->vsync_end == mode2->vsync_end &&
	    mode1->vtotal == mode2->vtotal)
		return true;

	return false;
}

void inno_mode_set_name(struct drm_display_mode *mode)
{
}

int inno_mode_vrefresh(const struct drm_display_mode *mode)
{
	int refresh = 0;

	if (drm_mode_vrefresh(mode) > 0)
		refresh = drm_mode_vrefresh(mode);
	else if (mode->htotal > 0 && mode->vtotal > 0) {
		unsigned int num, den;

		num = mode->clock * 1000;
		den = mode->htotal * mode->vtotal;

		if (mode->flags & DRM_MODE_FLAG_INTERLACE)
			num *= 2;
		if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
			den *= 2;

		refresh = DIV_ROUND_CLOSEST(num, den);
	}
	return refresh;
}

int inno_mode_hsync(const struct drm_display_mode *mode)
{
	return mode->hsync_end - mode->hsync_start;
}

struct drm_display_mode *inno_mode_create(void)
{
	struct drm_display_mode *nmode;

	nmode = osal_malloc(sizeof(struct drm_display_mode));
	if (!nmode)
		return NULL;

	osal_memset(nmode, sizeof(struct drm_display_mode), 0);
	return nmode;
}

void inno_mode_copy(struct drm_display_mode *dst,
		    const struct drm_display_mode *src)
{
	struct list_head head = dst->head;

	osal_memcpy(dst, src, sizeof(struct drm_display_mode));
	dst->head = head;
}

struct drm_display_mode *inno_mode_duplicate(
					    const struct drm_display_mode *mode)
{
	struct drm_display_mode *nmode;

	nmode = inno_mode_create();
	if (!nmode)
		return NULL;

	inno_mode_copy(nmode, mode);

	return nmode;
}

struct drm_display_mode *
inno_gtf_mode_complex(int hdisplay, int vdisplay,
		     int vrefresh, bool interlaced, int margins,
		     int GTF_M, int GTF_2C, int GTF_K, int GTF_2J)
{	/* 1) top/bottom margin size (% of height) - default: 1.8, */
#define	GTF_MARGIN_PERCENTAGE		18
	/* 2) character cell horizontal granularity (pixels) - default 8 */
#define	GTF_CELL_GRAN			8
	/* 3) Minimum vertical porch (lines) - default 3 */
#define	GTF_MIN_V_PORCH			1
	/* width of vsync in lines */
#define V_SYNC_RQD			3
	/* width of hsync as % of total line */
#define H_SYNC_PERCENT			8
	/* min time of vsync + back porch (microsec) */
#define MIN_VSYNC_PLUS_BP		550
	/* C' and M' are part of the Blanking Duty Cycle computation */
#define GTF_C_PRIME	((((GTF_2C - GTF_2J) * GTF_K / 256) + GTF_2J) / 2)
#define GTF_M_PRIME	(GTF_K * GTF_M / 256)
	struct drm_display_mode *drm_mode;
	unsigned int hdisplay_rnd, vdisplay_rnd, vfieldrate_rqd;
	int top_margin, bottom_margin;
	int interlace;
	unsigned int hfreq_est;
	int vsync_plus_bp;
	unsigned int vtotal_lines;
	__maybe_unused unsigned int vfieldrate_est, hperiod, vback_porch, vframe_rate;
	unsigned int vfield_rate;
	int left_margin, right_margin;
	unsigned int total_active_pixels, ideal_duty_cycle;
	unsigned int hblank, total_pixels, pixel_freq;
	int hsync, hfront_porch, vodd_front_porch_lines;
	unsigned int tmp1, tmp2;

	if (!hdisplay || !vdisplay)
		return NULL;

	drm_mode = inno_mode_create();
	if (!drm_mode)
		return NULL;

	/* 1. In order to give correct results, the number of horizontal
	 * pixels requested is first processed to ensure that it is divisible
	 * by the character size, by rounding it to the nearest character
	 * cell boundary:
	 */
	hdisplay_rnd = (hdisplay + GTF_CELL_GRAN / 2) / GTF_CELL_GRAN;
	hdisplay_rnd = hdisplay_rnd * GTF_CELL_GRAN;

	/* 2. If interlace is requested, the number of vertical lines assumed
	 * by the calculation must be halved, as the computation calculates
	 * the number of vertical lines per field.
	 */
	if (interlaced)
		vdisplay_rnd = vdisplay / 2;
	else
		vdisplay_rnd = vdisplay;

	/* 3. Find the frame rate required: */
	if (interlaced)
		vfieldrate_rqd = vrefresh * 2;
	else
		vfieldrate_rqd = vrefresh;

	/* 4. Find number of lines in Top margin: */
	top_margin = 0;
	if (margins)
		top_margin = (vdisplay_rnd * GTF_MARGIN_PERCENTAGE + 500) /
				1000;
	/* 5. Find number of lines in bottom margin: */
	bottom_margin = top_margin;

	/* 6. If interlace is required, then set variable interlace: */
	if (interlaced)
		interlace = 1;
	else
		interlace = 0;

	/* 7. Estimate the Horizontal frequency */
	tmp1 = (1000000 - MIN_VSYNC_PLUS_BP * vfieldrate_rqd) / 500;
	tmp2 = (vdisplay_rnd + 2 * top_margin + GTF_MIN_V_PORCH) *
			2 + interlace;
	hfreq_est = (tmp2 * 1000 * vfieldrate_rqd) / tmp1;

	/* 8. Find the number of lines in V sync + back porch */
	vsync_plus_bp = MIN_VSYNC_PLUS_BP * hfreq_est / 1000;
	vsync_plus_bp = (vsync_plus_bp + 500) / 1000;

	/* 9. Find the number of lines in V back porch alone: */
	vback_porch = vsync_plus_bp - V_SYNC_RQD;

	/* 10. Find the total number of lines in Vertical field period: */
	vtotal_lines = vdisplay_rnd + top_margin + bottom_margin +
			vsync_plus_bp + GTF_MIN_V_PORCH;
	/*  11. Estimate the Vertical field frequency: */
	vfieldrate_est = hfreq_est / vtotal_lines;
	/*  12. Find the actual horizontal period: */
	hperiod = 1000000 / (vfieldrate_rqd * vtotal_lines);

	/* 13. Find the actual Vertical field frequency: */
	vfield_rate = hfreq_est / vtotal_lines;

	/* 14. Find the Vertical frame frequency: */
	if (interlaced)
		vframe_rate = vfield_rate / 2;
	else
		vframe_rate = vfield_rate;

	/* 15. Find number of pixels in left margin: */
	if (margins)
		left_margin = (hdisplay_rnd * GTF_MARGIN_PERCENTAGE + 500) /
				1000;
	else
		left_margin = 0;

	/* 16. Find number of pixels in right margin: */
	right_margin = left_margin;

	/* 17. Find total number of active pixels in image and left and right */
	total_active_pixels = hdisplay_rnd + left_margin + right_margin;

	/* 18. Find the ideal blanking duty cycle from blanking duty cycle */
	ideal_duty_cycle = GTF_C_PRIME * 1000 -
				(GTF_M_PRIME * 1000000 / hfreq_est);

	/* 19. Find the number of pixels in the blanking time to the nearest
	 * double character cell:
	 */
	hblank = total_active_pixels * ideal_duty_cycle /
			(100000 - ideal_duty_cycle);
	hblank = (hblank + GTF_CELL_GRAN) / (2 * GTF_CELL_GRAN);
	hblank = hblank * 2 * GTF_CELL_GRAN;
	/* 20.Find total number of pixels: */
	total_pixels = total_active_pixels + hblank;

	/* 21. Find pixel clock frequency: */
	pixel_freq = total_pixels * hfreq_est / 1000;
	/* Stage 1 computations are now complete; I should really pass
	 * the results to another function and do the Stage 2 computations,
	 * but I only need a few more values so I'll just append the
	 * computations here for now
	 */
	/* 17. Find the number of pixels in the horizontal sync period: */
	hsync = H_SYNC_PERCENT * total_pixels / 100;
	hsync = (hsync + GTF_CELL_GRAN / 2) / GTF_CELL_GRAN;
	hsync = hsync * GTF_CELL_GRAN;
	/* 18. Find the number of pixels in horizontal front porch period */
	hfront_porch = hblank / 2 - hsync;

	/* 36. Find the number of lines in the odd front porch period: */
	vodd_front_porch_lines = GTF_MIN_V_PORCH;

	/* finally, pack the results in the mode struct */
	drm_mode->hdisplay = hdisplay_rnd;
	drm_mode->hsync_start = hdisplay_rnd + hfront_porch;
	drm_mode->hsync_end = drm_mode->hsync_start + hsync;
	drm_mode->htotal = total_pixels;
	drm_mode->vdisplay = vdisplay_rnd;
	drm_mode->vsync_start = vdisplay_rnd + vodd_front_porch_lines;
	drm_mode->vsync_end = drm_mode->vsync_start + V_SYNC_RQD;
	drm_mode->vtotal = vtotal_lines;

	drm_mode->clock = pixel_freq;

	if (interlaced) {
		drm_mode->vtotal *= 2;
		drm_mode->flags |= DRM_MODE_FLAG_INTERLACE;
	}

	inno_mode_set_name(drm_mode);

	if (GTF_M == 600 && GTF_2C == 80 && GTF_K == 128 && GTF_2J == 40)
		drm_mode->flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC;
	else
		drm_mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC;

	return drm_mode;
}

struct drm_display_mode *
inno_gtf_mode(int hdisplay, int vdisplay, int vrefresh,
	      bool interlaced, int margins)
{
	return inno_gtf_mode_complex(hdisplay, vdisplay, vrefresh,
				     interlaced, margins,
				     600, 40 * 2, 128, 20 * 2);
}

/**
 * drm_cvt_mode -create a modeline based on the CVT algorithm
 * @hdisplay: hdisplay size
 * @vdisplay: vdisplay size
 * @vrefresh: vrefresh rate
 * @reduced: whether to use reduced blanking
 * @interlaced: whether to compute an interlaced mode
 * @margins: whether to add margins (borders)
 *
 * This function is called to generate the modeline based on CVT algorithm
 * according to the hdisplay, vdisplay, vrefresh.
 * It is based from the VESA(TM) Coordinated Video Timing Generator by
 * Graham Loveridge April 9, 2003 available at
 * http://www.elo.utfsm.cl/~elo212/docs/CVTd6r1.xls
 *
 * And it is copied from xf86CVTmode in xserver/hw/xfree86/modes/xf86cvt.c.
 * What I have done is to translate it by using integer calculation.
 *
 * Returns:
 * The modeline based on the CVT algorithm stored in a drm_inno_mode object.
 * The display mode object is allocated with drm_mode_create(). Returns NULL
 * when no mode could be allocated.
 */
struct drm_display_mode *inno_cvt_mode(int hdisplay,
				       int vdisplay, int vrefresh,
				       bool reduced, bool interlaced, bool margins)
{
#define HV_FACTOR			1000
	/* 1) top/bottom margin size (% of height) - default: 1.8, */
#define CVT_MARGIN_PERCENTAGE		18
	/* 2) character cell horizontal granularity (pixels) - default 8 */
#define CVT_H_GRANULARITY		8
	/* 3) Minimum vertical porch (lines) - default 3 */
#define CVT_MIN_V_PORCH			3
	/* 4) Minimum number of vertical back porch lines - default 6 */
#define CVT_MIN_V_BPORCH		6
	/* Pixel Clock step (kHz) */
#define CVT_CLOCK_STEP			250
	struct drm_display_mode *drm_mode;
	unsigned int vfieldrate, hperiod;
	int hdisplay_rnd, hmargin, vdisplay_rnd, vmargin, vsync;
	int interlace;
	unsigned long long tmp;

	if (!hdisplay || !vdisplay)
		return NULL;

	/* allocate the drm_inno_mode structure. If failure, we will
	 * return directly
	 */
	drm_mode = inno_mode_create();
	if (!drm_mode)
		return NULL;

	/* the CVT default refresh rate is 60Hz */
	if (!vrefresh)
		vrefresh = 60;

	/* the required field fresh rate */
	if (interlaced)
		vfieldrate = vrefresh * 2;
	else
		vfieldrate = vrefresh;

	/* horizontal pixels */
	hdisplay_rnd = hdisplay - (hdisplay % CVT_H_GRANULARITY);

	/* determine the left&right borders */
	hmargin = 0;
	if (margins) {
		hmargin = hdisplay_rnd * CVT_MARGIN_PERCENTAGE / 1000;
		hmargin -= hmargin % CVT_H_GRANULARITY;
	}
	/* find the total active pixels */
	drm_mode->hdisplay = hdisplay_rnd + 2 * hmargin;

	/* find the number of lines per field */
	if (interlaced)
		vdisplay_rnd = vdisplay / 2;
	else
		vdisplay_rnd = vdisplay;

	/* find the top & bottom borders */
	vmargin = 0;
	if (margins)
		vmargin = vdisplay_rnd * CVT_MARGIN_PERCENTAGE / 1000;

	drm_mode->vdisplay = vdisplay + 2 * vmargin;

	/* Interlaced */
	if (interlaced)
		interlace = 1;
	else
		interlace = 0;

	/* Determine VSync Width from aspect ratio */
	if (!(vdisplay % 3) && ((vdisplay * 4 / 3) == hdisplay))
		vsync = 4;
	else if (!(vdisplay % 9) && ((vdisplay * 16 / 9) == hdisplay))
		vsync = 5;
	else if (!(vdisplay % 10) && ((vdisplay * 16 / 10) == hdisplay))
		vsync = 6;
	else if (!(vdisplay % 4) && ((vdisplay * 5 / 4) == hdisplay))
		vsync = 7;
	else if (!(vdisplay % 9) && ((vdisplay * 15 / 9) == hdisplay))
		vsync = 7;
	else /* custom */
		vsync = 10;

	if (!reduced) {
		/* simplify the GTF calculation */
		/* 4) Minimum time of vertical sync + back porch interval (µs)
		 * default 550.0
		 */
		int tmp1, tmp2;
#define CVT_MIN_VSYNC_BP	550
		/* 3) Nominal HSync width (% of line period) - default 8 */
#define CVT_HSYNC_PERCENTAGE	8
		unsigned int hblank_percentage;
		int vsyncandback_porch, hblank;
		__maybe_unused int vback_porch;

		/* estimated the horizontal period */
		tmp1 = HV_FACTOR * 1000000 -
			CVT_MIN_VSYNC_BP * HV_FACTOR * vfieldrate;
		tmp2 = (vdisplay_rnd + 2 * vmargin + CVT_MIN_V_PORCH) * 2 +
			interlace;
		hperiod = tmp1 * 2 / (tmp2 * vfieldrate);

		tmp1 = CVT_MIN_VSYNC_BP * HV_FACTOR / hperiod + 1;
		/* 9. Find number of lines in sync + backporch */
		if (tmp1 < (vsync + CVT_MIN_V_PORCH))
			vsyncandback_porch = vsync + CVT_MIN_V_PORCH;
		else
			vsyncandback_porch = tmp1;
		/* 10. Find number of lines in back porch */
		vback_porch = vsyncandback_porch - vsync;
		drm_mode->vtotal = vdisplay_rnd + 2 * vmargin +
				vsyncandback_porch + CVT_MIN_V_PORCH;
		/* 5) Definition of Horizontal blanking time limitation */
		/* Gradient (%/kHz) - default 600 */
#define CVT_M_FACTOR	600
		/* Offset (%) - default 40 */
#define CVT_C_FACTOR	40
		/* Blanking time scaling factor - default 128 */
#define CVT_K_FACTOR	128
		/* Scaling factor weighting - default 20 */
#define CVT_J_FACTOR	20
#define CVT_M_PRIME	(CVT_M_FACTOR * CVT_K_FACTOR / 256)
#define CVT_C_PRIME	((CVT_C_FACTOR - CVT_J_FACTOR) * CVT_K_FACTOR / 256 + \
			 CVT_J_FACTOR)
		/* 12. Find ideal blanking duty cycle from formula */
		hblank_percentage = CVT_C_PRIME * HV_FACTOR - CVT_M_PRIME *
					hperiod / 1000;
		/* 13. Blanking time */
		if (hblank_percentage < 20 * HV_FACTOR)
			hblank_percentage = 20 * HV_FACTOR;
		hblank = drm_mode->hdisplay * hblank_percentage /
			 (100 * HV_FACTOR - hblank_percentage);
		hblank -= hblank % (2 * CVT_H_GRANULARITY);
		/* 14. find the total pixels per line */
		drm_mode->htotal = drm_mode->hdisplay + hblank;
		drm_mode->hsync_end = drm_mode->hdisplay + hblank / 2;
		drm_mode->hsync_start = drm_mode->hsync_end -
			(drm_mode->htotal * CVT_HSYNC_PERCENTAGE) / 100;
		drm_mode->hsync_start += CVT_H_GRANULARITY -
			drm_mode->hsync_start % CVT_H_GRANULARITY;
		/* fill the Vsync values */
		drm_mode->vsync_start = drm_mode->vdisplay + CVT_MIN_V_PORCH;
		drm_mode->vsync_end = drm_mode->vsync_start + vsync;
	} else {
		/* Reduced blanking */
		/* Minimum vertical blanking interval time (µs)- default 460 */
#define CVT_RB_MIN_VBLANK	460
		/* Fixed number of clocks for horizontal sync */
#define CVT_RB_H_SYNC		32
		/* Fixed number of clocks for horizontal blanking */
#define CVT_RB_H_BLANK		160
		/* Fixed number of lines for vertical front porch - default 3*/
#define CVT_RB_VFPORCH		3
		int vbilines;
		int tmp1, tmp2;
		/* 8. Estimate Horizontal period. */
		tmp1 = HV_FACTOR * 1000000 -
			CVT_RB_MIN_VBLANK * HV_FACTOR * vfieldrate;
		tmp2 = vdisplay_rnd + 2 * vmargin;
		hperiod = tmp1 / (tmp2 * vfieldrate);
		/* 9. Find number of lines in vertical blanking */
		vbilines = CVT_RB_MIN_VBLANK * HV_FACTOR / hperiod + 1;
		/* 10. Check if vertical blanking is sufficient */
		if (vbilines < (CVT_RB_VFPORCH + vsync + CVT_MIN_V_BPORCH))
			vbilines = CVT_RB_VFPORCH + vsync + CVT_MIN_V_BPORCH;
		/* 11. Find total number of lines in vertical field */
		drm_mode->vtotal = vdisplay_rnd + 2 * vmargin + vbilines;
		/* 12. Find total number of pixels in a line */
		drm_mode->htotal = drm_mode->hdisplay + CVT_RB_H_BLANK;
		/* Fill in HSync values */
		drm_mode->hsync_end = drm_mode->hdisplay + CVT_RB_H_BLANK / 2;
		drm_mode->hsync_start = drm_mode->hsync_end - CVT_RB_H_SYNC;
		/* Fill in VSync values */
		drm_mode->vsync_start = drm_mode->vdisplay + CVT_RB_VFPORCH;
		drm_mode->vsync_end = drm_mode->vsync_start + vsync;
	}
	/* 15/13. Find pixel clock frequency (kHz for xf86) */
	tmp = drm_mode->htotal; /* perform intermediate calcs in u64 */
	tmp *= HV_FACTOR * 1000;
	do_div(tmp, hperiod);
	tmp -= drm_mode->clock % CVT_CLOCK_STEP;
	drm_mode->clock = tmp;
	/* 18/16. Find actual vertical frame frequency */
	/* ignore - just set the mode flag for interlaced */
	if (interlaced) {
		drm_mode->vtotal *= 2;
		drm_mode->flags |= DRM_MODE_FLAG_INTERLACE;
	}

	/* Fill the mode line name */
	inno_mode_set_name(drm_mode);

	if (reduced)
		drm_mode->flags |= (DRM_MODE_FLAG_PHSYNC |
					DRM_MODE_FLAG_NVSYNC);
	else
		drm_mode->flags |= (DRM_MODE_FLAG_PVSYNC |
					DRM_MODE_FLAG_NHSYNC);

	return drm_mode;
}

enum drm_mode_status
inno_mode_validate_basic(const struct drm_display_mode *mode)
{
	if (mode->clock == 0)
		return MODE_CLOCK_LOW;

	if (mode->hdisplay == 0 ||
	    mode->hsync_start < mode->hdisplay ||
	    mode->hsync_end < mode->hsync_start ||
	    mode->htotal < mode->hsync_end)
		return MODE_H_ILLEGAL;

	if (mode->vdisplay == 0 ||
	    mode->vsync_start < mode->vdisplay ||
	    mode->vsync_end < mode->vsync_start ||
	    mode->vtotal < mode->vsync_end)
		return MODE_V_ILLEGAL;

	return MODE_OK;
}

enum drm_mode_status
inno_mode_validate_size(const struct drm_display_mode *mode,
			int maxX, int maxY)
{
	if (maxX > 0 && mode->hdisplay > maxX)
		return MODE_VIRTUAL_X;

	if (maxY > 0 && mode->vdisplay > maxY)
		return MODE_VIRTUAL_Y;

	return MODE_OK;
}

enum drm_mode_status
inno_mode_validate_flag(const struct drm_display_mode *mode,
			int flags)
{
	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) &&
	    !(flags & DRM_MODE_FLAG_INTERLACE))
		return MODE_NO_INTERLACE;

	if ((mode->flags & DRM_MODE_FLAG_DBLSCAN) &&
	    !(flags & DRM_MODE_FLAG_DBLSCAN))
		return MODE_NO_DBLESCAN;

	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) &&
	    !(flags & DRM_MODE_FLAG_3D_MASK))
		return MODE_NO_STEREO;

	return MODE_OK;
}

void inno_mode_prune_invalid(struct list_head *mode_list)
{
	struct drm_display_mode *mode, *t;

	list_for_each_entry_safe(mode, t, mode_list, head) {
		if (mode->status != MODE_OK) {
			list_del(&mode->head);
			osal_free(mode);
		}
	}
}

static struct drm_display_mode edid_cvtrb_modes[] = {
	{ DRM_MODE("2240x1400", DRM_MODE_TYPE_DRIVER, 212500, 2240, 2240 + 48,
			   2240 + 48 + 32, 2240 + 220, 0, 1400, 1400 + 3, 1400 + 3 + 6,
			   1400 + 40, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3},

	{ DRM_MODE("2240x1400", DRM_MODE_TYPE_DRIVER, 141696, 2240, 2240 + 48,
			   2240 + 48 + 32, 2240 + 220, 0, 1400, 1400 + 3, 1400 + 3 + 6,
			   1400 + 40, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_4_3},

	/* 2160x1400@30Hz 16:9 */
	{ DRM_MODE("2160x1400", DRM_MODE_TYPE_DRIVER, 98750, 2160, 2208,
			   2240, 2320, 0, 1400, 1403, 1413, 1420, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = 0, },

	/* 2160x1400@60Hz 16:9 */
	{ DRM_MODE("2160x1400", DRM_MODE_TYPE_DRIVER, 200250, 2160, 2208,
			   2240, 2320, 0, 1400, 1403, 1413, 1440, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = 0, },

	/* 2160x1400@75Hz 16:9 */
	{ DRM_MODE("2160x1400", DRM_MODE_TYPE_DRIVER, 252250, 2160, 2208,
			   2240, 2320, 0, 1400, 1403, 1413, 1451, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = 0, },


	/* 2560x1440@30Hz 16:9 */
	{ DRM_MODE("2560x1440", DRM_MODE_TYPE_DRIVER, 119000, 2560, 2608,
			   2640, 2720, 0, 1440, 1443, 1448, 1461, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },

	/* 2560x1440@60Hz 16:9 */
	{ DRM_MODE("2560x1440", DRM_MODE_TYPE_DRIVER, 241500, 2560, 2608,
			   2640, 2720, 0, 1440, 1443, 1448, 1481, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },

	/* 2560x1440@75Hz 16:9 */
	{ DRM_MODE("2560x1440", DRM_MODE_TYPE_DRIVER, 304250, 2560, 2608,
			   2640, 2720, 0, 1440, 1443, 1448, 1492, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },

	/* 2560x1600@30Hz 16:9 */
	{ DRM_MODE("2560x1600", DRM_MODE_TYPE_DRIVER, 128541, 2560, 2568,
			   2600, 2640, 0, 1600, 1609, 1617, 1623, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 2560x1600@60Hz 16:9 */
	{ DRM_MODE("2560x1600", DRM_MODE_TYPE_DRIVER, 260726, 2560, 2568,
			   2600, 2640, 0, 1600, 1632, 1640, 1646, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 2560x1600@75Hz 16:9 */
	{ DRM_MODE("2560x1600", DRM_MODE_TYPE_DRIVER, 328284, 2560, 2568,
			   2600, 2640, 0, 1600, 1644, 1652, 1658, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 3840x2160@30Hz 16:9 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 297000, 3840, 4016,
			   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 3840x2160@60Hz 16:9 */
	{ DRM_MODE("3840x2160", DRM_MODE_TYPE_DRIVER, 594000, 3840, 4016,
			   4104, 4400, 0, 2160, 2168, 2178, 2250, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
	/* 3840x1080@60Hz 32:9*/
	{ DRM_MODE("3840x1080", DRM_MODE_TYPE_DRIVER, 292350, 3840, 4018,
			   4178, 4388, 0, 1080, 1083, 1093, 1111, 0,
			   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
			   .picture_aspect_ratio = HDMI_PICTURE_ASPECT_16_9, },
};

static bool inno_modes_is_match(struct drm_display_mode *mode1,
				struct drm_display_mode *mode2)
{
	if (mode1->hdisplay == mode2->hdisplay &&
	    mode1->vdisplay == mode2->vdisplay &&
	    abs(inno_mode_vrefresh(mode1) - inno_mode_vrefresh(mode2)) <= 1) {
		return true;
	}

	return false;
}

int inno_modes_replace_timing(struct drm_display_mode *mode)
{
	struct drm_display_mode *rmode;
	int i;
	int ret = false;

	for (i = 0; i < ARRAY_SIZE(edid_cvtrb_modes); i++) {
		rmode = &edid_cvtrb_modes[i];

		if (inno_modes_is_match(mode, rmode)) {
			mode->clock = rmode->clock;
			mode->hdisplay = rmode->hdisplay;
			mode->hsync_start = rmode->hsync_start;
			mode->hsync_end = rmode->hsync_end;
			mode->htotal = rmode->htotal;
			mode->hskew = rmode->hskew;
			mode->vdisplay = rmode->vdisplay;
			mode->vsync_start = rmode->vsync_start;
			mode->vsync_end = rmode->vsync_end;
			mode->vtotal = rmode->vtotal;
			mode->vscan = rmode->vscan;
			mode->flags = rmode->flags;
			mode->picture_aspect_ratio = rmode->picture_aspect_ratio;
			mode->type = (mode->type & DRM_MODE_TYPE_PREFERRED) ?
					(rmode->type | DRM_MODE_TYPE_PREFERRED) : rmode->type;
			ret = true;
			break;
		}
	}

	return ret;
}

struct drm_display_mode *inno_mode_find_out_mode(int perfect_w, int perfect_h,
						 struct list_head *mode_list)
{
	struct drm_display_mode *mode, *t;
	struct drm_display_mode *min_mode = NULL;

	/* find mode */
	list_for_each_entry_safe(mode, t, mode_list, head) {
		if (mode->hdisplay <= perfect_w &&
		    mode->vdisplay <= perfect_h &&
		    mode->hdisplay % 16 == 0 &&
		    inno_mode_vrefresh(mode) <= 60) {
			return mode;
		}
	}

	list_for_each_entry_safe(mode, t, mode_list, head) {
		if (mode->hdisplay % 16 != 0)
			continue;

		if (!min_mode)
			min_mode = mode;

		if (min_mode && min_mode->hdisplay > mode->hdisplay)
			min_mode = mode;
	}

	return min_mode;
}

static int drm_mode_compare(void *priv, struct list_head *lh_a,
			    struct list_head *lh_b)
{
	struct drm_display_mode *a = list_entry(lh_a, struct drm_display_mode, head);
	struct drm_display_mode *b = list_entry(lh_b, struct drm_display_mode, head);
	int diff;

	diff = ((b->type & DRM_MODE_TYPE_PREFERRED) != 0) -
		((a->type & DRM_MODE_TYPE_PREFERRED) != 0);
	if (diff)
		return diff;
	diff = b->hdisplay * b->vdisplay - a->hdisplay * a->vdisplay;
	if (diff)
		return diff;

	diff = drm_mode_vrefresh(b) - drm_mode_vrefresh(a);
	if (diff)
		return diff;

	diff = b->clock - a->clock;
	return diff;
}

void list_sort(void *priv, struct list_head *head,
	       int (*cmp)(void *priv, struct list_head *a,
			  struct list_head *b));

void inno_mode_sort(struct list_head *mode_list)
{
	list_sort(NULL, mode_list, drm_mode_compare);
}
