/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 SPACEMIT Micro Limited
 * All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/delay.h>
#include "pwrctrl.h"

#define PMUA_REG_BASE (0xd4282800)
#define PMUA_CSI_CCIC2_CLK_RES_CTRL (0x0024)
#define PMUA_ISP_CLK_RES_CTRL (0x0038)
#define PMUA_LCD_CLK_RES_CTRL2 (0x004c)
#define PMUA_CCIC_CLK_RES_CTRL (0x0050)
#define PMUA_PWR_CTRL_ISP (0x037c)
#define PMUA_PWR_STBL_TIMER (0x0084)
#define PMUA_PWR_BLK_TMR_REG (0x00DC)
#define PMUA_PWR_STATUS_REG (0x00F0)

typedef struct cam_clk_ctrl {
	char *clk_name;
	struct mutex mutex;
	int refcnt;
	void __iomem *reg_base;
	uint32_t reg_offset;
	uint32_t mask_src_sel;
	uint32_t bits_src_sel;
	uint32_t mask_div_rat;
	uint32_t bits_div_rat;
	uint32_t bits_enable;
	uint32_t bits_reset;
	uint32_t bits_fc_request;
} cam_clk_ctrl_t;

cam_clk_ctrl_t ahb_clk = {
	.clk_name = "ahb",
	.reg_offset = PMUA_CCIC_CLK_RES_CTRL,
	.mask_src_sel = 0x0,
	.bits_src_sel = 0x0,
	.mask_div_rat = 0x0,
	.bits_div_rat = 0x0,
	.bits_enable = BIT(3),
	.bits_reset = BIT(0),
	.bits_fc_request = 0x0,
};

cam_clk_ctrl_t isp_fnc_clk = {
	.clk_name = "isp function",
	.reg_offset = PMUA_ISP_CLK_RES_CTRL,
	.mask_src_sel = BIT(8) | BIT(9) | BIT(10),
	.bits_src_sel =
		0x0
		<< 8, // 0x0 = 416MHz, 0x1 = 499MHz, 0x2 = 624,  0x3 = pll1_307
	.mask_div_rat = BIT(4) | BIT(5) | BIT(6),
	.bits_div_rat = 0x0 << 4,
	.bits_enable = BIT(1),
	.bits_reset = BIT(0),
	.bits_fc_request = BIT(7),
};

cam_clk_ctrl_t isp_bus_clk = {
	.clk_name = "isp bus",
	.reg_offset = PMUA_ISP_CLK_RES_CTRL,
	.mask_src_sel = BIT(20) | BIT(21) | BIT(22),
	.bits_src_sel =
		0x0
		<< 20, // 0x0 = 416MHz, 0x1 = 499MHz, 0x2 = 624,  0x3 = pll1_307
	.mask_div_rat = BIT(17) | BIT(18) | BIT(19),
	.bits_div_rat = 0x0 << 17,
	.bits_enable = BIT(16),
	.bits_reset = BIT(15),
	.bits_fc_request = BIT(23),
};

cam_clk_ctrl_t cpp_fnc_clk = {
	.clk_name = "cpp function",
	.reg_offset = PMUA_ISP_CLK_RES_CTRL,
	.mask_src_sel = BIT(27) | BIT(28) | BIT(29),
	.bits_src_sel = 0x0 << 27, // 0x0 = 312MHz, 0x1 = 416MHz
	.mask_div_rat = BIT(24) | BIT(25) | BIT(26),
	.bits_div_rat = 0x0 << 24,
	.bits_enable = BIT(31),
	.bits_reset = BIT(30), // 0 = Reset
	.bits_fc_request = BIT(12),
};

cam_clk_ctrl_t snr_mclk0_clk = {
	.clk_name = "sensor mclk0",
	.reg_offset = PMUA_CSI_CCIC2_CLK_RES_CTRL,
	.mask_src_sel = BIT(16) | BIT(17) | BIT(18),
	.bits_src_sel =
		0x0
		<< 16, // 0x0 = 312 MHz, 0x1 = PLL2_DIV5, 0x2 = 416Mhz, 0x3 = 24Mhz
	.mask_div_rat = BIT(26) | BIT(25) | BIT(24) | BIT(23),
	.bits_div_rat =
		0x0
		<< 23, //Isim_vclk_out = ISIM_VCLK_OUT_DIV / (this field +1), The default value is 312/(11+1) = 26 MHz
	.bits_enable = BIT(28),
	.bits_reset = 0x0,
	.bits_fc_request = 0x0,
};

cam_clk_ctrl_t snr_mclk1_clk = {
	.clk_name = "sensor mclk1",
	.reg_offset = PMUA_CSI_CCIC2_CLK_RES_CTRL,
	.mask_src_sel = BIT(16) | BIT(17) | BIT(18),
	.bits_src_sel =
		0x0
		<< 16, // 0x0 = 312 MHz, 0x1 = PLL2_DIV5, 0x2 = 416Mhz, 0x3 = 26Mhz vctcxo
	.mask_div_rat = BIT(26) | BIT(25) | BIT(24) | BIT(23),
	.bits_div_rat =
		0x0
		<< 23, //Isim_vclk_out = ISIM_VCLK_OUT_DIV / (this field +1), The default value is 312/(11+1) = 26 MHz
	.bits_enable = BIT(6),
	.bits_reset = 0x0,
	.bits_fc_request = 0x0,
};

cam_clk_ctrl_t snr_mclk2_clk = {
	.clk_name = "sensor mclk2",
	.reg_offset = PMUA_CSI_CCIC2_CLK_RES_CTRL,
	.mask_src_sel = BIT(16) | BIT(17) | BIT(18),
	.bits_src_sel =
		0x0
		<< 16, // 0x0 = 312 MHz, 0x1 = PLL2_DIV5, 0x2 = 416Mhz, 0x3 = 26Mhz vctcxo
	.mask_div_rat = BIT(26) | BIT(25) | BIT(24) | BIT(23),
	.bits_div_rat =
		0x0
		<< 23, //Isim_vclk_out = ISIM_VCLK_OUT_DIV / (this field +1), The default value is 312/(11+1) = 26 MHz
	.bits_enable = BIT(27),
	.bits_reset = 0x0,
	.bits_fc_request = 0x0,
};

cam_clk_ctrl_t snr_mclk3_clk = {
	.clk_name = "sensor mclk3",
	.reg_offset = PMUA_CSI_CCIC2_CLK_RES_CTRL,
	.mask_src_sel = BIT(16) | BIT(17) | BIT(18),
	.bits_src_sel =
		0x0
		<< 16, // 0x0 = 312 MHz, 0x1 = PLL2_DIV5, 0x2 = 416Mhz, 0x3 = 26Mhz vctcxo
	.mask_div_rat = BIT(26) | BIT(25) | BIT(24) | BIT(23),
	.bits_div_rat =
		0x0
		<< 23, //Isim_vclk_out = ISIM_VCLK_OUT_DIV / (this field +1), The default value is 312/(11+1) = 26 MHz
	.bits_enable = BIT(10),
	.bits_reset = 0x0,
	.bits_fc_request = 0x0,
};

cam_clk_ctrl_t ccic1_phy_clk = {
	.clk_name = "ccic1 phy",
	.reg_offset = PMUA_CCIC_CLK_RES_CTRL,
	.mask_src_sel = BIT(7),
	.bits_src_sel = 0x0 << 7, // 0 = 104 MHz, 1 = 52 MHz
	.mask_div_rat = 0x0,
	.bits_div_rat = 0x0,
	.bits_enable = BIT(5),
	.bits_reset = BIT(2),
	.bits_fc_request = 0x0,
};

cam_clk_ctrl_t ccic2_phy_clk = {
	.clk_name = "ccic2 phy",
	.reg_offset = PMUA_CSI_CCIC2_CLK_RES_CTRL,
	.mask_src_sel = BIT(7),
	.bits_src_sel = 0x0 << 7, // 0 = 104 MHz, 1 = 52 MHz
	.mask_div_rat = 0x0,
	.bits_div_rat = 0x0, //
	.bits_enable = BIT(5),
	.bits_reset = BIT(2),
	.bits_fc_request = 0x0,
};

cam_clk_ctrl_t ccic3_phy_clk = {
	.clk_name = "ccic3 phy",
	.reg_offset = PMUA_CSI_CCIC2_CLK_RES_CTRL,
	.mask_src_sel = BIT(31),
	.bits_src_sel = 0x0 << 31, //0 = 104 MHz, 1 = 52 MHz
	.mask_div_rat = 0x0,
	.bits_div_rat = 0x0, //
	.bits_enable = BIT(30),
	.bits_reset = BIT(29),
	.bits_fc_request = 0x0,
};

cam_clk_ctrl_t csi_ctl_func_clk = {
	.clk_name = "csi controller function",
	.reg_offset = PMUA_CSI_CCIC2_CLK_RES_CTRL,
	.mask_src_sel = BIT(18) | BIT(17) | BIT(16),
	.bits_src_sel =
		0x0
		<< 16, //0x0 = pll1_499 MHz, 0x1 = pll1_312, 0x2 = pll1_416, 0x3 = pll1_624, 0x4 = pll1_832, 0x5 = pll2_div2, 0x6 = pll2_div3, 0x7 = PLL1_1248
	.mask_div_rat = BIT(22) | BIT(21) | BIT(20),
	.bits_div_rat = 0x0
			<< 20, //csi_fnc_clk = CSI_FNC_CLK_DIV / (this field +1)
	.bits_enable = BIT(4),
	.bits_reset = BIT(1),
	.bits_fc_request = 0x0,
};

cam_clk_ctrl_t ccic_4x_ctl_func_clk = {
	.clk_name = "ccic 4x controller function",
	.reg_offset = PMUA_CCIC_CLK_RES_CTRL,
	.mask_src_sel = BIT(25) | BIT(24) | BIT(23),
	.bits_src_sel =
		0x0
		<< 23, //0x0 = pll1_499, 0x1 = pll1_312, 0x2 = pll1_416, 0x3 = pll1_624, 0x4 = pll1_832, 0x5= pll2_div2, 0x6 = pll2_div3, 0x7 = pll1_1248M
	.mask_div_rat = BIT(20) | BIT(19) | BIT(18),
	.bits_div_rat = 0x0
			<< 18, //ci_fnc_clk = CI_FNC_CLK_DIV / (this field +1)
	.bits_enable = BIT(4),
	.bits_reset = BIT(1),
	.bits_fc_request = 0x0,
};

static inline void reg_wr_mask(void __iomem *addr, uint32_t val, uint32_t mask)
{
	uint32_t v;

	v = ioread32(addr);
	v = (v & ~mask) | (val & mask);
	iowrite32(v, addr);
}

static inline void reg_set_bit(void __iomem *addr, uint32_t val)
{
	reg_wr_mask(addr, val, val);
}

static inline void reg_clr_bit(void __iomem *addr, uint32_t val)
{
	reg_wr_mask(addr, 0, val);
}

static int clock_sequence_on(cam_clk_ctrl_t *clk)
{
	// 1.Clock Select and Clock Divide Ratio
	if (clk->mask_src_sel | clk->mask_div_rat)
		reg_wr_mask(clk->reg_base + clk->reg_offset,
			    clk->bits_src_sel | clk->bits_div_rat,
			    clk->mask_src_sel | clk->mask_div_rat);

	// 2.clk enable
	reg_set_bit(clk->reg_base + clk->reg_offset, clk->bits_enable);

	// 3.clk unreset
	reg_set_bit(clk->reg_base + clk->reg_offset, clk->bits_reset);

	if (clk->bits_fc_request) {
		uint32_t read_data = 0;

		// 4.Clock FC Request
		reg_set_bit(clk->reg_base + clk->reg_offset,
			    clk->bits_fc_request);

		// 5.wait fc done
		read_data = ioread32(clk->reg_base + clk->reg_offset);
		while (read_data & clk->bits_fc_request) {
			read_data = ioread32(clk->reg_base + clk->reg_offset);
			pr_info("~");
		}
	}

	return 0;
}

static int clock_sequence_off(cam_clk_ctrl_t *clk)
{
	// 1. clk reset
	reg_clr_bit(clk->reg_base + clk->reg_offset, clk->bits_reset);

	// 2. clk diable
	reg_clr_bit(clk->reg_base + clk->reg_offset, clk->bits_enable);

	return 0;
}

static int cam_clock_enable(cam_clk_ctrl_t *clk)
{
	mutex_lock(&clk->mutex);
	if (clk->refcnt == 0) {
		clock_sequence_on(clk);
	}
	clk->refcnt++;
	mutex_unlock(&clk->mutex);

	pr_info("%s clock enabled\n", clk->clk_name);
	return 0;
}

static int cam_clock_disable(cam_clk_ctrl_t *clk)
{
	mutex_lock(&clk->mutex);
	if (clk->refcnt == 0) {
		mutex_unlock(&clk->mutex);
		return 0;
	}

	clk->refcnt--;
	if (clk->refcnt == 0) {
		clock_sequence_off(clk);
	}
	mutex_unlock(&clk->mutex);

	return 0;
}

static void __iomem *pmua_reg_base;
__maybe_unused static void apmu_regs_dump(void)
{
	pr_info("apmu: 0x%08x = 0x%08x\n",
		PMUA_REG_BASE + PMUA_CSI_CCIC2_CLK_RES_CTRL,
		ioread32(pmua_reg_base + PMUA_CSI_CCIC2_CLK_RES_CTRL));
	pr_info("apmu: 0x%08x = 0x%08x\n",
		PMUA_REG_BASE + PMUA_ISP_CLK_RES_CTRL,
		ioread32(pmua_reg_base + PMUA_ISP_CLK_RES_CTRL));
	pr_info("apmu: 0x%08x = 0x%08x\n",
		PMUA_REG_BASE + PMUA_LCD_CLK_RES_CTRL2,
		ioread32(pmua_reg_base + PMUA_LCD_CLK_RES_CTRL2));
	pr_info("apmu: 0x%08x = 0x%08x\n",
		PMUA_REG_BASE + PMUA_CCIC_CLK_RES_CTRL,
		ioread32(pmua_reg_base + PMUA_CCIC_CLK_RES_CTRL));
}

#define loop_wait(us) udelay(us)
static void isp_hardware_power_up(void)
{
	unsigned int rdata;

	loop_wait(10);
	// REG32(PMUA_REG_BASE + 0x38) |= 0x20000000;
	// rdata = ioread32(pmua_reg_base + 0x38) | 0x20000000;
	// iowrite32(rdata, pmua_reg_base + 0x38);
	// REG32(PMUA_PWR_CTRL_ISP) |= (0x0010);
	rdata = ioread32(pmua_reg_base + PMUA_PWR_CTRL_ISP) | 0x0010;
	iowrite32(rdata, pmua_reg_base + PMUA_PWR_CTRL_ISP);
	// REG32(PMUA_PWR_STBL_TIMER) = 0x0f100f;
	iowrite32(0x0f100f, pmua_reg_base + PMUA_PWR_STBL_TIMER);
	// REG32(PMUA_PWR_BLK_TMR_REG) = 0x10f3f0f;
	iowrite32(0x10f3f0f, pmua_reg_base + PMUA_PWR_BLK_TMR_REG);
	// REG32(PMUA_PWR_CTRL_ISP) |= 0x1;
	rdata = ioread32(pmua_reg_base + PMUA_PWR_CTRL_ISP) | 0x1;
	iowrite32(rdata, pmua_reg_base + PMUA_PWR_CTRL_ISP);

	loop_wait(20);
#ifndef CONFIG_SOC_SPACEMIT_K3_FPGA
	while ((ioread32(pmua_reg_base + PMUA_PWR_STATUS_REG) & 0x400) !=
	       0x400) {
		loop_wait(2);
	}
#endif /* timeout on fpga */

	return;
}

static void isp_hardware_power_down(void)
{
	/* TODO:  <21-02-24, yourname> */
	return;
}

int bare_cpp_power_on(void)
{
	unsigned int rdata;

	// isp_hardware_power_up();

	/*
     * Dove:
     * this clock is also used for camera ahb clk, camera can only enble this clk, never change the freq
     * Lark:
     * Not Required
     */
	// REG32(pmua_reg_base + 0x4c) |= 0x1;
	// rdata = ioread32(pmua_reg_base + 0x4c) | 0x1;
	// iowrite32(rdata, pmua_reg_base + 0x4c);
#if 0
	pr_info("====== cfg 0x38  ======\n");
	rdata = ioread32(pmua_reg_base + 0x38);
	pr_info("0 pmua_reg_base +0x38 =0x%x", rdata);

	rdata = ioread32(pmua_reg_base + 0x38);
	rdata = ioread32(pmua_reg_base + 0x38);
	pr_info("1 pmua_reg_base +0x38 =0x%x", rdata);

	// REG32(pmua_reg_base + 0x38) = 0x19A3718F;
	rdata = ioread32(pmua_reg_base + 0x38) | 0xC9A3F18F;
	iowrite32(rdata, pmua_reg_base + 0x38);

	rdata = ioread32(pmua_reg_base + 0x38);
	rdata = ioread32(pmua_reg_base + 0x38);

	pr_info("2 pmua_reg_base +0x38 =0x%x", rdata);
#else
	cam_clock_enable(&ahb_clk);
	cam_clock_enable(&isp_bus_clk);
	cam_clock_enable(&cpp_fnc_clk);
#endif

	rdata = ioread32(pmua_reg_base + PMUA_ISP_CLK_RES_CTRL);
	pr_info("pmua: 0x%x =0x%x\n", PMUA_REG_BASE + PMUA_ISP_CLK_RES_CTRL,
		rdata);

	return 0;
}

int bare_cpp_power_off(void)
{
	unsigned int rdata;

	pr_info("%s E\n", __func__);
#if 0
	iowrite32(0x0, pmua_reg_base + 0x38);
#else
	cam_clock_disable(&cpp_fnc_clk);
	cam_clock_disable(&isp_bus_clk);
	cam_clock_disable(&ahb_clk);
#endif
	// isp_hardware_power_down();
	rdata = ioread32(pmua_reg_base + PMUA_ISP_CLK_RES_CTRL);
	pr_info("pmua: 0x%x =0x%x\n", PMUA_REG_BASE + PMUA_ISP_CLK_RES_CTRL,
		rdata);

	return 0;
}

int bare_isp_power_on(void)
{
	return bare_cpp_power_on();
}

int bare_isp_power_off(void)
{
	return bare_cpp_power_off();
}

int bare_ccic_power_on(void)
{
	unsigned int rdata;

	isp_hardware_power_up();

	cam_clock_enable(&ahb_clk);
	cam_clock_enable(&isp_fnc_clk);
	cam_clock_enable(&ccic_4x_ctl_func_clk);
	cam_clock_enable(&csi_ctl_func_clk);
	cam_clock_enable(&ccic1_phy_clk);
	cam_clock_enable(&ccic2_phy_clk);
	cam_clock_enable(&ccic3_phy_clk);

	rdata = ioread32(pmua_reg_base + 0x38);
	pr_info("pmua_reg_base +0x38 =0x%x\n", rdata);

	return 0;
}

int bare_ccic_power_off(void)
{
	pr_info("%s E\n", __func__);

	cam_clock_disable(&ccic_4x_ctl_func_clk);
	cam_clock_disable(&csi_ctl_func_clk);
	cam_clock_disable(&ccic1_phy_clk);
	cam_clock_disable(&ccic2_phy_clk);
	cam_clock_disable(&ccic3_phy_clk);
	cam_clock_disable(&isp_fnc_clk);
	cam_clock_disable(&ahb_clk);
	isp_hardware_power_down();

	return 0;
}

int bare_sensor_mclk_enable(int clkId, unsigned long rate)
{
	unsigned int rdata;

	switch (clkId) {
	case 0:
	case 1:
	case 2:
	case 3:
		cam_clock_enable(&snr_mclk0_clk);
		cam_clock_enable(&snr_mclk1_clk);
		cam_clock_enable(&snr_mclk2_clk);
		cam_clock_enable(&snr_mclk3_clk);
		break;
	default:
		pr_err("%s: invalid clkId %d\n", __func__, clkId);
		return -1;
	}

	rdata = ioread32(pmua_reg_base + PMUA_CSI_CCIC2_CLK_RES_CTRL);
	pr_info("pmua: 0x%x =0x%x\n",
		PMUA_REG_BASE + PMUA_CSI_CCIC2_CLK_RES_CTRL, rdata);

	return 0;
}

int bare_sensor_mclk_disable(int clkId)
{
	unsigned int rdata;

	switch (clkId) {
	case 0:
	case 1:
	case 2:
	case 3:
		cam_clock_disable(&snr_mclk0_clk);
		cam_clock_disable(&snr_mclk1_clk);
		cam_clock_disable(&snr_mclk2_clk);
		cam_clock_disable(&snr_mclk3_clk);
		break;
	default:
		pr_err("%s: invalid clkId %d\n", __func__, clkId);
		return -1;
	}

	rdata = ioread32(pmua_reg_base + PMUA_CSI_CCIC2_CLK_RES_CTRL);
	pr_info("pmua: 0x%x =0x%x\n",
		PMUA_REG_BASE + PMUA_CSI_CCIC2_CLK_RES_CTRL, rdata);

	return 0;
}

static int __init pwrctrl_init(void)
{
	if (!pmua_reg_base) {
		pmua_reg_base = ioremap(PMUA_REG_BASE, 0x3ff);
	} else {
		return 0;
	}

	if (!pmua_reg_base) {
		pr_err("failed to map pmua reg base\n");
		return -1;
	}

	ahb_clk.reg_base = pmua_reg_base;
	mutex_init(&ahb_clk.mutex);
	isp_fnc_clk.reg_base = pmua_reg_base;
	mutex_init(&isp_fnc_clk.mutex);
	isp_bus_clk.reg_base = pmua_reg_base;
	mutex_init(&isp_bus_clk.mutex);
	cpp_fnc_clk.reg_base = pmua_reg_base;
	mutex_init(&cpp_fnc_clk.mutex);
	snr_mclk0_clk.reg_base = pmua_reg_base;
	mutex_init(&snr_mclk0_clk.mutex);
	snr_mclk1_clk.reg_base = pmua_reg_base;
	mutex_init(&snr_mclk1_clk.mutex);
	snr_mclk2_clk.reg_base = pmua_reg_base;
	mutex_init(&snr_mclk2_clk.mutex);
	snr_mclk3_clk.reg_base = pmua_reg_base;
	mutex_init(&snr_mclk3_clk.mutex);
	ccic1_phy_clk.reg_base = pmua_reg_base;
	mutex_init(&ccic1_phy_clk.mutex);
	ccic2_phy_clk.reg_base = pmua_reg_base;
	mutex_init(&ccic2_phy_clk.mutex);
	ccic3_phy_clk.reg_base = pmua_reg_base;
	mutex_init(&ccic3_phy_clk.mutex);
	csi_ctl_func_clk.reg_base = pmua_reg_base;
	mutex_init(&csi_ctl_func_clk.mutex);
	ccic_4x_ctl_func_clk.reg_base = pmua_reg_base;
	mutex_init(&ccic_4x_ctl_func_clk.mutex);

	return 0;
}

static void __exit pwrctrl_exit(void)
{
}

module_init(pwrctrl_init);
module_exit(pwrctrl_exit);
