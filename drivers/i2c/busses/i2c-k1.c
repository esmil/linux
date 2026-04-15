// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2025 Troy Mitchell <troymitchell988@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

/* spacemit i2c registers */
#define SPACEMIT_ICR		 0x0		/* Control register */
#define SPACEMIT_ISR		 0x4		/* Status register */
#define SPACEMIT_IDBR		 0xc		/* Data buffer register */
#define SPACEMIT_ILCR		 0x10		/* Load Count Register */
#define SPACEMIT_IWCR		 0x14		/* Wait Count Register */
#define SPACEMIT_IRCR		 0x18		/* Reset cycle counter */
#define SPACEMIT_IBMR		 0x1c		/* Bus monitor register */

/* SPACEMIT_ICR register fields */
#define SPACEMIT_CR_START        BIT(0)		/* start bit */
#define SPACEMIT_CR_STOP         BIT(1)		/* stop bit */
#define SPACEMIT_CR_ACKNAK       BIT(2)		/* send ACK(0) or NAK(1) */
#define SPACEMIT_CR_TB           BIT(3)		/* transfer byte bit */
/* Bits 4-7 are reserved */
#define SPACEMIT_CR_MODE_FAST    BIT(8)		/* bus mode (master operation) */
/* Bit 9 is reserved */
#define SPACEMIT_CR_UR           BIT(10)	/* unit reset */
#define SPACEMIT_CR_RSTREQ	 BIT(11)	/* i2c bus reset request */
/* Bit 12 is reserved */
#define SPACEMIT_CR_SCLE         BIT(13)	/* master clock enable */
#define SPACEMIT_CR_IUE          BIT(14)	/* unit enable */
/* Bits 15-17 are reserved */
#define SPACEMIT_CR_ALDIE        BIT(18)	/* enable arbitration interrupt */
#define SPACEMIT_CR_DTEIE        BIT(19)	/* enable TX interrupts */
#define SPACEMIT_CR_DRFIE        BIT(20)	/* enable RX interrupts */
#define SPACEMIT_CR_GCD          BIT(21)	/* general call disable */
#define SPACEMIT_CR_BEIE         BIT(22)	/* enable bus error ints */
/* Bits 23-24 are reserved */
#define SPACEMIT_CR_MSDIE        BIT(25)	/* master STOP detected int enable */
#define SPACEMIT_CR_MSDE         BIT(26)	/* master STOP detected enable */
#define SPACEMIT_CR_TXDONEIE     BIT(27)	/* transaction done int enable */
#define SPACEMIT_CR_TXEIE        BIT(28)	/* transmit FIFO empty int enable */
#define SPACEMIT_CR_RXHFIE       BIT(29)	/* receive FIFO half-full int enable */
#define SPACEMIT_CR_RXFIE        BIT(30)	/* receive FIFO full int enable */
#define SPACEMIT_CR_RXOVIE       BIT(31)	/* receive FIFO overrun int enable */

#define SPACEMIT_I2C_INT_CTRL_MASK	(SPACEMIT_CR_ALDIE | SPACEMIT_CR_DTEIE | \
					 SPACEMIT_CR_DRFIE | SPACEMIT_CR_BEIE | \
					 SPACEMIT_CR_TXDONEIE | SPACEMIT_CR_TXEIE | \
					 SPACEMIT_CR_RXHFIE | SPACEMIT_CR_RXFIE | \
					 SPACEMIT_CR_RXOVIE | SPACEMIT_CR_MSDIE)

/* SPACEMIT_ISR register fields */
/* Bits 0-13 are reserved */
#define SPACEMIT_SR_ACKNAK       BIT(14)	/* ACK/NACK status */
#define SPACEMIT_SR_UB           BIT(15)	/* unit busy */
#define SPACEMIT_SR_IBB          BIT(16)	/* i2c bus busy */
#define SPACEMIT_SR_EBB          BIT(17)	/* early bus busy */
#define SPACEMIT_SR_ALD          BIT(18)	/* arbitration loss detected */
#define SPACEMIT_SR_ITE          BIT(19)	/* TX buffer empty */
#define SPACEMIT_SR_IRF          BIT(20)	/* RX buffer full */
#define SPACEMIT_SR_GCAD         BIT(21)	/* general call address detected */
#define SPACEMIT_SR_BED          BIT(22)	/* bus error no ACK/NAK */
#define SPACEMIT_SR_SAD          BIT(23)	/* slave address detected */
#define SPACEMIT_SR_SSD          BIT(24)	/* slave stop detected */
/* Bit 25 is reserved */
#define SPACEMIT_SR_MSD          BIT(26)	/* master stop detected */
#define SPACEMIT_SR_TXDONE       BIT(27)	/* transaction done */
#define SPACEMIT_SR_TXE          BIT(28)	/* TX FIFO empty */
#define SPACEMIT_SR_RXHF         BIT(29)	/* RX FIFO half-full */
#define SPACEMIT_SR_RXF          BIT(30)	/* RX FIFO full */
#define SPACEMIT_SR_RXOV         BIT(31)	/* RX FIFO overrun */

#define SPACEMIT_I2C_INT_STATUS_MASK	(SPACEMIT_SR_RXOV | SPACEMIT_SR_RXF | SPACEMIT_SR_RXHF | \
					SPACEMIT_SR_TXE | SPACEMIT_SR_TXDONE | SPACEMIT_SR_MSD | \
					SPACEMIT_SR_SSD | SPACEMIT_SR_SAD | SPACEMIT_SR_BED | \
					SPACEMIT_SR_GCAD | SPACEMIT_SR_IRF | SPACEMIT_SR_ITE | \
					SPACEMIT_SR_ALD)

#define SPACEMIT_RCR_SDA_GLITCH_NOFIX		BIT(7)		/* bypass the SDA glitch fix */
/* the cycles of SCL during bus reset */
#define SPACEMIT_RCR_FIELD_RST_CYC		GENMASK(3, 0)

/* SPACEMIT_IBMR register fields */
#define SPACEMIT_BMR_SDA         BIT(0)		/* SDA line level */
#define SPACEMIT_BMR_SCL         BIT(1)		/* SCL line level */

#define SPACEMIT_LCR_LV_STANDARD_SHIFT		0
#define SPACEMIT_LCR_LV_FAST_SHIFT		9
#define SPACEMIT_LCR_LV_STANDARD_MASK		GENMASK(8, 0)
#define SPACEMIT_LCR_LV_FAST_MASK		GENMASK(17, 9)
#define SPACEMIT_LCR_LV_STANDARD_MAX_VALUE	FIELD_MAX(SPACEMIT_LCR_LV_STANDARD_MASK)
#define SPACEMIT_LCR_LV_FAST_MAX_VALUE		FIELD_MAX(SPACEMIT_LCR_LV_FAST_MASK)

/* i2c bus recover timeout: us */
#define SPACEMIT_I2C_BUS_BUSY_TIMEOUT		100000

#define SPACEMIT_I2C_MAX_STANDARD_MODE_FREQ	100000	/* Hz */
#define SPACEMIT_I2C_MAX_FAST_MODE_FREQ		400000	/* Hz */

#define SPACEMIT_SR_ERR	(SPACEMIT_SR_BED | SPACEMIT_SR_RXOV | SPACEMIT_SR_ALD)

#define SPACEMIT_BUS_RESET_CLK_CNT_MAX		9

/* slave-related registers */
#define SPACEMIT_SAR				0x8	   /* Slave Address Register */

#define SPACEMIT_CR_SADIE			BIT(23)	   /* slave address detected int enable */
#define SPACEMIT_CR_SSDIE			BIT(24)	   /* slave STOP detected int enable */

#define SPACEMIT_SR_RWM				BIT(13)	   /* read/write mode */

#define SPACEMIT_I2C_SLAVE_CRINIT		(SPACEMIT_CR_IUE | SPACEMIT_CR_ALDIE | \
						 SPACEMIT_CR_DTEIE | SPACEMIT_CR_DRFIE | \
						 SPACEMIT_CR_GCD | SPACEMIT_CR_BEIE | \
						 SPACEMIT_CR_SADIE | SPACEMIT_CR_SSDIE)

enum spacemit_i2c_state {
	SPACEMIT_STATE_IDLE,
	SPACEMIT_STATE_START,
	SPACEMIT_STATE_READ,
	SPACEMIT_STATE_WRITE,
};

enum spacemit_i2c_mode {
	SPACEMIT_MODE_STANDARD,
	SPACEMIT_MODE_FAST
};

/* i2c-spacemit driver's main struct */
struct spacemit_i2c_dev {
	struct device *dev;
	struct i2c_adapter adapt;

	struct clk_hw scl_clk_hw;
	struct clk *scl_clk;
	struct clk *func_clk;
	struct clk *bus_clk;
	enum spacemit_i2c_mode mode;

	/* hardware resources */
	void __iomem *base;
	int irq;
	u32 clock_freq;
	struct reset_control *resets;

	struct i2c_msg *msgs;
	u32 msg_num;

	/* index of the current message being processed */
	u32 msg_idx;
	u8 *msg_buf;
	/* the number of unprocessed bytes remaining in the current message  */
	u32 unprocessed;

	enum spacemit_i2c_state state;
	bool read;
	struct completion complete;
	u32 status;

	/* Controls whether to bypass the controller's SDA glitch fix logic. */
	bool sda_glitch_nofix;

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	struct i2c_client *slave;
	bool is_slave_xfer;
#endif
};

static int spacemit_i2c_prepare_enable_clks(struct spacemit_i2c_dev *i2c)
{
	int ret;

	ret = clk_prepare_enable(i2c->func_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(i2c->bus_clk);
	if (ret) {
		clk_disable_unprepare(i2c->func_clk);
		return ret;
	}

	return 0;
}

static void spacemit_i2c_disable_unprepare_clks(struct spacemit_i2c_dev *i2c)
{
	clk_disable_unprepare(i2c->bus_clk);
	clk_disable_unprepare(i2c->func_clk);
}

static int spacemit_i2c_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct spacemit_i2c_dev *i2c = container_of(hw, struct spacemit_i2c_dev, scl_clk_hw);
	u32 lv, lcr, mask, shift, max_lv;
	u32 denom;

	/*
	 * Controller timing (from vendor formula):
	 * - standard mode: SCL = FCLK / (2 * SLV + 0x8)
	 * - fast mode:     SCL = FCLK / (2 * (FLV + 1) + 8)
	 */
	denom = DIV_ROUND_UP(parent_rate, rate);

	if (i2c->mode == SPACEMIT_MODE_STANDARD) {
		mask = SPACEMIT_LCR_LV_STANDARD_MASK;
		shift = SPACEMIT_LCR_LV_STANDARD_SHIFT;
		max_lv = SPACEMIT_LCR_LV_STANDARD_MAX_VALUE;
		/*
		 * SLV >= (denom - 8) / 2
		 * Allow SLV=0 (max SCL = FCLK/8).
		 */
		lv = (denom <= 8) ? 0 : DIV_ROUND_UP(denom - 8, 2);
	} else if (i2c->mode == SPACEMIT_MODE_FAST) {
		mask = SPACEMIT_LCR_LV_FAST_MASK;
		shift = SPACEMIT_LCR_LV_FAST_SHIFT;
		max_lv = SPACEMIT_LCR_LV_FAST_MAX_VALUE;
		/*
		 * FLV >= (denom - 10) / 2
		 * Allow FLV=0 (max SCL = FCLK/10).
		 */
		lv = (denom <= 10) ? 0 : DIV_ROUND_UP(denom - 10, 2);
	} else {
		return -EINVAL;
	}

	if (lv > max_lv) {
		dev_err(i2c->dev, "set scl clock failed: lv 0x%x", lv);
		return -EINVAL;
	}

	lcr = readl(i2c->base + SPACEMIT_ILCR);
	lcr &= ~mask;
	lcr |= lv << shift;
	writel(lcr, i2c->base + SPACEMIT_ILCR);

	return 0;
}

static long spacemit_i2c_clk_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	struct spacemit_i2c_dev *i2c = container_of(hw, struct spacemit_i2c_dev, scl_clk_hw);
	u32 lv, freq, denom;

	denom = DIV_ROUND_UP(*parent_rate, rate);
	if (i2c->mode == SPACEMIT_MODE_STANDARD) {
		lv = (denom <= 8) ? 0 : DIV_ROUND_UP(denom - 8, 2);
		freq = DIV_ROUND_UP(*parent_rate, lv * 2 + 8);
	} else if (i2c->mode == SPACEMIT_MODE_FAST) {
		lv = (denom <= 10) ? 0 : DIV_ROUND_UP(denom - 10, 2);
		freq = DIV_ROUND_UP(*parent_rate, lv * 2 + 10);
	} else {
		return 0;
	}

	return freq;
}

static unsigned long spacemit_i2c_clk_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct spacemit_i2c_dev *i2c = container_of(hw, struct spacemit_i2c_dev, scl_clk_hw);
	u32 lcr, lv = 0;

	lcr = readl(i2c->base + SPACEMIT_ILCR);

	if (i2c->mode == SPACEMIT_MODE_STANDARD) {
		lv = FIELD_GET(SPACEMIT_LCR_LV_STANDARD_MASK, lcr);
		return DIV_ROUND_UP(parent_rate, lv * 2 + 8);
	} else if (i2c->mode == SPACEMIT_MODE_FAST) {
		lv = FIELD_GET(SPACEMIT_LCR_LV_FAST_MASK, lcr);
		return DIV_ROUND_UP(parent_rate, lv * 2 + 10);
	} else {
		return 0;
	}

}

static const struct clk_ops spacemit_i2c_clk_ops = {
	.set_rate = spacemit_i2c_clk_set_rate,
	.round_rate = spacemit_i2c_clk_round_rate,
	.recalc_rate = spacemit_i2c_clk_recalc_rate,
};

static void spacemit_i2c_enable(struct spacemit_i2c_dev *i2c)
{
	u32 val;

	val = readl(i2c->base + SPACEMIT_ICR);
	val |= SPACEMIT_CR_IUE;
	writel(val, i2c->base + SPACEMIT_ICR);
}

static void spacemit_i2c_disable(struct spacemit_i2c_dev *i2c)
{
	u32 val;

	val = readl(i2c->base + SPACEMIT_ICR);
	val &= ~SPACEMIT_CR_IUE;
	writel(val, i2c->base + SPACEMIT_ICR);
}

static struct clk *spacemit_i2c_register_scl_clk(struct spacemit_i2c_dev *i2c,
						 struct clk *parent)
{
	struct clk_init_data init = {};
	char name[32];

	snprintf(name, sizeof(name), "%s_scl_clk", dev_name(i2c->dev));

	init.name = name;
	init.ops = &spacemit_i2c_clk_ops;
	init.parent_data = (struct clk_parent_data[]) {
		{ .fw_name = "func" },
	};
	init.num_parents = 1;

	i2c->scl_clk_hw.init = &init;

	return devm_clk_register(i2c->dev, &i2c->scl_clk_hw);
}

static void spacemit_i2c_reset(struct spacemit_i2c_dev *i2c)
{
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	u32 slave_value;
#endif
	writel(SPACEMIT_CR_UR, i2c->base + SPACEMIT_ICR);
	udelay(5);
	writel(0, i2c->base + SPACEMIT_ICR);

	writel(0x0000142A, i2c->base + SPACEMIT_IWCR);

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (i2c->slave) {
		slave_value = SPACEMIT_I2C_SLAVE_CRINIT;
		if (i2c->mode == SPACEMIT_MODE_FAST)
			slave_value |= SPACEMIT_CR_MODE_FAST;
		writel(slave_value, i2c->base + SPACEMIT_ICR);
	}
#endif
}

static int spacemit_i2c_handle_err(struct spacemit_i2c_dev *i2c)
{
	dev_dbg(i2c->dev, "i2c error status: 0x%08x\n", i2c->status);

	/* Arbitration Loss Detected */
	if (i2c->status & SPACEMIT_SR_ALD) {
		spacemit_i2c_reset(i2c);
		return -EAGAIN;
	}

	/* Bus Error No ACK/NAK */
	if (i2c->status & SPACEMIT_SR_BED)
		spacemit_i2c_reset(i2c);

	return i2c->status & SPACEMIT_SR_ACKNAK ? -ENXIO : -EIO;
}

static void spacemit_i2c_conditionally_reset_bus(struct spacemit_i2c_dev *i2c)
{
	u32 status;
	u8 clk_cnt;

	/* if bus is locked, reset unit. 0: locked */
	status = readl(i2c->base + SPACEMIT_IBMR);
	if ((status & SPACEMIT_BMR_SDA) && (status & SPACEMIT_BMR_SCL))
		return;

	spacemit_i2c_reset(i2c);
	usleep_range(10, 20);

	for (clk_cnt = 0; clk_cnt < SPACEMIT_BUS_RESET_CLK_CNT_MAX; clk_cnt++) {
		status = readl(i2c->base + SPACEMIT_IBMR);
		if (status & SPACEMIT_BMR_SDA)
			return;

		/* There's nothing left to save here, we are about to exit */
		writel(FIELD_PREP(SPACEMIT_RCR_FIELD_RST_CYC, 1),
		       i2c->base + SPACEMIT_IRCR);
		writel(SPACEMIT_CR_RSTREQ, i2c->base + SPACEMIT_ICR);
		usleep_range(20, 30);
	}

	/* check sda again here */
	status = readl(i2c->base + SPACEMIT_IBMR);
	if (!(status & SPACEMIT_BMR_SDA))
		dev_warn_ratelimited(i2c->dev, "unit reset failed\n");
}

static int spacemit_i2c_wait_bus_idle(struct spacemit_i2c_dev *i2c)
{
	int ret;
	u32 val;

	val = readl(i2c->base + SPACEMIT_ISR);
	if (!(val & (SPACEMIT_SR_UB | SPACEMIT_SR_IBB)))
		return 0;

	ret = readl_poll_timeout(i2c->base + SPACEMIT_ISR,
				 val, !(val & (SPACEMIT_SR_UB | SPACEMIT_SR_IBB)),
				 1500, SPACEMIT_I2C_BUS_BUSY_TIMEOUT);
	if (ret)
		spacemit_i2c_reset(i2c);

	return ret;
}

static void spacemit_i2c_check_bus_release(struct spacemit_i2c_dev *i2c)
{
	/* in case bus is not released after transfer completes */
	if (readl(i2c->base + SPACEMIT_ISR) & SPACEMIT_SR_EBB) {
		spacemit_i2c_conditionally_reset_bus(i2c);
		usleep_range(90, 150);
	}
}

static inline void
spacemit_i2c_clear_int_status(struct spacemit_i2c_dev *i2c, u32 mask)
{
	writel(mask & SPACEMIT_I2C_INT_STATUS_MASK, i2c->base + SPACEMIT_ISR);
}

static void spacemit_i2c_init(struct spacemit_i2c_dev *i2c)
{
	u32 val;

	/*
	 * Unmask interrupt bits for all xfer mode:
	 * bus error, arbitration loss detected.
	 * For transaction complete signal, we use master stop
	 * interrupt, so we don't need to unmask SPACEMIT_CR_TXDONEIE.
	 */
	val = SPACEMIT_CR_BEIE | SPACEMIT_CR_ALDIE;

	/*
	 * Unmask interrupt bits for interrupt xfer mode:
	 * When IDBR receives a byte, an interrupt is triggered.
	 *
	 * For the tx empty interrupt, it will be enabled in the
	 * i2c_start function.
	 * Otherwise, it will cause an erroneous empty interrupt before i2c_start.
	 */
	val |= SPACEMIT_CR_DRFIE;

	if (i2c->mode == SPACEMIT_MODE_FAST)
		val |= SPACEMIT_CR_MODE_FAST;

	/* disable response to general call */
	val |= SPACEMIT_CR_GCD;

	/* enable SCL clock output */
	val |= SPACEMIT_CR_SCLE;

	/* enable master stop detected */
	val |= SPACEMIT_CR_MSDE | SPACEMIT_CR_MSDIE;

	writel(val, i2c->base + SPACEMIT_ICR);

	/*
	 * The K1 I2C controller has an SDA glitch fix which can suppress short
	 * pulses on SDA, but it may also introduce a small delay on restart
	 * (repeated-start) signals on some systems.
	 */
	val = readl(i2c->base + SPACEMIT_IRCR);
	if (i2c->sda_glitch_nofix)
		val |= SPACEMIT_RCR_SDA_GLITCH_NOFIX;
	writel(val, i2c->base + SPACEMIT_IRCR);

	spacemit_i2c_clear_int_status(i2c, SPACEMIT_I2C_INT_STATUS_MASK);
}

static void spacemit_i2c_start(struct spacemit_i2c_dev *i2c)
{
	u32 target_addr_rw, val;
	struct i2c_msg *cur_msg = i2c->msgs + i2c->msg_idx;

	i2c->read = !!(cur_msg->flags & I2C_M_RD);

	i2c->state = SPACEMIT_STATE_START;

	target_addr_rw = (cur_msg->addr & 0x7f) << 1;
	if (cur_msg->flags & I2C_M_RD)
		target_addr_rw |= 1;

	writel(target_addr_rw, i2c->base + SPACEMIT_IDBR);

	/* send start pulse */
	val = readl(i2c->base + SPACEMIT_ICR);
	val &= ~SPACEMIT_CR_STOP;
	val |= SPACEMIT_CR_START | SPACEMIT_CR_TB | SPACEMIT_CR_DTEIE;
	writel(val, i2c->base + SPACEMIT_ICR);
}

static int spacemit_i2c_xfer_msg(struct spacemit_i2c_dev *i2c)
{
	unsigned long time_left;
	struct i2c_msg *msg;

	for (i2c->msg_idx = 0; i2c->msg_idx < i2c->msg_num; i2c->msg_idx++) {
		msg = &i2c->msgs[i2c->msg_idx];
		i2c->msg_buf = msg->buf;
		i2c->unprocessed = msg->len;
		i2c->status = 0;

		reinit_completion(&i2c->complete);

		spacemit_i2c_start(i2c);

		time_left = wait_for_completion_timeout(&i2c->complete,
							i2c->adapt.timeout);
		if (!time_left) {
			dev_err(i2c->dev, "msg completion timeout\n");
			spacemit_i2c_conditionally_reset_bus(i2c);
			spacemit_i2c_reset(i2c);
			return -ETIMEDOUT;
		}

		if (i2c->status & SPACEMIT_SR_ERR)
			return spacemit_i2c_handle_err(i2c);
	}

	return 0;
}

static bool spacemit_i2c_is_last_msg(struct spacemit_i2c_dev *i2c)
{
	if (i2c->msg_idx != i2c->msg_num - 1)
		return false;

	if (i2c->read)
		return i2c->unprocessed == 1;

	return !i2c->unprocessed;
}

static void spacemit_i2c_handle_write(struct spacemit_i2c_dev *i2c)
{
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	u8 slave_value;
	if (i2c->is_slave_xfer) {
		/* to confirm its not NACK */
		if (i2c->status & SPACEMIT_SR_ACKNAK)
			return;

		i2c_slave_event(i2c->slave, I2C_SLAVE_READ_PROCESSED, &slave_value);
		writel(slave_value, i2c->base + SPACEMIT_IDBR);
		return;
	}
#endif

	/* if transfer completes, SPACEMIT_ISR will handle it */
	if (i2c->status & SPACEMIT_SR_MSD)
		return;

	if (i2c->unprocessed) {
		writel(*i2c->msg_buf++, i2c->base + SPACEMIT_IDBR);
		i2c->unprocessed--;
		return;
	}

	/* SPACEMIT_STATE_IDLE avoids trigger next byte */
	i2c->state = SPACEMIT_STATE_IDLE;
	complete(&i2c->complete);
}

static void spacemit_i2c_handle_read(struct spacemit_i2c_dev *i2c)
{
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	u8 slave_value;
	if (i2c->is_slave_xfer) {
		/* to confirm that it's triggered by IRF */
		if (i2c->status & SPACEMIT_SR_IRF) {
			slave_value = readl(i2c->base + SPACEMIT_IDBR);
			i2c_slave_event(i2c->slave, I2C_SLAVE_WRITE_RECEIVED, &slave_value);
		}
		return;
	}
#endif

	if (i2c->unprocessed) {
		*i2c->msg_buf++ = readl(i2c->base + SPACEMIT_IDBR);
		i2c->unprocessed--;
	}

	/* if transfer completes, SPACEMIT_ISR will handle it */
	if (i2c->status & (SPACEMIT_SR_MSD | SPACEMIT_SR_ACKNAK))
		return;

	/* it has to append stop bit in icr that read last byte */
	if (i2c->unprocessed)
		return;

	/* SPACEMIT_STATE_IDLE avoids trigger next byte */
	i2c->state = SPACEMIT_STATE_IDLE;
	complete(&i2c->complete);
}

static void spacemit_i2c_handle_start(struct spacemit_i2c_dev *i2c)
{
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	u8 value;
	if (i2c->is_slave_xfer) {
		if (i2c->status & SPACEMIT_SR_RWM) {
			i2c->state = SPACEMIT_STATE_WRITE;
			i2c_slave_event(i2c->slave, I2C_SLAVE_READ_REQUESTED, &value);
			writel(value, i2c->base + SPACEMIT_IDBR);
		} else {
			i2c->state = SPACEMIT_STATE_READ;
			i2c_slave_event(i2c->slave, I2C_SLAVE_WRITE_REQUESTED, &value);
		}
		return;
	}
#endif
	i2c->state = i2c->read ? SPACEMIT_STATE_READ : SPACEMIT_STATE_WRITE;
	if (i2c->state == SPACEMIT_STATE_WRITE)
		spacemit_i2c_handle_write(i2c);
}

static void spacemit_i2c_err_check(struct spacemit_i2c_dev *i2c)
{
	u32 val;

	/*
	 * Send transaction complete signal:
	 * error happens, detect master stop
	 */
	if (!(i2c->status & (SPACEMIT_SR_ERR | SPACEMIT_SR_MSD)))
		return;

	/*
	 * Here the transaction is already done, we don't need any
	 * other interrupt signals from now, in case any interrupt
	 * happens before spacemit_i2c_xfer to disable irq and i2c unit,
	 * we mask all the interrupt signals and clear the interrupt
	 * status.
	 */
	val = readl(i2c->base + SPACEMIT_ICR);
	val &= ~SPACEMIT_I2C_INT_CTRL_MASK;
	writel(val, i2c->base + SPACEMIT_ICR);

	spacemit_i2c_clear_int_status(i2c, SPACEMIT_I2C_INT_STATUS_MASK);

	i2c->state = SPACEMIT_STATE_IDLE;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	i2c->is_slave_xfer = false;
#endif
	complete(&i2c->complete);
}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
static int spacemit_i2c_reg_slave(struct i2c_client *slave)
{
	struct spacemit_i2c_dev *i2c = i2c_get_adapdata(slave->adapter);
	u32 val;
	int ret;

	if (i2c->slave)
		return -EBUSY;

	if (slave->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	ret = pm_runtime_resume_and_get(i2c->dev);
	if (ret < 0)
		return ret;

	i2c->slave = slave;

	writel(slave->addr, i2c->base + SPACEMIT_SAR);

	val = SPACEMIT_I2C_SLAVE_CRINIT;
	if (i2c->mode == SPACEMIT_MODE_FAST)
		val |= SPACEMIT_CR_MODE_FAST;

	writel(val, i2c->base + SPACEMIT_ICR);

	return 0;
}

static int spacemit_i2c_unreg_slave(struct i2c_client *slave)
{
	struct spacemit_i2c_dev *i2c = i2c_get_adapdata(slave->adapter);
	int ret = 0;

	if (!i2c->slave) {
		dev_err(i2c->dev, "no slave registered\n");
		ret = -EINVAL;
	}

	writel(0, i2c->base + SPACEMIT_ICR);
	writel(0, i2c->base + SPACEMIT_SAR);

	i2c->slave = NULL;
	pm_runtime_put_autosuspend(i2c->dev);

	return ret;
}
#endif

static irqreturn_t spacemit_i2c_irq_handler(int irq, void *devid)
{
	struct spacemit_i2c_dev *i2c = devid;
	u32 status, val;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	u8 slave_value;
#endif

	status = readl(i2c->base + SPACEMIT_ISR);
	if (!status)
		return IRQ_HANDLED;

	i2c->status = status;

	spacemit_i2c_clear_int_status(i2c, status);

	if (i2c->status & SPACEMIT_SR_ERR)
		goto err_out;

	val = readl(i2c->base + SPACEMIT_ICR);
	val &= ~(SPACEMIT_CR_TB | SPACEMIT_CR_ACKNAK | SPACEMIT_CR_STOP | SPACEMIT_CR_START);

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (i2c->slave && (status & SPACEMIT_SR_SAD)) {
		/* slave address detected */
		i2c->is_slave_xfer = true;
		i2c->state = SPACEMIT_STATE_START;
	}
#endif

	switch (i2c->state) {
	case SPACEMIT_STATE_START:
		spacemit_i2c_handle_start(i2c);
		break;
	case SPACEMIT_STATE_READ:
		spacemit_i2c_handle_read(i2c);
		break;
	case SPACEMIT_STATE_WRITE:
		spacemit_i2c_handle_write(i2c);
		break;
	default:
		break;
	}

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (i2c->slave && (status & SPACEMIT_SR_SSD)) {
		i2c_slave_event(i2c->slave, I2C_SLAVE_STOP, &slave_value);
		i2c->state = SPACEMIT_STATE_IDLE;
		i2c->is_slave_xfer = false;
	}
#endif

	if (i2c->state != SPACEMIT_STATE_IDLE) {
		val |= SPACEMIT_CR_TB | SPACEMIT_CR_ALDIE;

#if IS_ENABLED(CONFIG_I2C_SLAVE)
		/* Do not trigger master STOP generation if we are in a slave transfer */
		if (!i2c->is_slave_xfer && spacemit_i2c_is_last_msg(i2c)) {
#else
		if (spacemit_i2c_is_last_msg(i2c)) {
#endif
			/* trigger next byte with stop */
			val |= SPACEMIT_CR_STOP;

			if (i2c->read)
				val |= SPACEMIT_CR_ACKNAK;
		}
		writel(val, i2c->base + SPACEMIT_ICR);
	}

err_out:
	spacemit_i2c_err_check(i2c);
	return IRQ_HANDLED;
}

static void spacemit_i2c_calc_timeout(struct spacemit_i2c_dev *i2c)
{
	unsigned long timeout;
	int idx = 0, cnt = 0;

	for (; idx < i2c->msg_num; idx++)
		cnt += (i2c->msgs + idx)->len + 1;

	/*
	 * Multiply by 9 because each byte in I2C transmission requires
	 * 9 clock cycles: 8 bits of data plus 1 ACK/NACK bit.
	 */
	timeout = cnt * 9 * USEC_PER_SEC / i2c->clock_freq;

	i2c->adapt.timeout = usecs_to_jiffies(timeout + USEC_PER_SEC / 10) / i2c->msg_num;
}

static int spacemit_i2c_xfer(struct i2c_adapter *adapt, struct i2c_msg *msgs, int num)
{
	struct spacemit_i2c_dev *i2c = i2c_get_adapdata(adapt);
	bool clk_directly = false;
	int ret;

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	if (i2c->slave) {
		dev_err(i2c->dev, "working as slave mode here\n");
		return -EBUSY;
	}
#endif

	ret = pm_runtime_get_sync(i2c->dev);
	if (ret < 0) {
		/*
		 * During system suspend_late to system resume_early stage,
		 * if PM runtime is suspended, we will get -EACCES return
		 * value, so we need to enable clock directly, and disable after
		 * i2c transfer is finished. During this stage, pmic onkey ISR
		 * that invoked in an irq thread may use i2c interface if we have
		 * onkey press action.
		 */
		if (ret == -EACCES) {
			ret = clk_prepare_enable(i2c->func_clk);
			if (ret) {
				dev_err(i2c->dev,
					"failed to enable func clock directly: %d\n",
					ret);
				pm_runtime_put_noidle(i2c->dev);
				return ret;
			}
			ret = clk_prepare_enable(i2c->bus_clk);
			if (ret) {
				dev_err(i2c->dev,
					"failed to enable bus clock directly: %d\n",
					ret);
				clk_disable_unprepare(i2c->func_clk);
				pm_runtime_put_noidle(i2c->dev);
				return ret;
			}
			clk_directly = true;
		} else {
			dev_err(i2c->dev, "pm runtime sync error: %d\n", ret);
			pm_runtime_put_noidle(i2c->dev);
			return ret;
		}
	}

	i2c->msgs = msgs;
	i2c->msg_num = num;

	spacemit_i2c_calc_timeout(i2c);

	spacemit_i2c_init(i2c);

	spacemit_i2c_enable(i2c);

	ret = spacemit_i2c_wait_bus_idle(i2c);
	if (!ret) {
		ret = spacemit_i2c_xfer_msg(i2c);
		if (ret < 0)
			dev_dbg(i2c->dev, "i2c transfer error: %d\n", ret);
	} else {
		spacemit_i2c_check_bus_release(i2c);
	}

	spacemit_i2c_disable(i2c);

	if (ret == -ETIMEDOUT || ret == -EAGAIN)
		dev_err(i2c->dev, "i2c transfer failed, ret %d err 0x%lx\n",
			  ret, i2c->status & SPACEMIT_SR_ERR);

	if (clk_directly) {
		/* If clocks are enabled directly, here disable them */
		clk_disable_unprepare(i2c->bus_clk);
		clk_disable_unprepare(i2c->func_clk);
	}

	pm_runtime_mark_last_busy(i2c->dev);
	pm_runtime_put_autosuspend(i2c->dev);

	return ret < 0 ? ret : num;
}

static u32 spacemit_i2c_func(struct i2c_adapter *adap)
{
	u32 flags = I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	flags |= I2C_FUNC_SLAVE;
#endif
	return flags;
}

static const struct i2c_algorithm spacemit_i2c_algo = {
	.xfer = spacemit_i2c_xfer,
	.functionality = spacemit_i2c_func,

#if IS_ENABLED(CONFIG_I2C_SLAVE)
	.reg_slave = spacemit_i2c_reg_slave,
	.unreg_slave = spacemit_i2c_unreg_slave,
#endif
};

static int spacemit_i2c_probe(struct platform_device *pdev)
{
	struct clk *clk;
	struct device *dev = &pdev->dev;
	struct device_node *of_node = pdev->dev.of_node;
	struct spacemit_i2c_dev *i2c;
	int ret;

	i2c = devm_kzalloc(dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	i2c->dev = &pdev->dev;

	ret = of_property_read_u32(of_node, "clock-frequency", &i2c->clock_freq);
	if (ret && ret != -EINVAL)
		dev_warn(dev, "failed to read clock-frequency property: %d\n", ret);

	i2c->sda_glitch_nofix = of_property_read_bool(of_node, "spacemit,sda-glitch-nofix");

	i2c->dev = &pdev->dev;
	/* For now, this driver doesn't support high-speed. */
	if (i2c->clock_freq > SPACEMIT_I2C_MAX_STANDARD_MODE_FREQ &&
	    i2c->clock_freq <= SPACEMIT_I2C_MAX_FAST_MODE_FREQ) {
		i2c->mode = SPACEMIT_MODE_FAST;
	} else if (i2c->clock_freq && i2c->clock_freq <= SPACEMIT_I2C_MAX_STANDARD_MODE_FREQ) {
		i2c->mode = SPACEMIT_MODE_STANDARD;
	} else {
		dev_warn(i2c->dev, "invalid clock-frequency, fallback to fast mode");
		i2c->mode = SPACEMIT_MODE_FAST;
		i2c->clock_freq = SPACEMIT_I2C_MAX_FAST_MODE_FREQ;
	}

	i2c->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(i2c->base))
		return dev_err_probe(dev, PTR_ERR(i2c->base), "failed to do ioremap");

	i2c->irq = platform_get_irq(pdev, 0);
	if (i2c->irq < 0)
		return dev_err_probe(dev, i2c->irq, "failed to get irq resource");

	ret = devm_request_irq(i2c->dev, i2c->irq, spacemit_i2c_irq_handler,
			       IRQF_NO_SUSPEND | IRQF_ONESHOT, dev_name(i2c->dev), i2c);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request irq");

	clk = devm_clk_get(dev, "func");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get func clock");

	i2c->func_clk = clk;

	i2c->scl_clk = spacemit_i2c_register_scl_clk(i2c, clk);
	if (IS_ERR(i2c->scl_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(i2c->scl_clk),
				     "failed to register scl clock\n");

	clk = devm_clk_get(dev, "bus");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get bus clock");

	i2c->bus_clk = clk;

	ret = spacemit_i2c_prepare_enable_clks(i2c);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to prepare and enable controller clocks");

	/* Get reset control */
	i2c->resets = devm_reset_control_get_optional(dev, NULL);
	if (IS_ERR(i2c->resets)) {
		spacemit_i2c_disable_unprepare_clks(i2c);
		return dev_err_probe(dev, PTR_ERR(i2c->resets), "failed to get reset control");
	}

	/* Reset the I2C controller */
	if (i2c->resets) {
		reset_control_assert(i2c->resets);
		udelay(200);
		reset_control_deassert(i2c->resets);
	}

	ret = clk_set_rate(i2c->scl_clk, i2c->clock_freq);
	if (ret) {
		spacemit_i2c_disable_unprepare_clks(i2c);
		return dev_err_probe(&pdev->dev, ret, "failed to set rate for SCL clock");
	}

	spacemit_i2c_reset(i2c);

	i2c_set_adapdata(&i2c->adapt, i2c);
	i2c->adapt.owner = THIS_MODULE;
	i2c->adapt.algo = &spacemit_i2c_algo;
	i2c->adapt.dev.parent = i2c->dev;
	i2c->adapt.nr = pdev->id;

	i2c->adapt.dev.of_node = of_node;

	strscpy(i2c->adapt.name, "spacemit-i2c-adapter", sizeof(i2c->adapt.name));

	init_completion(&i2c->complete);

	platform_set_drvdata(pdev, i2c);

	spacemit_i2c_disable_unprepare_clks(i2c);

	pm_runtime_set_autosuspend_delay(dev, MSEC_PER_SEC);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_suspended(dev);
	pm_suspend_ignore_children(dev, 1);
	pm_runtime_enable(dev);

	ret = i2c_add_numbered_adapter(&i2c->adapt);
	if (ret) {
		pm_runtime_disable(dev);
		return dev_err_probe(&pdev->dev, ret, "failed to add i2c adapter");
	}

	return 0;
}

static void spacemit_i2c_remove(struct platform_device *pdev)
{
	struct spacemit_i2c_dev *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adapt);

	pm_runtime_force_suspend(i2c->dev);

	if (i2c->resets)
		reset_control_assert(i2c->resets);
}

static const struct of_device_id spacemit_i2c_of_match[] = {
	{ .compatible = "spacemit,k1-i2c", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_i2c_of_match);

static struct platform_driver spacemit_i2c_driver = {
	.probe = spacemit_i2c_probe,
	.remove = spacemit_i2c_remove,
	.driver = {
		.name = "i2c-k1",
		.of_match_table = spacemit_i2c_of_match,
	},
};
module_platform_driver(spacemit_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C bus driver for SpacemiT K1 SoC");
