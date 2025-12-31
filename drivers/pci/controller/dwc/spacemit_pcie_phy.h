/*
 * spacemit_pcie_phy.h
 * Header for Spacemit PCIe PHY initialization
 */

#ifndef _SPACEMIT_PCIE_PHY_H
#define _SPACEMIT_PCIE_PHY_H

#include <linux/types.h>

struct spacemit_pcie;

int spacemit_pcie_init_phy(int port_id);

#endif /* _SPACEMIT_PCIE_PHY_H */
