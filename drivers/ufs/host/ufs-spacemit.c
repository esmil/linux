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
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <scsi/scsi_device.h>

#include <ufs/ufshcd.h>
#include <ufs/ufs_quirks.h>
#include <ufs/unipro.h>

#include "ufshcd-pltfrm.h"
#include "ufs-spacemit.h"

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
#define PA_LOCAL_TX_LCC_ENABLE 0x155E
#define PA_PEER_TX_LCC_ENABLE 0x155F

#define PA_SCRAMBLING 0x1585
#define PA_STALLNOCONFIGTIME 0x15A3
#define PA_TACTIVATE 0x15A8
#define PA_GRANULARITY 0x15AA
#define PA_MK2EXTENSIONGUARDBAND 0x15AB
#define PA_TXTRAILINGCLOCKS 0x1564

/*special TX/RX Configuration Attributes*/
#define RX_LS_PRE_LEN_CAP 0x008D
#define RX_LANE_HB8_BKDOOR_ATTR 0x00F4
#define RX_PWRM_CLOSURE_LEN_CAP 0x008E
#define RX_MIN_STALL_CAP 0x0088
#define TX_HIBERN8TIME_CAP 0x000F
#define RX_HIBERN8TIME_CAP 0x0092
#define RX_LANE_SOF_BKDOOR_ATT 0x00F2
#define RX_GARBAGE_COUNT_OFFSET 0x00F2

/*special analog reg*/
#define ANA_EQ_CTRL_REG_ATTR 0x00CD
#define ANA_HSGEAR_CTRL_ATTR 0x00C1

/* the delay between TX bursts */
#define VS_TX_BURST_CLOSURE_DELAY 0xD084

/* host registers for lark */
static int spacemit_regs[] = {
	(UFS_PHY_MNG_BASE + UFS_MPHY_RST_CTRL),
	(UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL),
	(UFS_PHY_MNG_BASE + UFS_DEVICE_IO_CTRL),
	0xFFF,
};

static u32 spacemit_clock_freq;

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

static void ufs_spacemit_dump_host_regs(struct ufs_hba *hba)
{
	u8 *buf;
	u32 val;
	int offset;
	int i;
	size_t len;
	int *host_reg = spacemit_regs;

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

#define MPHY_TX_FSM_STATE 0x41
#define MPHY_RX_FSM_STATE 0xC1

static int ufs_spacemit_check_hibern8(struct ufs_hba *hba)
{
	u32 tx_fsm_val_0 = 0;
	u32 tx_fsm_val_1 = 0;
	int retries = DIV_ROUND_UP(HBRN8_POLL_TOUT_MS * 1000, 100);
	int err = 0;

	do {
		err = ufshcd_dme_get(hba,
				     UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE,
						     UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
				     &tx_fsm_val_0);
		err |= ufshcd_dme_get(hba,
				      UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE,
						      UIC_ARG_MPHY_TX_GEN_SEL_INDEX(1)),
				      &tx_fsm_val_1);
		if (err || (tx_fsm_val_0 == TX_FSM_HIBERN8 &&
			    tx_fsm_val_1 == TX_FSM_HIBERN8))
			break;

		usleep_range(100, 200);
	} while (--retries > 0);

	if (!err && retries <= 0) {
		err = ufshcd_dme_get(hba,
				     UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE,
						     UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
				     &tx_fsm_val_0);
		err |= ufshcd_dme_get(hba,
				      UIC_ARG_MIB_SEL(MPHY_TX_FSM_STATE,
						      UIC_ARG_MPHY_TX_GEN_SEL_INDEX(1)),
				      &tx_fsm_val_1);
	}

	if (err) {
		dev_err(hba->dev, "%s: unable to get TX_FSM_STATE, err %d\n",
			__func__, err);
	} else if (tx_fsm_val_0 != TX_FSM_HIBERN8 ||
		   tx_fsm_val_1 != TX_FSM_HIBERN8) {
		err = -ETIMEDOUT;
		dev_err(hba->dev,
			"%s: invalid TX_FSM_STATE, lane0 = %u, lane1 = %u\n",
			__func__, tx_fsm_val_0, tx_fsm_val_1);
	}

	return err;
}

static int ufs_spacemit_get_connected_tx_lanes(struct ufs_hba *hba, u32 *tx_lanes)
{
	int err = 0;

	err = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDTXDATALANES), tx_lanes);
	if (err)
		dev_err(hba->dev, "%s: couldn't read PA_CONNECTEDTXDATALANES %d\n", __func__, err);

	return err;
}

static u32 ufs_spacemit_get_sys1clk_1us(struct ufs_hba *hba)
{
	struct ufs_clk_info *clki, *ufs_aclk = NULL;
	struct list_head *head = &hba->clk_list_head;
	unsigned long rate_hz = 0;

	if (!list_empty(head)) {
		list_for_each_entry(clki, head, list) {
			if (clki->name && !strcmp(clki->name, "ufs-aclk") && clki->clk) {
				ufs_aclk = clki;
				break;
			}
		}
	}

	if (ufs_aclk && ufs_aclk->clk)
		rate_hz = clk_get_rate(ufs_aclk->clk);

	if (!rate_hz)
		return 0;

	return DIV_ROUND_CLOSEST(rate_hz, 1000000);
}

static int ufs_spacemit_wait_mphy_pll_lock(struct ufs_hba *hba)
{
	u32 reg_val = 0;
	int timeout = MPHY_PLL_LOCK_TIMEOUT_US;

	while (timeout-- > 0) {
		reg_val = ufshcd_readl(hba, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);
		if (reg_val & MPHY_PLL_LOCK_BIT)
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static __maybe_unused void ufs_spacemit_get_unipro_ver(struct ufs_hba *hba);

struct ufs_spacemit_dme_setting {
	const char *name;
	u32 attr_sel;
	u32 value;
};

static int ufs_spacemit_dme_set_checked(struct ufs_hba *hba, u32 attr_sel,
					     u32 value, const char *name)
{
	int err;

	err = ufshcd_dme_set(hba, attr_sel, value);
	if (err)
		dev_err(hba->dev, "failed to set %s (0x%x) to 0x%x: %d\n",
			name, attr_sel, value, err);

	return err;
}

static int ufs_spacemit_mphy_init(struct ufs_hba *hba)
{
	int ret;

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
	ufshcd_writel(hba, MPHY_DEVICE_RESET_DEASSERT,
		      UFS_PHY_MNG_BASE + UFS_DEVICE_IO_CTRL);
	mdelay(1);

	ret = ufs_spacemit_wait_mphy_pll_lock(hba);
	if (ret) {
		dev_err(hba->dev, "%s: M-PHY PLL lock timeout, UFS_MPHY_PU_CTRL=0x%08x\n",
			__func__, ufshcd_readl(hba, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL));
		return ret;
	}

	ufshcd_writel(hba, 0x1, UFS_PHY_MNG_BASE + UFS_MPHY_BKDR_CTRL);
	udelay(20);

	ufshcd_writel(hba, 0x00, UFS_ATOP_BASE + (0xC1 << 2));
	ufshcd_writel(hba, 0x00, UFS_ATOP_BASE + (0xC2 << 2));
	udelay(20);

	ufshcd_writel(hba, 0x0, UFS_PHY_MNG_BASE + UFS_MPHY_BKDR_CTRL);
	udelay(20);

	return 0;
}

static int ufs_spacemit_uniprov1p6_init(struct ufs_hba *hba)
{
	static const struct ufs_spacemit_dme_setting init_seq[] = {
		{ "PA_TXHSG1SYNCLENGTH", UIC_ARG_MIB(PA_TXHSG1SYNCLENGTH), 0x4f },
		{ "PA_TXHSG1PREPARELENGTH", UIC_ARG_MIB(PA_TXHSG1PREPARELENGTH), 0x0f },
		{ "PA_TXHSG2SYNCLENGTH", UIC_ARG_MIB(PA_TXHSG2SYNCLENGTH), 0x4f },
		{ "PA_TXHSG2PREPARELENGTH", UIC_ARG_MIB(PA_TXHSG2PREPARELENGTH), 0x0f },
		{ "PA_TXHSG3SYNCLENGTH", UIC_ARG_MIB(PA_TXHSG3SYNCLENGTH), 0x4f },
		{ "PA_TXHSG3PREPARELENGTH", UIC_ARG_MIB(PA_TXHSG3PREPARELENGTH), 0x0f },
		{ "PA_TXMK2EXTENSION", UIC_ARG_MIB(PA_TXMK2EXTENSION), 0x0 },
		{ "PA_PEERSCRAMBLING", UIC_ARG_MIB(PA_PEERSCRAMBLING), 0x1 },
		{ "PA_TXSKIP", UIC_ARG_MIB(PA_TXSKIP), 0x1 },
		{ "PA_TXSKIPPERIOD", UIC_ARG_MIB(PA_TXSKIPPERIOD), 250 },
		{ "PA_LOCAL_TX_LCC_ENABLE", UIC_ARG_MIB(PA_LOCAL_TX_LCC_ENABLE), 0x0 },
		{ "PA_PEER_TX_LCC_ENABLE", UIC_ARG_MIB(PA_PEER_TX_LCC_ENABLE), 0x0 },
		{ "PA_SCRAMBLING", UIC_ARG_MIB(PA_SCRAMBLING), 0x1 },
		{ "PA_GRANULARITY", UIC_ARG_MIB(PA_GRANULARITY), 0x1 },
		{ "PA_MK2EXTENSIONGUARDBAND", UIC_ARG_MIB(PA_MK2EXTENSIONGUARDBAND), 0x0 },
		{ "PA_STALLNOCONFIGTIME", UIC_ARG_MIB(PA_STALLNOCONFIGTIME), 15 },
		{ "PA_TACTIVATE", UIC_ARG_MIB(PA_TACTIVATE), 0x64 },
		{ "PA_TXTRAILINGCLOCKS", UIC_ARG_MIB(PA_TXTRAILINGCLOCKS), 0x64 },
		{ "RX_LS_PREPARELEN_TIME RX0",
		  UIC_ARG_MIB_SEL(RX_LS_PRE_LEN_CAP, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0x0b },
		{ "RX_LS_PREPARELEN_TIME RX1",
		  UIC_ARG_MIB_SEL(RX_LS_PRE_LEN_CAP, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0x0b },
		{ "RX_HIBERNATE_BKEN RX0",
		  UIC_ARG_MIB_SEL(RX_LANE_HB8_BKDOOR_ATTR, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0x9f },
		{ "RX_HIBERNATE_BKEN RX1",
		  UIC_ARG_MIB_SEL(RX_LANE_HB8_BKDOOR_ATTR, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0x9f },
		{ "PWM_BURST_closure_length RX0",
		  UIC_ARG_MIB_SEL(RX_PWRM_CLOSURE_LEN_CAP, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 15 },
		{ "PWM_BURST_closure_length RX1",
		  UIC_ARG_MIB_SEL(RX_PWRM_CLOSURE_LEN_CAP, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 15 },
		{ "min_stall_not_config_time RX0",
		  UIC_ARG_MIB_SEL(RX_MIN_STALL_CAP, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0xff },
		{ "min_stall_not_config_time RX1",
		  UIC_ARG_MIB_SEL(RX_MIN_STALL_CAP, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0xff },
		{ "TX HB8_TIME CAP TX0",
		  UIC_ARG_MIB_SEL(TX_HIBERN8TIME_CAP, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)), 0x64 },
		{ "TX HB8_TIME CAP TX1",
		  UIC_ARG_MIB_SEL(TX_HIBERN8TIME_CAP, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(1)), 0x64 },
		{ "RX HB8_TIME CAP RX0",
		  UIC_ARG_MIB_SEL(RX_HIBERN8TIME_CAP, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0x64 },
		{ "RX HB8_TIME CAP RX1",
		  UIC_ARG_MIB_SEL(RX_HIBERN8TIME_CAP, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0x64 },
		{ "TX EQ 3DB",
		  UIC_ARG_MIB_SEL(ANA_EQ_CTRL_REG_ATTR, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)), 0x5 },
		{ "RX garbage cnt RX0",
		  UIC_ARG_MIB_SEL(RX_GARBAGE_COUNT_OFFSET, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)), 0x9f },
		{ "RX garbage cnt RX1",
		  UIC_ARG_MIB_SEL(RX_GARBAGE_COUNT_OFFSET, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)), 0x9f },
		{ "UFS_HCLKDIV_REG", UIC_ARG_MIB(UFS_HCLKDIV_REG), 0xfc },
	};
	int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(init_seq); i++) {
		err = ufs_spacemit_dme_set_checked(hba, init_seq[i].attr_sel,
						   init_seq[i].value,
						   init_seq[i].name);
		if (err)
			return err;
	}

	dev_info(hba->dev, "UniPro v1.6 init completed\n");

	return 0;
}

static void ufs_spacemit_set_dev_cap(struct ufs_host_params *ufs_spacemit_cap)
{
	if (!ufs_spacemit_cap)
		return;

	memset(ufs_spacemit_cap, 0, sizeof(struct ufs_host_params));
	ufs_spacemit_cap->tx_lanes = UFS_SPACEMIT_LIMIT_NUM_LANES_TX;
	ufs_spacemit_cap->rx_lanes = UFS_SPACEMIT_LIMIT_NUM_LANES_RX;
	ufs_spacemit_cap->hs_rx_gear = UFS_SPACEMIT_LIMIT_HSGEAR_RX;
	ufs_spacemit_cap->hs_tx_gear = UFS_SPACEMIT_LIMIT_HSGEAR_TX;
	ufs_spacemit_cap->pwm_rx_gear = UFS_SPACEMIT_LIMIT_PWMGEAR_RX;
	ufs_spacemit_cap->pwm_tx_gear = UFS_SPACEMIT_LIMIT_PWMGEAR_TX;
	ufs_spacemit_cap->rx_pwr_pwm = UFS_SPACEMIT_LIMIT_RX_PWR_PWM;
	ufs_spacemit_cap->tx_pwr_pwm = UFS_SPACEMIT_LIMIT_TX_PWR_PWM;
	ufs_spacemit_cap->rx_pwr_hs = UFS_SPACEMIT_LIMIT_RX_PWR_HS;
	ufs_spacemit_cap->tx_pwr_hs = UFS_SPACEMIT_LIMIT_TX_PWR_HS;
	ufs_spacemit_cap->hs_rate = UFS_SPACEMIT_LIMIT_HS_RATE;
	ufs_spacemit_cap->desired_working_mode = UFS_HS_MODE;
}

static int ufs_spacemit_link_startup_pre_change(struct ufs_hba *hba)
{
	uint32_t reg_val;
	u32 real_sysclk;
	int err;

	/*mphy_init*/
	err = ufs_spacemit_mphy_init(hba);
	if (err)
		return err;

	/* unipro v1p6 init */
	err = ufs_spacemit_uniprov1p6_init(hba);
	if (err)
		return err;

	real_sysclk = spacemit_clock_freq > 0 ?
		      spacemit_clock_freq / 1000000 :
		      ufs_spacemit_get_sys1clk_1us(hba);
	if (!real_sysclk) {
		dev_err(hba->dev, "%s: invalid sysclk\n", __func__);
		return -EINVAL;
	}

	ufshcd_writel(hba, real_sysclk, UFS_SYS1CLK_1US);

	reg_val = 1000 / real_sysclk;
	if ((1000 % real_sysclk) > (real_sysclk / 2))
		reg_val += 1;
	reg_val <<= 10;
	ufshcd_writel(hba, reg_val, UFS_TX_SYMBOL_CLK_NS_US);

	reg_val = real_sysclk * 100000;
	reg_val &= ~0xf;
	ufshcd_writel(hba, reg_val, UFS_PA_LINK_STARTUP_TIMER);

	return 0;
}

static int ufs_spacemit_link_startup_post_change(struct ufs_hba *hba)
{
	u32 tx_lanes;
	u32 rx1_fsm_status;
	struct ufs_spacemit_host *host = ufshcd_get_variant(hba);

	ufs_spacemit_get_unipro_ver(hba);
	ufshcd_dme_get(hba,
		       UIC_ARG_MIB_SEL(0xC1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)),
		       &rx1_fsm_status);

	if (rx1_fsm_status == 0xe &&
	    host->remote_unipro_ver < UFS_UNIPRO_VER_1_8) {
		ufshcd_dme_set(hba,
			       UIC_ARG_MIB_SEL(0xe8, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
			       0x97);
		ufshcd_dme_set(hba,
			       UIC_ARG_MIB_SEL(0xe8, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
			       0xd7);
		ufshcd_dme_set(hba,
			       UIC_ARG_MIB_SEL(0xe8, UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)),
			       0x17);

		ufshcd_dme_get(hba,
			       UIC_ARG_MIB_SEL(0xC1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)),
			       &rx1_fsm_status);
		dev_err(hba->dev, "ufs: send dummy frame, rx1_fsm_status:0x%x\n",
			rx1_fsm_status);
	}

	/* DL_AFC0REQTIMEOUTVAL_MAX */
	ufshcd_dme_set(hba, UIC_ARG_MIB(DL_AFC0REQTIMEOUTVAL), UFS_DL_AFC0REQTIMEOUTVAL_MAX);

	return ufs_spacemit_get_connected_tx_lanes(hba, &tx_lanes);
}

static int ufs_spacemit_link_startup_notify(struct ufs_hba *hba,
					       enum ufs_notify_change_status status)
{
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		err = ufs_spacemit_link_startup_pre_change(hba);
		break;
	case POST_CHANGE:
		err = ufs_spacemit_link_startup_post_change(hba);
		break;
	default:
		break;
	}

	if (err)
		dev_err(hba->dev, "%s: status=%d failed: %d\n", __func__, status, err);

	return err;
}

#ifdef CONFIG_PM
static int ufs_spacemit_runtime_suspend(struct device *dev)
{
	int ret;

	ret = ufshcd_runtime_suspend(dev);
	if (ret)
		dev_err(dev, "Runtime suspend failed: %d\n", ret);

	return ret;
}

static int ufs_spacemit_runtime_resume(struct device *dev)
{
	int ret;

	ret = ufshcd_runtime_resume(dev);
	if (ret)
		dev_err(dev, "Runtime resume failed: %d\n", ret);

	return ret;
}
#endif

static int ufs_spacemit_pwr_change_notify(struct ufs_hba *hba,
					     enum ufs_notify_change_status status,
					     const struct ufs_pa_layer_attr *dev_max_params,
					     struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_spacemit_host *host = ufshcd_get_variant(hba);
	struct ufs_host_params ufs_spacemit_cap;
	int ret = 0;

	if (!dev_req_params) {
		dev_err(hba->dev, "dev_req_params is NULL\n");
		return -EINVAL;
	}

	switch (status) {
	case PRE_CHANGE:
		ufs_spacemit_set_dev_cap(&ufs_spacemit_cap);
		ret = ufshcd_negotiate_pwr_params(&ufs_spacemit_cap, dev_max_params,
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

		ret = ufs_spacemit_wait_mphy_pll_lock(hba);
		if (ret) {
			dev_err(hba->dev,
				"%s: M-PHY PLL lock timeout after power mode change, UFS_MPHY_PU_CTRL=0x%08x\n",
				__func__, ufshcd_readl(hba, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL));
			return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int ufs_spacemit_quirk_host_pa_saveconfigtime(struct ufs_hba *hba)
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

static int ufs_spacemit_apply_dev_quirks(struct ufs_hba *hba)
{
	int err = 0;

	if (hba->dev_quirks & UFS_DEVICE_QUIRK_HOST_PA_SAVECONFIGTIME)
		err = ufs_spacemit_quirk_host_pa_saveconfigtime(hba);

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

	err = ufs_spacemit_wait_mphy_pll_lock(hba);
	if (err) {
		dev_err(hba->dev,
			"%s: M-PHY PLL lock timeout after applying device quirks, UFS_MPHY_PU_CTRL=0x%08x\n",
			__func__, ufshcd_readl(hba, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL));
		return err;
	}

	return err;
}

static __maybe_unused void ufs_spacemit_get_unipro_ver(struct ufs_hba *hba)
{
	struct ufs_spacemit_host *host = ufshcd_get_variant(hba);

	if (ufshcd_dme_get(hba, UIC_ARG_MIB(PA_LOCALVERINFO), &host->unipro_ver))
		host->unipro_ver = 0;
	if (ufshcd_dme_get(hba, UIC_ARG_MIB(PA_REMOTEVERINFO), &host->remote_unipro_ver))
		host->remote_unipro_ver = 0;
}

/**
 * ufs_spacemit_advertise_quirks - advertise the known Spacemit K3 UFS controller quirks
 * @hba: host controller instance
 *
 * Spacemit K3 UFS host controller might have some non standard behaviours (quirks)
 * than what is specified by UFSHCI specification. Advertise all such
 * quirks to standard UFS host controller driver so standard takes them into
 * account.
 */
static void ufs_spacemit_advertise_quirks(struct ufs_hba *hba)
{
	/* break auto hibern8 */
	hba->quirks |= UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8;
}

static void ufs_spacemit_set_caps(struct ufs_hba *hba)
{
	/* support clock-gating */
	/* hba->caps |= UFSHCD_CAP_CLK_GATING; */

	/* support inline encryption */
	/* hba->caps |= UFSHCD_CAP_CRYPTO; */

	/* support write booster */
	/* hba->caps |= UFSHCD_CAP_WB_EN; */

	/* support runtime autosuspend */
	/* hba->caps |= UFSHCD_CAP_RPM_AUTOSUSPEND; */
}

static void ufs_spacemit_config_scsi_dev(struct scsi_device *sdev)
{
	struct ufs_hba *hba = shost_priv(sdev->host);

	/* Serialize I/O to avoid command loss under high-concurrency stress. */
	scsi_change_queue_depth(sdev, 1);
	dev_info(hba->dev, "lu %llu scsi queue depth limited to %u\n",
		 sdev->lun, sdev->queue_depth);
}

/**
 * ufs_spacemit_setup_xfer_req
 * @hba: host controller instance
 * tag: current task slot index
 * is_scsi_cmd: scsi command or not
 */
static void ufs_spacemit_setup_xfer_req(struct ufs_hba *hba, int tag, bool is_scsi_cmd)
{
	/*
	 * Ensure UTRD/UPIU writes are visible before the core rings doorbell.
	 * This mitigates command loss under high-concurrency random IO.
	 */
	wmb();
}

/**
 * ufs_spacemit_setup_clocks - restore ufs-aclk rate after clocks turn on
 * @hba: host controller instance
 * @on: If true, enable clocks else disable them.
 * @status: PRE_CHANGE or POST_CHANGE notify
 *
 * Returns 0 on success, non-zero on failure.
 */
static int ufs_spacemit_setup_clocks(struct ufs_hba *hba, bool on,
					enum ufs_notify_change_status status)
{
	struct ufs_clk_info *clki = NULL;
	struct ufs_clk_info *iter;
	unsigned long rate;
	int ret;

	if (status != POST_CHANGE || !on)
		return 0;

	list_for_each_entry(iter, &hba->clk_list_head, list) {
		if (iter->name && !strcmp(iter->name, "ufs-aclk")) {
			clki = iter;
			break;
		}
	}
	if (!clki || !clki->clk)
		return 0;

	rate = spacemit_clock_freq ?: clki->curr_freq ?:
	       clki->max_freq ?: clk_get_rate(clki->clk);
	if (!rate)
		return 0;

	ret = clk_set_rate(clki->clk, rate);
	if (ret) {
		dev_err(hba->dev, "failed to set ufs-aclk to %luHz: %d\n",
			rate, ret);
		return ret;
	}

	clki->curr_freq = rate;

	return 0;
}

/**
 * ufs_spacemit_platform_init
 * @pdev: platform device pointer
 *
 * Prepare the clk source and reset ufs_aclk
 */
static int ufs_spacemit_platform_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk *ufs_aclk;
	struct reset_control *rst;
	u32 freq_table[2];
	u32 clock_freq = 0;
	unsigned long rate;
	int ret;

	ufs_aclk = devm_clk_get_optional(dev, "ufs-aclk");
	if (IS_ERR(ufs_aclk)) {
		dev_err(dev, "Failed to get %s: %ld\n",
			"ufs-aclk", PTR_ERR(ufs_aclk));
		return PTR_ERR(ufs_aclk);
	}

	rst = reset_control_get_exclusive(dev, "ufs-aclk-rst");
	if (IS_ERR(rst)) {
		dev_err_probe(dev, PTR_ERR(rst),
			      "Failed to get required reset control\n");
		return PTR_ERR(rst);
	}

	ret = reset_control_assert(rst);
	if (ret) {
		dev_err(dev, "Reset assert failed: %d\n", ret);
		goto out_put_reset;
	}
	udelay(1);
	ret = reset_control_deassert(rst);
	if (ret) {
		dev_err(dev, "Reset deassert failed: %d\n", ret);
		goto out_put_reset;
	}
	reset_control_put(rst);

	if (!ufs_aclk)
		return 0;

	rate = 0;
	if (dev->of_node &&
	    !of_property_read_u32(dev->of_node, "clock-freq", &clock_freq) &&
	    clock_freq)
		rate = clock_freq;
	else if (dev->of_node &&
		 !of_property_read_u32_array(dev->of_node, "freq-table-hz",
					     freq_table, ARRAY_SIZE(freq_table)) &&
		 freq_table[0])
		rate = freq_table[0];

	if (!rate)
		rate = clk_get_rate(ufs_aclk);

	spacemit_clock_freq = rate;

	ret = clk_set_rate(ufs_aclk, rate);
	if (ret) {
		dev_err(dev, "Failed to set %s rate to %luHz: %d\n",
			"ufs-aclk", rate, ret);
		return ret;
	}

	return 0;

out_put_reset:
	reset_control_put(rst);
	return ret;
}

/**
 * ufs_spacemit_init - init phy and prepare clk
 * @hba: host controller instance
 */
static int ufs_spacemit_init(struct ufs_hba *hba)
{
	int err = 0;
	struct device *dev = hba->dev;
	struct ufs_spacemit_host *host;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host) {
		err = -ENOMEM;
		dev_err(dev, "%s: no memory for Spacemit K3 UFS host\n", __func__);
		goto out;
	}

	host->rst = devm_reset_control_get_exclusive(dev, "ufs-aclk-rst");
	if (IS_ERR(host->rst)) {
		err = PTR_ERR(host->rst);
		dev_err_probe(dev, err, "Failed to get required reset control\n");
		goto out;
	}

	/* Make a two way bind between the spacemit k3 host and the hba */
	host->hba = hba;
	host->saved_spm_lvl = -1;
	ufshcd_set_variant(hba, host);
	ufs_spacemit_set_caps(hba);
	ufs_spacemit_advertise_quirks(hba);

	/*
	 * Keep the link active by default. Standby, where the UFS power is lost
	 * externally, overrides system PM to UFS_PM_LVL_5 in prepare().
	 */
	hba->rpm_lvl = UFS_PM_LVL_2;
	hba->spm_lvl = UFS_PM_LVL_2;

	err = ufshcd_vops_phy_initialization(host->hba);
out:
	return err;
}

static int __maybe_unused ufs_spacemit_axi_reset(struct ufs_hba *hba)
{
	int ret = 0;
	struct ufs_spacemit_host *host = ufshcd_get_variant(hba);
	struct ufs_reg_snapshot save_regs;
	struct ufs_clk_info *clki;
	struct ufs_clk_info *iter;
	struct device *dev = hba->dev;
	bool clk_was_enabled = false;
	unsigned long rate = 0;

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

	clki = NULL;
	list_for_each_entry(iter, &hba->clk_list_head, list) {
		if (iter->name && !strcmp(iter->name, "ufs-aclk")) {
			clki = iter;
			break;
		}
	}
	if (!clki || !clki->clk) {
		dev_err(dev, "Failed to find %s in clock list\n",
			"ufs-aclk");
		ret = -ENOENT;
		goto out;
	}

	/* Disable clock before reset */
	clk_was_enabled = clki->enabled || __clk_is_enabled(clki->clk);
	if (clk_was_enabled) {
		clk_disable_unprepare(clki->clk);
		clki->enabled = false;
	}

	dev_dbg(dev, "%s: assert ufs-aclk-rst\n", __func__);
	ret = reset_control_assert(host->rst);
	if (ret) {
		dev_err(dev, "%s: reset assert failed: %d\n", __func__, ret);
		goto out;
	}
	usleep_range(10, 20);

	ret = reset_control_deassert(host->rst);
	if (ret) {
		dev_err(dev, "%s: reset deassert failed: %d\n", __func__, ret);
		goto out;
	}
	dev_dbg(dev, "%s: deassert ufs-aclk-rst done\n", __func__);

	rate = spacemit_clock_freq ?: clki->curr_freq ?:
	       clki->max_freq ?: clk_get_rate(clki->clk);
	if (rate) {
		ret = clk_set_rate(clki->clk, rate);
		if (ret) {
			dev_err(dev, "%s: %s clk set rate(%uHz) failed, %d\n",
				__func__, clki->name, (u32)rate, ret);
			goto out;
		}
		clki->curr_freq = rate;
	}

	ret = clk_prepare_enable(clki->clk);
	if (ret) {
		dev_err(hba->dev, "%s: %s prepare enable failed, %d\n", __func__, clki->name, ret);
		goto out;
	}
	clki->enabled = 1;
	dev_dbg(dev, "%s: restore host regs\n", __func__);

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
	dev_dbg(dev, "%s: done\n", __func__);

out:
	if (ret && clk_was_enabled && clki && clki->clk) {
		int restore_ret;

		restore_ret = clk_prepare_enable(clki->clk);
		if (restore_ret) {
			dev_err(dev, "%s: failed to restore %s clock, %d\n", __func__,
				clki->name, restore_ret);
		} else {
			clki->enabled = true;
			dev_dbg(dev, "%s: restored clock %s after reset failure\n", __func__,
				clki->name);
		}
	}

	return ret;
}

/**
 * ufs_spacemit_device_reset - Toggle device reset line
 * @hba: per-adapter instance
 *
 * Toggles the reset line to reset the attached UFS device.
 * On first call, skip reset. On subsequent calls, perform full reset.
 *
 * Returns: 0 on success
 */
static int ufs_spacemit_device_reset(struct ufs_hba *hba)
{
	static bool is_first_init = true;

	if (is_first_init) {
		is_first_init = false;
	} else {
		ufshcd_set_link_off(hba);

		ufshcd_writel(hba, 0x000, UFS_PHY_MNG_BASE + UFS_DEVICE_IO_CTRL);
		mdelay(5);

		ufshcd_writel(hba, 0x000, UFS_PHY_MNG_BASE + UFS_MPHY_RST_CTRL);
		mdelay(5);

		ufshcd_writel(hba, 0x000, UFS_PHY_MNG_BASE + UFS_MPHY_PU_CTRL);
		mdelay(5);

		dev_dbg(hba->dev, "ufs: ufs_spacemit_device_reset done\n");
	}

	return 0;
}

/**
 * ufs_spacemit_event_notify - Handle UFS error events
 * @hba: host controller instance
 * @evt: event type
 * @data: event-specific data
 *
 * Observes UFS core error events without changing recovery flow.
 */
static void ufs_spacemit_event_notify(struct ufs_hba *hba, enum ufs_event_type evt, void *data)
{
	u32 val = data ? *(u32 *)data : 0;

	if (evt == UFS_EVT_PA_ERR) {
		dev_dbg_ratelimited(hba->dev,
				    "ufs: event_notify, evt:%d, INT errors:0x%x, PA 0x38:0x%x\n",
				    evt, hba->errors, val);
		return;
	}
	if (evt == UFS_EVT_DL_ERR) {
		dev_dbg_ratelimited(hba->dev,
				    "ufs: event_notify, evt:%d, INT errors:0x%x, DL 0x3C:0x%x\n",
				    evt, hba->errors, val);
		return;
	}
	if (evt == UFS_EVT_ABORT) {
		dev_dbg_ratelimited(hba->dev,
				    "ufs: event_notify, evt:%d, INT errors:0x%x\n",
				    evt, hba->errors);
	}
}

/**
 * ufs_spacemit_hibern8_notify - Handle hibernate enter/exit
 * @hba: host controller instance
 * @cmd: UIC command (HIBER_ENTER or HIBER_EXIT)
 * @status: notification status
 *
 * Manages M-PHY power state during hibernate transitions.
 */
static void ufs_spacemit_hibern8_notify(struct ufs_hba *hba, enum uic_cmd_dme cmd,
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

			if (timeout <= 0) {
				dev_err(hba->dev, "PLL lock timeout on hibern8 exit\n");
				return;
			}

			mdelay(1);
			ufshcd_dme_set(hba, UIC_ARG_MIB(0xdd), 0x57);
			mdelay(1);
			ufshcd_dme_set(hba, UIC_ARG_MIB(0xe8), 0x57);
		}
	}

	if (status == PRE_CHANGE) {
		if (cmd == UIC_CMD_DME_HIBER_ENTER) {
			ufshcd_dme_set(hba,
				       UIC_ARG_MIB_SEL(0xf1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)),
				       0x84);
			ufshcd_dme_set(hba,
				       UIC_ARG_MIB_SEL(0xf1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)),
				       0x84);
			ufshcd_dme_set(hba,
				       UIC_ARG_MIB_SEL(0xf1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)),
				       0x85);
			ufshcd_dme_set(hba,
				       UIC_ARG_MIB_SEL(0xf1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)),
				       0x85);
		}
	}

	if (status == POST_CHANGE) {
		if (cmd == UIC_CMD_DME_HIBER_ENTER) {
			ufs_spacemit_check_hibern8(hba);
			ufshcd_dme_set(hba,
				       UIC_ARG_MIB_SEL(0xf1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)),
				       0x84);
			ufshcd_dme_set(hba,
				       UIC_ARG_MIB_SEL(0xf1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)),
				       0x84);
			ufshcd_dme_set(hba,
				       UIC_ARG_MIB_SEL(0xf1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)),
				       0x80);
			ufshcd_dme_set(hba,
				       UIC_ARG_MIB_SEL(0xf1, UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)),
				       0x80);

			mdelay(1);
			ufshcd_dme_set(hba, UIC_ARG_MIB(0xdd), 0x57);
			mdelay(1);
			ufshcd_dme_set(hba, UIC_ARG_MIB(0xdd), 0xd7);
			mdelay(1);
			ufshcd_dme_set(hba, UIC_ARG_MIB(0xe8), 0x57);
			mdelay(1);
			ufshcd_dme_set(hba, UIC_ARG_MIB(0xe8), 0xd7);
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
 * ufs_spacemit_hce_enable_notify - Configure HCE enable sequence
 * @hba: host controller instance
 * @status: notification status (PRE_CHANGE or POST_CHANGE)
 *
 * Configures host controller enable with proper sequencing.
 * Handles crypto enable if supported.
 *
 * Returns: 0 on success
 */
static int ufs_spacemit_hce_enable_notify(struct ufs_hba *hba,
					     enum ufs_notify_change_status status)
{
	static bool is_first_hce = true;
	u32 enable_val, val;

	if (status == PRE_CHANGE) {
		enable_val = CONTROLLER_ENABLE;

		if (hba->caps & UFSHCD_CAP_CRYPTO)
			enable_val = CRYPTO_GENERAL_ENABLE | CONTROLLER_ENABLE;

		if (is_first_hce) {
			is_first_hce = false;
		} else {
			val = ufshcd_readl(hba, REG_CONTROLLER_ENABLE);
			if (val == enable_val) {
				ufshcd_writel(hba,
					      enable_val & (1 << CONTROLLER_ENABLE),
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
 * struct ufs_hba_spacemit_vops - UFS Spacemit specific variant operations
 *
 * The variant operations configure the necessary controller and PHY
 * handshake during initialization.
 */
static const struct ufs_hba_variant_ops ufs_hba_spacemit_vops = {
	.name = "lark_ufs",
	.init = ufs_spacemit_init,
	.link_startup_notify = ufs_spacemit_link_startup_notify,
	.pwr_change_notify = ufs_spacemit_pwr_change_notify,
	.setup_clocks = ufs_spacemit_setup_clocks,
	.setup_xfer_req = ufs_spacemit_setup_xfer_req,
	.config_scsi_dev = ufs_spacemit_config_scsi_dev,
	.device_reset = ufs_spacemit_device_reset,
	.event_notify = ufs_spacemit_event_notify,
	.apply_dev_quirks = ufs_spacemit_apply_dev_quirks,
	.hibern8_notify = ufs_spacemit_hibern8_notify,
	.hce_enable_notify = ufs_spacemit_hce_enable_notify,
	.dbg_register_dump = ufs_spacemit_dump_host_regs,
};

static const struct of_device_id ufs_spacemit_of_match[] = {
	{ .compatible = "spacemit,k3-ufshcd", .data = &ufs_hba_spacemit_vops },
	{},
};
MODULE_DEVICE_TABLE(of, ufs_spacemit_of_match);

/**
 * ufs_spacemit_probe - probe routine of the driver
 * @pdev: pointer to Platform device handle
 *
 * Return zero for success and non-zero for failure
 */
static int ufs_spacemit_probe(struct platform_device *pdev)
{
	int err;
	const struct of_device_id *of_id;
	struct ufs_hba_variant_ops *vops;
	struct device *dev = &pdev->dev;

	of_id = of_match_node(ufs_spacemit_of_match, dev->of_node);
	if (!of_id) {
		dev_err(dev, "ufs: no matching of_node found\n");
		return -ENODEV;
	}

	err = ufs_spacemit_platform_init(pdev);
	if (err)
		return err;

	vops = (struct ufs_hba_variant_ops *)of_id->data;
	err = ufshcd_pltfrm_init(pdev, vops);
	if (err) {
		dev_err(dev, "ufs: ufshcd_pltfrm_init() failed %d\n", err);
	}

	return err;
}

/**
 * ufs_spacemit_remove - set driver_data of the device to NULL
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static void ufs_spacemit_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufshcd_remove(hba);
	pm_runtime_put(&(pdev)->dev);
}

static bool ufs_spacemit_standby_loses_power(void)
{
	return pm_suspend_target_state == PM_SUSPEND_STANDBY;
}

static int ufs_spacemit_suspend_prepare(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_spacemit_host *host = ufshcd_get_variant(hba);
	int ret;

	if (ufs_spacemit_standby_loses_power() && hba->spm_lvl < UFS_PM_LVL_5) {
		host->saved_spm_lvl = hba->spm_lvl;
		hba->spm_lvl = UFS_PM_LVL_5;
	}

	ret = ufshcd_suspend_prepare(dev);
	if (ret < 0 && host->saved_spm_lvl != -1) {
		hba->spm_lvl = host->saved_spm_lvl;
		host->saved_spm_lvl = -1;
	}

	return ret;
}

static void ufs_spacemit_resume_complete(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_spacemit_host *host = ufshcd_get_variant(hba);

	ufshcd_resume_complete(dev);

	if (host->saved_spm_lvl != -1) {
		hba->spm_lvl = host->saved_spm_lvl;
		host->saved_spm_lvl = -1;
	}
}

static const struct dev_pm_ops ufs_spacemit_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufshcd_system_suspend, ufshcd_system_resume)
	SET_RUNTIME_PM_OPS(ufs_spacemit_runtime_suspend, ufs_spacemit_runtime_resume, NULL)
	.prepare = ufs_spacemit_suspend_prepare,
	.complete = ufs_spacemit_resume_complete,
};

static struct platform_driver ufs_spacemit_platform_driver = {
	.probe	= ufs_spacemit_probe,
	.remove	= ufs_spacemit_remove,
	.driver	= {
		.name	= "ufshcd-spacemit",
		.pm	= &ufs_spacemit_pm_ops,
		.of_match_table = of_match_ptr(ufs_spacemit_of_match),
	},
};
module_platform_driver(ufs_spacemit_platform_driver);

MODULE_LICENSE("GPL v2");
