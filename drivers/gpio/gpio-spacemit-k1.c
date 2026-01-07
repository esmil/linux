// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2023-2025 SpacemiT (Hangzhou) Technology Co. Ltd
 * Copyright (C) 2025 Yixun Lan <dlan@gentoo.org>
 */

#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/version.h>

/* register offset structure */
struct spacemit_gpio_reg_offsets {
	u32 gplr;      /* port level - R */
	u32 gpdr;      /* port direction - R/W */
	u32 gpsr;      /* port set - W */
	u32 gpcr;      /* port clear - W */
	u32 grer;      /* port rising edge R/W */
	u32 gfer;      /* port falling edge R/W */
	u32 gedr;      /* edge detect status - R/W1C */
	u32 gsdr;      /* (set) direction - W */
	u32 gcdr;      /* (clear) direction - W */
	u32 gsrer;     /* (set) rising edge detect enable - W */
	u32 gcrer;     /* (clear) rising edge detect enable - W */
	u32 gsfer;     /* (set) falling edge detect enable - W */
	u32 gcfer;     /* (clear) falling edge detect enable - W */
	u32 gapmask;   /* interrupt mask , 0 disable, 1 enable - R/W */
	u32 gcpmask;   /* interrupt mask for K3 */
};

#define SPACEMIT_NR_BANKS		4
#define SPACEMIT_NR_GPIOS_PER_BANK	32

/* K1 reg_offsets */
static const struct spacemit_gpio_reg_offsets k1_regs = {
	.gplr    = 0x00,
	.gpdr    = 0x0c,
	.gpsr    = 0x18,
	.gpcr    = 0x24,
	.grer    = 0x30,
	.gfer    = 0x3c,
	.gedr    = 0x48,
	.gsdr    = 0x54,
	.gcdr    = 0x60,
	.gsrer   = 0x6c,
	.gcrer   = 0x78,
	.gsfer   = 0x84,
	.gcfer   = 0x90,
	.gapmask = 0x9c,
	.gcpmask = 0xA8,
};

/* K3 reg_offsets */
static const struct spacemit_gpio_reg_offsets k3_regs = {
	.gplr    = 0x0,
	.gpdr    = 0x4,
	.gpsr    = 0x8,
	.gpcr    = 0xc,
	.grer    = 0x10,
	.gfer    = 0x14,
	.gedr    = 0x18,
	.gsdr    = 0x1c,
	.gcdr    = 0x20,
	.gsrer   = 0x24,
	.gcrer   = 0x28,
	.gsfer   = 0x2c,
	.gcfer   = 0x30,
	.gapmask = 0x34,
	.gcpmask = 0x38,
};

/* K1 bank_offsets */
static const u32 k1_bank_offsets[] = { 0x0, 0x4, 0x8, 0x100 };

/* K3 bank_offsets */
static const u32 k3_bank_offsets[] = { 0x0, 0x40, 0x80, 0x100 };

#define to_spacemit_gpio_bank(x) container_of((x), struct spacemit_gpio_bank, gc)

struct spacemit_gpio;

struct spacemit_gpio_data {
	const struct spacemit_gpio_reg_offsets *reg_offsets;
	const u32 *bank_offsets;
};

struct spacemit_gpio_bank {
	struct gpio_generic_chip chip;
	struct spacemit_gpio *sg;
	u32 bank_offset;
	u32 irq_mask;
	u32 irq_rising_edge;
	u32 irq_falling_edge;
};

struct spacemit_gpio {
	struct device *dev;
	struct regmap *rm_gpio;
	struct regmap *rm_gpio_edge;
	const struct spacemit_gpio_data *data;
	struct spacemit_gpio_bank sgb[SPACEMIT_NR_BANKS];
};

static u32 spacemit_gpio_bank_index(struct spacemit_gpio_bank *gb)
{
	return (u32)(gb - gb->sg->sgb);
}

static irqreturn_t spacemit_gpio_irq_handler(int irq, void *dev_id)
{
	struct spacemit_gpio_bank *gb = dev_id;
	u32 reg_offset = gb->bank_offset + gb->sg->data->reg_offsets->gedr;
	unsigned long pending;
	u32 n, gedr;

	regmap_read(gb->sg->rm_gpio, reg_offset, &gedr);
	if (!gedr)
		return IRQ_NONE;
	regmap_write(gb->sg->rm_gpio, reg_offset, gedr);

	pending = gedr & gb->irq_mask;
	if (!pending)
		return IRQ_NONE;

	for_each_set_bit(n, &pending, BITS_PER_LONG)
		handle_nested_irq(irq_find_mapping(gb->chip.gc.irq.domain, n));

	return IRQ_HANDLED;
}

static void spacemit_gpio_irq_ack(struct irq_data *d)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 reg_offset = gb->bank_offset + gb->sg->data->reg_offsets->gedr;

	regmap_write(gb->sg->rm_gpio, reg_offset, BIT(irqd_to_hwirq(d)));
}

static void spacemit_gpio_irq_mask(struct irq_data *d)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 bit = BIT(irqd_to_hwirq(d));

	gb->irq_mask &= ~bit;
	regmap_write(gb->sg->rm_gpio,
		     gb->bank_offset + gb->sg->data->reg_offsets->gapmask,
		     gb->irq_mask);

	if (gb->irq_rising_edge & bit)
		regmap_write(gb->sg->rm_gpio,
			     gb->bank_offset + gb->sg->data->reg_offsets->gcrer,
			     bit);

	if (gb->irq_falling_edge & bit)
		regmap_write(gb->sg->rm_gpio,
			     gb->bank_offset + gb->sg->data->reg_offsets->gcfer,
			     bit);

}

static void spacemit_gpio_irq_unmask(struct irq_data *d)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 bit = BIT(irqd_to_hwirq(d));

	gb->irq_mask |= bit;

	if (gb->irq_rising_edge & bit)
		regmap_write(gb->sg->rm_gpio,
			     gb->bank_offset + gb->sg->data->reg_offsets->gsrer,
			     bit);

	if (gb->irq_falling_edge & bit)
		regmap_write(gb->sg->rm_gpio,
			     gb->bank_offset + gb->sg->data->reg_offsets->gsfer,
			     bit);

	regmap_write(gb->sg->rm_gpio,
		     gb->bank_offset + gb->sg->data->reg_offsets->gapmask,
		     gb->irq_mask);
}

static int spacemit_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(d);
	u32 bit = BIT(irqd_to_hwirq(d));

	if (type & IRQ_TYPE_EDGE_RISING) {
		gb->irq_rising_edge |= bit;
		regmap_write(gb->sg->rm_gpio,
			     gb->bank_offset + gb->sg->data->reg_offsets->gsrer,
			     bit);
	} else {
		gb->irq_rising_edge &= ~bit;
		regmap_write(gb->sg->rm_gpio,
			     gb->bank_offset + gb->sg->data->reg_offsets->gcrer,
			     bit);
	}

	if (type & IRQ_TYPE_EDGE_FALLING) {
		gb->irq_falling_edge |= bit;
		regmap_write(gb->sg->rm_gpio,
			     gb->bank_offset + gb->sg->data->reg_offsets->gsfer,
			     bit);
	} else {
		gb->irq_falling_edge &= ~bit;
		regmap_write(gb->sg->rm_gpio,
			     gb->bank_offset + gb->sg->data->reg_offsets->gcfer,
			     bit);
	}

	return 0;
}

static void spacemit_gpio_irq_print_chip(struct irq_data *data, struct seq_file *p)
{
	struct spacemit_gpio_bank *gb = irq_data_get_irq_chip_data(data);

	seq_printf(p, "%s-%d", dev_name(gb->chip.gc.parent), spacemit_gpio_bank_index(gb));
}

static struct irq_chip spacemit_gpio_chip = {
	.name		= "spacemit-gpio-irqchip",
	.irq_ack	= spacemit_gpio_irq_ack,
	.irq_mask	= spacemit_gpio_irq_mask,
	.irq_unmask	= spacemit_gpio_irq_unmask,
	.irq_set_type	= spacemit_gpio_irq_set_type,
	.irq_print_chip	= spacemit_gpio_irq_print_chip,
	.flags		= IRQCHIP_IMMUTABLE | IRQCHIP_SKIP_SET_WAKE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static bool spacemit_of_node_instance_match(struct gpio_chip *gc, unsigned int i)
{
	struct spacemit_gpio_bank *gb = gpiochip_get_data(gc);
	struct spacemit_gpio *sg = gb->sg;

	if (i >= SPACEMIT_NR_BANKS)
		return false;
	return (gc == &sg->sgb[i].chip.gc);
}

static int spacemit_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct spacemit_gpio_bank *gb = gpiochip_get_data(gc);
	u32 reg_offset = gb->bank_offset + gb->sg->data->reg_offsets->gplr;
	u32 val;

	regmap_read(gb->sg->rm_gpio, reg_offset, &val);
	return !!(val & BIT(offset));
}

static int spacemit_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct spacemit_gpio_bank *gb = gpiochip_get_data(gc);
	u32 reg_offset;

	if (value)
		reg_offset = gb->bank_offset + gb->sg->data->reg_offsets->gpsr;
	else
		reg_offset = gb->bank_offset + gb->sg->data->reg_offsets->gpcr;

	return regmap_write(gb->sg->rm_gpio, reg_offset, BIT(offset));
}

static int spacemit_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	struct spacemit_gpio_bank *gb = gpiochip_get_data(gc);
	u32 reg_offset = gb->bank_offset + gb->sg->data->reg_offsets->gcdr;

	regmap_write(gb->sg->rm_gpio, reg_offset, BIT(offset));
	return 0;
}

static int spacemit_gpio_direction_output(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct spacemit_gpio_bank *gb = gpiochip_get_data(gc);
	u32 reg_offset = gb->bank_offset + gb->sg->data->reg_offsets->gsdr;

	regmap_write(gb->sg->rm_gpio, reg_offset, BIT(offset));
	spacemit_gpio_set(gc, offset, value);
	return 0;
}

static int spacemit_gpio_add_bank(struct spacemit_gpio *sg,
				  int index, int irq)
{
	struct spacemit_gpio_bank *gb = &sg->sgb[index];
	struct gpio_chip *gc = &gb->chip.gc;
	struct device *dev = sg->dev;
	struct gpio_irq_chip *girq;
	int ret;

	gb->bank_offset = sg->data->bank_offsets[index];
	gc->label	= devm_kasprintf(dev, GFP_KERNEL, "%s-bank%d", dev_name(dev), index);
	gc->request	= gpiochip_generic_request;
	gc->free	= gpiochip_generic_free;
	gc->get		= spacemit_gpio_get;
	gc->set		= spacemit_gpio_set;
	gc->parent	= dev;
	gc->direction_input	= spacemit_gpio_direction_input;
	gc->direction_output	= spacemit_gpio_direction_output;

	gb->sg = sg;

	gc->base		= index * SPACEMIT_NR_GPIOS_PER_BANK;
	gc->ngpio		= SPACEMIT_NR_GPIOS_PER_BANK;
	gc->of_gpio_n_cells	= 3;
	gc->of_node_instance_match = spacemit_of_node_instance_match;

	girq			= &gc->irq;
	girq->threaded		= true;
	girq->handler		= handle_simple_irq;

	gpio_irq_chip_set_chip(girq, &spacemit_gpio_chip);

	/* Disable Interrupt */
	regmap_write(gb->sg->rm_gpio, gb->bank_offset + gb->sg->data->reg_offsets->gapmask, 0);
	/* Disable Edge Detection Settings */
	regmap_write(gb->sg->rm_gpio, gb->bank_offset + gb->sg->data->reg_offsets->grer, 0x0);
	regmap_write(gb->sg->rm_gpio, gb->bank_offset + gb->sg->data->reg_offsets->gfer, 0x0);
	/* Clear Interrupt */
	regmap_write(gb->sg->rm_gpio,
		     gb->bank_offset + gb->sg->data->reg_offsets->gcrer,
		     0xffffffff);
	regmap_write(gb->sg->rm_gpio,
		     gb->bank_offset + gb->sg->data->reg_offsets->gcfer,
		     0xffffffff);

	ret = devm_request_threaded_irq(dev, irq, NULL,
					spacemit_gpio_irq_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					gb->chip.gc.label, gb);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to register IRQ\n");

	ret = devm_gpiochip_add_data(dev, gc, gb);
	if (ret)
		return ret;

	/* Distuingish IRQ domain, for selecting threecells mode */
	irq_domain_update_bus_token(girq->domain, DOMAIN_BUS_WIRED);

	return 0;
}

static int spacemit_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct spacemit_gpio *sg;
	struct clk *core_clk, *bus_clk;
	const struct spacemit_gpio_data *data;
	int i, irq, ret;

	sg = devm_kzalloc(dev, sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return -ENOMEM;

	sg->dev = dev;

	data = of_device_get_match_data(dev);
	if (!data)
		return dev_err_probe(dev, -EINVAL, "Unsupported compatible\n");

	sg->data = data;

	sg->rm_gpio = syscon_regmap_lookup_by_phandle(np, "syscon-gpio");
	if (IS_ERR(sg->rm_gpio))
		return dev_err_probe(dev, PTR_ERR(sg->rm_gpio),
				     "Failed to get syscon-gpio regmap\n");

	sg->rm_gpio_edge = syscon_regmap_lookup_by_phandle(np, "syscon-gpio-edge");
	if (IS_ERR(sg->rm_gpio_edge))
		return dev_err_probe(dev, PTR_ERR(sg->rm_gpio_edge),
				     "Failed to get syscon-gpio-edge regmap\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	core_clk = devm_clk_get_enabled(dev, "core");
	if (IS_ERR(core_clk))
		return dev_err_probe(dev, PTR_ERR(core_clk), "failed to get clock\n");

	bus_clk = devm_clk_get_enabled(dev, "bus");
	if (IS_ERR(bus_clk))
		return dev_err_probe(dev, PTR_ERR(bus_clk), "failed to get bus clock\n");

	for (i = 0; i < SPACEMIT_NR_BANKS; i++) {
		ret = spacemit_gpio_add_bank(sg, i, irq);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct spacemit_gpio_data k1_data = {
	.reg_offsets = &k1_regs,
	.bank_offsets = k1_bank_offsets,
};

static const struct spacemit_gpio_data k3_data = {
	.reg_offsets = &k3_regs,
	.bank_offsets = k3_bank_offsets,
};

static const struct of_device_id spacemit_gpio_dt_ids[] = {
	{ .compatible = "spacemit,k1-gpio", .data = &k1_data },
	{ .compatible = "spacemit,k3-gpio", .data = &k3_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_gpio_dt_ids);

static struct platform_driver spacemit_gpio_driver = {
	.probe		= spacemit_gpio_probe,
	.driver		= {
		.name	= "spacemit-gpio",
		.of_match_table = spacemit_gpio_dt_ids,
	},
};
module_platform_driver(spacemit_gpio_driver);

MODULE_AUTHOR("Yixun Lan <dlan@gentoo.org>");
MODULE_DESCRIPTION("GPIO driver for SpacemiT K1/K3 SoC");
MODULE_LICENSE("GPL");
