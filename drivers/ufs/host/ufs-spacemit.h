// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit k3 ufs controller driver
 *
 * Copyright (c) 2025, spacemit Corporation.
 *
 */

#ifndef UFS_SPACEMIT_H_
#define UFS_SPACEMIT_H_

#include <linux/reset-controller.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_qos.h>

/* Spacemit K3 UFS host controller vendor specific registers */
#define UFS_SYS1CLK_1US 0xC0
#define UFS_TX_SYMBOL_CLK_NS_US 0xC4
#define UFS_LOCAL_PORT_ID_REG 0xC8
#define UFS_PA_ERR_CODE 0xCC
#define UFS_RETRY_TIMER_REG 0xD0
#define UFS_PA_LINK_STARTUP_TIMER 0xD8
#define UFS_CFG1 0xDC

/* MPHY control registers */
#define UFS_PHY_MNG_BASE 0x1B00
#define UFS_SNPS_PHY_MNG_BASE 0x0B00
#define UFS_MPHY_RST_CTRL 0x0
#define UFS_MPHY_PU_CTRL 0x4
#define UFS_MPHY_BKDR_CTRL 0x8
#define UFS_DEVICE_IO_CTRL 0xC

/* ATOP base*/
#define UFS_ATOP_BASE 0x1C00
#define UFS_SNPS_ATOP_BASE 0x0C00

#define UFS_TX_SYMBO_CLK 0x800
#define UFS_MAX_LINKSTARTUP_TIMER 0xFFFFFFFF
#define UFS_DL_AFC0REQTIMEOUTVAL_MAX 0xFFFF

#define MPHY_TX_FSM_STATE 0x41
#define TX_FSM_HIBERN8 0x1
#define HBRN8_POLL_TOUT_MS 100
#define DEFAULT_CLK_RATE_HZ 1000000
#define BUS_VECTOR_NAME_LEN 32

#define UFS_SPACEMIT_LIMIT_NUM_LANES_RX 2
#define UFS_SPACEMIT_LIMIT_NUM_LANES_TX 2
#define UFS_SPACEMIT_LIMIT_HSGEAR_RX UFS_HS_G3
#define UFS_SPACEMIT_LIMIT_HSGEAR_TX UFS_HS_G3
#define UFS_SPACEMIT_LIMIT_PWMGEAR_RX UFS_PWM_G4
#define UFS_SPACEMIT_LIMIT_PWMGEAR_TX UFS_PWM_G4
#define UFS_SPACEMIT_LIMIT_RX_PWR_PWM SLOW_MODE
#define UFS_SPACEMIT_LIMIT_TX_PWR_PWM SLOW_MODE
#define UFS_SPACEMIT_LIMIT_RX_PWR_HS FAST_MODE
#define UFS_SPACEMIT_LIMIT_TX_PWR_HS FAST_MODE
#define UFS_SPACEMIT_LIMIT_HS_RATE PA_HS_MODE_B
#define UFS_SPACEMIT_LIMIT_DESIRED_MODE 2

#define UFS_PA_VS_CONFIG_REG1 0x9000
#define UFS_DME_VS_CORE_CLK_CTRL 0xD002

/*SNPS host reg*/
#define UFS_HCLKDIV_REG 0xFC

struct gpio_desc;

struct ufs_spacemit_host {
	u32 caps;
	struct ufs_hba *hba;
	struct ufs_pa_layer_attr dev_req_params;
	bool is_lane_clks_on;
	u32 lpm_qos;
	u32 unipro_ver;
	u32 remote_unipro_ver;
	u8 prev_request_crypto;
	struct regulator *ufs_vcc; /* Card power supply */
	struct regulator *ufs_vccq; /* Optional Vccq supply 1.2V */
	struct regulator *ufs_vccq2; /* Optional Vccq2 supply 1.8V*/
	struct reset_control *rst; /* Reset control for UFS AXI */
	int saved_spm_lvl;
};

#define ufs_spacemit_is_link_off(hba) ufshcd_is_link_off(hba)
#define ufs_spacemit_is_link_active(hba) ufshcd_is_link_active(hba)
#define ufs_spacemit_is_link_hibern8(hba) ufshcd_is_link_hibern8(hba)

#endif /* UFS_SPACEMIT_H_ */
