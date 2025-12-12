// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Spacemit Co., Ltd.
 *
 */

#include "linux/compiler.h"
#include <linux/stddef.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>
#include <drm/drm_framebuffer.h>

#include "spacemit_cmdlist.h"
#include "spacemit_dmmu.h"
#include "spacemit_gem.h"
#include "spacemit_drm.h"
#include "spacemit_crtc.h"
#include "spacemit_dpu_reg.h"
#include "spacemit_wb.h"

static inline void spacemit_dmmu_fill_pgtable(uint32_t *ttbr, struct sg_table *sgt)
{
	struct sg_dma_page_iter dma_iter;
	uint32_t i = 0, temp = 0;

	for_each_sgtable_dma_page(sgt, &dma_iter, 0)
		*ttbr++ = (sg_page_iter_dma_address(&dma_iter) >> PAGE_SHIFT) & 0x3FFFFFF;
	/*
	 * Due to slicon's alignment access requirement, fill more mmu entries
	 * to avoid ddrc filter error. We simply use the last valid ttbr value.
	 */
	temp = *(ttbr - 1);
	for (i = 0; i < HW_ALIGN_TTB_NUM; i++)
		*ttbr++ = temp;
}

int spacemit_dmmu_map(struct drm_framebuffer *fb, struct dpu_mmu_tbl *mmu_tbl,
		 u8 tbu_id, struct spacemit_wb *wb, struct cmdlist_regs *cl_rdma,
		 struct drm_plane *plane)
{
	struct spacemit_drm_private *priv = fb->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	const struct drm_format_info *format = NULL;
	struct sg_table *sgt = NULL;
	uint32_t total_size, offset1, offset2;
	struct tbu_instance tbu = {0x0};
	u8 fbc_mode, plane_num;
	u32 val;
	int header_size;

	format = fb->format;
	sgt = to_spacemit_obj(fb->obj[0])->sgt;

	if (!wb && priv->contig_mem) {
		phys_addr_t contig_pa = sg_dma_address(sgt->sgl);

		hwdev->rdma_contig_mem(hwdev, tbu_id, contig_pa, fb, cl_rdma, plane);
		return 0;
	}

	plane_num = format->num_planes;
	fbc_mode = (fb->modifier > 0);

	total_size = roundup(fb->obj[0]->size, PAGE_SIZE);

	if (fbc_mode && wb)
		header_size = spacemit_wb_get_afbc_header_size(wb, fb, fb->width, fb->height);

	offset1 = plane_num > 1 ? fb->offsets[1] : total_size;
	offset2 = plane_num > 2 ? fb->offsets[2] : total_size;

	switch (plane_num) {
	case 3:
		tbu.ttb_pa[2] = mmu_tbl->pa + (offset2 >> PAGE_SHIFT) * 4;
		tbu.tbu_va[2] = TBU_BASE_VA(tbu_id) + offset2;
		tbu.ttb_size[2] = PAGE_ALIGN(total_size - rounddown(offset2, PAGE_SIZE)) >> PAGE_SHIFT;
		fallthrough;
	case 2:
		tbu.ttb_pa[1] = mmu_tbl->pa + (offset1 >> PAGE_SHIFT) * 4;
		tbu.tbu_va[1] = TBU_BASE_VA(tbu_id) + offset1;
		tbu.ttb_size[1] = PAGE_ALIGN(offset2 - rounddown(offset1, PAGE_SIZE)) >> PAGE_SHIFT;
		fallthrough;
	case 1:
		tbu.ttb_pa[0] = mmu_tbl->pa;
		tbu.tbu_va[0] = TBU_BASE_VA(tbu_id);
		tbu.ttb_size[0] = PAGE_ALIGN(offset1) >> PAGE_SHIFT;
		fallthrough;
	default:
		break;
	}
	/* Special handling for afbc */
	if (fbc_mode) {
		tbu.tbu_va[1] = tbu.tbu_va[0];
		tbu.ttb_pa[1] = tbu.ttb_pa[0];
		tbu.ttb_size[1] = tbu.ttb_size[0];
	}

	spacemit_dmmu_fill_pgtable((uint32_t *)(mmu_tbl->va), sgt);

	val = 0x1 | (fbc_mode << 1) | ((fbc_mode ? 1 : plane_num - 1) << 2);
	/* tbu_burst_limit_en = 1, tlb_fetch_active_en = 1 */
	val |= BIT(4) | BIT(5);
	/* Config hw regs */
	if (wb) {
		val = val | (DPU_QOS_LOW << 8);
		hwdev->wb_dmmu(hwdev, tbu_id, &tbu, fb, val, fbc_mode, header_size, wb);
	} else {
		val = val | (DPU_QOS_URGENT << 8);
		hwdev->rdma_dmmu(hwdev, tbu_id, &tbu, fb, val, cl_rdma, plane);
	}

	return 0;
}

void spacemit_dmmu_unmap(struct drm_plane *plane)
{
	return;
}
