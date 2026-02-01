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
	[INNO_CONN_DP] = {
		.conn_id = INNO_CONN_DP,
		.valid = true,
		.flag = INNO_CONN_FLAG_NONE,
		.regbase = DP_REGISTER_BASE_ADDRESS,
		.regsize = DP_REGISTER_SIZE,
		.use_phy_board = false,
		.phy_i2c_id = 3,
		.lane_count = 4,
		.lane_rate = INNODP_LINK_BW_2_7,
		.vic  = INNO_VIC_1920x1080, /* use vic=1080p when edid valid. */
		.width = 1920,
		.height = 1080,
		.edp_enable = false,
		.edid_valid = false,
		.func = &g_inno_dp_func,
	},
	[INNO_CONN_EDP] = {
		.conn_id = INNO_CONN_EDP,
		.valid = true,
		.flag = INNO_CONN_FLAG_NONE,
		.regbase = DP_REGISTER_BASE_ADDRESS,
		.regsize = DP_REGISTER_SIZE,
		.use_phy_board = false,
		.phy_i2c_id = 3,
		.lane_count = 4,
		.lane_rate = INNODP_LINK_BW_2_7,
		.vic  = INNO_VIC_1920x1080, /* use vic=1080p when edid valid. */
		.width = 1920,
		.height = 1080,
		.edp_enable = true,
		.edid_valid = false,
		.func = &g_inno_dp_func,
	},
};

struct inno_conn_t *inno_get_conn_module(enum modules module_id)
{
	if (module_id >= INNO_CONN_MAX)
		return NULL;

	return &g_inno_conn_table[module_id];
}

int inno_init(struct inno_conn_t *conn)
{
	if (conn->func->init)
		conn->func->init(conn);

	return 0;
}


int inno_exit(struct inno_conn_t *conn)
{
	if (conn->func->exit)
		conn->func->exit(conn);

	return 0;
}

bool inno_hpd_detect(struct inno_conn_t *conn)
{
	if (conn->func->hpd_detect)
		return conn->func->hpd_detect(conn);

	return false;
}

int inno_get_edid(struct inno_conn_t *conn)
{
	int ret;
	uint8_t edid[256];

	memset(edid, 0, sizeof(edid));
	if (conn->func->get_edid) {
		ret = conn->func->get_edid(conn, edid);
		if (ret || !inno_edid_is_valid((struct edid *)edid)) {
			inno_mode_copy_cea(&conn->out_mode, conn->vic);
			osal_printf("%s() get edid failed\n", __func__);
			return ret;
		} else {
			conn->edid_valid = true;
			memset(conn->edid_data, 0, sizeof(conn->edid_data));
			memcpy(conn->edid_data, edid, sizeof(edid));
			osal_printf("%s() get edid successful\n", __func__);

			// for(int i = 0; i < 256; i += 8){
			// 	osal_printf("EDID 0x%x: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\r\n", i,
			// 	conn->edid_data[i], conn->edid_data[i+1], conn->edid_data[i+2], conn->edid_data[i+3],
			// 	conn->edid_data[i+4], conn->edid_data[i+5], conn->edid_data[i+6], conn->edid_data[i+7]);
			// }
		}
	}

	// if (conn->func->show_edid) {
	// 	if (conn->func->show_edid(conn, edid) < 0) {
	// 		osal_printf("[%d]show edid failed\n", conn->conn_id);
	// 	}
	// }

	return 0;
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

	return 0;
}
