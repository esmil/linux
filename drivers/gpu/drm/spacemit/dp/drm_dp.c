// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/component.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <drm/drm_of.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_state_helper.h>

#include "inno_conn.h"
#include "inno_dp_api.h"

#define INVALID_GPIO			0xFFFFFFFF

#define INNO_DP_HPD_IRQ_EVENT		BIT(31)
#define INNO_DP_HPD_PLUG_EVENT		BIT(29)
#define INNO_DP_HPD_UNPLUG_EVENT	BIT(28)
#define INNO_DP_HPD_STATUS		BIT(26)

struct dp_dev {
	struct device *dev;

	struct drm_device *drm;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct proc_dir_entry *proc_irq;
	enum drm_connector_status connector_status;
	struct drm_display_mode mode;

	struct reset_control *reset;
	struct clk *pxclk;

	u32 gpio_bl;
	u32 gpio_power;
	u32 gpio_enable;

	struct inno_conn_t *conn;
};

static enum drm_connector_status dp_conn_detect(struct drm_connector *connector, bool force)
{
	struct dp_dev *dp_dev = container_of(connector, struct dp_dev, connector);

	if (inno_hpd_detect(dp_dev->conn))
		dp_dev->connector_status = connector_status_connected;
	else
		dp_dev->connector_status = connector_status_disconnected;

	return dp_dev->connector_status;
}

static int
dp_conn_probe_single_connector_modes(struct drm_connector *connector,
				       uint32_t maxX, uint32_t maxY)
{
	return drm_helper_probe_single_connector_modes(connector, 2560, 1600);
}

static const struct drm_connector_funcs dp_connector_funcs = {
	.fill_modes = dp_conn_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.detect = dp_conn_detect,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int dp_conn_get_edid_block(void *data, uint8_t *buf, unsigned int block, size_t len)
{
	struct dp_dev *dp_dev = data;
	switch (block) {
	case 0:
		memcpy(buf, &dp_dev->conn->edid_data[0], EDID_LENGTH);
		break;
	case 1:
		memcpy(buf, &dp_dev->conn->edid_data[128], EDID_LENGTH);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dp_conn_get_modes(struct drm_connector *connector)
{
	int count;
	const struct drm_edid *edid;
	struct drm_display_mode *mode, *tmp;
	struct drm_device *dev = connector->dev;
	struct dp_dev *dp_dev = container_of(connector, struct dp_dev, connector);

	inno_get_edid(dp_dev->conn);

	if (dp_dev->conn->edid_valid) {
		edid = drm_edid_read_custom(connector, dp_conn_get_edid_block, dp_dev);
		drm_edid_connector_update(connector, edid);
		count = drm_edid_connector_add_modes(connector);
		list_for_each_entry_safe(mode, tmp, &connector->probed_modes, head) {
			if (mode->hdisplay == 2560) {
				if (drm_mode_vrefresh(mode) > 90) {
					list_del(&mode->head);
					drm_mode_destroy(dev, mode);
					count--;
				}
			}
		}
		drm_edid_free(edid);
		return count;
	} else {
		return drm_add_modes_noedid(connector, 1920, 1080);
	}
}

static enum drm_mode_status dp_conn_mode_valid(struct drm_connector *connector,
					       const struct drm_display_mode *mode)
{
	return MODE_OK;
}

static const struct drm_connector_helper_funcs dp_conn_helper_funcs = {
	.get_modes = dp_conn_get_modes,
	.mode_valid = dp_conn_mode_valid,
};

static const struct drm_encoder_funcs dp_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void dp_encoder_enable(struct drm_encoder *encoder)
{
	struct dp_dev *dp_dev = container_of(encoder, struct dp_dev, encoder);
	uint64_t clk_val;
	uint64_t set_clk_val;
	struct drm_display_mode *mode = &dp_dev->mode;

	DRM_INFO("%s()\n", __func__);

	if (dp_dev->pxclk) {
		// clk_prepare_enable(dp_dev->pxclk);

		set_clk_val = mode->clock * 1000;
		DRM_INFO("pxclk set_clk_val %lld\n", set_clk_val);

		if (set_clk_val) {
			set_clk_val = clk_round_rate(dp_dev->pxclk, set_clk_val);
			clk_val = clk_get_rate(dp_dev->pxclk);
			if(clk_val != set_clk_val){
				clk_set_rate(dp_dev->pxclk, set_clk_val);
				DRM_INFO("set pxclk=%lld\n", set_clk_val);
			}
		}

		clk_val = clk_get_rate(dp_dev->pxclk);
		DRM_INFO("get pxclk=%lld\n", clk_val);
	}

	dp_dev->conn->is_enable = 0;
	inno_do_display(dp_dev->conn, mode);
}

static void dp_encoder_disable(struct drm_encoder *encoder)
{
	DRM_INFO("%s()\n", __func__);
}

static int dp_encoder_atomic_check(struct drm_encoder *encoder,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	DRM_DEBUG("%s()\n", __func__);
	return 0;
}

static void dp_mode_set(struct drm_encoder *encoder,
			 struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode)
{
	struct dp_dev *dp_dev = container_of(encoder, struct dp_dev, encoder);

	DRM_INFO("%s()\n", __func__);

	drm_mode_copy(&dp_dev->mode, adjusted_mode);
}

static const struct drm_encoder_helper_funcs dp_encoder_helper_funcs = {
	.enable = dp_encoder_enable,
	.disable = dp_encoder_disable,
	.atomic_check = dp_encoder_atomic_check,
	.mode_set = dp_mode_set,
};

static ssize_t dp_irq_proc_write(struct file *filp, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct dp_dev *dp_dev = pde_data(file_inode(filp));
	char write_status[2] = { 0 };

	if (copy_from_user(write_status, buf, 1))
		write_status[0] = '1';

	if (write_status[0] == '1')
		dp_dev->connector_status = connector_status_connected;
	else if (write_status[0] == '0')
		dp_dev->connector_status = connector_status_disconnected;
	else
		return -EINVAL;

	drm_kms_helper_hotplug_event(dp_dev->drm);
	return count;
}

static const struct proc_ops dp_irq_proc_ops = {
	.proc_flags = PROC_ENTRY_PERMANENT,
	.proc_write = dp_irq_proc_write,
};

static void dp_proc_irq_debug_init(struct dp_dev *dp_dev)
{
	dp_dev->proc_irq = proc_create_data(dp_dev->connector.name,
					    S_IWUSR, NULL,
					    &dp_irq_proc_ops, dp_dev);
}

static void dp_proc_irq_debug_exit(struct dp_dev *dp_dev)
{
	if (dp_dev->proc_irq)
		proc_remove(dp_dev->proc_irq);
	dp_dev->proc_irq = NULL;
}

static irqreturn_t soc_dp_irq_handler(int irq, void *data)
{
	struct dp_dev *dp_dev = data;
	irqreturn_t ret = IRQ_NONE;
	u32 status;

	status = readl(dp_dev->conn->reg_mmap_addr + 0x80);
	DRM_INFO("%s() status 0x%x\n", __func__, status);
	if (status & BIT(17)) {
		status = readl(dp_dev->conn->reg_mmap_addr + 0x88);
		DRM_INFO("%s() HPD plug event 0x%x\n", __func__, status);
		if (status & INNO_DP_HPD_PLUG_EVENT)
			status |= INNO_DP_HPD_PLUG_EVENT;
		if (status & INNO_DP_HPD_UNPLUG_EVENT)
			status |= INNO_DP_HPD_UNPLUG_EVENT;
		writel(status, (dp_dev->conn->reg_mmap_addr + 0x88));
		ret = IRQ_WAKE_THREAD;
	} else {
		ret = IRQ_NONE;
	}

	return ret;
}

static irqreturn_t soc_dp_irq_thread_handler(int irq, void *data)
{
	struct dp_dev *dp_dev = data;
	u32 hpd_status;

	hpd_status = readl(dp_dev->conn->reg_mmap_addr + 0x88);
	DRM_INFO("%s() hpd_status 0x%x\n", __func__, hpd_status);
	if (hpd_status & INNO_DP_HPD_STATUS)
		dp_dev->connector_status = connector_status_connected;
	else
		dp_dev->connector_status = connector_status_disconnected;

	drm_kms_helper_hotplug_event(dp_dev->drm);

	return IRQ_HANDLED;
}

static int dp_dev_resource_init(struct dp_dev *dp_dev,
				struct platform_device *pdev)
{
	uint32_t dp_id, edp_id;
	struct resource *res;
	void __iomem *pmu_addr = (void __iomem *)ioremap(0xd4282800, 0x400);
	void __iomem *ciu_addr = (void __iomem *)ioremap(0xd4282c00, 0x200);
	u32 value;

	if (of_property_read_u32(pdev->dev.of_node, "dp-id", &dp_id))
		dp_id = -1;

	if (of_property_read_u32(pdev->dev.of_node, "edp-id", &edp_id))
		edp_id = -1;

	if (edp_id != -1)
		dp_dev->conn = inno_get_conn_module(INNO_CONN_EDP);
	else
		dp_dev->conn = inno_get_conn_module(INNO_CONN_DP);

	if (dp_id == 0 || edp_id == 0) {
		// mux dp0
		value = readl(ciu_addr + 0x12c);
		value |= BIT(8);
		writel(value, (ciu_addr + 0x12c));
	}

	// use dp pll
	// if (dp_id == 0 || edp_id == 0) {
	// 	value = readl(pmu_addr + 0x23c);
	// 	value |= BIT(2);
	// 	writel(value, (pmu_addr + 0x23c));
	// } else {
	// 	value = readl(pmu_addr + 0x23c);
	// 	value |= BIT(18);
	// 	writel(value, (pmu_addr + 0x23c));
	// }

	iounmap(ciu_addr);
	iounmap(pmu_addr);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to obtain resource.\n");
		return -EINVAL;
	}

	dp_dev->conn->reg_mmap_addr =
		devm_ioremap(&pdev->dev, res->start, res->end - res->start + 1);
	if (IS_ERR(dp_dev->conn->reg_mmap_addr)) {
		dev_err(&pdev->dev, "mapping failed.\n");
		return PTR_ERR(dp_dev->conn->reg_mmap_addr);
	}

	dev_set_drvdata(&pdev->dev, dp_dev->conn);
	dp_dev->conn->dev = &pdev->dev;

	return 0;
}

static int inno_dp_bind(struct device *dev, struct device *master, void *data)
{
	int ret;
	struct dp_dev *dp_dev;
	struct drm_device *drm = (struct drm_device *)data;
	struct platform_device *pdev = to_platform_device(dev);
	uint64_t clk_val;
	uint64_t set_clk_val;
	int irq;
	u32 status;

	DRM_INFO("%s()\n", __func__);

	dp_dev = devm_kmalloc(dev, sizeof(*dp_dev), GFP_KERNEL);
	if (!dp_dev)
		return -ENOMEM;
	memset(dp_dev, 0, sizeof(*dp_dev));

	dp_dev->dev = dev;
	dp_dev->drm = drm;
	dp_dev->proc_irq = NULL;
	dp_dev->connector_status = connector_status_connected;

	dp_dev->reset = devm_reset_control_get_optional_shared(&pdev->dev, "reset");
	if (IS_ERR_OR_NULL(dp_dev->reset)) {
		DRM_INFO("Failed to found reset\n");
	}

	dp_dev->pxclk = of_clk_get_by_name(dev->of_node, "pxclk");
	if (IS_ERR(dp_dev->pxclk)) {
		dp_dev->pxclk = NULL;
		DRM_INFO("Failed to found pxclk\n");
	}

	ret = of_property_read_u32(dev->of_node, "gpios-bl", &dp_dev->gpio_bl);
	if (ret || !gpio_is_valid(dp_dev->gpio_bl)) {
		dev_info(dev, "missing dt property: gpios-bl\n");
		dp_dev->gpio_bl = INVALID_GPIO;
	} else {
		ret = gpio_request(dp_dev->gpio_bl, NULL);
		if (ret) {
			pr_err("gpio_bl request fail\n");
		}
	}

	ret = of_property_read_u32(dev->of_node, "gpios-enable", &dp_dev->gpio_enable);
	if (ret || !gpio_is_valid(dp_dev->gpio_enable)) {
		dev_info(dev, "missing dt property: gpios-enable\n");
		dp_dev->gpio_enable = INVALID_GPIO;
	} else {
		ret = gpio_request(dp_dev->gpio_enable, NULL);
		if (ret) {
			pr_err("gpio_enable request fail\n");
		}
	}

	ret = of_property_read_u32(dev->of_node, "gpios-power", &dp_dev->gpio_power);
	if (ret || !gpio_is_valid(dp_dev->gpio_power)) {
		dev_info(dev, "missing dt property: gpios-power\n");
		dp_dev->gpio_power = INVALID_GPIO;
	} else {
		ret = gpio_request(dp_dev->gpio_power, NULL);
		if (ret) {
			pr_err("gpio_power request fail\n");
		}
	}

	if (!IS_ERR_OR_NULL(dp_dev->reset)) {
		ret = reset_control_deassert(dp_dev->reset);
		if (ret < 0) {
			DRM_INFO("Failed to deassert reset\n");
		}
	}

	if (dp_dev->pxclk)
		clk_prepare_enable(dp_dev->pxclk);

	if(INVALID_GPIO != dp_dev->gpio_power)
		gpio_direction_output(dp_dev->gpio_power, 1);
	if(INVALID_GPIO != dp_dev->gpio_enable)
		gpio_direction_output(dp_dev->gpio_enable, 1);
	if(INVALID_GPIO != dp_dev->gpio_bl)
		gpio_direction_output(dp_dev->gpio_bl, 1);

	ret = drm_connector_init(drm, &dp_dev->connector,
				 &dp_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		dev_err(dev, "Connector initialization failed.\n");
		return ret;
	}
	drm_connector_helper_add(&dp_dev->connector,
				 &dp_conn_helper_funcs);

	ret = drm_encoder_init(drm, &dp_dev->encoder,
			       &dp_encoder_funcs, DRM_MODE_ENCODER_NONE, NULL);
	if (ret) {
		dev_err(dev, "Encoder initialization failed.\n");
		drm_connector_cleanup(&dp_dev->connector);
		return ret;
	}
	drm_encoder_helper_add(&dp_dev->encoder,
			       &dp_encoder_helper_funcs);

	dp_dev->encoder.possible_crtcs =
		drm_of_find_possible_crtcs(drm, dev->of_node);
	drm_connector_attach_encoder(&dp_dev->connector,
				     &dp_dev->encoder);

	platform_set_drvdata(pdev, dp_dev);
	// dp_proc_irq_debug_init(dp_dev);

	ret = dp_dev_resource_init(dp_dev, pdev);
	if (ret) {
		drm_connector_cleanup(&dp_dev->connector);
		return ret;
	}

	inno_init(dp_dev->conn);

	if (inno_hpd_detect(dp_dev->conn))
		dp_dev->connector_status = connector_status_connected;
	else
		dp_dev->connector_status = connector_status_disconnected;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to obtain interrupt ret = %d.\n", irq);
		return irq;
	}

	status = readl(dp_dev->conn->reg_mmap_addr + 0x84);
	status &= ~BIT(16);
	status |= BIT(17);
	writel(status, (dp_dev->conn->reg_mmap_addr + 0x84));

	ret = devm_request_threaded_irq(&pdev->dev, irq, soc_dp_irq_handler,
			soc_dp_irq_thread_handler, 0, dev_name(&pdev->dev), dp_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failure requesting irq %d: %d.\n", irq, ret);
		return ret;
	}

	return 0;
}

static void inno_dp_unbind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dp_dev *dp_dev = platform_get_drvdata(pdev);
	int ret;

	DRM_INFO("%s()\n", __func__);

	// dp_proc_irq_debug_exit(dp_dev);
	drm_encoder_cleanup(&dp_dev->encoder);
	drm_connector_cleanup(&dp_dev->connector);

	if(INVALID_GPIO != dp_dev->gpio_bl)
		gpio_direction_output(dp_dev->gpio_bl, 0);
	if(INVALID_GPIO != dp_dev->gpio_enable)
		gpio_direction_output(dp_dev->gpio_enable, 0);
	if(INVALID_GPIO != dp_dev->gpio_power)
		gpio_direction_output(dp_dev->gpio_power, 0);

	inno_exit(dp_dev->conn);

	if (dp_dev->pxclk)
		clk_disable_unprepare(dp_dev->pxclk);

	if (!IS_ERR_OR_NULL(dp_dev->reset)) {
		ret = reset_control_assert(dp_dev->reset);
		if (ret < 0) {
			DRM_INFO("Failed to assert reset\n");
		}
	}
}

static const struct component_ops inno_dp_ops = {
	.bind = inno_dp_bind,
	.unbind = inno_dp_unbind,
};

static int inno_dp_probe(struct platform_device *pdev)
{
	DRM_INFO("%s()\n", __func__);
	return component_add(&pdev->dev, &inno_dp_ops);
}

static void inno_dp_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &inno_dp_ops);
}

static const struct of_device_id inno_dp_match[] = {
	{ .compatible = "spacemit,inno-dp0" },
	{ .compatible = "spacemit,inno-dp1" },
	{ .compatible = "spacemit,inno-edp0" },
	{ .compatible = "spacemit,inno-edp1" },
	{}
};
MODULE_DEVICE_TABLE(of, inno_dp_match);

struct platform_driver inno_dp_driver = {
	.probe = inno_dp_probe,
	.remove = inno_dp_remove,
	.driver = {
		.name = "spacemit-innodp-drv",
		.of_match_table = inno_dp_match,
	},
};

// module_platform_driver(inno_dp_driver);

static int inno_dp_driver_init(void)
{
	return platform_driver_register(&inno_dp_driver);
}
late_initcall(inno_dp_driver_init);
