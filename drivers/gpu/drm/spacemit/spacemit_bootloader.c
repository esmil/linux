// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include "spacemit_bootloader.h"

static struct reserved_mem *bootloader_mem;
static struct reserved_mem bootloader_mem_copy;
static struct work_struct work_free_bootloader_mem;
static DEFINE_MUTEX(bootloader_mem_lock);
static bool bootloader_mem_release_queued;
static unsigned int bootloader_mem_release_target = 1;
static unsigned int bootloader_mem_release_count;

static void __free_bootloader_mem(struct work_struct *work)
{
	struct reserved_mem rmem;
	struct page *page;
	phys_addr_t size;

	mutex_lock(&bootloader_mem_lock);
	if (!bootloader_mem) {
		bootloader_mem_release_queued = false;
		mutex_unlock(&bootloader_mem_lock);
		return;
	}

	rmem = *bootloader_mem;
	bootloader_mem = NULL;
	bootloader_mem_release_queued = false;
	mutex_unlock(&bootloader_mem_lock);

	/* Give back reserved framebuffer pages to buddy system */
	for (size = 0; size < rmem.size; size += PAGE_SIZE) {
		page = phys_to_page(rmem.base + size);
		free_reserved_page(page);
	}

	pr_debug("released reserved framebuffer memory at %pa, size %ld MB\n",
		&rmem.base, (unsigned long)rmem.size / SZ_1M);
}

void spacemit_dpu_free_bootloader_mem(void)
{
	unsigned int target, count;

	/*
	 * Freeing pages to buddy system may take several milliseconds.
	 * Use workqueue here for drm performance consideration.
	 */
	mutex_lock(&bootloader_mem_lock);
	if (!bootloader_mem || bootloader_mem_release_queued) {
		mutex_unlock(&bootloader_mem_lock);
		return;
	}

	bootloader_mem_release_count++;
	target = bootloader_mem_release_target;
	count = bootloader_mem_release_count;
	if (count < target) {
		mutex_unlock(&bootloader_mem_lock);
		pr_debug("defer bootloader framebuffer release (%u/%u)\n",
			count, target);
		return;
	}

	bootloader_mem_release_queued = true;
	mutex_unlock(&bootloader_mem_lock);

	pr_debug("bootloader framebuffer release threshold reached (%u/%u)\n",
		count, target);
	queue_work(system_wq, &work_free_bootloader_mem);
}

void spacemit_dpu_set_bootloader_mem_release_target(unsigned int count)
{
	mutex_lock(&bootloader_mem_lock);
	bootloader_mem_release_target = count ? count : 1;
	mutex_unlock(&bootloader_mem_lock);
}

int spacemit_dpu_bootloader_mem_setup(struct reserved_mem *rmem)
{
	mutex_lock(&bootloader_mem_lock);
	if (bootloader_mem &&
	    bootloader_mem->base == rmem->base &&
	    bootloader_mem->size == rmem->size) {
		mutex_unlock(&bootloader_mem_lock);
		return 0;
	}

	bootloader_mem_copy = *rmem;
	bootloader_mem = &bootloader_mem_copy;
	bootloader_mem_release_queued = false;
	bootloader_mem_release_count = 0;
	INIT_WORK(&work_free_bootloader_mem, __free_bootloader_mem);
	mutex_unlock(&bootloader_mem_lock);

	pr_info("Reserved memory: detected framebuffer at %pa, size %ld MB\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M);

	return 0;
}

#ifndef MODULE
static int __init spacemit_dpu_bootloader_reserved_mem_setup(struct reserved_mem *rmem)
{
	return spacemit_dpu_bootloader_mem_setup(rmem);
}

RESERVEDMEM_OF_DECLARE(framebuffer, "framebuffer",
		       spacemit_dpu_bootloader_reserved_mem_setup);
#endif
