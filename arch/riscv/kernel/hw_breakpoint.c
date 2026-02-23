// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Qualcomm Technologies, Inc.
 */

#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <linux/kdebug.h>
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/cpuhotplug.h>

#include <asm/sbi.h>

/* Registered per-cpu bp/wp */
static DEFINE_PER_CPU(struct perf_event *, pcpu_hw_bp_events[HW_BP_NUM_MAX]);
static DEFINE_PER_CPU(unsigned long, ecall_lock_flags);
static DEFINE_PER_CPU(raw_spinlock_t, ecall_lock);

/* Per-cpu shared memory between S and M mode */
static union sbi_dbtr_shmem_entry __percpu *sbi_dbtr_shmem;

/* number of debug triggers on this cpu . */
static int dbtr_total_num __ro_after_init;
static int dbtr_type __ro_after_init;
static int dbtr_init __ro_after_init;

#if __riscv_xlen == 64
#define MEM_HI(_m)	0
#define MEM_LO(_m)	((u64)(_m))
#elif __riscv_xlen == 32
#define MEM_HI(_m)	((u64)(_m) >> 32)
#define MEM_LO(_m)	((u64)(_m) & 0xFFFFFFFFUL)
#else
#error "Unknown __riscv_xlen"
#endif

static int arch_smp_setup_sbi_shmem(unsigned int cpu)
{
	union sbi_dbtr_shmem_entry *dbtr_shmem;
	unsigned long shmem_pa;
	struct sbiret ret;
	int rc = 0;

	dbtr_shmem = per_cpu_ptr(sbi_dbtr_shmem, cpu);
	if (!dbtr_shmem) {
		pr_err("Invalid per-cpu shared memory for debug triggers\n");
		return -ENODEV;
	}

	shmem_pa = __pa(dbtr_shmem);

	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_SETUP_SHMEM,
			MEM_LO(shmem_pa), MEM_HI(shmem_pa), 0, 0, 0, 0);

	if (ret.error) {
		switch (ret.error) {
		case SBI_ERR_DENIED:
			pr_warn("%s: Access denied for shared memory at %lx\n",
				__func__, shmem_pa);
			rc = -EPERM;
			break;

		case SBI_ERR_INVALID_PARAM:
		case SBI_ERR_INVALID_ADDRESS:
			pr_warn("%s: Invalid address parameter (%lu)\n",
				__func__, ret.error);
			rc = -EINVAL;
			break;

		case SBI_ERR_ALREADY_AVAILABLE:
			pr_warn("%s: Shared memory is already set\n",
				__func__);
			rc = -EADDRINUSE;
			break;

		case SBI_ERR_FAILURE:
			pr_err("%s: Internal sdtrig state error\n",
			       __func__);
			rc = -ENXIO;
			break;

		default:
			pr_warn("%s: Unknown error %lu\n", __func__, ret.error);
			rc = -ENXIO;
			break;
		}
	}

	pr_info("CPU %d: HW Breakpoint shared memory registered.\n", cpu);

	return rc;
}

static int arch_smp_teardown_sbi_shmem(unsigned int cpu)
{
	struct sbiret ret;

	/* Disable shared memory */
	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_SETUP_SHMEM,
			-1UL, -1UL, 0, 0, 0, 0);

	if (ret.error) {
		switch (ret.error) {
		case SBI_ERR_DENIED:
			pr_err("%s: Access denied for shared memory.\n",
			       __func__);
			break;

		case SBI_ERR_INVALID_PARAM:
		case SBI_ERR_INVALID_ADDRESS:
			pr_err("%s: Invalid address parameter (%lu)\n",
			       __func__, ret.error);
			break;

		case SBI_ERR_ALREADY_AVAILABLE:
			pr_err("%s: Shared memory is already set\n",
			       __func__);
			break;
		case SBI_ERR_FAILURE:
			pr_err("%s: Internal sdtrig state error\n",
			       __func__);
			break;
		default:
			pr_err("%s: Unknown error %lu\n", __func__, ret.error);
			break;
		}
	}

	pr_warn("CPU %d: HW Breakpoint shared memory disabled.\n", cpu);

	return 0;
}

static void init_sbi_dbtr(void)
{
	unsigned long tdata1;
	struct sbiret ret;

	if (sbi_probe_extension(SBI_EXT_DBTR) <= 0) {
		pr_warn("%s: SBI_EXT_DBTR is not supported\n", __func__);
		dbtr_total_num = 0;
		goto done;
	}

	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_NUM_TRIGGERS,
			0, 0, 0, 0, 0, 0);
	if (ret.error) {
		pr_warn("%s: Failed to detect triggers\n", __func__);
		dbtr_total_num = 0;
		goto done;
	}

	tdata1 = 0;
	tdata1 = RV_DBTR_SET_TDATA1_TYPE(tdata1, RV_DBTR_TRIG_MCONTROL6);

	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_NUM_TRIGGERS,
			tdata1, 0, 0, 0, 0, 0);
	if (ret.error) {
		pr_warn("%s: failed to detect mcontrol6 triggers\n", __func__);
	} else if (!ret.value) {
		pr_warn("%s: type 6 triggers not available\n", __func__);
	} else {
		dbtr_total_num = ret.value;
		dbtr_type = RV_DBTR_TRIG_MCONTROL6;
		pr_warn("%s: mcontrol6 trigger available.\n", __func__);
		goto done;
	}

	/* fallback to type 2 triggers if type 6 is not available */

	tdata1 = 0;
	tdata1 = RV_DBTR_SET_TDATA1_TYPE(tdata1, RV_DBTR_TRIG_MCONTROL);

	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_NUM_TRIGGERS,
			tdata1, 0, 0, 0, 0, 0);
	if (ret.error) {
		pr_warn("%s: failed to detect mcontrol triggers\n", __func__);
	} else if (!ret.value) {
		pr_warn("%s: type 2 triggers not available\n", __func__);
	} else {
		dbtr_total_num = ret.value;
		dbtr_type = RV_DBTR_TRIG_MCONTROL;
		goto done;
	}

done:
	dbtr_init = 1;
}

int hw_breakpoint_slots(int type)
{
	/*
	 * We can be called early, so don't rely on
	 * static variables being initialised.
	 */

	if (!dbtr_init)
		init_sbi_dbtr();

	return dbtr_total_num;
}

int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw)
{
	unsigned int len;
	unsigned long va;

	va = hw->address;
	len = hw->len;

	return (va >= TASK_SIZE) && ((va + len - 1) >= TASK_SIZE);
}

static int rv_init_mcontrol_trigger(const struct perf_event_attr *attr,
				    struct arch_hw_breakpoint *hw)
{
	switch (attr->bp_type) {
	case HW_BREAKPOINT_X:
		hw->type = RV_DBTR_BP;
		RV_DBTR_SET_MC_EXEC_BIT(hw->tdata1);
		break;
	case HW_BREAKPOINT_R:
		hw->type = RV_DBTR_WP;
		RV_DBTR_SET_MC_LOAD_BIT(hw->tdata1);
		break;
	case HW_BREAKPOINT_W:
		hw->type = RV_DBTR_WP;
		RV_DBTR_SET_MC_STORE_BIT(hw->tdata1);
		break;
	case HW_BREAKPOINT_RW:
		hw->type = RV_DBTR_WP;
		RV_DBTR_SET_MC_LOAD_BIT(hw->tdata1);
		RV_DBTR_SET_MC_STORE_BIT(hw->tdata1);
		break;
	default:
		return -EINVAL;
	}

	switch (attr->bp_len) {
	case HW_BREAKPOINT_LEN_1:
		hw->len = 1;
		hw->tdata1 = RV_DBTR_SET_MC_SIZELO(hw->tdata1, 1);
		break;
	case HW_BREAKPOINT_LEN_2:
		hw->len = 2;
		hw->tdata1 = RV_DBTR_SET_MC_SIZELO(hw->tdata1, 2);
		break;
	case HW_BREAKPOINT_LEN_4:
		hw->len = 4;
		hw->tdata1 = RV_DBTR_SET_MC_SIZELO(hw->tdata1, 3);
		break;
#if __riscv_xlen >= 64
	case HW_BREAKPOINT_LEN_8:
		hw->len = 8;
		hw->tdata1 = RV_DBTR_SET_MC_SIZELO(hw->tdata1, 1);
		hw->tdata1 = RV_DBTR_SET_MC_SIZEHI(hw->tdata1, 1);
		break;
#endif
	default:
		return -EINVAL;
	}

	hw->tdata1 = RV_DBTR_SET_MC_TYPE(hw->tdata1, RV_DBTR_TRIG_MCONTROL);

	CLEAR_DBTR_BIT(hw->tdata1, MC, DMODE);
	CLEAR_DBTR_BIT(hw->tdata1, MC, TIMING);
	CLEAR_DBTR_BIT(hw->tdata1, MC, SELECT);
	CLEAR_DBTR_BIT(hw->tdata1, MC, ACTION);
	CLEAR_DBTR_BIT(hw->tdata1, MC, CHAIN);
	CLEAR_DBTR_BIT(hw->tdata1, MC, MATCH);
	CLEAR_DBTR_BIT(hw->tdata1, MC, M);

	SET_DBTR_BIT(hw->tdata1, MC, S);
	SET_DBTR_BIT(hw->tdata1, MC, U);

	return 0;
}

static int rv_init_mcontrol6_trigger(const struct perf_event_attr *attr,
				     struct arch_hw_breakpoint *hw)
{
	switch (attr->bp_type) {
	case HW_BREAKPOINT_X:
		hw->type = RV_DBTR_BP;
		RV_DBTR_SET_MC6_EXEC_BIT(hw->tdata1);
		break;
	case HW_BREAKPOINT_R:
		hw->type = RV_DBTR_WP;
		RV_DBTR_SET_MC6_LOAD_BIT(hw->tdata1);
		break;
	case HW_BREAKPOINT_W:
		hw->type = RV_DBTR_WP;
		RV_DBTR_SET_MC6_STORE_BIT(hw->tdata1);
		break;
	case HW_BREAKPOINT_RW:
		hw->type = RV_DBTR_WP;
		RV_DBTR_SET_MC6_STORE_BIT(hw->tdata1);
		RV_DBTR_SET_MC6_LOAD_BIT(hw->tdata1);
		break;
	default:
		return -EINVAL;
	}

	switch (attr->bp_len) {
	case HW_BREAKPOINT_LEN_1:
		hw->len = 1;
		hw->tdata1 = RV_DBTR_SET_MC6_SIZE(hw->tdata1, 1);
		break;
	case HW_BREAKPOINT_LEN_2:
		hw->len = 2;
		hw->tdata1 = RV_DBTR_SET_MC6_SIZE(hw->tdata1, 2);
		break;
	case HW_BREAKPOINT_LEN_4:
		hw->len = 4;
		hw->tdata1 = RV_DBTR_SET_MC6_SIZE(hw->tdata1, 3);
		break;
	case HW_BREAKPOINT_LEN_8:
		hw->len = 8;
		hw->tdata1 = RV_DBTR_SET_MC6_SIZE(hw->tdata1, 5);
		break;
	default:
		return -EINVAL;
	}

	hw->tdata1 = RV_DBTR_SET_MC6_TYPE(hw->tdata1, RV_DBTR_TRIG_MCONTROL6);

	CLEAR_DBTR_BIT(hw->tdata1, MC6, DMODE);
	CLEAR_DBTR_BIT(hw->tdata1, MC6, TIMING);
	CLEAR_DBTR_BIT(hw->tdata1, MC6, SELECT);
	CLEAR_DBTR_BIT(hw->tdata1, MC6, ACTION);
	CLEAR_DBTR_BIT(hw->tdata1, MC6, CHAIN);
	CLEAR_DBTR_BIT(hw->tdata1, MC6, MATCH);
	CLEAR_DBTR_BIT(hw->tdata1, MC6, M);
	CLEAR_DBTR_BIT(hw->tdata1, MC6, VS);
	CLEAR_DBTR_BIT(hw->tdata1, MC6, VU);

	SET_DBTR_BIT(hw->tdata1, MC6, S);
	SET_DBTR_BIT(hw->tdata1, MC6, U);

	return 0;
}

int hw_breakpoint_arch_parse(struct perf_event *bp,
			     const struct perf_event_attr *attr,
			     struct arch_hw_breakpoint *hw)
{
	int ret;

	/* Breakpoint address */
	hw->address = attr->bp_addr;
	hw->tdata2 = attr->bp_addr;
	hw->tdata3 = 0x0;

	switch (dbtr_type) {
	case RV_DBTR_TRIG_MCONTROL:
		ret = rv_init_mcontrol_trigger(attr, hw);
		break;
	case RV_DBTR_TRIG_MCONTROL6:
		ret = rv_init_mcontrol6_trigger(attr, hw);
		break;
	default:
		pr_warn("unsupported trigger type\n");
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

/*
 * HW Breakpoint/watchpoint handler
 */
static int hw_breakpoint_handler(struct die_args *args)
{
	int ret = NOTIFY_DONE;
	struct arch_hw_breakpoint *bp;
	struct perf_event *event;
	int i;

	for (i = 0; i < dbtr_total_num; i++) {
		event = this_cpu_read(pcpu_hw_bp_events[i]);
		if (!event)
			continue;

		bp = counter_arch_bp(event);
		switch (bp->type) {
		/* Breakpoint */
		case RV_DBTR_BP:
			if (bp->address == args->regs->epc) {
				perf_bp_event(event, args->regs);
				ret = NOTIFY_STOP;
			}
			break;

		/* Watchpoint */
		case RV_DBTR_WP:
			if (bp->address == csr_read(CSR_STVAL)) {
				perf_bp_event(event, args->regs);
				ret = NOTIFY_STOP;
			}
			break;

		default:
			pr_warn("%s: Unknown type: %u\n", __func__, bp->type);
			break;
		}
	}

	return ret;
}

int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
				    unsigned long val, void *data)
{
	if (val != DIE_DEBUG)
		return NOTIFY_DONE;

	return hw_breakpoint_handler(data);
}

/* atomic: counter->ctx->lock is held */
int arch_install_hw_breakpoint(struct perf_event *event)
{
	struct arch_hw_breakpoint *bp = counter_arch_bp(event);
	union sbi_dbtr_shmem_entry *shmem = this_cpu_ptr(sbi_dbtr_shmem);
	struct sbi_dbtr_data_msg *xmit;
	struct sbi_dbtr_id_msg *recv;
	struct perf_event **slot;
	unsigned long idx;
	struct sbiret ret;
	int err = 0;

	raw_spin_lock_irqsave(this_cpu_ptr(&ecall_lock),
			      *this_cpu_ptr(&ecall_lock_flags));

	xmit = &shmem->data;
	recv = &shmem->id;
	xmit->tdata1 = cpu_to_le(bp->tdata1);
	xmit->tdata2 = cpu_to_le(bp->tdata2);
	xmit->tdata3 = cpu_to_le(bp->tdata3);

	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_TRIG_INSTALL,
			1, 0, 0, 0, 0, 0);

	if (ret.error) {
		pr_warn("%s: failed to install trigger\n", __func__);
		err = -EIO;
		goto done;
	}

	idx = le_to_cpu(recv->idx);
	if (idx >= dbtr_total_num) {
		pr_warn("%s: invalid trigger index %lu\n", __func__, idx);
		err = -EINVAL;
		goto done;
	}

	slot = this_cpu_ptr(&pcpu_hw_bp_events[idx]);
	if (*slot) {
		pr_warn("%s: slot %lu is in use\n", __func__, idx);
		err = -EBUSY;
		goto done;
	}

	pr_debug("Trigger %lu installed at index 0x%lx\n", bp->tdata2, idx);

	/* Save the event - to be looked up in handler */
	*slot = event;

done:
	raw_spin_unlock_irqrestore(this_cpu_ptr(&ecall_lock),
				   *this_cpu_ptr(&ecall_lock_flags));
	return err;
}

/* atomic: counter->ctx->lock is held */
void arch_uninstall_hw_breakpoint(struct perf_event *event)
{
	struct sbiret ret;
	int i;

	for (i = 0; i < dbtr_total_num; i++) {
		struct perf_event **slot = this_cpu_ptr(&pcpu_hw_bp_events[i]);

		if (*slot == event) {
			*slot = NULL;
			break;
		}
	}

	if (i == dbtr_total_num) {
		pr_warn("%s: Breakpoint not installed.\n", __func__);
		return;
	}

	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_TRIG_UNINSTALL,
			i, 1, 0, 0, 0, 0);
	if (ret.error)
		pr_warn("%s: Failed to uninstall trigger %d.\n", __func__, i);
}

void arch_enable_hw_breakpoint(struct perf_event *event)
{
	struct sbiret ret;
	int i;
	struct perf_event **slot;

	for (i = 0; i < dbtr_total_num; i++) {
		slot = this_cpu_ptr(&pcpu_hw_bp_events[i]);

		if (*slot == event)
			break;
	}

	if (i == dbtr_total_num) {
		pr_warn("%s: Breakpoint not installed.\n", __func__);
		return;
	}

	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_TRIG_ENABLE,
			i, 1, 0, 0, 0, 0);

	if (ret.error) {
		pr_warn("%s: Failed to install trigger %d\n", __func__, i);
		return;
	}
}
EXPORT_SYMBOL_GPL(arch_enable_hw_breakpoint);

void arch_update_hw_breakpoint(struct perf_event *event)
{
	struct arch_hw_breakpoint *bp = counter_arch_bp(event);
	union sbi_dbtr_shmem_entry *shmem = this_cpu_ptr(sbi_dbtr_shmem);
	struct sbi_dbtr_data_msg *xmit;
	struct perf_event **slot;
	struct sbiret ret;
	int i;

	for (i = 0; i < dbtr_total_num; i++) {
		slot = this_cpu_ptr(&pcpu_hw_bp_events[i]);

		if (*slot == event)
			break;
	}

	if (i == dbtr_total_num) {
		pr_warn("%s: Breakpoint not installed.\n", __func__);
		return;
	}

	raw_spin_lock_irqsave(this_cpu_ptr(&ecall_lock),
			      *this_cpu_ptr(&ecall_lock_flags));

	xmit = &shmem->data;
	xmit->tdata1 = cpu_to_le(bp->tdata1);
	xmit->tdata2 = cpu_to_le(bp->tdata2);
	xmit->tdata3 = cpu_to_le(bp->tdata3);

	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_TRIG_UPDATE,
			i, 1, 0, 0, 0, 0);
	if (ret.error)
		pr_warn("%s: Failed to update trigger %d.\n", __func__, i);

	raw_spin_unlock_irqrestore(this_cpu_ptr(&ecall_lock),
				   *this_cpu_ptr(&ecall_lock_flags));
}
EXPORT_SYMBOL_GPL(arch_update_hw_breakpoint);

void arch_disable_hw_breakpoint(struct perf_event *event)
{
	struct sbiret ret;
	int i;

	for (i = 0; i < dbtr_total_num; i++) {
		struct perf_event **slot = this_cpu_ptr(&pcpu_hw_bp_events[i]);

		if (*slot == event)
			break;
	}

	if (i == dbtr_total_num) {
		pr_warn("%s: Breakpoint not installed.\n", __func__);
		return;
	}

	ret = sbi_ecall(SBI_EXT_DBTR, SBI_EXT_DBTR_TRIG_DISABLE,
			i, 1, 0, 0, 0, 0);

	if (ret.error) {
		pr_warn("%s: Failed to uninstall trigger %d.\n", __func__, i);
		return;
	}
}
EXPORT_SYMBOL_GPL(arch_disable_hw_breakpoint);

void hw_breakpoint_pmu_read(struct perf_event *bp)
{
	/* TODO */
}

void clear_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	/* TODO */
}

void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	/* TODO */
}

static int __init arch_hw_breakpoint_init(void)
{
	unsigned int cpu;
	int rc = 0;

	for_each_possible_cpu(cpu)
		raw_spin_lock_init(&per_cpu(ecall_lock, cpu));

	if (!dbtr_init)
		init_sbi_dbtr();

	if (dbtr_total_num) {
		pr_info("%s: total number of type %d triggers: %u\n",
			__func__, dbtr_type, dbtr_total_num);
	} else {
		pr_info("%s: No hardware triggers available\n", __func__);
		goto out;
	}

	/* Allocate per-cpu shared memory */
	sbi_dbtr_shmem = __alloc_percpu(sizeof(*sbi_dbtr_shmem) * dbtr_total_num,
					PAGE_SIZE);

	if (!sbi_dbtr_shmem) {
		pr_warn("%s: Failed to allocate shared memory.\n", __func__);
		rc = -ENOMEM;
		goto out;
	}

	/* Hotplug handler to register/unregister shared memory with SBI */
	rc = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
			       "riscv/hw_breakpoint:prepare",
			       arch_smp_setup_sbi_shmem,
			       arch_smp_teardown_sbi_shmem);

	if (rc < 0) {
		pr_warn("%s: Failed to setup CPU hotplug state\n", __func__);
		free_percpu(sbi_dbtr_shmem);
		return rc;
	}
 out:
	return rc;
}
arch_initcall(arch_hw_breakpoint_init);
