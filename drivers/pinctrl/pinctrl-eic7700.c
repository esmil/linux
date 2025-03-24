// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl driver for the ESWIN EIC7700 SoC
 *
 * Copyright (C) 2025 Emil Renner Berthing <emil.renner.berthing@canonical.com>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <linux/of_address.h>
#include <linux/of_device.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinmux.h"
#include "pinconf.h"

#define EIC7700_PWDATA_FUNCSEL	GENMASK(18, 16)
#define EIC7700_PWDATA_ST	BIT(7)
#define EIC7700_PWDATA_DS	GENMASK(6, 3)
#define EIC7700_PWDATA_PD	BIT(2)
#define EIC7700_PWDATA_PU	BIT(1)
#define EIC7700_PWDATA_IE	BIT(0)

#define EIC7700_PULLDOWN_OHM	22000
#define EIC7700_PULLUP_OHM	25000
#define EIC7700_STRONGUP_OHM	3500

struct eic7700_pinctrl {
	struct pinctrl_desc desc;
	struct mutex mutex;	/* serialize adding functions */
	raw_spinlock_t lock;	/* serialize register access */
	void __iomem *base;
	struct pinctrl_dev *pctl;
};

static void __iomem *eic7700_pwdata(struct eic7700_pinctrl *ep,
				    unsigned int pin)
{
	return ep->base + 4 * pin;
}

enum eic7700_muxtype {
	EIC7700_MUX_____,
	EIC7700_MUX_T___,
	EIC7700_MUX_CSI,
	EIC7700_MUX_DBG,
	EIC7700_MUX_DDR,
	EIC7700_MUX_FAN,
	EIC7700_MUX_GPIO,
	EIC7700_MUX_HDMI,
	EIC7700_MUX_I2C,
	EIC7700_MUX_I2S,
	EIC7700_MUX_JTAG,
	EIC7700_MUX_MIPI,
	EIC7700_MUX_MODE,
	EIC7700_MUX_OSC,
	EIC7700_MUX_PCI,
	EIC7700_MUX_PWM,
	EIC7700_MUX_RGMI,
	EIC7700_MUX_RST,
	EIC7700_MUX_SATA,
	EIC7700_MUX_SPI,
	EIC7700_MUX_SDIO,
	EIC7700_MUX_UART,
	EIC7700_MUX_USB,
};

static const char *const eic7700_muxtype_string[] = {
	[EIC7700_MUX_T___] = "tristate",
	[EIC7700_MUX_CSI]  = "csi",
	[EIC7700_MUX_DBG]  = "debug",
	[EIC7700_MUX_DDR]  = "ddr",
	[EIC7700_MUX_FAN]  = "fan",
	[EIC7700_MUX_GPIO] = "gpio",
	[EIC7700_MUX_HDMI] = "hdmi",
	[EIC7700_MUX_I2C]  = "i2c",
	[EIC7700_MUX_I2S]  = "i2s",
	[EIC7700_MUX_JTAG] = "jtag",
	[EIC7700_MUX_MIPI] = "mipi",
	[EIC7700_MUX_MODE] = "mode",
	[EIC7700_MUX_OSC]  = "oscillator",
	[EIC7700_MUX_PCI]  = "pci",
	[EIC7700_MUX_PWM]  = "pwm",
	[EIC7700_MUX_RGMI] = "rgmii",
	[EIC7700_MUX_RST]  = "reset",
	[EIC7700_MUX_SATA] = "sata",
	[EIC7700_MUX_SPI]  = "spi",
	[EIC7700_MUX_SDIO] = "sdio",
	[EIC7700_MUX_UART] = "uart",
	[EIC7700_MUX_USB]  = "usb",
};

static enum eic7700_muxtype eic7700_muxtype_get(const char *str)
{
	enum eic7700_muxtype mt;

	for (mt = EIC7700_MUX_T___; mt < ARRAY_SIZE(eic7700_muxtype_string); mt++) {
		if (!strcmp(str, eic7700_muxtype_string[mt]))
			return mt;
	}
	return EIC7700_MUX_____;
}

#define EIC7700_PAD(_nr, _name, m0, m1, m2, m3, m6, m7) \
	{ .number = _nr, .name = #_name, .drv_data = (void *)( \
		(EIC7700_MUX_##m0 <<  0) | (EIC7700_MUX_##m1 <<  5) | (EIC7700_MUX_##m2 << 10) | \
		(EIC7700_MUX_##m3 << 15) | (EIC7700_MUX_##m6 << 20) | (EIC7700_MUX_##m7 << 25)) }

static unsigned long eic7700_pad_muxdata(void *drv_data)
{
	return (uintptr_t)drv_data;
}

static bool eic7700_pad_is_oscillator(void *drv_data)
{
	return (eic7700_pad_muxdata(drv_data) & GENMASK(4, 0)) == EIC7700_MUX_OSC;
}

static bool eic7700_pad_is_rgmii(void *drv_data)
{
	return (eic7700_pad_muxdata(drv_data) & GENMASK(4, 0)) == EIC7700_MUX_RGMI;
}

static const struct pinctrl_pin_desc eic7700_pins[] = {
	EIC7700_PAD(0,   CHIP_MODE,       MODE, T___, ____, ____, ____, ____),
	EIC7700_PAD(1,   MODE_SET0,       SDIO, T___, GPIO, ____, ____, ____), /* GPIO13  */
	EIC7700_PAD(2,   MODE_SET1,       SDIO, T___, GPIO, ____, ____, ____), /* GPIO14  */
	EIC7700_PAD(3,   MODE_SET2,       SDIO, T___, GPIO, ____, ____, ____), /* GPIO15  */
	EIC7700_PAD(4,   MODE_SET3,       SDIO, T___, GPIO, ____, ____, ____), /* GPIO16  */
	EIC7700_PAD(5,   XIN,             OSC,  T___, ____, ____, ____, ____),
	EIC7700_PAD(6,   RTC_XIN,         OSC,  T___, ____, ____, ____, ____),
	EIC7700_PAD(7,   RST_OUT_N,       RST,  T___, ____, ____, ____, ____),
	EIC7700_PAD(8,   KEY_RESET_N,     RST,  T___, ____, ____, ____, ____),
	/* skip 9, 10 and 11 so we can calculate register offsets from the pin number */
	EIC7700_PAD(12,  GPIO0,           GPIO, T___, ____, ____, ____, ____), /* GPIO0   */
	EIC7700_PAD(13,  POR_SEL,         MODE, T___, ____, ____, ____, ____),
	EIC7700_PAD(14,  JTAG0_TCK,       JTAG, SPI,  GPIO, T___, ____, ____), /* GPIO1   */
	EIC7700_PAD(15,  JTAG0_TMS,       JTAG, SPI,  GPIO, T___, ____, ____), /* GPIO2   */
	EIC7700_PAD(16,  JTAG0_TDI,       JTAG, SPI,  GPIO, T___, ____, ____), /* GPIO3   */
	EIC7700_PAD(17,  JTAG0_TDO,       JTAG, SPI,  GPIO, T___, ____, ____), /* GPIO4   */
	EIC7700_PAD(18,  GPIO5,           GPIO, SPI,  T___, ____, ____, ____), /* GPIO5   */
	EIC7700_PAD(19,  SPI2_CS0_N,      SPI,  T___, GPIO, ____, ____, ____), /* GPIO6   */
	EIC7700_PAD(20,  JTAG1_TCK,       JTAG, T___, GPIO, ____, ____, ____), /* GPIO7   */
	EIC7700_PAD(21,  JTAG1_TMS,       JTAG, T___, GPIO, ____, ____, ____), /* GPIO8   */
	EIC7700_PAD(22,  JTAG1_TDI,       JTAG, T___, GPIO, ____, ____, ____), /* GPIO9   */
	EIC7700_PAD(23,  JTAG1_TDO,       JTAG, T___, GPIO, ____, ____, ____), /* GPIO10  */
	EIC7700_PAD(24,  GPIO11,          GPIO, T___, ____, ____, ____, ____), /* GPIO11  */
	EIC7700_PAD(25,  SPI2_CS1_N,      SPI,  T___, GPIO, ____, ____, ____), /* GPIO12  */
	EIC7700_PAD(26,  PCIE_CLKREQ_N,   PCI,  T___, ____, ____, ____, ____),
	EIC7700_PAD(27,  PCIE_WAKE_N,     PCI,  T___, ____, ____, ____, ____),
	EIC7700_PAD(28,  PCIE_PERST_N,    PCI,  T___, ____, ____, ____, ____),
	EIC7700_PAD(29,  HDMI_SCL,        HDMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(30,  HDMI_SDA,        HDMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(31,  HDMI_CEC,        HDMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(32,  JTAG2_TRST,      JTAG, T___, GPIO, ____, CSI,  ____), /* GPIO17  */
	EIC7700_PAD(33,  RGMII0_CLK_125,  RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(34,  RGMII0_TXEN,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(35,  RGMII0_TXCLK,    RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(36,  RGMII0_TXD0,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(37,  RGMII0_TXD1,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(38,  RGMII0_TXD2,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(39,  RGMII0_TXD3,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(40,  I2S0_BCLK,       I2S,  T___, GPIO, ____, CSI,  ____), /* GPIO18  */
	EIC7700_PAD(41,  I2S0_WCLK,       I2S,  T___, GPIO, ____, CSI,  ____), /* GPIO19  */
	EIC7700_PAD(42,  I2S0_SDI,        I2S,  T___, GPIO, ____, CSI,  ____), /* GPIO20  */
	EIC7700_PAD(43,  I2S0_SDO,        I2S,  T___, GPIO, ____, CSI,  ____), /* GPIO21  */
	EIC7700_PAD(44,  I2S_MCLK,        I2S,  T___, GPIO, ____, CSI,  ____), /* GPIO22  */
	EIC7700_PAD(45,  RGMII0_RXCLK,    RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(46,  RGMII0_RXDV,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(47,  RGMII0_RXD0,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(48,  RGMII0_RXD1,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(49,  RGMII0_RXD2,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(50,  RGMII0_RXD3,     RGMI, T___, ____, ____, CSI,  ____),
	EIC7700_PAD(51,  I2S2_BCLK,       I2S,  T___, GPIO, ____, CSI,  ____), /* GPIO23  */
	EIC7700_PAD(52,  I2S2_WCLK,       I2S,  T___, GPIO, ____, CSI,  ____), /* GPIO24  */
	EIC7700_PAD(53,  I2S2_SDI,        I2S,  T___, GPIO, ____, CSI,  ____), /* GPIO25  */
	EIC7700_PAD(54,  I2S2_SDO,        I2S,  T___, GPIO, ____, CSI,  ____), /* GPIO26  */
	EIC7700_PAD(55,  GPIO27,          GPIO, SATA, T___, ____, CSI,  ____), /* GPIO27  */
	EIC7700_PAD(56,  GPIO28,          GPIO, T___, ____, ____, ____, ____), /* GPIO28  */
	EIC7700_PAD(57,  GPIO29,          MODE, SDIO, GPIO, T___, ____, ____), /* GPIO29  */
	EIC7700_PAD(58,  RGMII0_MDC,      RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(59,  RGMII0_MDIO,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(60,  RGMII0_INTB,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(61,  RGMII1_CLK_125,  RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(62,  RGMII1_TXEN,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(63,  RGMII1_TXCLK,    RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(64,  RGMII1_TXD0,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(65,  RGMII1_TXD1,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(66,  RGMII1_TXD2,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(67,  RGMII1_TXD3,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(68,  I2S1_BCLK,       I2S,  T___, GPIO, ____, ____, ____), /* GPIO30  */
	EIC7700_PAD(69,  I2S1_WCLK,       I2S,  T___, GPIO, ____, ____, ____), /* GPIO31  */
	EIC7700_PAD(70,  I2S1_SDI,        I2S,  T___, GPIO, ____, ____, ____), /* GPIO32  */
	EIC7700_PAD(71,  I2S1_SDO,        I2S,  T___, GPIO, ____, ____, ____), /* GPIO33  */
	EIC7700_PAD(72,  GPIO34,          MODE, SDIO, GPIO, T___, ____, ____), /* GPIO34  */
	EIC7700_PAD(73,  RGMII1_RXCLK,    RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(74,  RGMII2_RXDV,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(75,  RGMII2_RXD0,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(76,  RGMII2_RXD1,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(77,  RGMII2_RXD2,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(78,  RGMII2_RXD3,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(79,  SPI1_CS0_N,      SPI,  T___, GPIO, ____, ____, ____), /* GPIO35  */
	EIC7700_PAD(80,  SPI1_CLK,        SPI,  T___, GPIO, ____, ____, ____), /* GPIO36  */
	EIC7700_PAD(81,  SPI1_D0,         SPI,  I2C,  GPIO, UART, T___, ____), /* GPIO37  */
	EIC7700_PAD(82,  SPI1_D1,         SPI,  I2C,  GPIO, UART, T___, ____), /* GPIO38  */
	EIC7700_PAD(83,  SPI1_D2,         SPI,  SDIO, GPIO, T___, ____, ____), /* GPIO39  */
	EIC7700_PAD(84,  SPI1_D3,         SPI,  PWM,  GPIO, T___, ____, ____), /* GPIO40  */
	EIC7700_PAD(85,  SPI1_CS1_N,      SPI,  PWM,  GPIO, T___, ____, ____), /* GPIO41  */
	EIC7700_PAD(86,  RGMII1_MDC,      RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(87,  RGMII1_MDIO,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(88,  RGMII1_INTB,     RGMI, T___, ____, ____, ____, ____),
	EIC7700_PAD(89,  USB0_PWREN,      USB,  T___, GPIO, ____, ____, ____), /* GPIO42  */
	EIC7700_PAD(90,  USB1_PWREN,      USB,  T___, GPIO, ____, ____, ____), /* GPIO43  */
	EIC7700_PAD(91,  I2C0_SCL,        I2C,  T___, GPIO, ____, ____, ____), /* GPIO44  */
	EIC7700_PAD(92,  I2C0_SDA,        I2C,  T___, GPIO, ____, ____, ____), /* GPIO45  */
	EIC7700_PAD(93,  I2C1_SCL,        I2C,  T___, GPIO, ____, ____, ____), /* GPIO46  */
	EIC7700_PAD(94,  I2C1_SDA,        I2C,  T___, GPIO, ____, ____, ____), /* GPIO47  */
	EIC7700_PAD(95,  I2C2_SCL,        I2C,  T___, GPIO, ____, ____, ____), /* GPIO48  */
	EIC7700_PAD(96,  I2C2_SDA,        I2C,  T___, GPIO, ____, CSI,  ____), /* GPIO49  */
	EIC7700_PAD(97,  I2C3_SCL,        I2C,  T___, GPIO, ____, CSI,  ____), /* GPIO50  */
	EIC7700_PAD(98,  I2C3_SDA,        I2C,  T___, GPIO, ____, CSI,  ____), /* GPIO51  */
	EIC7700_PAD(99,  I2C4_SCL,        I2C,  T___, GPIO, ____, CSI,  ____), /* GPIO52  */
	EIC7700_PAD(100, I2C4_SDA,        I2C,  T___, GPIO, ____, CSI,  ____), /* GPIO53  */
	EIC7700_PAD(101, I2C5_SCL,        I2C,  T___, GPIO, ____, CSI,  ____), /* GPIO54  */
	EIC7700_PAD(102, I2C5_SDA,        I2C,  T___, GPIO, ____, CSI,  ____), /* GPIO55  */
	EIC7700_PAD(103, UART0_TX,        UART, T___, GPIO, ____, ____, ____), /* GPIO56  */
	EIC7700_PAD(104, UART0_RX,        UART, T___, GPIO, ____, ____, ____), /* GPIO57  */
	EIC7700_PAD(105, UART1_TX,        UART, T___, GPIO, ____, ____, ____), /* GPIO58  */
	EIC7700_PAD(106, UART1_RX,        UART, T___, GPIO, ____, ____, ____), /* GPIO59  */
	EIC7700_PAD(107, UART1_CTS,       UART, I2C,  GPIO, T___, ____, ____), /* GPIO60  */
	EIC7700_PAD(108, UART1_RTS,       UART, I2C,  GPIO, T___, ____, ____), /* GPIO61  */
	EIC7700_PAD(109, UART2_TX,        UART, I2C,  GPIO, T___, CSI,  ____), /* GPIO62  */
	EIC7700_PAD(110, UART2_RX,        UART, I2C,  GPIO, T___, DBG,  ____), /* GPIO63  */
	EIC7700_PAD(111, JTAG2_TCK,       JTAG, T___, GPIO, ____, DBG,  ____), /* GPIO64  */
	EIC7700_PAD(112, JTAG2_TMS,       JTAG, T___, GPIO, ____, DBG,  ____), /* GPIO65  */
	EIC7700_PAD(113, JTAG2_TDI,       JTAG, T___, GPIO, ____, DBG,  ____), /* GPIO66  */
	EIC7700_PAD(114, JTAG2_TDO,       JTAG, T___, GPIO, ____, DBG,  ____), /* GPIO67  */
	EIC7700_PAD(115, FAN_PWM,         FAN,  T___, GPIO, ____, DBG,  ____), /* GPIO68  */
	EIC7700_PAD(116, FAN_TACH,        FAN,  T___, GPIO, ____, DBG,  ____), /* GPIO69  */
	EIC7700_PAD(117, MIPI_CSI0_XVS,   MIPI, T___, GPIO, ____, DBG,  ____), /* GPIO70  */
	EIC7700_PAD(118, MIPI_CSI0_XHS,   MIPI, T___, GPIO, ____, DBG,  ____), /* GPIO71  */
	EIC7700_PAD(119, MIPI_CSI0_MCLK,  MIPI, T___, GPIO, ____, DBG,  ____), /* GPIO72  */
	EIC7700_PAD(120, MIPI_CSI1_XVS,   MIPI, T___, GPIO, ____, DBG,  ____), /* GPIO73  */
	EIC7700_PAD(121, MIPI_CSI1_XHS,   MIPI, T___, GPIO, ____, DBG,  ____), /* GPIO74  */
	EIC7700_PAD(122, MIPI_CSI1_MCLK,  MIPI, T___, GPIO, ____, DBG,  ____), /* GPIO75  */
	EIC7700_PAD(123, MIPI_CSI2_XVS,   MIPI, T___, GPIO, ____, DBG,  ____), /* GPIO76  */
	EIC7700_PAD(124, MIPI_CSI2_XHS,   MIPI, T___, GPIO, ____, DBG,  ____), /* GPIO77  */
	EIC7700_PAD(125, MIPI_CSI2_MCLK,  MIPI, T___, GPIO, ____, DBG,  ____), /* GPIO78  */
	EIC7700_PAD(126, MIPI_CSI3_XVS,   MIPI, T___, GPIO, ____, SATA, ____), /* GPIO79  */
	EIC7700_PAD(127, MIPI_CSI3_XHS,   MIPI, T___, GPIO, ____, SATA, ____), /* GPIO80  */
	EIC7700_PAD(128, MIPI_CSI3_MCLK,  MIPI, T___, GPIO, ____, SATA, ____), /* GPIO81  */
	EIC7700_PAD(129, MIPI_CSI4_XVS,   MIPI, T___, GPIO, ____, CSI,  ____), /* GPIO82  */
	EIC7700_PAD(130, MIPI_CSI4_XHS,   MIPI, T___, GPIO, ____, CSI,  ____), /* GPIO83  */
	EIC7700_PAD(131, MIPI_CSI4_MCLK,  MIPI, T___, GPIO, ____, CSI,  ____), /* GPIO84  */
	EIC7700_PAD(132, MIPI_CSI5_XVS,   MIPI, T___, GPIO, ____, CSI,  ____), /* GPIO85  */
	EIC7700_PAD(133, MIPI_CSI5_XHS,   MIPI, T___, GPIO, ____, CSI,  ____), /* GPIO86  */
	EIC7700_PAD(134, MIPI_CSI5_MCLK,  MIPI, T___, GPIO, ____, CSI,  ____), /* GPIO87  */
	EIC7700_PAD(135, SPI3_CS_N,       SPI,  T___, GPIO, ____, ____, ____), /* GPIO88  */
	EIC7700_PAD(136, SPI3_CLK,        SPI,  T___, GPIO, ____, ____, ____), /* GPIO89  */
	EIC7700_PAD(137, SPI3_DI,         SPI,  T___, GPIO, ____, ____, ____), /* GPIO90  */
	EIC7700_PAD(138, SPI3_DO,         SPI,  T___, GPIO, ____, ____, ____), /* GPIO91  */
	EIC7700_PAD(139, GPIO92,          I2C,  MIPI, GPIO, UART, T___, ____), /* GPIO92  */
	EIC7700_PAD(140, GPIO93,          I2C,  MIPI, GPIO, UART, T___, ____), /* GPIO93  */
	EIC7700_PAD(141, S_MODE,          MODE, T___, GPIO, ____, ____, ____), /* GPIO94  */
	EIC7700_PAD(142, GPIO95,          MODE, T___, GPIO, ____, ____, ____), /* GPIO95  */
	EIC7700_PAD(143, SPI0_CS_N,       SPI,  T___, GPIO, ____, ____, ____), /* GPIO96  */
	EIC7700_PAD(144, SPI0_CLK,        SPI,  T___, GPIO, ____, ____, ____), /* GPIO97  */
	EIC7700_PAD(145, SPI0_D0,         SPI,  T___, GPIO, ____, ____, ____), /* GPIO98  */
	EIC7700_PAD(146, SPI0_D1,         SPI,  T___, GPIO, ____, ____, ____), /* GPIO99  */
	EIC7700_PAD(147, SPI0_D2,         SPI,  T___, GPIO, ____, ____, ____), /* GPIO100 */
	EIC7700_PAD(148, SPI0_D3,         SPI,  T___, GPIO, ____, ____, ____), /* GPIO101 */
	EIC7700_PAD(149, I2C10_SCL,       I2C,  T___, GPIO, ____, ____, ____), /* GPIO102 */
	EIC7700_PAD(150, I2C10_SDA,       I2C,  T___, GPIO, ____, ____, ____), /* GPIO103 */
	EIC7700_PAD(151, I2C11_SCL,       I2C,  T___, GPIO, ____, ____, ____), /* GPIO104 */
	EIC7700_PAD(152, I2C11_SDA,       I2C,  T___, GPIO, ____, ____, ____), /* GPIO105 */
	EIC7700_PAD(153, GPIO106,         GPIO, T___, ____, ____, ____, ____), /* GPIO106 */
	EIC7700_PAD(154, BOOT_SEL0,       MODE, T___, GPIO, ____, ____, ____), /* GPIO107 */
	EIC7700_PAD(155, BOOT_SEL1,       MODE, T___, GPIO, ____, ____, ____), /* GPIO108 */
	EIC7700_PAD(156, BOOT_SEL2,       MODE, T___, GPIO, ____, ____, ____), /* GPIO109 */
	EIC7700_PAD(157, BOOT_SEL3,       MODE, T___, GPIO, ____, ____, ____), /* GPIO110 */
	EIC7700_PAD(158, GPIO111,         GPIO, T___, ____, ____, ____, ____), /* GPIO111 */
	/* skip 159, 160, 161 and 162 */
	EIC7700_PAD(163, LPDDR_REF_CLK,   DDR,  T___, ____, ____, ____, ____),
};

static int eic7700_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);

	return ep->desc.npins;
}

static const char *eic7700_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						  unsigned int gsel)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);

	return ep->desc.pins[gsel].name;
}

static int eic7700_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					  unsigned int gsel,
					  const unsigned int **pins,
					  unsigned int *npins)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);

	*pins = &ep->desc.pins[gsel].number;
	*npins = 1;
	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void eic7700_pin_dbg_show(struct pinctrl_dev *pctldev,
				 struct seq_file *s, unsigned int pin)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);
	void __iomem *pwdata = eic7700_pwdata(ep, pin);
	u32 value;

	scoped_guard(raw_spinlock_irqsave, &ep->lock) {
		value = readl_relaxed(pwdata);
	}

	seq_printf(s, "[pwdata:0x%x=0x%05x]", 0x80 + 4 * pin, value);
}
#else
#define eic7700_pin_dbg_show NULL
#endif

static void eic7700_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
					struct pinctrl_map *map,
					unsigned int nmaps)
{
	unsigned long *seen = NULL;
	unsigned int i;

	for (i = 0; i < nmaps; i++) {
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_PIN &&
		    map[i].data.configs.configs != seen) {
			seen = map[i].data.configs.configs;
			kfree(seen);
		}
	}

	kfree(map);
}

static int eic7700_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					  struct device_node *np,
					  struct pinctrl_map **maps,
					  unsigned int *num_maps)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_map *map;
	unsigned long *configs;
	unsigned int nconfigs;
	unsigned int nmaps;
	int ret;

	nmaps = 0;
	for_each_available_child_of_node_scoped(np, child) {
		int npins = of_property_count_strings(child, "pins");

		if (npins <= 0) {
			dev_err(ep->pctl->dev, "no pins selected for %pOFn.%pOFn\n",
				np, child);
			return -EINVAL;
		}
		nmaps += npins;
		if (of_property_present(child, "function"))
			nmaps += npins;
	}

	map = kcalloc(nmaps, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	nmaps = 0;
	guard(mutex)(&ep->mutex);
	for_each_available_child_of_node_scoped(np, child) {
		unsigned int rollback = nmaps;
		enum eic7700_muxtype muxtype;
		struct property *prop;
		const char *funcname;
		const char **pgnames;
		const char *pinname;
		int npins;

		ret = pinconf_generic_parse_dt_config(child, pctldev, &configs, &nconfigs);
		if (ret) {
			dev_err(ep->pctl->dev, "%pOFn.%pOFn: error parsing pin config\n",
				np, child);
			goto free_map;
		}

		if (!of_property_read_string(child, "function", &funcname)) {
			muxtype = eic7700_muxtype_get(funcname);
			if (!muxtype) {
				dev_err(ep->pctl->dev, "%pOFn.%pOFn: unknown function '%s'\n",
					np, child, funcname);
				ret = -EINVAL;
				goto free_configs;
			}

			funcname = devm_kasprintf(ep->pctl->dev, GFP_KERNEL, "%pOFn.%pOFn",
						  np, child);
			if (!funcname) {
				ret = -ENOMEM;
				goto free_configs;
			}

			npins = of_property_count_strings(child, "pins");
			pgnames = devm_kcalloc(ep->pctl->dev, npins, sizeof(*pgnames), GFP_KERNEL);
			if (!pgnames) {
				ret = -ENOMEM;
				goto free_configs;
			}
		} else {
			funcname = NULL;
		}

		npins = 0;
		of_property_for_each_string(child, "pins", prop, pinname) {
			unsigned int i;

			for (i = 0; i < ep->desc.npins; i++) {
				if (!strcmp(pinname, ep->desc.pins[i].name))
					break;
			}
			if (i == ep->desc.npins) {
				nmaps = rollback;
				dev_err(ep->pctl->dev, "%pOFn.%pOFn: unknown pin '%s'\n",
					np, child, pinname);
				ret = -EINVAL;
				goto free_configs;
			}

			if (nconfigs) {
				map[nmaps].type = PIN_MAP_TYPE_CONFIGS_PIN;
				map[nmaps].data.configs.group_or_pin = ep->desc.pins[i].name;
				map[nmaps].data.configs.configs = configs;
				map[nmaps].data.configs.num_configs = nconfigs;
				nmaps += 1;
			}
			if (funcname) {
				pgnames[npins++] = ep->desc.pins[i].name;
				map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
				map[nmaps].data.mux.function = funcname;
				map[nmaps].data.mux.group = ep->desc.pins[i].name;
				nmaps += 1;
			}
		}

		if (funcname) {
			ret = pinmux_generic_add_function(pctldev, funcname, pgnames,
							  npins, (void *)muxtype);
			if (ret < 0) {
				dev_err(ep->pctl->dev, "error adding function %s\n", funcname);
				goto free_map;
			}
		}
	}

	*maps = map;
	*num_maps = nmaps;
	return 0;

free_configs:
	kfree(configs);
free_map:
	eic7700_pinctrl_dt_free_map(pctldev, map, nmaps);
	return ret;
}

static const struct pinctrl_ops eic7700_pinctrl_ops = {
	.get_groups_count = eic7700_pinctrl_get_groups_count,
	.get_group_name = eic7700_pinctrl_get_group_name,
	.get_group_pins = eic7700_pinctrl_get_group_pins,
	.pin_dbg_show = eic7700_pin_dbg_show,
	.dt_node_to_map = eic7700_pinctrl_dt_node_to_map,
	.dt_free_map = eic7700_pinctrl_dt_free_map,
};

static const u16 eic7700_drive_strength_reg_uA[8] = {
	3100, 6700, 9600, 12900, 18000, 20900, 23200, 25900,
};

static const u16 eic7700_drive_strength_rgmii_uA[8] = {
	3600, 6900, 10700, 13500, 17300, 20600, 23900, 26800,
};

static u32 eic7700_drive_strength_to_uA(void *drv_data, u32 ds)
{
	u32 ret = 0;

	if (eic7700_pad_is_rgmii(drv_data)) {
		if (ds < ARRAY_SIZE(eic7700_drive_strength_rgmii_uA))
			ret = eic7700_drive_strength_rgmii_uA[ds];
	} else {
		if (ds < ARRAY_SIZE(eic7700_drive_strength_reg_uA))
			ret = eic7700_drive_strength_reg_uA[ds];
	}
	return ret;
}

static u32 eic7700_drive_strength_from_uA(void *drv_data, u32 arg)
{
	const u16 *table;
	u32 len, ds;

	if (eic7700_pad_is_rgmii(drv_data)) {
		table = eic7700_drive_strength_rgmii_uA;
		len = ARRAY_SIZE(eic7700_drive_strength_rgmii_uA);
	} else {
		table = eic7700_drive_strength_reg_uA;
		len = ARRAY_SIZE(eic7700_drive_strength_reg_uA);
	}

	for (ds = 0; ds < len - 1; ds++) {
		if (arg <= table[ds])
			return ds;
	}
	return len;
}

static int eic7700_pwdata_rmw(struct eic7700_pinctrl *ep, unsigned int pin,
			      u32 mask, u32 value)
{
	void __iomem *pwdata = eic7700_pwdata(ep, pin);
	u32 tmp;

	scoped_guard(raw_spinlock_irqsave, &ep->lock) {
		tmp = readl_relaxed(pwdata);
		tmp = (tmp & ~mask) | value;
		writel_relaxed(tmp, pwdata);
	}
	return 0;
}

static int eic7700_pinconf_get(struct pinctrl_dev *pctldev,
			       unsigned int pin, unsigned long *config)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	u32 value = readl_relaxed(eic7700_pwdata(ep, pin));
	int param = pinconf_to_config_param(*config);
	bool enabled;
	u32 arg;

	if (eic7700_pad_is_oscillator(desc->drv_data))
		return -ENOTSUPP;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		enabled = !(value & (EIC7700_PWDATA_PD | EIC7700_PWDATA_PU));
		arg = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		enabled = value & EIC7700_PWDATA_PD;
		arg = EIC7700_PULLDOWN_OHM;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		enabled = value & EIC7700_PWDATA_PU;
		arg = EIC7700_PULLUP_OHM;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		enabled = true;
		arg = eic7700_drive_strength_to_uA(desc->drv_data,
						   FIELD_GET(EIC7700_PWDATA_DS, value));
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		enabled = value & EIC7700_PWDATA_IE;
		arg = enabled ? 1 : 0;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		enabled = value & EIC7700_PWDATA_ST;
		arg = enabled ? 1 : 0;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return enabled ? 0 : -EINVAL;
}

static int eic7700_pinconf_group_get(struct pinctrl_dev *pctldev,
				     unsigned int gsel, unsigned long *config)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);
	unsigned int pin = ep->desc.pins[gsel].number;

	return eic7700_pinconf_get(pctldev, pin, config);
}

static int eic7700_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			       unsigned long *configs, unsigned int num_configs)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	unsigned int i;
	u32 value = 0;
	u32 mask = 0;

	if (eic7700_pad_is_oscillator(desc->drv_data))
		return -ENOTSUPP;

	for (i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			mask |= EIC7700_PWDATA_PU | EIC7700_PWDATA_PD;
			value &= ~(EIC7700_PWDATA_PU | EIC7700_PWDATA_PD);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= EIC7700_PWDATA_PU | EIC7700_PWDATA_PD;
			value &= ~EIC7700_PWDATA_PU;
			value |= EIC7700_PWDATA_PD;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= EIC7700_PWDATA_PU | EIC7700_PWDATA_PD;
			value &= ~EIC7700_PWDATA_PD;
			value |= EIC7700_PWDATA_PU;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH_UA:
			mask |= EIC7700_PWDATA_DS;
			value &= ~EIC7700_PWDATA_DS;
			value |= FIELD_PREP(EIC7700_PWDATA_DS,
					    eic7700_drive_strength_from_uA(desc->drv_data, arg));
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			mask |= EIC7700_PWDATA_IE;
			if (arg)
				value |= EIC7700_PWDATA_IE;
			else
				value &= ~EIC7700_PWDATA_IE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			mask |= EIC7700_PWDATA_ST;
			if (arg)
				value |= EIC7700_PWDATA_ST;
			else
				value &= ~EIC7700_PWDATA_ST;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	return eic7700_pwdata_rmw(ep, pin, mask, value);
}

static int eic7700_pinconf_group_set(struct pinctrl_dev *pctldev,
				     unsigned int gsel,
				     unsigned long *configs,
				     unsigned int num_configs)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);
	unsigned int pin = ep->desc.pins[gsel].number;

	return eic7700_pinconf_set(pctldev, pin, configs, num_configs);
}

#ifdef CONFIG_DEBUG_FS
static void eic7700_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				     struct seq_file *s, unsigned int pin)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);
	u32 value = readl_relaxed(eic7700_pwdata(ep, pin));

	seq_printf(s, " [0x%02lx]", value & GENMASK(7, 0));
}
#else
#define eic7700_pinconf_dbg_show NULL
#endif

static const struct pinconf_ops eic7700_pinconf_ops = {
	.pin_config_get = eic7700_pinconf_get,
	.pin_config_group_get = eic7700_pinconf_group_get,
	.pin_config_set = eic7700_pinconf_set,
	.pin_config_group_set = eic7700_pinconf_group_set,
	.pin_config_dbg_show = eic7700_pinconf_dbg_show,
	.is_generic = true,
};

static int eic7700_pinmux_set(struct eic7700_pinctrl *ep, unsigned int pin,
			      unsigned long muxdata, enum eic7700_muxtype muxtype)
{
	u32 value;

	for (value = 0; muxdata; muxdata >>= 5, value++) {
		if ((muxdata & GENMASK(4, 0)) == muxtype)
			break;
	}
	if (!muxdata) {
		dev_err(ep->pctl->dev, "invalid mux %s for pin %s\n",
			eic7700_muxtype_string[muxtype], pin_get_name(ep->pctl, pin));
		return -EINVAL;
	}

	/* only pwdata[18:16] = 0, 1, 2, 3, 6 and 7 are used */
	if (value >= 4)
		value += 2;

	return eic7700_pwdata_rmw(ep, pin, EIC7700_PWDATA_FUNCSEL,
				  FIELD_PREP(EIC7700_PWDATA_FUNCSEL, value));
}

static int eic7700_pinmux_set_mux(struct pinctrl_dev *pctldev,
				  unsigned int fsel, unsigned int gsel)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *func = pinmux_generic_get_function(pctldev, fsel);
	enum eic7700_muxtype muxtype;

	if (!func)
		return -EINVAL;

	muxtype = (uintptr_t)func->data;
	return eic7700_pinmux_set(ep, ep->desc.pins[gsel].number,
				  eic7700_pad_muxdata(ep->desc.pins[gsel].drv_data),
				  muxtype);
}

static int eic7700_gpio_request_enable(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range,
				       unsigned int offset)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, offset);

	return eic7700_pinmux_set(ep, offset,
				  eic7700_pad_muxdata(desc->drv_data),
				  EIC7700_MUX_GPIO);
}

static int eic7700_gpio_set_direction(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset, bool input)
{
	struct eic7700_pinctrl *ep = pinctrl_dev_get_drvdata(pctldev);

	return eic7700_pwdata_rmw(ep, offset, EIC7700_PWDATA_IE,
				  input ? EIC7700_PWDATA_IE : 0);
}

static const struct pinmux_ops eic7700_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = eic7700_pinmux_set_mux,
	.gpio_request_enable = eic7700_gpio_request_enable,
	.gpio_set_direction = eic7700_gpio_set_direction,
	.strict = true,
};

static int eic7700_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct eic7700_pinctrl *ep;
	int ret;

	ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	ep->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ep->base))
		return PTR_ERR(ep->base);

	ep->desc.name = "eic7700";
	ep->desc.pins = eic7700_pins;
	ep->desc.npins = ARRAY_SIZE(eic7700_pins);
	ep->desc.pctlops = &eic7700_pinctrl_ops;
	ep->desc.pmxops = &eic7700_pinmux_ops;
	ep->desc.confops = &eic7700_pinconf_ops;
	ep->desc.owner = THIS_MODULE;
	raw_spin_lock_init(&ep->lock);

	ret = devm_mutex_init(dev, &ep->mutex);
	if (ret)
		return ret;

	ret = devm_pinctrl_register_and_init(dev, &ep->desc, ep, &ep->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "could not register pinctrl driver\n");

	return pinctrl_enable(ep->pctl);
}

static const struct of_device_id eic7700_pinctrl_of_match[] = {
	{ .compatible = "eswin,eic7700-pinctrl"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, eic7700_pinctrl_of_match);

static struct platform_driver eic7700_pinctrl_driver = {
	.probe = eic7700_pinctrl_probe,
	.driver = {
		.name = "pinctrl-eic7700",
		.of_match_table = eic7700_pinctrl_of_match,
	},
};
module_platform_driver(eic7700_pinctrl_driver);

MODULE_DESCRIPTION("Pinctrl driver for the ESWIN EIC7700 SoC");
MODULE_AUTHOR("Emil Renner Berthing <emil.renner.berthing@canonical.com>");
MODULE_LICENSE("GPL");
