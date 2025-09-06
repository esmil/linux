/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2025 Spacemit */

#ifndef _SPACEMIT_HMP_H_
#define _SPACEMIT_HMP_H_

enum hmp_thread_type_e {
	/* regular thread is the default type, should be 0 */
	HMP_REGULAR_THREAD = 0,
	HMP_AI_THREAD = 1,
};

extern struct cpumask  regular_cpu_mask __read_mostly;
extern struct cpumask  ai_cpu_mask __read_mostly;

void hmp_cpumask_init(void);
bool hmp_set_default_cpumask(struct task_struct *p);
bool hmp_cpu_can_offline(unsigned int cpu);
int hmp_cpu_affinity_restrict(struct task_struct *p, const struct cpumask *new_mask);

#endif /* _SPACEMIT_HMP_H_ */
