// SPDX-License-Identifier: GPL-2.0

#include <linux/limits.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/kthread.h>
#include <linux/clk-provider.h>
#include <linux/mailbox_client.h>
#include <linux/completion.h>
#include <linux/freezer.h>
#include <linux/firmware.h>
#include <linux/elf.h>
#include <uapi/linux/sched/types.h>
#include <uapi/linux/sched.h>
#include <linux/sched/prio.h>
#include <linux/rpmsg.h>
#include <linux/pm_qos.h>
#include <linux/delay.h>
#include <linux/syscore_ops.h>
#include <linux/fs.h>
#include <linux/pm_domain.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-direction.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include "remoteproc_internal.h"
#include "remoteproc_elf_helpers.h"

#define MAX_MEM_BASE	2
#define MAX_MBOX	2

#define K3_MBOX_VQ0_ID	0
#define K3_MBOX_VQ1_ID	1

struct spacemit_mbox {
	const char *name;
	struct mbox_chan *chan;
	struct mbox_client client;
	struct task_struct *mb_thread;
	bool kthread_running;
	struct completion mb_comp;
	int vq_id;
};

struct spacemit_rproc {
	struct device *dev;
	struct spacemit_mbox mb[MAX_MBOX];
	unsigned int size;
};

static int spacemit_rproc_mem_alloc(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	void __iomem *va = NULL;

	dev_dbg(&rproc->dev, "map memory: %pa+%zx\n", &mem->dma, mem->len);
	va = ioremap(mem->dma, mem->len);
	if (!va) {
		dev_err(&rproc->dev, "Unable to map memory region: %pa+%zx\n",
			&mem->dma, mem->len);
		return -ENOMEM;
	}

	/* Update memory entry va */
	mem->va = va;

	return 0;
}

static int spacemit_rproc_mem_release(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	dev_dbg(&rproc->dev, "unmap memory: %pa\n", &mem->dma);

	iounmap(mem->va);

	return 0;
}

static int spacemit_rproc_prepare(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct of_phandle_iterator it;
	struct rproc_mem_entry *mem;
	struct reserved_mem *rmem;
	int index = 0;

	/* Register associated reserved memory regions */
	of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	while (of_phandle_iterator_next(&it) == 0) {
		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			dev_err(&rproc->dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}

		if (rmem->base > U64_MAX) {
			dev_err(&rproc->dev, "the rmem base is overflow\n");
			return -EINVAL;
		}

		mem = rproc_mem_entry_init(dev, NULL,
					   rmem->base,
					   rmem->size, rmem->base,
					   spacemit_rproc_mem_alloc,
					   spacemit_rproc_mem_release,
					   it.node->name);
		if (!mem)
			return -ENOMEM;

		rproc_add_carveout(rproc, mem);
		index++;
	}

	return 0;
}

static int spacemit_rproc_start(struct rproc *rproc)
{
	/* Do nothing: has been latched from spl */
	return 0;
}

static int spacemit_rproc_stop(struct rproc *rproc)
{
	/* TODO */

	return 0;
}

static void spacemit_rproc_kick(struct rproc *rproc, int vqid)
{
	struct spacemit_rproc *ddata = rproc->priv;
	unsigned int i;
	int err;

	if (WARN_ON(vqid >= MAX_MBOX))
		return;

	for (i = 0; i < MAX_MBOX; i++) {
		if (vqid != ddata->mb[i].vq_id)
			continue;
		if (!ddata->mb[i].chan)
			return;
		err = mbox_send_message(ddata->mb[i].chan, "kick");
		if (err < 0)
			dev_err(&rproc->dev, "%s: failed (%s, err:%d)\n",
				__func__, ddata->mb[i].name, err);
		return;
	}
}

static int spacemit_rproc_attach(struct rproc *rproc)
{
	return 0;
}

static int spacemit_rproc_detach(struct rproc *rproc)
{
	return 0;
}

static struct resource_table* spacemit_get_loaded_rsc_table(
				struct rproc *rproc, size_t *size)
{
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct of_phandle_iterator it;
	struct reserved_mem *rmem;

	/* Register associated reserved memory regions */
	of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	while (of_phandle_iterator_next(&it) == 0) {
		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			dev_err(&rproc->dev, "unable to acquire memory-region\n");
			return NULL;
		}

		if (rmem->base > U64_MAX) {
			dev_err(&rproc->dev, "the rmem base is overflow\n");
			return NULL;
		}

		if (!strcmp(it.node->name, "rcpu0_rsc_table")) {
			*size = rmem->size;
			return (struct resource_table *)ioremap(rmem->base, rmem->size);
		} else if (!strcmp(it.node->name, "rcpu1_rsc_table")) {
			*size = rmem->size;
			return (struct resource_table *)ioremap(rmem->base, rmem->size);
		}
	}

	return NULL;
}

static struct rproc_ops spacemit_rproc_ops = {
	.prepare	= spacemit_rproc_prepare,
	.start		= spacemit_rproc_start,
	.stop		= spacemit_rproc_stop,
	.attach		= spacemit_rproc_attach,
	.detach		= spacemit_rproc_detach,
	.kick		= spacemit_rproc_kick,
	.get_loaded_rsc_table	= spacemit_get_loaded_rsc_table,
};

static int __process_theread(void *arg)
{
	int ret;
	struct mbox_client *cl = arg;
	struct rproc *rproc = dev_get_drvdata(cl->dev);
	struct spacemit_mbox *mb = container_of(cl, struct spacemit_mbox, client);
	struct sched_param param = {.sched_priority = 0 };

	mb->kthread_running = true;
	ret = sched_setscheduler(current, SCHED_FIFO, &param);
	set_freezable();

	do {
		try_to_freeze();
		wait_for_completion_timeout(&mb->mb_comp, 10);
		if (rproc_vq_interrupt(rproc, mb->vq_id) == IRQ_NONE)
			dev_dbg(&rproc->dev, "no message found in vq%d\n", mb->vq_id);
	} while (!kthread_should_stop());

	mb->kthread_running = false;

	return 0;
}
static void k3_rproc_mb_callback(struct mbox_client *cl, void *data)
{
	struct spacemit_mbox *mb = container_of(cl, struct spacemit_mbox, client);

	complete(&mb->mb_comp);
}

static int spacemit_rproc_probe(struct platform_device *pdev)
{
	int ret, i;
	const char *name;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const char *fw_name;
	struct spacemit_rproc *priv;
	struct mbox_client *cl;
	struct rproc *rproc;

	ret = rproc_of_parse_firmware(dev, 0, &fw_name);
	if (ret < 0 && ret != -EINVAL)
		return ret;

	rproc = devm_rproc_alloc(dev, np->name, &spacemit_rproc_ops,
				 fw_name, sizeof(*priv));
	if (!rproc)
		return -ENOMEM;

	priv = rproc->priv;
	priv->dev = dev;

	platform_set_drvdata(pdev, rproc);

	/* tx */
	priv->mb[0].name = "vq0";
	priv->mb[0].vq_id = K3_MBOX_VQ0_ID;
	priv->mb[0].client.rx_callback = k3_rproc_mb_callback,
	priv->mb[0].client.tx_block = true,

	/* rx */
	priv->mb[1].name = "vq1";
	priv->mb[1].vq_id = K3_MBOX_VQ1_ID;
	priv->mb[1].client.rx_callback = k3_rproc_mb_callback,
	priv->mb[1].client.tx_block = true;

	for (i = 0; i < MAX_MBOX; ++i) {
		name = priv->mb[i].name;

		cl = &priv->mb[i].client;
		cl->dev = dev;
		init_completion(&priv->mb[i].mb_comp);

		priv->mb[i].chan = mbox_request_channel_byname(cl, name);
		if (IS_ERR(priv->mb[i].chan)) {
			dev_err(dev, "failed to request mbox channel\n");
			ret = -EINVAL;
			goto err_0;
		}

		if (priv->mb[i].vq_id >= 0) {
			priv->mb[i].mb_thread = kthread_run(__process_theread, (void *)cl, name);
			if (IS_ERR(priv->mb[i].mb_thread)) {
				ret = PTR_ERR(priv->mb[i].mb_thread);
				goto err_0;
			}
		}
	}

	rproc->auto_boot = true;
	rproc->state = RPROC_DETACHED; 
	ret = devm_rproc_add(dev, rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed\n");
		ret = -EINVAL;
		goto err_0;
	}

	return 0;

err_0:
	while (--i >= 0) {
		if (priv->mb[i].chan)
			mbox_free_channel(priv->mb[i].chan);
		if (priv->mb[i].mb_thread)
			kthread_stop(priv->mb[i].mb_thread);
	}

	rproc_free(rproc);

	return ret;
}

static void k3_rproc_free_mbox(struct rproc *rproc)
{
	struct spacemit_rproc *ddata = rproc->priv;
	unsigned int i;

	for (i = 0; i < MAX_MBOX; i++) {
		if (ddata->mb[i].chan)
			mbox_free_channel(ddata->mb[i].chan);
		ddata->mb[i].chan = NULL;
	}
}

static void spacemit_rproc_remove(struct platform_device *pdev)
{
	int i = 0;
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct spacemit_rproc *ddata = rproc->priv;

	for (i = 0; i < MAX_MBOX; ++i)
		if (ddata->mb[i].kthread_running)
			kthread_stop(ddata->mb[i].mb_thread);

	rproc_del(rproc);
	k3_rproc_free_mbox(rproc);
	rproc_free(rproc);
}

static const struct of_device_id spacemit_rproc_of_match[] = {
	{ .compatible = "spacemit,k3-rproc" },
	{},
};

MODULE_DEVICE_TABLE(of, spacemit_rproc_of_match);

static void spacemit_rproc_shutdown(struct platform_device *pdev)
{
	int i;
	struct rproc *rproc;
	struct spacemit_rproc *priv;

	rproc = dev_get_drvdata(&pdev->dev);
	priv = rproc->priv;

	for (i = 0; i < MAX_MBOX; ++i) {
		/* release the resource of rt thread */
		if (priv->mb[i].kthread_running) {
			if (!frozen((priv->mb[i].mb_thread)))
				kthread_stop(priv->mb[i].mb_thread);
		}
		/* mbox_free_channel(priv->mb[i].chan); */
	}
}

static struct platform_driver spacemit_rproc_driver = {
	.probe = spacemit_rproc_probe,
	.remove = spacemit_rproc_remove,
	.shutdown = spacemit_rproc_shutdown,
	.driver = {
		.name = "spacemit-rproc",
		.of_match_table = spacemit_rproc_of_match,
	},
};

static __init int spacemit_rproc_driver_init(void)
{
	return platform_driver_register(&spacemit_rproc_driver);
}
device_initcall(spacemit_rproc_driver_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("sapcemit remote processor control driver");
