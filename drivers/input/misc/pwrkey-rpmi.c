// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V MPXY Based PWRKEY Driver
 *
 * Copyright (C) 2025 Spacemit.
 */

#include <linux/rtc.h>
#include <linux/err.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/pm_wakeirq.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/mailbox/riscv-rpmi-message.h>

static int report_event, fall_triggered;
static struct notifier_block   pm_notify;
static spinlock_t pm_lock;

/** RPMI pwrkey service IDs */
enum rpmi_pwrkey_service_id {
	RPMI_PWRKEY_SRV_ENABLE_NOTIFICATION = 0x01,
	RPMI_PWRKEY_SRV_QUERY_PENDING = 0x02,
	RPMI_PWRKEY_SRV_CLR_PENDING = 0x03,
	RPMI_PWRKEY_SRV_ID_MAX_COUNT,
};

struct rpmi_pwrkey_query_pending_req {
	u32 dummy;
};

struct rpmi_pwrkey_query_pending_resp {
#define RPMI_PWRKEY_RELEASE_OFFSET	(1 << 0)
#define RPMI_PWRKEY_PRESS_OFFSET	(1 << 1)
	s32 status;
};

struct rpmi_pwrkey_clear_pending_req {
#define RPMI_PWRKEY_RELEASE_OFFSET	(1 << 0)
#define RPMI_PWRKEY_PRESS_OFFSET	(1 << 1)
	u32 clear;
};

struct rpmi_pwrkey_clear_pending_resp {
	s32 status;
};

struct rpmi_pwrkey_context {
	int virt_irq;
	struct device *dev;
	struct input_dev *input;
	struct mbox_chan *chan;
	struct mbox_client client;
	u32 max_msg_data_size;
};

static int pwrk_pm_notify(struct notifier_block *notify_block,
			unsigned long mode, void *unused)
{
	unsigned long flags;

	spin_lock_irqsave(&pm_lock, flags);

	switch (mode) {
	case PM_SUSPEND_PREPARE:
		/* don't report power-key when enter suspend */
		report_event = 0;
		break;

	case PM_POST_SUSPEND:
		/* restore report power-key */
		report_event = 1;
		break;
	}

	spin_unlock_irqrestore(&pm_lock, flags);

	return 0;
}

static irqreturn_t mpxy_pwrkey_irq_event(int irq, void *dev_id)
{
	/* We only have MSI for notification so just wakeup IRQ thread */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t mpxy_pwrkey_irq_thread(int irq, void *dev_id)
{
	int ret;
	unsigned long flags;
	struct rpmi_mbox_message msg;
	struct rpmi_pwrkey_context *context = dev_id;
	struct rpmi_pwrkey_query_pending_req alarmtx;
	struct rpmi_pwrkey_query_pending_resp alarmrx;
	struct rpmi_pwrkey_clear_pending_req alarmctx;
	struct rpmi_pwrkey_clear_pending_resp alarmcrx;

	rpmi_mbox_init_send_with_response(&msg, RPMI_PWRKEY_SRV_QUERY_PENDING,
					  &alarmtx, sizeof(alarmtx), &alarmrx, sizeof(alarmrx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return IRQ_HANDLED;

	if (alarmrx.status) {
		spin_lock_irqsave(&pm_lock, flags);

		/* update the power key event */
		if (alarmrx.status & RPMI_PWRKEY_PRESS_OFFSET) {
			if (report_event) {
				input_report_key(context->input, KEY_POWER, 1);
				input_sync(context->input);
				fall_triggered = 1;
			}
		}

		if (alarmrx.status & RPMI_PWRKEY_RELEASE_OFFSET) {
			if (fall_triggered) {
				input_report_key(context->input, KEY_POWER, 0);
				input_sync(context->input);
				fall_triggered = 0;
			}
		}

		spin_unlock_irqrestore(&pm_lock, flags);

		pm_wakeup_event(context->dev, 0);

		/* clear pending */
		alarmctx.clear = alarmrx.status;
		rpmi_mbox_init_send_with_response(&msg, RPMI_PWRKEY_SRV_CLR_PENDING,
					  &alarmctx, sizeof(alarmctx), &alarmcrx, sizeof(alarmcrx));
		ret = rpmi_mbox_send_message(context->chan, &msg);
		if (ret)
			return IRQ_HANDLED;

		if (alarmcrx.status)
			return IRQ_HANDLED;
	}

	return IRQ_HANDLED;
}

static int rpmi_pwrkey_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct rpmi_pwrkey_context *context;
	struct rpmi_mbox_message msg;

	/* Allocate RPMI rtc context */
	context = devm_kzalloc(dev, sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;
	context->dev = dev;
	platform_set_drvdata(pdev, context);

	/* Setup mailbox client */
	context->client.dev		= context->dev;
	context->client.rx_callback	= NULL;
	context->client.tx_block	= false;
	context->client.knows_txdone	= true;
	context->client.tx_tout		= 0;

	/* Request mailbox channel */
	context->chan = mbox_request_channel(&context->client, 0);
	if (IS_ERR(context->chan))
		return PTR_ERR(context->chan);

	/* Validate RPMI specification version */
	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_SPEC_VERSION);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to get spec version\n");
		goto fail_free_channel;
	}
	if (msg.attr.value < RPMI_MKVER(1, 0)) {
		ret = dev_err_probe(dev, -EINVAL,
				    "msg protocol version mismatch, expected 0x%x, found 0x%x\n",
				    RPMI_MKVER(1, 0), msg.attr.value);
		goto fail_free_channel;
	}

	/* Save the maximum message data size of mailbox channel */
	rpmi_mbox_init_get_attribute(&msg, RPMI_MBOX_ATTR_MAX_MSG_DATA_SIZE);
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to get max message data size\n");
		goto fail_free_channel;
	}
	context->max_msg_data_size = msg.attr.value;

	/* register input event */
	context->input = devm_input_allocate_device(&pdev->dev);
	if (context->input == NULL) {
		dev_err_probe(dev, -EINVAL, "Failed to allocate input device\n");
		goto fail_free_channel;
	}

        context->input->name = "rpmi pwrkey";
        context->input->phys = "rpmi-pwrkey/input0";
        context->input->id.bustype = BUS_HOST;
        input_set_capability(context->input, EV_KEY, KEY_POWER);

	ret = input_register_device(context->input);
	if (ret) {
		dev_err(dev, "failed to register input device\n");
		goto fail_free_channel;
	}

	context->virt_irq = platform_get_irq_byname(pdev, "rpmi pwrkey");
	ret = request_threaded_irq(context->virt_irq,
				  mpxy_pwrkey_irq_event,
				  mpxy_pwrkey_irq_thread,
				  IRQF_SHARED | IRQF_ONESHOT, dev_name(dev), context);
	if (ret) {
		dev_err(dev, "failed to request MPXY channel IRQ\n");
		goto fail_free_channel;
	}

        dev_pm_set_wake_irq(&pdev->dev, context->virt_irq);
        device_init_wakeup(&pdev->dev, true);

	spin_lock_init(&pm_lock);

	pm_notify.notifier_call = pwrk_pm_notify;
	ret = register_pm_notifier(&pm_notify);
	if (ret) {
		dev_err(&pdev->dev, "Register pm notifier failed\n");
		return ret;
	}

	return 0;

fail_free_channel:
	mbox_free_channel(context->chan);
	return ret;
}

static const struct of_device_id rpmi_pwrkey_of_match[] = {
	{ .compatible = "riscv,rpmi-pwrkey" },
	{ }
};
MODULE_DEVICE_TABLE(of, rpmi_pwrkey_of_match);

static struct platform_driver rpmi_pwrkey_driver = {
	.driver = {
		.name = "riscv-rpmi-pwrkey",
		.of_match_table = rpmi_pwrkey_of_match,
	},
	.probe = rpmi_pwrkey_probe,
};
module_platform_driver(rpmi_pwrkey_driver);

MODULE_AUTHOR("xianbin.zhu <xianbin.zhu@linux.spacemit.com>");
MODULE_DESCRIPTION("PWRKEY Driver based on RPMI message protocol");
MODULE_LICENSE("GPL");
