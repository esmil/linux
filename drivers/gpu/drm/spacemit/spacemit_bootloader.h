/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_BOOTLOADER_H_
#define _SPACEMIT_BOOTLOADER_H_

#include <linux/of_reserved_mem.h>

void spacemit_dpu_free_bootloader_mem(void);
void spacemit_dpu_set_bootloader_mem_release_target(unsigned int count);
int spacemit_dpu_bootloader_mem_setup(struct reserved_mem *rmem);

#endif
