// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 */

#include "inno_dp_phy_board.h"
#include "inno_dp_common.h"
#include "inno_dp.h"
#include "inno_utils.h"

void innodp_phyboard_reset(struct inno_conn_t *conn)
{
	/* Set phy test board power down */
	osal_i2c_write8(0x3f04, 0, conn);

	/* Phy board reset */
	osal_i2c_write8(0x3f00, 0, conn);
	osal_usleep(5);
	osal_i2c_write8(0x3f00, 0x3f, conn);

	/* Set analog bypass mode */
	osal_i2c_write8(0x3f30, 0x1, conn);
	osal_i2c_write8(0x3624, 0x0, conn);
}

void innodp_phyboard_set_swinglevel(struct inno_conn_t *conn)
{
	uint32_t temp;

	temp = osal_i2c_read8(0x3021, conn) & 0xff;

	osal_i2c_write8(0x3021, temp | BIT(6) | BIT(7), conn);
	osal_i2c_write8(0x3027, 0x66, conn);
	osal_i2c_write8(0x3028, 0x66, conn);

	osal_i2c_write8(0x302a, 0x1f, conn);
	osal_i2c_write8(0x302b, 0x1f, conn);
	osal_i2c_write8(0x302c, 0x1f, conn);
	osal_i2c_write8(0x302d, 0x1f, conn);

	osal_i2c_write8(0x302e, 0x33, conn);
	osal_i2c_write8(0x302f, 0x33, conn);
	osal_i2c_write8(0x3030, 0x00, conn);
	osal_i2c_write8(0x3031, 0x00, conn);

	osal_i2c_write8(0x3044, 0x28, conn);
	osal_i2c_write8(0x3045, 0x28, conn);
	osal_i2c_write8(0x3046, 0x28, conn);
	osal_i2c_write8(0x3047, 0x28, conn);
}

void innodp_phyboard_link_config(struct inno_conn_t *conn)
{
	/* Set data sync */
	osal_i2c_write8(0x3624, 0x1, conn);

	/* Set phy lanes */
	osal_i2c_write8(0x361c, conn->lane_count - 1, conn);

	/* Set core clock output */
	osal_i2c_write8(0x3f10, 0x84, conn);
}

void innodp_phyboard_set_tps(struct inno_conn_t *conn, uint32_t pattern)
{
	/* Set training pattern */
	osal_i2c_write8(0x3620, pattern, conn);
}

void innodp_phyboard_video_enable(struct inno_conn_t *conn)
{
	osal_i2c_write8(0x3620, 0, conn);
}

int innodp_phyboard_core_pll_cfg(struct dp_chip_t *inno)
{
	struct inno_conn_t *conn = (struct inno_conn_t *)inno->priv;
	uint32_t corepll_lock;

#define pll_prediv     (0)
#define pll_fbdiv      (1)
#define pll_postdiv    (2)
#define pll_clkdiv_16m (3)

	/* for reference clk 27mhz config */
	uint32_t pll_table[][4] = {
		{1, 120,  2, 13}, //1.62g
		{1, 200,  2, 21}, //2.7g
		{1, 400,  2, 42}, //5.4g
	};

	osal_printf_func("pll table phy rate:%d\n", inno->phy_rate);

	if (inno->phy_rate >= ARRAY_SIZE(pll_table) || inno->phy_rate < 0)
		inno->phy_rate = 1;

	/* Turn off frac ctr */
	osal_i2c_write8(0x3000, 0x30, conn);

	/* set prediv */
	osal_i2c_write8(0x3001, pll_table[inno->phy_rate][pll_prediv], conn);

	/* set fbdiv high8bit */
	osal_i2c_write8(0x3002, 0x30 | (pll_table[inno->phy_rate][pll_fbdiv] >> 8), conn);

	/* set fbdiv lower8bit */
	osal_i2c_write8(0x3003, pll_table[inno->phy_rate][pll_fbdiv] & 0xff, conn);

	/* set postdiv */
	osal_i2c_write8(0x3008, (pll_table[inno->phy_rate][pll_postdiv] << 2) | BIT(0), conn);

	osal_i2c_read8(0x300c, conn);
	osal_i2c_write8(0x300c, pll_table[inno->phy_rate][pll_clkdiv_16m], conn);

	osal_msleep(1);

	corepll_lock = (osal_i2c_read8(0x3000, conn) >> 7) & 0x01;
	if (corepll_lock == 1)
		osal_printf_func("[DP] DP core pll output 1/2 lane rate -> lane rate: 0x%02x\n",
				 (inno->lane_rate));
	else
		return -1;

#undef pll_fbdiv
#undef pll_prediv
#undef pll_postdiv
#undef pll_clkdiv_16m

	return 0;
}

void innodp_phyboard_pixel_pll_cfg(struct inno_conn_t *conn, uint32_t index)
{
	/* For ref clk 27Mhz */
	uint32_t g_pll_map[][9] = {
		/* vic, fbdiv, prediv, pclkdiva, pclkdivb, pclkdivc, pclkfrac,
		 * refclksel, targetfreq
		 */
		/* 3840*2160p 60Hz 594MHz */
		{97,  99, 1, 1, 1, 1, 0, 0, INNODP_PCLK_594_00M},
		/* 3840*2160p 30Hz 297MHz */
		{95,  99, 1, 1, 1, 2, 0, 0, INNODP_PCLK_297_00M},
		/* 1600*1200p 60Hz 162MHz */
		{235, 81, 1, 1, 1, 3, 0, 0, INNODP_PCLK_162_00M},
		/* 1920*1080p 60Hz 148.5MHz */
		{16,  88, 1, 8, 0, 1, 0, 0, INNODP_PCLK_148_50M},
		/* 1920*1200p 60Hz 154.12MHz */
		{0,   88, 1, 8, 0, 1, 0, 0, INNODP_PCLK_154_12M},
		/* 1680*1050p 60Hz 146MHz */
		{240, 73, 1, 1, 1, 3, 0, 0, INNODP_PCLK_146_25M},
		/* 1400*1050p 60Hz 122MHz */
		{229, 61, 1, 1, 1, 3, 0, 0, INNODP_PCLK_121_75M},
		/* 1600*900p 60Hz  108MHz */
		{225, 72, 1, 1, 1, 4, 0, 0, INNODP_PCLK_108_00M},
		/* 1400*900p 60Hz */
		{232, 71, 1, 1, 1, 4, 0, 0, INNODP_PCLK_106_50M},
		/* 1366*768p 60Hz 85.5MHz */
		{48,  57, 1, 1, 1, 4, 0, 0, INNODP_PCLK_85_50M},
		/* 1280*800p 60Hz 83.5MHz */
		{220, 167, 1, 1, 1, 12, 0, 0, INNODP_PCLK_83_50M},
		/* 1280*720p 60Hz 74.25MHz */
		{4,   88, 1, 16, 0, 1, 0, 0, INNODP_PCLK_74_25M},
		/* 1024*768p 60Hz 65MHz */
		{120, 65, 1, 1, 2, 4, 0, 0, INNODP_PCLK_65_00M},
		/* 800*600p 60Hz  40MHz */
		{52,  80, 1, 2, 1, 12, 0, 0, INNODP_PCLK_40_00M},
		/* 720*480p 60Hz  27MHz */
		{3,   88, 1, 44, 0, 1, 0, 0, INNODP_PCLK_27_00M},
		/* 640*480p 60Hz  25.175MHz */
		{1,   88, 1, 47, 0, 1, 0, 0, INNODP_PCLK_25_175M},
		/* auto calc */
		{0,   0, 0, 0, 0, 0, 0, 0, INNODP_PCLK_AUTO_CALC},
	};

	osal_printf_func("pll table index: %d  fbdiv:%d\n", index,
			 g_pll_map[index][DP_PLL_FBDIV]);

	/* Set prediv */
	osal_i2c_write8(0x304c, g_pll_map[index][DP_PLL_PREDIV], conn);
	/* Set fbdiv high8 bit */
	osal_i2c_write8(0x304d, (g_pll_map[index][DP_PLL_FBDIV] >> 8) & 0xf, conn);
	/* Set fbdiv low8 bit */
	osal_i2c_write8(0x304e, g_pll_map[index][DP_PLL_FBDIV] & 0xff, conn);
	/* Set div */
	osal_i2c_write8(0x304f, g_pll_map[index][DP_PLL_DIVA], conn);
	osal_i2c_write8(0x3050, g_pll_map[index][DP_PLL_DIVB], conn);
	osal_i2c_write8(0x3051, g_pll_map[index][DP_PLL_DIVC], conn);
	osal_i2c_write8(0x3052, g_pll_map[index][DP_PLL_FRAC], conn);

	osal_msleep(1);
}
