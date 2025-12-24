// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2025 SpacemiT (Hangzhou) Technology Co. Ltd
 * Copyright (c) 2025 Yixun Lan <dlan@gentoo.org>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/init.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "sdhci.h"
#include "sdhci-pltfm.h"

#define SPACEMIT_SDHC_OP_EXT_REG	0x108
#define  SDHC_OVRRD_CLK_OEN		BIT(11)
#define  SDHC_FORCE_CLK_ON		BIT(12)

#define SPACEMIT_SDHC_MMC_CTRL_REG	0x114
#define  SDHC_MISC_INT_EN		BIT(1)
#define  SDHC_MISC_INT			BIT(2)
#define  SDHC_ENHANCE_STROBE_EN		BIT(8)
#define  SDHC_MMC_HS400			BIT(9)
#define  SDHC_MMC_HS200			BIT(10)
#define  SDHC_MMC_CARD_MODE		BIT(12)

#define SPACEMIT_SDHC_RX_CFG_REG	0x118
#define  SDHC_RX_SDCLK_SEL0		GENMASK(1, 0)
#define  SDHC_RX_SDCLK_SEL1		GENMASK(3, 2)

#define SPACEMIT_SDHC_TX_CFG_REG	0x11C
#define  SDHC_TX_INT_CLK_SEL		BIT(30)
#define  SDHC_TX_MUX_SEL		BIT(31)

#define SPACEMIT_SDHC_DLINE_CTRL_REG	0x130
#define  SDHC_DLINE_PU			BIT(0)
#define  SDHC_RX_DLINE_CODE		GENMASK(23, 16)
#define  SDHC_TX_DLINE_CODE		GENMASK(31, 24)

#define SPACEMIT_SDHC_DLINE_CFG_REG	0x134
#define  SDHC_RX_DLINE_REG		GENMASK(7, 0)
#define  SDHC_RX_DLINE_GAIN		BIT(8)
#define  SDHC_TX_DLINE_REG		GENMASK(23, 16)

#define SPACEMIT_SDHC_PHY_CTRL_REG	0x160
#define  SDHC_PHY_FUNC_EN		BIT(0)
#define  SDHC_PHY_PLL_LOCK		BIT(1)
#define  SDHC_HOST_LEGACY_MODE		BIT(31)

#define SPACEMIT_SDHC_PHY_FUNC_REG	0x164
#define  SDHC_PHY_TEST_EN		BIT(7)
#define  SDHC_HS200_USE_RFIFO		BIT(15)

#define SPACEMIT_SDHC_PHY_DLLCFG	0x168
#define  SDHC_DLL_PREDLY_NUM		GENMASK(3, 2)
#define  SDHC_DLL_FULLDLY_RANGE		GENMASK(5, 4)
#define  SDHC_DLL_VREG_CTRL		GENMASK(7, 6)
#define  SDHC_DLL_ENABLE		BIT(31)

#define SPACEMIT_SDHC_PHY_DLLCFG1	0x16C
#define  SDHC_DLL_REG1_CTRL		GENMASK(7, 0)
#define  SDHC_DLL_REG2_CTRL		GENMASK(15, 8)
#define  SDHC_DLL_REG3_CTRL		GENMASK(23, 16)
#define  SDHC_DLL_REG4_CTRL		GENMASK(31, 24)

#define SPACEMIT_SDHC_PHY_DLLSTS	0x170
#define  SDHC_DLL_LOCK_STATE		BIT(0)

#define SPACEMIT_SDHC_PHY_PADCFG_REG	0x178
#define  SDHC_PHY_DRIVE_SEL		GENMASK(2, 0)
#define  SDHC_RX_BIAS_CTRL		BIT(5)

#define SDHC_RX_TUNE_DELAY_MIN		0x0
#define SDHC_RX_TUNE_DELAY_MAX		0xFF
#define SDHC_RX_TUNE_DELAY_STEP		0x1

#define PHY_DRIVE_SEL_DEFAULT		0x4
#define RX_TUNING_WINDOW_THRESHOLD	50
#define RX_TUNING_DLINE_REG		0x00
#define TX_TUNING_DLINE_REG		0x00
#define TX_TUNING_DELAYCODE		0x7F

enum window_type {
	LEFT_WINDOW = 0,
	MIDDLE_WINDOW = 1,
	RIGHT_WINDOW = 2,
};

struct tuning_window {
	u8 min_delay;
	u8 max_delay;
};

struct rx_tuning {
	u8 tx_delaycode;
	u8 tx_dline_reg;
	u8 rx_dline_reg;
	u8 select_delay_num;
	struct tuning_window windows;
	u8 select_delay;

	u8 window_limit;
	u8 window_type;
};

struct spacemit_sdhci_host {
	struct clk *clk_core;
	struct clk *clk_io;
	struct clk *clk_aib_bus;
	struct clk *clk_aib;
	struct reset_control *reset;

	struct rx_tuning rxtuning;
	u8 phy_driver_sel;
};

static struct sdhci_host *sdio_host;

void spacemit_sdio_detect_change(int enable_scan);

/* All helper functions will update clr/set while preserve rest bits */
static inline void spacemit_sdhci_setbits(struct sdhci_host *host, u32 val, int reg)
{
	sdhci_writel(host, sdhci_readl(host, reg) | val, reg);
}

static inline void spacemit_sdhci_clrbits(struct sdhci_host *host, u32 val, int reg)
{
	sdhci_writel(host, sdhci_readl(host, reg) & ~val, reg);
}

static inline void spacemit_sdhci_clrsetbits(struct sdhci_host *host, u32 clr, u32 set, int reg)
{
	u32 val = sdhci_readl(host, reg);

	val = (val & ~clr) | set;
	sdhci_writel(host, val, reg);
}

void spacemit_sdio_detect_change(int enable_scan)
{
	if (!sdio_host)
		return;

	sdio_host->mmc->rescan_entered = 0;
	mmc_detect_change(sdio_host->mmc, 0);
}
EXPORT_SYMBOL(spacemit_sdio_detect_change);

static void spacemit_sdhci_reset(struct sdhci_host *host, u8 mask)
{
#if !defined(CONFIG_SOC_SPACEMIT_K1_FPGA) && !defined(CONFIG_SOC_SPACEMIT_K3_FPGA)
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(sdhci_priv(host));
#endif
	sdhci_reset(host, mask);

	if (mask != SDHCI_RESET_ALL)
		return;

	if (host->mmc->caps2 & MMC_CAP2_NO_MMC) {
		/* sd/sdio has no phy */
		spacemit_sdhci_setbits(host, SDHC_TX_INT_CLK_SEL, SPACEMIT_SDHC_TX_CFG_REG);
	} else {
#if !defined(CONFIG_SOC_SPACEMIT_K1_FPGA) && !defined(CONFIG_SOC_SPACEMIT_K3_FPGA)
		/* use phy func mode */
		spacemit_sdhci_setbits(host, SDHC_PHY_FUNC_EN | SDHC_PHY_PLL_LOCK,
				       SPACEMIT_SDHC_PHY_CTRL_REG);
		spacemit_sdhci_clrsetbits(host, SDHC_PHY_DRIVE_SEL,
					  SDHC_RX_BIAS_CTRL |
					  FIELD_PREP(SDHC_PHY_DRIVE_SEL, sdhst->phy_driver_sel),
					  SPACEMIT_SDHC_PHY_PADCFG_REG);
#else
		/* use phy bypass */
		spacemit_sdhci_setbits(host, SDHC_TX_INT_CLK_SEL, SPACEMIT_SDHC_TX_CFG_REG);
		spacemit_sdhci_setbits(host, SDHC_HOST_LEGACY_MODE, SPACEMIT_SDHC_PHY_CTRL_REG);
		spacemit_sdhci_setbits(host, SDHC_PHY_TEST_EN, SPACEMIT_SDHC_PHY_FUNC_REG);
#endif
		/* mmc card mode */
		spacemit_sdhci_setbits(host, SDHC_MMC_CARD_MODE, SPACEMIT_SDHC_MMC_CTRL_REG);
	}
}

static void spacemit_sdhci_set_uhs_signaling(struct sdhci_host *host, unsigned int timing)
{
	if (timing == MMC_TIMING_MMC_HS200)
		spacemit_sdhci_setbits(host, SDHC_MMC_HS200, SPACEMIT_SDHC_MMC_CTRL_REG);

	if (timing == MMC_TIMING_MMC_HS400)
		spacemit_sdhci_setbits(host, SDHC_MMC_HS400, SPACEMIT_SDHC_MMC_CTRL_REG);

	sdhci_set_uhs_signaling(host, timing);
}

static void spacemit_sdhci_set_clk_gate(struct sdhci_host *host, unsigned int auto_gate)
{
	if (auto_gate)
		spacemit_sdhci_clrbits(host, SDHC_OVRRD_CLK_OEN | SDHC_FORCE_CLK_ON,
				       SPACEMIT_SDHC_OP_EXT_REG);
	else
		spacemit_sdhci_setbits(host, SDHC_OVRRD_CLK_OEN | SDHC_FORCE_CLK_ON,
				       SPACEMIT_SDHC_OP_EXT_REG);
}

static int spacemit_sdhci_card_busy(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u32 present_state;
	u32 ret;

	/* Check whether DAT[0] is 0 */
	present_state = sdhci_readl(host, SDHCI_PRESENT_STATE);
	ret = !(present_state & SDHCI_DATA_0_LVL_MASK);

	if (host->mmc->caps2 & MMC_CAP2_NO_MMC) {
		if ((SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND)) == SD_SWITCH_VOLTAGE) &&
		    (host->mmc->ios.signal_voltage == MMC_SIGNAL_VOLTAGE_180))
			/* recover the auto clock */
			spacemit_sdhci_set_clk_gate(host, 1);
	}

	return ret;
}

static void spacemit_sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct mmc_host *mmc = host->mmc;

	if (mmc->ios.timing <= MMC_TIMING_UHS_SDR50)
		spacemit_sdhci_setbits(host, SDHC_TX_INT_CLK_SEL, SPACEMIT_SDHC_TX_CFG_REG);
	else
		spacemit_sdhci_clrbits(host, SDHC_TX_INT_CLK_SEL, SPACEMIT_SDHC_TX_CFG_REG);

	sdhci_set_clock(host, clock);

	if (host->mmc->caps2 & MMC_CAP2_NO_MMC) {
		/*
		 * according to the SD spec, during a signal voltage level switch,
		 * the clock must be closed for 5 ms.
		 * then, the host starts providing clk at 1.8 and the host checks whether
		 * DAT[3:0] is high after 1ms clk.
		 *
		 * for the above goal, temporarily disable the auto clk and keep clk
		 * always on for 1ms.
		 */
		if ((SDHCI_GET_CMD(sdhci_readw(host, SDHCI_COMMAND)) == SD_SWITCH_VOLTAGE) &&
		   (host->mmc->ios.signal_voltage == MMC_SIGNAL_VOLTAGE_180)) {
			/* disable auto clock */
			if (clock)
				/*
				 * some sdio device's signal level is already 1.8V before voltage
				 * switch, so we should avoid generating clock multiple times
				 * during switch sequence.
				 */
				spacemit_sdhci_set_clk_gate(host, 0);
		}
	}
};

static void spacemit_set_aib_mmc1_io(struct sdhci_host *host)
{
#define MMC1_IO_V18EN	BIT(2)
#define AKEY_ASFAR	0xBABA
#define AKEY_ASSAR	0xEB10
	void __iomem *aib_mmc1_io, *apbc_asfar, *apbc_assar;
	int vol = host->mmc->ios.signal_voltage;
	u32 reg;

	aib_mmc1_io = ioremap(0xD401E81C, 4);
	apbc_asfar = ioremap(0xD4015050, 4);
	apbc_assar = ioremap(0xD4015054, 4);

	writel(AKEY_ASFAR, apbc_asfar);
	writel(AKEY_ASSAR, apbc_assar);
	reg = readl(aib_mmc1_io);
	switch (vol) {
	case MMC_SIGNAL_VOLTAGE_180:
		reg |= MMC1_IO_V18EN;
		break;
	default:
		reg &= ~MMC1_IO_V18EN;
		break;
	}
	writel(AKEY_ASFAR, apbc_asfar);
	writel(AKEY_ASSAR, apbc_assar);
	writel(reg, aib_mmc1_io);

	iounmap(apbc_assar);
	iounmap(apbc_asfar);
	iounmap(aib_mmc1_io);
}

static void spacemit_sdhci_voltage_switch(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	/*
	 * v18en(MS) bit should meet TSMC's requirement
	 * when switch SOC SD IO voltage from 3.3v to 1.8v
	 */
	if (!(mmc->caps2 & MMC_CAP2_NO_SD))
		spacemit_set_aib_mmc1_io(host);
}

static void spacemit_sdhci_phy_dll_init(struct sdhci_host *host)
{
	u32 state;
	int ret;

	spacemit_sdhci_clrsetbits(host, SDHC_DLL_PREDLY_NUM |
				  SDHC_DLL_FULLDLY_RANGE |
				  SDHC_DLL_VREG_CTRL,
				  FIELD_PREP(SDHC_DLL_PREDLY_NUM, 1) |
				  FIELD_PREP(SDHC_DLL_FULLDLY_RANGE, 1) |
				  FIELD_PREP(SDHC_DLL_VREG_CTRL, 1),
				  SPACEMIT_SDHC_PHY_DLLCFG);

	spacemit_sdhci_clrsetbits(host, SDHC_DLL_REG1_CTRL,
				  FIELD_PREP(SDHC_DLL_REG1_CTRL, 0x92),
				  SPACEMIT_SDHC_PHY_DLLCFG1);

	spacemit_sdhci_setbits(host, SDHC_DLL_ENABLE, SPACEMIT_SDHC_PHY_DLLCFG);

	ret = readl_poll_timeout(host->ioaddr + SPACEMIT_SDHC_PHY_DLLSTS, state,
				 state & SDHC_DLL_LOCK_STATE, 2, 100);
	if (ret == -ETIMEDOUT)
		dev_warn(mmc_dev(host->mmc), "fail to lock phy dll in 100us!\n");
}

static void spacemit_sdhci_hs400_enhanced_strobe(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);

	if (!ios->enhanced_strobe) {
		spacemit_sdhci_clrbits(host, SDHC_ENHANCE_STROBE_EN, SPACEMIT_SDHC_MMC_CTRL_REG);
		return;
	}

	spacemit_sdhci_setbits(host, SDHC_ENHANCE_STROBE_EN, SPACEMIT_SDHC_MMC_CTRL_REG);
	spacemit_sdhci_phy_dll_init(host);
}

static unsigned int spacemit_sdhci_clk_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	if (pltfm_host->clk)
		return clk_get_rate(pltfm_host->clk);
	else
		return pltfm_host->clock;
}

static int spacemit_sdhci_pre_select_hs400(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	spacemit_sdhci_setbits(host, SDHC_MMC_HS400, SPACEMIT_SDHC_MMC_CTRL_REG);

	return 0;
}

static void spacemit_sdhci_post_select_hs400(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	spacemit_sdhci_phy_dll_init(host);
}

static void spacemit_sdhci_pre_hs400_to_hs200(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	spacemit_sdhci_clrbits(host, SDHC_PHY_FUNC_EN | SDHC_PHY_PLL_LOCK,
			       SPACEMIT_SDHC_PHY_CTRL_REG);
	spacemit_sdhci_clrbits(host, SDHC_MMC_HS400 | SDHC_MMC_HS200 | SDHC_ENHANCE_STROBE_EN,
			       SPACEMIT_SDHC_MMC_CTRL_REG);
	spacemit_sdhci_clrbits(host, SDHC_HS200_USE_RFIFO, SPACEMIT_SDHC_PHY_FUNC_REG);

	udelay(5);

	spacemit_sdhci_setbits(host, SDHC_PHY_FUNC_EN | SDHC_PHY_PLL_LOCK,
			       SPACEMIT_SDHC_PHY_CTRL_REG);
}

static inline int spacemit_sdhci_get_clocks(struct device *dev,
					    struct sdhci_pltfm_host *pltfm_host)
{
#if !defined(CONFIG_SOC_SPACEMIT_K1_FPGA) && !defined(CONFIG_SOC_SPACEMIT_K3_FPGA)
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(pltfm_host);
	int ret;

	sdhst->clk_core = devm_clk_get_enabled(dev, "core");
	if (IS_ERR(sdhst->clk_core))
		return PTR_ERR(sdhst->clk_core);

	sdhst->clk_io = devm_clk_get_enabled(dev, "io");
	if (IS_ERR(sdhst->clk_io))
		return PTR_ERR(sdhst->clk_io);

	pltfm_host->clk = sdhst->clk_io;
	if (pltfm_host->clock) {
		ret = clk_set_rate(sdhst->clk_io, pltfm_host->clock);
		if (ret) {
			dev_err(dev, "failed to set io clock rate\n");
			return ret;
		}
	}

	if (!(host->mmc->caps2 & MMC_CAP2_NO_SD)) {
		sdhst->clk_aib_bus = devm_clk_get_enabled(dev, "aib-bus");
		if (IS_ERR(sdhst->clk_aib_bus))
			return PTR_ERR(sdhst->clk_aib_bus);

		sdhst->clk_aib = devm_clk_get_enabled(dev, "aib");
		if (IS_ERR(sdhst->clk_aib))
			return PTR_ERR(sdhst->clk_aib);
	}
#endif
	return 0;
}

static void spacemit_sw_rx_tuning_prepare(struct sdhci_host *host, u8 dline_reg)
{
	struct mmc_host *mmc = host->mmc;
	u32 reg;

	reg = sdhci_readl(host, SPACEMIT_SDHC_DLINE_CFG_REG);
	if ((mmc->ios.timing == MMC_TIMING_UHS_SDR50) && (reg & 0x40))
		spacemit_sdhci_clrsetbits(host, SDHC_RX_DLINE_REG |
					SDHC_RX_DLINE_GAIN,
					FIELD_PREP(SDHC_RX_DLINE_REG, dline_reg) |
					FIELD_PREP(SDHC_RX_DLINE_GAIN, 1),
					SPACEMIT_SDHC_DLINE_CFG_REG);
	else
		spacemit_sdhci_clrsetbits(host, SDHC_RX_DLINE_REG |
					SDHC_RX_DLINE_GAIN,
					FIELD_PREP(SDHC_RX_DLINE_REG, dline_reg),
					SPACEMIT_SDHC_DLINE_CFG_REG);

	spacemit_sdhci_setbits(host, SDHC_DLINE_PU, SPACEMIT_SDHC_DLINE_CTRL_REG);
	udelay(5);
	spacemit_sdhci_clrsetbits(host, SDHC_RX_SDCLK_SEL1,
				  FIELD_PREP(SDHC_RX_SDCLK_SEL1, 1),
				  SPACEMIT_SDHC_RX_CFG_REG);

	if (mmc->ios.timing == MMC_TIMING_MMC_HS200)
		spacemit_sdhci_setbits(host, SDHC_HS200_USE_RFIFO, SPACEMIT_SDHC_PHY_FUNC_REG);
}

static void spacemit_sw_rx_set_delaycode(struct sdhci_host *host, u32 delay)
{
	spacemit_sdhci_clrsetbits(host, SDHC_RX_DLINE_CODE,
				  FIELD_PREP(SDHC_RX_DLINE_CODE, delay),
				  SPACEMIT_SDHC_DLINE_CTRL_REG);
}

static void spacemit_sw_tx_tuning_prepare(struct sdhci_host *host)
{
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(sdhci_priv(host));
	struct rx_tuning *rxtuning = &sdhst->rxtuning;

	/* set TX_DLINE_REG */
	spacemit_sdhci_clrsetbits(host, SDHC_RX_DLINE_GAIN,
				  FIELD_PREP(SDHC_TX_DLINE_REG, rxtuning->tx_dline_reg),
				  SPACEMIT_SDHC_DLINE_CFG_REG);
	/* set TX_DLINE_CODE */
	spacemit_sdhci_clrsetbits(host, SDHC_TX_DLINE_CODE,
				  FIELD_PREP(SDHC_TX_DLINE_CODE, rxtuning->tx_delaycode),
				  SPACEMIT_SDHC_DLINE_CTRL_REG);
	/* set TX_MUX_SEL */
	spacemit_sdhci_setbits(host, SDHC_TX_MUX_SEL, SPACEMIT_SDHC_TX_CFG_REG);
	spacemit_sdhci_setbits(host, SDHC_DLINE_PU, SPACEMIT_SDHC_DLINE_CTRL_REG);
}

static int spacemit_sw_rx_select_window(struct sdhci_host *host, u32 opcode)
{
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(sdhci_priv(host));
	struct mmc_host *mmc = host->mmc;
	struct rx_tuning *rxtuning = &sdhst->rxtuning;
	struct tuning_window *window = &rxtuning->windows;
	int min, max, start, ret;
	int cur_windows = 0;
	int max_windows = 0;

	start = SDHC_RX_TUNE_DELAY_MIN;
	while (start <= SDHC_RX_TUNE_DELAY_MAX) {
		if (!mmc->ops->get_cd(mmc))
			return -ENODEV;

		spacemit_sw_rx_set_delaycode(host, start);
		ret = mmc_send_tuning(mmc, opcode, NULL);
		if (ret) {
			if (cur_windows)
				dev_info(mmc_dev(mmc), "%s: pass window [%d %d)\n",
						mmc_hostname(host->mmc), min, start);
			cur_windows = 0;
		} else {
			if (!cur_windows)
				min = start;

			cur_windows++;
			if (cur_windows > max_windows) {
				max_windows = cur_windows;
				max = start;
			}
			if (start == SDHC_RX_TUNE_DELAY_MAX)
				dev_info(mmc_dev(mmc), "%s: pass window [%d %d]\n",
						mmc_hostname(host->mmc), min, start);
		}
		start += SDHC_RX_TUNE_DELAY_STEP;
	}

	if (max_windows < rxtuning->window_limit) {
		dev_warn(mmc_dev(mmc), "%s: fail to find valid tuning window, max_windows:%d\n",
			 mmc_hostname(mmc), max_windows);
		return -EIO;
	}

	window->min_delay = max - max_windows + 1;
	window->max_delay = max;

	if (rxtuning->window_type == LEFT_WINDOW)
		rxtuning->select_delay = window->min_delay + max_windows/3;
	else if (rxtuning->window_type == RIGHT_WINDOW)
		rxtuning->select_delay = window->min_delay + max_windows*2/3;
	else
		rxtuning->select_delay = window->min_delay + max_windows/2;

	return 0;
}

static int spacemit_sdhci_execute_sw_tuning(struct sdhci_host *host, u32 opcode)
{
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(sdhci_priv(host));
	struct mmc_host *mmc = host->mmc;
	struct rx_tuning *rxtuning = &sdhst->rxtuning;
	int ret;
	/*
	 * Tuning is required for SDR50/SDR104, HS200/HS400 cards and
	 * if clock frequency is greater than 100MHz in these modes.
	 */
	if (host->clock < 100 * 1000 * 1000 ||
	    !(mmc->ios.timing == MMC_TIMING_MMC_HS200 ||
	    (mmc->ios.timing == MMC_TIMING_UHS_SDR50) ||
	    mmc->ios.timing == MMC_TIMING_UHS_SDR104))
		return 0;

	/* step 1: set tx tuning config */
	if (host->mmc->caps2 & MMC_CAP2_NO_MMC) {
		dev_info(mmc_dev(mmc), "%s: set tx_delaycode: %d\n",
			 mmc_hostname(mmc), rxtuning->tx_delaycode);
		spacemit_sw_tx_tuning_prepare(host);
	}

	/* step 2: get pass window and calculate the select_delay */
	spacemit_sw_rx_tuning_prepare(host, rxtuning->rx_dline_reg);
	ret = spacemit_sw_rx_select_window(host, opcode);
	if (ret) {
		dev_warn(mmc_dev(mmc), "%s: tuning fail, err:%d\n", mmc_hostname(mmc), ret);
		return ret;
	}

	/* step 3: set rx delay code */
	spacemit_sw_rx_set_delaycode(host, rxtuning->select_delay);
	dev_info(mmc_dev(mmc), "%s: tuning done, use delay_code:%d\n",
		 mmc_hostname(mmc), rxtuning->select_delay);

	return ret;
}

static const struct sdhci_ops spacemit_sdhci_ops = {
	.get_max_clock		= spacemit_sdhci_clk_get_max_clock,
	.reset			= spacemit_sdhci_reset,
	.set_bus_width		= sdhci_set_bus_width,
	.set_clock		= spacemit_sdhci_set_clock,
	.set_uhs_signaling	= spacemit_sdhci_set_uhs_signaling,
	.voltage_switch		= spacemit_sdhci_voltage_switch,
	.set_power		= sdhci_set_power_and_bus_voltage,
	.platform_execute_tuning = spacemit_sdhci_execute_sw_tuning,
};

static const struct sdhci_pltfm_data spacemit_sdhci_k1_pdata = {
	.ops = &spacemit_sdhci_ops,
	.quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		  SDHCI_QUIRK_32BIT_ADMA_SIZE |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		  SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL,
	.quirks2 = SDHCI_QUIRK2_BROKEN_64_BIT_DMA |
		   SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct sdhci_pltfm_data spacemit_sdhci_k3_pdata = {
	.ops = &spacemit_sdhci_ops,
	.quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		  SDHCI_QUIRK_32BIT_ADMA_SIZE |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		  SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct of_device_id spacemit_sdhci_of_match[] = {
	{
		.compatible = "spacemit,k1-sdhci",
		.data = &spacemit_sdhci_k1_pdata,
	},
	{
		.compatible = "spacemit,k3-sdhci",
		.data = &spacemit_sdhci_k3_pdata,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_sdhci_of_match);

static void spacemit_sdhci_get_of_property(struct platform_device *pdev, struct sdhci_host *host)
{
	struct device_node *node = pdev->dev.of_node;
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(sdhci_priv(host));
	u32 property;

	sdhst->rxtuning.rx_dline_reg = RX_TUNING_DLINE_REG;
	sdhst->rxtuning.tx_dline_reg = TX_TUNING_DLINE_REG;

	/* read rx tuning window limit */
	if (!of_property_read_u32(node, "spacemit,rx_tuning_limit", &property))
		sdhst->rxtuning.window_limit = (u8)property;
	else
		sdhst->rxtuning.window_limit = RX_TUNING_WINDOW_THRESHOLD;

	/* read rx tuning window type */
	if (!of_property_read_u32(node, "spacemit,rx_tuning_type", &property))
		sdhst->rxtuning.window_type = (u8)property;
	else
		sdhst->rxtuning.window_type = MIDDLE_WINDOW;

	/* read tx delaycode */
	if (!of_property_read_u32(node, "spacemit,tx_delaycode", &property))
		sdhst->rxtuning.tx_delaycode = (u8)property;
	else
		sdhst->rxtuning.tx_delaycode = TX_TUNING_DELAYCODE;

	/* phy driver select */
	if (!of_property_read_u32(node, "spacemit,phy_driver_sel", &property))
		sdhst->phy_driver_sel = (u8)property;
	else
		sdhst->phy_driver_sel = PHY_DRIVE_SEL_DEFAULT;
}

static ssize_t spacemit_tx_delaycode_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(sdhci_priv(host));

	return sprintf(buf, "0x%02x\n", sdhst->rxtuning.tx_delaycode);
}

static ssize_t spacemit_tx_delaycode_set(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(sdhci_priv(host));
	u8 delaycode;

	if (kstrtou8(buf, 0, &delaycode))
		return -EINVAL;

	sdhst->rxtuning.tx_delaycode = delaycode;
	return count;
}

static struct device_attribute spacemit_sysfs_files[] = {
	__ATTR(tx_delaycode, 0644, spacemit_tx_delaycode_show, spacemit_tx_delaycode_set),
};

static int spacemit_sdhci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spacemit_sdhci_host *sdhst;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_host *host;
	const struct sdhci_pltfm_data *data;
	struct mmc_host_ops *mops;
	int i, ret;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(dev, "no sdhci spacemit data\n");
		return -EINVAL;
	}

	host = sdhci_pltfm_init(pdev, data, sizeof(*sdhst));
	if (IS_ERR(host))
		return PTR_ERR(host);

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err_pltfm;

	sdhci_get_of_property(pdev);
	spacemit_sdhci_get_of_property(pdev, host);

	mops = &host->mmc_host_ops;
	if (!(host->mmc->caps2 & MMC_CAP2_NO_MMC)) {
		mops->hs400_prepare_ddr	= spacemit_sdhci_pre_select_hs400;
		mops->hs400_complete	= spacemit_sdhci_post_select_hs400;
		mops->hs400_downgrade	= spacemit_sdhci_pre_hs400_to_hs200;
		mops->hs400_enhanced_strobe = spacemit_sdhci_hs400_enhanced_strobe;
	}
	mops->card_busy = spacemit_sdhci_card_busy;

	if (!(host->mmc->caps2 & MMC_CAP2_NO_SDIO))
		/* skip auto rescan */
		host->mmc->rescan_entered = 1;

	host->mmc->caps |= MMC_CAP_NEED_RSP_BUSY;

	pltfm_host = sdhci_priv(host);
	ret = spacemit_sdhci_get_clocks(dev, pltfm_host);
	if (ret)
		goto err_pltfm;

	sdhst = sdhci_pltfm_priv(pltfm_host);
	sdhst->reset = devm_reset_control_array_get_optional_shared(dev);
	if (IS_ERR(sdhst->reset)) {
		dev_err(dev, "failed to get reset control\n");
		ret = PTR_ERR(sdhst->reset);
		goto err_pltfm;
	}

	ret = reset_control_deassert(sdhst->reset);
	if (ret)
		goto err_pltfm;

	ret = sdhci_add_host(host);
	if (ret)
		goto err_add_host;

	if (!(host->mmc->caps2 & MMC_CAP2_NO_SDIO)) {
		dev_notice(dev, "sdio host: 0x%p\n", host);
		sdio_host = host;
	}

	if (host->mmc->caps2 & MMC_CAP2_NO_MMC) {
		for (i = 0; i < ARRAY_SIZE(spacemit_sysfs_files); i++)
			device_create_file(dev, &spacemit_sysfs_files[i]);
	}

	return 0;

err_add_host:
	reset_control_assert(sdhst->reset);
err_pltfm:
	sdhci_pltfm_free(pdev);
	return ret;
}

static void spacemit_sdhci_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(sdhci_priv(host));
	int i;

	if (host->mmc->caps2 & MMC_CAP2_NO_MMC) {
		for (i = 0; i < ARRAY_SIZE(spacemit_sysfs_files); i++)
			device_remove_file(&pdev->dev, &spacemit_sysfs_files[i]);
	}

	sdhci_remove_host(host, 1);
	reset_control_assert(sdhst->reset);
	sdhci_pltfm_free(pdev);
}

static struct platform_driver spacemit_sdhci_driver = {
	.driver		= {
		.name	= "sdhci-spacemit",
		.of_match_table = spacemit_sdhci_of_match,
	},
	.probe		= spacemit_sdhci_probe,
	.remove_new	= spacemit_sdhci_remove,
};
module_platform_driver(spacemit_sdhci_driver);

MODULE_DESCRIPTION("SpacemiT SDHCI platform driver");
MODULE_LICENSE("GPL");
