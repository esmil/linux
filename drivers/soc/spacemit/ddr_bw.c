// SPDX-License-Identifier: GPL-2.0
/*
 * Spacemit DDR bandwidth monitor driver
 *
 * Copyright (C) 2026 Spacemit
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define PERF_BASE0_PHYS     0xCB400000
#define PERF_BASE1_PHYS     0xCC400000
#define PERF_MAP_SIZE       0x1000
#define PERF_BASE_NUM       2
#define DDR_DBG0_PHYS       0xCB60000C
#define DDR_DBG1_PHYS       0xCC60000C

/* IOCTL Definitions */
#define DDR_PERF_MAGIC      'D'
#define IOC_INIT_PORT       _IOW(DDR_PERF_MAGIC, 1, int)
#define IOC_GET_STAT        _IOWR(DDR_PERF_MAGIC, 2, struct ddr_perf_stat)

/* Data structure shared with user space */
struct ddr_perf_stat {
	__u32 port_id;       /* IN: 0 to 4 */
	__u32 window;        /* OUT */
	__u32 read_bytes;    /* OUT (cntl 16) */
	__u32 write_bytes;   /* OUT (cntl 18) */
	__u32 read_reqs;     /* OUT (cntl 1) */
	__u32 write_reqs;    /* OUT (cntl 9) */
	__u32 read_latency;  /* OUT (cntl 5) */
};

struct ddr_perf_raw_stat {
	__u64 window;
	__u64 read_bytes;
	__u64 write_bytes;
	__u64 read_reqs;
	__u64 write_reqs;
	__u64 read_latency;
};

struct ddr_perf_prev_stat {
	bool valid;
	struct ddr_perf_raw_stat stat;
};

static void __iomem *perf_base[PERF_BASE_NUM];
static void __iomem *dbg_reg0;
static void __iomem *dbg_reg1;
static struct ddr_perf_prev_stat prev_stats[5];
static DEFINE_MUTEX(perf_lock);

/* Helper to read a multiplexed AXI monitor register */
static u32 read_axi_mon(void __iomem *base, u32 port, u32 cntl_sel)
{
	u32 ctrl_offset = 0x30 + (port * 0x10);
	u32 data_offset = 0x3C + (port * 0x10);
	u32 val;

	/* Read CTRL, clear bottom 5 bits, set cntl_sel */
	val = readl(base + ctrl_offset);
	val &= ~0x1f;
	val |= (cntl_sel & 0x1f);
	writel(val, base + ctrl_offset);

	/* Read DATA */
	return readl(base + data_offset);
}

static u32 read_axi_mon_sum(u32 port, u32 cntl_sel)
{
	u32 i;
	u32 sum = 0;

	for (i = 0; i < PERF_BASE_NUM; i++)
		sum += read_axi_mon(perf_base[i], port, cntl_sel);

	return sum;
}

static u32 ddr_perf_clamp_u64_to_u32(u64 val)
{
	return val > U32_MAX ? U32_MAX : (u32)val;
}

static void ddr_perf_read_current_stat(struct ddr_perf_stat *stat,
				       struct ddr_perf_raw_stat *raw_stat)
{
	raw_stat->window = (u64)read_axi_mon_sum(stat->port_id, 0) * 16ULL;
	raw_stat->read_reqs = (u64)read_axi_mon_sum(stat->port_id, 1) * 16ULL;
	raw_stat->write_reqs = (u64)read_axi_mon_sum(stat->port_id, 9) * 16ULL;
	raw_stat->read_bytes = (u64)read_axi_mon_sum(stat->port_id, 16) * 16ULL;
	raw_stat->write_bytes = (u64)read_axi_mon_sum(stat->port_id, 18) * 16ULL;
	raw_stat->read_latency = (u64)read_axi_mon_sum(stat->port_id, 5) * 16ULL;

	stat->window = ddr_perf_clamp_u64_to_u32(raw_stat->window);
	stat->read_reqs = ddr_perf_clamp_u64_to_u32(raw_stat->read_reqs);
	stat->write_reqs = ddr_perf_clamp_u64_to_u32(raw_stat->write_reqs);
	stat->read_bytes = ddr_perf_clamp_u64_to_u32(raw_stat->read_bytes);
	stat->write_bytes = ddr_perf_clamp_u64_to_u32(raw_stat->write_bytes);
	stat->read_latency = ddr_perf_clamp_u64_to_u32(raw_stat->read_latency);
}

static void ddr_perf_calc_delta(struct ddr_perf_stat *cur,
				struct ddr_perf_raw_stat *cur_raw,
				struct ddr_perf_prev_stat *prev)
{
	struct ddr_perf_raw_stat old;

	if (!prev->valid) {
		prev->valid = true;
		prev->stat = *cur_raw;
		cur->window = 0;
		cur->read_reqs = 0;
		cur->write_reqs = 0;
		cur->read_bytes = 0;
		cur->write_bytes = 0;
		cur->read_latency = 0;
		return;
	}

	old = prev->stat;

	cur->window = ddr_perf_clamp_u64_to_u32(cur_raw->window - old.window);
	cur->read_reqs = ddr_perf_clamp_u64_to_u32(cur_raw->read_reqs - old.read_reqs);
	cur->write_reqs = ddr_perf_clamp_u64_to_u32(cur_raw->write_reqs - old.write_reqs);
	cur->read_bytes = ddr_perf_clamp_u64_to_u32(cur_raw->read_bytes - old.read_bytes);
	cur->write_bytes = ddr_perf_clamp_u64_to_u32(cur_raw->write_bytes - old.write_bytes);
	cur->read_latency = ddr_perf_clamp_u64_to_u32(cur_raw->read_latency - old.read_latency);

	prev->stat = *cur_raw;
}

static long ddr_perf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ddr_perf_stat stat;
	struct ddr_perf_raw_stat raw_stat;
	u32 port;
	u32 val;
	u32 i;

	switch (cmd) {
	case IOC_INIT_PORT:
		if (get_user(port, (u32 __user *)arg))
			return -EFAULT;
		if (port > 4)
			return -EINVAL;

		mutex_lock(&perf_lock);
		/* Enable AXI Monitor (Bit 31) */
		for (i = 0; i < PERF_BASE_NUM; i++) {
			val = readl(perf_base[i] + 0x30 + (port * 0x10));
			val |= BIT(31);
			val |= BIT(29);
			writel(val, perf_base[i] + 0x30 + (port * 0x10));
		}
		mutex_unlock(&perf_lock);
		break;

	case IOC_GET_STAT:
		if (copy_from_user(&stat, (void __user *)arg, sizeof(stat)))
			return -EFAULT;
		if (stat.port_id > 4)
			return -EINVAL;

		mutex_lock(&perf_lock);
		ddr_perf_read_current_stat(&stat, &raw_stat);
		ddr_perf_calc_delta(&stat, &raw_stat, &prev_stats[stat.port_id]);
		mutex_unlock(&perf_lock);

		if (copy_to_user((void __user *)arg, &stat, sizeof(stat)))
			return -EFAULT;
		break;

	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations ddr_perf_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = ddr_perf_ioctl,
};

static struct miscdevice ddr_perf_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "ddr_perf",
	.fops  = &ddr_perf_fops,
};

static int __init ddr_perf_init(void)
{
	perf_base[0] = ioremap(PERF_BASE0_PHYS, PERF_MAP_SIZE);
	if (!perf_base[0]) {
		pr_err("DDR PERF: Failed to map memory at 0x%08x\n", PERF_BASE0_PHYS);
		return -ENOMEM;
	}

	perf_base[1] = ioremap(PERF_BASE1_PHYS, PERF_MAP_SIZE);
	if (!perf_base[1]) {
		pr_err("DDR PERF: Failed to map memory at 0x%08x\n", PERF_BASE1_PHYS);
		iounmap(perf_base[0]);
		perf_base[0] = NULL;
		return -ENOMEM;
	}

	dbg_reg0 = ioremap(DDR_DBG0_PHYS, sizeof(u32));
	if (!dbg_reg0) {
		pr_err("DDR PERF: Failed to map memory at 0x%08x\n", DDR_DBG0_PHYS);
		iounmap(perf_base[1]);
		iounmap(perf_base[0]);
		perf_base[1] = NULL;
		perf_base[0] = NULL;
		return -ENOMEM;
	}

	dbg_reg1 = ioremap(DDR_DBG1_PHYS, sizeof(u32));
	if (!dbg_reg1) {
		pr_err("DDR PERF: Failed to map memory at 0x%08x\n", DDR_DBG1_PHYS);
		iounmap(dbg_reg0);
		dbg_reg0 = NULL;
		iounmap(perf_base[1]);
		iounmap(perf_base[0]);
		perf_base[1] = NULL;
		perf_base[0] = NULL;
		return -ENOMEM;
	}

	return misc_register(&ddr_perf_misc);
}

static void __exit ddr_perf_exit(void)
{
	misc_deregister(&ddr_perf_misc);
	iounmap(dbg_reg1);
	iounmap(dbg_reg0);
	iounmap(perf_base[1]);
	iounmap(perf_base[0]);
}

module_init(ddr_perf_init);
module_exit(ddr_perf_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("haodong.yan@spacemit.com");
MODULE_DESCRIPTION("DDR Performance Monitor Driver");
