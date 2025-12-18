// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include "spacemit_bootloader.h"

bool spacemit_dpu_logo_booton;
EXPORT_SYMBOL_GPL(spacemit_dpu_logo_booton);
struct reserved_mem *bootloader_mem;
struct work_struct work_free_bootloader_mem;

static void __free_bootloader_mem(struct work_struct *work)
{
	struct page *page;
	phys_addr_t size;

	/* Give back logo reserved memory to buddy system */
	memblock_free((void *)bootloader_mem->base, bootloader_mem->size);
	for (size = 0; size < bootloader_mem->size; size += PAGE_SIZE) {
		page = phys_to_page(bootloader_mem->base + size);
		free_reserved_page(page);
	}

	pr_debug("released bootloader logo memory!\n");
}

void spacemit_dpu_free_bootloader_mem(void)
{
	/*
	 * Freeing pages to buddy system may take several milliseconds.
	 * Use workqueue here for drm performance consideration.
	 */
	queue_work(system_wq, &work_free_bootloader_mem);
}

#ifndef MODULE
static int __init spacemit_dpu_bootloader_mem_setup(struct reserved_mem *rmem)
{
	pr_info("Reserved memory: detected reboot memory at %pa, size %ld MB\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M);

	spacemit_dpu_logo_booton = true;
	bootloader_mem = rmem;
	INIT_WORK(&work_free_bootloader_mem, __free_bootloader_mem);

	return 0;
}

RESERVEDMEM_OF_DECLARE(bootloader_logo, "bootloader_logo", spacemit_dpu_bootloader_mem_setup);
#else
struct reserved_mem r_mem;
int spacemit_dpu_bootloader_mem_setup(struct reserved_mem *rmem)
{
	pr_info("Reserved memory: detected bootloader_logo memory at %pa, size %ld MB\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M);

	spacemit_dpu_logo_booton = true;
	r_mem = *rmem;
	bootloader_mem = &r_mem;
	INIT_WORK(&work_free_bootloader_mem, __free_bootloader_mem);

	return 0;
}
#endif
