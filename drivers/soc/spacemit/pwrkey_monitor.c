// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 Spacemit */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/pm_wakeirq.h>
#include <linux/gpio/consumer.h>
#include <linux/property.h>
#include <linux/suspend.h>
#include <linux/pm_wakeirq.h>
#include <linux/cleanup.h>
#include <linux/of.h>

#define PWRKEY_EVENT_DISABLED	0
#define PWRKEY_EVENT_ENABLED	1
#define PWRKEY_PRESSED		1
#define PWRKEY_RELEASED		0
#define PWRKEY_FALL_TRIGGERED	1
#define PWRKEY_FALL_CLEARED	0

struct pwrkey_monitor {
	struct notifier_block pm_notify;
	int report_event, fall_triggered, wakeup_irq;
	struct device *dev;
	struct input_dev *input;
	struct gpio_desc *gpio;
	spinlock_t pm_lock;
};

static int pwrk_pm_notify(struct notifier_block *notify_block,
			unsigned long mode, void *unused)
{
	struct pwrkey_monitor *monitor = container_of(notify_block, struct pwrkey_monitor, pm_notify);

	guard(spinlock_irqsave)(&monitor->pm_lock);

	switch (mode) {
	case PM_SUSPEND_PREPARE:
		/* don't report power-key when enter suspend */
		monitor->report_event = PWRKEY_EVENT_DISABLED;
		break;

	case PM_POST_SUSPEND:
		/* restore report power-key */
		monitor->report_event = PWRKEY_EVENT_ENABLED;
		break;
	default:
		break;
	}

	return 0;
}

static irqreturn_t monitor_wakeup_detect(int irq, void *arg)
{
	unsigned char state = PWRKEY_RELEASED;
	struct pwrkey_monitor *monitor = (struct pwrkey_monitor *)arg;

	guard(spinlock_irqsave)(&monitor->pm_lock);

	state = gpiod_get_value(monitor->gpio);

	pm_wakeup_event(monitor->dev, 0);

	if (!state && monitor->report_event) {
		input_report_switch(monitor->input, KEY_POWER, PWRKEY_PRESSED);
	 	input_sync(monitor->input);
		monitor->fall_triggered = PWRKEY_FALL_TRIGGERED;
	} else if (state && monitor->fall_triggered) {
		input_report_switch(monitor->input, KEY_POWER, PWRKEY_RELEASED);
	 	input_sync(monitor->input);
		monitor->fall_triggered = PWRKEY_FALL_CLEARED;
	}

	return IRQ_HANDLED;
}

static int spacemit_pwrkey_monitor_probe(struct platform_device *pdev)
{
	struct pwrkey_monitor *monitor;
	struct input_dev *input;
	int error;

	monitor = devm_kzalloc(&pdev->dev, sizeof(*monitor), GFP_KERNEL);
	if (!monitor)
		return -ENOMEM;

	input = devm_input_allocate_device(&pdev->dev);
	if (!input)
		return -ENOMEM;

	input->name = pdev->name;
	input->id.bustype = BUS_HOST;
	input_set_capability(input, EV_KEY, KEY_POWER);

	input_set_drvdata(input, monitor);

	monitor->dev = &pdev->dev;
	monitor->input = input;
	monitor->report_event = PWRKEY_EVENT_ENABLED;

	spin_lock_init(&monitor->pm_lock);

	monitor->gpio = devm_gpiod_get(&pdev->dev, "monitor", GPIOD_IN);
	if (IS_ERR_OR_NULL(monitor->gpio))
		return PTR_ERR(monitor->gpio);

	monitor->wakeup_irq = platform_get_irq(pdev, 0);
	if (monitor->wakeup_irq < 0)
		return dev_err_probe(&pdev->dev, monitor->wakeup_irq, "Get irq failed\n");

	error = devm_request_irq(&pdev->dev, monitor->wakeup_irq,
				 monitor_wakeup_detect, IRQF_ONESHOT,
				 "monitor-detect", (void *)monitor);
	if (error)
		return dev_err_probe(&pdev->dev, error,
				     "request monitor pinctrl dectect failed:%d\n", error);

	error = input_register_device(input);
	if (error)
		return dev_err_probe(&pdev->dev, error, "could not register input device\n");

	monitor->pm_notify.notifier_call = pwrk_pm_notify;
	error = register_pm_notifier(&monitor->pm_notify);
	if (error) {
		input_unregister_device(input);
		return dev_err_probe(&pdev->dev, error, "Register pm notifier failed\n");
	}

	dev_pm_set_wake_irq(&pdev->dev, monitor->wakeup_irq);
 	device_init_wakeup(&pdev->dev, true);

	return 0;
}

static const struct of_device_id spacemit_pwrkey_monitor_of_match[] = {
	{ .compatible = "spacemit,k3-pwrkey-monitor", },
	{ },
};
MODULE_DEVICE_TABLE(of, spacemit_pwrkey_monitor_of_match);

static struct platform_driver spacemit_pwrkey_monitor_device_driver = {
	.probe		= spacemit_pwrkey_monitor_probe,
	.driver		= {
		.name	= "k3-pwrkey-monitor",
		.of_match_table = spacemit_pwrkey_monitor_of_match,
	}
};
module_platform_driver(spacemit_pwrkey_monitor_device_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("spacemit pwrkey-monitor driver");
