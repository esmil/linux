/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_DPU_REG_H_
#define _SPACEMIT_DPU_REG_H_
#include "dpu/dpu_trace.h"

enum REG_FILED_TYPE {
	FIELD_DEF,  //reg field is rw
	FIELD_W1C,  //reg field is write 1 cleared
	FIELD_W1S,  //reg field is write 1 set
	FIELD_W0C,  //reg field is write 0 cleared
};

static inline void dpu_writel(void __iomem *addr, uint32_t offset, uint32_t data)
{
	trace_spacemit_dpu_reg_write((u32)data, offset, NULL);
	writel(data, (addr + offset));
}

static inline void dpu_set_bit(void __iomem *addr, uint32_t offset, uint32_t mask)
{
	u32 value = 0;
	value = readl(addr + offset) | mask;
	trace_spacemit_dpu_reg_write(value, offset, NULL);
	writel(value, (addr + offset));
}

static inline void dpu_clr_bit(void __iomem *addr, uint32_t offset, uint32_t mask)
{
	u32 value = 0;
	value = readl(addr + offset) & (~mask);
	trace_spacemit_dpu_reg_write(value, offset, NULL);
	writel(value, (addr + offset));
}

#define dpu_read_reg(hwdev, module_name, module_base, field) \
	({ u32 __v = (((volatile module_name *)(module_base + hwdev->base))->field); \
	__v; }) \

#define dpu_write_reg_common(hwdev, module_name, module_base, field, data, type) \
{ \
	volatile module_name *module = (module_name *)(module_base + hwdev->base); \
		module->field = data; \
		trace_spacemit_dpu_reg_write((u32)data, module_base, #field); \
}

/*
 * This API unifies cpu and cmdlist io operations.
 * For cpu io, parameters cl_regs and offset are not required;
 * For cmdlist io,
 *     cl_regs: got from alloc_cmdlist_regs before any io operations to this module;
 *     offset:  the offset of the field in this module.
 */
#define _dpu_write_reg(hwdev, module_name, module_base, field, data, cl_regs, offset, ...) \
{ \
	if (cl_regs != NULL) \
		cmdlist_write_reg(hwdev, module_name, module_base, field, data, cl_regs, offset) \
	else \
		dpu_write_reg_common(hwdev, module_name, module_base, field, data, FIELD_DEF) \
}

#define dpu_write(...) _dpu_write_reg(__VA_ARGS__, NULL, -1)

#define dpu_write_reg(hwdev, module_name, module_base, field, data) \
	dpu_write_reg_common(hwdev, module_name, module_base, field, data, FIELD_DEF)

#endif
