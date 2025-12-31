/*
 * spacemit_pcie_phy.c
 * Ported from Spacemit U-Boot PHY driver
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/iopoll.h>
#include "spacemit_pcie_phy.h"

#define PHYS_PU_COMBO_MGMT_BASE 0x81400000
#define PHYS_PMUA_REG_BASE      0xD4282800
#define PHYS_K2_APB_BASE        0xD4090000

#define MAP_SIZE_DEFAULT        0x10000
#define MAP_SIZE_PHY_COMBO      0x2000000

#define PHY0_OFFSET  0x0900000
#define PHY1_OFFSET  0x0A00000
#define PHY2_OFFSET  0x0B00000
#define PHY3_OFFSET  0x0C00000
#define PHY4_OFFSET  0x0D00000
#define PHY5_OFFSET  0x0E00000

#define K2_APB_SPARE31_OFFSET   0x178
#define K2_APB_SPARE32_OFFSET   0x17C

#define PCIE_REF_CLK_OUTPUT
#define PORTA_X4

static inline u32 phy_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static inline void phy_writel(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + offset);
}

static inline void phy_mod_bit(void __iomem *base, u32 offset, u32 mask, u32 set)
{
	u32 val = readl(base + offset);
	val &= ~mask;
	if (set)
		val |= mask;
	writel(val, base + offset);
}


#ifdef R_CAL_CHECK
static void hsio_rcal_ovrd_start(void __iomem *apb_base)
{
	u32 rd_data;

	phy_mod_bit(apb_base, 0x178, (1 << 17), 0);

	rd_data = phy_readl(apb_base, 0x17c);
	pr_debug("APB+0x17c: %x\n", rd_data);

	phy_mod_bit(apb_base, 0x17c, (0xff << 20), 0);

	rd_data = phy_readl(apb_base, 0x17c);
	rd_data |= (0x9 << 20);
	rd_data |= (0x9 << 24);
	phy_writel(apb_base, 0x17c, rd_data);

	rd_data = phy_readl(apb_base, 0x17c);
	pr_debug("APB+0x17c after: %x\n", rd_data);
}

static void hsio_rcal_ovrd_check(void __iomem *phy_base, void __iomem *apb_base)
{
	u32 rd_data;
	u32 val;

	phy_mod_bit(phy_base, 0x14c, (1 << 9), 0);

	/* ovrd settings */
	val = phy_readl(apb_base, 0x17c);
	val |= (3 << 28);
	val |= (1 << 30);
	val |= (1U << 31);
	phy_writel(apb_base, 0x17c, val);

	/* wait for r calibration status done stable */
	readl_poll_timeout(phy_base + 0x150, rd_data, ((rd_data >> 8) & 0x1), 10, 10000);

	phy_mod_bit(phy_base, 0x14c, (1 << 9), 1);

	rd_data = phy_readl(phy_base, 0x150);
	if (((rd_data >> 4 & 0xf) == 0x9) && (rd_data & 0xf) == 0x9)
		pr_debug("R calibration value top ovrd success\n");
	else
		pr_err("R calibration value top ovrd Error\n");
}
#endif

static void init_x1_phy(void __iomem *phy_base, void __iomem *pmu_base)
{
	int i;
        u32 rd_data;
        u32 clk_res_offset = 0x1E8; /* PMUA_REG_BASE+0x1E8 */

        phy_mod_bit(pmu_base, clk_res_offset, (1 << 30), 0);

        pr_info("Now int init_x1_puphy...\n");

#ifndef PCIE_100M_REF_CLK
        /* select 24Mhz refclock input pll_reg2[7:4]=2 */
        rd_data = phy_readl(phy_base, (0x16 << 2));
        rd_data &= 0xffff0fff;
        rd_data |= 0x00002000;
        phy_writel(phy_base, (0x16 << 2), rd_data);

        phy_mod_bit(phy_base, (0x17 << 2), (0x1 << 21), 0);

        for (i = 0; i < 2; i++) {
                phy_mod_bit(phy_base + (0x400 * i), (0x14 << 2), 0x3, 0);
        }

#ifdef PCIE_REF_CLK_OUTPUT
        phy_mod_bit(phy_base, (0x17 << 2), (0x1 << 20), 1);
        phy_writel(phy_base, (0x14 << 2), 0x00006505);
#endif
#endif

	/* pll_reg1 of lane0, disable ssc pll_reg4[3:0]=4'h0 */
        rd_data = phy_readl(phy_base, (0x16 << 2));
        rd_data &= 0xf0ffffff;
        phy_writel(phy_base, (0x16 << 2), rd_data);

	for (i = 0; i < 1; i++) {
                void __iomem *lane_base = phy_base + (0x400 * i);

                phy_mod_bit(lane_base, (0x10 << 2), (0x1 << 13), 1);

                phy_writel(lane_base, (0x02 << 2), 0xf << 3);

                phy_mod_bit(lane_base, (0x50 << 2), (1 << 4), 1);

                phy_mod_bit(lane_base, (0x19 << 2), (1 << 22), 1);
        }

        /* Force RCV Good / Dynamic Lock */
        for (i = 0; i < 1; i++) {
                void __iomem *lane_base = phy_base + (0x400 * i);

                /* cdr fix bypass */
                phy_mod_bit(lane_base, 0x4, (0x1 << 6), 0);
                /* dynamic lock */
                phy_mod_bit(lane_base, 0xC, (0x1 << 2), 1);
        }

	/* Force RCV done */
        for (i = 0; i < 1; i++) {
                phy_mod_bit(phy_base + (0x400 * i), (0x06 << 2), (0x1 << 10), 1);
        }

        /* Set init done */
        for (i = 0; i < 1; i++) {
                void __iomem *lane_base = phy_base + (0x400 * i);
                /* cfg_sw_phy_init_done */
                phy_mod_bit(lane_base, (0x02 << 2), (0x1 << 11), 1);

                rd_data = phy_readl(lane_base, (0x02 << 2));
                rd_data &= ~(0xf << 7);
                phy_writel(lane_base, (0x02 << 2), rd_data);

                /* aux clk 24M */
                rd_data = phy_readl(lane_base, (0x02 << 2));
                rd_data |= (0x2 << 7);
                phy_writel(lane_base, (0x02 << 2), rd_data);
        }
}


static void init_x2_phy(void __iomem *phy_base, void __iomem *pmu_base)
{
	int i;
	u32 rd_data;
	u32 clk_res_offset = 0x1F0; /* PMUA_REG_BASE+0x1F0 */

	phy_mod_bit(pmu_base, clk_res_offset, (1 << 30), 0);

	pr_info("Now int init_x2_puphy...\n");

#ifndef PCIE_100M_REF_CLK
	/* select 24Mhz refclock input pll_reg2[7:4]=2 */
	rd_data = phy_readl(phy_base, (0x16 << 2));
	rd_data &= 0xffff0fff;
	rd_data |= 0x00002000;
	phy_writel(phy_base, (0x16 << 2), rd_data);

	phy_mod_bit(phy_base, (0x17 << 2), (0x1 << 21), 0);

	for (i = 0; i < 2; i++) {
		phy_mod_bit(phy_base + (0x400 * i), (0x14 << 2), 0x3, 0);
	}

#ifdef PCIE_REF_CLK_OUTPUT
	phy_mod_bit(phy_base, (0x17 << 2), (0x1 << 20), 1);
	phy_writel(phy_base, (0x14 << 2), 0x00006505);
#endif
#endif

	/* pll_reg1 of lane0, disable ssc pll_reg4[3:0]=4'h0 */
	rd_data = phy_readl(phy_base, (0x16 << 2));
	rd_data &= 0xf0ffffff;
	phy_writel(phy_base, (0x16 << 2), rd_data);

	for (i = 0; i < 2; i++) {
		void __iomem *lane_base = phy_base + (0x400 * i);

		phy_mod_bit(lane_base, (0x10 << 2), (0x1 << 13), 1);

		phy_writel(lane_base, (0x02 << 2), 0xf << 3);

		phy_mod_bit(lane_base, (0x50 << 2), (1 << 4), 1);

		phy_mod_bit(lane_base, (0x19 << 2), (1 << 22), 1);
	}

	/* Force RCV Good / Dynamic Lock */
	for (i = 0; i < 2; i++) {
		void __iomem *lane_base = phy_base + (0x400 * i);

		/* cdr fix bypass */
		phy_mod_bit(lane_base, 0x4, (0x1 << 6), 0);
		/* dynamic lock */
		phy_mod_bit(lane_base, 0xC, (0x1 << 2), 1);
	}

	/* Force RCV done */
	for (i = 0; i < 2; i++) {
		phy_mod_bit(phy_base + (0x400 * i), (0x06 << 2), (0x1 << 10), 1);
	}

	/* Set init done */
	for (i = 0; i < 2; i++) {
		void __iomem *lane_base = phy_base + (0x400 * i);
		/* cfg_sw_phy_init_done */
		phy_mod_bit(lane_base, (0x02 << 2), (0x1 << 11), 1);

		rd_data = phy_readl(lane_base, (0x02 << 2));
		rd_data &= ~(0xf << 7);
		phy_writel(lane_base, (0x02 << 2), rd_data);

		/* aux clk 24M */
		rd_data = phy_readl(lane_base, (0x02 << 2));
		rd_data |= (0x2 << 7);
		phy_writel(lane_base, (0x02 << 2), rd_data);
	}
}

static void pcie_ssc_open(void __iomem *phy_base)
{
	/* SSC option is not used; keep empty stub for callers */
	(void)phy_base;
}

static void wait_phy_pll_lock(void __iomem *phy_base)
{
	u32 rd_data = 0;
	pr_info("waiting pll lock...\n");
	if (readl_poll_timeout(phy_base + 0x8, rd_data, (rd_data & 0x1), 100, 100000)) {
		pr_err("PHY PLL Lock Timeout! Status: 0x%x\n", rd_data);
	} else {
		pr_info("PHY PLL Locked.\n");
	}
}

int spacemit_pcie_init_phy(int port_id)
{
	void __iomem *combo_base;
	void __iomem *pmu_base;
	void __iomem *apb_base;
	void __iomem *phy0_base;
	void __iomem *phy1_base;
	void __iomem *phy5_base;
	u32 val;
	int phy_init_done = 0;

	if (phy_init_done) {
		pr_info("spacemit-pcie: PHY already initialized\n");
		return 0;
	}

	combo_base = ioremap(PHYS_PU_COMBO_MGMT_BASE, MAP_SIZE_PHY_COMBO);
	if (!combo_base) {
		pr_err("Failed to map PHY COMBO base\n");
		return -ENOMEM;
	}

	pmu_base = ioremap(PHYS_PMUA_REG_BASE, MAP_SIZE_DEFAULT);
	if (!pmu_base) {
		pr_err("Failed to map PMU base\n");
		iounmap(combo_base);
		return -ENOMEM;
	}

	apb_base = ioremap(PHYS_K2_APB_BASE, MAP_SIZE_DEFAULT);
	if (!apb_base) {
		pr_err("Failed to map APB base\n");
		iounmap(pmu_base);
		iounmap(combo_base);
		return -ENOMEM;
	}

	phy0_base = combo_base + PHY0_OFFSET;
	phy1_base = combo_base + PHY1_OFFSET;
	phy5_base = combo_base + PHY5_OFFSET;

	pr_info("spacemit-pcie: Starting PHY initialization...\n");

	phy_mod_bit(apb_base, K2_APB_SPARE31_OFFSET, (0x1 << 17), 1);

	phy_mod_bit(pmu_base, 0x1D8, 0x00000010, 1);
	if (port_id == 0){
		phy_mod_bit(pmu_base, 0x1D8, 0x00000008, 1);
#ifdef PORTA_X4
		phy_mod_bit(pmu_base, 0x1D8, (0x1 << 3), 0);
#endif

		val = phy_readl(pmu_base, 0x1D8);
		if (((val >> 3) & 0x1) == 1) {
			/* PCIe A x2(phy0) + PCIe B x2(phy1) */
			pr_info("Configuring PCIe A x2\n");
			init_x2_phy(phy0_base, pmu_base);
			wait_phy_pll_lock(phy0_base);
			printk("Now finish puphy PHY0 init ...\n");
			pcie_ssc_open(phy0_base);
		} else {
			/* PCIe A x4(phy0,phy1) */
			pr_info("Configuring PCIe A x4\n");
			init_x2_phy(phy0_base, pmu_base);
			init_x2_phy(phy1_base, pmu_base);

			wait_phy_pll_lock(phy0_base);
			wait_phy_pll_lock(phy1_base);

			pcie_ssc_open(phy0_base);
		}
	} else if (port_id == 4) {
		init_x1_phy(phy5_base, pmu_base);
		wait_phy_pll_lock(phy5_base);
		pcie_ssc_open(phy5_base);
	}

	phy_init_done = 1;
	pr_info("spacemit-pcie: PHY initialization done.\n");

	iounmap(apb_base);
	iounmap(pmu_base);
	iounmap(combo_base);

	return 0;
}
