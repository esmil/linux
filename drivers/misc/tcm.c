#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define TCM_NAME		"tcm"

#define IOC_MAGIC		'c'
#define TCM_INFO_GET		_IOR(IOC_MAGIC, 7, int)
#define TCM_BLOCK_INFO_GET	_IOWR(IOC_MAGIC, 9, int)

typedef struct {
	void *base;
	size_t block_size;
	size_t block_num;
} tcm_info_t;

typedef struct {
	u32 block_id;
	u32 reserved;
	u64 phys;
	u64 size;
	u64 cpu_affinity_mask;
} tcm_block_info_t;

struct tcm_block {
	phys_addr_t phys;
	size_t size;
	u64 cpu_affinity_mask;
};

struct tcm_direct_dev {
	struct miscdevice miscdev;
	struct device *dev;
	phys_addr_t phys;
	size_t size;
	size_t block_size;
	size_t block_num;
	struct tcm_block *blocks;
};

static int tcm_parse_cpu_mask(struct device_node *tcm_node, u64 *mask)
{
	struct device_node *cpu_node;
	u64 cpu_mask = 0;
	int i;

	if (!mask)
		return -EINVAL;

	for (i = 0;; i++) {
		u32 cpu_reg;

		cpu_node = of_parse_phandle(tcm_node, "cpus", i);
		if (!cpu_node)
			break;

		if (!of_property_read_u32(cpu_node, "reg", &cpu_reg) &&
		    cpu_reg < 64)
			cpu_mask |= BIT_ULL(cpu_reg);

		of_node_put(cpu_node);
	}

	*mask = cpu_mask;
	return i == 0 ? -ENOENT : 0;
}

static int tcm_parse_layout(struct platform_device *pdev,
			    struct tcm_direct_dev *tcm)
{
	struct resource *parent_res;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	size_t child_count;
	size_t block_idx = 0;
	int ret;

	parent_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!parent_res)
		return -EINVAL;

	tcm->phys = parent_res->start;
	tcm->size = resource_size(parent_res);
	tcm->block_size = tcm->size;
	tcm->block_num = 1;
	tcm->blocks = NULL;

	child_count = np ? of_get_available_child_count(np) : 0;
	if (!child_count) {
		tcm->blocks = devm_kcalloc(&pdev->dev, 1,
					   sizeof(*tcm->blocks),
					   GFP_KERNEL);
		if (!tcm->blocks)
			return -ENOMEM;

		tcm->blocks[0].phys = tcm->phys;
		tcm->blocks[0].size = tcm->size;
		return 0;
	}

	tcm->blocks = devm_kcalloc(&pdev->dev, child_count,
				   sizeof(*tcm->blocks), GFP_KERNEL);
	if (!tcm->blocks)
		return -ENOMEM;

	for_each_available_child_of_node(np, child) {
		struct resource child_res;
		size_t child_size;
		u64 cpu_affinity_mask = 0;

		ret = of_address_to_resource(child, 0, &child_res);
		if (ret < 0)
			return ret;

		if (child_res.start < parent_res->start ||
		    child_res.end > parent_res->end)
			return -EINVAL;

		child_size = resource_size(&child_res);
		if (!block_idx) {
			tcm->phys = child_res.start;
			tcm->block_size = child_size;
		} else if (child_size != tcm->block_size) {
			dev_err(&pdev->dev,
				"direct mmap layout requires equal-sized blocks, block%zu size=0x%zx expected=0x%zx\n",
				block_idx, child_size, tcm->block_size);
			return -EINVAL;
		}

		tcm->blocks[block_idx].phys = child_res.start;
		tcm->blocks[block_idx].size = child_size;
		if (tcm_parse_cpu_mask(child, &cpu_affinity_mask) == 0)
			tcm->blocks[block_idx].cpu_affinity_mask =
				cpu_affinity_mask;
		block_idx++;
	}

	tcm->block_num = block_idx;
	tcm->size = tcm->block_size * tcm->block_num;
	return 0;
}

static int tcm_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct tcm_direct_dev *tcm;

	tcm = container_of(miscdev, struct tcm_direct_dev, miscdev);
	file->private_data = tcm;
	return 0;
}

static int tcm_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct tcm_direct_dev *tcm = file->private_data;
	unsigned long vma_size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long mapped = 0;

	if (!tcm)
		return -ENODEV;

	if (offset >= tcm->size)
		return -EINVAL;
	if (vma_size > tcm->size - offset)
		return -EINVAL;

	if (!tcm->block_size || !tcm->blocks)
		return -ENODEV;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	vm_flags_set(vma, VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
#else
	vma->vm_flags |= VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;
#endif

	while (mapped < vma_size) {
		unsigned long cur = offset + mapped;
		size_t block_idx = cur / tcm->block_size;
		unsigned long block_off = cur % tcm->block_size;
		unsigned long chunk;
		unsigned long long phys;

		if (block_idx >= tcm->block_num)
			return -EINVAL;
		if (block_off >= tcm->blocks[block_idx].size)
			return -EINVAL;

		chunk = tcm->blocks[block_idx].size - block_off;
		if (chunk > vma_size - mapped)
			chunk = vma_size - mapped;

		phys = tcm->blocks[block_idx].phys + block_off;
		if (remap_pfn_range(vma, vma->vm_start + mapped,
				    phys >> PAGE_SHIFT, chunk,
				    vma->vm_page_prot))
			return -EAGAIN;

		mapped += chunk;
	}

	return 0;
}

static long tcm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct tcm_direct_dev *tcm = file->private_data;

	if (!tcm)
		return -ENODEV;

	if (cmd == TCM_INFO_GET) {
		tcm_info_t info;

		info.base = (void *)(uintptr_t)tcm->phys;
		info.block_size = tcm->block_size;
		info.block_num = tcm->block_num;

		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;

		return 0;
	}

	if (cmd == TCM_BLOCK_INFO_GET) {
		tcm_block_info_t block_info;

		if (copy_from_user(&block_info, (void __user *)arg,
				   sizeof(block_info)))
			return -EFAULT;

		if (block_info.block_id >= tcm->block_num)
			return -EINVAL;

		block_info.phys = tcm->blocks[block_info.block_id].phys;
		block_info.size = tcm->blocks[block_info.block_id].size;
		block_info.cpu_affinity_mask =
			tcm->blocks[block_info.block_id].cpu_affinity_mask;

		if (copy_to_user((void __user *)arg, &block_info,
				 sizeof(block_info)))
			return -EFAULT;

		return 0;
	}

	return -ENOTTY;
}

static const struct file_operations tcm_fops = {
	.owner = THIS_MODULE,
	.open = tcm_open,
	.mmap = tcm_mmap,
	.unlocked_ioctl = tcm_ioctl,
};

static const struct of_device_id tcm_dt_ids[] = {
	{ .compatible = "spacemit,k1-pro-tcm" },
	{ .compatible = "spacemit,k1-x-tcm" },
	{ .compatible = "spacemit,k1-tcm" },
	{}
};
MODULE_DEVICE_TABLE(of, tcm_dt_ids);

static int tcm_probe(struct platform_device *pdev)
{
	struct tcm_direct_dev *tcm;
	int ret;

	tcm = devm_kzalloc(&pdev->dev, sizeof(*tcm), GFP_KERNEL);
	if (!tcm)
		return -ENOMEM;

	tcm->dev = &pdev->dev;
	ret = tcm_parse_layout(pdev, tcm);
	if (ret < 0)
		return ret;

	tcm->miscdev.minor = MISC_DYNAMIC_MINOR;
	tcm->miscdev.name = TCM_NAME;
	tcm->miscdev.fops = &tcm_fops;
	tcm->miscdev.mode = 0666;

	ret = misc_register(&tcm->miscdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, tcm);

	dev_info(&pdev->dev,
		 "direct mmap phys 0x%llx size 0x%zx block_size 0x%zx block_num %zu via /dev/%s\n",
		 (unsigned long long)tcm->phys, tcm->size,
		 tcm->block_size, tcm->block_num, TCM_NAME);

	return 0;
}

static void tcm_remove(struct platform_device *pdev)
{
	struct tcm_direct_dev *tcm = platform_get_drvdata(pdev);

	if (!tcm)
		return;

	misc_deregister(&tcm->miscdev);
}

static struct platform_driver tcm_driver = {
	.driver = {
		.name = TCM_NAME,
		.of_match_table = tcm_dt_ids,
	},
	.probe = tcm_probe,
	.remove = tcm_remove,
};

module_platform_driver(tcm_driver);
