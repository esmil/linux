/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Furong Xu <0x1207@gmail.com>
 * stmmac FPE(802.3 Qbu) handling
 */
#ifndef _STMMAC_FPE_H_
#define _STMMAC_FPE_H_

#include <linux/types.h>
#include <linux/netdevice.h>

struct stmmac_priv;

bool ec_stmmac_fpe_supported(struct stmmac_priv *priv);
void ec_stmmac_fpe_init(struct stmmac_priv *priv);
void ec_stmmac_fpe_irq_status(struct stmmac_priv *priv);
int ec_stmmac_fpe_get_add_frag_size(struct stmmac_priv *priv);
void ec_stmmac_fpe_set_add_frag_size(struct stmmac_priv *priv, u32 add_frag_size);

int ec_dwmac5_fpe_map_preemption_class(struct net_device *ndev,
				    struct netlink_ext_ack *extack, u32 pclass);
int ec_dwxgmac3_fpe_map_preemption_class(struct net_device *ndev,
				      struct netlink_ext_ack *extack, u32 pclass);

extern const struct stmmac_fpe_reg ec_dwmac5_fpe_reg;
extern const struct stmmac_fpe_reg ec_dwxgmac3_fpe_reg;

#endif
