// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/init.h>
#include <linux/types.h>

#include "inno_dp_api.h"
#include "inno_conn.h"
#include "inno_dp_reg.h"
#include "inno_dp.h"
#include "inno_edid.h"
#include "inno_dp_common.h"

extern struct inno_conn_func_t g_inno_dp_func;

struct inno_conn_t g_inno_conn_table[INNO_CONN_MAX] = {
	[INNO_CONN_DP0] = {
		.conn_id = INNO_CONN_DP0,
		.valid = true,
		.flag = INNO_CONN_FLAG_NONE,
		.regbase = DP_REGISTER_BASE_ADDRESS,
		.regsize = DP_REGISTER_SIZE,
		.use_phy_board = true,
		.phy_i2c_id = 0,
		.lane_count = 2, /* support 2lanes */
		.lane_rate = INNODP_LINK_BW_2_7,
		/* .lane_rate = INNODP_LINK_BW_1_62, */
		.vic  = INNO_VIC_1920x1200, /* use vic=1080p when edid valid. */
		.width = 1920,
		.height = 1200,
		.func = &g_inno_dp_func,
	},
};

struct inno_conn_t *inno_get_conn_module(enum modules module_id)
{
	if (module_id >= INNO_CONN_MAX)
		return NULL;

	return &g_inno_conn_table[module_id];
}

int inno_do_display(struct inno_conn_t *conn, struct drm_display_mode *mode)
{
	int ret = 0;

	/* init modules */
	if (conn->func->init)
		conn->func->init(conn);

	/* copy out_mode to conn */
	if (!mode) {
		osal_printf_func("use default vic....\n");
		inno_mode_copy_cea(&conn->out_mode, conn->vic);
	} else {
		inno_mode_copy(&conn->out_mode, mode);
	}

	osal_printf_func("vic mode clock: %d, h:%d, v:%d, vfresh:%d vtotal:%d, htotal: %d\n",
			 conn->out_mode.clock, conn->out_mode.hdisplay,
			 conn->out_mode.vdisplay,
			 drm_mode_vrefresh(&conn->out_mode),
			 conn->out_mode.vtotal, conn->out_mode.htotal);

	if (!conn->is_enable && conn->func->modeset) {
		ret = conn->func->modeset(conn, &conn->out_mode);
		if (ret) {
			osal_printf_func("[%d]modeset failed\n\n",
					 conn->conn_id);
			return -1;
		}
	}

	if (conn->func->disable)
		conn->func->disable(conn);

	if (!conn->is_enable && conn->func->enable) {
		ret = conn->func->enable(conn);
		if (ret) {
			osal_printf_func("[%d]enable failed\n\n",
					 conn->conn_id);
			return -1;
		}
	}

	return 0;
}
