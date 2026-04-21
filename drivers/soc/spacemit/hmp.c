// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Spacemit */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/rcupdate.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/soc/spacemit/spacemit-hmp.h>


static atomic_t system_suspending = ATOMIC_INIT(0);

static struct cpumask	regular_cpu_mask __read_mostly;
static struct cpumask	ai_cpu_mask __read_mostly;

int hmp_get_cpumask(struct cpumask *mask, hmp_type_e type)
{
	int	ret = 0;

	if (!mask) {
		return -EINVAL;
	}

	switch (type) {
	case HMP_REGULAR:
		cpumask_copy(mask, &regular_cpu_mask);
		break;

	case HMP_AI:
		cpumask_copy(mask, &ai_cpu_mask);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int hmp_map_ai_to_regular(struct cpumask *mask)
{
	unsigned int ai_first, reg_first;

	ai_first = cpumask_first(&ai_cpu_mask);
	reg_first = cpumask_first(&regular_cpu_mask);

	if (ai_first > reg_first) {
		cpumask_shift_right(mask, mask, ai_first - reg_first);
	} else {
		cpumask_shift_left(mask, mask, reg_first - ai_first);
	}

	cpumask_and(mask, mask, &regular_cpu_mask);

	return cpumask_weight(mask);
}

int hmp_cpu_affinity_restrict(struct task_struct *p, const struct cpumask *new_mask)
{
	const struct cpumask *allowed_mask;

	if (atomic_read(&system_suspending)) {
		/* should not bind thread when systemm suspending */
		return -EINVAL;
	}

	if (cpumask_empty(new_mask))
		return -EINVAL;

	allowed_mask = (p->thread_type == HMP_REGULAR) ?
					&regular_cpu_mask : &ai_cpu_mask;

	if (!cpumask_subset(new_mask, allowed_mask))
		return -EINVAL;

	return 0;
}

/**
 * hmp_cpu_can_offline - Check the restriction when try offline a cpu core.
 * @cpu: cpu id which will be offline
 *
 * This function check the cpu offline restriction:
 *  1. If some ai thread is exist, the last cpu of ai_cpu_mask offline should
 *     be rejected;
 *  2. The last cpu of regular_cpu_mask offline should be rejected, because there
 *     should be some regulater tasks running.
 *  3. If the offline request is under system suspend processing, user tasks
 *     should be frozen already, so, any offline request is allowed.
 *
 * Returns: true if the offline request is allowed, otherwise, return false.
 */
bool hmp_cpu_can_offline(unsigned int cpu)
{
	/* reuse existing logic from cpu_hotplug_callback for CPU_DOWN_PREPARE */
	cpumask_t online_ai_cpus, online_regular_cpu_mask;
	struct task_struct *g, *p;

	if (atomic_read(&system_suspending))
		return true;

	cpumask_and(&online_regular_cpu_mask, &regular_cpu_mask, cpu_online_mask);
	cpumask_and(&online_ai_cpus, &ai_cpu_mask, cpu_online_mask);

	if (cpumask_test_cpu(cpu, &regular_cpu_mask)) {
		if (cpumask_weight(&online_regular_cpu_mask) <= 1) {
			pr_err("Cannot offline the last Regular cpu\n");
			return false;
		}
	} else {
		if (cpumask_weight(&online_ai_cpus) <= 1) {
			for_each_process_thread(g, p) {
				if (p->thread_type == HMP_AI) {
					pr_err("Cannot offline the last AI cpu\n");
					return false;
				}
			}
		}
	}

	return true;
}

/**
 * hmp_cpumask_init - Try to init hybrid multi-processor cpumask
 *
 * This function try to get the cpumask information from dts, if the
 * 'cpu-ai' property is set as 'true', it will be set to the ai_cpu_mask,
 * otherwise, it will be set to the regular_cpu_mask.
 */
void hmp_cpumask_init(void)
{
	struct device_node *node;
	const char *cpu_ai;
	unsigned long hartid;
	int rc;

	cpumask_clear(&ai_cpu_mask);
	cpumask_clear(&regular_cpu_mask);

	for_each_of_cpu_node(node) {
		rc = riscv_of_processor_hartid(node, &hartid);
		if (rc < 0) {
			pr_warn("Failed to get hartid for CPU node\n");
			continue;
		}

		if (of_property_read_string(node, "cpu-ai", &cpu_ai)) {
			cpumask_set_cpu(hartid, &regular_cpu_mask);
			continue;
		}

		if (!strcmp(cpu_ai, "true"))
			cpumask_set_cpu(hartid, &ai_cpu_mask);
	}
}

/**
 * is_per_cpu_kthread - Check if a kernel thread is a critical per-CPU thread
 * @p: task_struct pointer
 *
 * This function checks if a kernel thread is a critical per-CPU thread that
 * must be bound to its specific CPU core (including AI cores).
 * Only essential per-CPU threads are allowed to bind to AI cores.
 *
 * Returns: true if the thread is a critical per-CPU thread, false otherwise
 */
static bool is_per_cpu_kthread(struct task_struct *p)
{
	if (!(p->flags & PF_KTHREAD))
		return false;

	/* Check if it's a per-CPU thread with bound to single CPU*/
	if (p->nr_cpus_allowed == 1)
		return true;

	return false;
}

/**
 * hmp_set_default_cpumask - Set the default cpumask
 * @p: task_struct pointer
 *
 * This function set the default cpumask which allowed running the thread.
 * If the thread is not a per-cpu thread:
 * Set p->cpus_mask to ai_cpu_mask if the thread is an AI thread,
 * otherwise, set p->cpus_mask to regular_cpu_mask
 *
 * Returns: true if set cpumask set to ai_cpumask
 */

 static inline void _set_cpumask(struct task_struct *p, const struct cpumask *allowed_mask)
{
	struct cpumask mask;

	cpumask_and(&mask, allowed_mask, &p->cpus_mask);
	if (WARN_ON(cpumask_empty(&mask)))
		return;

	cpumask_copy(&p->cpus_mask, &mask);
	p->nr_cpus_allowed = cpumask_weight(&p->cpus_mask);
	p->cpus_ptr = &p->cpus_mask;
}

bool hmp_set_default_cpumask(struct task_struct *p)
{
	/* check if called from set ai task */
	if (p->thread_type == HMP_AI) {
		_set_cpumask(p, &ai_cpu_mask);
		return !cpumask_empty(&p->cpus_mask);
	}

	/* Check if this is a per-CPU kernel thread that should keep its binding */
	if ((p->flags & PF_KTHREAD) && is_per_cpu_kthread(p))
		return true;

	/* All new threads default to regular cores unless it's a per-CPU kthread
	 * AI thread property will be set explicitly via /proc/ai_threads
	 */
	p->thread_type = HMP_REGULAR;
	_set_cpumask(p, &regular_cpu_mask);
	return !cpumask_empty(&p->cpus_mask);
}

static int pm_callback(struct notifier_block *nb, unsigned long action, void *data)
{
	switch (action) {
	case PM_SUSPEND_PREPARE:
		/* setup system suspending flag */
		atomic_set(&system_suspending, 1);
		return NOTIFY_OK;

	case PM_POST_SUSPEND:
		/* clear system suspending flag */
		atomic_set(&system_suspending, 0);
		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block pm_notifier = {
	.notifier_call = pm_callback,
	.priority = INT_MAX,
};

int hmp_set_ai_thread(pid_t pid)
{
	unsigned int cpu;
	int ret = 0;
	cpumask_t online_ai_cpus;
	struct task_struct *t = NULL;

	/* enable one ai cpu core if none ai core enabled */
	cpumask_and(&online_ai_cpus, &ai_cpu_mask, cpu_online_mask);
	if (!cpumask_weight(&online_ai_cpus)) {
		cpu = cpumask_any(&ai_cpu_mask);
		ret = cpu_device_up(get_cpu_device(cpu));
		if (ret) {
			/* try to online an ai cpu failed */
			pr_err("None ai cpu is activated\n");
			return ret;
		}
	}

	/* lookup target task and take a ref */
	rcu_read_lock();
	if (pid == 0)
		t = current;
	else
		t = pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!t) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(t);
	rcu_read_unlock();

	/* mark the thread type as an ai thread under task_lock */
	task_lock(t);
	t->thread_type = HMP_AI;
	task_unlock(t);

	ret = set_cpus_allowed_ptr(t, &ai_cpu_mask);
	if (ret) {
		task_lock(t);
		t->thread_type = HMP_REGULAR;
		task_unlock(t);
	} else if (t->thread.vstate.datap) {
		/*
		 * the vector context should not be initiated before switch
		 * to AI cores, if it has been initiated already, just
		 * reset the vector state!!
		 */
		pr_warn("%s: pid:%u(%s), vector has been enabled already!!!\n",
			__func__, t->pid, t->comm);
	}

	put_task_struct(t);
	return ret;
}
EXPORT_SYMBOL_GPL(hmp_set_ai_thread);

static ssize_t proc_set_thread_type(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	pid_t pid;
	int ret = 0;

	if (kstrtoint_from_user(buf, count, 10, &pid))
		return -EINVAL;

	if (pid < 0)
        return -EINVAL;

	ret = hmp_set_ai_thread(pid);
	return ret == 0 ? count : ret;
}

static const struct proc_ops thread_type_proc_ops = {
	.proc_write = proc_set_thread_type,
};


static int __init hmp_service_init(void)
{
	int ret;

	/* register the platform suspend call-back for migrating
	 * some thread from one type to the boot-core type.
	 */
	atomic_set(&system_suspending, 0);
	ret = register_pm_notifier(&pm_notifier);
	if (ret) {
		pr_err("Failed to register PM notifier: %d\n", ret);
		return ret;
	}

	/* create proc fs node for user set thread type */
	if (!proc_create("set_ai_thread", 0222, NULL, &thread_type_proc_ops)) {
		pr_err("Failed to create proc interface\n");
		unregister_pm_notifier(&pm_notifier);
		return -ENOMEM;
	}

	return 0;
}
late_initcall(hmp_service_init);
