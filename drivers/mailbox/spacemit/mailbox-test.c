// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 ST Microelectronics
 *
 * Author: Lee Jones <lee.jones@linaro.org>
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/delay.h>

struct mbox_test_device {
	struct device		*dev;
	/* system0 channels */
	struct mbox_chan	*s0s_channel;
	struct mbox_chan	*s0r_channel;
	/* system1 channels */
	struct mbox_chan	*s1s_channel;
	struct mbox_chan	*s1r_channel;

	struct task_struct	*s0r_thread;
	struct task_struct	*s1r_thread;

	/* system0 clint */
	struct mbox_client	*s0r_clint;
	/* system1 clint */
	struct mbox_client	*s1r_clint;
	struct completion	user0_comp;
	struct completion	user1_comp;
	bool			user0_thread_run;
	bool			user1_thread_run;
};

static unsigned long long user0_count;
static unsigned long long user1_count;

static void user0_mbox_test_receive_message(struct mbox_client *client, void *message)
{
	struct mbox_test_device *tdev;

	tdev = dev_get_drvdata(client->dev);

	++user0_count;

	if (user0_count != user1_count)
		pr_err("testing wrong: asymmetric sending and receiving\n");

	complete(&tdev->user0_comp);
}

static void user1_mbox_test_receive_message(struct mbox_client *client, void *message)
{
	struct mbox_test_device *tdev;

	tdev = dev_get_drvdata(client->dev);

	++user1_count;

	complete(&tdev->user1_comp);
}

static int __user0_process_theread(void *arg)
{
	int ret;
	char c = 'c';
	struct mbox_client *cl = arg;
	struct mbox_test_device *tdev = dev_get_drvdata(cl->dev);
	struct sched_param param = {.sched_priority = 0 };

	tdev->user0_thread_run = true;
	ret = sched_setscheduler(current, SCHED_FIFO, &param);
	set_freezable();

	do {
		try_to_freeze();
		wait_for_completion(&tdev->user0_comp);
		msleep(200);
		/* send message again */
		mbox_send_message(tdev->s0s_channel, &c);
	} while (!kthread_should_stop());

	tdev->user0_thread_run = true;

	return 0;
}

static int __user1_process_theread(void *arg)
{
	int ret;
	char c = 'c';
	struct mbox_client *cl = arg;
	struct mbox_test_device *tdev = dev_get_drvdata(cl->dev);
	struct sched_param param = {.sched_priority = 0 };

	tdev->user1_thread_run = true;
	ret = sched_setscheduler(current, SCHED_FIFO, &param);
	set_freezable();

	do {
		try_to_freeze();
		wait_for_completion(&tdev->user1_comp);
		msleep(200);
		/* send message again */
		mbox_send_message(tdev->s1s_channel, &c);
	} while (!kthread_should_stop());

	tdev->user0_thread_run = true;

	return 0;
}

static struct mbox_chan *mbox_test_request_channel(struct platform_device *pdev, const char *name)
{
	struct mbox_client *client;
	struct mbox_chan *channel;
	struct mbox_test_device *tdev = platform_get_drvdata(pdev);

	client = devm_kzalloc(&pdev->dev, sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	if (strcmp(name, "s0r") == 0) {
		client->rx_callback	= user0_mbox_test_receive_message;
		init_completion(&tdev->user0_comp);
		tdev->s0r_clint = client;
	} else if (strcmp(name, "s1r") == 0) {
		client->rx_callback	= user1_mbox_test_receive_message;
		init_completion(&tdev->user1_comp);
		tdev->s1r_clint = client;
	}

	client->dev = &pdev->dev;
	client->tx_block = true;

	channel = mbox_request_channel_byname(client, name);
	if (IS_ERR(channel)) {
		dev_warn(&pdev->dev, "Failed to request %s channel\n", name);
		return NULL;
	}

	return channel;
}

static int mbox_test_probe(struct platform_device *pdev)
{
	struct mbox_test_device *tdev;
	char c = 'c';

	tdev = devm_kzalloc(&pdev->dev, sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;

	tdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, tdev);

	tdev->s0s_channel = mbox_test_request_channel(pdev, "s0s");
	tdev->s0r_channel = mbox_test_request_channel(pdev, "s0r");
	if (IS_ERR_OR_NULL(tdev->s0s_channel) && IS_ERR_OR_NULL(tdev->s0r_channel))
		return -EPROBE_DEFER;

	tdev->s1s_channel = mbox_test_request_channel(pdev, "s1s");
	tdev->s1r_channel = mbox_test_request_channel(pdev, "s1r");
	if (IS_ERR_OR_NULL(tdev->s1s_channel) && IS_ERR_OR_NULL(tdev->s1r_channel))
		return -EPROBE_DEFER;


	tdev->s0r_thread = kthread_run(__user0_process_theread, (void *)tdev->s0r_clint, "user0_test_thread"); 
	tdev->s1r_thread = kthread_run(__user1_process_theread, (void *)tdev->s1r_clint, "user1_test_thread"); 

	msleep(100);

	/* trigger the test */
	mbox_send_message(tdev->s0s_channel, &c);

	dev_info(&pdev->dev, "Successfully registered\n");

	return 0;
}

static void mbox_test_remove(struct platform_device *pdev)
{
	struct mbox_test_device *tdev = platform_get_drvdata(pdev);

	mbox_free_channel(tdev->s0s_channel);
	mbox_free_channel(tdev->s0r_channel);
	mbox_free_channel(tdev->s1s_channel);
	mbox_free_channel(tdev->s1r_channel);
}

static const struct of_device_id mbox_test_match[] = {
	{ .compatible = "spacemit,mailbox_test" },
	{},
};
MODULE_DEVICE_TABLE(of, mbox_test_match);

static struct platform_driver mbox_test_driver = {
	.driver = {
		.name = "mailbox_test",
		.of_match_table = mbox_test_match,
	},
	.probe  = mbox_test_probe,
	.remove_new = mbox_test_remove,
};
module_platform_driver(mbox_test_driver);

MODULE_DESCRIPTION("spacemit Message Box test driver");
MODULE_LICENSE("GPL v2");
