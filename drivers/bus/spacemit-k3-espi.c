// SPDX-License-Identifier: GPL-2.0-only
/*
 * SpacemiT K3 eSPI controller driver
 *
 * This driver owns controller-level initialization and recovery so child
 * devices such as cros_ec_espi can treat the peripheral shared memory window
 * as a transport and ask the parent controller to recover the link when the
 * EC reboots underneath Linux.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sizes.h>
#include <linux/spacemit-k3-espi.h>
#include <linux/wordpart.h>

#define ESPI_DN_TXHDR			0x00
#define ESPI_DN_TXDATA_PORT		0x0c
#define ESPI_MASTER_CAP			0x2c
#define ESPI_GLOBAL_CONTROL_0		0x30
#define ESPI_GLOBAL_CONTROL_1		0x34
#define ESPI_PR_BASE_ADDR_MEM0		0x38
#define ESPI_PR_BASE_ADDR_MEM1		0x3c
#define ESPI_SLAVE0_CONFIG		0x68
#define ESPI_SLAVE0_INT_EN		0x6c
#define ESPI_SLAVE0_INT_STS		0x70

#define ESPI_DNCMD_TYPE_MASK		GENMASK(2, 0)
#define ESPI_DNCMD_EN			BIT(3)
#define ESPI_DNCMD_SLAVE_SEL_MASK	GENMASK(5, 4)

#define ESPI_DNCMD_SET_CONFIGURATION	0x00
#define ESPI_DNCMD_GET_CONFIGURATION	0x01
#define ESPI_DNCMD_IN_BAND_RESET	0x02

#define ESPI_FLASH_REQ_INT		BIT(31)
#define ESPI_RXOOB_INT			BIT(30)
#define ESPI_RXMSG_INT			BIT(29)
#define ESPI_DNCMD_INT			BIT(28)
#define ESPI_RXVW_GRP3_INT		BIT(27)
#define ESPI_RXVW_GRP2_INT		BIT(26)
#define ESPI_RXVW_GRP1_INT		BIT(25)
#define ESPI_RXVW_GRP0_INT		BIT(24)
#define ESPI_PROTOCOL_ERR_INT		BIT(15)
#define ESPI_RXFLASH_OFLOW_INT		BIT(14)
#define ESPI_RXMSG_OFLOW_INT		BIT(13)
#define ESPI_RXOOB_OFLOW_INT		BIT(12)
#define ESPI_ILLEGAL_LEN_INT		BIT(11)
#define ESPI_ILLEGAL_TAG_INT		BIT(10)
#define ESPI_UNSUCSS_CPL_INT		BIT(9)
#define ESPI_INVALID_CT_RSP_INT		BIT(8)
#define ESPI_UNKNOWN_RSP_INT		BIT(7)
#define ESPI_NON_FATAL_INT		BIT(6)
#define ESPI_FATAL_ERR_INT		BIT(5)
#define ESPI_NO_RSP_INT			BIT(4)
#define ESPI_CRC_ERR_INT		BIT(2)
#define ESPI_WAIT_TIMEOUT_INT		BIT(1)
#define ESPI_BUS_ERR_INT		BIT(0)

#define ESPI_CALLBACK_INT		(ESPI_FLASH_REQ_INT | \
					 ESPI_RXOOB_INT | \
					 ESPI_RXMSG_INT | \
					 ESPI_DNCMD_INT | \
					 ESPI_RXVW_GRP3_INT | \
					 ESPI_RXVW_GRP2_INT | \
					 ESPI_RXVW_GRP1_INT | \
					 ESPI_RXVW_GRP0_INT)
#define ESPI_PROTOCOL_INT		(ESPI_PROTOCOL_ERR_INT | \
					 ESPI_RXFLASH_OFLOW_INT | \
					 ESPI_RXMSG_OFLOW_INT | \
					 ESPI_RXOOB_OFLOW_INT | \
					 ESPI_ILLEGAL_LEN_INT | \
					 ESPI_ILLEGAL_TAG_INT | \
					 ESPI_UNSUCSS_CPL_INT | \
					 ESPI_INVALID_CT_RSP_INT | \
					 ESPI_UNKNOWN_RSP_INT | \
					 ESPI_CRC_ERR_INT | \
					 ESPI_WAIT_TIMEOUT_INT | \
					 ESPI_BUS_ERR_INT)
#define ESPI_RESPONSE_INT		(ESPI_NON_FATAL_INT | \
					 ESPI_FATAL_ERR_INT | \
					 ESPI_NO_RSP_INT)
#define ESPI_ALL_INT			(ESPI_CALLBACK_INT | \
					 ESPI_PROTOCOL_INT | \
					 ESPI_RESPONSE_INT)

#define ESPI_MST_STOP_EN		BIT(3)
#define ESPI_CONTROL_WATCHDOG_EN	BIT(0)
#define ESPI_CONTROL_WATCHDOG_CNT_MASK	GENMASK(23, 8)

#define ESPI_CRC_CHECK_EN		BIT(31)
#define ESPI_ALERT_MODE_SEL		BIT(30)
#define ESPI_IO_MODE_SEL_MASK		GENMASK(29, 28)
#define ESPI_CLK_FREQ_SEL_MASK		GENMASK(27, 25)
#define ESPI_PR_CHANNEL_ENABLE		BIT(3)
#define ESPI_VW_CHANNEL_ENABLE		BIT(2)
#define ESPI_OOB_CHANNEL_ENABLE		BIT(1)
#define ESPI_FLASH_CHANNEL_ENABLE	BIT(0)

#define ESPI_ESPI_RSTN			BIT(5)
#define ESPI_SW_RST			BIT(0)

#define ESPI_SLAVE_GEN_CFG		0x08
#define ESPI_SLAVE_PERI_CFG		0x10
#define ESPI_SLAVE_VWIRE_CFG		0x20
#define ESPI_SLAVE_OOB_CFG		0x30
#define ESPI_SLAVE_FLASH_CFG		0x40

#define ESPI_GEN_CRC_ENABLE		BIT(31)
#define ESPI_GEN_ALERT_MODE_PIN		BIT(28)
#define ESPI_GEN_IO_MODE_SUP_MASK	GENMASK(25, 24)
#define ESPI_GEN_OPEN_DRAIN_ALERT_SEL	BIT(23)
#define ESPI_GEN_OPEN_DRAIN_ALERT_SUP	BIT(19)
#define ESPI_GEN_OP_FREQ_SUP_MASK	GENMASK(18, 16)
#define ESPI_GEN_OP_FREQ_SEL_MASK	GENMASK(22, 20)
#define ESPI_GEN_IO_MODE_SEL_MASK	GENMASK(27, 26)
#define ESPI_GEN_FLASH_CHAN_SUP		BIT(3)
#define ESPI_GEN_OOB_CHAN_SUP		BIT(2)
#define ESPI_GEN_VWIRE_CHAN_SUP		BIT(1)
#define ESPI_GEN_PERI_CHAN_SUP		BIT(0)

#define ESPI_GEN_IO_MODE_SINGLE		0x0
#define ESPI_GEN_IO_MODE_DUAL		0x1
#define ESPI_GEN_IO_MODE_QUAD		0x2

#define ESPI_GEN_OP_FREQ_20MHZ		0x0
#define ESPI_GEN_OP_FREQ_25MHZ		0x1
#define ESPI_GEN_OP_FREQ_33MHZ		0x2
#define ESPI_GEN_OP_FREQ_50MHZ		0x3
#define ESPI_GEN_OP_FREQ_66MHZ		0x4

#define ESPI_ENABLE_PR_CHANNEL		0x1115
#define ESPI_ENABLE_VW_CHANNEL		0x70701
#define ESPI_ENABLE_OOB_CHANNEL		0x111
#define ESPI_ENABLE_FLASH_CHANNEL	0x11125

#define ESPI_CFG_TIMEOUT_US		50000
#define ESPI_TX_IDLE_TIMEOUT_US		1000

#define SPACEMIT_ESPI_DEFAULT_FREQ_MHZ	20
#define SPACEMIT_ESPI_DEFAULT_WDT	0xffff

struct spacemit_espi {
	struct device *dev;
	struct clk_bulk_data *clks;
	int num_clks;
	struct clk *sclk;
	struct reset_control *resets;
	bool clocks_enabled;
	void __iomem *regs;
	u32 pr_mem_base0;
	u32 pr_mem_base1;
	u32 op_freq_mhz;
	u32 watchdog_timeout;
	u32 io_mode;
	bool alert_pin_mode;
	bool alert_open_drain;
	bool crc_enable;
	bool enable_peri;
	bool enable_vwire;
	bool enable_oob;
	bool enable_flash;
	bool auto_gating;
	bool initialized;
	bool slave_ready;
	struct mutex lock;
};

static bool spacemit_espi_property_enabled(struct device_node *np,
					   const char *name, bool default_value)
{
	if (of_find_property(np, name, NULL))
		return of_property_read_bool(np, name);

	return default_value;
}

static u32 spacemit_espi_txhdr_reg(unsigned int index)
{
	return ESPI_DN_TXHDR + ((index + 1) & ~0x3);
}

static u32 spacemit_espi_txhdr_shift(unsigned int index)
{
	return ((index + 1) & 0x3) * 8;
}

static void spacemit_espi_write_hdr(struct spacemit_espi *espi, unsigned int index,
				    u8 value)
{
	u32 reg = spacemit_espi_txhdr_reg(index);
	u32 shift = spacemit_espi_txhdr_shift(index);
	u32 data;

	data = readl(espi->regs + reg);
	data &= ~(GENMASK(7, 0) << shift);
	data |= (u32)value << shift;
	writel(data, espi->regs + reg);
}

static u8 spacemit_espi_read_hdr(struct spacemit_espi *espi, unsigned int index)
{
	u32 reg = spacemit_espi_txhdr_reg(index);
	u32 shift = spacemit_espi_txhdr_shift(index);

	return (readl(espi->regs + reg) >> shift) & 0xff;
}

static void spacemit_espi_trigger_cmd(struct spacemit_espi *espi, u32 cmd_type)
{
	u32 val;

	val = readl(espi->regs + ESPI_DN_TXHDR);
	val &= ~(ESPI_DNCMD_TYPE_MASK | ESPI_DNCMD_EN |
		 ESPI_DNCMD_SLAVE_SEL_MASK);
	val |= FIELD_PREP(ESPI_DNCMD_TYPE_MASK, cmd_type);
	val |= ESPI_DNCMD_EN;
	writel(val, espi->regs + ESPI_DN_TXHDR);
}

static int spacemit_espi_wait_tx_idle(struct spacemit_espi *espi)
{
	unsigned int timeout = ESPI_TX_IDLE_TIMEOUT_US;

	do {
		if (!(readl(espi->regs + ESPI_DN_TXHDR) & ESPI_DNCMD_EN))
			return 0;
		udelay(1);
	} while (--timeout);

	return -EBUSY;
}

static int spacemit_espi_poll_status(struct spacemit_espi *espi, u32 expected)
{
	unsigned int timeout = ESPI_CFG_TIMEOUT_US / 10;
	u32 status;

	writel(expected, espi->regs + ESPI_SLAVE0_INT_STS);

	do {
		status = readl(espi->regs + ESPI_SLAVE0_INT_STS);
		if (!status) {
			udelay(10);
			continue;
		}

		writel(status, espi->regs + ESPI_SLAVE0_INT_STS);

		if (status & expected)
			return 0;

		if (status & (ESPI_PROTOCOL_INT | ESPI_RESPONSE_INT))
			return -EIO;

		udelay(10);
	} while (--timeout);

	return -ETIMEDOUT;
}

static int spacemit_espi_cmd_get_config(struct spacemit_espi *espi, u16 addr, u32 *value)
{
	int ret;

	spacemit_espi_write_hdr(espi, 0, 0);
	spacemit_espi_write_hdr(espi, 1, addr >> 8);
	spacemit_espi_write_hdr(espi, 2, addr);
	spacemit_espi_trigger_cmd(espi, ESPI_DNCMD_GET_CONFIGURATION);

	ret = spacemit_espi_poll_status(espi, ESPI_DNCMD_INT);
	if (ret)
		return ret;

	*value = spacemit_espi_read_hdr(espi, 3);
	*value |= (u32)spacemit_espi_read_hdr(espi, 4) << 8;
	*value |= (u32)spacemit_espi_read_hdr(espi, 5) << 16;
	*value |= (u32)spacemit_espi_read_hdr(espi, 6) << 24;

	return 0;
}

static int spacemit_espi_cmd_set_config(struct spacemit_espi *espi, u16 addr, u32 value)
{
	spacemit_espi_write_hdr(espi, 0, 0);
	spacemit_espi_write_hdr(espi, 1, addr >> 8);
	spacemit_espi_write_hdr(espi, 2, addr);
	spacemit_espi_write_hdr(espi, 3, value);
	spacemit_espi_write_hdr(espi, 4, value >> 8);
	spacemit_espi_write_hdr(espi, 5, value >> 16);
	spacemit_espi_write_hdr(espi, 6, value >> 24);
	spacemit_espi_trigger_cmd(espi, ESPI_DNCMD_SET_CONFIGURATION);

	return spacemit_espi_poll_status(espi, ESPI_DNCMD_INT);
}

static void spacemit_espi_enable_irqs(struct spacemit_espi *espi)
{
	writel(ESPI_ALL_INT, espi->regs + ESPI_SLAVE0_INT_EN);
}

static void spacemit_espi_reset_controller(struct spacemit_espi *espi)
{
	u32 val;

	val = readl(espi->regs + ESPI_GLOBAL_CONTROL_1);
	val &= ~ESPI_ESPI_RSTN;
	writel(val, espi->regs + ESPI_GLOBAL_CONTROL_1);

	val = readl(espi->regs + ESPI_GLOBAL_CONTROL_1);
	val |= ESPI_ESPI_RSTN | ESPI_SW_RST;
	writel(val, espi->regs + ESPI_GLOBAL_CONTROL_1);
}

static void spacemit_espi_send_inband_reset(struct spacemit_espi *espi)
{
	spacemit_espi_trigger_cmd(espi, ESPI_DNCMD_IN_BAND_RESET);
	spacemit_espi_wait_tx_idle(espi);
	udelay(1000);
}

static u32 spacemit_espi_freq_to_sel(u32 mhz)
{
	switch (mhz) {
	case 66:
		return ESPI_GEN_OP_FREQ_66MHZ;
	case 50:
		return ESPI_GEN_OP_FREQ_50MHZ;
	case 33:
		return ESPI_GEN_OP_FREQ_33MHZ;
	case 25:
		return ESPI_GEN_OP_FREQ_25MHZ;
	case 20:
	default:
		return ESPI_GEN_OP_FREQ_20MHZ;
	}
}

static u32 spacemit_espi_build_master_only_config(struct spacemit_espi *espi)
{
	u32 cfg = 0;

	if (espi->crc_enable)
		cfg |= ESPI_GEN_CRC_ENABLE;

	if (espi->alert_pin_mode) {
		cfg |= ESPI_GEN_ALERT_MODE_PIN;
		if (espi->alert_open_drain)
			cfg |= ESPI_GEN_OPEN_DRAIN_ALERT_SEL;
	}

	cfg |= FIELD_PREP(ESPI_GEN_IO_MODE_SEL_MASK, espi->io_mode);
	cfg |= FIELD_PREP(ESPI_GEN_OP_FREQ_SEL_MASK,
			  spacemit_espi_freq_to_sel(espi->op_freq_mhz));

	if (espi->enable_peri)
		cfg |= ESPI_GEN_PERI_CHAN_SUP;
	if (espi->enable_vwire)
		cfg |= ESPI_GEN_VWIRE_CHAN_SUP;
	if (espi->enable_oob)
		cfg |= ESPI_GEN_OOB_CHAN_SUP;
	if (espi->enable_flash)
		cfg |= ESPI_GEN_FLASH_CHAN_SUP;

	return cfg;
}

static u32 spacemit_espi_build_negotiated_config(struct spacemit_espi *espi,
						 u32 slave_caps)
{
	u32 cfg = 0;
	u32 io_mode;
	u32 freq;

	if (espi->crc_enable)
		cfg |= ESPI_GEN_CRC_ENABLE;

	if (espi->alert_pin_mode) {
		cfg |= ESPI_GEN_ALERT_MODE_PIN;
		if (espi->alert_open_drain &&
		    (slave_caps & ESPI_GEN_OPEN_DRAIN_ALERT_SUP))
			cfg |= ESPI_GEN_OPEN_DRAIN_ALERT_SEL;
	}

	io_mode = min_t(u32, espi->io_mode,
			FIELD_GET(ESPI_GEN_IO_MODE_SUP_MASK, slave_caps));
	freq = min_t(u32, spacemit_espi_freq_to_sel(espi->op_freq_mhz),
		     FIELD_GET(ESPI_GEN_OP_FREQ_SUP_MASK, slave_caps));
	cfg |= FIELD_PREP(ESPI_GEN_IO_MODE_SEL_MASK, io_mode);
	cfg |= FIELD_PREP(ESPI_GEN_OP_FREQ_SEL_MASK, freq);

	if (espi->enable_peri && (slave_caps & ESPI_GEN_PERI_CHAN_SUP))
		cfg |= ESPI_GEN_PERI_CHAN_SUP;
	if (espi->enable_vwire && (slave_caps & ESPI_GEN_VWIRE_CHAN_SUP))
		cfg |= ESPI_GEN_VWIRE_CHAN_SUP;
	if (espi->enable_oob && (slave_caps & ESPI_GEN_OOB_CHAN_SUP))
		cfg |= ESPI_GEN_OOB_CHAN_SUP;
	if (espi->enable_flash && (slave_caps & ESPI_GEN_FLASH_CHAN_SUP))
		cfg |= ESPI_GEN_FLASH_CHAN_SUP;

	return cfg;
}

static int spacemit_espi_configure_channels(struct spacemit_espi *espi, u32 cfg)
{
	int ret;

	if (cfg & ESPI_GEN_PERI_CHAN_SUP) {
		ret = spacemit_espi_cmd_set_config(espi, ESPI_SLAVE_PERI_CFG,
						   ESPI_ENABLE_PR_CHANNEL);
		if (ret)
			return ret;
	}

	if (cfg & ESPI_GEN_VWIRE_CHAN_SUP) {
		ret = spacemit_espi_cmd_set_config(espi, ESPI_SLAVE_VWIRE_CFG,
						   ESPI_ENABLE_VW_CHANNEL);
		if (ret)
			return ret;
	}

	if (cfg & ESPI_GEN_OOB_CHAN_SUP) {
		ret = spacemit_espi_cmd_set_config(espi, ESPI_SLAVE_OOB_CFG,
						   ESPI_ENABLE_OOB_CHANNEL);
		if (ret)
			return ret;
	}

	if (cfg & ESPI_GEN_FLASH_CHAN_SUP) {
		ret = spacemit_espi_cmd_set_config(espi, ESPI_SLAVE_FLASH_CFG,
						   ESPI_ENABLE_FLASH_CHANNEL);
		if (ret)
			return ret;
	}

	return 0;
}

static void spacemit_espi_apply_controller_config(struct spacemit_espi *espi, u32 cfg)
{
	u32 val;
	u32 mask = ESPI_CRC_CHECK_EN | ESPI_ALERT_MODE_SEL |
		   ESPI_IO_MODE_SEL_MASK | ESPI_CLK_FREQ_SEL_MASK |
		   ESPI_PR_CHANNEL_ENABLE | ESPI_VW_CHANNEL_ENABLE |
		   ESPI_OOB_CHANNEL_ENABLE | ESPI_FLASH_CHANNEL_ENABLE;

	val = readl(espi->regs + ESPI_SLAVE0_CONFIG);
	val &= ~mask;

	if (cfg & ESPI_GEN_CRC_ENABLE)
		val |= ESPI_CRC_CHECK_EN;
	if (cfg & ESPI_GEN_ALERT_MODE_PIN)
		val |= ESPI_ALERT_MODE_SEL;

	val |= FIELD_PREP(ESPI_IO_MODE_SEL_MASK,
			  FIELD_GET(ESPI_GEN_IO_MODE_SEL_MASK, cfg));
	val |= FIELD_PREP(ESPI_CLK_FREQ_SEL_MASK,
			  FIELD_GET(ESPI_GEN_OP_FREQ_SEL_MASK, cfg));

	if (cfg & ESPI_GEN_PERI_CHAN_SUP)
		val |= ESPI_PR_CHANNEL_ENABLE;
	if (cfg & ESPI_GEN_VWIRE_CHAN_SUP)
		val |= ESPI_VW_CHANNEL_ENABLE;
	if (cfg & ESPI_GEN_OOB_CHAN_SUP)
		val |= ESPI_OOB_CHANNEL_ENABLE;
	if (cfg & ESPI_GEN_FLASH_CHAN_SUP)
		val |= ESPI_FLASH_CHANNEL_ENABLE;

	writel(val, espi->regs + ESPI_SLAVE0_CONFIG);
}

static void spacemit_espi_program_window(struct spacemit_espi *espi)
{
	u32 val;

	val = readl(espi->regs + ESPI_GLOBAL_CONTROL_0);
	val &= ~(ESPI_CONTROL_WATCHDOG_CNT_MASK | ESPI_MST_STOP_EN);
	val |= ESPI_CONTROL_WATCHDOG_EN;
	val |= FIELD_PREP(ESPI_CONTROL_WATCHDOG_CNT_MASK,
			  espi->watchdog_timeout);
	if (espi->auto_gating)
		val |= ESPI_MST_STOP_EN;
	writel(val, espi->regs + ESPI_GLOBAL_CONTROL_0);

	writel(espi->pr_mem_base0, espi->regs + ESPI_PR_BASE_ADDR_MEM0);
	writel(espi->pr_mem_base1, espi->regs + ESPI_PR_BASE_ADDR_MEM1);
}

static int spacemit_espi_parse_dt(struct platform_device *pdev,
				  struct spacemit_espi *espi)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource range;
	const char *str;
	u32 value;
	int ret;

	espi->op_freq_mhz = SPACEMIT_ESPI_DEFAULT_FREQ_MHZ;
	of_property_read_u32(np, "operating-frequency", &espi->op_freq_mhz);

	espi->watchdog_timeout = SPACEMIT_ESPI_DEFAULT_WDT;
	of_property_read_u32(np, "watchdog-timeout", &espi->watchdog_timeout);

	ret = of_range_to_resource(np, 0, &range);
	if (!ret)
		espi->pr_mem_base0 = lower_32_bits(range.start);

	if (!of_property_read_u32(np, "pr-mem-base0", &value))
		espi->pr_mem_base0 = value;

	if (!espi->pr_mem_base0)
		dev_warn(dev, "missing PR memory base, shared window recovery may fail\n");

	if (!of_property_read_u32(np, "pr-mem-base1", &value))
		espi->pr_mem_base1 = value;
	else if (espi->pr_mem_base0)
		espi->pr_mem_base1 = espi->pr_mem_base0 + SZ_16M;

	if (!of_property_read_string(np, "io-mode", &str)) {
		if (!strcmp(str, "quad"))
			espi->io_mode = ESPI_GEN_IO_MODE_QUAD;
		else if (!strcmp(str, "dual"))
			espi->io_mode = ESPI_GEN_IO_MODE_DUAL;
		else
			espi->io_mode = ESPI_GEN_IO_MODE_SINGLE;
	} else {
		espi->io_mode = ESPI_GEN_IO_MODE_SINGLE;
	}

	if (!of_property_read_string(np, "alert-mode", &str))
		espi->alert_pin_mode = !strcmp(str, "pin");
	else
		espi->alert_pin_mode = false;

	if (!of_property_read_string(np, "alert-type", &str))
		espi->alert_open_drain = !strcmp(str, "open-drain");
	else
		espi->alert_open_drain = true;

	espi->crc_enable = spacemit_espi_property_enabled(np, "crc-enable", true);
	espi->enable_peri = spacemit_espi_property_enabled(np,
						"peripheral-channel-enable",
						true);
	espi->enable_vwire = spacemit_espi_property_enabled(np,
						"vwire-channel-enable",
						true);
	espi->enable_oob = spacemit_espi_property_enabled(np,
						"oob-channel-enable",
						true);
	espi->enable_flash = spacemit_espi_property_enabled(np,
						"flash-channel-enable",
						true);
	espi->auto_gating = spacemit_espi_property_enabled(np,
						"auto-gating-enable",
						true);

	return 0;
}

static int spacemit_espi_enable_clks(struct device *dev, struct spacemit_espi *espi)
{
	int ret;

	if (!espi->num_clks || espi->clocks_enabled)
		return 0;

	ret = clk_bulk_prepare_enable(espi->num_clks, espi->clks);
	if (ret) {
		dev_err(dev, "failed to enable clocks: %d\n", ret);
		return ret;
	}

	espi->clocks_enabled = true;
	return 0;
}

static void spacemit_espi_disable_clks(struct spacemit_espi *espi)
{
	if (!espi->num_clks || !espi->clocks_enabled)
		return;

	clk_bulk_disable_unprepare(espi->num_clks, espi->clks);
	espi->clocks_enabled = false;
}

static int spacemit_espi_init_locked(struct spacemit_espi *espi)
{
	u32 slave_caps = 0;
	u32 cfg;
	int ret;

	/*
	 * Clear the cached link state before starting a new recovery cycle so
	 * callers never observe a stale "ready" state after an early failure.
	 */
	espi->initialized = false;
	espi->slave_ready = false;

	ret = spacemit_espi_enable_clks(espi->dev, espi);
	if (ret)
		return ret;

	ret = reset_control_deassert(espi->resets);
	if (ret)
		return ret;

	if (espi->sclk && !IS_ERR(espi->sclk) && espi->op_freq_mhz) {
		ret = clk_set_rate(espi->sclk, espi->op_freq_mhz * 1000000UL);
		if (ret)
			dev_dbg(espi->dev, "failed to set sclk rate: %d\n", ret);
	}

	spacemit_espi_enable_irqs(espi);
	writel(ESPI_ALL_INT, espi->regs + ESPI_SLAVE0_INT_STS);

	spacemit_espi_reset_controller(espi);
	spacemit_espi_send_inband_reset(espi);

	ret = spacemit_espi_cmd_get_config(espi, ESPI_SLAVE_GEN_CFG, &slave_caps);
	if (ret) {
		cfg = spacemit_espi_build_master_only_config(espi);
		espi->slave_ready = false;
		dev_warn(espi->dev, "slave capability negotiation failed: %d\n", ret);
	} else {
		cfg = spacemit_espi_build_negotiated_config(espi, slave_caps);
		ret = spacemit_espi_configure_channels(espi, cfg);
		if (ret) {
			dev_warn(espi->dev, "channel configuration failed: %d\n", ret);
			cfg = spacemit_espi_build_master_only_config(espi);
			espi->slave_ready = false;
		} else {
			ret = spacemit_espi_cmd_set_config(espi, ESPI_SLAVE_GEN_CFG, cfg);
			if (ret) {
				dev_warn(espi->dev, "general configuration failed: %d\n", ret);
				cfg = spacemit_espi_build_master_only_config(espi);
				espi->slave_ready = false;
			} else {
				espi->slave_ready = true;
			}
		}
	}

	spacemit_espi_apply_controller_config(espi, cfg);
	spacemit_espi_program_window(espi);
	espi->initialized = true;

	dev_dbg(espi->dev, "controller ready, slave_ready=%d master_cap=0x%08x\n",
		espi->slave_ready,
		readl(espi->regs + ESPI_MASTER_CAP));

	return 0;
}

int spacemit_k3_espi_recover(struct device *dev)
{
	struct spacemit_espi *espi;
	int ret;

	if (!dev)
		return -EINVAL;

	espi = dev_get_drvdata(dev);
	if (!espi)
		return -ENODEV;

	mutex_lock(&espi->lock);
	ret = spacemit_espi_init_locked(espi);
	mutex_unlock(&espi->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(spacemit_k3_espi_recover);

static int spacemit_espi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spacemit_espi *espi;
	struct resource *res;
	int ret;

	espi = devm_kzalloc(dev, sizeof(*espi), GFP_KERNEL);
	if (!espi)
		return -ENOMEM;

	espi->dev = dev;
	mutex_init(&espi->lock);

	espi->num_clks = devm_clk_bulk_get_all(dev, &espi->clks);
	if (espi->num_clks < 0)
		return dev_err_probe(dev, espi->num_clks,
				     "failed to get clocks\n");

	espi->sclk = devm_clk_get_optional(dev, "sclk");
	if (IS_ERR(espi->sclk))
		return dev_err_probe(dev, PTR_ERR(espi->sclk),
				     "failed to get sclk\n");

	espi->resets = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(espi->resets))
		return dev_err_probe(dev, PTR_ERR(espi->resets),
				     "failed to get resets\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	espi->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(espi->regs))
		return PTR_ERR(espi->regs);

	spacemit_espi_parse_dt(pdev, espi);
	platform_set_drvdata(pdev, espi);

	ret = spacemit_espi_enable_clks(dev, espi);
	if (ret)
		return ret;

	ret = reset_control_deassert(espi->resets);
	if (ret) {
		dev_err_probe(dev, ret, "failed to deassert resets\n");
		goto err_disable_clks;
	}

	ret = spacemit_k3_espi_recover(dev);
	if (ret)
		dev_warn(dev, "controller init failed, keeping bootloader state: %d\n",
			 ret);

	ret = devm_of_platform_populate(dev);
	if (ret) {
		dev_err_probe(dev, ret, "failed to populate child devices\n");
		goto err_assert_resets;
	}

	return 0;

err_assert_resets:
	reset_control_assert(espi->resets);
err_disable_clks:
	spacemit_espi_disable_clks(espi);
	return ret;
}

static void spacemit_espi_remove(struct platform_device *pdev)
{
	struct spacemit_espi *espi = platform_get_drvdata(pdev);

	reset_control_assert(espi->resets);
	spacemit_espi_disable_clks(espi);
}

#ifdef CONFIG_PM_SLEEP
static int spacemit_espi_suspend_noirq(struct device *dev)
{
	struct spacemit_espi *espi = dev_get_drvdata(dev);

	spacemit_espi_disable_clks(espi);
	return 0;
}

static int spacemit_espi_resume_noirq(struct device *dev)
{
	return spacemit_k3_espi_recover(dev);
}
#endif

static const struct dev_pm_ops spacemit_espi_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(spacemit_espi_suspend_noirq,
				      spacemit_espi_resume_noirq)
};

static const struct of_device_id spacemit_espi_of_match[] = {
	{ .compatible = "spacemit,k3-espi" },
	{}
};
MODULE_DEVICE_TABLE(of, spacemit_espi_of_match);

static struct platform_driver spacemit_espi_driver = {
	.probe = spacemit_espi_probe,
	.remove = spacemit_espi_remove,
	.driver = {
		.name = "spacemit-k3-espi",
		.of_match_table = spacemit_espi_of_match,
		.pm = pm_ptr(&spacemit_espi_pm_ops),
	},
};
module_platform_driver(spacemit_espi_driver);

MODULE_DESCRIPTION("SpacemiT K3 eSPI controller driver");
MODULE_LICENSE("GPL");
