// SPDX-License-Identifier: GPL-2.0
/*
 * Spacemit Ethernet QoS glue driver
 *
 * Copyright (c) 2025, Spacemit Corporation.
 *
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>
#include <linux/mfd/syscon.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif
#include "stmmac.h"
#include "stmmac_platform.h"

#define DRIVER_NAME			"dwmac-spacemit-ethqos"

#define TUNING_CMD_LEN			50
#define CLK_PHASE_CNT			256
#define CLK_PHASE_REVERT		180

#define TXCLK_PHASE_DEFAULT		0
#define RXCLK_PHASE_DEFAULT		0

struct spacemit_ethqos {
	struct platform_device *pdev;
	struct plat_stmmacenet_data *plat;
	struct regmap *apmu;
	u32 ctrl_off;
	u32 dline_off;
	phy_interface_t phy_iface;
	u8 tx_clk_phase;
	u8 rx_clk_phase;
	u8 clk_tuning_way;
	struct clk *tx_clk;
	struct clk *phy_clk;
	int speed;
	bool clk_tuning_enable;
	bool tx_clk_from_soc;
	bool phy_clk_from_soc;
	bool wol_irq_enable;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dbg_dir;
	struct dentry *dbg_clk_tuning;
#endif
};

/**
 * struct spacemit_ethqos_ops - Spacemit platform glue ops
 * @devm_glue_parse_dt:        Parse platform data from DT (devm-managed)
 * @devm_glue_bind_ops:        Install platform callbacks (devm-rollback)
 * @devm_glue_config_plat:     Program platform registers (devm-managed)
 */
struct spacemit_ethqos_ops {
	int (*devm_glue_parse_dt)(struct platform_device *pdev,
				  struct spacemit_ethqos *eqos);
	int (*devm_glue_bind_ops)(struct spacemit_ethqos *eqos);
	int (*devm_glue_config_plat)(struct spacemit_ethqos *eqos);
};

static int devm_spacemit_glue_init(struct platform_device *pdev,
				   struct plat_stmmacenet_data *plat_dat,
				   const struct spacemit_ethqos_ops *ops)
{
	struct device *dev = &pdev->dev;
	struct spacemit_ethqos *eqos;
	int ret;

	eqos = devm_kzalloc(dev, sizeof(*eqos), GFP_KERNEL);
	if (!eqos)
		return -ENOMEM;

	eqos->pdev = pdev;
	eqos->plat = plat_dat;
	plat_dat->bsp_priv = eqos;

	if (ops->devm_glue_parse_dt) {
		ret = ops->devm_glue_parse_dt(pdev, eqos);
		if (ret)
			return dev_err_probe(dev, ret, "glue layer: dt parse failed\n");
	}

	if (ops->devm_glue_bind_ops) {
		ret = ops->devm_glue_bind_ops(eqos);
		if (ret)
			return dev_err_probe(dev, ret, "glue layer: bind ops failed\n");
	}

	if (ops->devm_glue_config_plat) {
		ret = ops->devm_glue_config_plat(eqos);
		if (ret)
			return dev_err_probe(dev, ret, "glue layer: config failed\n");
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * K3 SoC-specific macros
 * -----------------------------------------------------------------------------
 */
#define TX_PHASE			1
#define RX_PHASE			0

#define PHY_INTF_MODE_MASK		GENMASK(4, 3)

#define PHY_INTF_RMII			FIELD_PREP(PHY_INTF_MODE_MASK, 0x0)
#define PHY_INTF_RGMII			FIELD_PREP(PHY_INTF_MODE_MASK, 0x1)
#define PHY_INTF_MII			FIELD_PREP(PHY_INTF_MODE_MASK, 0x3)

/* only valid for rmii, invert tx clk */
#define RMII_TX_CLK_SEL			BIT(6)
/* only valid for rmii, invert rx clk */
#define RMII_RX_CLK_SEL			BIT(7)

#define LPI_IRQ_EN			BIT(9)
#define WAKE_IRQ_EN			BIT(12)
#define AXI_SINGLE_ID			BIT(13)

/* dline register bits */
#define EMAC_RX_DLINE_EN		BIT(0)
#define EMAC_TX_DLINE_EN		BIT(16)

#define RMII_TX_PHASE_MASK		GENMASK(18, 16)
#define RMII_RX_PHASE_MASK		GENMASK(22, 20)

#define RGMII_RX_PHASE_MASK		GENMASK(22, 20)
#define RGMII_TX_PHASE_MASK		GENMASK(26, 24)

#define EMAC_RX_DLINE_CODE_MASK		GENMASK(15, 8)
#define EMAC_TX_DLINE_CODE_MASK		GENMASK(31, 24)

enum clk_tuning_way {
	/* fpga clk tuning register */
	CLK_TUNING_BY_REG,
	/* zebu/evb rgmii delayline register */
	CLK_TUNING_BY_DLINE,
	/* evb rmii only revert tx/rx clock for clk tuning */
	CLK_TUNING_BY_CLK_REVERT,
	CLK_TUNING_MAX,
};

static int clk_phase_rmii_set(struct spacemit_ethqos *eqos, bool is_tx)
{
	struct device *dev = &eqos->pdev->dev;
	u32 mask, val, phase;
	int ret;

	switch (eqos->clk_tuning_way) {
	case CLK_TUNING_BY_REG:
		if (is_tx) {
			mask = RMII_TX_PHASE_MASK;
			val  = FIELD_PREP(RMII_TX_PHASE_MASK, eqos->tx_clk_phase);
		} else {
			mask = RMII_RX_PHASE_MASK;
			val  = FIELD_PREP(RMII_RX_PHASE_MASK, eqos->rx_clk_phase);
		}
		ret = regmap_update_bits(eqos->apmu, eqos->ctrl_off, mask, val);
		break;

	case CLK_TUNING_BY_CLK_REVERT: {
		mask = is_tx ? RMII_TX_CLK_SEL : RMII_RX_CLK_SEL;
		phase = is_tx ? eqos->tx_clk_phase : eqos->rx_clk_phase;
		val = (phase == CLK_PHASE_REVERT) ? mask : 0;
		ret = regmap_update_bits(eqos->apmu, eqos->ctrl_off, mask, val);
		break;
	}

	default:
		dev_err(dev, "invalid clk tuning way: %d\n", eqos->clk_tuning_way);
		return -EINVAL;
	}

	if (ret < 0)
		dev_err(dev, "failed to update RMII %s phase (ret=%d)\n",
			is_tx ? "tx" : "rx", ret);

	return ret;
}

static int clk_phase_rgmii_set(struct spacemit_ethqos *eqos, bool is_tx)
{
	struct device *dev = &eqos->pdev->dev;
	u32 mask, val;
	int ret;

	switch (eqos->clk_tuning_way) {
	case CLK_TUNING_BY_REG:
		if (is_tx) {
			mask = RGMII_TX_PHASE_MASK;
			val  = FIELD_PREP(RGMII_TX_PHASE_MASK, eqos->tx_clk_phase);
		} else {
			mask = RGMII_RX_PHASE_MASK;
			val  = FIELD_PREP(RGMII_RX_PHASE_MASK, eqos->rx_clk_phase);
		}
		ret = regmap_update_bits(eqos->apmu, eqos->ctrl_off, mask, val);
		break;

	case CLK_TUNING_BY_DLINE:
		if (is_tx) {
			mask = EMAC_TX_DLINE_CODE_MASK;
			val  = FIELD_PREP(EMAC_TX_DLINE_CODE_MASK, eqos->tx_clk_phase);
		} else {
			mask = EMAC_RX_DLINE_CODE_MASK;
			val  = FIELD_PREP(EMAC_RX_DLINE_CODE_MASK, eqos->rx_clk_phase);
		}
		ret = regmap_update_bits(eqos->apmu, eqos->dline_off, mask, val);
		break;

	default:
		dev_err(dev, "invalid clk tuning way: %d\n", eqos->clk_tuning_way);
		return -EINVAL;
	}

	if (ret < 0)
		dev_err(dev, "failed to update RGMII %s phase (ret=%d)\n",
			is_tx ? "tx" : "rx", ret);

	return ret;
}

static int clk_phase_set(struct spacemit_ethqos *eqos, bool is_tx)
{
	if (!eqos->clk_tuning_enable)
		return 0;

	if (eqos->phy_iface == PHY_INTERFACE_MODE_MII)
		return 0;

	if (phy_interface_mode_is_rgmii(eqos->phy_iface))
		return clk_phase_rgmii_set(eqos, is_tx);
	else
		return clk_phase_rmii_set(eqos, is_tx);
}

static int k3_delayline_init(struct spacemit_ethqos *eqos)
{
	u32 mask, val;
	int ret;

	/*
	 * On K3, TX/RX delayline must be enabled for reliable DMA init.
	 * This is required for all phy-modes (rgmii/rmii/mii).
	 * Also clear delay codes to remove any prior configuration residue.
	 */
	mask = EMAC_TX_DLINE_EN | EMAC_RX_DLINE_EN |
	       EMAC_TX_DLINE_CODE_MASK | EMAC_RX_DLINE_CODE_MASK;

	val = EMAC_TX_DLINE_EN | EMAC_RX_DLINE_EN;

	ret = regmap_update_bits(eqos->apmu, eqos->dline_off,
				 mask, val);
	if (ret)
		dev_err(&eqos->pdev->dev, "failed to init delayline\n");

	return ret;
}

static int k3_eqos_iface_config(struct spacemit_ethqos *eqos)
{
	struct device *dev = &eqos->pdev->dev;
	phy_interface_t iface = eqos->phy_iface;
	u32 val;
	int ret;

	switch (iface) {
	case PHY_INTERFACE_MODE_MII:
		val = PHY_INTF_MII;
		break;

	case PHY_INTERFACE_MODE_RMII:
		val = PHY_INTF_RMII;
		break;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = PHY_INTF_RGMII;
		break;

	default:
		dev_warn(dev, "unsupported phy-mode: %s\n", phy_modes(iface));
		return -EINVAL; /* don't write unexpected bits */
	}
	ret = regmap_update_bits(eqos->apmu, eqos->ctrl_off,
				 PHY_INTF_MODE_MASK, val);
	if (!ret)
		dev_info(dev, "phy-mode=%s val=0x%08x\n", phy_modes(iface), val);

	return ret;
}

static int k3_eqos_wol_config(struct spacemit_ethqos *eqos)
{
	u32 val = eqos->wol_irq_enable ? WAKE_IRQ_EN : 0;

	return regmap_update_bits(eqos->apmu, eqos->ctrl_off, WAKE_IRQ_EN, val);
}

#ifdef CONFIG_DEBUG_FS
static int clk_phase_show(struct seq_file *s, void *data)
{
	struct spacemit_ethqos *eqos = s->private;

	seq_printf(s, "phy-mode : %s\n", phy_modes(eqos->phy_iface));
	seq_printf(s, "rx phase : %d\n", eqos->rx_clk_phase);
	seq_printf(s, "tx phase : %d\n", eqos->tx_clk_phase);

	return 0;
}

static ssize_t clk_tuning_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct spacemit_ethqos *eqos =
				((struct seq_file *)(file->private_data))->private;
	char buff[TUNING_CMD_LEN];
	char mode_str[20];
	size_t len = min_t(size_t, count, TUNING_CMD_LEN - 1);
	int err, clk_phase;

	if (copy_from_user(buff, user_buf, len))
		return -EFAULT;
	buff[len] = '\0';

	err = sscanf(buff, "%19s %d", mode_str, &clk_phase);
	if (err != 2)
		return -EINVAL;
	if (clk_phase < 0 || clk_phase >= CLK_PHASE_CNT)
		return -EINVAL;

	if (!strcmp(mode_str, "tx")) {
		eqos->tx_clk_phase = clk_phase;
		clk_phase_set(eqos, TX_PHASE);
	} else if (!strcmp(mode_str, "rx")) {
		eqos->rx_clk_phase = clk_phase;
		clk_phase_set(eqos, RX_PHASE);
	} else {
		return -EINVAL;
	}

	return count;
}

static int clk_tuning_open(struct inode *inode, struct file *file)
{
	return single_open(file, clk_phase_show, inode->i_private);
}

static const struct file_operations clk_tuning_fops = {
	.open		= clk_tuning_open,
	.write		= clk_tuning_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#endif

static int k3_parse_dt(struct platform_device *pdev, struct spacemit_ethqos *eqos)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	u32 tx_phase, rx_phase;
	int ret;

	eqos->phy_iface = eqos->plat->phy_interface;

	eqos->tx_clk = NULL;
	if (phy_interface_mode_is_rgmii(eqos->phy_iface)) {
		eqos->tx_clk = devm_clk_get_optional(dev, "tx_clk");
		if (IS_ERR(eqos->tx_clk))
			return dev_err_probe(dev, PTR_ERR(eqos->tx_clk), "tx clock");
		if (!eqos->tx_clk)
			dev_info(dev, "rgmii tx clk is derived from the rx clk\n");
	}

	eqos->tx_clk_from_soc = !!eqos->tx_clk;

	eqos->phy_clk = devm_clk_get_optional(dev, "phy_clk");
	if (IS_ERR(eqos->phy_clk))
		return dev_err_probe(dev, PTR_ERR(eqos->phy_clk), "phy clock");

	eqos->phy_clk_from_soc = !!eqos->phy_clk;
	if (!eqos->phy_clk_from_soc)
		dev_info(dev, "phy clk is provided by a external crystal oscillator\n");

	eqos->apmu = syscon_regmap_lookup_by_phandle(np, "spacemit,apmu");
	if (IS_ERR(eqos->apmu))
		return dev_err_probe(dev, PTR_ERR(eqos->apmu), "spacemit,apmu lookup failed");

	ret = of_property_read_u32(np, "spacemit,ctrl-offset", &eqos->ctrl_off);
	if (ret)
		return dev_err_probe(dev, ret, "missing spacemit,ctrl-offset");

	ret = of_property_read_u32(np, "spacemit,dline-offset", &eqos->dline_off);
	if (ret)
		return dev_err_probe(dev, ret, "missing spacemit,dline-offset");

	eqos->wol_irq_enable = of_property_read_bool(np, "spacemit,wake-irq-enable");

	eqos->clk_tuning_enable = of_property_read_bool(np, "spacemit,clk-tuning-enable");
	if (!eqos->clk_tuning_enable)
		return 0;

	if (of_property_read_bool(np, "spacemit,clk-tuning-by-reg")) {
		eqos->clk_tuning_way = CLK_TUNING_BY_REG;
	} else if (of_property_read_bool(np, "spacemit,clk-tuning-by-clk-revert")) {
		eqos->clk_tuning_way = CLK_TUNING_BY_CLK_REVERT;
	} else if (of_property_read_bool(np, "spacemit,clk-tuning-by-delayline")) {
		eqos->clk_tuning_way = CLK_TUNING_BY_DLINE;
	} else {
		eqos->clk_tuning_way = CLK_TUNING_BY_REG;
	}

	if (of_property_read_u32(np, "spacemit,tx-phase", &tx_phase))
		eqos->tx_clk_phase = TXCLK_PHASE_DEFAULT;
	else
		eqos->tx_clk_phase = tx_phase;

	if (of_property_read_u32(np, "spacemit,rx-phase", &rx_phase))
		eqos->rx_clk_phase = RXCLK_PHASE_DEFAULT;
	else
		eqos->rx_clk_phase = rx_phase;

#ifdef CONFIG_DEBUG_FS
	if (!eqos->dbg_dir) {
		eqos->dbg_dir = debugfs_create_dir(dev_name(dev), NULL);

		if (IS_ERR_OR_NULL(eqos->dbg_dir)) {
			dev_err(dev, "debugfs: failed to create dir\n");
		} else {
			eqos->dbg_clk_tuning = debugfs_create_file("clk_tuning", 0644,
								   eqos->dbg_dir, eqos,
								   &clk_tuning_fops);
			if (IS_ERR_OR_NULL(eqos->dbg_clk_tuning))
				dev_err(dev, "debugfs: failed to create file\n");
		}
	}
#endif

	return 0;
}

static void devm_k3_release_dt(void *arg)
{
	struct spacemit_ethqos *eqos = arg;

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(eqos->dbg_dir);
	eqos->dbg_dir = NULL;
	eqos->dbg_clk_tuning = NULL;
#endif
}

static int devm_k3_parse_dt(struct platform_device *pdev,
			    struct spacemit_ethqos *eqos)
{
	int ret;

	ret = k3_parse_dt(pdev, eqos);
	if (ret)
		return ret;

	return devm_add_action_or_reset(&pdev->dev,
					devm_k3_release_dt, eqos);
}

static void k3_fix_mac_speed(void *bsp_priv, int speed, unsigned int mode)
{
	struct spacemit_ethqos *eqos = bsp_priv;
	struct device *dev = &eqos->pdev->dev;
	phy_interface_t iface = eqos->phy_iface;

	eqos->speed = speed;

	if (!eqos->clk_tuning_enable)
		return;

	switch (iface) {
	case PHY_INTERFACE_MODE_RGMII_ID:
		/* PHY already provides TX+RX delay */
		return;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* PHY provides TX delay; only adjust RX */
		clk_phase_set(eqos, RX_PHASE);
		return;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		/* PHY provides RX delay; only adjust TX */
		clk_phase_set(eqos, TX_PHASE);
		return;
	case PHY_INTERFACE_MODE_RMII:
	case PHY_INTERFACE_MODE_RGMII:
		/* rgmii/rmii: adjust both TX and RX phases */
		clk_phase_set(eqos, TX_PHASE);
		clk_phase_set(eqos, RX_PHASE);
		return;
	default:
		dev_warn(dev, "clk tuning skipped for phy-mode: %s\n", phy_modes(iface));
		return;
	}
}

static int k3_clks_config(void *bsp_priv, bool enabled)
{
	struct spacemit_ethqos *eqos = bsp_priv;
	int ret = 0;

	if (enabled) {
		if (eqos->tx_clk_from_soc) {
			ret = clk_prepare_enable(eqos->tx_clk);
			if (ret)
				return ret;
		}

		if (eqos->phy_clk_from_soc) {
			ret = clk_prepare_enable(eqos->phy_clk);
			if (ret) {
				if (eqos->tx_clk_from_soc)
					clk_disable_unprepare(eqos->tx_clk);
				return ret;
			}
		}
	} else {
		if (eqos->phy_clk_from_soc)
			clk_disable_unprepare(eqos->phy_clk);
		if (eqos->tx_clk_from_soc)
			clk_disable_unprepare(eqos->tx_clk);
	}

	return ret;
}

static int devm_k3_bind_plat_ops(struct spacemit_ethqos *eqos)
{
	struct plat_stmmacenet_data *plat_dat = eqos->plat;

	plat_dat->fix_mac_speed = k3_fix_mac_speed;
	plat_dat->clks_config = k3_clks_config;

	return 0;
}

static void k3_eqos_config_caps(struct spacemit_ethqos *eqos)
{
	struct plat_stmmacenet_data *plat_dat = eqos->plat;
	int i;

	/*
	 * If GMAC TX clock is derived from PHY RXC, stopping RXC in EEE/LPI
	 * will remove the TX clock and cause TX timeouts. Require RXC to run
	 * in LPI.
	 */
	if (phy_interface_mode_is_rgmii(eqos->phy_iface) && !eqos->tx_clk_from_soc)
		plat_dat->flags |= STMMAC_FLAG_RX_CLK_RUNS_IN_LPI;

	/* Fall back to PHY WOL when GMAC WOL IRQ is not enabled */
	if (!eqos->wol_irq_enable)
		plat_dat->flags |= STMMAC_FLAG_USE_PHY_WOL;

	/* Enable TSO on queue0 and enable TBS on rest of the queues */
	for (i = 1; i < plat_dat->tx_queues_to_use; i++)
		plat_dat->tx_queues_cfg[i].tbs_en = 1;
}

static int k3_setup_plat(struct spacemit_ethqos *eqos)
{
	struct device *dev = &eqos->pdev->dev;
	int ret;

	if (eqos->phy_clk_from_soc) {
		ret = clk_prepare_enable(eqos->phy_clk);
		if (ret)
			return dev_err_probe(dev, ret, "failed to enable phy_clk\n");
	}

	if (eqos->tx_clk_from_soc) {
		ret = clk_prepare_enable(eqos->tx_clk);
		if (ret) {
			ret = dev_err_probe(dev, ret, "failed to enable tx_clk\n");
			goto err_disable_phy_clk;
		}
	}

	ret = k3_eqos_iface_config(eqos);
	if (ret)
		goto err_disable_tx_clk;

	ret = k3_eqos_wol_config(eqos);
	if (ret)
		goto err_disable_tx_clk;

	ret = k3_delayline_init(eqos);
	if (ret)
		goto err_disable_tx_clk;

	k3_eqos_config_caps(eqos);

	return 0;

err_disable_tx_clk:
	if (eqos->tx_clk_from_soc)
		clk_disable_unprepare(eqos->tx_clk);

err_disable_phy_clk:
	if (eqos->phy_clk_from_soc)
		clk_disable_unprepare(eqos->phy_clk);

	return ret;
}

static void devm_k3_cleanup_plat(void *arg)
{
	struct spacemit_ethqos *eqos = arg;

	if (eqos->tx_clk_from_soc)
		clk_disable_unprepare(eqos->tx_clk);

	if (eqos->phy_clk_from_soc)
		clk_disable_unprepare(eqos->phy_clk);
}

static int devm_k3_setup_plat(struct spacemit_ethqos *eqos)
{
	struct device *dev = &eqos->pdev->dev;
	int ret;

	ret = k3_setup_plat(eqos);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, devm_k3_cleanup_plat, eqos);
}

static const struct spacemit_ethqos_ops k3_gmac_ops = {
	.devm_glue_parse_dt = devm_k3_parse_dt,
	.devm_glue_bind_ops = devm_k3_bind_plat_ops,
	.devm_glue_config_plat = devm_k3_setup_plat,
};

/* TODO: add K4/K5/K6 SoC-specific GMAC macros/ops here in future */

static int spacemit_ethqos_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stmmac_resources stmmac_res;
	struct plat_stmmacenet_data *plat_dat;
	const struct spacemit_ethqos_ops *ops;
	int ret;

	ops = of_device_get_match_data(dev);
	if (!ops)
		return dev_err_probe(dev, -EINVAL, "no of_match data");

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	ret = devm_spacemit_glue_init(pdev, plat_dat, ops);
	if (ret)
		return ret;

	ret = devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id spacemit_ethqos_match[] = {
	{ .compatible = "spacemit,k3-gmac", .data = &k3_gmac_ops },
	{}
};
MODULE_DEVICE_TABLE(of, spacemit_ethqos_match);

static struct platform_driver spacemit_ethqos_driver = {
	.probe  = spacemit_ethqos_probe,
	.driver = {
		.name           = DRIVER_NAME,
		.pm             = &stmmac_pltfr_pm_ops,
		.of_match_table = spacemit_ethqos_match,
	},
};
module_platform_driver(spacemit_ethqos_driver);

MODULE_DESCRIPTION("Spacemit dwmac ethqos specific glue layer");
MODULE_LICENSE("GPL v2");
