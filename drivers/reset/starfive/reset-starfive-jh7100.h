// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 Emil Renner Berthing <kernel@esmil.dk>
 */

#ifndef RESET_JH7100_H
#define RESET_JH7100_H

#include <linux/platform_device.h>

int starfive_jh7100_reset_init(struct platform_device *pdev,
			       const u64 *asserted,
			       unsigned int status_offset,
			       unsigned int nr_resets);

#endif
