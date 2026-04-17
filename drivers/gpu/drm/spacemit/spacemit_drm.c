// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_of.h>
#include <linux/aperture.h>
#include <linux/component.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/kernel.h>
#include <drm/clients/drm_client_setup.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>

#include "spacemit_drm.h"
#include "spacemit_dmmu.h"
#include "spacemit_gem.h"
#include "dpu/dpu_saturn.h"

#define DRIVER_NAME	"spacemit"
#define DRIVER_DESC	"Spacemit SoCs' DRM Driver"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static void spacemit_drm_atomic_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;
#ifdef CONFIG_SPACEMIT_DEBUG
	struct spacemit_drm_private *priv = dev->dev_private;
	unsigned long flags;
#endif

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state,
					DRM_PLANE_COMMIT_ACTIVE_ONLY);
#ifdef CONFIG_SPACEMIT_DEBUG
	spin_lock_irqsave(&priv->ur_dump_lock, flags);
	priv->old_state = old_state;
	priv->underrun_debug = true;
	spin_unlock_irqrestore(&priv->ur_dump_lock, flags);
#endif

	drm_atomic_helper_wait_for_flip_done(dev, old_state);

	drm_atomic_helper_commit_hw_done(old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

static const struct drm_mode_config_helper_funcs spacemit_drm_mode_config_helper = {
	.atomic_commit_tail = spacemit_drm_atomic_commit_tail,
};

/* timgings except vstart, vend and vtotal */
static bool spacemit_drm_mode_match_timings_1(const struct drm_display_mode *mode1,
				   const struct drm_display_mode *mode2)
{
	return mode1->hdisplay == mode2->hdisplay &&
		mode1->hsync_start == mode2->hsync_start &&
		mode1->hsync_end == mode2->hsync_end &&
		mode1->htotal == mode2->htotal &&
		mode1->hskew == mode2->hskew &&
		mode1->vdisplay == mode2->vdisplay &&
		mode1->vscan == mode2->vscan;
}

static bool spacemit_drm_mode_match_timings_2(const struct drm_display_mode *mode1,
				   const struct drm_display_mode *mode2)
{
	return mode1->vsync_start == mode2->vsync_start &&
		mode1->vsync_end == mode2->vsync_end &&
		mode1->vtotal == mode2->vtotal;
}

/* check whether only timing vfp not equal, based on drm_mode_equal */
static bool spacemit_drm_mode_equal(const struct drm_display_mode *mode1,
		const struct drm_display_mode *mode2)
{
	int vfp1, vbp1, vfp2, vbp2;
	bool ret;

	ret = drm_mode_match(mode1, mode2,
			      DRM_MODE_MATCH_CLOCK |
			      DRM_MODE_MATCH_FLAGS |
			      DRM_MODE_MATCH_3D_FLAGS|
			      DRM_MODE_MATCH_ASPECT_RATIO);

	if (ret == false)
		return false;

	if (!spacemit_drm_mode_match_timings_1(mode1, mode2))
		return false;

	vfp1 = mode1->vsync_start - mode1->vdisplay;
	vbp1 = mode1->vtotal - mode1->vsync_end;
	vfp2 = mode2->vsync_start - mode2->vdisplay;
	vbp2 = mode2->vtotal - mode2->vsync_end;
	if (!spacemit_drm_mode_match_timings_2(mode1, mode2)) {
		if ((vbp1 == vbp2) && (vfp1 != vfp2))	/* Just vfp change for fps update */
			return true;
		else
			return false;
	}

	return true;
}

/* based on drm_atomic_helper_check */
static int spacemit_drm_atomic_helper_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	int ret;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	int i;

	/* original drm api */
	ret = drm_atomic_helper_check(dev, state);

	/* if new crtc mode only update vfp, no need to disable and enable crtc and connector */
	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		if (new_crtc_state->mode_changed) {
			if (spacemit_drm_mode_equal(&old_crtc_state->mode, &new_crtc_state->mode))
				new_crtc_state->mode_changed = false;
		}
	}

	return ret;
}

static const struct drm_mode_config_funcs spacemit_drm_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = spacemit_drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void spacemit_drm_mode_config_init(struct drm_device *drm)
{
	drm_mode_config_init(drm);

	/* HW has no limitation of min width and min height,
	 * even for YUV format, which is limitated in plane check
	 */
	drm->mode_config.min_width = 1;
	drm->mode_config.min_height = 1;
	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;

	drm->mode_config.funcs = &spacemit_drm_mode_config_funcs;
	drm->mode_config.helper_private = &spacemit_drm_mode_config_helper;
}

#ifdef CONFIG_DEBUG_FS

#define FRAMEBUFFER_DUMP_PATH "/tmp"
static int spacemit_framebuffer_dump(struct drm_plane *plane)
{
	unsigned int buffer_size = 0;
	int i, j;
	void *mmu_tbl_vaddr = NULL;
	phys_addr_t dpu_buffer_paddr = 0;
	void __iomem *dpu_buffer_vaddr = NULL;
	loff_t pos = 0;
	struct file *filep = NULL;
	struct drm_framebuffer *fb;
	char file_name[128];
	struct spacemit_plane_state *spacemit_pstate = to_spacemit_plane_state(plane->state);

	fb = plane->state->fb;
	if (!fb)
		return -EINVAL;

	for (i = 0; i < fb->format->num_planes; i++) {
		if (fb->obj[i]) {
			char format[5];

			format[0] = (fb->format->format & 0xFF);
			format[1] = ((fb->format->format >> 8) & 0xFF);
			format[2] = ((fb->format->format >> 16) & 0xFF);
			format[3] = ((fb->format->format >> 24) & 0xFF);
			format[4] = '\0';

			if (fb->format->is_yuv) {
				snprintf(file_name, 100, "%s/plane%d_fb%d_%s_planes%d_%dx%d.%s", FRAMEBUFFER_DUMP_PATH, plane->base.id, fb->base.id,
				format, i, fb->width, fb->height, "yuv");
			} else {
				snprintf(file_name, 100, "%s/plane%d_fb%d_%s_planes%d_%dx%d.%s", FRAMEBUFFER_DUMP_PATH, plane->base.id, fb->base.id,
				format, i, fb->width, fb->height, "rgb");
			}

			mmu_tbl_vaddr = spacemit_pstate->mmu_tbl.va;
			buffer_size = plane->state->fb->obj[i]->size >> PAGE_SHIFT;
			filep = filp_open(file_name, O_RDWR | O_APPEND | O_CREAT, 0644);

			if (IS_ERR(filep)) {
				DRM_ERROR("Open file %s failed!\n", file_name);
				return -EINVAL;
			}
			for (j = 0; j < buffer_size; j++) {
				dpu_buffer_paddr = *(volatile u32 __force *)mmu_tbl_vaddr;
				dpu_buffer_paddr = dpu_buffer_paddr << PAGE_SHIFT;
				dpu_buffer_vaddr = phys_to_virt((unsigned long)dpu_buffer_paddr);
				mmu_tbl_vaddr += 4;
				kernel_write(filep, (void *)dpu_buffer_vaddr, PAGE_SIZE, &pos);
			}
			filp_close(filep, NULL);
			filep = NULL;
			DRM_INFO("dump framebuffer: %s\n", file_name);
		}

	}

	return 0;
}

static int spacemit_drm_dump_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *drm_dev = minor->dev;
	struct drm_plane *plane;
	struct drm_framebuffer *fb;

	DRM_INFO("%s()\n", __func__);
	mutex_lock(&drm_dev->mode_config.fb_lock);
	drm_for_each_fb(fb, drm_dev) {
		DRM_INFO("framebuffer[%u]\n", fb->base.id);
		drm_for_each_plane(plane, drm_dev) {
			if (plane->state->fb != fb)
				continue;

			spacemit_framebuffer_dump(plane);
		}
	}
	mutex_unlock(&drm_dev->mode_config.fb_lock);

	return 0;
}

static const struct drm_info_list spacemit_debugfs_files[] = {
	{ "dump", spacemit_drm_dump_show, 0 },
};

static void spacemit_drm_debugfs_init(struct drm_minor *minor)
{
	DRM_DEBUG("%s()\n", __func__);
	drm_debugfs_create_files(spacemit_debugfs_files,
						ARRAY_SIZE(spacemit_debugfs_files),
						minor->debugfs_root,
						minor);
}
#endif

static const struct file_operations spacemit_drm_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release		= drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.compat_ioctl	= drm_compat_ioctl,
	.poll			= drm_poll,
	.read			= drm_read,
	// .llseek		= no_llseek,
	.mmap		= spacemit_gem_mmap,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

static struct drm_driver spacemit_drm_drv = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET |
					DRIVER_ATOMIC | DRIVER_HAVE_IRQ,
	.fops = &spacemit_drm_fops,

	.dumb_create = spacemit_gem_dumb_create,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.gem_prime_import_sg_table = spacemit_gem_prime_import_sg_table,
	DRM_FBDEV_DMA_DRIVER_OPS,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= spacemit_drm_debugfs_init,
#endif

	.name		= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.major		= DRIVER_MAJOR,
	.minor		= DRIVER_MINOR,
};

static int spacemit_drm_bind(struct device *dev)
{
	struct drm_device *drm;
	struct spacemit_drm_private *priv;
	int err;

	DRM_DEBUG("%s()\n", __func__);
	/* Remove existing drivers that may own the framebuffer memory. */
	err = aperture_remove_all_conflicting_devices(spacemit_drm_drv.name);
	if (err) {
		DRM_ERROR("Failed to remove existing framebuffers - %d.\n", err);
		return err;
	}

	drm = drm_dev_alloc(&spacemit_drm_drv, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	priv = dev_get_drvdata(dev);
	priv->ddev = drm;
	drm->dev_private = priv;
	priv->dev = dev;

	spacemit_drm_mode_config_init(drm);

	/* bind and init sub drivers */
	err = component_bind_all(drm->dev, drm);
	if (err) {
		DRM_ERROR("failed to bind all component.\n");
		goto err_dc_cleanup;
	}

	/* vblank init */
	err = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (err) {
		DRM_ERROR("failed to initialize vblank.\n");
		goto err_unbind_all;
	}

	/* reset all the states of crtc/plane/encoder/connector */
	drm_mode_config_reset(drm);

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(drm);

	/* force detection after connectors init */
	drm_helper_hpd_irq_event(drm);

	err = drm_dev_register(drm, 0);
	if (err < 0)
		goto err_kms_helper_poll_fini;
	drm_client_setup(drm, NULL);

	return 0;

err_kms_helper_poll_fini:
	drm_kms_helper_poll_fini(drm);
err_unbind_all:
	component_unbind_all(drm->dev, drm);
err_dc_cleanup:
	drm_mode_config_cleanup(drm);
	drm_dev_put(drm);
	return err;
}

static void spacemit_drm_put_dev(struct drm_device *dev)
{
	DRM_DEBUG("\n");

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}

	drm_dev_unregister(dev);
	drm_dev_put(dev);
}

static void spacemit_drm_unbind(struct device *dev)
{
	DRM_DEBUG("%s()\n", __func__);
	spacemit_drm_put_dev(dev_get_drvdata(dev));
}

static const struct component_master_ops drm_component_ops = {
	.bind = spacemit_drm_bind,
	.unbind = spacemit_drm_unbind,
};

static int compare_of(struct device *dev, void *data)
{
	struct device_node *np = data;

	DRM_DEBUG("compare %s\n", np->full_name);

	return dev->of_node == np;
}

static int spacemit_drm_of_component_probe(struct device *dev,
			   int (*compare_of)(struct device *, void *),
			   const struct component_master_ops *m_ops)
{
	struct device_node *crtc_np;
	struct device_node *ports, *port, *ep, *remote;
	struct component_match *match = NULL;
	int i;

	if (!dev->of_node)
		return -EINVAL;

	/*
	 * Bind the crtc's ports first, so that drm_of_find_possible_crtcs()
	 * called from encoder's .bind callbacks works as expected
	 */
	for (i = 0; ; i++) {
		crtc_np = of_parse_phandle(dev->of_node, "ports", i);
		if (!crtc_np)
			break;

		if (of_device_is_available(crtc_np))
			drm_of_component_match_add(dev, &match,
						   compare_of, crtc_np);

		of_node_put(crtc_np);
	}

	if (i == 0) {
		dev_err(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!match) {
		dev_err(dev, "no available port\n");
		return -ENODEV;
	}

	/*
	 * For bound crtcs, bind the encoders attached to their remote endpoint
	 */
	for (i = 0; ; i++) {
		crtc_np = of_parse_phandle(dev->of_node, "ports", i);
		if (!crtc_np)
			break;

		if (!of_device_is_available(crtc_np))
			goto next_crtc;

		ports = of_get_child_by_name(crtc_np, "ports");
		if (!ports)
			goto next_crtc;

		for_each_child_of_node(ports, port) {
			if (!of_node_name_eq(port, "port"))
				continue;
			for_each_child_of_node(port, ep) {
				if (!of_node_name_eq(ep, "endpoint"))
					continue;
				remote = of_graph_get_remote_port_parent(ep);
				if (!remote)
					continue;
				if (of_device_is_available(remote)) {
					drm_of_component_match_add(dev, &match, compare_of,
								remote);
				}
				of_node_put(remote);
			}
		}
		of_node_put(ports);

next_crtc:
		of_node_put(crtc_np);
	}

	return component_master_add_with_match(dev, m_ops, match);
}

static int spacemit_drm_probe(struct platform_device *pdev)
{
	int ret;
	struct spacemit_drm_private *priv;
	struct device_node *np = pdev->dev.of_node;
	struct resource *r;

	DRM_INFO("%s()\n", __func__);

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		DRM_ERROR("dma_set_mask_and_coherent failed (%d)\n", ret);

	/* Allocate and initialize the driver private structure. */
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, priv);

	if (of_property_read_u32(np, "hw_ver", &priv->hw_ver))
		return -EINVAL;

	priv->contig_mem = false;

	priv->hwdev = (struct spacemit_hw_device *)of_device_get_match_data(&pdev->dev);

	priv->cmdlist_groups = kzalloc(sizeof(struct cmdlist *) *
			(priv->hwdev->rdma_nums + priv->hwdev->crtc_nums), GFP_KERNEL);
	if (!priv->cmdlist_groups)
		return -ENOMEM;

	priv->wb_connector = kzalloc(sizeof(struct drm_writeback_connector *) *
			N_WRITEBACK_MAX, GFP_KERNEL);
	if (!priv->wb_connector)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->hwdev->phy_addr = r->start;
	priv->hwdev->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(priv->hwdev->base))
		return PTR_ERR(priv->hwdev->base);

	/* for cmd panel */
	r = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (r) {
		priv->hwdev->top_phy_addr = r->start;
		priv->hwdev->top_base = devm_ioremap_resource(&pdev->dev, r);
		if (IS_ERR(priv->hwdev->top_base))
			DRM_ERROR("get top_base fail!\n");
	}

#ifdef CONFIG_SPACEMIT_DEBUG
	spin_lock_init(&priv->ur_dump_lock);
#endif

	return spacemit_drm_of_component_probe(&pdev->dev, compare_of,  &drm_component_ops);
}

static void spacemit_drm_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &drm_component_ops);
}

static void spacemit_drm_shutdown(struct platform_device *pdev)
{
	struct spacemit_drm_private *priv = dev_get_drvdata(&pdev->dev);
	struct drm_device *drm = priv->ddev;

	if (!drm) {
		DRM_WARN("drm device is not available, no shutdown\n");
		return;
	}

	priv->hwdev->reboot_flag = 1;
	drm_atomic_helper_shutdown(drm);
}

static const struct of_device_id drm_match_table[] = {
	{
		.compatible = "spacemit,saturn-hee",
		.data = &spacemit_dp_devices[SATURN_HEE],
	},
	{
		.compatible = "spacemit,saturn-edp",
		.data = &spacemit_dp_devices[SATURN_EDP],
	},
	{},

};
MODULE_DEVICE_TABLE(of, drm_match_table);

static int __maybe_unused spacemit_drm_suspend(struct device *dev)
{
	struct drm_device *drm;
	struct spacemit_drm_private *priv;

	priv = dev_get_drvdata(dev);
	drm = priv->ddev;

	return drm_mode_config_helper_suspend(drm);
}

static int __maybe_unused spacemit_drm_resume(struct device *dev)
{
	struct drm_device *drm;
	struct spacemit_drm_private *priv;

	priv = dev_get_drvdata(dev);
	drm = priv->ddev;

	return drm_mode_config_helper_resume(drm);
}

static SIMPLE_DEV_PM_OPS(spacemit_drm_pm_ops, spacemit_drm_suspend,
			 spacemit_drm_resume);


static struct platform_driver spacemit_drm_driver = {
	.probe = spacemit_drm_probe,
	.remove = spacemit_drm_remove,
	.shutdown = spacemit_drm_shutdown,
	.driver = {
		.name = "spacemit-drm-drv",
		.of_match_table = drm_match_table,
		.pm = &spacemit_drm_pm_ops,
	},
};

static struct platform_driver * const spacemit_drm_drivers[] = {
	&spacemit_drm_driver,
	&spacemit_dpu_driver,
	&spacemit_dsi_driver,
	&spacemit_wb_driver,
	&spacemit_dphy_driver,
};

#ifdef MODULE
extern int dsi_core_register(void);
extern int dpu_core_register(void);
extern int dphy_core_register(void);
extern int display_class_init(void);
void drm_core_register(void)
{
	display_class_init();
	dsi_core_register();
	dpu_core_register();
	dphy_core_register();
}
#endif
static int __init spacemit_drm_drivers_init(void)
{
#ifdef MODULE
	drm_core_register();
#endif
	return platform_register_drivers(spacemit_drm_drivers,
					 ARRAY_SIZE(spacemit_drm_drivers));
}

static void __exit spacemit_drm_drivers_exit(void)
{
	platform_unregister_drivers(spacemit_drm_drivers,
				    ARRAY_SIZE(spacemit_drm_drivers));
}

module_init(spacemit_drm_drivers_init);
module_exit(spacemit_drm_drivers_exit);

MODULE_DESCRIPTION("Spacemit DRM KMS Master Driver");
MODULE_LICENSE("GPL v2");
