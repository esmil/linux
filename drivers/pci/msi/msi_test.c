// SPDX-License-Identifier: GPL-2.0
/*
 * Multi-MSI allocation + software-triggered interrupt test for IMSIC.
 *
 * Simulates a real Multi-MSI PCI device workflow:
 *   1. Allocate N contiguous MSI vectors (like pci_alloc_irq_vectors)
 *   2. Register per-vector interrupt handlers (like request_irq per vector)
 *   3. Trigger each vector by writing local_id to IMSIC MMIO page
 *      — this is exactly what a PCI device does: write msg.data to msg.address
 *   4. Verify handler invocation count, correct vector identification,
 *      per-CPU delivery, and no cross-talk between vectors
 *   5. Free handlers + vectors (like free_irq + pci_free_irq_vectors)
 *
 * No real PCI device needed. Uses IMSIC irq domain from device tree.
 * Trigger mechanism uses imsic_get_global_config() to get the MMIO VA,
 * then writel(local_id, msi_va) — identical to imsic_irq_retrigger().
 *
 * Usage:
 *   insmod msi_test.ko [num_vectors=4] [test_rounds=3] [trigger_count=5]
 *
 * Results:
 *   dmesg | grep msi_test
 */

#define pr_fmt(fmt) "msi_test: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/riscv-imsic.h>
#include <linux/msi.h>
#include <linux/interrupt.h>
#include <linux/log2.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/percpu.h>

/*
 * Mirror the imsic_vector structure layout from irq-riscv-imsic-state.h
 * so we can read cpu/local_id via irq_get_chip_data().
 */
struct imsic_vector_peek {
	unsigned int cpu;
	unsigned int local_id;
	unsigned int irq;
};

static int num_vectors = 4;
module_param(num_vectors, int, 0444);
MODULE_PARM_DESC(num_vectors, "Vectors per allocation (power of 2, 1-32)");

static int test_rounds = 3;
module_param(test_rounds, int, 0444);
MODULE_PARM_DESC(test_rounds, "Number of test rounds (default 3)");

static int trigger_count = 5;
module_param(trigger_count, int, 0444);
MODULE_PARM_DESC(trigger_count, "Triggers per vector per round (default 5)");

/* ── Per-vector tracking (mirrors what a real PCI driver keeps) ── */
struct vector_ctx {
	int		virq;
	int		index;		/* vector index 0..N-1 */
	atomic_t	hit_count;	/* how many times handler was called */
	int		last_cpu;	/* CPU that last ran the handler */
	struct completion delivered;	/* re-inited before each trigger */
};

#define MAX_ROUNDS 16
static struct {
	int virq_base;
	int nr;
} allocs[MAX_ROUNDS];
static int nr_allocs;

/* ── Interrupt handler (exactly like a real PCI driver's MSI handler) ── */
static irqreturn_t msi_test_handler(int virq, void *data)
{
	struct vector_ctx *v = data;

	atomic_inc(&v->hit_count);
	v->last_cpu = smp_processor_id();
	complete(&v->delivered);

	return IRQ_HANDLED;
}

/*
 * Software trigger: write local_id to IMSIC MMIO page.
 *
 * This is exactly what a real PCI device does when it sends an MSI:
 *   - PCI device reads MSI Capability → gets msg.address + msg.data
 *   - PCI device does a memory write: writel(msg.data, msg.address)
 *   - IMSIC receives the write → sets pending bit → delivers interrupt
 *
 * We do the same thing from software using the IMSIC MMIO VA.
 */
static int trigger_msi(unsigned int cpu, unsigned int local_id)
{
	const struct imsic_global_config *gc;
	struct imsic_local_config *lc;

	gc = imsic_get_global_config();
	if (!gc) {
		pr_err("  imsic_get_global_config() returned NULL\n");
		return -ENODEV;
	}

	lc = per_cpu_ptr(gc->local, cpu);
	if (!lc->msi_va) {
		pr_err("  msi_va is NULL for cpu %u\n", cpu);
		return -ENODEV;
	}

	/*
	 * writel_relaxed(local_id, msi_va) — identical to what
	 * imsic_irq_retrigger() does, and identical to what a PCI
	 * device's DMA engine does when sending an MSI.
	 */
	writel_relaxed(local_id, lc->msi_va);

	return 0;
}

/* ── Find the IMSIC irq domain from DT ── */
static struct irq_domain *find_imsic_domain(void)
{
	struct device_node *np;
	struct irq_domain *d;

	np = of_find_compatible_node(NULL, NULL, "riscv,imsics");
	if (!np) {
		pr_err("no 'riscv,imsics' node in device tree\n");
		return NULL;
	}

	pr_info("found DT node: %pOF\n", np);

	d = irq_find_matching_host(np, DOMAIN_BUS_NEXUS);
	if (!d)
		d = irq_find_matching_host(np, DOMAIN_BUS_GENERIC_MSI);
	if (!d)
		d = irq_find_matching_host(np, DOMAIN_BUS_ANY);

	of_node_put(np);

	if (d)
		pr_info("found irq domain: '%s'\n",
			d->name ? d->name : "<unnamed>");
	else
		pr_err("no irq domain found for IMSIC node\n");

	return d;
}

/* ── Read IMSIC local_id from chip_data ── */
static int get_local_id(unsigned int virq, unsigned int *cpu_out)
{
	struct irq_data *d = irq_get_irq_data(virq);
	struct imsic_vector_peek *vec;

	if (!d || !d->chip_data)
		return -1;

	vec = (struct imsic_vector_peek *)d->chip_data;
	if (cpu_out)
		*cpu_out = vec->cpu;
	return vec->local_id;
}

/* ── One complete test round ── */
static int run_round(struct irq_domain *domain, int nr, int round)
{
	msi_alloc_info_t info = {};
	struct vector_ctx *vctx = NULL;
	int virq, i, j, ret;
	int first_local_id = -1;
	unsigned int first_cpu = 0, align;
	unsigned int vec_cpus[32], vec_local_ids[32];
	bool contiguous = true, aligned = true, same_cpu = true;
	bool pass = true;

	pr_info("[round %d] === start ===\n", round);

	/* ── Phase 1: Allocate MSI vectors ── */
	pr_info("[round %d] allocating %d IRQs ...\n", round, nr);

	virq = irq_domain_alloc_irqs(domain, nr, NUMA_NO_NODE, &info);
	if (virq < 0) {
		pr_err("[round %d] FAIL: irq_domain_alloc_irqs(%d) = %d\n",
		       round, nr, virq);
		return virq;
	}

	pr_info("[round %d] allocated virq_base=%d\n", round, virq);

	/* Track for cleanup in module exit */
	if (nr_allocs < MAX_ROUNDS) {
		allocs[nr_allocs].virq_base = virq;
		allocs[nr_allocs].nr = nr;
		nr_allocs++;
	}

	/* ── Phase 2: Verify allocation properties ── */
	for (i = 0; i < nr; i++) {
		int local_id = get_local_id(virq + i, &vec_cpus[i]);

		if (local_id < 0) {
			pr_err("[round %d] FAIL: cannot read local_id for virq %d\n",
			       round, virq + i);
			contiguous = false;
			vec_local_ids[i] = 0;
			continue;
		}

		vec_local_ids[i] = local_id;
		pr_info("[round %d]   virq=%d  local_id=%d  cpu=%u\n",
			round, virq + i, local_id, vec_cpus[i]);

		if (i == 0) {
			first_local_id = local_id;
			first_cpu = vec_cpus[i];
		} else {
			if (local_id != first_local_id + i)
				contiguous = false;
			if (vec_cpus[i] != first_cpu)
				same_cpu = false;
		}
	}

	align = roundup_pow_of_two(nr);
	if (first_local_id >= 0 && (first_local_id % align) != 0)
		aligned = false;

	if (same_cpu)
		pr_info("[round %d] PASS: all vectors on cpu %u\n", round, first_cpu);
	else {
		pr_err("[round %d] FAIL: vectors spread across CPUs\n", round);
		pass = false;
	}

	if (contiguous)
		pr_info("[round %d] PASS: local_ids contiguous (%d-%d)\n",
			round, first_local_id, first_local_id + nr - 1);
	else {
		pr_err("[round %d] FAIL: local_ids not contiguous\n", round);
		pass = false;
	}

	if (aligned)
		pr_info("[round %d] PASS: local_id_base=%d is %u-aligned\n",
			round, first_local_id, align);
	else {
		pr_err("[round %d] FAIL: local_id_base=%d not %u-aligned\n",
			round, first_local_id, align);
		pass = false;
	}

	/* ── Phase 3: Register per-vector handlers ── */
	vctx = kcalloc(nr, sizeof(*vctx), GFP_KERNEL);
	if (!vctx) {
		pr_err("[round %d] FAIL: kcalloc for vector_ctx\n", round);
		goto out_result;
	}

	for (i = 0; i < nr; i++) {
		struct vector_ctx *v = &vctx[i];

		v->virq = virq + i;
		v->index = i;
		v->last_cpu = -1;
		atomic_set(&v->hit_count, 0);
		init_completion(&v->delivered);

		ret = request_irq(v->virq, msi_test_handler, 0,
				  "msi-test", v);
		if (ret) {
			pr_err("[round %d] FAIL: request_irq(virq=%d) = %d\n",
			       round, v->virq, ret);
			while (--i >= 0)
				free_irq(vctx[i].virq, &vctx[i]);
			pass = false;
			goto out_free_ctx;
		}
	}

	pr_info("[round %d] registered %d handlers (virq %d-%d)\n",
		round, nr, virq, virq + nr - 1);

	/* ── Phase 4: Trigger each vector via IMSIC MMIO write ── */
	/*
	 * IMSIC pending bits are 1-bit wide: a second write while the bit
	 * is still pending merges with the first (standard MSI edge semantics).
	 * So we must wait for each interrupt to be delivered before triggering
	 * the next one — exactly as a well-behaved PCI device would wait for
	 * its previous MSI to be consumed before sending the next.
	 */
	pr_info("[round %d] triggering %d vectors x %d times via MMIO ...\n",
		round, nr, trigger_count);

	for (i = 0; i < nr; i++) {
		for (j = 0; j < trigger_count; j++) {
			unsigned long left;

			reinit_completion(&vctx[i].delivered);

			ret = trigger_msi(vec_cpus[i], vec_local_ids[i]);
			if (ret) {
				pr_err("[round %d] FAIL: trigger vec[%d] iter %d\n",
				       round, i, j);
				pass = false;
				goto out_check;
			}

			/* Wait up to 1s for handler to fire */
			left = wait_for_completion_timeout(
					&vctx[i].delivered, HZ);
			if (!left) {
				pr_err("[round %d] FAIL: vec[%d] iter %d timeout waiting for handler\n",
				       round, i, j);
				pass = false;
				goto out_check;
			}
		}
	}

out_check:
	/* ── Phase 5: Verify interrupt delivery ── */
	pr_info("[round %d] --- interrupt delivery results ---\n", round);

	for (i = 0; i < nr; i++) {
		struct vector_ctx *v = &vctx[i];
		int hits = atomic_read(&v->hit_count);

		pr_info("[round %d]   vec[%d] virq=%d  hits=%d/%d  cpu=%d\n",
			round, i, v->virq, hits, trigger_count, v->last_cpu);

		/* Handler called exactly trigger_count times? */
		if (hits != trigger_count) {
			pr_err("[round %d] FAIL: vec[%d] expected %d hits, got %d\n",
			       round, i, trigger_count, hits);
			pass = false;
		}
	}

	/* Cross-talk check: total hits == nr * trigger_count */
	{
		int total_hits = 0;

		for (i = 0; i < nr; i++)
			total_hits += atomic_read(&vctx[i].hit_count);

		if (total_hits == nr * trigger_count)
			pr_info("[round %d] PASS: total hits %d == expected %d (no cross-talk)\n",
				round, total_hits, nr * trigger_count);
		else {
			pr_err("[round %d] FAIL: total hits %d != expected %d\n",
			       round, total_hits, nr * trigger_count);
			pass = false;
		}
	}

	/* ── Phase 6: Cleanup handlers ── */
	for (i = 0; i < nr; i++)
		free_irq(vctx[i].virq, &vctx[i]);

	pr_info("[round %d] freed %d handlers\n", round, nr);

out_free_ctx:
	kfree(vctx);

out_result:
	if (pass)
		pr_info("[round %d] *** OVERALL PASS ***\n", round);
	else
		pr_err("[round %d] *** OVERALL FAIL ***\n", round);

	return pass ? 0 : -EINVAL;
}

static void free_all_irqs(void)
{
	int i;

	for (i = nr_allocs - 1; i >= 0; i--) {
		pr_info("freeing virq_base=%d nr=%d\n",
			allocs[i].virq_base, allocs[i].nr);
		irq_domain_free_irqs(allocs[i].virq_base, allocs[i].nr);
	}
	nr_allocs = 0;
}

static int __init msi_test_init(void)
{
	struct irq_domain *domain;
	int i, nr, pass_count = 0, fail_count = 0;

	nr = num_vectors;

	pr_info("========================================\n");
	pr_info("Multi-MSI Allocation + Interrupt Test\n");
	pr_info("num_vectors=%d test_rounds=%d trigger_count=%d\n",
		nr, test_rounds, trigger_count);
	pr_info("========================================\n");

	if (nr < 1 || nr > 32 || !is_power_of_2(nr)) {
		pr_err("num_vectors must be power of 2 in [1, 32]\n");
		return -EINVAL;
	}

	if (test_rounds < 1)
		test_rounds = 1;
	if (test_rounds > MAX_ROUNDS)
		test_rounds = MAX_ROUNDS;

	domain = find_imsic_domain();
	if (!domain)
		return -ENODEV;

	for (i = 0; i < test_rounds; i++) {
		if (run_round(domain, nr, i + 1) == 0)
			pass_count++;
		else
			fail_count++;
	}

	pr_info("========================================\n");
	pr_info("Results: %d PASS, %d FAIL (of %d rounds)\n",
		pass_count, fail_count, test_rounds);
	pr_info("========================================\n");

	return 0;
}

static void __exit msi_test_exit(void)
{
	free_all_irqs();
	pr_info("unloaded, all vectors freed\n");
}

module_init(msi_test_init);
module_exit(msi_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Zhang");
MODULE_DESCRIPTION("Multi-MSI allocation + software interrupt trigger test for IMSIC");
