// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sysfs.h>
#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/rtc.h>

#include "../spacemit_lib.h"
#include "../spacemit_crtc.h"
#include "../spacemit_mipi_panel.h"
#include "sysfs_display.h"

#ifdef CONFIG_PM
static ssize_t spacemit_dpu_get_lpm_period(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "lpm peroid = %d\n", a_crtc->lpm_period);
}

static ssize_t spacemit_dpu_set_lpm_period(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t count)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);
	int period = 0;
	int ret = 0;

	ret = sscanf(buf, "%d\n", &period);
	if ((ret != 1) || (period < SPACEMIT_DPU_LPM_PERIOD_MIN_MS)
		|| (period > SPACEMIT_DPU_LPM_PERIOD_MAX_MS)) {
		pr_err("Wrong parameter! Please echo peroid in[%d, %d]\n",
			SPACEMIT_DPU_LPM_PERIOD_MIN_MS, SPACEMIT_DPU_LPM_PERIOD_MAX_MS);
		return -EINVAL;
	}

	a_crtc->lpm_period = period;

	return count;
}

static ssize_t spacemit_dpu_get_enable_auto_fc(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "enable_auto_fc = %d %d\n", a_crtc->enable_auto_fc, a_crtc->dev->power.runtime_status);
}

static ssize_t spacemit_dpu_set_enable_auto_fc(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf,
					       size_t count)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);
	int enable_auto_fc = 0;
	int ret = 0;

	if (a_crtc->dev->power.runtime_status != RPM_SUSPENDED) {
		pr_err("set dpu_enable_auto_fc only support when screen off!\n");
		return -EINVAL;
	}

	ret = sscanf(buf, "%d\n", &enable_auto_fc);
	if ((ret != 1) || (enable_auto_fc < 0) || (enable_auto_fc > 1)) {
		pr_err("Wrong parameter! Please echo 0 or 1\n");
		return -EINVAL;
	}

	if (enable_auto_fc == 0) {
		a_crtc->new_mclk = DPU_MCLK_DEFAULT;
		if (a_crtc->core && a_crtc->core->update_clk)
			a_crtc->core->update_clk(a_crtc, a_crtc->new_mclk);
	}

	a_crtc->enable_auto_fc = enable_auto_fc;

	return count;
}
#endif

static ssize_t spacemit_dpu_get_enable_dump_reg(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "enable_dump_reg = %d\n", a_crtc->enable_dump_reg);
}

static ssize_t spacemit_dpu_set_enable_dump_reg(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t count)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);
	int enable_dump_reg = 0;
	int ret = 0;

	ret = sscanf(buf, "%d\n", &enable_dump_reg);
	if ((ret != 1) || (enable_dump_reg < 0) || (enable_dump_reg > 1)) {
		pr_err("Wrong parameter! Please echo 0 or 1\n");
		return -EINVAL;
	}

	a_crtc->enable_dump_reg = enable_dump_reg;

	return count;
}

static ssize_t spacemit_dpu_get_enable_dump_fps(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "enable_dump_fps = %d\n", a_crtc->enable_dump_fps);
}

static ssize_t spacemit_dpu_set_enable_dump_fps(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t count)
{
	struct spacemit_crtc *a_crtc = dev_get_drvdata(dev);
	int enable_dump_fps = 0;
	int ret = 0;

	ret = sscanf(buf, "%d\n", &enable_dump_fps);
	if ((ret != 1) || (enable_dump_fps < 0) || (enable_dump_fps > 1)) {
		pr_err("Wrong parameter! Please echo 0 or 1\n");
		return -EINVAL;
	}

	a_crtc->enable_dump_fps = enable_dump_fps;

	return count;
}

static DEVICE_ATTR(dpu_enable_dump_fps, S_IRUGO | S_IWUSR, spacemit_dpu_get_enable_dump_fps, spacemit_dpu_set_enable_dump_fps);
static DEVICE_ATTR(dpu_enable_dump_reg, S_IRUGO | S_IWUSR, spacemit_dpu_get_enable_dump_reg, spacemit_dpu_set_enable_dump_reg);
#ifdef CONFIG_PM
static DEVICE_ATTR(dpu_enable_auto_fc, S_IRUGO | S_IWUSR, spacemit_dpu_get_enable_auto_fc, spacemit_dpu_set_enable_auto_fc);
static DEVICE_ATTR(dpu_set_lpm_period, S_IRUGO | S_IWUSR, spacemit_dpu_get_lpm_period, spacemit_dpu_set_lpm_period);
#endif

int spacemit_dpu_sysfs_init(struct device *dev)
{

	int ret = 0;

	ret = device_create_file(dev, &dev_attr_dpu_enable_dump_reg);
	if (ret)
		DRM_ERROR("failed to create device file: enable_dump_reg\n");
	else
		DRM_INFO("create device file enable_dump_reg\n");

	ret = device_create_file(dev, &dev_attr_dpu_enable_dump_fps);
	if (ret)
		DRM_ERROR("failed to create device file: enable_dump_fps\n");
	else
		DRM_INFO("create device file enable_dump_fps\n");
#ifdef CONFIG_PM
	ret = device_create_file(dev, &dev_attr_dpu_enable_auto_fc);
	if (ret)
		DRM_ERROR("failed to create device file: enable_auto_fc\n");
	else
		DRM_INFO("create device file enable_auto_fc\n");

	ret = device_create_file(dev, &dev_attr_dpu_set_lpm_period);
	if (ret)
		DRM_ERROR("failed to create device file: dpu_set_lpm_period\n");
	else
		DRM_INFO("create device file dpu_set_lpm_period\n");
#endif
	return 0;
}
EXPORT_SYMBOL(spacemit_dpu_sysfs_init);

MODULE_DESCRIPTION("Add dpu attribute nodes for userspace");
MODULE_LICENSE("GPL v2");
