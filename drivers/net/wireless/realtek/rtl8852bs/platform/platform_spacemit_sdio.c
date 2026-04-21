/******************************************************************************
 *
 * Copyright(c) 2013 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#include <drv_types.h>
#include <linux/of.h>
#include <linux/of_irq.h>

extern void spacemit_sdio_detect_change(int enable_scan);

/*
 * Find the wifi device_node under the sdio host in devicetree.
 * Expected dts layout:
 *   &sdio {
 *       wifi@1 {
 *           reg = <1>;
 *           compatible = "realtek,rtl8852bs";
 *           interrupts = <...>;
 *       };
 *   };
 */
static struct device_node *spacemit_wifi_get_of_node(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "realtek,rtl8852bs");
	return np;
}

void platform_wifi_get_oob_irq(int *oob_irq)
{
	struct device_node *np;
	int irq;

	np = spacemit_wifi_get_of_node();
	if (!np) {
		RTW_ERR("%s: no wifi node in devicetree\n", __func__);
		return;
	}

	irq = of_irq_get(np, 0);
	of_node_put(np);

	if (irq <= 0) {
		RTW_ERR("%s: no interrupt in wifi node (%d)\n", __func__, irq);
		return;
	}

	*oob_irq = irq;
	RTW_INFO("%s: oob_irq = %d from devicetree\n", __func__, irq);
}

void platform_wifi_mac_addr(u8 *mac_addr)
{

}

/*
 * Return:
 *	0:	power on successfully
 *	others:	power on failed
 */
int platform_wifi_power_on(void)
{
	int ret = 0;

	RTW_PRINT("\n");
	RTW_PRINT("=======================================================\n");
	RTW_PRINT("==== Launching Wi-Fi driver! (Powered by Spacemit) ====\n");
	RTW_PRINT("=======================================================\n");
	RTW_PRINT("Realtek %s WiFi driver (Powered by Spacemit,Ver %s) init.\n", DRV_NAME, DRIVERVERSION);
	spacemit_sdio_detect_change(1);

	return ret;
}

void platform_wifi_power_off(void)
{
	RTW_PRINT("\n");
	RTW_PRINT("=======================================================\n");
	RTW_PRINT("==== Dislaunching Wi-Fi driver! (Powered by Spacemit) ====\n");
	RTW_PRINT("=======================================================\n");
	RTW_PRINT("Realtek %s WiFi driver (Powered by Spacemit,Ver %s) init.\n", DRV_NAME, DRIVERVERSION);
	spacemit_sdio_detect_change(0);
}
