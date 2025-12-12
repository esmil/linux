/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_DSI_H_
#define _SPACEMIT_DSI_H_

#include <linux/of.h>
#include <linux/device.h>
#include <linux/types.h>
#include <video/videomode.h>

#include <drm/drm_print.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>

#include "spacemit_lib.h"
#include "spacemit_dphy.h"

/* define LCD_IS_READY to bypass kernel dsi driver */
/* #define LCD_IS_READY */

/*advanced setting*/
#define LPM_FRAME_EN_DEFAULT 0
#define LAST_LINE_TURN_DEFAULT 0
#define HEX_SLOT_EN_DEFAULT 0
#define HSA_PKT_EN_DEFAULT_SYNC_PULSE 1
#define HSA_PKT_EN_DEFAULT_OTHER 0
#define HSE_PKT_EN_DEFAULT_SYNC_PULSE 1
#define HSE_PKT_EN_DEFAULT_OTHER 0
#define HBP_PKT_EN_DEFAULT 1
#define HFP_PKT_EN_DEFAULT 0
#define HEX_PKT_EN_DEFAULT 0
#define HLP_PKT_EN_DEFAULT 0
#define AUTO_DLY_DIS_DEFAULT 0
#define TIMING_CHECK_DIS_DEFAULT 0
#define HACT_WC_EN_DEFAULT 1
#define AUTO_WC_DIS_DEFAULT 0
#define VSYNC_RST_EN_DEFAULT 1
#define DSI_DEFAULT_PHY_FREQ 911000
#define DSI_DEFAULT_ESCAPE_FREQ 52000

#define MAX_TX_CMD_COUNT 300
#define MAX_RX_DATA_COUNT 64

#define DSC_MIN_BPP	(6)
#define DSC_MAX_BPP	(15)

enum spacemit_mipi_burst_mode {
	DSI_BURST_MODE_NON_BURST_SYNC_PULSE = 0,
	DSI_BURST_MODE_NON_BURST_SYNC_EVENT = 1,
	DSI_BURST_MODE_BURST = 2,
	DSI_BURST_MODE_MAX
};

enum spacemit_mipi_input_data_mode {
	DSI_INPUT_DATA_RGB_MODE_565 = 0,
	DSI_INPUT_DATA_RGB_MODE_666PACKET = 1,
	DSI_INPUT_DATA_RGB_MODE_666UNPACKET = 2,
	DSI_INPUT_DATA_RGB_MODE_888 = 3,
	DSI_INPUT_DATA_RGB_MODE_101010 = 4,
	DSI_INPUT_DATA_RGB_MODE_DSC = 5,
	DSI_INPUT_DATA_RGB_MODE_MAX
};

enum spacemit_dsi_work_mode {
	SPACEMIT_DSI_MODE_VIDEO,
	SPACEMIT_DSI_MODE_CMD,
	SPACEMIT_DSI_MODE_MAX
};

enum spacemit_dsi_cmd_type {
	SPACEMIT_DSI_DCS_SWRITE = 0x5,
	SPACEMIT_DSI_DCS_SWRITE1 = 0x15,
	SPACEMIT_DSI_DCS_LWRITE = 0x39,
	SPACEMIT_DSI_DCS_READ = 0x6,
	SPACEMIT_DSI_GENERIC_LWRITE = 0x29,
	SPACEMIT_DSI_GENERIC_READ1 = 0x14,
	SPACEMIT_DSI_SET_MAX_PKT_SIZE = 0x37,
	SPACEMIT_DSI_PICTURE_PARAMETER_SET = 0x0a,
	SPACEMIT_DSI_COMPRESSION_MODE = 0x07,
};

enum spacemit_dsi_tx_mode {
	SPACEMIT_DSI_HS_MODE = 0,
	SPACEMIT_DSI_LP_MODE = 1,
};

enum spacemit_dsi_rx_data_type {
	SPACEMIT_DSI_ACK_ERR_RESP = 0x2,
	SPACEMIT_DSI_EOTP = 0x8,
	SPACEMIT_DSI_GEN_READ1_RESP = 0x11,
	SPACEMIT_DSI_GEN_READ2_RESP = 0x12,
	SPACEMIT_DSI_GEN_LREAD_RESP = 0x1A,
	SPACEMIT_DSI_DCS_READ1_RESP = 0x21,
	SPACEMIT_DSI_DCS_READ2_RESP = 0x22,
	SPACEMIT_DSI_DCS_LREAD_RESP = 0x1C,
};

enum spacemit_dsi_polarity {
	SPACEMIT_DSI_POLARITY_POS = 0,
	SPACEMIT_DSI_POLARITY_NEG,
	SPACEMIT_DSI_POLARITY_MAX
};

enum spacemit_dsi_te_mode {
	SPACEMIT_DSI_TE_MODE_NO = 0,
	SPACEMIT_DSI_TE_MODE_A,
	SPACEMIT_DSI_TE_MODE_B,
	SPACEMIT_DSI_TE_MODE_C,
	SPACEMIT_DSI_TE_MODE_MAX,
};

enum spacemit_dsi_event_id {
	SPACEMIT_DSI_EVENT_ERROR,
	SPACEMIT_DSI_EVENT_MAX,
};

enum spacemit_dsi_status {
	DSI_STATUS_UNINIT = 0,
	DSI_STATUS_OPENED = 1,
	DSI_STATUS_INIT = 2,
	DSI_STATUS_MAX
};

enum spacemit_dsc_bpc {
	DSC_BPC_8 = 8,  /* FORMAT_RGB888 */
	DSC_BPC_10 = 10, /* FORMAT_ARGB2101010 */
};

enum spacemit_dsi_version {
	DSI_VERSION_1 = 0,
	DSI_VERSION_2,
	DSI_VERSION_MAX
};

struct spacemit_dsi_vrr_param {
	uint32_t vrr_vfp;
};

struct spacemit_dsi_info {
	unsigned int cmd_pkt_max_size;
	unsigned int cmd_pkt_default_size;
	unsigned int cmd_fifo_max_level;
	unsigned int cmd_fifo_default_level;
	unsigned int vpn_burst_mode_shift;
};

struct spacemit_dsi_advanced_setting {
	uint32_t lpm_frame_en; /*return to LP mode every frame*/
	uint32_t last_line_turn;
	uint32_t hex_slot_en;
	uint32_t hsa_pkt_en;
	uint32_t hse_pkt_en;
	uint32_t hbp_pkt_en; /*bit:18*/
	uint32_t hfp_pkt_en; /*bit:20*/
	uint32_t hex_pkt_en;
	uint32_t hlp_pkt_en; /*bit:22*/
	uint32_t auto_dly_dis;
	uint32_t timing_check_dis;
	uint32_t hact_wc_en;
	uint32_t auto_wc_dis;
	uint32_t vsync_rst_en;
};

struct spacemit_mipi_info {
	unsigned int height;
	unsigned int width;
	unsigned int hfp; /*pixel*/
	unsigned int hbp;
	unsigned int hsync;
	unsigned int vfp; /*line*/
	unsigned int vbp;
	unsigned int vsync;
	unsigned int fps;
	struct spacemit_dsi_vrr_param vrr_param; /* for variable refresh rate */

	unsigned int work_mode; /*command_mode, video_mode*/
	unsigned int rgb_mode;
	unsigned int lane_number;
	unsigned int phy_bit_clock;
	unsigned int phy_esc_clock;
	unsigned int split_enable;
	unsigned int eotp_enable;

	/*for video mode*/
	unsigned int burst_mode;

	/*for cmd mode*/
	unsigned int te_enable;
	unsigned int vsync_pol;
	unsigned int te_pol;
	unsigned int te_mode;

	/* dsc parameters */
	unsigned int dsc_enable;
	unsigned int dsc_bpc;
	unsigned int dsc_bpp;

	unsigned int cmd_pkg_ctrl;
	unsigned int cmd_pkt_size;
	unsigned int cmd_fifo_level;
	/*The following fields need not be set by panel*/
	unsigned int real_fps;

	/* the following dsi version need to set: dsi version2 */
	unsigned int vpn_tx_dly_cnt;
	unsigned int vpn_dly_cnt;

	unsigned int h_extra;
};

struct spacemit_dsi_cmd_desc {
	enum spacemit_dsi_cmd_type cmd_type;
	uint8_t lp;
	uint32_t delay;	/* time to delay */
	uint32_t length;	/* cmds length */
	uint8_t data[MAX_TX_CMD_COUNT];
};

struct spacemit_dsi_rx_buf {
	enum spacemit_dsi_rx_data_type data_type;
	uint32_t length; /* cmds length */
	uint8_t data[MAX_RX_DATA_COUNT];
};

enum spacemit_dsi_subconnector {
	SPACEMIT_DSI_SUBCONNECTOR_MIPI_DSI   = 0,
	SPACEMIT_DSI_SUBCONNECTOR_HDMI       = 1,
	SPACEMIT_DSI_SUBCONNECTOR_DP         = 2,
	SPACEMIT_DSI_SUBCONNECTOR_eDP        = 3,
};

struct spacemit_dsi_device {
	uint16_t id;
	uint16_t version; /* lark：DSI_VERSION_1, dovenr:DSI_VERSION_2 */
	void __iomem *base_addr;
	struct regmap *apmu_base;
	struct spacemit_dsi_advanced_setting adv_setting;
	struct spacemit_mipi_info mipi_info;
	struct videomode vm;
	struct spacemit_dphy *phy;
	enum spacemit_dsi_status status;
	enum drm_connector_status previous_connector_status;
	enum drm_connector_status connector_status;
	enum spacemit_dsi_subconnector dsi_subconnector;
};

struct dsi_core_ops {
	int (*parse_dt)(struct spacemit_dsi_device *ctx, struct device_node *np);
	int (*isr)(struct spacemit_dsi_device *ctx);
	int (*dsi_open)(struct spacemit_dsi_device *ctx, bool ready);
	int (*dsi_close)(struct spacemit_dsi_device *ctx);
	int (*dsi_write_cmds)(struct spacemit_dsi_device *ctx, struct spacemit_dsi_cmd_desc *cmds, int count);
	int (*dsi_read_cmds)(struct spacemit_dsi_device *ctx, struct spacemit_dsi_rx_buf *dbuf,
								struct spacemit_dsi_cmd_desc *cmds, int count);
	int (*dsi_ready_for_datatx)(struct spacemit_dsi_device *ctx);
	int (*dsi_close_datatx)(struct spacemit_dsi_device *ctx);
	void (*dsi_enable_irq)(struct spacemit_dsi_device *ctx, bool enable);
	void (*dsi_update_vrr)(struct spacemit_dsi_device *ctx, struct spacemit_dsi_vrr_param *vrr_param);
};

struct spacemit_dsi {
	struct device dev;
	struct mipi_dsi_host host;
	struct mipi_dsi_device *slave;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	struct dsi_core_ops *core;
	struct spacemit_dsi_device ctx;
	struct mutex disable_lock;	/* serialize dsi disable and esd recovery */
	bool is_disabled;		/* abort esd when dsi(panel) disabled */
	struct drm_property *panel_name_prop;
	const char *panel_name;
};

extern struct list_head dsi_core_head;

#define dsi_core_ops_register(entry) \
	disp_ops_register(entry, &dsi_core_head)
#define dsi_core_ops_attach(str) \
	disp_ops_attach(str, &dsi_core_head)

#define encoder_to_dsi(encoder) \
	container_of(encoder, struct spacemit_dsi, encoder)
#define host_to_dsi(host) \
	container_of(host, struct spacemit_dsi, host)
#define connector_to_dsi(connector) \
	container_of(connector, struct spacemit_dsi, connector)
#endif /* _SPACEMIT_DSI_H_ */
