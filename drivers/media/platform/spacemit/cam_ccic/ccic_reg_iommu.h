/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hw_reg_iommu.h
 *
 * register for iommu
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */

#ifndef _CCIC_REG_IOMMU_H_
#define _CCIC_REG_IOMMU_H_

/* TBU(n) registers */
#define REG_IOMMU_TTBL(n)		(0x080 + 0x20 * (n))
#define REG_IOMMU_TTBH(n)		(0x084 + 0x20 * (n))
#define REG_IOMMU_TCR0(n)		(0x088 + 0x20 * (n))
#define REG_IOMMU_TCR1(n)		(0x08c + 0x20 * (n))
#define REG_IOMMU_STAT(n)		(0x090 + 0x20 * (n))

/* TOP registers */
#define REG_IOMMU_BVAL		(0x000)
#define REG_IOMMU_BVAH		(0x004)
#define REG_IOMMU_TVAL		(0x020)
#define REG_IOMMU_TVAH		(0x024)
#define REG_IOMMU_GIRQ_STAT	(0x018)
#define REG_IOMMU_GIRQ_ENA	(0x01c)
#define REG_IOMMU_TIMEOUT	(0x010)
#define REG_IOMMU_ERR_CLR	(0x014)
#define REG_IOMMU_RD_LVAL	(0x028)
#define REG_IOMMU_RD_LVAH	(0x02c)
#define REG_IOMMU_RD_LPAL	(0x030)
#define REG_IOMMU_RD_LPAH	(0x034)
#define REG_IOMMU_WR_LVAL	(0x038)
#define REG_IOMMU_WR_LVAH	(0x03c)
#define REG_IOMMU_WR_LPAL	(0x040)
#define REG_IOMMU_WR_LPAH	(0x044)
//#define REG_IOMMU_LVAL		(0x12220)
//#define REG_IOMMU_LVAH		(0x12224)
//#define REG_IOMMU_LPAL		(0x12228)
//#define REG_IOMMU_LPAH		(0x1222c)
#define REG_IOMMU_TIMEOUT_ADDR_LOW	(0x008)
#define REG_IOMMU_TIMEOUT_ADDR_HIGH	(0x00c)
//#define REG_IOMMU_VER		(0x1223c)

#define MMU_RD_TIMEOUT	(1 << 16)
#define MMU_WR_TIMEOUT	(1 << 17)

#endif /* ifndef __REGS_ISP_IOMMU_H__ */
