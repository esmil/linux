/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2017 Andes Technology Corporation */

#ifndef _ASM_RISCV_FTRACE_H
#define _ASM_RISCV_FTRACE_H

/*
 * The graph frame test is not possible if CONFIG_FRAME_POINTER is not enabled.
 * Check arch/riscv/kernel/mcount.S for detail.
 */
#if defined(CONFIG_FUNCTION_GRAPH_TRACER) && defined(CONFIG_FRAME_POINTER)
#define HAVE_FUNCTION_GRAPH_FP_TEST
#endif
#define HAVE_FUNCTION_GRAPH_RET_ADDR_PTR

#define ARCH_SUPPORTS_FTRACE_OPS 1
#ifndef __ASSEMBLY__
void _mcount(void);
static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

struct dyn_arch_ftrace {
};
#endif

#ifdef CONFIG_DYNAMIC_FTRACE
#define MCOUNT_ADDR		((unsigned long)_mcount)

/*
 * Let auipc+jalr be the basic *mcount unit*, so we make it 8 bytes here.
 */
#define MCOUNT_INSN_SIZE 8
#endif

#endif /* _ASM_RISCV_FTRACE_H */
