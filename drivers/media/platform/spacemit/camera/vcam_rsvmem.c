/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 SPACEMIT Micro Limited
 * All Rights Reserved.
 */
/* #define DEBUG */

#include "linux/io.h"
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "spacemit_rsvmem"

struct spacemitRsvdMemDev {
	resource_size_t paddr;
	void *vaddr;
};

static int spacemit_rsvmem_probe(struct platform_device *pdev)
{
	struct spacemitRsvdMemDev *lp;
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct resource r;
	int rc;

	/* Get reserved memory region from Device-tree */
	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!np) {
		dev_err(dev, "No %s specified\n", "memory-region");
		return -ENODEV;
	}

	rc = of_address_to_resource(np, 0, &r);
	if (rc) {
		dev_err(dev, "No memory address assigned to the region\n");
		return rc;
	}

	lp = kzalloc(sizeof(struct spacemitRsvdMemDev), GFP_KERNEL);
	if (lp == NULL) {
		dev_err(dev, "No memmory\n");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, lp);
	lp->paddr = r.start;
	lp->vaddr = memremap(r.start, resource_size(&r), MEMREMAP_WC);
	dev_info(dev,
		 "Allocated reserved memory, vaddr: 0x%0llX, paddr: 0x%0llX\n",
		 (u64)lp->vaddr, lp->paddr);

	return rc;
}

static void spacemit_rsvmem_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spacemitRsvdMemDev *lp = dev_get_drvdata(dev);
    memunmap(lp->vaddr);
}

#ifdef CONFIG_OF
static struct of_device_id spacemit_rsvmem_of_match[] = {
	{
		.compatible = "spacemit,reserved-memory",
	},
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, spacemit_rsvmem_of_match);
#else
#define spacemit_rsvmem_of_match
#endif

static struct platform_driver spacemit_rsvmem_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= spacemit_rsvmem_of_match,
	},
	.probe		= spacemit_rsvmem_probe,
	.remove		= spacemit_rsvmem_remove,
};

static int __init spacemit_rsvmem_init(void)
{
	return platform_driver_register(&spacemit_rsvmem_driver);
}

static void __exit spacemit_rsvmem_exit(void)
{
	platform_driver_unregister(&spacemit_rsvmem_driver);
}

module_init(spacemit_rsvmem_init);
module_exit(spacemit_rsvmem_exit);

MODULE_AUTHOR("SPACEMIT Inc.");
MODULE_DESCRIPTION("SPACEMIT Reserved Memory Driver");
MODULE_LICENSE("GPL v2");
