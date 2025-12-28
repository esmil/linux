/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef __INNO_UTILS_H__
#define __INNO_UTILS_H__

#include <linux/init.h>
#include <linux/types.h>
#include <linux/printk.h>

#define osal_printf(fmt, args...) pr_info(fmt, ##args)
#define osal_printf_func(fmt, args...) \
	pr_err("%s:%d: " fmt, __func__, __LINE__, ##args)

struct inno_conn_t;

int osal_mem_init(struct inno_conn_t *conn);
void osal_mem_deinit(struct inno_conn_t *conn);
int osal_i2c_init(struct inno_conn_t *conn);
void osal_i2c_deinit(struct inno_conn_t *conn);
void osal_usleep(uint32_t us);
void osal_msleep(uint32_t ms);
void osal_write32(uint32_t offset, uint32_t val,
		  struct inno_conn_t *conn);
uint32_t osal_read32(uint32_t offset, struct inno_conn_t *conn);
void osal_update_bits(uint32_t reg, uint32_t mask, uint32_t val,
		      struct inno_conn_t *conn);
int osal_i2c_write8(uint32_t addr, uint32_t wdata,
		    struct inno_conn_t *conn);
int osal_i2c_read8(uint32_t addr, struct inno_conn_t *conn);
void *osal_malloc(uint32_t size);
void osal_free(void *ptr);
void *osal_memset(void *s, int32_t c, size_t n);
void *osal_memcpy(void *dest, const void *src, size_t n);
int osal_memcmp(void *sl, const void *s2, size_t n);

#endif
