/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_DMMU_H_
#define _SPACEMIT_DMMU_H_

#include <linux/types.h>

#include <drm/drm_file.h>
//#include "dpu/dpu_saturn.h"
#include "spacemit_cmdlist.h"

#define DPU_QOS_URGENT	4
#define DPU_QOS_NORMAL	3
#define DPU_QOS_LOW	2
/*
 * In worst case, the tlb alignment requires src_x * 16 * pixel_bytes.
 * The maximum src_x currently support is 4096. So we need to fill
 * extra 60 (4096*15*4) entries into mmu page table.
 */
#define HW_ALIGN_TTB_NUM	60

#define RD_OUTS_NUM             16
#define RDMA_TIMELIMIT  0xFFFF

#define BASE_VA         0x10000000ULL
#define VA_STEP_PER_TBU 0x40000000ULL

#define TBU_BASE_VA(tbu_id) ((uint64_t)BASE_VA + (uint64_t)VA_STEP_PER_TBU * tbu_id)

struct tbu_instance {
	uint64_t ttb_pa[3];
	uint64_t tbu_va[3];
	uint32_t ttb_size[3];
};

struct dpu_mmu_tbl;
struct spacemit_wb;
int spacemit_dmmu_map(struct drm_framebuffer *fb, struct dpu_mmu_tbl *mmu_tbl,
			u8 tbu_id, struct spacemit_wb *wb, struct cmdlist_regs *cl_rdma,
			struct drm_plane *plane);
void spacemit_dmmu_unmap(struct drm_plane *plane);

#endif /* _SPACEMIT_DMMU_H_ */
