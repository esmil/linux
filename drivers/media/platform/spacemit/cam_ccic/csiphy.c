// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Spacemit CCIC MIPI D-PHY MODULE
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/atomic.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>
#include <linux/mfd/syscon.h>
#include "ccic_drv.h"
#include "ccic_hwreg.h"
#include "csiphy.h"

#define DEBUG 0 /* for pr_debug() */

#define CSIPHY_DRV_NAME "spacemit-csi-phy"

#define CAMERA_ENABLE_BIT BIT(1)

struct csiphy_device {
	struct list_head list;
	struct platform_device *pdev;
	struct regmap *apmu;
	u32 ctrl_off;
	struct device *dev;
	struct resource *mem;
	void __iomem *base;
	struct clk *csiphy_clk;
	struct reset_control *csi_dphy_reset;
	atomic_t usecnt;
	int is_bifmode;
};

/*
 * Device register I/O
 */
static inline u32 csiphy_readl(struct csiphy_device *csiphy, unsigned int reg)
{
	return readl(csiphy->base + reg);
}

static inline void csiphy_writel(struct csiphy_device *csiphy, unsigned int reg,
				 u32 val)
{
	writel(val, csiphy->base + reg);
}

static inline void csiphy_mask_writel(struct csiphy_device *csiphy,
				      unsigned int reg, u32 val, u32 mask)
{
	u32 v = csiphy_readl(csiphy, reg);

	v = (v & ~mask) | (val & mask);
	csiphy_writel(csiphy, reg, v);
}

static inline void csiphy_set_bit(struct csiphy_device *csiphy,
				  unsigned int reg, u32 val)
{
	csiphy_mask_writel(csiphy, reg, val, val);
}

static inline void csiphy_clear_bit(struct csiphy_device *csiphy,
				    unsigned int reg, u32 val)
{
	csiphy_mask_writel(csiphy, reg, 0, val);
}

static int csiphy_camera_sys_enable(struct csiphy_device *csiphy_dev,
				    bool enable)
{
	unsigned int val;
	int ret;

	val = enable ? 1 : 0;

	ret = regmap_update_bits(csiphy_dev->apmu, csiphy_dev->ctrl_off,
				 CAMERA_ENABLE_BIT, val << 1);
	if (ret < 0) {
		dev_err(csiphy_dev->dev, "failed to update camera sys enable, ret=%d\n", ret);
	} else {
		dev_info(csiphy_dev->dev, "camera sys enable success:%d\n", val);
	}
	return ret;
}

/**
 * csiphy_set_power - Power on/off csiphy module
 *
 * @csiphy_dev: csiphy device
 * @on: requested power state
 *
 * Return: 0 on success, error code otherwise.
 */
static int csiphy_set_power(struct csiphy_device *csiphy_dev, int on)
{
	int ret;
	unsigned long clk_val;

	if (on) {
		clk_val = clk_round_rate(csiphy_dev->csiphy_clk, 104000000);
		clk_set_rate(csiphy_dev->csiphy_clk, clk_val);
		clk_val = clk_get_rate(csiphy_dev->csiphy_clk);
		dev_info(csiphy_dev->dev, "csiphy clk:%ld\n", clk_val);
		ret = clk_prepare_enable(csiphy_dev->csiphy_clk);
		if (ret < 0) {
			dev_err(csiphy_dev->dev,
				"enable dphy clk failed, err=%d\n", ret);
			return ret;
		}
		ret = reset_control_deassert(csiphy_dev->csi_dphy_reset);
		if (ret < 0) {
			dev_err(csiphy_dev->dev,
				"deassert dphy reset failed, err=%d\n", ret);
			return ret;
		}
		ret = csiphy_camera_sys_enable(csiphy_dev, 1);
		if (ret < 0) {
			dev_err(csiphy_dev->dev,
				"enable camera sys failed, err=%d\n", ret);
			return ret;
		}
	} else {
		clk_disable_unprepare(csiphy_dev->csiphy_clk);
		reset_control_assert(csiphy_dev->csi_dphy_reset);
		csiphy_camera_sys_enable(csiphy_dev, 0);
	}

	return 0;
}

/**
 * csiphy_set_2to2dphy
 *
 * Return: 0 on success, error code otherwise.
 */
static int csiphy_set_2to2dphy(struct csiphy_device *csiphy_dev, int enable)
{
	if (enable) {
		/* REG_CSI2_DPHY1[1]: analog bif mode on */
		csiphy_set_bit(csiphy_dev, REG_CSI2_DPHY1, CSI2_DHPY1_BIF_EN);
		/* ccic3:ccic_csi2_dphy9[23]: dphy3 2+2 lane mux */
		csiphy_set_bit(csiphy_dev, REG_CSI2_DPHY9, CSI2_DHPY9_EN_BIF2);
	} else {
		csiphy_clear_bit(csiphy_dev, REG_CSI2_DPHY1, CSI2_DHPY1_BIF_EN);
		csiphy_clear_bit(csiphy_dev, REG_CSI2_DPHY9, CSI2_DHPY9_EN_BIF2);
	}

	return 0;
}

static struct csi_dphy_calc dphy_calc_profiles[] = {
	{
		.hs_termen_pos = 0,
		.hs_settle_pos = 50,
	},
};

static int ccic_calc_dphy(struct csi_dphy_desc *desc,
			  struct csi_dphy_calc *algo, struct csi_dphy_reg *reg)
{
	u32 ps_period, ps_ui, ps_termen_max, ps_prep_max, ps_prep_min;
	u32 ps_sot_min, ps_termen, ps_settle;

	ps_period = MHZ * 1000 / (desc->clk_freq / 1000);
	ps_ui = ps_period / 2;
	ps_termen_max = NS_TO_PS(D_TERMEN_MAX) + 4 * ps_ui;
	ps_prep_min = NS_TO_PS(HS_PREP_MIN) + 4 * ps_ui;
	ps_prep_max = NS_TO_PS(HS_PREP_MAX) + 6 * ps_ui;
	ps_sot_min = NS_TO_PS(HS_PREP_ZERO_MIN) + 10 * ps_ui;
	ps_termen = ps_termen_max + algo->hs_termen_pos * ps_period;
	ps_settle = NS_TO_PS(desc->hs_prepare + desc->hs_zero *
							algo->hs_settle_pos /
							HS_SETTLE_POS_MAX);

	reg->cl_termen = 0x01;
	reg->cl_settle = 0x10;
	reg->cl_miss = 0x00;
	/* term_en = round_up(ps_termen / ps_period) - 1 */
	reg->hs_termen = (ps_termen + ps_period - 1) / ps_period - 1;
	/* For Marvell DPHY, Ths-settle started from HS-0, not VILmax */
	ps_settle -= (reg->hs_termen + 1) * ps_period;
	/* DE recommend this value reset to zero */
	reg->hs_termen = 0x0;
	/* round_up(ps_settle / ps_period) - 1 */
	reg->hs_settle = (ps_settle + ps_period - 1) / ps_period - 1;
	reg->hs_rx_to = 0xFFFF;
	reg->lane = desc->nr_lane;
	return 0;
}

int csiphy_stop(struct csiphy_device *csiphy_dev)
{
	if (csiphy_dev->is_bifmode)
		csiphy_set_2to2dphy(csiphy_dev, 0);

	csiphy_clear_bit(csiphy_dev, REG_CSI2_CTRL0, CSI2_C0_ENABLE);
	csiphy_writel(csiphy_dev, REG_CSI2_DPHY5, 0x00);
	csiphy_clear_bit(csiphy_dev, REG_CSI2_DPHY1,
			 CSI2_DHPY1_ANA_PU); /* analog power off */

	if (atomic_dec_if_positive(&csiphy_dev->usecnt) == 0)
		csiphy_set_power(csiphy_dev, 0);

	return 0;
}

int csiphy_start(struct csiphy_device *csiphy_dev, struct mipi_csi2 *csi)
{
	unsigned int dphy2_val = 0;
	unsigned int dphy3_val = 0;
	unsigned int dphy5_val = 0;
	unsigned int dphy6_val = 0;
	unsigned int ctrl0_val = 0;
	struct csi_dphy_reg dphy_reg;
	int lanes = csi->dphy_desc.nr_lane;

	if (lanes < 1 || lanes > (csiphy_dev->is_bifmode ? 2 : 4)) {
		dev_err(csiphy_dev->dev, "wrong lanes num %d\n", lanes);
		return -EINVAL;
	}

	if (atomic_inc_return(&csiphy_dev->usecnt) == 1)
		csiphy_set_power(csiphy_dev, 1);

	if (csi->calc_dphy) {
		ccic_calc_dphy(&csi->dphy_desc, dphy_calc_profiles, &dphy_reg);
		dphy3_val = dphy_reg.hs_settle & 0xFF;
		dphy3_val = dphy_reg.hs_termen |
			    (dphy3_val << CSI2_DPHY3_HS_SETTLE_SHIFT);
		dphy6_val = dphy_reg.cl_settle & 0xFF;
		dphy6_val = dphy_reg.cl_termen |
			    (dphy6_val << CSI2_DPHY6_CK_SETTLE_SHIFT);
	} else {
		dphy2_val = csi->dphy[1];
		dphy3_val = csi->dphy[2];
		dphy6_val = csi->dphy[4];
	}

	dphy5_val = CSI2_DPHY5_LANE_ENA(lanes);
	dphy5_val = dphy5_val | (dphy5_val << CSI2_DPHY5_LANE_RESC_ENA_SHIFT);

	ctrl0_val = csiphy_readl(csiphy_dev, REG_CSI2_CTRL0);
	ctrl0_val &= ~(CSI2_C0_LANE_NUM_MASK);
	ctrl0_val |= CSI2_C0_LANE_NUM(lanes);
	ctrl0_val |= CSI2_C0_ENABLE;
	ctrl0_val &= ~(CSI2_C0_VLEN_MASK);
	ctrl0_val |= CSI2_C0_VLEN;

	/* CSI2_DPHY1 must set bit,otherwise may cover someone else's config. */
	csiphy_set_bit(csiphy_dev, REG_CSI2_DPHY1,
		       CSI2_DHPY1_ANA_PU); /* analog power on */
	csiphy_writel(csiphy_dev, REG_CSI2_DPHY2, dphy2_val);
	if (!csiphy_readl(csiphy_dev, REG_CSI2_DPHY3))
		csiphy_writel(csiphy_dev, REG_CSI2_DPHY3, dphy3_val);
	csiphy_writel(csiphy_dev, REG_CSI2_DPHY5, dphy5_val);
	csiphy_writel(csiphy_dev, REG_CSI2_DPHY6, dphy6_val);
	csiphy_writel(csiphy_dev, REG_CSI2_CTRL0, ctrl0_val);

	/* for lark */
	csiphy_writel(csiphy_dev, REG_CSI2_DPHY4, 0x0013a35a);
	csiphy_writel(csiphy_dev, REG_CSI2_DPHY9, 0x1a280090);

	if (csiphy_dev->is_bifmode)
		csiphy_set_2to2dphy(csiphy_dev, 1);

	return 0;
}

void csiphy_timming_setting(struct csiphy_device *csiphy_dev, unsigned int settle)
{
	csiphy_writel(csiphy_dev, REG_CSI2_DPHY3, settle);
}

static DEFINE_MUTEX(csiphy_list_mutex);
static LIST_HEAD(csiphy_list);
struct csiphy_device *csiphy_lookup_by_phandle(struct device *dev,
					       const char *name)
{
	struct device_node *csiphy_node =
		of_parse_phandle(dev->of_node, name, 0);
	struct csiphy_device *csiphy_dev;

	mutex_lock(&csiphy_list_mutex);
	list_for_each_entry(csiphy_dev, &csiphy_list, list) {
		if (csiphy_node == csiphy_dev->dev->of_node) {
			mutex_unlock(&csiphy_list_mutex);
			of_node_put(csiphy_node);
			return csiphy_dev;
		}
	}
	mutex_unlock(&csiphy_list_mutex);

	of_node_put(csiphy_node);
	return NULL;
}

static int csiphy_probe(struct platform_device *pdev)
{
	struct csiphy_device *csiphy_dev;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	csiphy_dev = devm_kzalloc(&pdev->dev, sizeof(struct csiphy_device),
				  GFP_KERNEL);
	if (!csiphy_dev) {
		dev_err(&pdev->dev, "no enough memory\n");
		return -ENOMEM;
	}

	ret = of_property_read_u32(np, "cell-index", &pdev->id);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", ret);
		return ret;
	}

	/* get mem */
	csiphy_dev->mem = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						       "csiphy-regs");
	if (!csiphy_dev->mem) {
		dev_err(&pdev->dev, "no mem resource\n");
		return -ENODEV;
	}

	csiphy_dev->base = devm_ioremap(&pdev->dev, csiphy_dev->mem->start,
					resource_size(csiphy_dev->mem));
	if (IS_ERR(csiphy_dev->base)) {
		dev_err(&pdev->dev, "fail to remap iomem\n");
		return PTR_ERR(csiphy_dev->base);
	}

	/* get clock(s) */
	csiphy_dev->csi_dphy_reset =
		devm_reset_control_get(&pdev->dev, "csi_dphy_reset");
	if (IS_ERR(csiphy_dev->csi_dphy_reset)) {
		ret = PTR_ERR(csiphy_dev->csi_dphy_reset);
		dev_err(&pdev->dev, "failed to get csi_dphy reset: %d\n", ret);
		return ret;
	}
	csiphy_dev->csiphy_clk = devm_clk_get(&pdev->dev, "csi_dphy");
	if (IS_ERR(csiphy_dev->csiphy_clk)) {
		ret = PTR_ERR(csiphy_dev->csiphy_clk);
		dev_err(&pdev->dev, "failed to get csiphy clock: %d\n", ret);
		return ret;
	}

	csiphy_dev->apmu = syscon_regmap_lookup_by_phandle(np, "spacemit,apmu");
	if (IS_ERR(csiphy_dev->apmu)) {
		ret = PTR_ERR(csiphy_dev->apmu);
		dev_err(&pdev->dev, "spacemit,apmu lookup failed: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(np, "spacemit,ctrl-offset",
				   &csiphy_dev->ctrl_off);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get ctrl-offset, errno %d\n",
			ret);
		return ret;
	}

	ret = of_property_read_s32(np, "spacemit,bifmode-enable",
				   &csiphy_dev->is_bifmode);
	if (ret < 0) {
		dev_warn(&pdev->dev, "failed to get bifmode, ret=%d\n", ret);
		csiphy_dev->is_bifmode = 0;
	}

	atomic_set(&csiphy_dev->usecnt, 0);
	csiphy_dev->pdev = pdev;
	csiphy_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, csiphy_dev);

	mutex_lock(&csiphy_list_mutex);
	list_add(&csiphy_dev->list, &csiphy_list);
	mutex_unlock(&csiphy_list_mutex);

	pr_info("%s probed", dev_name(&pdev->dev));

	return ret;
}

static void csiphy_remove(struct platform_device *pdev)
{
	struct csiphy_device *csiphy_dev;

	csiphy_dev = platform_get_drvdata(pdev);
	if (!csiphy_dev) {
		dev_err(&pdev->dev, "csiphy device is NULL\n");
		return;
	}

	devm_kfree(&pdev->dev, csiphy_dev);
	pr_info("%s removed", dev_name(&pdev->dev));

	return;
}

static const struct of_device_id csiphy_dt_match[] = {
	{
		.compatible = "spacemit,csi-dphy",
		.data = NULL
	},
	{},
};
MODULE_DEVICE_TABLE(of, csiphy_dt_match);

struct platform_driver csiphy_driver = {
	.driver = {
		.name = CSIPHY_DRV_NAME,
		.of_match_table = of_match_ptr(csiphy_dt_match),
	},
	.probe = csiphy_probe,
	.remove = csiphy_remove,
};

int ccic_csiphy_register(void)
{
	return platform_driver_register(&csiphy_driver);
}

void ccic_csiphy_unregister(void)
{
	platform_driver_unregister(&csiphy_driver);
}
/* module_platform_driver(csiphy_driver); */

MODULE_DESCRIPTION("SPACEMIT CSI-PHY Driver");
MODULE_LICENSE("GPL");
