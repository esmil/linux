// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/component.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/trace_events.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <dt-bindings/display/spacemit_dpu.h>
#include <linux/reset.h>
#include "spacemit_cmdlist.h"
#include "spacemit_dmmu.h"
#include "spacemit_drm.h"
#include "spacemit_crtc.h"
#include "spacemit_wb.h"
#include "spacemit_gem.h"
#include "spacemit_lib.h"
#include "spacemit_bootloader.h"
#include "dpu/dpu_saturn.h"
#include "dpu/dpu_debug.h"
#include "sysfs/sysfs_display.h"
#include "dpu/dpu_trace.h"

LIST_HEAD(dpu_core_head);

static int spacemit_crtc_init(struct spacemit_crtc *a_crtc);
static int spacemit_crtc_uninit(struct spacemit_crtc *a_crtc);
static int dpu_pm_suspend(struct device *dev);
static int dpu_pm_resume(struct device *dev);
static unsigned int spacemit_dpu_get_bootlogo_total_count(void);
static DEFINE_MUTEX(spacemit_bootloader_mem_setup_lock);
static bool spacemit_bootloader_mem_setup_done = false;

static struct device_node *spacemit_dpu_find_bootloader_mem_node(void)
{
	struct device_node *rmem_np, *child;

	rmem_np = of_find_node_by_path("/reserved-memory");
	if (!rmem_np)
		return NULL;

	for_each_child_of_node(rmem_np, child) {
		if (of_device_is_compatible(child, "framebuffer") ||
		    !strncmp(child->name, "framebuffer",
			     sizeof("framebuffer") - 1)) {
			of_node_put(rmem_np);
			return child;
		}
	}

	of_node_put(rmem_np);
	return NULL;
}

static void spacemit_dpu_setup_bootloader_mem(struct device *dev)
{
	struct device_node *np;
	struct resource rsrv_mem;
	struct reserved_mem rmem;
	int ret;

	mutex_lock(&spacemit_bootloader_mem_setup_lock);
	if (spacemit_bootloader_mem_setup_done) {
		mutex_unlock(&spacemit_bootloader_mem_setup_lock);
		return;
	}

	np = spacemit_dpu_find_bootloader_mem_node();
	if (!np) {
		mutex_unlock(&spacemit_bootloader_mem_setup_lock);
		return;
	}

	ret = of_address_to_resource(np, 0, &rsrv_mem);
	if (ret < 0) {
		DRM_INFO("no bootloader reserved memory resource found\n");
		of_node_put(np);
		mutex_unlock(&spacemit_bootloader_mem_setup_lock);
		return;
	}

	rmem.base = rsrv_mem.start;
	rmem.size = resource_size(&rsrv_mem);
	spacemit_dpu_set_bootloader_mem_release_target(spacemit_dpu_get_bootlogo_total_count());
	ret = spacemit_dpu_bootloader_mem_setup(&rmem);
	if (ret)
		DRM_INFO("failed to setup bootloader reserved memory: %d\n", ret);
	else
		spacemit_bootloader_mem_setup_done = true;

	of_node_put(np);
	mutex_unlock(&spacemit_bootloader_mem_setup_lock);
}

static unsigned int spacemit_dpu_get_bootlogo_total_count(void)
{
	struct device_node *np;
	unsigned int count = 0;

	for_each_compatible_node(np, NULL, "spacemit,dpu-saturn") {
		if (of_device_is_available(np))
			count++;
	}

	return count ? count : 1;
}

static atomic_t mclk_cnt = ATOMIC_INIT(0);
bool dpu_mclk_exclusive_get(void)
{
	if (atomic_cmpxchg(&mclk_cnt, 0, 1) == 0)
		return true;
	else
		return false;
}
EXPORT_SYMBOL(dpu_mclk_exclusive_get);

void dpu_mclk_exclusive_put(void)
{
	atomic_set(&mclk_cnt, 0);
}
EXPORT_SYMBOL(dpu_mclk_exclusive_put);

static BLOCKING_NOTIFIER_HEAD(dpu_max_mclk_notifier_list);
int dpu_max_mclk_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&dpu_max_mclk_notifier_list, nb);
}
EXPORT_SYMBOL(dpu_max_mclk_notifier_register);

int dpu_max_mclk_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&dpu_max_mclk_notifier_list, nb);
}
EXPORT_SYMBOL(dpu_max_mclk_notifier_unregister);

int dpu_max_mclk_notifier_call_chain(unsigned int mclk_rate)
{
	return blocking_notifier_call_chain(&dpu_max_mclk_notifier_list, (unsigned long)mclk_rate, NULL);
}
EXPORT_SYMBOL(dpu_max_mclk_notifier_call_chain);

static int spacemit_crtc_atomic_check_color_matrix(struct drm_crtc *crtc,
					  struct drm_crtc_state *state)
{
	struct spacemit_drm_private *priv = crtc->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	int ret = 0;

	if (hwdev->check_end_matrix)
		ret = hwdev->check_end_matrix(state);

	return ret;
}

static int spacemit_crtc_atomic_check_color_temp(struct drm_crtc *crtc,
					  struct drm_crtc_state *state)
{
	struct spacemit_crtc_state *ac = to_spacemit_crtc_state(state);
	struct drm_property_blob *blob = ac->pp_color_temperature_blob_property;
	int *coef_data;
	int n;

	if (blob){
		coef_data = (int *)blob->data;
		for (n = 0; n < 9; n++){
			if ((coef_data[n] > 32767) || (coef_data[n] < -32768)){
				DRM_ERROR("color temp table is invalid %d\n", coef_data[n]);
				return -EINVAL;
			}
		}

		for (n = 9; n < 12; n++){
			if ((coef_data[n] > 16777215) || (coef_data[n] < -16777216)){
				DRM_ERROR("color temp offset is invalid %d\n", coef_data[n]);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int spacemit_crtc_atomic_check_gamma_table(struct drm_crtc *crtc,
					  struct drm_crtc_state *state)
{
	struct spacemit_crtc_state *ac = to_spacemit_crtc_state(state);
	struct drm_property_blob *blob = ac->gamma_table_blob_prop;
	uint16_t *data;
	int idx;
	int len;

	if (blob) {
		data = (uint16_t *)blob->data;
		len = blob->length / sizeof(uint16_t);
		for (idx = 0; idx < len; idx++) {
			if ((data[idx] > 4095) || (data[idx] < 0)) {
				DRM_DEBUG("The value of gamma table is invalid: value %d, n %d\n", data[idx], idx);
				return -EINVAL;
			}
		}
	}
	return 0;
}

static int spacemit_crtc_atomic_check_end_tone_mapping(struct drm_crtc *crtc,
						struct drm_crtc_state *state)
{
	struct spacemit_crtc_state *ac = to_spacemit_crtc_state(state);
	struct drm_property_blob *blob = ac->end_tone_mapping_blob_prop;
	uint16_t *data;
	int idx, len;

	if (blob) {
		data = (uint16_t *)blob->data;
		len = blob->length / sizeof(uint16_t);
		for (idx = 0; idx < len; idx++) {
			if (data[idx] > 65535 || data[idx] < 0) {
				DRM_DEBUG("The value of end tone mapping is invalid: value %d, n %d\n", data[idx], idx);
				return -EINVAL;
			}
		}
	}
	return 0;
}

static int spacemit_crtc_atomic_check_scaling(struct drm_crtc *crtc,
					    struct drm_crtc_state *crtc_state)
{
	struct drm_plane *plane;
	struct spacemit_crtc_state *ac = to_spacemit_crtc_state(crtc_state);
	const struct drm_plane_state *pstate;
	struct spacemit_crtc_scaler *scaler = NULL;
	struct spacemit_plane_state *spacemit_pstate;
	u32 i;

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		spacemit_pstate = to_spacemit_plane_state(pstate);

		if (spacemit_pstate->use_scl) {
			for (i = 0; i < MAX_SCALER_NUMS; i++) {
				scaler = &(ac->scalers[i]);
				if (scaler->in_use == 0x0 || scaler->rdma_id == spacemit_pstate->rdma_id) {
					scaler->in_use |= (1 << plane->index);
					scaler->rdma_id = spacemit_pstate->rdma_id;
					break;
				}
			}

			if (i == MAX_SCALER_NUMS) {
				DRM_ERROR("Exceeds the max scaler number\n");
				return -EINVAL;
			}
		}
	}

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		spacemit_pstate = to_spacemit_plane_state(pstate);

		if (spacemit_pstate->rdma_id != RDMA_INVALID_ID) {
			for (i = 0; i < MAX_SCALER_NUMS; i++) {
				scaler = &(ac->scalers[i]);
				if (scaler->rdma_id == spacemit_pstate->rdma_id && scaler->in_use)
					spacemit_pstate->scaler_id = i;
			}
		}
	}

	return 0;
}

/* crtc actual mclk depends on max(mclk, aclk) */
static int spacemit_crtc_atomic_update_mclk(struct drm_crtc *crtc,
		struct drm_crtc_state *old_state)
{
	struct spacemit_crtc_state *new_ac = to_spacemit_crtc_state(crtc->state);

	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct dpu_clk_context *clk_ctx = NULL;

	if (a_crtc == NULL)
		return 0;

	if (!a_crtc->enable_auto_fc)
		return 0;

	/* when shutdown, all planes disabled, crtc still work, need keep cur mclk */
	if (new_ac->real_mclk == 0) {
		trace_u64_data("new_ac->real_mclk", new_ac->real_mclk);
		return 0;
	}

	clk_ctx = &a_crtc->clk_ctx;
	cancel_work_sync(&a_crtc->work_update_clk); /* incase mclk fq doing */

	a_crtc->cur_mclk = clk_get_rate(clk_ctx->mclk);

	if (a_crtc->fix_max_mclk)
		a_crtc->new_mclk = clk_round_rate(clk_ctx->mclk, a_crtc->max_mclk);
	else
		a_crtc->new_mclk = clk_round_rate(clk_ctx->mclk, new_ac->real_mclk);

	trace_u64_data("crtc mclk cur", a_crtc->cur_mclk);
	trace_u64_data("crtc mclk new", a_crtc->new_mclk);
	if (a_crtc->core && a_crtc->core->update_clk && (a_crtc->new_mclk > a_crtc->cur_mclk)) {
		trace_u64_data("mclk increase to", a_crtc->new_mclk);
		a_crtc->core->update_clk(a_crtc, a_crtc->new_mclk);
	}

	cancel_work_sync(&a_crtc->work_update_bw);

	a_crtc->new_bw = new_ac->bw;

	trace_u64_data("crtc bw cur", a_crtc->cur_bw);
	trace_u64_data("crtc bw new", a_crtc->new_bw);

	if (a_crtc->core && a_crtc->core->update_bw && (a_crtc->new_bw > a_crtc->cur_bw)) {
		trace_u64_data("bw increase to", a_crtc->new_bw);
		a_crtc->core->update_bw(a_crtc, a_crtc->new_bw);
	}

	return 0;
}

#define  RDMA_NUM_MAX   16
/* crtc aclk depends on sum of all rdma bw */
static int spacemit_crtc_atomic_check_aclk(struct drm_crtc *crtc,
					    struct drm_crtc_state *crtc_state)
{
	const struct drm_plane_state *pstate;
	struct drm_plane *plane;
	struct spacemit_crtc_state *ac = to_spacemit_crtc_state(crtc_state); /* new state */
	bool scl_en = false;
	uint64_t afbc_effc = ~0ULL;
	uint64_t tmp, tmp1;
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct spacemit_drm_private *priv = crtc->dev->dev_private;
	uint32_t rdma_num = priv->hwdev->rdma_nums;
	uint64_t rdma_bw[RDMA_NUM_MAX] = { 0 };

	if (ARRAY_SIZE(rdma_bw) < rdma_num) {
		DRM_ERROR("size of rdma_bw(%lu) too small, rdma_num: %d", ARRAY_SIZE(rdma_bw), rdma_num);
		return -EINVAL;
	}

	if (!a_crtc->enable_auto_fc)
		return 0;

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		uint64_t tmp_bw = to_spacemit_plane_state(pstate)->bw;
		bool tmp_scl_en = to_spacemit_plane_state(pstate)->use_scl;
		uint32_t rdma_id = to_spacemit_plane_state(pstate)->rdma_id;

		if (rdma_id < rdma_num)
			rdma_bw[rdma_id] = max(tmp_bw, rdma_bw[rdma_id]);

		if (tmp_scl_en) {
			scl_en = true;
			if (afbc_effc > to_spacemit_plane_state(pstate)->afbc_effc)
				afbc_effc = to_spacemit_plane_state(pstate)->afbc_effc;
		}
	}

	for (int i = 0; i < rdma_num; i++)
		ac->bw += rdma_bw[i];

	if (ac->bw == 0)
		ac->bw = DPU_MIN_QOS_REQ;

	if (ac->bw > (a_crtc->max_bw * MHZ2KHZ)) {
		DRM_INFO("crtc:%lld bandwidth too large\n", ac->bw);
		return -EINVAL;
	}

	if (scl_en) {
		//ac->aclk = max(ac->bw / 16 * 10 / 5, ac->mclk / afbc_effc);
		tmp = ac->bw;
		do_div(tmp, 16);
		tmp = tmp * 100;
		do_div(tmp, 50);
		tmp1 = ac->mclk;
		do_div(tmp1, afbc_effc);
		ac->aclk = max(tmp, tmp1);
	} else {
		//ac->aclk = ac->bw / 16 * 10 / 5;
		tmp = ac->bw;
		do_div(tmp, 16);
		tmp = tmp * 100;
		do_div(tmp, 50);
		ac->aclk = tmp;
	}

	trace_u64_data("crtc calc bw", ac->bw);
	if (a_crtc->bw_margin)
		ac->bw += a_crtc->bw_margin * MHZ2KHZ;

	ac->real_mclk = max(ac->mclk, ac->aclk);

	if (ac->real_mclk < a_crtc->min_mclk)
		ac->real_mclk = a_crtc->min_mclk;

	if (ac->real_mclk > a_crtc->max_mclk) {
		pr_err_ratelimited("real_mclk = %llu, max_mclk = %d\n", ac->real_mclk, a_crtc->max_mclk);
		return -EINVAL;
	}

	if (a_crtc->bw_margin)
		trace_u64_data("crtc fixed bw", ac->bw);
	trace_u64_data("crtc calc aclk", ac->aclk);
	trace_u64_data("crtc calc mclk", ac->mclk);
	trace_u64_data("crtc calc real_mclk", ac->real_mclk);

	return 0;
}

/* crtc mclk depends on max of all rdma mclk */
static int spacemit_crtc_atomic_check_mclk(struct drm_crtc *crtc,
					    struct drm_crtc_state *crtc_state)
{
	const struct drm_plane_state *pstate;
	struct drm_plane *plane;
	struct spacemit_crtc_state *ac = to_spacemit_crtc_state(crtc_state);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);

	if (!a_crtc->enable_auto_fc)
		return 0;

	ac->mclk = DPU_MCLK_MIN;

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		uint64_t tmp_mclk = to_spacemit_plane_state(pstate)->mclk;

		if (ac->mclk < tmp_mclk)
			ac->mclk = tmp_mclk;
	}

	return 0;
}

static void saturn_check_dpuctrl_scl_reuse(struct drm_crtc *crtc,
					    struct drm_crtc_state *crtc_state)
{
	struct drm_plane *plane;
	struct spacemit_crtc_state *spacemit_state = to_spacemit_crtc_state(crtc_state);
	struct spacemit_crtc_rdma *rdmas = spacemit_state->rdmas;
	u32 rdma_id = 0;
	u32 scl_rdma_id = 0;
	struct spacemit_crtc_scaler *scaler = NULL;
	const struct drm_plane_state *pstate;
	int i = 0;

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		rdma_id = to_spacemit_plane_state(pstate)->rdma_id;
		if (rdma_id != RDMA_INVALID_ID)
			rdmas[rdma_id].use_cnt++;
	}

	//TODO: SCALER_NUMS for different IP
	for (i = 0; i < MAX_SCALER_NUMS; i++) {
		scaler = &(spacemit_state->scalers[i]);

		if (scaler->in_use)
			spacemit_state->scl_rdma_id[i] = scaler->rdma_id;
		else
			spacemit_state->scl_rdma_id[i] = RDMA_INVALID_ID;

		if (rdmas[scl_rdma_id].use_cnt > 1)
			spacemit_state->scl_rdma_reuse[i] = true;
	}
}

static int spacemit_crtc_atomic_check_fbmem(struct drm_crtc *crtc,
					    struct drm_crtc_state *crtc_state)
{
	struct spacemit_crtc_rdma *rdmas = to_spacemit_crtc_state(crtc_state)->rdmas;
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct spacemit_drm_private *priv = crtc->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	const struct drm_plane_state *pstate;
	struct drm_plane *plane;
	/* Calc each rdma required fbc mem size */
	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, crtc_state) {
		u32 rdma_id = to_spacemit_plane_state(pstate)->rdma_id;
		u32 layer_fbcmem_size = to_spacemit_plane_state(pstate)->fbcmem_size;

		if (rdma_id != RDMA_INVALID_ID) {
			if (rdmas[rdma_id].mode == UP_DOWN)
				rdmas[rdma_id].fbcmem.size = max(layer_fbcmem_size,
								 rdmas[rdma_id].fbcmem.size);
			else
				rdmas[rdma_id].fbcmem.size += layer_fbcmem_size;
		}
	}

	/* Adjust each rdma's fbcmem layout */
	return a_crtc->core->adjust_rdma_fbcmem(hwdev, rdmas);

}

static void spacemit_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct videomode vm;

	DRM_INFO("%s()\n", __func__);
	trace_spacemit_crtc_mode_set_nofb(a_crtc->dev_id);
	drm_display_mode_to_videomode(&crtc->mode, &vm);
}

static void spacemit_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_atomic_state *old_state)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);

	DRM_INFO("%s(power on)\n", __func__);
	trace_spacemit_crtc_atomic_enable(a_crtc->dev_id);

	/* If bootloader framebuffer is active, release its resources first */
	if (unlikely(a_crtc->logo_booton)) {
		pm_runtime_enable(a_crtc->dev);
		spacemit_dpu_power_enable(a_crtc, true);
		dpu_pm_resume(a_crtc->dev);
		dpu_pm_suspend(a_crtc->dev);
		spacemit_dpu_power_enable(a_crtc, false);
		spacemit_dpu_free_bootloader_mem();
		a_crtc->logo_booton = false;
		msleep(10);
	}

	if (!a_crtc->power_on) {
		spacemit_dpu_power_enable(a_crtc, true);
		dpu_pm_resume(a_crtc->dev);
	}

#ifdef CONFIG_SPACEMIT_DEBUG
	a_crtc->is_working = true;
#endif
	drm_crtc_vblank_on(&a_crtc->crtc);

	spacemit_crtc_init(a_crtc);
	a_crtc->rpm_status = true;
}

static void spacemit_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_atomic_state *old_state)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct drm_device *drm = a_crtc->crtc.dev;

	DRM_INFO("%s(power off)\n", __func__);
	trace_spacemit_crtc_atomic_disable(a_crtc->dev_id);

	if (a_crtc->is_offline_mode)
		spacemit_crtc_stop(a_crtc);
	spacemit_crtc_uninit(a_crtc);

	drm_crtc_vblank_off(&a_crtc->crtc);
#ifdef CONFIG_SPACEMIT_DEBUG
	a_crtc->is_working = false;
#endif

	dpu_pm_suspend(a_crtc->dev);
	spacemit_dpu_power_enable(a_crtc, false);

	spin_lock_irq(&drm->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&drm->event_lock);
	a_crtc->rpm_status = false;
}

void spacemit_crtc_fill_slice(struct spacemit_crtc *a_crtc, u32 width, u32 height)
{
	a_crtc->slice_wb[a_crtc->slice_num].wb_in_w = width;
	a_crtc->slice_wb[a_crtc->slice_num].wb_in_h = height;
	a_crtc->slice_wb[a_crtc->slice_num].wb_out_w = width;
	a_crtc->slice_wb[a_crtc->slice_num].wb_out_h = height;
	a_crtc->slice_wb[a_crtc->slice_num].wb_out_x =
		a_crtc->slice_num ? a_crtc->slice_wb[a_crtc->slice_num - 1].wb_out_x + a_crtc->slice_wb[a_crtc->slice_num - 1].wb_out_w : 0;
	a_crtc->slice_wb[a_crtc->slice_num].wb_out_y = 0;
	a_crtc->slice_wb[a_crtc->slice_num].rdma_w = width;
	a_crtc->slice_wb[a_crtc->slice_num].rdma_h = height;
	a_crtc->slice_wb[a_crtc->slice_num].rdma_x =
		a_crtc->slice_num ? a_crtc->slice_wb[a_crtc->slice_num - 1].rdma_x + a_crtc->slice_wb[a_crtc->slice_num - 1].rdma_w : 0;
	a_crtc->slice_wb[a_crtc->slice_num].rdma_y = 0;
	a_crtc->slice_wb[a_crtc->slice_num].crtc_w = width;
	a_crtc->slice_wb[a_crtc->slice_num].crtc_h = height;
	a_crtc->slice_wb[a_crtc->slice_num].crtc_x = 0;
	a_crtc->slice_wb[a_crtc->slice_num].crtc_y = 0;
}

int spacemit_crtc_calc_slices(struct spacemit_crtc *a_crtc, u32 width, u32 height)
{
	int default_slice_width = DEFAULT_SLICE_WIDTH;
	int slice_width;
	int left_width;
	int total_slice_width;

	DRM_DEBUG("%s() %d\n", __func__, __LINE__);
	if (default_slice_width > width)
		default_slice_width = width;

	DRM_DEBUG("wb slice: default_slice_width: %d\n", default_slice_width);

	//analyze slices
	total_slice_width = 0;
	a_crtc->slice_num = 0;
	while (total_slice_width < width) {
		slice_width = default_slice_width;
		left_width = width - total_slice_width;
		if (left_width <= slice_width)
			slice_width = left_width;
		else if (left_width < slice_width + 16)
			slice_width -= 16;

		DRM_DEBUG("slice%d: use slice_width:%d\n", a_crtc->slice_num, slice_width);
		total_slice_width += slice_width;
		spacemit_crtc_fill_slice(a_crtc, slice_width, height);
		a_crtc->slice_num++;
	}
	return a_crtc->slice_num;
}

static int spacemit_crtc_atomic_check(struct drm_crtc *crtc,
						struct drm_atomic_state *atomic_state)
{
	struct drm_crtc_state *state = drm_atomic_get_new_crtc_state(atomic_state, crtc);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	int ret = 0;
	struct drm_display_mode *mode = NULL;

	DRM_DEBUG("%s()\n", __func__);
	trace_spacemit_crtc_atomic_check(a_crtc->dev_id);

	ret = spacemit_crtc_atomic_check_scaling(crtc, state);
	if (ret) {
		DRM_ERROR("crtc_id=%u check_scaling FAILED ret=%d\n",
			  crtc->base.id, ret);
		return -EINVAL;
	}

	if (spacemit_crtc_atomic_check_color_temp(crtc, state)){
		DRM_ERROR("The value of color temperature is invalid\n");
		return -EINVAL;
	}

	if (spacemit_crtc_atomic_check_color_matrix(crtc, state)) {
		DRM_ERROR("The value of color matrix is invalid\n");
		return -EINVAL;
	}

	if (spacemit_crtc_atomic_check_gamma_table(crtc, state)) {
		DRM_ERROR("The value of gamma table is invalid!\n");
		return -EINVAL;
	}

	if (spacemit_crtc_atomic_check_end_tone_mapping(crtc, state)) {
		DRM_ERROR("The value of end tone mapping is invalid!\n");
		return -EINVAL;
	}

	if (spacemit_crtc_atomic_check_fbmem(crtc, state)) {
		DRM_ERROR("Failed to satisfy fbcmem size for all rdmas!\n");
		return -EINVAL;
	}

	if (spacemit_crtc_atomic_check_mclk(crtc, state)) {
		DRM_ERROR("Failed to satisfy mclk for all rdmas!\n");
		return -EINVAL;
	}

	if (spacemit_crtc_atomic_check_aclk(crtc, state)) {
		DRM_ERROR("Failed to satisfy aclk for all rdmas!\n");
		return -EINVAL;
	}

	saturn_check_dpuctrl_scl_reuse(crtc, state);

	if (a_crtc->is_offline_mode) {
		mode = &state->adjusted_mode;
		if (mode->hdisplay > MAX_WIDTH) {
			DRM_INFO("Frame width %d over max %d!\n", mode->hdisplay, MAX_WIDTH);
			return -EINVAL;
		}
		if (mode->hdisplay > DEFAULT_SLICE_WIDTH) {
			spacemit_crtc_calc_slices(a_crtc, mode->hdisplay, mode->vdisplay);
			a_crtc->is_slice_mode = 1;
		}
	}

	return ret;
}

static void spacemit_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct videomode vm;

	DRM_DEBUG("%s()\n", __func__);
	trace_spacemit_crtc_atomic_begin(a_crtc->dev_id);
	drm_display_mode_to_videomode(&crtc->mode, &vm);
	if (a_crtc->vrr_vfp != vm.vfront_porch)
		a_crtc->vrr_vfp = vm.vfront_porch;

	if (a_crtc->core && a_crtc->core->begin)
		a_crtc->core->begin(a_crtc);
}

#define VSYNC_PERIOD_VARIANCE_NS		2000000

static void spacemit_wait_earliest_process_time(int32_t vrefresh, uint64_t expected_present_time)
{

	ktime_t now;
	int64_t present_time_adjust, delay_until_process;
	int64_t vsync_period_ns = mult_frac(1000, 1000 * 1000, vrefresh);

	present_time_adjust = expected_present_time - (vsync_period_ns - VSYNC_PERIOD_VARIANCE_NS);
	if (present_time_adjust <= 0) {
		// Don't need to wait
		return;
	}

	now = ktime_get();
	delay_until_process = (int64_t)ktime_us_delta(present_time_adjust, now);
	if (delay_until_process > 0) {
		int32_t max_delay_us = (10 * vsync_period_ns) / 1000;

		if (delay_until_process > max_delay_us) {
			delay_until_process = max_delay_us;
			pr_warn("expected present time seems incorrect(now %llu, earliest %llu)\n",
					now, present_time_adjust);
		}
		usleep_range(delay_until_process, delay_until_process + 10);
	}
}

static void spacemit_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)

{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct spacemit_crtc_state *new_state = to_spacemit_crtc_state(crtc->state);
	struct drm_crtc_state *old_state = drm_atomic_get_old_crtc_state(state, crtc);
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	DRM_DEBUG("%s()\n", __func__);
	trace_spacemit_crtc_atomic_flush(a_crtc->dev_id);
	hwdev->conf_dpuctrl_color_matrix(a_crtc, old_state);
	if (hwdev->conf_dpuctrl_acad)
		hwdev->conf_dpuctrl_acad(a_crtc, old_state);
	if (hwdev->conf_ee)
		hwdev->conf_ee(a_crtc, old_state);
	if (hwdev->conf_gamma_table)
		hwdev->conf_gamma_table(a_crtc, old_state);
	if (hwdev->conf_end_tone_mapping)
		hwdev->conf_end_tone_mapping(a_crtc, old_state);
	if (hwdev->conf_matrix)
		hwdev->conf_matrix(a_crtc, old_state);
	spacemit_crtc_atomic_update_mclk(crtc, old_state);

	if (new_state->expected_present_time != 0) {
		int32_t vrefresh = drm_mode_vrefresh(&old_state->mode);

		if (vrefresh == 0) {
			/* decon just be enabled */
			vrefresh = drm_mode_vrefresh(&crtc->state->mode);
		}

		spacemit_wait_earliest_process_time(vrefresh, new_state->expected_present_time);
	}

	spacemit_crtc_run(crtc, old_state);
}

static struct drm_crtc_state *spacemit_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct spacemit_crtc_state *state;
	struct spacemit_drm_private *priv = crtc->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	u8 n_rdma, i;
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);

	if (WARN_ON(!crtc->state))
		return NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);
	memset(&state->scalers, 0x0, sizeof(struct spacemit_crtc_scaler) * MAX_SCALER_NUMS);

	n_rdma = hwdev->rdma_nums;
	state->rdmas = kzalloc(sizeof(struct spacemit_crtc_rdma) * n_rdma, GFP_KERNEL);
	if (!state->rdmas) {
		kfree(state);
		return NULL;
	}

	/* Rdma use UP_DOWN mode by default */
	for (i = 0; i < n_rdma; i++) {
		state->rdmas[i].mode = UP_DOWN;
		state->rdmas[i].in_use = false;
		if (a_crtc->is_offline_mode)
			state->rdmas[i].is_offline_mode = true;
	}

	if (state->color_matrix_blob_prop)
		drm_property_blob_get(state->color_matrix_blob_prop);

	if (state->gamma_table_blob_prop)
		drm_property_blob_get(state->gamma_table_blob_prop);

	if (state->pp_color_temperature_blob_property)
		drm_property_blob_get(state->pp_color_temperature_blob_property);

	if (state->end_tone_mapping_blob_prop)
		drm_property_blob_get(state->end_tone_mapping_blob_prop);

	if (state->pp_acad_blob_prop)
		drm_property_blob_get(state->pp_acad_blob_prop);

	for (i = 0; i < MAX_CL_NUM; i++) {
		state->cl[i].index = i;
		state->cl[i].type = CMDLIST_CRTC;
	}

	return &state->base;
}

static void spacemit_crtc_destroy_state(struct drm_crtc *crtc,
				struct drm_crtc_state *state)
{
	struct spacemit_crtc_state *spacemit_state = NULL;
	struct spacemit_crtc *a_crtc = NULL;
	int i = 0;

	if (state) {
		spacemit_state = to_spacemit_crtc_state(state);
		__drm_atomic_helper_crtc_destroy_state(state);
		a_crtc = to_spacemit_crtc(crtc);
		if (spacemit_state->wb_mmu_tbl.va)
			dma_free_coherent(a_crtc->dev, spacemit_state->wb_mmu_tbl.size,
				spacemit_state->wb_mmu_tbl.va, spacemit_state->wb_mmu_tbl.pa);

		for (i = 0; i < MAX_CL_NUM; i++) {
			if (spacemit_state->cl[i].va)
				dma_free_coherent(a_crtc->dev, spacemit_state->cl[i].size,
					spacemit_state->cl[i].va, spacemit_state->cl[i].pa);
		}
		if (spacemit_state->color_matrix_blob_prop)
			drm_property_blob_put(spacemit_state->color_matrix_blob_prop);

		if (spacemit_state->gamma_table_blob_prop)
			drm_property_blob_put(spacemit_state->gamma_table_blob_prop);

		if (spacemit_state->pp_color_temperature_blob_property)
			drm_property_blob_put(spacemit_state->pp_color_temperature_blob_property);

		if (spacemit_state->pp_acad_blob_prop)
			drm_property_blob_put(spacemit_state->pp_acad_blob_prop);

		if (spacemit_state->end_tone_mapping_blob_prop)
			drm_property_blob_put(spacemit_state->end_tone_mapping_blob_prop);

		kfree(spacemit_state->rdmas);
		kfree(spacemit_state);
	}
}

static void spacemit_crtc_reset(struct drm_crtc *crtc)
{
	struct spacemit_crtc_state *state =
		kzalloc(sizeof(*state), GFP_KERNEL);
	struct spacemit_drm_private *priv = crtc->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	u8 n_rdma;

	if (crtc->state)
		spacemit_crtc_destroy_state(crtc, crtc->state);

	__drm_atomic_helper_crtc_reset(crtc, &state->base);

	n_rdma = hwdev->rdma_nums;
	state->rdmas = kzalloc(sizeof(struct spacemit_crtc_rdma) * n_rdma, GFP_KERNEL);
	if (!state->rdmas) {
		DRM_ERROR("Failed to allocate memory of struct spacemit_crtc_rdma!\n");
		return;
	}

}

static int spacemit_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);

	DRM_DEBUG("%s()\n", __func__);
	trace_spacemit_crtc_enable_vblank(a_crtc->dev_id);

	if (a_crtc->core && a_crtc->core->enable_vsync)
		a_crtc->core->enable_vsync(a_crtc);

	return 0;
}

static void spacemit_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);

	DRM_DEBUG("%s()\n", __func__);
	trace_spacemit_crtc_disable_vblank(a_crtc->dev_id);

	if (a_crtc->core && a_crtc->core->disable_vsync)
		a_crtc->core->disable_vsync(a_crtc);
}

static int spacemit_crtc_atomic_set_property(struct drm_crtc *crtc,
				   struct drm_crtc_state *state,
				   struct drm_property *property,
				   uint64_t val)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct spacemit_crtc_state *s = to_spacemit_crtc_state(state);
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	bool replaced = false;
	int ret = 0;

	DRM_DEBUG("%s() name = %s, val = %llu\n",
		  __func__, property->name, val);

	if (property == a_crtc->color_matrix_property) {
		ret = spacemit_atomic_replace_property_blob_from_id(crtc->dev,
					&s->color_matrix_blob_prop,
					val,
					-1,
					sizeof(int),
					&replaced);
		return ret;
	} else if (property == a_crtc->expected_present_time) {
		s->expected_present_time = val;
		return 0;
	} else if (property == a_crtc->offline_mode_property) {
		a_crtc->is_offline_mode = val;
		return 0;
	} else if (property == a_crtc->pp_color_temperature_property){
		ret = spacemit_atomic_replace_property_blob_from_id(crtc->dev,
					&s->pp_color_temperature_blob_property,
					val,
					-1,
					sizeof(int),
					&replaced);
		return ret;
	} else if (property == a_crtc->wb_property) {
		a_crtc->wb_pos = val;
		return 0;
	} else if (property == a_crtc->post_scaler_property) {
		s->post_scl_on = val;
		return 0;
	} else if (property == a_crtc->gamma_table_property) {
		ret = spacemit_atomic_replace_property_blob_from_id(crtc->dev,
					&s->gamma_table_blob_prop,
					val,
					-1,
					sizeof(uint16_t),
					&replaced);
		return ret;
	} else if (property == a_crtc->ee_property) {
		ret = spacemit_atomic_replace_property_blob_from_id(crtc->dev,
					&s->ee_blob_prop,
					val,
					-1,
					sizeof(uint32_t),
					&replaced);
		return ret;
	} else if (property == a_crtc->acad_property) {
		ret = spacemit_atomic_replace_property_blob_from_id(crtc->dev,
					&s->pp_acad_blob_prop,
					val,
					-1,
					sizeof(int),
					&replaced);
		return ret;
	} else if (property == a_crtc->end_tone_mapping_property) {
		ret = spacemit_atomic_replace_property_blob_from_id(crtc->dev,
					&s->end_tone_mapping_blob_prop,
					val,
					-1,
					sizeof(uint16_t),
					&replaced);
		return ret;
	} else if (property == a_crtc->acad_status_property) {
		hwdev->is_acad_on = val;
		return 0;
	} else if (property == a_crtc->bl_save_status_property) {
		hwdev->is_bl_save_on = val;
		return 0;
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static int spacemit_crtc_atomic_get_property(struct drm_crtc *crtc,
					  const struct drm_crtc_state *state,
					  struct drm_property *property,
					  u64 *val)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct spacemit_crtc_state *s = to_spacemit_crtc_state(state);
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	DRM_DEBUG("%s() name = %s\n", __func__, property->name);

	if (property == a_crtc->color_matrix_property) {
		if (s->color_matrix_blob_prop)
			*val = (s->color_matrix_blob_prop) ? s->color_matrix_blob_prop->base.id : 0;
	} else if (property == a_crtc->hw_info_property) {
		*val = a_crtc->hw_info_property->base.id;
	} else if (property == a_crtc->offline_mode_property) {
		*val = a_crtc->is_offline_mode;
	} else if (property == a_crtc->gamma_table_property) {
		*val = (s->gamma_table_blob_prop) ? s->gamma_table_blob_prop->base.id : 0;
	} else if (property == a_crtc->ee_property) {
		*val = (s->ee_blob_prop) ? s->ee_blob_prop->base.id : 0;
	} else if (property == a_crtc->post_scaler_property) {
		*val = s->post_scl_on;
	} else if (property == a_crtc->acad_property) {
		*val = (s->pp_acad_blob_prop) ? s->pp_acad_blob_prop->base.id : 0;
	} else if (property == a_crtc->pp_color_temperature_property){
		if (s->pp_color_temperature_blob_property)
			*val = (s->pp_color_temperature_blob_property) ? s->pp_color_temperature_blob_property->base.id : 0;
	} else if (property == a_crtc->end_tone_mapping_property) {
		*val = (s->end_tone_mapping_blob_prop) ? s->end_tone_mapping_blob_prop->base.id : 0;
	} else if (property == a_crtc->expected_present_time) {
		*val = (a_crtc->expected_present_time) ? a_crtc->expected_present_time->base.id : 0;
	} else if (property == a_crtc->wb_property) {
		*val =  a_crtc->wb_pos;
	} else if (property == a_crtc->acad_status_property) {
		*val = hwdev->is_acad_on ? 1 : 0;
	} else if (property == a_crtc->bl_save_status_property) {
		*val = hwdev->is_bl_save_on ? 1 : 0;
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static const struct drm_crtc_helper_funcs spacemit_crtc_helper_funcs = {
	.mode_set_nofb = spacemit_crtc_mode_set_nofb,
	.atomic_check = spacemit_crtc_atomic_check,
	.atomic_begin = spacemit_crtc_atomic_begin,
	.atomic_flush = spacemit_crtc_atomic_flush,
	.atomic_enable = spacemit_crtc_atomic_enable,
	.atomic_disable = spacemit_crtc_atomic_disable,
};

static const struct drm_crtc_funcs spacemit_crtc_funcs = {
	.atomic_get_property = spacemit_crtc_atomic_get_property,
	.atomic_set_property = spacemit_crtc_atomic_set_property,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = spacemit_crtc_reset,
	.atomic_duplicate_state = spacemit_crtc_duplicate_state,
	.atomic_destroy_state = spacemit_crtc_destroy_state,
	.enable_vblank = spacemit_crtc_enable_vblank,
	.disable_vblank = spacemit_crtc_disable_vblank,
};

static inline struct spacemit_hw_rdma *
modifiers_ptr(struct drm_dpu_hw_version_blob *blob)
{
	return (struct spacemit_hw_rdma *)(((char *)blob) + blob->rdma_offset);
}

static int create_hwinfo_blob(struct drm_crtc *crtc)
{
	struct drm_property_blob *blob;
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	struct drm_property *prop;
	struct drm_dpu_hw_version_blob *blob_data;
	struct spacemit_drm_private *priv = crtc->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct spacemit_hw_rdma *hw_rdma;
	u8 rdma_nums = hwdev->rdma_nums;
	size_t blob_size = 0;

	prop = drm_property_create(crtc->dev,
			DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_BLOB,
			"hw_info", 0);
	if (!prop)
		return -ENOMEM;

	blob_size = sizeof(struct drm_dpu_hw_version_blob);
	blob_size += rdma_nums * sizeof(struct spacemit_crtc_rdma);

	blob = drm_property_create_blob(crtc->dev, blob_size, NULL);
	if (IS_ERR(blob))
		return -1;

	blob_data = blob->data;
	blob_data->rdma_offset = sizeof(struct drm_dpu_hw_version_blob);
	blob_data->rdma_nums = rdma_nums;
	blob_data->scaler_num = hwdev->scaler_num;
	blob_data->ver_scaler_num = hwdev->ver_scale_coef_size;
	blob_data->hor_scaler_num = hwdev->hor_scale_coef_size;
	blob_data->acad_num = hwdev->acad_num;
	blob_data->dpu_version = hwdev->dpu_version;

	hw_rdma = modifiers_ptr(blob_data);
	memcpy(hw_rdma, hwdev->rdmas, sizeof(struct spacemit_hw_rdma) * rdma_nums);

	drm_object_attach_property(&crtc->base, prop, blob->base.id);
	a_crtc->hw_info_property = prop;

	return 0;
}

static int spacemit_crtc_create_properties(struct drm_crtc *crtc)
{
	struct drm_property *prop;
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);
	int ret = 0;

	DRM_DEBUG("%s()\n", __func__);
	/* create rotation property */

	prop = drm_property_create(crtc->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"color_matrix_coef", 0);
	if (!prop) {
		DRM_ERROR("create color_matrix_coef faild!");
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->color_matrix_property = prop;

	prop = drm_property_create(crtc->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"ee_coef", 0);

	if (!prop) {
		DRM_ERROR("create ee_property faild!");
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->ee_property = prop;

	ret = create_hwinfo_blob(crtc);
	if (ret)
		DRM_ERROR("create_hwinfo_blob failed %d\n", ret);

	prop = drm_property_create_range(crtc->dev, 0,
			"expected_present_time", 0, ULLONG_MAX);
	if (!prop) {
		DRM_ERROR("expected_present_time failed %d\n", ret);
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->expected_present_time = prop;

	prop = drm_property_create_bool(crtc->dev, DRM_MODE_PROP_ATOMIC,
			"offline_mode");
	if (!prop) {
		DRM_ERROR("offline_mode failed %d\n", ret);
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->offline_mode_property = prop;

	prop = drm_property_create_bool(crtc->dev, DRM_MODE_PROP_ATOMIC,
			"pos_scl");
	if (!prop) {
		DRM_ERROR("pos_scl %d\n", ret);
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->post_scaler_property = prop;

	prop = drm_property_create(crtc->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"pp_cct", 0);
	if (!prop) {
		DRM_ERROR("create cct_prop failed!");
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->pp_color_temperature_property = prop;

	prop = drm_property_create_range(crtc->dev, DRM_MODE_PROP_RANGE,
			"WB_POS", SPACEMIT_WB_RDMA0, SPACEMIT_WB_POST2);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&crtc->base, prop, SPACEMIT_WB_COMP0);
	a_crtc->wb_property = prop;
	prop = drm_property_create(crtc->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"pp_gamma", 0);

	if (!prop) {
		DRM_ERROR("create gamma_table_prop faild!");
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->gamma_table_property = prop;

	prop = drm_property_create(crtc->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"acad_coef", 0);

	if (!prop) {
		DRM_ERROR("create acad_property faild!");
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->acad_property = prop;

	prop = drm_property_create(crtc->dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"pp_etm", 0);
	if (!prop) {
		DRM_ERROR("create end_tone_mapping_prop faild!");
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->end_tone_mapping_property = prop;

	prop = drm_property_create_bool(crtc->dev, DRM_MODE_PROP_ATOMIC,
			"acad_status");
	if (!prop) {
		DRM_ERROR("create acad_status failed!");
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->acad_status_property = prop;

	prop = drm_property_create_bool(crtc->dev, DRM_MODE_PROP_ATOMIC,
			"bl_save_status");
	if (!prop) {
		DRM_ERROR("create bl_save_status failed!");
		return -ENOMEM;
	}
	drm_object_attach_property(&crtc->base, prop, 0);
	a_crtc->bl_save_status_property = prop;
	return 0;
}

static int spacemit_crtc_bind_init(struct drm_device *drm, struct drm_crtc *crtc,
			 struct drm_plane *primary, struct device_node *port)
{
	int err;

	/*
	 * set crtc port so that drm_of_find_possible_crtcs call works
	 */
	of_node_put(port);
	crtc->port = port;

	err = drm_crtc_init_with_planes(drm, crtc, primary, NULL,
					&spacemit_crtc_funcs, NULL);
	if (err) {
		DRM_ERROR("failed to init crtc.\n");
		return err;
	}

	drm_mode_crtc_set_gamma_size(crtc, 256);

	drm_crtc_helper_add(crtc, &spacemit_crtc_helper_funcs);

	spacemit_crtc_create_properties(crtc);

	DRM_INFO("%s() ok\n", __func__);
	return 0;
}

int spacemit_crtc_run(struct drm_crtc *crtc,
		struct drm_crtc_state *old_state)
{
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(crtc);

	DRM_DEBUG("%s()\n", __func__);
	trace_spacemit_crtc_run(a_crtc->dev_id);

	if (a_crtc->core && a_crtc->core->run)
		a_crtc->core->run(crtc, old_state);

	return 0;
}

int spacemit_dpu_esd_restart(struct spacemit_crtc *a_crtc)
{
	if (a_crtc->core && a_crtc->core->esd_restart)
		a_crtc->core->esd_restart(a_crtc);
	return 0;
}
EXPORT_SYMBOL(spacemit_dpu_esd_restart);

int spacemit_crtc_stop(struct spacemit_crtc *a_crtc)
{
	trace_spacemit_crtc_stop(a_crtc->dev_id);

	if (a_crtc->core && a_crtc->core->stop)
		a_crtc->core->stop(a_crtc);

	drm_crtc_handle_vblank(&a_crtc->crtc);

	return 0;
}
EXPORT_SYMBOL(spacemit_crtc_stop);

static int spacemit_crtc_init(struct spacemit_crtc *a_crtc)
{
	trace_spacemit_crtc_init(a_crtc->dev_id);
#ifdef CONFIG_PM
#if IS_ENABLED(CONFIG_SPACEMIT_DDR_FC)
	update_spacemit_ddr_bw_read_req(a_crtc->ddr_qos_cons, DPU_QOS_REQ);
#endif
#endif
	if (a_crtc->core && a_crtc->core->init)
		a_crtc->core->init(a_crtc);

	a_crtc->is_1st_f = true;

	return 0;
}

static int spacemit_crtc_uninit(struct spacemit_crtc *a_crtc)
{
	trace_spacemit_crtc_uninit(a_crtc->dev_id);

	if (a_crtc->core && a_crtc->core->uninit)
		a_crtc->core->uninit(a_crtc);
#ifdef CONFIG_PM
#if IS_ENABLED(CONFIG_SPACEMIT_DDR_FC)
	update_spacemit_ddr_bw_read_req(a_crtc->ddr_qos_cons, PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
#endif
#endif
	return 0;
}

static irqreturn_t spacemit_dpu_isr(int irq, void *data)
{
	struct spacemit_crtc *a_crtc = data;

	if (a_crtc->core) {
		if (a_crtc->is_offline_mode == 0) {
			if (a_crtc->core->online_isr)
				a_crtc->core->online_isr(a_crtc);
		} else {
			if (a_crtc->core->offline_isr)
				a_crtc->core->offline_isr(a_crtc);
		}

	}

	return IRQ_HANDLED;
}

static int spacemit_dpu_irqs_init(struct spacemit_crtc *a_crtc,
				struct device_node *np, struct platform_device *pdev)
{
	int err;

	int irq_online, irq_offline;

	DRM_INFO("%s()\n", __func__);
	/*request irq*/
	irq_online = platform_get_irq_byname(pdev, "ONLINE_IRQ");
	irq_offline = platform_get_irq_byname(pdev, "OFFLINE_IRQ");
	if ((irq_online < 0) && (irq_offline < 0)) {
		DRM_ERROR("failed to get ONLINE irq number %d\n", irq_online);
		DRM_ERROR("failed to get OFFLINE irq number %d\n", irq_offline);
		// return -EINVAL;
	}
	DRM_DEBUG("dpu online_irq = %d\n", irq_online);
	DRM_DEBUG("dpu offline_irq = %d\n", irq_offline);

	if (irq_online > 0) {
		err = request_irq(irq_online, spacemit_dpu_isr, 0, "DPU_ONLINE", a_crtc);
		if (err) {
			DRM_ERROR("error: dpu request online irq failed\n");
			return -EINVAL;
		}
	}

	if (irq_offline > 0) {
		err = request_irq(irq_offline, spacemit_dpu_isr, 0, "DPU_OFFLINE", a_crtc);
		if (err) {
			DRM_ERROR("error: dpu request offline irq failed\n");
			free_irq(irq_online, a_crtc);
			return -EINVAL;
		}
	}

	return 0;
}

static void dpu_wq_update_bw(struct work_struct *work)
{
	struct spacemit_crtc *a_crtc =
		container_of(work, struct spacemit_crtc, work_update_bw);

	if (a_crtc->core && a_crtc->core->update_bw) {
		trace_u64_data("bw decrease to", a_crtc->new_bw);
		a_crtc->core->update_bw(a_crtc, a_crtc->new_bw);
	}
}

static void dpu_wq_update_clk(struct work_struct *work)
{
	struct spacemit_crtc *a_crtc =
		container_of(work, struct spacemit_crtc, work_update_clk);

	if (a_crtc->core && a_crtc->core->update_clk) {
		trace_u64_data("mclk decrease to", a_crtc->new_mclk);
		a_crtc->core->update_clk(a_crtc, a_crtc->new_mclk);
	}
}

#ifdef CONFIG_SPACEMIT_DEBUG
static bool check_dpu_running_status(struct spacemit_crtc *a_crtc)
{
	return a_crtc->is_working;
}

#define to_dpuinfo(_nb) container_of(_nb, struct spacemit_crtc, nb)
static int dpu_clkoffdet_notifier_handler(struct notifier_block *nb,
			    unsigned long msg, void *data)
{
	struct clk_notifier_data *cnd = data;
	struct spacemit_crtc *a_crtc = to_dpuinfo(nb);

	if ((__clk_is_enabled(cnd->clk)) && (msg & PRE_RATE_CHANGE) && (cnd->new_rate == 0) && (cnd->old_rate != 0)) {
		if (a_crtc->is_dpu_running(a_crtc))
			return NOTIFY_BAD;
	}

	return NOTIFY_OK;
}
#endif
#define max_mclk_nb_to_dpuinfo(_nb) container_of(_nb, struct spacemit_crtc, max_mclk_nb)
static int dpu_max_mclk_notifier_hander(struct notifier_block *nb,
		unsigned long fix_mclk, void *data)
{
	struct spacemit_crtc *a_crtc = max_mclk_nb_to_dpuinfo(nb);
	struct drm_device *drm = a_crtc->crtc.dev;
	char *cam_on_event[] = { "CAMERA=1", NULL };
	char *cam_off_event[] = { "CAMERA=0", NULL };



	if (fix_mclk) {
		a_crtc->fix_max_mclk = true;
		if (a_crtc->cur_mclk < a_crtc->max_mclk && a_crtc->core && a_crtc->core->update_clk) {
			cancel_work_sync(&a_crtc->work_update_clk);
			trace_u64_data("mclk increase to", a_crtc->max_mclk);
			a_crtc->core->update_clk(a_crtc, a_crtc->max_mclk);
			kobject_uevent_env(&drm->primary->kdev->kobj, KOBJ_CHANGE, cam_on_event);

		}
	} else {
		a_crtc->fix_max_mclk = false;
		kobject_uevent_env(&drm->primary->kdev->kobj, KOBJ_CHANGE, cam_off_event);
	}

	return NOTIFY_OK;
}

static void dpu_parse_dsi_ops(struct spacemit_crtc *a_crtc)
{
	struct platform_device *dsi_pdev;

	if (!a_crtc->dsi_node)
		return;

	dsi_pdev = of_find_device_by_node(a_crtc->dsi_node);
	if (!dsi_pdev) {
		DRM_ERROR("Failed to find dsi platform device\n");
		goto node_put;
	}

	a_crtc->dsi = platform_get_drvdata(dsi_pdev);
	if (!a_crtc->dsi) {
		DRM_ERROR("Failed to get dsi drvdata\n");
		goto node_put;
	}

node_put:
	of_node_put(a_crtc->dsi_node);
}

static int spacemit_dpu_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm_dev = data;
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node;
	struct device_node *ports, *port;
	struct drm_plane *plane;
	int ret;
#ifdef CONFIG_SPACEMIT_DEBUG
	struct dpu_clk_context *clk_ctx = NULL;
#endif
	DRM_INFO("%s()\n", __func__);
	if (a_crtc->is_offline_mode){
		dpu_pm_suspend(a_crtc->dev);
		spacemit_dpu_power_enable(a_crtc, false);
	} else if (!a_crtc->is_edp) {
		dpu_parse_dsi_ops(a_crtc);
	}

	ret = spacemit_dpu_irqs_init(a_crtc, np, pdev);
	if (ret)
		return ret;

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		DRM_ERROR("dma_set_mask_and_coherent failed (%d)\n", ret);
		return ret;
	}

	ret = of_reserved_mem_device_init(a_crtc->dev);
	if (ret) {
		DRM_ERROR("Failed to reserve dpu memory, ret:%d\n", ret);
		return ret;
	}

#if IS_ENABLED(CONFIG_SPACEMIT_DDR_FC) && defined(CONFIG_PM)
	a_crtc->ddr_qos_cons = register_spacemit_ddr_bw_cons("DPU", PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE, PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
#endif

	a_crtc->dpu_trace_wq = create_singlethread_workqueue("dpu_trace_wq");
	if (!a_crtc->dpu_trace_wq) {
		DRM_ERROR("%s: failed to create wq.\n", __func__);
		ret = -ESRCH;
		goto alloc_fail;
	}

	timer_setup(&a_crtc->cfg_rdy_timer, spacemit_cfg_rdy_timer_handler, 0);
	INIT_WORK(&a_crtc->work_dpu_trace, dpu_trace);
	INIT_WORK(&a_crtc->work_update_clk, dpu_wq_update_clk);
	INIT_WORK(&a_crtc->work_update_bw, dpu_wq_update_bw);

	plane = spacemit_plane_init(drm_dev, a_crtc);
	if (IS_ERR_OR_NULL(plane)) {
		ret = PTR_ERR(plane);
		goto err_destroy_workqueue;
	}

	ports = of_get_child_by_name(np, "ports");
	if (!ports) {
		DRM_ERROR("CRTC %pOF has no ports node\n", np);
		ret = -EINVAL;
		goto err_destroy_workqueue;
	}

	port = of_get_child_by_name(ports, "port");
	if (!port) {
		DRM_ERROR("CRTC %pOF has no port@X node\n", np);
		of_node_put(ports);
		ret = -EINVAL;
		goto err_destroy_workqueue;
	}

	ret = spacemit_crtc_bind_init(drm_dev, &a_crtc->crtc, plane, port);
	if (ret)
		goto err_destroy_workqueue;

	spacemit_dpu_sysfs_init(dev);

#ifdef CONFIG_SPACEMIT_DEBUG
	clk_ctx = &a_crtc->clk_ctx;
	a_crtc->is_dpu_running = check_dpu_running_status;
	a_crtc->nb.notifier_call = dpu_clkoffdet_notifier_handler;
	clk_notifier_register(clk_ctx->mclk, &a_crtc->nb);
#endif
	a_crtc->max_mclk_nb.notifier_call = dpu_max_mclk_notifier_hander;
	dpu_max_mclk_notifier_register(&a_crtc->max_mclk_nb);

	DRM_INFO("dpu driver probe success\n");

	return 0;

err_destroy_workqueue:
	destroy_workqueue(a_crtc->dpu_trace_wq);
alloc_fail:
	of_reserved_mem_device_release(a_crtc->dev);
	return ret;
}

static void spacemit_dpu_unbind(struct device *dev, struct device *master,
	void *data)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);
#ifdef CONFIG_SPACEMIT_DEBUG
	struct dpu_clk_context *clk_ctx = &a_crtc->clk_ctx;
#endif

	DRM_INFO("%s()\n", __func__);

#if IS_ENABLED(CONFIG_SPACEMIT_DDR_FC) && defined(CONFIG_PM)
	rm_spacemit_ddr_bw_cons(a_crtc->ddr_qos_cons);
#endif
	pm_runtime_disable(dev);
	of_reserved_mem_device_release(a_crtc->dev);
	drm_crtc_cleanup(&a_crtc->crtc);

#ifdef CONFIG_SPACEMIT_DEBUG
	clk_notifier_unregister(clk_ctx->mclk, &a_crtc->nb);
#endif

}

static const struct component_ops dpu_component_ops = {
	.bind = spacemit_dpu_bind,
	.unbind = spacemit_dpu_unbind,
};

static int spacemit_dpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spacemit_crtc *a_crtc;
	struct device_node *np = dev->of_node;
	const char *str;
	u32 dpu_id;
	u32 pipeline_id;
	u32 is_edp;
	u32 dpu_out_format;
	DRM_INFO("%s()\n", __func__);
	if (!dev->of_node) {
		DRM_DEV_ERROR(dev, "can't find dpu devices\n");
		return -ENODEV;
	}

	a_crtc = devm_kzalloc(dev, sizeof(*a_crtc), GFP_KERNEL);
	if (!a_crtc)
		return -ENOMEM;
	a_crtc->dev = dev;
	a_crtc->power_on = false;
	a_crtc->logo_booton = true;
	a_crtc->rpm_status = false;
	dev_set_drvdata(dev, a_crtc);

	a_crtc->mclk_reset = devm_reset_control_get_optional_shared(&pdev->dev, "mclk_reset");
	if (IS_ERR_OR_NULL(a_crtc->mclk_reset))
		DRM_DEV_DEBUG(dev, "not found mclk_reset\n");
	a_crtc->esc_reset = devm_reset_control_get_optional_shared(&pdev->dev, "esc_reset");
	if (IS_ERR_OR_NULL(a_crtc->esc_reset))
		DRM_DEV_DEBUG(dev, "not found esc_reset\n");
	a_crtc->lcd_reset = devm_reset_control_get_optional_shared(&pdev->dev, "lcd_reset");
	if (IS_ERR_OR_NULL(a_crtc->lcd_reset))
		DRM_DEV_DEBUG(dev, "not found lcd_reset\n");
	a_crtc->aclk_reset = devm_reset_control_get_optional_shared(&pdev->dev, "aclk_reset");
	if (IS_ERR_OR_NULL(a_crtc->aclk_reset))
		DRM_DEV_DEBUG(dev, "not found aclk_reset\n");
	a_crtc->dsc_reset = devm_reset_control_get_optional_shared(&pdev->dev, "dsc_reset");
	if (IS_ERR_OR_NULL(a_crtc->dsc_reset))
		DRM_DEV_DEBUG(dev, "not found dsc_reset\n");

	if (of_property_read_u32(np, "pipeline-id", &pipeline_id))
		return -EINVAL;
	a_crtc->dev_id = pipeline_id;

	if (of_property_read_u32(np, "dpu-id", &dpu_id)) {
		DRM_INFO("%s() dpu id was not found\n", __func__);
		a_crtc->dpu_id = 0;
	} else
		a_crtc->dpu_id = dpu_id;

	if (of_property_read_u32(np, "is_edp", &is_edp))
		return -EINVAL;
	a_crtc->is_edp = is_edp ? true : false;

	if (of_property_read_u32(np, "out-format", &dpu_out_format))
		dpu_out_format = OUTFMT_RGB888;
	a_crtc->out_format = dpu_out_format;

	if (!of_property_read_string(np, "ip", &str))
		a_crtc->core = dpu_core_ops_attach(str);
	else
		DRM_WARN("ip was not found\n");

	/* Clk dts nodes must be parsed in head of pm_runtime_xxx */
	if (a_crtc->core && a_crtc->core->parse_dt)
		a_crtc->core->parse_dt(a_crtc, np);

	spacemit_dpu_setup_bootloader_mem(dev);

	return component_add(dev, &dpu_component_ops);
}

static void spacemit_dpu_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dpu_component_ops);
}

static int __maybe_unused dpu_pm_suspend(struct device *dev)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);
	int result;

	DRM_DEBUG("%s()\n", __func__);

	if (a_crtc->core && a_crtc->core->disable_clk)
		a_crtc->core->disable_clk(a_crtc);

	if (!IS_ERR_OR_NULL(a_crtc->lcd_reset)) {
		result = reset_control_assert(a_crtc->lcd_reset);
		if (result < 0)
			DRM_INFO("Failed to assert lcd_reset: %d\n", result);
	}
	if (!IS_ERR_OR_NULL(a_crtc->esc_reset)) {
		result = reset_control_assert(a_crtc->esc_reset);
		if (result < 0)
			DRM_INFO("Failed to assert esc_reset: %d\n", result);
	}
	if (!IS_ERR_OR_NULL(a_crtc->mclk_reset)) {
		result = reset_control_assert(a_crtc->mclk_reset);
		if (result < 0)
			DRM_INFO("Failed to assert mclk_reset: %d\n", result);
	}
	if (!IS_ERR_OR_NULL(a_crtc->aclk_reset)) {
		result = reset_control_assert(a_crtc->aclk_reset);
		if (result < 0)
			DRM_INFO("Failed to assert aclk_reset: %d\n", result);
	}
	if (!IS_ERR_OR_NULL(a_crtc->dsc_reset)) {
			result = reset_control_assert(a_crtc->dsc_reset);
			if (result < 0)
				DRM_INFO("Failed to assert dsc_reset: %d\n", result);
	}

	return 0;
}

static int __maybe_unused dpu_pm_resume(struct device *dev)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);
	int result;

	DRM_DEBUG("%s()\n", __func__);

	if (!IS_ERR_OR_NULL(a_crtc->mclk_reset)) {
		result = reset_control_deassert(a_crtc->mclk_reset);
		if (result < 0)
			DRM_INFO("Failed to deassert mclk_reset: %d\n", result);
	}
	if (!IS_ERR_OR_NULL(a_crtc->esc_reset)) {
		result = reset_control_deassert(a_crtc->esc_reset);
		if (result < 0)
			DRM_INFO("Failed to deassert esc_reset: %d\n", result);
	}
	if (!IS_ERR_OR_NULL(a_crtc->lcd_reset)) {
		result = reset_control_deassert(a_crtc->lcd_reset);
		if (result < 0)
			DRM_INFO("Failed to deassert lcd_reset: %d\n", result);
	}
	if (!IS_ERR_OR_NULL(a_crtc->aclk_reset)) {
		result = reset_control_deassert(a_crtc->aclk_reset);
		if (result < 0)
			DRM_INFO("Failed to deassert aclk_reset: %d\n", result);
	}
	if (!IS_ERR_OR_NULL(a_crtc->dsc_reset)) {
			result = reset_control_deassert(a_crtc->dsc_reset);
			if (result < 0)
				DRM_INFO("Failed to deassert dsc_reset: %d\n", result);
	}

	if (a_crtc->core && a_crtc->core->enable_clk)
		a_crtc->core->enable_clk(a_crtc);

	return 0;
}

static int __maybe_unused dpu_rt_pm_suspend(struct device *dev)
{
	// struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);

	DRM_DEBUG("%s() \n", __func__);

	return 0;
}

static int __maybe_unused dpu_rt_pm_resume(struct device *dev)
{
	// struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);

	DRM_DEBUG("%s() \n", __func__);

	return 0;
}

static const struct dev_pm_ops dpu_pm_ops = {
	SET_RUNTIME_PM_OPS(dpu_rt_pm_suspend,
			dpu_rt_pm_resume,
			NULL)
};

static const struct of_device_id dpu_match_table[] = {
	{ .compatible = "spacemit,dpu-saturn" },
	{},
};
MODULE_DEVICE_TABLE(of, dpu_match_table);

struct platform_driver spacemit_dpu_driver = {
	.probe = spacemit_dpu_probe,
	.remove = spacemit_dpu_remove,
	.driver = {
		.name = "spacemit-dpu-drv",
		.of_match_table = dpu_match_table,
		.pm = &dpu_pm_ops,
	},
};

MODULE_DESCRIPTION("SPACEMIT Display Controller Driver");
MODULE_LICENSE("GPL v2");
