// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit hdma service driver support
 *
 * Copyright (c) 2025 SPACEMIT, Co. Ltd.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>
#include <linux/pid.h>
#include "ai_dma.h"

#define DEVICE_NAME	"ai_dma"
#define REQLIST_NAME	"aidma_list"
#define DMAMSI_NAME	"dma_msi"
#define IOC_MAGIC	"c"
#define MAP_SIZE	4096
#define GET_MSI_HWIRQ	0

spinlock_t aidma_lock;
static struct list_head dma_req_list;
unsigned long msi_addr;
unsigned int msi_hwirq;

typedef struct {
	size_t			size;
	unsigned long		user_addr;
	void			*kern_addr;
	dma_addr_t		dma_addr;
	struct list_head	list;
} dma_map_info_t;

typedef struct {
	dma_addr_t		addr;
	size_t			size;
} va2pa_t;

static int va2pa(void *va, size_t size, va2pa_t **va2pa, pid_t pid)
{
	pmd_t *pmd;
	pte_t *pte;
	struct task_struct *task;
	struct mm_struct *mm;
	unsigned long pg_offset;
	unsigned long pg_address;
	int flag = 0;
	va2pa_t *p;

	unsigned long vaddr = (unsigned long)va;

	task = get_pid_task(find_get_pid(pid), PIDTYPE_PID);
	if (!task) {
		return -ESRCH;
	}
	mm = get_task_mm(task);
	if (!mm) {
		return -EINVAL;
	}

	p = kmalloc(sizeof(va2pa_t), GFP_ATOMIC);
	if (!p)
		printk("failed to alloc for pages\n");
	*va2pa = p;

	memset(p, 0x00, sizeof(va2pa_t));
	pmd = pmd_off(mm, vaddr);
	if (pmd_none(*pmd)) {
		printk("not in the pmd!\n");
		flag = -1;
	}

	pte = pte_offset_map(pmd, vaddr);
	if (pte_none(*pte)) {
		printk("not in the pte!\n");
		flag = -1;
	}

	pg_offset = offset_in_page(vaddr);
	pg_address = pte_pfn(__pte(pte_val(*pte))) << PAGE_SHIFT;
	p->addr = pg_address | pg_offset;
	p->size = size;
	pte_unmap(pte);

	return flag;
}

static int dma_malloc(struct ai_dmac *dma, dma_map_info_t *dma_info, struct vm_area_struct *vma)
{
	dma_info->kern_addr = kmalloc(dma_info->size, GFP_KERNEL);
	if (!dma_info->kern_addr) {
		dev_err(dma->dev,"kmalloc failed\n");
		return -ENOMEM;
	}

	dma_info->dma_addr = dma_map_single(dma->dev, dma_info->kern_addr, dma_info->size, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dma->dev, dma_info->dma_addr)) {
		dev_err(dma->dev,"mapping buffer failed\n");
		return -1;
	}

	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	if (remap_pfn_range(vma, vma->vm_start, (virt_to_phys(dma_info->kern_addr) >> PAGE_SHIFT),
			    vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		dma_unmap_single(dma->dev, dma_info->dma_addr, dma_info->size, DMA_BIDIRECTIONAL);
		kfree(dma_info->kern_addr);
		return -EAGAIN;
	}

	return 0;
}

static void dma_free(struct ai_dmac *dma, dma_map_info_t *dma_info)
{
	dma_unmap_single(dma->dev, dma_info->dma_addr, dma_info->size, DMA_BIDIRECTIONAL);
	kfree(dma_info->kern_addr);
}

static int dma_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int dma_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static void dma_vma_close(struct vm_area_struct *vma)
{
	dma_map_info_t *dma_info;
	struct ai_dmac *dma = aidma_info[0].dma;;

	dma_info = vma->vm_private_data;
	dma_free(dma, dma_info);
}

static const struct vm_operations_struct dma_vm_ops = {
	.close = dma_vma_close,
};

static int dma_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	dma_map_info_t *dma_info;
	struct ai_dmac *dma;

	if (aidma_info[0].dma == NULL)
		return -1;
	else
		dma = aidma_info[0].dma;

	dma_info = kmalloc(sizeof(*dma_info), GFP_KERNEL);
	if (dma_info == NULL) {
		printk("Unable to allocate VMA data structure.\n");
		return -ENOMEM;
	}
	dma_info->size = vma->vm_end - vma->vm_start;
	dma_info->user_addr = vma->vm_start;
	ret = dma_malloc(dma, dma_info, vma);
	if (ret < 0)
		return -1;

	vma->vm_ops = &dma_vm_ops;
	vma->vm_private_data = dma_info;
	vm_flags_set(vma, VM_DONTCOPY);

	return 0;
}

static int dma_list_open(struct inode *inode, struct file *filp)
{
	struct ai_dmac *dma;
	struct req_addr_info *info;
	struct req_node *node;
	unsigned long flags;

	if (aidma_info[0].dma == NULL)
		return -1;
	else
		dma = aidma_info[0].dma;

	info = kmalloc(sizeof(struct req_addr_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	node = kmalloc(sizeof(struct req_node), GFP_KERNEL);
	if (!node) {
		kfree(info);
		return -ENOMEM;
	}

	info->req_list = dma_alloc_coherent(dma->dev, AIDMA_MAX_REQ * sizeof(struct aidma_req),
					    &info->req_addr, GFP_KERNEL);
	if (!info->req_list) {
		kfree(node);
		kfree(info);
		return -ENOMEM;
	}

	node->info = info;
	INIT_LIST_HEAD(&node->list);

	spin_lock_irqsave(&aidma_lock, flags);
	list_add_tail(&node->list, &dma_req_list);
	spin_unlock_irqrestore(&aidma_lock, flags);

	filp->private_data = info;

	return 0;
}

static int dma_list_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ai_dmac *dma;
	struct req_addr_info *info = file->private_data;

	if (aidma_info[0].dma == NULL)
		return -1;
	else
		dma = aidma_info[0].dma;

	dma_mmap_coherent(dma->dev, vma, info->req_list, info->req_addr, AIDMA_MAX_REQ * sizeof(struct aidma_req));
	return 0;
}

static int dma_list_release(struct inode *inode, struct file *filp)
{
	struct req_addr_info *info = filp->private_data;
	struct req_node *node, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&aidma_lock, flags);
	list_for_each_entry_safe(node, tmp, &dma_req_list, list) {
		if (node->info->req_addr == info->req_addr) {
			list_del(&node->list);
			kfree(node);
			break;
		}
	}
	spin_unlock_irqrestore(&aidma_lock, flags);

	dma_free_coherent(aidma_info[0].dma->dev, AIDMA_MAX_REQ * sizeof(struct aidma_req),
				info->req_list, info->req_addr);
	kfree(info);
	return 0;
}

static const struct file_operations ai_dma_fops = {
	.owner		= THIS_MODULE,
	.open		= dma_open,
	.release	= dma_release,
	.mmap		= dma_mmap,
};

static const struct file_operations req_fops = {
	.owner		= THIS_MODULE,
	.open		= dma_list_open,
	.release	= dma_list_release,
	.mmap		= dma_list_mmap,
};

void start_transfer() {
	struct aidma_req *req;
	struct dma_transfer_param *param;
	struct ai_pack_param *params;
	struct ai_dmac *dma;
	struct req_node *node;
	int i, j, ret;
	size_t size = 0;
	dma_addr_t src_addr, dst_addr;
	va2pa_t *s_pa_l, *d_pa_l;
	unsigned long flags;

	spin_lock_irqsave(&aidma_lock, flags);
	list_for_each_entry(node, &dma_req_list, list) {
		req = node->info->req_list;
		for (i = 0; i < AIDMA_MAX_REQ; i++) {
			if (req[i].status == DMA_REQ_SUBMIT) {
				for (j = 0; j < AXI_DMAC_NUM; j++) {
					if (aidma_info[j].work != true) {
						aidma_info[j].work = true;
						aidma_info[j].work_id = i;
						aidma_info[j].req = req;
						dma = aidma_info[j].dma;
						req[i].status = DMA_REQ_PROCESS;
						break;
					}
				}
				if (dma == NULL) {
					spin_unlock_irqrestore(&aidma_lock, flags);
					return;
				}
				param = &req[i].params;
				if (param->is_sgdg == true) {
					size = param->ai_param.m_size * param->ai_param.k_size;
				} else {
					size = param->size;
				}

				ret = va2pa(param->src, size, &s_pa_l, param->pid);
				if (ret != 0) {
					pr_info("src va2pa error, addr:%llx, ret = %d\n",(unsigned long long)param->src,ret);
					if (s_pa_l)
						kfree(s_pa_l);
					spin_unlock_irqrestore(&aidma_lock, flags);
					return;
				}

				ret = va2pa(param->dst, size, &d_pa_l, param->pid);
				if (ret != 0) {
					pr_info("dst va2pa error, addr:%llx, ret = %d\n", (unsigned long long)param->dst,ret);
					if (d_pa_l)
						kfree(d_pa_l);
					kfree(s_pa_l);
					spin_unlock_irqrestore(&aidma_lock, flags);
					return;
				}
				src_addr = s_pa_l->addr;
				dst_addr = d_pa_l->addr;
				pr_debug("src_addr:%llx dst_addr:%llx\n",src_addr,dst_addr);

				if (param->is_sgdg == true) {
					params = kmalloc(sizeof(struct ai_pack_param), GFP_ATOMIC);
					if (!params) {
						kfree(s_pa_l);
						kfree(d_pa_l);
						spin_unlock_irqrestore(&aidma_lock, flags);
						return;
					}
					params->sgdg = true;
					params->pack = param->ai_param.pack;
					params->pad_value = param->ai_param.pad_value;
					params->transpose = param->ai_param.transpose;
					params->ele_size = param->ai_param.ele_size;
					params->m_size = param->ai_param.m_size;
					params->k_size = param->ai_param.k_size;
					params->mr_size = param->ai_param.mr_size;
					params->kr_size = param->ai_param.kr_size;
					ai_dmac_pack_start(dma, params, dst_addr, src_addr);
				} else {
					ai_dmac_memcpy_by_2d(dma, dst_addr, src_addr, param->size);
				}
			}
		}
	}
	spin_unlock_irqrestore(&aidma_lock, flags);
	return;
}

static void dma_msi_write(struct msi_desc *desc, struct msi_msg *msg)
{
	unsigned int msi_addr_lo = msg->address_lo;
	unsigned int msi_addr_hi = msg->address_hi;
	msi_addr = ((unsigned long)msi_addr_hi << 32) + msi_addr_lo;
	msi_hwirq = msg->data;
}

static int dma_msi_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int dma_msi_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int dma_msi_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long pfn = msi_addr >> PAGE_SHIFT;
	unsigned long vsize = vma->vm_end - vma->vm_start;
	unsigned long psize = MAP_SIZE - offset;
	int ret;

	if (vsize > psize)
		return -EINVAL;

	ret = remap_pfn_range(vma, vma->vm_start, pfn, vsize, vma->vm_page_prot);
	if (ret)
		return -EAGAIN;

	pr_debug("Successfully mapped physical 0x%lx to user virtual 0x%lx\n",
			msi_addr, vma->vm_start);
	return 0;
}

static long dma_msi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case GET_MSI_HWIRQ:
		if (copy_to_user((unsigned int __user *)arg, &msi_hwirq, sizeof(msi_hwirq)))
			return -EFAULT;
		return 0;
	default:
		return -ENOTTY;
	};
}

static const struct file_operations dma_msi_fops = {
	.owner		= THIS_MODULE,
	.open		= dma_msi_open,
	.release	= dma_msi_release,
	.mmap		= dma_msi_mmap,
	.unlocked_ioctl	= dma_msi_ioctl,
};

static irqreturn_t ai_dma_start_transfer(int irq, void *devid)
{
	start_transfer();
	return IRQ_HANDLED;
}

static char *ai_dma_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return NULL;
}

static int ai_dmadev_probe(struct platform_device *pdev) {
	struct device *dev;
	static unsigned char dma_major, dma_req, msi_major;
	static struct class *dma_class, *req_class, *msi_class;
	int msi_irq, ret = 0;

	dev = &pdev->dev;
	if (!dev_get_msi_domain(dev)) {
		if (is_of_node(dev->fwnode))
			of_msi_configure(dev, to_of_node(dev->fwnode));
	}
	ret = platform_device_msi_init_and_alloc_irqs(dev, 1, dma_msi_write);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret, "Failed to allocate 1 MSI\n");
	}
	msi_irq = msi_get_virq(&pdev->dev, 0);
	ret = request_irq(msi_irq, ai_dma_start_transfer, IRQF_SHARED, dev_name(&pdev->dev), &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to request msi irq\n");
		goto err_free_msi;
	}

	msi_major = register_chrdev(0, DMAMSI_NAME, &dma_msi_fops);
	if (msi_major < 0) {
		ret = msi_major;
		goto err_free_irq;
	}

	msi_class = class_create(DMAMSI_NAME);
	if (IS_ERR(msi_class)) {
		ret = PTR_ERR(msi_class);
		goto err_unregister_msi_chrdev;
	}

	msi_class->devnode = ai_dma_devnode;

	dev = device_create(msi_class, NULL, MKDEV(msi_major, 0), NULL, DMAMSI_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_destroy_msi_class;
	}

	dma_major = register_chrdev(0 , DEVICE_NAME, &ai_dma_fops);
	if (dma_major < 0) {
		ret = dma_major;
		goto err_destroy_msi_device;
	}

	dma_class = class_create(DEVICE_NAME);
	if (IS_ERR(dma_class)) {
		ret = PTR_ERR(dma_class);
		goto err_unregister_dma_chrdev;
	}

	dma_class->devnode = ai_dma_devnode;

	dev = device_create(dma_class, NULL, MKDEV(dma_major, 0), NULL, DEVICE_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_destroy_dma_class;
	}

	dma_req = register_chrdev(0, REQLIST_NAME, &req_fops);
	if (dma_req < 0) {
		ret = dma_req;
		goto err_destroy_dma_device;
	}

	req_class = class_create(REQLIST_NAME);
	if (IS_ERR(req_class)) {
		ret = PTR_ERR(req_class);
		goto err_unregister_req_chrdev;
	}

	req_class->devnode = ai_dma_devnode;

	dev = device_create(req_class, NULL, MKDEV(dma_req, 0), NULL, REQLIST_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_destroy_req_class;
	}

	INIT_LIST_HEAD(&dma_req_list);
	spin_lock_init(&aidma_lock);

	return 0;

err_destroy_req_class:
	class_destroy(req_class);
err_unregister_req_chrdev:
	unregister_chrdev(dma_req, REQLIST_NAME);
err_destroy_dma_device:
	device_destroy(dma_class, MKDEV(dma_major, 0));
err_destroy_dma_class:
	class_destroy(dma_class);
err_unregister_dma_chrdev:
	unregister_chrdev(dma_major, DEVICE_NAME);
err_destroy_msi_device:
	device_destroy(msi_class, MKDEV(msi_major, 0));
err_destroy_msi_class:
	class_destroy(msi_class);
err_unregister_msi_chrdev:
	unregister_chrdev(msi_major, DMAMSI_NAME);
err_free_irq:
	free_irq(msi_irq, &pdev->dev);
err_free_msi:
	platform_device_msi_free_irqs_all(dev);

	return ret;
}

static void ai_dmadev_remove(struct platform_device *pdev)
{
	platform_device_msi_free_irqs_all(&pdev->dev);
}

static const struct of_device_id ai_dmadev_of_match[] = {
	{ .compatible = "ai-dma-dev", },
	{},
};
MODULE_DEVICE_TABLE(of, ai_dmadev_of_match);

static struct platform_driver ai_dma_driver = {
	.driver		= {
		.name	= "ai_dma_dev",
		.of_match_table = ai_dmadev_of_match,
	},
	.probe		= ai_dmadev_probe,
	.remove		= ai_dmadev_remove,
};
module_platform_driver(ai_dma_driver);
