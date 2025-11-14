// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit Generic power domain support over rpmi.
 *
 * Copyright (c) 2025 SPACEMIT, Co. Ltd.
 */

#ifndef __K3_PM_DOMIAN_H__
#define __K3_PM_DOMIAN_H__

#define RPMI_DOMAIN_NAME_LEN	16

enum rpmi_domain_state {
	RPMI_DEVICE_POWER_STATE_OFF = 0,
	RPMI_DEVICE_POWER_STATE_ON,
};

struct rpmi_domain_context {
	struct device *dev;
	struct mbox_chan *chan;
	struct mbox_client client;
	u32 max_msg_data_size;
};

struct rpmi_domain {
	struct rpmi_domain_context *context;
	u32 id;
	char name[RPMI_DOMAIN_NAME_LEN];
};

struct rpmi_get_num_pdomain_rx {
	u32 status;
	u32 num_domains;
};

struct rpmi_domain_get_attr_tx {
	u32 domain_id;
};

struct rpmi_domain_get_attr_rx {
	u32 status;
	u32 flags;
	u32 trans_latency;
	char name[RPMI_DOMAIN_NAME_LEN];
};

struct rpmi_domain_set_state_tx {
	u32 domain_id;
	u32 state;
};

struct rpmi_domain_set_state_rx {
	u32 status;
};

struct rpmi_domain_get_state_tx {
	u32 domain_id;
};

struct rpmi_domain_get_state_rx {
	u32 status;
	u32 state;
};

#endif
