/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_DPHY_H_
#define _SPACEMIT_DPHY_H_

#include <asm/types.h>
#include <drm/drm_print.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "spacemit_lib.h"

#define DPHY_BITCLK_DEFAULT	614400000
#define DPHY_ESCCLK_DEFAULT	51200000

enum spacemit_dphy_lane_map {
	DPHY_LANE_MAP_0123 = 0,
	DPHY_LANE_MAP_0312 = 1,
	DPHY_LANE_MAP_0231 = 2,
	DPHY_LANE_MAP_MAX
};

enum spacemit_dphy_status {
	DPHY_STATUS_UNINIT = 0,
	DPHY_STATUS_INIT = 1,
	DPHY_STATUS_MAX
};

enum spacemit_dphy_bit_clk_src {
	DPHY_BIT_CLK_SRC_PLL5 = 1,
	DPHY_BIT_CLK_SRC_MUX = 2,
	DPHY_BIT_CLK_SRC_MAX
};

struct spacemit_dphy_timing {
	uint32_t hs_prep_constant;    /* Unit: ns. */
	uint32_t hs_prep_ui;
	uint32_t hs_zero_constant;
	uint32_t hs_zero_ui;
	uint32_t hs_trail_constant;
	uint32_t hs_trail_ui;
	uint32_t hs_exit_constant;
	uint32_t hs_exit_ui;
	uint32_t ck_zero_constant;
	uint32_t ck_zero_ui;
	uint32_t ck_trail_constant;
	uint32_t ck_trail_ui;
	uint32_t req_ready;
	uint32_t wakeup_constant;
	uint32_t wakeup_ui;
	uint32_t lpx_constant;
	uint32_t lpx_ui;
};

struct spacemit_dphy_ctx {
	u8 id;
	void __iomem *base_addr;
	uint32_t phy_freq; /*kHz*/
	uint32_t lane_num;
	uint32_t esc_clk; /*kHz*/
	uint32_t half_pll5;
	enum spacemit_dphy_bit_clk_src clk_src;
	struct spacemit_dphy_timing dphy_timing;
	uint32_t dphy_status0; /*status0 reg*/
	uint32_t dphy_status1; /*status1 reg*/
	uint32_t dphy_status2; /*status2 reg*/

	enum spacemit_dphy_status status;
};

struct dphy_core_ops {
	int (*parse_dt)(struct spacemit_dphy_ctx *ctx, struct device_node *np);
	void (*init)(struct spacemit_dphy_ctx *ctx);
	void (*uninit)(struct spacemit_dphy_ctx *ctx);
	void (*reset)(struct spacemit_dphy_ctx *ctx);
	void (*get_status)(struct spacemit_dphy_ctx *ctx);
};

struct spacemit_dphy {
	struct device dev;
	struct spacemit_dphy_ctx ctx;
	struct dphy_core_ops *core;
};

extern struct list_head dphy_core_head;

#define dphy_core_ops_register(entry) \
	disp_ops_register(entry, &dphy_core_head)
#define dphy_core_ops_attach(str) \
	disp_ops_attach(str, &dphy_core_head)

int spacemit_dphy_resume(struct spacemit_dphy *dphy);
int spacemit_dphy_suspend(struct spacemit_dphy *dphy);
int spacemit_dphy_reset(struct spacemit_dphy *dphy);
int spacemit_dphy_get_status(struct spacemit_dphy *dphy);

#endif
