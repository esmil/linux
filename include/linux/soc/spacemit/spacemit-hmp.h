/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2025 Spacemit */

#ifndef _SPACEMIT_HMP_H_
#define _SPACEMIT_HMP_H_

typedef enum {
	/* regular thread is the default type, should be 0 */
	HMP_REGULAR = 0,
	HMP_AI = 1,
} hmp_type_e;

void hmp_cpumask_init(void);
bool hmp_set_default_cpumask(struct task_struct *p);
bool hmp_cpu_can_offline(unsigned int cpu);
int hmp_cpu_affinity_restrict(struct task_struct *p, const struct cpumask *new_mask);
int hmp_set_ai_thread(pid_t pid);
int hmp_get_cpumask(struct cpumask *mask, hmp_type_e type);

#endif /* _SPACEMIT_HMP_H_ */
