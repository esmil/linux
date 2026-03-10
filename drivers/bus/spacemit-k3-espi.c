// SPDX-License-Identifier: GPL-2.0-only
/*
 * SpacemiT eSPI controller glue driver
 *
 * This is currently a minimal bus driver that only keeps the controller
 * clocks enabled while the system is running and toggles them across
 * system suspend/resume so child devices (for example cros_ec_espi) can
 * complete their PM callbacks.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

struct spacemit_espi {
	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control *resets;
	bool clocks_enabled;
};

static int spacemit_espi_enable_clks(struct device *dev, struct spacemit_espi *espi)
{
	int ret;

	if (!espi->num_clks || espi->clocks_enabled)
		return 0;

	ret = clk_bulk_prepare_enable(espi->num_clks, espi->clks);
	if (ret) {
		dev_err(dev, "failed to enable clocks: %d\n", ret);
		return ret;
	}

	espi->clocks_enabled = true;

	return 0;
}

static void spacemit_espi_disable_clks(struct spacemit_espi *espi)
{
	if (!espi->num_clks || !espi->clocks_enabled)
		return;

	clk_bulk_disable_unprepare(espi->num_clks, espi->clks);
	espi->clocks_enabled = false;
}

static int spacemit_espi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spacemit_espi *espi;
	int ret;

	espi = devm_kzalloc(dev, sizeof(*espi), GFP_KERNEL);
	if (!espi)
		return -ENOMEM;

	espi->num_clks = devm_clk_bulk_get_all(dev, &espi->clks);
	if (espi->num_clks < 0)
		return dev_err_probe(dev, espi->num_clks,
				     "failed to get clocks\n");

	espi->resets = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(espi->resets))
		return dev_err_probe(dev, PTR_ERR(espi->resets),
				     "failed to get resets\n");

	ret = spacemit_espi_enable_clks(dev, espi);
	if (ret)
		return ret;

	ret = reset_control_deassert(espi->resets);
	if (ret) {
		dev_err_probe(dev, ret, "failed to deassert resets\n");
		goto err_disable_clks;
	}

	platform_set_drvdata(pdev, espi);

	ret = devm_of_platform_populate(dev);
	if (ret) {
		dev_err_probe(dev, ret, "failed to populate child devices\n");
		goto err_assert_resets;
	}

	return 0;

err_assert_resets:
	reset_control_assert(espi->resets);
err_disable_clks:
	spacemit_espi_disable_clks(espi);
	return ret;
}

static void spacemit_espi_remove(struct platform_device *pdev)
{
	struct spacemit_espi *espi = platform_get_drvdata(pdev);

	reset_control_assert(espi->resets);
	spacemit_espi_disable_clks(espi);
}

#ifdef CONFIG_PM_SLEEP
static int spacemit_espi_suspend_noirq(struct device *dev)
{
	struct spacemit_espi *espi = dev_get_drvdata(dev);

	spacemit_espi_disable_clks(espi);

	return 0;
}

static int spacemit_espi_resume_noirq(struct device *dev)
{
	struct spacemit_espi *espi = dev_get_drvdata(dev);

	return spacemit_espi_enable_clks(dev, espi);
}
#endif

static const struct dev_pm_ops spacemit_espi_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(spacemit_espi_suspend_noirq,
				      spacemit_espi_resume_noirq)
};

static const struct of_device_id spacemit_espi_of_match[] = {
	{ .compatible = "spacemit,k3-espi" },
	{}
};
MODULE_DEVICE_TABLE(of, spacemit_espi_of_match);

static struct platform_driver spacemit_espi_driver = {
	.probe = spacemit_espi_probe,
	.remove = spacemit_espi_remove,
	.driver = {
		.name = "spacemit-k3-espi",
		.of_match_table = spacemit_espi_of_match,
		.pm = pm_ptr(&spacemit_espi_pm_ops),
	},
};
module_platform_driver(spacemit_espi_driver);

MODULE_DESCRIPTION("SpacemiT K3 eSPI controller glue driver");
MODULE_LICENSE("GPL");
