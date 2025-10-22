/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 */

#ifndef __INNO_DP_PHY_BOARD_H__
#define __INNO_DP_PHY_BOARD_H__

#include "inno_conn.h"
#include "inno_dp_common.h"

void innodp_phyboard_reset(struct inno_conn_t *conn);
void innodp_phyboard_set_swinglevel(struct inno_conn_t *conn);
void innodp_phyboard_link_config(struct inno_conn_t *conn);
void innodp_phyboard_set_tps(struct inno_conn_t *conn, uint32_t pattern);
void innodp_phyboard_video_enable(struct inno_conn_t *conn);
int innodp_phyboard_core_pll_cfg(struct dp_chip_t *inno);
void innodp_phyboard_pixel_pll_cfg(struct inno_conn_t *conn, uint32_t index);

#endif /* __INNO_DP_PHY_BOARD_H__ */
