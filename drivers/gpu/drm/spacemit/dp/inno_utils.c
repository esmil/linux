// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>

#include "inno_utils.h"
#include "inno_conn.h"

int osal_mem_init(struct inno_conn_t *conn)
{
	return 0;
}

void osal_mem_deinit(struct inno_conn_t *conn)
{
}

int osal_i2c_init(struct inno_conn_t *conn)
{
	return 0;
}

void osal_i2c_deinit(struct inno_conn_t *conn)
{
}

void osal_usleep(uint32_t us)
{
	udelay(us);
}

void osal_msleep(uint32_t ms)
{
	msleep(ms);
}

void osal_write32(uint32_t offset, uint32_t val,
		  struct inno_conn_t *conn)
{
	*(volatile uint32_t *)(conn->reg_mmap_addr + offset) = val;
	osal_printf_func("[w] reg: %#x, val: %#x\n", offset, val);
}

uint32_t osal_read32(uint32_t offset, struct inno_conn_t *conn)
{
	uint32_t val = *(volatile uint32_t *)(conn->reg_mmap_addr + offset);

	osal_printf_func("[r] reg: %#x, val: %#x\n", offset, val);
	return val;
}

void osal_update_bits(uint32_t reg, uint32_t mask, uint32_t val,
		      struct inno_conn_t *conn)
{
	unsigned int tmp, orig;

	orig = osal_read32(reg, conn);
	tmp = orig & ~mask;
	tmp |= val & mask;

	osal_write32(reg, tmp, conn);
}

int osal_i2c_write8(uint32_t addr, uint32_t wdata,
		    struct inno_conn_t *conn)
{
	uint8_t reg_addr = addr & 0xff;
	uint8_t dev_addr = (addr >> 8) & 0xff;
	unsigned char buf[2];
	struct i2c_msg messages;

	buf[0] = reg_addr;
	buf[1] = wdata;
	messages.addr = dev_addr;
	messages.flags = 0;
	messages.len = 2;
	messages.buf = buf;

	if (i2c_transfer(conn->phy_i2c_fd, &messages, 1) < 0) {
		osal_printf_func("i2c_write_8bit operation failed\n");
		return -1;
	}
	osal_printf_func("[w]devaddr:0x%x, offset:%#x, val:0x%x\n",
			 dev_addr, reg_addr, wdata);
	return 0;
}

int osal_i2c_read8(uint32_t addr, struct inno_conn_t *conn)
{
	uint8_t reg_addr = addr & 0xff;
	uint8_t dev_addr = (addr >> 8) & 0xff;
	uint8_t buf = reg_addr;
	struct i2c_msg messages;

	messages.addr = dev_addr;
	messages.flags = 0;
	messages.len = 1;
	messages.buf = &buf;

	if (i2c_transfer(conn->phy_i2c_fd, &messages, 1) < 0) {
		osal_printf_func("i2c_write_8bit operation failed\n");
		return -1;
	}

	messages.flags = I2C_M_RD;
	if (i2c_transfer(conn->phy_i2c_fd, &messages, 1) < 0) {
		osal_printf_func("i2c_write_8bit operation failed\n");
		return -1;
	}

	osal_printf_func("[r]devaddr:0x%x, offset:%#x, val:0x%x\n",
			 dev_addr, reg_addr, buf);
	return buf;
}

void *osal_malloc(uint32_t size)
{
	return kmalloc(size, GFP_KERNEL);
}

void osal_free(void *ptr)
{
	kfree(ptr);
}

void *osal_memset(void *s, int32_t c, size_t n)
{
	return memset(s, c, n);
}

void *osal_memcpy(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

int osal_memcmp(void *sl, const void *s2, size_t n)
{
	return memcmp(sl, s2, n);
}
