/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef __INNO_DP_API_H__
#define __INNO_DP_API_H__

#include "inno_conn.h"

struct inno_conn_t *inno_get_conn_module(enum modules module_id);

int inno_init(struct inno_conn_t *conn);
int inno_exit(struct inno_conn_t *conn);
bool inno_hpd_detect(struct inno_conn_t *conn);
int inno_get_edid(struct inno_conn_t *conn);
int inno_do_display(struct inno_conn_t *conn, struct drm_display_mode *mode);

#endif /* __INNO_DP_API_H__ */
