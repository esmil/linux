/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __CROS_EC_OEM_COMMANDS_H
#define __CROS_EC_OEM_COMMANDS_H

#include <linux/bits.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/types.h>

/*
 * Convert a board-specific command offset into a full host-command value.
 * Keep this helper in a local OEM header so vendor extensions are easy to add.
 */
#ifndef EC_CMD_BOARD_SPECIFIC
#define EC_CMD_BOARD_SPECIFIC(command) EC_PRIVATE_HOST_COMMAND_VALUE(command)
#endif

/* Board-specific command used to sync keyboard lock LEDs. */
#define EC_CMD_OEM_KB_LOCK_LED EC_CMD_BOARD_SPECIFIC(0x00)

#define EC_OEM_KB_LOCK_LED_SCROLL BIT(0)
#define EC_OEM_KB_LOCK_LED_NUM    BIT(1)
#define EC_OEM_KB_LOCK_LED_CAPS   BIT(2)

struct ec_params_oem_kb_lock_led {
	u8 lock_leds;
} __packed;

#endif /* __CROS_EC_OEM_COMMANDS_H */
