/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _LINUX_SPACEMIT_K3_ESPI_H
#define _LINUX_SPACEMIT_K3_ESPI_H

#include <linux/errno.h>
#include <linux/kconfig.h>

struct device;

#if IS_ENABLED(CONFIG_SPACEMIT_ESPI_BUS)
int spacemit_k3_espi_recover(struct device *dev);
#else
static inline int spacemit_k3_espi_recover(struct device *dev)
{
	return -EOPNOTSUPP;
}
#endif

#endif /* _LINUX_SPACEMIT_K3_ESPI_H */
