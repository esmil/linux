// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V MPXY Based RTC Driver
 *
 * Copyright (C) 2025 Spacemit.
 */

#include <linux/rtc.h>
#include <linux/err.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/msi_api.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/mailbox/riscv-rpmi-message.h>

/** RPMI rtc service IDs */
enum rpmi_rtc_service_id {
	RPMI_RTC_SRV_ENABLE_NOTIFICATION = 0x01,
	RPMI_RTC_SRV_SET_TIME = 0x02,
	RPMI_RTC_SRV_GET_TIME = 0x03,
	RPMI_RTC_SRV_SET_ALARM = 0x04,
	RPMI_RTC_SRV_GET_ALARM = 0x05,
	RPMI_RTC_SRV_ALARM_GET_EN = 0x06,
	RPMI_RTC_SRV_ALARM_SET_EN = 0x07,
	RPMI_RTC_SRV_QUERY_PENDING = 0x8,
	RPMI_RTC_SRV_CLR_PENDING = 0x9,
	RPMI_RTC_SRV_ID_MAX_COUNT,
};

struct rpmi_rtc_set_time_req {
	u32 year;
	u32 mon;
	u32 date;
	u32 hour;
	u32 min;
	u32 second;
};

struct rpmi_rtc_set_time_resp {
	u32 status;
};

struct rpmi_rtc_get_time_req {
	u32 dummy;
};

struct rpmi_rtc_get_time_resp {
	s32 status;
	u32 year;
	u32 mon;
	u32 date;
	u32 hour;
	u32 min;
	u32 second;
};

struct rpmi_rtc_set_alarm_req {
	u32 year;
	u32 mon;
	u32 date;
	u32 hour;
	u32 min;
	u32 second;
};

struct rpmi_rtc_set_alarm_resp {
	s32 status;
};

struct rpmi_rtc_get_alarm_req {
	u32 dummy;
};

struct rpmi_rtc_get_alarm_resp {
	s32 status;
	u32 year;
	u32 mon;
	u32 date;
	u32 hour;
	u32 min;
	u32 second;
};

struct rpmi_rtc_get_alarm_en_req {
	u32 dummy;
};

struct rpmi_rtc_get_alarm_en_resp {
	s32 status;
};

struct rpmi_rtc_set_alarm_en_req {
	u32 en;
};

struct rpmi_rtc_set_alarm_en_resp {
	s32 status;
};

struct rpmi_rtc_query_pending_req {
	u32 dummy;
};

struct rpmi_rtc_query_pending_resp {
	s32 status;
};

struct rpmi_rtc_clear_pending_req {
	u32 dummy;
};

struct rpmi_rtc_clear_pending_resp {
	s32 status;
};

struct rpmi_rtc_context {
	int virt_irq;
	struct device *dev;
	struct rtc_device *rtc;
	struct mbox_chan *chan;
	struct mbox_client client;
	u32 max_msg_data_size;
};

static int rpmi_read_time(struct device *dev, struct rtc_time *time)
{
	int ret;
	struct rpmi_mbox_message msg;
	struct rpmi_rtc_context *context;
	struct rpmi_rtc_get_time_req tx;
	struct rpmi_rtc_get_time_resp rx;

	context = dev_get_drvdata(dev);

	rpmi_mbox_init_send_with_response(&msg, RPMI_RTC_SRV_GET_TIME,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret) {
		return ret;
	}
	if (rx.status) {
		return rpmi_to_linux_error(rx.status);
	}

        time->tm_sec = rx.second;
        time->tm_min = rx.min;
        time->tm_hour = rx.hour;
        time->tm_mday = rx.date;
        time->tm_mon = rx.mon;
        time->tm_year = rx.year;

	return 0;
}

static int rpmi_set_time(struct device *dev, struct rtc_time *time)
{
	int ret;
	struct rpmi_mbox_message msg;
	struct rpmi_rtc_context *context;
	struct rpmi_rtc_set_time_req tx;
	struct rpmi_rtc_set_time_resp rx;

	context = dev_get_drvdata(dev);

	tx.year = time->tm_year;
	tx.mon = time->tm_mon;
	tx.date = time->tm_mday;
	tx.hour = time->tm_hour;
	tx.min = time->tm_min;
	tx.second = time->tm_sec;

	rpmi_mbox_init_send_with_response(&msg, RPMI_RTC_SRV_SET_TIME,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

	return 0;
}

static int rpmi_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	int ret;
	struct rpmi_mbox_message msg;
	struct rpmi_rtc_context *context;
	struct rpmi_rtc_get_alarm_req tx;
	struct rpmi_rtc_get_alarm_resp rx;
	struct rpmi_rtc_get_alarm_en_req alarmtx;
	struct rpmi_rtc_get_alarm_en_resp alarmrx;

	context = dev_get_drvdata(dev);

	rpmi_mbox_init_send_with_response(&msg, RPMI_RTC_SRV_GET_ALARM,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (rx.status)
		return rpmi_to_linux_error(rx.status);

        alarm->time.tm_sec = rx.second;
        alarm->time.tm_min = rx.min;
        alarm->time.tm_hour = rx.hour;
        alarm->time.tm_mday = rx.date;
        alarm->time.tm_mon = rx.mon;
        alarm->time.tm_year = rx.year;

	rpmi_mbox_init_send_with_response(&msg, RPMI_RTC_SRV_ALARM_GET_EN,
					  &alarmtx, sizeof(alarmtx), &alarmrx, sizeof(alarmrx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;

	alarm->enabled = alarmrx.status;

	return 0;
}

static int rpmi_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	int ret;
	struct rpmi_mbox_message msg;
	struct rpmi_rtc_context *context;
	struct rpmi_rtc_set_alarm_req tx;
	struct rpmi_rtc_set_alarm_resp rx;
	struct rpmi_rtc_set_alarm_en_req alarmtx;
	struct rpmi_rtc_set_alarm_en_resp alarmrx;

	context = dev_get_drvdata(dev);

	tx.year = alarm->time.tm_year;
	tx.mon = alarm->time.tm_mon;
	tx.date = alarm->time.tm_mday;
	tx.hour = alarm->time.tm_hour;
	tx.min = alarm->time.tm_min;
	tx.second = alarm->time.tm_sec;

	rpmi_mbox_init_send_with_response(&msg, RPMI_RTC_SRV_SET_ALARM,
					  &tx, sizeof(tx), &rx, sizeof(rx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;

	if (rx.status)
		return rpmi_to_linux_error(rx.status);


	alarmtx.en = alarm->enabled;

	rpmi_mbox_init_send_with_response(&msg, RPMI_RTC_SRV_ALARM_SET_EN,
					  &alarmtx, sizeof(alarmtx), &alarmrx, sizeof(alarmrx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (alarmrx.status)
		return rpmi_to_linux_error(alarmrx.status);

	return 0;
}

static int rpmi_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	int ret;
	struct rpmi_mbox_message msg;
	struct rpmi_rtc_context *context;
	struct rpmi_rtc_set_alarm_en_req alarmtx;
	struct rpmi_rtc_set_alarm_en_resp alarmrx;

	context = dev_get_drvdata(dev);

	alarmtx.en = enabled;

	rpmi_mbox_init_send_with_response(&msg, RPMI_RTC_SRV_ALARM_SET_EN,
					  &alarmtx, sizeof(alarmtx), &alarmrx, sizeof(alarmrx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return ret;
	if (alarmrx.status)
		return rpmi_to_linux_error(alarmrx.status);

	return 0;
}

static struct rtc_class_ops rpmi_rtc_class_ops = {
	.read_time = rpmi_read_time,
	.set_time = rpmi_set_time,
	.read_alarm = rpmi_read_alarm,
	.set_alarm = rpmi_set_alarm,
	.alarm_irq_enable = rpmi_alarm_irq_enable,
};

static irqreturn_t mpxy_rtc_irq_event(int irq, void *dev_id)
{
	/* We only have MSI for notification so just wakeup IRQ thread */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t mpxy_rtc_irq_thread(int irq, void *dev_id)
{
	int ret;
	struct rpmi_mbox_message msg;
	struct rpmi_rtc_context *context = dev_id;
	struct rpmi_rtc_query_pending_req alarmtx;
	struct rpmi_rtc_query_pending_resp alarmrx;
	struct rpmi_rtc_clear_pending_req alarmctx;
	struct rpmi_rtc_clear_pending_resp alarmcrx;

	rpmi_mbox_init_send_with_response(&msg, RPMI_RTC_SRV_QUERY_PENDING,
					  &alarmtx, sizeof(alarmtx), &alarmrx, sizeof(alarmrx));
	ret = rpmi_mbox_send_message(context->chan, &msg);
	if (ret)
		return IRQ_HANDLED;

	if (alarmrx.status) {
		rpmi_mbox_init_send_with_response(&msg, RPMI_RTC_SRV_CLR_PENDING,
					  &alarmctx, sizeof(alarmctx), &alarmcrx, sizeof(alarmcrx));
		ret = rpmi_mbox_send_message(context->chan, &msg);
		if (ret)
			return IRQ_HANDLED;
		if (alarmcrx.status)
			return IRQ_HANDLED;
	}

	rtc_update_irq(context->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static int rpmi_rtc_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct rpmi_rtc_context *context;
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

	/* request msi irq */
	context->virt_irq = platform_get_irq_byname(pdev, "rpmi rtc");
	/* Request channel MSI handler */
	ret = request_threaded_irq(context->virt_irq,
				  mpxy_rtc_irq_event,
				  mpxy_rtc_irq_thread,
				  IRQF_SHARED | IRQF_ONESHOT, dev_name(dev), context);
	if (ret) {
		dev_err(dev, "failed to request MPXY channel IRQ\n");
		return ret;
	}

	dev_pm_set_wake_irq(&pdev->dev, context->virt_irq);
	device_init_wakeup(&pdev->dev, 1);

	context->rtc = devm_rtc_allocate_device(dev);
	context->rtc->ops = &rpmi_rtc_class_ops;
	context->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	context->rtc->range_max = RTC_TIMESTAMP_END_2063;

	return devm_rtc_register_device(context->rtc);

fail_free_channel:
	mbox_free_channel(context->chan);
	return ret;
}

static const struct of_device_id rpmi_rtc_of_match[] = {
	{ .compatible = "riscv,rpmi-rtc" },
	{ }
};
MODULE_DEVICE_TABLE(of, rpmi_rtc_of_match);

static struct platform_driver rpmi_rtc_driver = {
	.driver = {
		.name = "riscv-rpmi-rtc",
		.of_match_table = rpmi_rtc_of_match,
	},
	.probe = rpmi_rtc_probe,
};
module_platform_driver(rpmi_rtc_driver);

MODULE_AUTHOR("xianbin.zhu <xianbin.zhu@linux.spacemit.com>");
MODULE_DESCRIPTION("RTC Driver based on RPMI message protocol");
MODULE_LICENSE("GPL");
