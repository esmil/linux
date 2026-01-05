// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory cacheability check for RISC-V uaccess optimization
 *
 * This file provides a C function that can be called from assembly
 * to determine if a buffer is cacheable before using vector instructions.
 */

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

/**
 * is_cacheable_safe - Check if memory buffer is cacheable
 * @addr: Virtual address to check (kernel or user space)
 *
 * Returns: 1 if cacheable, 0 if non-cacheable
 *
 * This function is designed to be called from assembly code in uaccess.S
 * to determine if vector instructions are safe to use for memory copy.
 *
 * Non-cacheable memory (device IO, DMA coherent buffers) should not use
 * vector instructions as they may cause cache coherency issues.
 *
 * Handles both kernel and user space addresses safely:
 * - Kernel direct mapping: Always cacheable
 * - Kernel vmalloc: Check VM flags
 * - User space: Check page table (most are cacheable)
 * - ioremap/DMA: Non-cacheable
 */
int is_cacheable_safe(const void *addr)
{
	unsigned long vaddr = (unsigned long)addr;

	/* Kernel direct mapped memory - always cacheable */
	if (virt_addr_valid(addr))
		return 1;

	if (vaddr < TASK_SIZE) {
		/*
		 * User space address, Determine it as a cacheable buffer,
		 * maybe not safe!!
		 */
		return 1;
	}

	/* Check if it's a vmalloc region (kernel virtual address) */
	if (is_vmalloc_addr(addr)) {
		struct vm_struct *vm;

		vm = find_vm_area(addr);
		if (!vm)
			return 0;

		/* Exclude ioremap and DMA coherent buffers */
		if (vm->flags & (VM_IOREMAP | VM_DMA_COHERENT))
			return 0;

		/* Normal vmalloc - cacheable */
		return 1;
	}

	/* Unknown kernel region - assume non-cacheable for safety */
	return 0;
}
