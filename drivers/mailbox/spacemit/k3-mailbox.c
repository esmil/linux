// SPDX-License-Identifier: GPL-2.0

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/of_address.h>
#include "../mailbox.h"
#include "k3_mailbox.h"

#define mbox_dbg(mbox, ...)	dev_dbg((mbox)->controller.dev, __VA_ARGS__)

static irqreturn_t spacemit_mbox_irq(int irq, void *dev_id)
{
	struct spacemit_mailbox *mbox = dev_id;
	struct mbox_chan *chan;
	u32 status, msg;
	int i, j;
	mbox_msg_status_t mstatus;
	unsigned long flags;

	spin_lock_irqsave(&mbox->lock, flags);

	status = readl((void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_status) &
		 readl((void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_en_set);

	if (!(status & 0xff)) {
		spin_unlock_irqrestore(&mbox->lock, flags);
		return IRQ_HANDLED;
	}

	for (i = 0; i < SPACEMIT_NUM_CHANNELS; ++i) {
		chan = &mbox->controller.chans[i];

		/* not full irq */
		if (status & (1 << (i * 2 + 1))) {
			/* disable not full irq */
			j = readl((void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_en_clr);
			j |= (1 << (i * 2 + 1));
			writel(j, (void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_en_clr);

			j = readl((void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_status_clr);
			j |= (1 << (i * 2 + 1));
			writel(j, (void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_status_clr);

			if (chan->txdone_method & TXDONE_BY_IRQ)
				mbox_chan_txdone(chan, 0);
		}

		/* new msg irq */
		if (status & (1 << (i * 2))) {

			/* clear the fifo */
			while (1) {
				msg = readl((void *)&mbox->regs->mbox_msg[i]);
				mstatus.val = readl((void *)&mbox->regs->msg_status[i]);
				if (mstatus.bits.num_msg == 0)
					break;
			}
			mbox_chan_received_data(chan, &msg);

#if 0
			/* disable the new msg irq */
			j = readl((void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_en_clr);
			j |= (1 << (i * 2));
			writel(j, (void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_en_clr);
#endif
			/* clear the irq pending */
			j = readl((void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_status_clr);
			j |= (1 << (i * 2));
			writel(j, (void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_status_clr);
		}
        }

	spin_unlock_irqrestore(&mbox->lock, flags);

	return IRQ_HANDLED;
}

static int spacemit_chan_send_data(struct mbox_chan *chan, void *data)
{
	int j;
	unsigned long flags;
	struct spacemit_mailbox *mbox = chan->con_priv;
	u32 chan_num = chan - mbox->controller.chans;

	spin_lock_irqsave(&mbox->lock, flags);

#if 0
	/* disable new msg irq */
	j = readl((void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_en_clr);
	j |= (1 << (chan_num * 2));
	writel(j, (void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_en_clr);

	/* clear pending */
	j = readl((void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_status_clr);
	j |= (1 << (chan_num * 2));
	writel(j, (void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_status_clr);
#endif

	/* enable the other end new msg irq */
	j = readl((void *)&mbox->regs->mbox_irq[USER1_MBOX_OFFSET].irq_en_set);
	j |= (1 << (chan_num * 2));
	writel(j, (void *)&mbox->regs->mbox_irq[USER1_MBOX_OFFSET].irq_en_set);

        /* send data */
	writel('c', (void *)&mbox->regs->mbox_msg[chan_num]);
#if 0
        /* set other end new msg irq thresh */
        j = readl((void *)&mbox->regs->mbox_thresh[USER1_MBOX_OFFSET].thresh0);
        j |= 7 << (chan_num * 8);
        writel(j, (void *)&mbox->regs->mbox_thresh[USER1_MBOX_OFFSET].thresh0);
#endif
	/* set not full thresh */
	j = readl((void *)&mbox->regs->mbox_thresh[USER0_MBOX_OFFSET].thresh0);
	j |= 1 << (chan_num * 8 + 4);
	writel(j, (void *)&mbox->regs->mbox_thresh[USER0_MBOX_OFFSET].thresh0);

	/* enable not full irq */
	j = readl((void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_en_set);
	j |= (1 << (chan_num * 2 + 1));
	writel(j, (void *)&mbox->regs->mbox_irq[USER0_MBOX_OFFSET].irq_en_set);

	spin_unlock_irqrestore(&mbox->lock, flags);

	return 0;
}

static int spacemit_chan_startup(struct mbox_chan *chan)
{
	struct spacemit_mailbox *mbox = chan->con_priv;
	u32 chan_num = chan - mbox->controller.chans;
	u32 msg;
	mbox_msg_status_t mstatus;
	unsigned long flags;

	spin_lock_irqsave(&mbox->lock, flags);

	/* clear the fifo */
	while (1) {
		mstatus.val = readl((void *)&mbox->regs->msg_status[chan_num]);
		msg = readl((void *)&mbox->regs->mbox_msg[chan_num]);
		if (mstatus.bits.num_msg == 0)
			break;
	}

	spin_unlock_irqrestore(&mbox->lock, flags);

        return 0;
}

static void spacemit_chan_shutdown(struct mbox_chan *chan)
{
	struct spacemit_mailbox *mbox = chan->con_priv;
	u32 chan_num = chan - mbox->controller.chans;
	u32 msg, j;
	mbox_msg_status_t mstatus;
	unsigned long flags;

	if (chan->cl->tx_prepare != NULL)
		return;

	spin_lock_irqsave(&mbox->lock, flags);

	/* disable new msg irq */
	j = readl((void *)&mbox->regs->mbox_irq[USER1_MBOX_OFFSET].irq_en_clr);
	j |= (1 << (chan_num * 2));
	writel(j, (void *)&mbox->regs->mbox_irq[USER1_MBOX_OFFSET].irq_en_clr);

	/* flush the fifo */
	while (1) {
		mstatus.val = readl((void *)&mbox->regs->msg_status[chan_num]);
		msg = readl((void *)&mbox->regs->mbox_msg[chan_num]);
		if (mstatus.bits.num_msg == 0)
			break;
	}

	/* clear pending */
	j = readl((void *)&mbox->regs->mbox_irq[USER1_MBOX_OFFSET].irq_status_clr);
	j |= (1 << (chan_num * 2));
	writel(j, (void *)&mbox->regs->mbox_irq[USER1_MBOX_OFFSET].irq_status_clr);

	spin_unlock_irqrestore(&mbox->lock, flags);
}

static bool spacemit_chan_last_tx_done(struct mbox_chan *chan)
{
	/* TODO */
	return true;
}

static bool spacemit_chan_peek_data(struct mbox_chan *chan)
{
	struct spacemit_mailbox *mbox = chan->con_priv;
	u32 chan_num = chan - mbox->controller.chans;

	return readl((void *)&mbox->regs->msg_status[chan_num]);
}

static const struct mbox_chan_ops spacemit_chan_ops = {
	.send_data    = spacemit_chan_send_data,
	.startup      = spacemit_chan_startup,
	.shutdown     = spacemit_chan_shutdown,
	.last_tx_done = spacemit_chan_last_tx_done,
	.peek_data    = spacemit_chan_peek_data,
};

static int spacemit_mailbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mbox_chan *chans;
	struct spacemit_mailbox *mbox;
	int i, ret;

	mbox = devm_kzalloc(dev, sizeof(*mbox), GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	chans = devm_kcalloc(dev, SPACEMIT_NUM_CHANNELS, sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;

	for (i = 0; i < SPACEMIT_NUM_CHANNELS; ++i)
		chans[i].con_priv = mbox;

	mbox->regs = (mbox_reg_desc_t *)of_iomap(pdev->dev.of_node, 0);
	if (IS_ERR(mbox->regs)) {
		ret = PTR_ERR(mbox->regs);
		dev_err(dev, "Failed to map MMIO resource: %d\n", ret);
		return -EINVAL;
	}

	mbox->ap_communicate = of_property_read_bool(pdev->dev.of_node, "ap-communicate");

	/* request irq */
	ret = devm_request_irq(dev, platform_get_irq(pdev, 0),
			       spacemit_mbox_irq, 0, dev_name(dev), mbox);
	if (ret) {
		dev_err(dev, "Failed to register IRQ handler: %d\n", ret);
		return ret;
	}

	/* register the mailbox controller */
	mbox->controller.dev = dev;
	mbox->controller.ops = &spacemit_chan_ops;
	mbox->controller.chans = chans;
	mbox->controller.num_chans = SPACEMIT_NUM_CHANNELS;
	mbox->controller.txdone_irq = true;
	mbox->controller.txdone_poll = false;
	mbox->controller.txpoll_period = 5;

	spin_lock_init(&mbox->lock);
	platform_set_drvdata(pdev, mbox);

	ret = mbox_controller_register(&mbox->controller);
	if (ret) {
		dev_err(dev, "Failed to register controller: %d\n", ret);
		return ret;
	}

	return 0;
}

static void spacemit_mailbox_remove(struct platform_device *pdev)
{
	struct spacemit_mailbox *mbox = platform_get_drvdata(pdev);

	mbox_controller_unregister(&mbox->controller);
}

static const struct of_device_id spacemit_mailbox_of_match[] = {
	{ .compatible = "spacemit,k3-mailbox", },
	{},
};
MODULE_DEVICE_TABLE(of, spacemit_mailbox_of_match);

static struct platform_driver spacemit_mailbox_driver = {
	.driver = {
		.name = "spacemit-mailbox",
		.of_match_table = spacemit_mailbox_of_match,
	},
	.probe  = spacemit_mailbox_probe,
	.remove = spacemit_mailbox_remove,
};
module_platform_driver(spacemit_mailbox_driver);

MODULE_DESCRIPTION("spacemit Message Box driver");
MODULE_LICENSE("GPL v2");
