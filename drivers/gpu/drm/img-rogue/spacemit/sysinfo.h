/* SPDX-License-Identifier: GPL-2.0-only OR MIT */

#if !defined(__SYSINFO_H__)
#define __SYSINFO_H__

/*!< System specific poll/timeout details */
#define MAX_HW_TIME_US                           (10000000)
#define DEVICES_WATCHDOG_POWER_ON_SLEEP_TIMEOUT  (3000)
#define DEVICES_WATCHDOG_POWER_OFF_SLEEP_TIMEOUT (3600000)
#define WAIT_TRY_COUNT                           (10000)
#define EVENT_OBJECT_TIMEOUT_US		(120000000ULL)

#define SYS_RGX_OF_COMPATIBLE "img,rgx"

#if defined(__linux__)
#define SYS_RGX_DEV_NAME "spacemit"
#endif

#endif	/* !defined(__SYSINFO_H__) */
