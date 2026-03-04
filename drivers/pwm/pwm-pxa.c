// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/pwm/pwm-pxa.c
 *
 * simple driver for PWM (Pulse Width Modulator) controller
 *
 * 2008-02-13	initial version
 *		eric miao <eric.miao@marvell.com>
 *
 * Links to reference manuals for some of the supported PWM chips can be found
 * in Documentation/arch/arm/marvell.rst.
 *
 * Limitations:
 * - When PWM is stopped, the current PWM period stops abruptly at the next
 *   input clock (PWMCR_SD is set) and the output is driven to inactive.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/of.h>
#include <linux/reset.h>

#include <asm/div64.h>

#define HAS_SECONDARY_PWM	0x10

static const struct platform_device_id pwm_id_table[] = {
	/*   PWM    has_secondary_pwm? */
	{ "pxa25x-pwm", 0 },
	{ "pxa27x-pwm", HAS_SECONDARY_PWM },
	{ "pxa168-pwm", 0 },
	{ "pxa910-pwm", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, pwm_id_table);

/* PWM registers and bits definitions */
#define PWMCR		(0x00)
#define PWMDCR		(0x04)
#define PWMPCR		(0x08)

#define PWMCR_SD	(1 << 6)
#define PWMDCR_FD	(1 << 10)

struct pxa_pwm_chip {
	struct device	*dev;

	struct clk	*clk;
	struct clk	*bus_clk;
	struct reset_control	*reset;
	void __iomem	*mmio_base;
	bool dcr_fd_disabled;
	bool rcpu_pwm;
};

static inline struct pxa_pwm_chip *to_pxa_pwm_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

/*
 * period_ns = 10^9 * (PRESCALE + 1) * (PV + 1) / PWM_CLK_RATE
 * duty_ns   = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
 */
static int pxa_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  u64 duty_ns, u64 period_ns)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);
	unsigned long long c;
	unsigned long period_cycles, prescale, pv, dc;
	unsigned long offset;

	offset = pwm->hwpwm ? 0x10 : 0;

	c = clk_get_rate(pc->clk);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	if (period_cycles < 1)
		period_cycles = 1;
	prescale = (period_cycles - 1) / 1024;
	pv = period_cycles / (prescale + 1) - 1;

	if (prescale > 63)
		return -EINVAL;

	if (duty_ns == period_ns) {
		if (pc->dcr_fd_disabled) {
			dc = (pv + 1) * duty_ns / period_ns;
			if (dc >= PWMDCR_FD) {
				dc = PWMDCR_FD - 1;
				pv = dc - 1;
			}
		} else {
			dc = PWMDCR_FD;
		}
	} else {
		dc = mul_u64_u64_div_u64(pv + 1, duty_ns, period_ns);
	}

	writel(prescale | PWMCR_SD, pc->mmio_base + offset + PWMCR);
	writel(dc, pc->mmio_base + offset + PWMDCR);
	writel(pv, pc->mmio_base + offset + PWMPCR);

	return 0;
}

static int pxa_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);
	u64 duty_cycle;
	int err;

	if (state->duty_cycle > state->period)
		return -EINVAL;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	/* Enable bus clock first if available */
	if (pc->bus_clk) {
		err = clk_prepare_enable(pc->bus_clk);
		if (err)
			return err;
	}

	/* Enable functional clock */
	err = clk_prepare_enable(pc->clk);
	if (err) {
		if (pc->bus_clk)
			clk_disable_unprepare(pc->bus_clk);
		return err;
	}

	duty_cycle = state->enabled ? state->duty_cycle : 0;

	err = pxa_pwm_config(chip, pwm, duty_cycle, state->period);
	if (err) {
		clk_disable_unprepare(pc->clk);
		if (pc->bus_clk)
			clk_disable_unprepare(pc->bus_clk);
		return err;
	}

	if (state->enabled && !pwm->state.enabled)
		return 0;

	/* Disable functional clock first */
	clk_disable_unprepare(pc->clk);
	/* Then disable bus clock if available */
	if (pc->bus_clk)
		clk_disable_unprepare(pc->bus_clk);

	if (!state->enabled && pwm->state.enabled) {
		/* Need to disable clocks one more time */
		clk_disable_unprepare(pc->clk);
		if (pc->bus_clk)
			clk_disable_unprepare(pc->bus_clk);
	}

	return 0;
}

static const struct pwm_ops pxa_pwm_ops = {
	.apply = pxa_pwm_apply,
};

#ifdef CONFIG_OF
/*
 * Device tree users must create one device instance for each PWM channel.
 * Hence we dispense with the HAS_SECONDARY_PWM and "tell" the original driver
 * code that this is a single channel pxa25x-pwm.  Currently all devices are
 * supported identically.
 */
static const struct of_device_id pwm_of_match[] = {
	{ .compatible = "marvell,pxa250-pwm", .data = &pwm_id_table[0]},
	{ .compatible = "marvell,pxa270-pwm", .data = &pwm_id_table[0]},
	{ .compatible = "marvell,pxa168-pwm", .data = &pwm_id_table[0]},
	{ .compatible = "marvell,pxa910-pwm", .data = &pwm_id_table[0]},
	{ }
};
MODULE_DEVICE_TABLE(of, pwm_of_match);
#else
#define pwm_of_match NULL
#endif

static int pwm_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct pwm_chip *chip;
	struct pxa_pwm_chip *pc;
	struct device *dev = &pdev->dev;
	int ret = 0;

	if (IS_ENABLED(CONFIG_OF) && id == NULL)
		id = of_device_get_match_data(dev);

	if (id == NULL)
		return -EINVAL;

	chip = devm_pwmchip_alloc(dev, (id->driver_data & HAS_SECONDARY_PWM) ? 2 : 1,
				  sizeof(*pc));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pc = to_pxa_pwm_chip(chip);

	if (pdev->dev.of_node)
		pc->dcr_fd_disabled = of_property_read_bool(pdev->dev.of_node,
							    "k1,pwm-disable-fd");

	/* Get functional clock */
	pc->clk = devm_clk_get(&pdev->dev, "func");
	if (IS_ERR(pc->clk)) {
		/* Fallback to unnamed clock for backward compatibility */
		pc->clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(pc->clk))
			return PTR_ERR(pc->clk);
	}

	/* Get bus clock (optional for new DT binding) */
	pc->bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(pc->bus_clk)) {
		/* Bus clock is optional, set to NULL if not found */
		pc->bus_clk = NULL;
	}

	pc->reset = devm_reset_control_get_optional_exclusive(&pdev->dev, NULL);
	if (IS_ERR(pc->reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(pc->reset),
				     "failed to get reset control\n");

	/* Check if PWM is in RCPU domain */
	if (pdev->dev.of_node)
		pc->rcpu_pwm = of_property_read_bool(pdev->dev.of_node, "rcpu-pwm");

	/* Deassert reset */
	if (pc->reset)
		reset_control_deassert(pc->reset);

	chip->ops = &pxa_pwm_ops;

	if (IS_ENABLED(CONFIG_OF))
		chip->of_xlate = of_pwm_single_xlate;

	pc->mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->mmio_base))
		return PTR_ERR(pc->mmio_base);

	ret = devm_pwmchip_add(dev, chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "pwmchip_add() failed\n");

	platform_set_drvdata(pdev, pc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pxa_pwm_suspend_noirq(struct device *dev)
{
	return 0;
}

static int pxa_pwm_resume_noirq(struct device *dev)
{
	struct pxa_pwm_chip *pc = dev_get_drvdata(dev);

	/* if pwm in rcpu domain, deassert reset first before apply the old state */
	if (pc->rcpu_pwm && pc->reset)
		reset_control_deassert(pc->reset);

	return 0;
}
#endif

static const struct dev_pm_ops pxa_pwm_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(pxa_pwm_suspend_noirq,
			pxa_pwm_resume_noirq)
};

static struct platform_driver pwm_driver = {
	.driver		= {
		.name	= "pxa25x-pwm",
#ifdef CONFIG_PM_SLEEP
		.pm	= &pxa_pwm_pm_ops,
#endif
		.of_match_table = pwm_of_match,
	},
	.probe		= pwm_probe,
	.id_table	= pwm_id_table,
};

module_platform_driver(pwm_driver);

MODULE_DESCRIPTION("PXA Pulse Width Modulator driver");
MODULE_LICENSE("GPL v2");
