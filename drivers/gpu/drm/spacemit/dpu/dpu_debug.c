// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/io.h>
#include <linux/trace_events.h>
#include <drm/drm_gem.h>
#include <drm/drm_framebuffer.h>
#include "dpu_debug.h"
#include "dpu_trace.h"
#include "../spacemit_dpu_reg.h"
#include "../spacemit_drm.h"

#if IS_ENABLED(CONFIG_GKI_FIX_WORKAROUND)
static struct file *gki_filp_open(const char *filename, int flags, umode_t mode)
{
	return 0;
}
static ssize_t gki_kernel_write(struct file *file, const void *buf, size_t count,
			    loff_t *pos)
{
	return 0;
}
#endif
#define DPU_BUFFER_DUMP_FILE "/mnt/dpu_buffer_dump"
int dpu_buffer_dump(struct drm_plane *plane)
{
	unsigned int buffer_size = 0;
	int i = 0;
	void *mmu_tbl_vaddr = NULL;
	phys_addr_t dpu_buffer_paddr = 0;
	void __iomem *dpu_buffer_vaddr = NULL;
	loff_t pos = 0;
	static int dump_once = true;
	struct file *filep = NULL;
	struct spacemit_plane_state *spacemit_pstate = to_spacemit_plane_state(plane->state);

	if (!dump_once)
		return 0;

	mmu_tbl_vaddr = spacemit_pstate->mmu_tbl.va;
	buffer_size = plane->state->fb->obj[0]->size >> PAGE_SHIFT;

#if IS_ENABLED(CONFIG_GKI_FIX_WORKAROUND)
	filep = gki_filp_open(DPU_BUFFER_DUMP_FILE, O_RDWR | O_APPEND | O_CREAT, 0644);
#else
	filep = filp_open(DPU_BUFFER_DUMP_FILE, O_RDWR | O_APPEND | O_CREAT, 0644);
#endif

	if (IS_ERR(filep)) {
		pr_err("Open file %s error\n", DPU_BUFFER_DUMP_FILE);
		return -EINVAL;
	}
	for (i = 0; i < buffer_size; i++) {
		dpu_buffer_paddr = *(volatile u32 __force *)mmu_tbl_vaddr;
		dpu_buffer_paddr = dpu_buffer_paddr << PAGE_SHIFT;
		dpu_buffer_vaddr = phys_to_virt((unsigned long)dpu_buffer_paddr);
		mmu_tbl_vaddr += 4;
#if IS_ENABLED(CONFIG_GKI_FIX_WORKAROUND)
		gki_kernel_write(filep, (void *)dpu_buffer_vaddr, PAGE_SIZE, &pos);
#else
		kernel_write(filep, (void *)dpu_buffer_vaddr, PAGE_SIZE, &pos);
#endif
	}

	filp_close(filep, NULL);
	filep = NULL;

	dump_once = false;

	return 0;
}

void dpu_dump_fps(struct spacemit_crtc *a_crtc)
{
	struct timespec64 cur_tm, tmp_tm;

	if (!a_crtc->enable_dump_fps)
		return;

	ktime_get_real_ts64(&cur_tm);
	tmp_tm = timespec64_sub(cur_tm, a_crtc->last_tm);
	a_crtc->last_tm.tv_sec = cur_tm.tv_sec;
	a_crtc->last_tm.tv_nsec = cur_tm.tv_nsec;
	if (tmp_tm.tv_sec == 0)
		trace_printk("fps: %ld\n", 1000000000 / (tmp_tm.tv_nsec / 1000));
}

//static unsigned long last_exec_time = 0;
void dpu_trace(struct work_struct *work)
{
	// unsigned long current_time = jiffies;
	// if (time_after(current_time, last_exec_time + (60 * HZ))) {
	// 	last_exec_time = current_time;
	// 	logger_noti_helper(LOG_DUMP_NOTI_DPU);
	// }
}
