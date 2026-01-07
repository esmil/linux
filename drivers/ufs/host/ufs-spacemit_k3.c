// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit k3 ufs controller driver
 *
 * Copyright (c) 2025, spacemit Corporation.
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <scsi/scsi_eh.h>

#include <ufs/ufshcd.h>
#include <ufs/ufshci.h>
#include <ufs/ufs_quirks.h>
#include <ufs/unipro.h>

#include "ufshcd-pltfrm.h"
#include "ufs-spacemit_k3.h"

/* PA Layer Gettable and settable M-PHY Specific Attributes */
#define PA_TXHSG1SYNCLENGTH 0x1552
#define PA_TXHSG1PREPARELENGTH 0x1553
#define PA_TXHSG2SYNCLENGTH 0x1554
#define PA_TXHSG2PREPARELENGTH 0x1555
#define PA_TXHSG3SYNCLENGTH 0x1556
#define PA_TXHSG3PREPARELENGTH 0x1557
#define PA_TXMK2EXTENSION 0x155A
#define PA_PEERSCRAMBLING 0x155B
#define PA_TXSKIP 0x155C
#define PA_TXSKIPPERIOD 0x155D
#define PA_PEER_TX_LCC_ENABLE 0x155F

#define PA_SCRAMBLING 0x1585
#define PA_MK2EXTENSIONGUARDBAND 0x15AB

/*special TX/RX Configuration Attributes*/
#define RX_LS_PRE_LEN_CAP 0x008D
#define RX_LANE_HB8_BKDOOR_ATTR 0x00F4
#define RX_PWRM_CLOSURE_LEN_CAP 0x008E
#define RX_MIN_STALL_CAP 0x0088
#define RX_LANE_SOF_BKDOOR_ATT 0x00F2
#define RX_GARBAGE_COUNT_OFFSET 0x00F2

/*special analog reg*/
#define ANA_EQ_CTRL_REG_ATTR 0x00CD
#define ANA_HSGEAR_CTRL_ATTR 0x00C1

/* the delay between TX bursts */
#define VS_TX_BURST_CLOSURE_DELAY 0xD084

/* host registers for lark */
static int spacemit_k3_regs[] = {
	(UFS_PHY_MNG_BASE + UFS_MPHY_RST_CTRL),
	(UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL),
	(UFS_PHY_MNG_BASE + UFS_DEVICE_IO_CTRL),
	0xFFF,
};

#define UFS_SPACEMIT_K3_ACLK_NAME "ufs-aclk"

/*
 * PMU/clock registers used to configure UFS ACLK source/divider and trigger
 * FC (frequency change) handshake.
 *
 * Kept consistent with the working ufs-asr driver + patch in this workspace.
 */
#define SPACEMIT_K3_UFS_PMUAP_REG	(0xd4282800 + 0x268)
#define SPACEMIT_K3_ACGR_REG_BASE	(0xd4050000 + 0x1024)

#define UFS_PMUAP_ACLK_FC_REQ		BIT(8)
#define UFS_ACLK_FC_TIMEOUT		10000

/* PHY register magic values */
#define MPHY_PU_ALL 0x87f
#define MPHY_PU_WITH_HB8_RESET 0xb7f
#define MPHY_DEVICE_RESET_DEASSERT 0x101
#define MPHY_DEVICE_RESET_ASSERT 0x001
#define MPHY_PLL_LOCK_BIT BIT(31)
#define MPHY_PLL_LOCK_TIMEOUT_US 10000

/* FSM states */
#define FSM_STATE_HIBERN8 0x1
#define FSM_STATE_ACTIVE 0x3
#define FSM_STATE_LS_BURST 0x5

struct ufs_reg_snapshot {
	u32 reg_utrlba;
	u32 reg_utrlbau;
	u32 reg_utrmlba;
	u32 reg_utrmlbau;

	u32 reg_sys1clk;
	u32 reg_tx_symbol_clk;
	u32 reg_retry_timer;
	u32 reg_pa_link;
	u32 reg_cfg1;
};

#define VENDOR_DUMP_BUF_SIZE 2048
#define VENDOR_MAX_OFFSET 0xE0

static void ufs_spacemit_k3_dump_host_regs(struct ufs_hba *hba)
{
	u8 *buf;
	u32 val;
	int offset;
	int i;
	size_t len;
	int *host_reg = spacemit_k3_regs;

	/* Use dynamic allocation to avoid large stack frame */
	buf = kzalloc(VENDOR_DUMP_BUF_SIZE, GFP_KERNEL);
	if (!buf) {
		dev_err(hba->dev, "Failed to allocate dump buffer\n");
		return;
	}

	len = 0;
	len += scnprintf(buf + len, VENDOR_DUMP_BUF_SIZE - len, "vendor specific registers:");

	for (offset = 0xC0, i = 0; offset < VENDOR_MAX_OFFSET; offset += 4, i++) {
		val = ufshcd_readl(hba, offset);
		if (i % 4 == 0)
			len += scnprintf(buf + len, VENDOR_DUMP_BUF_SIZE - len, "\n");
		len += scnprintf(buf + len, VENDOR_DUMP_BUF_SIZE - len, "    0x%03x: 0x%08x    ",
				 offset, val);
	}

	len += scnprintf(buf + len, VENDOR_DUMP_BUF_SIZE - len, "\n    mphy and atop registers:");

	for (i = 0; host_reg[i] != 0xFFF; i++) {
		val = ufshcd_readl(hba, host_reg[i]);
		if (i % 4 == 0)
			len += scnprintf(buf + len, VENDOR_DUMP_BUF_SIZE - len, "\n");
		len += scnprintf(buf + len, VENDOR_DUMP_BUF_SIZE - len, "    0x%03x: 0x%08x    ",
				 host_reg[i], val);
	}
	len += scnprintf(buf + len, VENDOR_DUMP_BUF_SIZE - len, "\n");

	dev_warn(hba->dev, "%s", buf);

	kfree(buf);
}

/* M-PHY FSM states */
#define MPHY_RX_FSM_STATE 0xC1
#define MPHY_TX_FSM_STATE 0x41

static bool is_fsm_state_valid(u32 state)
{
	return (state == FSM_STATE_ACTIVE || state == FSM_STATE_LS_BURST);
}

static void ufs_spacemit_k3_dump_fsm_state(struct ufs_hba *hba)
{
	u32 tx0_fsm_val, tx1_fsm_val, rx0_fsm_val, rx1_fsm_val;
	int err;

	err = ufshcd_dme_get(hba,
			     UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
			     &tx0_fsm_val);
	if (err)
		return;
	usleep_range(100, 200);

	err = ufshcd_dme_get(hba,
			     UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(1)),
			     &tx1_fsm_val);
	if (err)
		return;
	usleep_range(100, 200);

	err = ufshcd_dme_get(hba,
			     UIC_ARG_MIB_SEL(MPHY_RX_FSM_STATE, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)),
			     &rx0_fsm_val);
	if (err)
		return;
	usleep_range(100, 200);

	err = ufshcd_dme_get(hba,
			     UIC_ARG_MIB_SEL(MPHY_RX_FSM_STATE, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)),
			     &rx1_fsm_val);
	if (err)
		return;
	usleep_range(100, 200);

	if (!is_fsm_state_valid(tx0_fsm_val) || !is_fsm_state_valid(tx1_fsm_val) ||
	    !is_fsm_state_valid(rx0_fsm_val) || !is_fsm_state_valid(rx1_fsm_val)) {
		dev_warn(hba->dev, "FSM state invalid - TX:[0x%x, 0x%x], RX:[0x%x, 0x%x]\n",
			 tx0_fsm_val, tx1_fsm_val, rx0_fsm_val, rx1_fsm_val);
	}
}

static int ufs_spacemit_k3_get_connected_tx_lanes(struct ufs_hba *hba, u32 *tx_lanes)
{
	int err = 0;

	err = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDTXDATALANES), tx_lanes);
	if (err)
		dev_err(hba->dev, "%s: couldn't read PA_CONNECTEDTXDATALANES %d\n", __func__, err);

	return err;
}

static int ufs_spacemit_k3_trigger_aclk_fc(struct device *dev)
{
	void __iomem *ufs_pmuap_reg;
	u32 reg_val;
	u32 timeout;

	ufs_pmuap_reg = ioremap((phys_addr_t)SPACEMIT_K3_UFS_PMUAP_REG, 4);
	if (!ufs_pmuap_reg) {
		dev_err(dev, "Failed to map UFS PMUAP reg\n");
		return -ENOMEM;
	}

	reg_val = readl(ufs_pmuap_reg);
	reg_val |= UFS_PMUAP_ACLK_FC_REQ;
	writel(reg_val, ufs_pmuap_reg);

	timeout = UFS_ACLK_FC_TIMEOUT;
	while (timeout) {
		reg_val = readl(ufs_pmuap_reg);
		if (!(reg_val & UFS_PMUAP_ACLK_FC_REQ))
			break;
		timeout--;
		udelay(10);
	}

	if (reg_val & UFS_PMUAP_ACLK_FC_REQ)
		dev_err(dev, "ACLK FC request failed (PMUAP=0x%x)\n", reg_val);

	iounmap(ufs_pmuap_reg);
	return 0;
}

/**
 * ufs_spacemit_k3_mphy_init
 * @hba: host controller instance
 */
static int ufs_spacemit_k3_mphy_init(struct ufs_hba *hba)
{
	u32 reg_val;
	int timeout;

	/* reset all mphy logical */
	ufshcd_writel(hba, 0x003, UFS_PHY_MNG_BASE + 0x0);
	mdelay(1);

	/* power up all */
	ufshcd_writel(hba, MPHY_PU_ALL, UFS_PHY_MNG_BASE + 0x4);
	mdelay(1);

	/* asserted ana_rx_hb8_reset */
	ufshcd_writel(hba, 0xb7f, UFS_PHY_MNG_BASE + 0x4);
	mdelay(1);

	/* deasserted ana_rx_hb8_reset */
	ufshcd_writel(hba, MPHY_PU_ALL, UFS_PHY_MNG_BASE + 0x4);
	mdelay(1);

	/* deasserted ufs device reset & refer clk output enable */
	ufshcd_writel(hba, 0x101, UFS_PHY_MNG_BASE + 0xC);
	mdelay(1);

	/* note: refer clk 26MHz */

	/* wait PLL_lock here, bit31 at 0x0104 */
	timeout = MPHY_PLL_LOCK_TIMEOUT_US;
	while (timeout > 0) {
		reg_val = ufshcd_readl(hba, UFS_PHY_MNG_BASE + 0x4);
		if (reg_val & MPHY_PLL_LOCK_BIT)
			break;
		udelay(1);
		timeout--;
	}

	if (timeout <= 0) {
		dev_err(hba->dev, "M-PHY PLL lock timeout in mphy_init\n");
		return -ETIMEDOUT;
	}

	dev_info(hba->dev, "M-PHY PLL locked successfully\n");

	/*
	 * tx_gear switch
	 *
	 * TODO: Check if udelay(20) can be reduced or replaced with status
	 * polling. Need to verify if MPHY_BKDR_CTRL or ATOP 0xC2 registers
	 * have ready/done bits. The mdelay(5) may also be optimized.
	 */
	ufshcd_writel(hba, 0x1, UFS_PHY_MNG_BASE + 0x08);
	udelay(20);

	ufshcd_writel(hba, 0x40, UFS_ATOP_BASE + (0xC2 << 2));
	udelay(20);

	ufshcd_writel(hba, 0x0, UFS_PHY_MNG_BASE + 0x08);
	udelay(20);

	/* Extra settle time after MPHY tuning */
	mdelay(5);

	dev_dbg(hba->dev, "M-PHY init completed\n");

	return 0;
}

static int ufs_spacemit_k3_uniprov1p6_init(struct ufs_hba *hba)
{
	int err = 0;

	/* PA_TXHSG1SYNCLENGTH */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x1552), 0x4f);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXHSG1SYNCLENGTH error \n");
	}
	/* PA_TXHSG1PREPARELENGTH */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x1553), 0xf);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXHSG1PREPARELENGTH error \n");
	}

	/* PA_TXHSG2SYNCLENGTH */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x1554), 0x4f);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXHSG2SYNCLENGTH error \n");
	}
	/* PA_TXHSG2PREPARELENGTH */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x1555), 0xf);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXHSG2PREPARELENGTH error \n");
	}

	/* PA_TXHSG3SYNCLENGTH */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x1556), 0x4f);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXHSG3SYNCLENGTH error \n");
	}
	/* PA_TXHSG3PREPARELENGTH */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x1557), 0xf);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXHSG3PREPARELENGTH error \n");
	}

	/* PA_TXMK2EXTENSION */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x155A), 0x0);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXMK2EXTENSION error \n");
	}
	/* PA_PEERSCRAMBLING */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x155B), 0x1);
	if (err) {
		dev_err(hba->dev, "Writing PA_PEERSCRAMBLING error \n");
	}
	/* PA_TXSKIP */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x155C), 0x1);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXSKIP error \n");
	}
	/* PA_TXSKIPPERIOD */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x155D), 250);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXSKIPPERIOD error \n");
	}

	/* PA_LOCAL_TX_LCC_ENABLE */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x155E), 0x0);
	if (err) {
		dev_err(hba->dev, "Writing PA_LOCAL_TX_LCC_ENABLE error \n");
	}
	/* PA_PEER_TX_LCC_ENABLE */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x155F), 0x0);
	if (err) {
		dev_err(hba->dev, "Writing PA_PEER_TX_LCC_ENABLE error \n");
	}

	/* PA_SCRAMBLING */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x1585), 0x1);
	if (err) {
		dev_err(hba->dev, "Writing PA_SCRAMBLING error \n");
	}
	/* PA_GRANULARITY */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x15AA), 0x1);
	if (err) {
		dev_err(hba->dev, "Writing PA_GRANULARITY error \n");
	}

	/* PA_MK2EXTENSIONGUARDBAND */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x15AB), 0x0);
	if (err) {
		dev_err(hba->dev, "Writing PA_MK2EXTENSIONGUARDBAND error \n");
	}

	/* PA_STALLNOCONFIGTIME */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x15A3), 15);
	if (err) {
		dev_err(hba->dev, "Writing PA_STALLNOCONFIGTIME error \n");
	}

	/* PA_TACTIVATE */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x15A8), 0x64);
	if (err) {
		dev_err(hba->dev, "Writing PA_TACTIVATE error \n");
	}
	/* PA_TXTRAILINGCLOCKS */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0x1564), 0x64);
	if (err) {
		dev_err(hba->dev, "Writing PA_TXTRAILINGCLOCKS error \n");
	}

	/* RX_LS_PREPARELEN_TIME RX0 */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008D, 4), 0x0B);
	if (err) {
		dev_err(hba->dev, "Writing RX_LS_PREPARELEN_TIME RX0 error \n");
	}

	/* RX_LS_PREPARELEN_TIME RX1 */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008D, 5), 0X0B);
	if (err) {
		dev_err(hba->dev, "Writing RX_LS_PREPARELEN_TIME RX1 error \n");
	}

	/* RX_HIBERNATE_BKEN RX0 */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x00F4, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0x9F);
	if (err) {
		dev_err(hba->dev, "Writing RX_HIBERNATE_BKEN RX0 error \n");
	}

	/* RX_HIBERNATE_BKEN RX1 */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x00F4, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0x9F);
	if (err) {
		dev_err(hba->dev, "Writing RX_HIBERNATE_BKEN RX1 error \n");
	}

	/* PWM_BURST_closure_length */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008E, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 15);
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008E, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 15);

	/* min_stall_not_config_time*/
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0088, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0xFF);
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0088, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0xFF);

	/* TX HB8_TIME CAP */
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x000F, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)), 0x64);
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x000F, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(1)), 0x64);

	/*RX HB8_TIME CAP*/
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0092, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0x64);
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0092, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0x64);

	/*TX EQ 3DB*/
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x00CD, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)), 0x5);

	/*RX garbage cnt = 32 SI*/
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x00F2, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0x9F);
	err = ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x00F2, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0x9F);

	/*bypass B0 reduce phy power ECO*/
	err = ufshcd_dme_set(hba, UIC_ARG_MIB(0xfc), 0xfc);
	if (err) {
		dev_err(hba->dev, "Writing 0xfc error \n");
	}

	dev_info(hba->dev, "UniPro v1.6 init completed\n");

	return 0;
}

#ifdef CONFIG_SPACEMIT_K3_UFS_CRYPTO_DEBUG
static const u32 ufs_spacemit_k3_test_key128[] = {
	0x87563412,
	0x88573513,
	0x89583614,
	0x8a593715,
};

static const u32 ufs_spacemit_k3_test_key256[] = {
	0x87563412,
	0x88573513,
	0x89583614,
	0x8a593715,
	0x8b5a3816,
	0x8c5b3917,
	0x8d5c3a18,
	0x8e5d3b19,
};

static void ufs_spacemit_k3_program_key_slot(struct ufs_hba *hba, int key_slot,
					    const u32 *key_vals, int key_count,
					    u32 cfg16, u32 cfg17)
{
	int i;
	u32 slot_offset;
	union ufs_crypto_cfg_entry cfg;

	memset(&cfg, 0, sizeof(cfg));
	for (i = 0; i < key_count && i < 16; i++)
		cfg.reg_val[i] = key_vals[i];
	cfg.reg_val[16] = cfg16;
	cfg.reg_val[17] = cfg17;

	slot_offset = hba->crypto_cfg_register +
		     key_slot * sizeof(union ufs_crypto_cfg_entry);

	ufshcd_writel(hba, 0, slot_offset + 16 * sizeof(cfg.reg_val[0]));
	for (i = 0; i < 16; i++)
		ufshcd_writel(hba, cfg.reg_val[i],
			     slot_offset + i * sizeof(cfg.reg_val[0]));
	ufshcd_writel(hba, cfg.reg_val[17],
		     slot_offset + 17 * sizeof(cfg.reg_val[0]));
	ufshcd_writel(hba, cfg.reg_val[16],
		     slot_offset + 16 * sizeof(cfg.reg_val[0]));
}

static int ufs_spacemit_k3_program_key_test(struct ufs_hba *hba)
{
	dev_info(hba->dev, "Crypto key test start\n");

	ufs_spacemit_k3_program_key_slot(hba, 0, ufs_spacemit_k3_test_key128,
				       ARRAY_SIZE(ufs_spacemit_k3_test_key128),
				       0x80000003, 0x0);
	ufs_spacemit_k3_program_key_slot(hba, 1, ufs_spacemit_k3_test_key256,
				       ARRAY_SIZE(ufs_spacemit_k3_test_key256),
				       0x80000103, 0x0);

	return 0;
}
#endif

static void ufs_spacemit_k3_set_dev_cap(struct ufs_host_params *ufs_spacemit_k3_cap, u32 pwr_hs)
{
	if (!ufs_spacemit_k3_cap)
		return;

	memset(ufs_spacemit_k3_cap, 0, sizeof(struct ufs_host_params));
	ufs_spacemit_k3_cap->tx_lanes = UFS_SPACEMIT_K3_LIMIT_NUM_LANES_TX;
	ufs_spacemit_k3_cap->rx_lanes = UFS_SPACEMIT_K3_LIMIT_NUM_LANES_RX;
	ufs_spacemit_k3_cap->hs_rx_gear = UFS_SPACEMIT_K3_LIMIT_HSGEAR_RX;
	ufs_spacemit_k3_cap->hs_tx_gear = UFS_SPACEMIT_K3_LIMIT_HSGEAR_TX;
	ufs_spacemit_k3_cap->pwm_rx_gear = UFS_SPACEMIT_K3_LIMIT_PWMGEAR_RX;
	ufs_spacemit_k3_cap->pwm_tx_gear = UFS_SPACEMIT_K3_LIMIT_PWMGEAR_TX;
	ufs_spacemit_k3_cap->rx_pwr_pwm = UFS_SPACEMIT_K3_LIMIT_RX_PWR_PWM;
	ufs_spacemit_k3_cap->tx_pwr_pwm = UFS_SPACEMIT_K3_LIMIT_TX_PWR_PWM;
	ufs_spacemit_k3_cap->rx_pwr_hs = pwr_hs;
	ufs_spacemit_k3_cap->tx_pwr_hs = pwr_hs;
	ufs_spacemit_k3_cap->hs_rate = UFS_SPACEMIT_K3_LIMIT_HS_RATE;
	ufs_spacemit_k3_cap->desired_working_mode = UFS_HS_MODE;
}

static int ufs_spacemit_k3_link_startup_pre_change(struct ufs_hba *hba)
{
	uint32_t reg_val;

	/*mphy_init*/
	ufs_spacemit_k3_mphy_init(hba);

	/* unipro v1p6 init */
	ufs_spacemit_k3_uniprov1p6_init(hba);

	/* config sysclk and tx symbol clk before link startup */
	reg_val = UFS_MAX_LINKSTARTUP_TIMER;

	/* clear bit0 and bit1, select b0 design */
	reg_val &= ~0x3;

	ufshcd_writel(hba, reg_val, UFS_PA_LINK_STARTUP_TIMER);

	ufshcd_writel(hba, UFS_SYSCLK, UFS_SYS1CLK_1US);
	ufshcd_writel(hba, UFS_TX_SYMBO_CLK, UFS_TX_SYMBOL_CLK_NS_US);

	dev_dbg(hba->dev, "REG_UFS_SYS1CLK_1US: 0x%x\n", ufshcd_readl(hba, UFS_SYS1CLK_1US));
	dev_dbg(hba->dev, "REG_UFS_TX_SYMBOL_CLK_NS_US: 0x%x\n",
		ufshcd_readl(hba, UFS_TX_SYMBOL_CLK_NS_US));

#ifdef CONFIG_SCSI_UFS_CRYPTO
	if (hba->caps & UFSHCD_CAP_CRYPTO) {
		reg_val = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
		dev_dbg(hba->dev, "REG_INTERRUPT_ENABLE before: 0x%x\n", reg_val);

		/* Enable crypto interrupts */
		reg_val |= BIT(26) | BIT(27);
		ufshcd_writel(hba, reg_val, REG_INTERRUPT_ENABLE);

		dev_dbg(hba->dev, "REG_INTERRUPT_ENABLE after: 0x%x\n",
			ufshcd_readl(hba, REG_INTERRUPT_ENABLE));
	}
#ifdef CONFIG_SPACEMIT_K3_UFS_CRYPTO_DEBUG
	/* crypto base test */
	ufs_spacemit_k3_program_key_test(hba);
#endif

#endif

	return 0;
}

static int ufs_spacemit_k3_link_startup_post_change(struct ufs_hba *hba)
{
	u32 tx_lanes;
	/* add 0xe8 make UFS2.1 run GEAR3+2Lane@409M */
	mdelay(5);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xe8, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)), 0x97);
	mdelay(1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xe8, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)), 0xd7);
	mdelay(1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xe8, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)), 0x17);

	/* DL_AFC0REQTIMEOUTVAL_MAX */
	ufshcd_dme_set(hba, UIC_ARG_MIB(DL_AFC0REQTIMEOUTVAL), UFS_DL_AFC0REQTIMEOUTVAL_MAX);

	return ufs_spacemit_k3_get_connected_tx_lanes(hba, &tx_lanes);
}

static int ufs_spacemit_k3_link_startup_notify(struct ufs_hba *hba,
					       enum ufs_notify_change_status status)
{
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		ufs_spacemit_k3_link_startup_pre_change(hba);
		break;
	case POST_CHANGE:
		ufs_spacemit_k3_link_startup_post_change(hba);
		break;
	default:
		break;
	}

	return err;
}

#ifdef CONFIG_PM
static int ufs_spacemit_k3_runtime_suspend(struct device *dev)
{
	int ret;

	ret = ufshcd_runtime_suspend(dev);
	if (ret)
		dev_err(dev, "Runtime suspend failed: %d\n", ret);

	return ret;
}

static int ufs_spacemit_k3_runtime_resume(struct device *dev)
{
	int ret;

	ret = ufshcd_runtime_resume(dev);
	if (ret)
		dev_err(dev, "Runtime resume failed: %d\n", ret);

	return ret;
}
#endif

static int ufs_spacemit_k3_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op,
				   enum ufs_notify_change_status status)
{
	struct scsi_target *starget, *found_starget = NULL;
	struct Scsi_Host *shost = hba->host;
	int ret = 0;

	if (status == PRE_CHANGE)
		return 0;

	/* TODO: Handle link off/inactive states */
	if (ufs_spacemit_k3_is_link_off(hba) || !ufs_spacemit_k3_is_link_active(hba)) {
		dev_dbg(hba->dev, "Link not active during suspend\n");
	}

	pm_runtime_put_sync(hba->dev);

	if (shost) {
		list_for_each_entry(starget, &shost->__targets, siblings) {
			if (starget->id == 0 && starget->channel == 0) {
				found_starget = starget;
				break;
			}
		}
	}
	if (found_starget) {
		pm_runtime_put_sync(&starget->dev);
	}

	return ret;
}

static int ufs_spacemit_k3_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	struct scsi_target *starget, *found_starget = NULL;
	struct Scsi_Host *shost = hba->host;

	/* TODO: Handle link off/inactive states */
	if (ufs_spacemit_k3_is_link_off(hba) || !ufs_spacemit_k3_is_link_active(hba)) {
		dev_dbg(hba->dev, "Link not active during resume\n");
	}

	pm_runtime_get_sync(hba->dev);

	if (shost) {
		list_for_each_entry(starget, &shost->__targets, siblings) {
			if (starget->id == 0 && starget->channel == 0) {
				found_starget = starget;
				break;
			}
		}
	}

	if (found_starget) {
		pm_runtime_get_sync(&starget->dev);
	}

	return 0;
}

static int ufs_spacemit_k3_pwr_change_notify(struct ufs_hba *hba,
					     enum ufs_notify_change_status status,
					     struct ufs_pa_layer_attr *dev_max_params,
					     struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_spacemit_k3_host *host = ufshcd_get_variant(hba);
	struct ufs_host_params ufs_spacemit_k3_cap;
	int ret = 0;
	u32 reg_val;
	int timeout;

	if (!dev_req_params) {
		dev_err(hba->dev, "dev_req_params is NULL\n");
		return -EINVAL;
	}

	switch (status) {
	case PRE_CHANGE:
		ufs_spacemit_k3_set_dev_cap(&ufs_spacemit_k3_cap, FAST_MODE);
		ret = ufshcd_negotiate_pwr_params(&ufs_spacemit_k3_cap, dev_max_params,
						  dev_req_params);
		if (ret) {
			dev_err(hba->dev, "Failed to negotiate power params: %d\n", ret);
			return ret;
		}

		dev_dbg(hba->dev,
			"Power mode config - gear_rx:%d, gear_tx:%d, lane_rx:%d, lane_tx:%d, pwr_rx:%d, pwr_tx:%d, hs_rate:%d\n",
			dev_req_params->gear_rx, dev_req_params->gear_tx, dev_req_params->lane_rx,
			dev_req_params->lane_tx, dev_req_params->pwr_rx, dev_req_params->pwr_tx,
			dev_req_params->hs_rate);
		break;
	case POST_CHANGE:
		/* cache the power mode parameters to use internally */
		memcpy(&host->dev_req_params, dev_req_params, sizeof(*dev_req_params));

		/* wait PLL_lock here, bit31 at UFS_MPHY_PU_CTRL */
		timeout = MPHY_PLL_LOCK_TIMEOUT_US;
		while (timeout > 0) {
			reg_val = ufshcd_readl(hba, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);
			if (reg_val & MPHY_PLL_LOCK_BIT)
				break;
			udelay(1);
			timeout--;
		}

		if (timeout <= 0) {
			dev_err(hba->dev, "PLL lock timeout after power mode change\n");
			return -ETIMEDOUT;
		}

		dev_info(hba->dev, "M-PHY PLL locked after power mode change\n");
		/*set ANA_HSGEAR_CTRL_ATTR back to default value*/
		ufshcd_dme_set(hba, UIC_ARG_MIB(ANA_HSGEAR_CTRL_ATTR), 0x00);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int ufs_spacemit_k3_quirk_host_pa_saveconfigtime(struct ufs_hba *hba)
{
	int err;
	u32 pa_vs_config_reg1;

	err = ufshcd_dme_get(hba, UIC_ARG_MIB(UFS_PA_VS_CONFIG_REG1), &pa_vs_config_reg1);
	if (err)
		return err;

	/* Allow extension of MSB bits of PA_SaveConfigTime attribute */
	return ufshcd_dme_set(hba, UIC_ARG_MIB(UFS_PA_VS_CONFIG_REG1),
			      (pa_vs_config_reg1 | (1 << 12)));
}

static int ufs_spacemit_k3_apply_dev_quirks(struct ufs_hba *hba)
{
	int err = 0;
	u32 reg_val;
	int timeout;

	if (hba->dev_quirks & UFS_DEVICE_QUIRK_HOST_PA_SAVECONFIGTIME)
		err = ufs_spacemit_k3_quirk_host_pa_saveconfigtime(hba);

	if (hba->dev_info.wmanufacturerid == UFS_VENDOR_WDC)
		hba->dev_quirks |= UFS_DEVICE_QUIRK_HOST_PA_TACTIVATE;

	/*LCC_DISABLE*/
	mdelay(50);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(TX_LCC_ENABLE, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)), 0);
	mdelay(1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(TX_LCC_ENABLE, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(1)), 0);

	/*TX_Min_ActivateTime*/
	mdelay(1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(TX_MIN_ACTIVATETIME, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
		       0x0);
	mdelay(1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(TX_MIN_ACTIVATETIME, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(1)),
		       0x0);
	mdelay(1);

	mdelay(10);

	/*use backdoor reg to pre-set TX RATE/GEAR to let PLL lock before set_power_mode switch*/
	ufshcd_dme_set(hba, UIC_ARG_MIB(ANA_HSGEAR_CTRL_ATTR), 0x25);
	mdelay(10);

	/* wait PLL_lock here, bit31 at UFS_MPHY_PU_CTRL */
	timeout = MPHY_PLL_LOCK_TIMEOUT_US;
	while (timeout > 0) {
		reg_val = ufshcd_readl(hba, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);
		if (reg_val & MPHY_PLL_LOCK_BIT)
			break;
		udelay(1);
		timeout--;
	}

	if (timeout <= 0) {
		dev_err(hba->dev, "PLL lock timeout in apply_dev_quirks\n");
		return -ETIMEDOUT;
	}

	return err;
}

/*
 * Select UFS HCI version reported to the core based on hardware major
 * revision: hw_ver.major == 1 -> HCI 1.1, otherwise -> HCI 2.0.
 *
 * TODO: hw_ver is not currently populated from hardware, so this mapping
 * effectively always returns HCI 2.0 until proper version detection is added.
 */
static u32 ufs_spacemit_k3_get_ufs_hci_version(struct ufs_hba *hba)
{
	struct ufs_spacemit_k3_host *host = ufshcd_get_variant(hba);

	if (host->hw_ver.major == 0x1)
		return ufshci_version(1, 1);
	else
		return ufshci_version(2, 0);
}

static __maybe_unused void ufs_spacemit_k3_get_unipro_ver(struct ufs_hba *hba)
{
	struct ufs_spacemit_k3_host *host = ufshcd_get_variant(hba);

	if (ufshcd_dme_get(hba, UIC_ARG_MIB(PA_LOCALVERINFO), &host->unipro_ver))
		host->unipro_ver = 0;
	if (ufshcd_dme_get(hba, UIC_ARG_MIB(PA_REMOTEVERINFO), &host->remote_unipro_ver))
		host->remote_unipro_ver = 0;
}

/**
 * ufs_spacemit_k3_advertise_quirks - advertise the known Spacemit K3 UFS controller quirks
 * @hba: host controller instance
 *
 * Spacemit K3 UFS host controller might have some non standard behaviours (quirks)
 * than what is specified by UFSHCI specification. Advertise all such
 * quirks to standard UFS host controller driver so standard takes them into
 * account.
 */
static void ufs_spacemit_k3_advertise_quirks(struct ufs_hba *hba)
{
	struct ufs_spacemit_k3_host *host = ufshcd_get_variant(hba);

	/* TODO: hw_ver is not currently initialized; version-based quirks
	 * selection is effectively dead code. Either add proper hardware
	 * version detection and populate host->hw_ver, or remove/adjust
	 * these conditions based on real hardware behavior.
	 */
	if (host->hw_ver.major == 0x01) {
		hba->quirks |= UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS |
			       UFSHCD_QUIRK_BROKEN_PA_RXHSUNTERMCAP |
			       UFSHCD_QUIRK_DME_PEER_ACCESS_AUTO_MODE;

		if (host->hw_ver.minor == 0x0001 && host->hw_ver.step == 0x0001)
			hba->quirks |= UFSHCD_QUIRK_BROKEN_INTR_AGGR;
	}

	if (host->hw_ver.major == 0x2) {
		hba->quirks |= UFSHCD_QUIRK_BROKEN_UFS_HCI_VERSION;
	}

	/* break auto hibern8 */
	hba->quirks |= UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8;
}

static void ufs_spacemit_k3_set_caps(struct ufs_hba *hba)
{
	/* support clock-gating */
	/* hba->caps |= UFSHCD_CAP_CLK_GATING; */

	/* support inline encryption */
	/* hba->caps |= UFSHCD_CAP_CRYPTO; */

	/* support write booster */
	hba->caps |= UFSHCD_CAP_WB_EN;

	/* support runtime autosuspend - disabled for silicon bringup */
	/* hba->caps |= UFSHCD_CAP_RPM_AUTOSUSPEND; */
}

/**
 * ufs_spacemit_k3_setup_xfer_req
 * @hba: host controller instance
 * tag: current task slot index
 * is_scsi_cmd: scsi command or not
 */
static void ufs_spacemit_k3_setup_xfer_req(struct ufs_hba *hba, int tag, bool is_scsi_cmd)
{
#ifdef CONFIG_SCSI_UFS_CRYPTO
	u32 doorbell = 0;
	struct ufs_spacemit_k3_host *host = ufshcd_get_variant(hba);
	struct ufshcd_lrb *lrbp = &hba->lrb[tag];
	bool curr_request_crypto;

	if (!(hba->caps & UFSHCD_CAP_CRYPTO))
		return;

	if (is_scsi_cmd && (lrbp->crypto_key_slot >= 0))
		curr_request_crypto = true;
	else
		curr_request_crypto = false;

	/* crypto request need to wait for the clean doorbell to avoid data corruption */
	if (host->prev_request_crypto) {
		int timeout = 10000; /* 10ms timeout */

		while (timeout > 0) {
			doorbell = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_DOOR_BELL);
			if (!doorbell)
				break;
			udelay(1);
			timeout--;
		}

		if (timeout <= 0)
			dev_warn(hba->dev, "Doorbell wait timeout in crypto path\n");
	}

	host->prev_request_crypto = curr_request_crypto;
#endif
}

/**
 * ufs_spacemit_k3_setup_clocks - enables/disable clocks
 * @hba: host controller instance
 * @on: If true, enable clocks else disable them.
 * @status: PRE_CHANGE or POST_CHANGE notify
 *
 * Returns 0 on success, non-zero on failure.
 */
static int ufs_spacemit_k3_setup_clocks(struct ufs_hba *hba, bool on,
					enum ufs_notify_change_status status)
{
	struct ufs_clk_info *clki;
	int ret = 0;

	switch (status) {
	case PRE_CHANGE:
		if (on) {
		} else {
		}
		break;

	case POST_CHANGE:
		if (on) {
			/*
			 * Trigger FC handshake via clk framework.
			 * We re-apply the current rate to ensure the FC bit is toggled
			 * and checked by the clock driver.
			 */
			list_for_each_entry(clki, &hba->clk_list_head, list) {
				if (!strcmp(clki->name, "ufs-aclk")) {
					unsigned long rate = clk_get_rate(clki->clk);

					ret = clk_set_rate(clki->clk, rate);
					if (ret)
						dev_err(hba->dev, "Failed to trigger ACLK FC: %d\n",
							ret);
					break;
				}
			}

			/* Also check/trigger FC via PMUAP, matching ufs-asr logic. */
			ufs_spacemit_k3_trigger_aclk_fc(hba->dev);
		} else {
		}
		break;
	}

	dev_dbg(hba->dev, "ufs clocks %s, status=%s\n", on ? "on" : "off",
		status == PRE_CHANGE ? "PRE_CHANGE" : "POST_CHANGE");

	return ret;
}

/**
 * ufs_spacemit_k3_platform_init
 * @dev: device pointer
 *
 * Prepare the clk source and reset ufs_aclk
 */
static void ufs_spacemit_k3_platform_init(struct device *dev)
{
	void __iomem *ufs_pmuap_reg;
	void __iomem *acgr_reg;
	u32 reg_val;
	u32 timeout;

	acgr_reg = ioremap((phys_addr_t)SPACEMIT_K3_ACGR_REG_BASE, 4);
	if (!acgr_reg) {
		dev_err(dev, "Failed to map ACGR reg\n");
		return;
	}

	ufs_pmuap_reg = ioremap((phys_addr_t)SPACEMIT_K3_UFS_PMUAP_REG, 4);
	if (!ufs_pmuap_reg) {
		dev_err(dev, "Failed to map UFS PMUAP reg\n");
		iounmap(acgr_reg);
		return;
	}

	/* enable CLK_499M */
	reg_val = readl(acgr_reg);
	reg_val |= BIT(21);
	writel(reg_val, acgr_reg);

	/* ufs_aclk reset */
	writel(0x0, ufs_pmuap_reg);

	reg_val = BIT(0) | BIT(1);
	/*
	 * aclk selection and divider fields are kept consistent with the
	 * known-good ufs-asr settings in this workspace.
	 */
	writel(reg_val, ufs_pmuap_reg);

	/* set FC_REQ */
	reg_val |= UFS_PMUAP_ACLK_FC_REQ;
	writel(reg_val, ufs_pmuap_reg);

	timeout = UFS_ACLK_FC_TIMEOUT;
	while (timeout) {
		reg_val = readl(ufs_pmuap_reg);
		if (!(reg_val & UFS_PMUAP_ACLK_FC_REQ))
			break;
		timeout--;
		udelay(10);
	}
	if (reg_val & UFS_PMUAP_ACLK_FC_REQ)
		dev_err(dev, "Failed to select aclk (PMUAP=0x%x)\n", reg_val);

	dev_err(dev, "ufs_spacemit_k3_platform_init, PMUAP=0x%x\n",
		readl(ufs_pmuap_reg));

	iounmap(ufs_pmuap_reg);
	iounmap(acgr_reg);
}

/**
 * ufs_spacemit_k3_fsm_dump_work - Deferred work to dump FSM state
 * @work: work structure
 *
 * This function is called from a workqueue context (not interrupt context),
 * allowing safe execution of blocking operations like ufshcd_dme_get().
 */
static void ufs_spacemit_k3_fsm_dump_work(struct work_struct *work)
{
	struct ufs_spacemit_k3_host *host = container_of(work, struct ufs_spacemit_k3_host,
							  fsm_dump_work);
	struct ufs_hba *hba = host->hba;

	/* Safe to call blocking functions in workqueue context */
	if (ufshcd_is_link_active(hba))
		ufs_spacemit_k3_dump_fsm_state(hba);
}

/**
 * ufs_spacemit_k3_init - init phy and prepare clk
 * @hba: host controller instance
 */
static int ufs_spacemit_k3_init(struct ufs_hba *hba)
{
	int err = 0;
	struct device *dev = hba->dev;
	struct ufs_spacemit_k3_host *host;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host) {
		err = -ENOMEM;
		dev_err(dev, "%s: no memory for Spacemit K3 UFS host\n", __func__);
		goto out;
	}

	/* Get reset control from device tree */
	host->rst = devm_reset_control_get_optional_exclusive(dev, "ufs-aclk-rst");
	if (IS_ERR(host->rst)) {
		err = PTR_ERR(host->rst);
		dev_err(dev, "Failed to get reset control: %d\n", err);
		host->rst = NULL;
		/* Continue without reset control - will use manual PMUAP method */
	} else if (host->rst) {
		/* Perform initial reset cycle */
		reset_control_assert(host->rst);
		udelay(1);
		reset_control_deassert(host->rst);
		dev_info(dev, "Reset control initialized successfully\n");
	}

	/* Make a two way bind between the spacemit k3 host and the hba */
	host->hba = hba;
	ufshcd_set_variant(hba, host);
	ufs_spacemit_k3_set_caps(hba);
	ufs_spacemit_k3_advertise_quirks(hba);

	/* Initialize workqueue for deferred FSM state dump */
	INIT_WORK(&host->fsm_dump_work, ufs_spacemit_k3_fsm_dump_work);

	err = ufshcd_vops_phy_initialization(host->hba);
out:
	return err;
}

static int ufs_spacemit_k3_axi_reset(struct ufs_hba *hba)
{
	int ret = 0;
	struct ufs_spacemit_k3_host *host = ufshcd_get_variant(hba);
	struct ufs_reg_snapshot save_regs;
	struct ufs_clk_info *clki = NULL;
	struct device *dev = hba->dev;
	struct list_head *head = &hba->clk_list_head;

	/* save host registers */
	save_regs.reg_utrlba = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_LIST_BASE_L);
	save_regs.reg_utrlbau = ufshcd_readl(hba, REG_UTP_TRANSFER_REQ_LIST_BASE_H);
	save_regs.reg_utrmlba = ufshcd_readl(hba, REG_UTP_TASK_REQ_LIST_BASE_L);
	save_regs.reg_utrmlbau = ufshcd_readl(hba, REG_UTP_TASK_REQ_LIST_BASE_H);

	save_regs.reg_sys1clk = ufshcd_readl(hba, UFS_SYS1CLK_1US);
	save_regs.reg_tx_symbol_clk = ufshcd_readl(hba, UFS_TX_SYMBOL_CLK_NS_US);
	save_regs.reg_retry_timer = ufshcd_readl(hba, UFS_RETRY_TIMER_REG);
	save_regs.reg_pa_link = ufshcd_readl(hba, UFS_PA_LINK_STARTUP_TIMER);
	save_regs.reg_cfg1 = ufshcd_readl(hba, UFS_CFG1);

	/* get ufs aclk from clock list (already parsed from DTS) */
	if (!list_empty(head)) {
		list_for_each_entry(clki, head, list) {
			if (clki->name && !strcmp(clki->name, UFS_SPACEMIT_K3_ACLK_NAME))
				break;
		}
	}
	if (!clki || !clki->clk) {
		dev_err(dev, "Failed to find ufs-aclk in clock list\n");
		ret = -ENOENT;
		goto out;
	}

	/* Disable clock before reset */
	if (__clk_is_enabled(clki->clk)) {
		clk_disable_unprepare(clki->clk);
	}

	/* Perform AXI reset using Reset Framework */
	if (host->rst) {
		dev_dbg(dev, "Asserting UFS AXI reset via reset framework\n");
		ret = reset_control_assert(host->rst);
		if (ret) {
			dev_err(dev, "Reset assert failed: %d\n", ret);
			goto out;
		}
		usleep_range(10, 20);

		ret = reset_control_deassert(host->rst);
		if (ret) {
			dev_err(dev, "Reset deassert failed: %d\n", ret);
			goto out;
		}
		dev_info(dev, "UFS AXI reset completed via reset framework\n");
	} else {
		void __iomem *ufs_pmuap_reg;

		dev_warn(dev, "No reset control, using PMUAP for UFS AXI reset\n");
		ufs_pmuap_reg = ioremap((phys_addr_t)SPACEMIT_K3_UFS_PMUAP_REG, 4);
		if (!ufs_pmuap_reg) {
			dev_err(dev, "Failed to map UFS PMUAP reg\n");
			ret = -ENOMEM;
			goto out;
		}
		writel(0x0, ufs_pmuap_reg);
		iounmap(ufs_pmuap_reg);
	}

	/* Re-enable ufs aclk */
	if (clki->max_freq) {
		ret = clk_set_rate(clki->clk, clki->max_freq);
		if (ret) {
			dev_err(hba->dev, "%s: %s clk set rate(%dHz) failed, %d\n", __func__,
				clki->name, clki->max_freq, ret);
			goto out;
		}
		clki->curr_freq = clki->max_freq;
		dev_dbg(dev, "%s: clk: %s, rate: %lu\n", __func__, clki->name,
			clk_get_rate(clki->clk));
	}
	ret = clk_prepare_enable(clki->clk);
	if (ret) {
		dev_err(hba->dev, "%s: %s prepare enable failed, %d\n", __func__, clki->name, ret);
		goto out;
	}
	clki->enabled = 1;
	dev_dbg(hba->dev, "Clock %s re-enabled\n", clki->name);

	/* restore host registers */
	ufshcd_writel(hba, save_regs.reg_utrlba, REG_UTP_TRANSFER_REQ_LIST_BASE_L);
	ufshcd_writel(hba, save_regs.reg_utrlbau, REG_UTP_TRANSFER_REQ_LIST_BASE_H);
	ufshcd_writel(hba, save_regs.reg_utrmlba, REG_UTP_TASK_REQ_LIST_BASE_L);
	ufshcd_writel(hba, save_regs.reg_utrmlbau, REG_UTP_TASK_REQ_LIST_BASE_H);

	ufshcd_writel(hba, save_regs.reg_sys1clk, UFS_SYS1CLK_1US);
	ufshcd_writel(hba, save_regs.reg_tx_symbol_clk, UFS_TX_SYMBOL_CLK_NS_US);
	ufshcd_writel(hba, save_regs.reg_retry_timer, UFS_RETRY_TIMER_REG);
	ufshcd_writel(hba, save_regs.reg_pa_link, UFS_PA_LINK_STARTUP_TIMER);
	ufshcd_writel(hba, save_regs.reg_cfg1, UFS_CFG1);

out:
	return ret;
}

/**
 * ufs_spacemit_k3_device_reset - Toggle device reset line
 * @hba: per-adapter instance
 *
 * Toggles the reset line to reset the attached UFS device.
 * On first call, skip reset. On subsequent calls, perform full reset.
 *
 * Returns: 0 on success
 */
static int ufs_spacemit_k3_device_reset(struct ufs_hba *hba)
{
	struct ufs_spacemit_k3_host *host = ufshcd_get_variant(hba);

	if (!host->first_init_done) {
		host->first_init_done = true;
		dev_dbg(hba->dev, "First init, skipping device reset\n");
		return 0;
	}

	{
		ufshcd_set_link_off(hba);

		/* ufs axi reset */
		ufs_spacemit_k3_axi_reset(hba);

		/* stop device ref_clk & asserted ufs device reset */
		ufshcd_writel(hba, 0x000, UFS_PHY_MNG_BASE + UFS_DEVICE_IO_CTRL);
		mdelay(5);

		/*power off analog PHY, reset all host MPHY digital logic*/
		ufshcd_writel(hba, 0x000, UFS_PHY_MNG_BASE + UFS_MPHY_RST_CTRL);
		mdelay(5);

		ufshcd_writel(hba, 0x000, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);
		mdelay(5);

		dev_info(hba->dev, "Device reset completed\n");
	}

	return 0;
}

/**
 * ufs_spacemit_k3_event_notify - Handle UFS error events
 * @hba: host controller instance
 * @evt: event type
 * @data: event-specific data
 *
 * Handles error events from UFS core, dumps registers immediately
 * and schedules FSM state dump for later execution in workqueue context.
 */
static void ufs_spacemit_k3_event_notify(struct ufs_hba *hba, enum ufs_event_type evt, void *data)
{
	struct ufs_spacemit_k3_host *host = ufshcd_get_variant(hba);
	bool dump_regs = false;

	switch (evt) {
	case UFS_EVT_PA_ERR:
		if (data) {
			u32 pa_err = *(u32 *)data;
			dev_warn(hba->dev, "PA error event, INT errors:0x%x, PA_ERR_CODE:0x%x\n",
				 hba->errors, pa_err);
		}
		dump_regs = true;
		break;

	case UFS_EVT_DL_ERR:
		if (data) {
			u32 dl_err = *(u32 *)data;
			dev_warn(hba->dev, "DL error event, INT errors:0x%x, DL_ERR:0x%x\n",
				 hba->errors, dl_err);
		}
		dump_regs = true;
		break;

	case UFS_EVT_ABORT:
		dev_warn(hba->dev, "Abort event, INT errors:0x%x\n", hba->errors);
		break;

	default:
		break;
	}

	/* Dump registers if error occurred (safe in interrupt context) */
	if (hba->errors || dump_regs)
		ufs_spacemit_k3_dump_host_regs(hba);

	/* Schedule FSM state dump in workqueue context (not in interrupt context) */
	if (ufshcd_is_link_active(hba) && host)
		queue_work(system_wq, &host->fsm_dump_work);
}

/**
 * ufs_spacemit_k3_hibern8_notify - Handle hibernate enter/exit
 * @hba: host controller instance
 * @cmd: UIC command (HIBER_ENTER or HIBER_EXIT)
 * @status: notification status
 *
 * Manages M-PHY power state during hibernate transitions.
 */
static void ufs_spacemit_k3_hibern8_notify(struct ufs_hba *hba, enum uic_cmd_dme cmd,
					   enum ufs_notify_change_status status)
{
	u32 reg_val;
	int timeout;

	dev_dbg(hba->dev, "Hibern8 notify: cmd=%d, status=%d\n", cmd, status);
	if (status == PRE_CHANGE) {
		if (cmd == UIC_CMD_DME_HIBER_EXIT) {
			mdelay(1);

			/* Enable reference clock */
			ufshcd_writel(hba, MPHY_DEVICE_RESET_DEASSERT,
				      UFS_PHY_MNG_BASE + UFS_DEVICE_IO_CTRL);
			mdelay(1);

			/* Power up all */
			ufshcd_writel(hba, MPHY_PU_ALL, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);
			mdelay(1);

			/* Assert ana_rx_hb8_reset */
			ufshcd_writel(hba, MPHY_PU_WITH_HB8_RESET,
				      UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);
			mdelay(1);

			/* Deassert ana_rx_hb8_reset */
			ufshcd_writel(hba, MPHY_PU_ALL, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);

			/* Wait for PLL lock with timeout */
			timeout = MPHY_PLL_LOCK_TIMEOUT_US;
			while (timeout > 0) {
				reg_val = ufshcd_readl(hba, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);
				if (reg_val & MPHY_PLL_LOCK_BIT)
					break;
				udelay(1);
				timeout--;
			}

			if (timeout <= 0)
				dev_err(hba->dev, "PLL lock timeout on hibern8 exit\n");
		}
	}
	if (status == POST_CHANGE) {
		if (cmd == UIC_CMD_DME_HIBER_ENTER) {
			mdelay(1);

			/* Power down M-PHY */
			ufshcd_writel(hba, 0x0, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);
			mdelay(1);

			/* Keep reference clock enabled, assert device reset */
			ufshcd_writel(hba, MPHY_DEVICE_RESET_ASSERT,
				      UFS_PHY_MNG_BASE + UFS_DEVICE_IO_CTRL);
		}
	}
}

/**
 * ufs_spacemit_k3_hce_enable_notify - Configure HCE enable sequence
 * @hba: host controller instance
 * @status: notification status (PRE_CHANGE or POST_CHANGE)
 *
 * Configures host controller enable with proper sequencing.
 * Handles crypto enable if supported.
 *
 * Returns: 0 on success
 */
static int ufs_spacemit_k3_hce_enable_notify(struct ufs_hba *hba,
					     enum ufs_notify_change_status status)
{
	struct ufs_spacemit_k3_host *host = ufshcd_get_variant(hba);
	u32 enable_val, val;

	if (status == PRE_CHANGE) {
		enable_val = CONTROLLER_ENABLE;

		if (hba->caps & UFSHCD_CAP_CRYPTO)
			enable_val = CRYPTO_GENERAL_ENABLE | CONTROLLER_ENABLE;

		if (!host->first_hce_done) {
			host->first_hce_done = true;
			dev_dbg(hba->dev, "First HCE enable\n");
		} else {
			val = ufshcd_readl(hba, REG_CONTROLLER_ENABLE);
			if (val == enable_val) {
				ufshcd_writel(hba, enable_val & (1 << CONTROLLER_ENABLE),
					      REG_CONTROLLER_ENABLE);

				while (ufshcd_readl(hba, REG_CONTROLLER_ENABLE) ==
				       (enable_val & (1 << CONTROLLER_ENABLE)))
					;
			}
		}
	}
	return 0;
}

/**
 * struct ufs_hba_spacemit_k3_vops - UFS Spacemit K3 specific variant operations
 *
 * The variant operations configure the necessary controller and PHY
 * handshake during initialization.
 */
static const struct ufs_hba_variant_ops ufs_hba_spacemit_k3_vops = {
	.name = "lark_ufs",
	.init = ufs_spacemit_k3_init,
	.get_ufs_hci_version = ufs_spacemit_k3_get_ufs_hci_version,
	.link_startup_notify = ufs_spacemit_k3_link_startup_notify,
	.pwr_change_notify = ufs_spacemit_k3_pwr_change_notify,
	.setup_clocks = ufs_spacemit_k3_setup_clocks,
	.setup_xfer_req = ufs_spacemit_k3_setup_xfer_req,
	.device_reset = ufs_spacemit_k3_device_reset,
	.event_notify = ufs_spacemit_k3_event_notify,
	.apply_dev_quirks = ufs_spacemit_k3_apply_dev_quirks,
	.hibern8_notify = ufs_spacemit_k3_hibern8_notify,
	.hce_enable_notify = ufs_spacemit_k3_hce_enable_notify,
	.suspend = ufs_spacemit_k3_suspend,
	.resume = ufs_spacemit_k3_resume,
};

static const struct of_device_id ufs_spacemit_k3_of_match[] = {
	{ .compatible = "spacemit,k3-ufshcd", .data = &ufs_hba_spacemit_k3_vops },
	{},
};
MODULE_DEVICE_TABLE(of, ufs_spacemit_k3_of_match);

/**
 * ufs_spacemit_k3_probe - probe routine of the driver
 * @pdev: pointer to Platform device handle
 *
 * Return zero for success and non-zero for failure
 */
static int ufs_spacemit_k3_probe(struct platform_device *pdev)
{
	int err;
	const struct of_device_id *of_id;
	struct ufs_hba_variant_ops *vops;
	struct device *dev = &pdev->dev;

	of_id = of_match_node(ufs_spacemit_k3_of_match, dev->of_node);
	if (!of_id) {
		dev_err(dev, "ufs: no matching of_node found\n");
		return -ENODEV;
	}

	ufs_spacemit_k3_platform_init(dev);

	vops = (struct ufs_hba_variant_ops *)of_id->data;
	err = ufshcd_pltfrm_init(pdev, vops);
	if (err) {
		dev_err(dev, "ufs: ufshcd_pltfrm_init() failed %d\n", err);
	}

	return err;
}

/**
 * ufs_spacemit_k3_remove - set driver_data of the device to NULL
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static void ufs_spacemit_k3_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufshcd_remove(hba);
	pm_runtime_put(&(pdev)->dev);
}

static const struct dev_pm_ops ufs_spacemit_k3_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufshcd_system_suspend, ufshcd_system_resume) SET_RUNTIME_PM_OPS(
		ufs_spacemit_k3_runtime_suspend, ufs_spacemit_k3_runtime_resume, NULL)
		.prepare = ufshcd_suspend_prepare,
	.complete = ufshcd_resume_complete,
};

static struct platform_driver ufs_spacemit_k3_pltform = {
	.probe	= ufs_spacemit_k3_probe,
	.remove	= ufs_spacemit_k3_remove,
	.driver	= {
		.name	= "ufshcd-spacemit-k3",
		.pm	= &ufs_spacemit_k3_pm_ops,
		.of_match_table = of_match_ptr(ufs_spacemit_k3_of_match),
	},
};
module_platform_driver(ufs_spacemit_k3_pltform);

MODULE_LICENSE("GPL v2");
