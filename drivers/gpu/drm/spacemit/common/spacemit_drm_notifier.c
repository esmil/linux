// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/notifier.h>

#include "spacemit_drm_notifier.h"

BLOCKING_NOTIFIER_HEAD(drm_notifier_list);

/*	spacemit_drm_register_client - register a client notifier  */
int spacemit_drm_notifier_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&drm_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(spacemit_drm_notifier_register_client);

/*  spacemit_drm_unregister_client - unregister a client notifier  */
int spacemit_drm_notifier_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&drm_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(spacemit_drm_notifier_unregister_client);

/*  spacemit_drm_notifier_call_chain - notify clients of drm_events  */
int spacemit_drm_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&drm_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(spacemit_drm_notifier_call_chain);
