/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef __INNODP_COMMON_H__
#define __INNODP_COMMON_H__

#include "inno_utils.h"

#define DP_AUX_REPLY_ACK	(0x0)
#define DP_AUX_REPLY_NACK	(0x1)
#define DP_AUX_REPLY_DEFER	(0x2)

#define DP_AUX_CHECK_TIME	(20)
#define DP_AUX_CHECK_GAP_MS	(10)

/* AUX CH addresses */
/* DPCD */
#define DP_DPCD_REV		0x000

#define DP_MAX_LINK_RATE	0x001

#define DP_MAX_LANE_COUNT	0x002
#define DP_MAX_LANE_COUNT_MASK	0x1f
#define DP_TPS3_SUPPORTED	(1 << 6) /* 1.2 */
#define DP_ENHANCED_FRAME_CAP	(1 << 7)

#define DP_TRAINING_AUX_RD_INTERVAL	0x00e	/* XXX 1.2? */
#define DP_TRAINING_AUX_RD_MASK		0x7F	/* DP 1.3 */
#define DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT	(1 << 7) /* DP 1.3 */

/* link configuration */
#define	DP_LINK_BW_SET		0x100
#define INNODP_LINK_BW_1_62	(0x06)
#define INNODP_LINK_BW_2_7	(0x0a)
#define INNODP_LINK_BW_5_4	(0x14)
#define INNODP_LINK_BW_8_1	(0x1e)

#define DP_LANE_COUNT_SET		0x101
#define DP_TRAINING_PATTERN_SET		0x102
#define DP_TRAINING_PATTERN_DISABLE	0
#define DP_TRAINING_PATTERN_1		1
#define DP_TRAINING_PATTERN_2		2
#define DP_TRAINING_PATTERN_2_CDS	3	/* 2.0 E11 */
#define DP_TRAINING_PATTERN_3		3	/* 1.2 */
#define DP_TRAINING_PATTERN_4		7	/* 1.4 */
#define DP_TRAINING_PATTERN_MASK	0x3
#define DP_TRAINING_PATTERN_MASK_1_4	0xf

/* DPCD 1.1 only. For DPCD >= 1.2 see per-lane DP_LINK_QUAL_LANEn_SET */
#define DP_LINK_QUAL_PATTERN_11_DISABLE		(0 << 2)
#define DP_LINK_QUAL_PATTERN_11_D10_2		(1 << 2)
#define DP_LINK_QUAL_PATTERN_11_ERROR_RATE	(2 << 2)
#define DP_LINK_QUAL_PATTERN_11_PRBS7		(3 << 2)
#define DP_LINK_QUAL_PATTERN_11_MASK		(3 << 2)

#define DP_RECOVERED_CLOCK_OUT_EN	(1 << 4)
#define DP_LINK_SCRAMBLING_DISABLE	(1 << 5)

#define DP_SYMBOL_ERROR_COUNT_BOTH	(0 << 6)
#define DP_SYMBOL_ERROR_COUNT_DISPARITY	(1 << 6)
#define DP_SYMBOL_ERROR_COUNT_SYMBOL	(2 << 6)
#define DP_SYMBOL_ERROR_COUNT_MASK	(3 << 6)

#define DP_TRAINING_LANE0_SET	0x103
#define DP_TRAINING_LANE1_SET	0x104
#define DP_TRAINING_LANE2_SET	0x105
#define DP_TRAINING_LANE3_SET	0x106

#define DP_TRAIN_VOLTAGE_SWING_MASK	0x3
#define DP_TRAIN_VOLTAGE_SWING_SHIFT	0
#define DP_TRAIN_MAX_SWING_REACHED	(1 << 2)
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_0	(0 << 0)
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_1	(1 << 0)
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_2	(2 << 0)
#define DP_TRAIN_VOLTAGE_SWING_LEVEL_3	(3 << 0)

#define DP_TRAIN_PRE_EMPHASIS_MASK	(3 << 3)
#define DP_TRAIN_PRE_EMPH_LEVEL_0	(0 << 3)
#define DP_TRAIN_PRE_EMPH_LEVEL_1	(1 << 3)
#define DP_TRAIN_PRE_EMPH_LEVEL_2	(2 << 3)
#define DP_TRAIN_PRE_EMPH_LEVEL_3	(3 << 3)

#define DP_DOWNSPREAD_CTRL	0x107
#define DP_SPREAD_AMP_0_5	(1 << 4)
#define DP_FIXED_VTOTAL_AS_SDP_EN_IN_PR_ACTIVE	(1 << 6)
#define DP_MSA_TIMING_PAR_IGNORE_EN	(1 << 7) /* eDP */

#define DP_MAIN_LINK_CHANNEL_CODING_SET	0x108
#define DP_SET_ANSI_8B10B	(1 << 0)
#define DP_SET_ANSI_128B132B	(1 << 1)

#define DP_I2C_SPEED_CONTROL_STATUS	0x109	/* DPI */
/* bitmask as for DP_I2C_SPEED_CAP */

#define DP_EDP_CONFIGURATION_SET	0x10a	/* XXX 1.2? */
#define DP_ALTERNATE_SCRAMBLER_RESET_ENABLE	(1 << 0)
#define DP_FRAMING_CHANGE_ENABLE	(1 << 1)
#define DP_PANEL_SELF_TEST_ENABLE	(1 << 7)

#define DP_LINK_QUAL_LANE0_SET			0x10b	/* DPCD >= 1.2 */
#define DP_LINK_QUAL_LANE1_SET			0x10c
#define DP_LINK_QUAL_LANE2_SET			0x10d
#define DP_LINK_QUAL_LANE3_SET			0x10e
#define DP_LINK_QUAL_PATTERN_DISABLE		0
#define DP_LINK_QUAL_PATTERN_D10_2		1
#define DP_LINK_QUAL_PATTERN_ERROR_RATE		2
#define DP_LINK_QUAL_PATTERN_PRBS7		3
#define DP_LINK_QUAL_PATTERN_80BIT_CUSTOM	4
#define DP_LINK_QUAL_PATTERN_HBR2_EYE		5
#define DP_LINK_QUAL_PATTERN_MASK		7

#define DP_TRAINING_LANE0_1_SET2		0x10f
#define DP_TRAINING_LANE2_3_SET2		0x110
#define DP_LANE02_POST_CURSOR2_SET_MASK		(3 << 0)
#define DP_LANE02_MAX_POST_CURSOR2_REACHED	(1 << 2)
#define DP_LANE13_POST_CURSOR2_SET_MASK		(3 << 4)
#define DP_LANE13_MAX_POST_CURSOR2_REACHED	(1 << 6)

#define DP_MSTM_CTRL				0x111	/* 1.2 */
#define DP_MST_EN				(1 << 0)
#define DP_UP_REQ_EN				(1 << 1)
#define DP_UPSTREAM_IS_SRC			(1 << 2)

#define DP_AUDIO_DELAY0				0x112	/* 1.2 */
#define DP_AUDIO_DELAY1				0x113
#define DP_AUDIO_DELAY2				0x114

#define DP_TRAIN_PRE_EMPHASIS_SHIFT		3
#define DP_TRAIN_MAX_PRE_EMPHASIS_REACHED	(1 << 5)

#define DP_LINK_RATE_SET			0x115	/* eDP 1.4 */
#define DP_LINK_RATE_SET_SHIFT			0
#define DP_LINK_RATE_SET_MASK			(7 << 0)

#define DP_SINK_COUNT				0x200
#define DP_DEVICE_SERVICE_IRQ_VECTOR		0x201
#define DP_REMOTE_CONTROL_COMMAND_PENDING	(1 << 0)
#define DP_AUTOMATED_TEST_REQUEST		(1 << 1)
#define DP_CP_IRQ				(1 << 2)
#define DP_MCCS_IRQ				(1 << 3)
#define DP_DOWN_REP_MSG_RDY			(1 << 4) /* 1.2 MST */
#define DP_UP_REQ_MSG_RDY			(1 << 5) /* 1.2 MST */
#define DP_SINK_SPECIFIC_IRQ			(1 << 6)

#define DP_LANE0_1_STATUS			0x202
#define DP_LANE2_3_STATUS			0x203
#define DP_LANE_CR_DONE				(1 << 0)
#define DP_LANE_CHANNEL_EQ_DONE			(1 << 1)
#define DP_LANE_SYMBOL_LOCKED			(1 << 2)

#define DP_CHANNEL_EQ_BITS (DP_LANE_CR_DONE | \
			    DP_LANE_CHANNEL_EQ_DONE | \
			    DP_LANE_SYMBOL_LOCKED)

#define DP_LANE_ALIGN_STATUS_UPDATED			0x204
#define DP_INTERLANE_ALIGN_DONE				(1 << 0)
#define DP_128B132B_DPRX_EQ_INTERLANE_ALIGN_DONE	(1 << 2) /* 2.0 E11 */
#define DP_128B132B_DPRX_CDS_INTERLANE_ALIGN_DONE	(1 << 3) /* 2.0 E11 */
#define DP_128B132B_LT_FAILED				(1 << 4) /* 2.0 E11 */
#define DP_DOWNSTREAM_PORT_STATUS_CHANGED		(1 << 6)
#define DP_LINK_STATUS_UPDATED				(1 << 7)

#define DP_AUX_I2C_WRITE				0x0
#define DP_AUX_I2C_READ					0x1
#define DP_AUX_I2C_WRITE_STATUS_UPDATE			0x2
#define DP_AUX_I2C_MOT					0x4
#define DP_AUX_NATIVE_WRITE				0x8
#define DP_AUX_NATIVE_READ				0x9

#define DP_SUPPORTED_LINK_RATES				0x010 /* eDP 1.4 */
#define DP_MAX_SUPPORTED_RATES				8	/* 16-bit little-endian */

#define DP_SET_POWER					0x600
#define DP_SET_POWER_D0					0x1
#define DP_SET_POWER_D3					0x2
#define DP_SET_POWER_MASK				0x3

#define DP_EDP_DPCD_REV					0x700	/* eDP 1.2 */
#define DP_EDP_11					0x00
#define DP_EDP_12					0x01
#define DP_EDP_13					0x02
#define DP_EDP_14					0x03

#define DP_REV_10					0x10
#define DP_REV_11					0x11
#define DP_REV_12					0x12
#define DP_REV_13					0x13
#define DP_REV_14					0x14

#define DP_LINK_STATUS_SIZE				6

#define DP_ADJUST_REQUEST_LANE0_1			0x206
#define DP_ADJUST_REQUEST_LANE2_3			0x207
#define DP_ADJUST_VOLTAGE_SWING_LANE0_MASK		0x03
#define DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT		0
#define DP_ADJUST_PRE_EMPHASIS_LANE0_MASK		0x0c
#define DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT		2
#define DP_ADJUST_VOLTAGE_SWING_LANE1_MASK		0x30
#define DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT		4
#define DP_ADJUST_PRE_EMPHASIS_LANE1_MASK		0xc0
#define DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT		6

#define DP_TEST_REQUEST					0x218
#define DP_TEST_LINK_TRAINING				(1 << 0)
#define DP_TEST_LINK_VIDEO_PATTERN			(1 << 1)
#define DP_TEST_LINK_EDID_READ				(1 << 2)
#define DP_TEST_LINK_PHY_TEST_PATTERN			(1 << 3) /* DPCD >= 1.1 */
#define DP_TEST_LINK_FAUX_PATTERN			(1 << 4) /* DPCD >= 1.2 */

#define DP_TEST_LINK_RATE				0x219
#define DP_LINK_RATE_162				(0x6)
#define DP_LINK_RATE_27					(0xa)

#define DP_TEST_LANE_COUNT				0x220

#define DP_TEST_PATTERN					0x221
#define DP_NO_TEST_PATTERN				0x0
#define DP_COLOR_RAMP					0x1
#define DP_BLACK_AND_WHITE_VERTICAL_LINES		0x2
#define DP_COLOR_SQUARE					0x3

#define DP_TEST_H_TOTAL_HI		0x222
#define DP_TEST_H_TOTAL_LO		0x223

#define DP_TEST_V_TOTAL_HI		0x224
#define DP_TEST_V_TOTAL_LO		0x225

#define DP_TEST_H_START_HI		0x226
#define DP_TEST_H_START_LO		0x227

#define DP_TEST_V_START_HI		0x228
#define DP_TEST_V_START_LO		0x229

#define DP_TEST_HSYNC_HI		0x22A
#define DP_TEST_HSYNC_POLARITY		(1 << 7)
#define DP_TEST_HSYNC_WIDTH_HI_MASK	(127 << 0)
#define DP_TEST_HSYNC_WIDTH_LO		0x22B

#define DP_TEST_VSYNC_HI		0x22C
#define DP_TEST_VSYNC_POLARITY		(1 << 7)
#define DP_TEST_VSYNC_WIDTH_HI_MASK	(127 << 0)
#define DP_TEST_VSYNC_WIDTH_LO		0x22D

#define DP_TEST_H_WIDTH_HI		0x22E
#define DP_TEST_H_WIDTH_LO		0x22F

#define DP_TEST_V_HEIGHT_HI		0x230
#define DP_TEST_V_HEIGHT_LO		0x231

#define DP_TEST_MISC0			0x232
#define DP_TEST_SYNC_CLOCK		(1 << 0)
#define DP_TEST_COLOR_FORMAT_MASK	(3 << 1)
#define DP_TEST_COLOR_FORMAT_SHIFT	1
#define DP_COLOR_FORMAT_RGB		(0 << 1)
#define DP_COLOR_FORMAT_YCbCr422	(1 << 1)
#define DP_COLOR_FORMAT_YCbCr444	(2 << 1)
#define DP_TEST_DYNAMIC_RANGE_CEA	(1 << 3)
#define DP_TEST_YCBCR_COEFFICIENTS	(1 << 4)
#define DP_YCBCR_COEFFICIENTS_ITU601	(0 << 4)
#define DP_YCBCR_COEFFICIENTS_ITU709	(1 << 4)
#define DP_TEST_BIT_DEPTH_MASK		(7 << 5)
#define DP_TEST_BIT_DEPTH_SHIFT		5
#define DP_TEST_BIT_DEPTH_6		(0 << 5)
#define DP_TEST_BIT_DEPTH_8		(1 << 5)
#define DP_TEST_BIT_DEPTH_10		(2 << 5)
#define DP_TEST_BIT_DEPTH_12		(3 << 5)
#define DP_TEST_BIT_DEPTH_16		(4 << 5)

#define DP_TEST_MISC1				0x233
#define DP_TEST_REFRESH_DENOMINATOR		(1 << 0)
#define DP_TEST_INTERLACED			(1 << 1)

#define DP_TEST_REFRESH_RATE_NUMERATOR		0x234

#define DP_TEST_CRC_R_CR			0x240
#define DP_TEST_CRC_G_Y				0x242
#define DP_TEST_CRC_B_CB			0x244

#define DP_TEST_SINK_MISC			0x246
#define DP_TEST_CRC_SUPPORTED			(1 << 5)
#define DP_TEST_COUNT_MASK			0xf

#define DP_TEST_RESPONSE			0x260
#define DP_TEST_ACK				(1 << 0)
#define DP_TEST_NAK				(1 << 1)
#define DP_TEST_EDID_CHECKSUM_WRITE		(1 << 2)

#define DP_TEST_EDID_CHECKSUM			0x261

#define DP_TEST_SINK				0x270
#define DP_TEST_SINK_START			(1 << 0)

#define DP_PAYLOAD_TABLE_UPDATE_STATUS		0x2c0	/* 1.2 MST */
#define DP_PAYLOAD_TABLE_UPDATED		(1 << 0)
#define DP_PAYLOAD_ACT_HANDLED			(1 << 1)

#define DP_VC_PAYLOAD_ID_SLOT_1			0x2c1	/* 1.2 MST */
/* up to ID_SLOT_63 at 0x2ff */

#define DP_RECEIVER_CAP_SIZE			0xf

#define DP_AUX_MAX_PAYLOAD_BYTES		16

#define INNODP_HPD_IRQ				(0x31)
#define INNODP_HPD_IN				(0x29)
#define INNODP_HPD_OUT				(0x28)

#define INNODP_PHY_TEST_PATTERN			0x248
#define INNODP_PHY_TEST_PATTERN_SEL_MASK	0x7
#define INNODP_PHY_TEST_PATTERN_NONE		0x0
#define INNODP_PHY_TEST_PATTERN_D10_2		0x1
#define INNODP_PHY_TEST_PATTERN_ERROR_COUNT	0x2
#define INNODP_PHY_TEST_PATTERN_PRBS7		0x3
#define INNODP_PHY_TEST_PATTERN_80BIT_CUSTOM	0x4
#define INNODP_PHY_TEST_PATTERN_CP2520		0x5

#define INNODP_RX_CAP_CHANGED			(1 << 0) /* 1.2 */
#define INNODP_LINK_STATUS_CHANGED		(1 << 1)
#define INNODP_STREAM_STATUS_CHANGED		(1 << 2)
#define INNODP_HDMI_LINK_STATUS_CHANGED		(1 << 3)
#define INNODP_CONNECTED_OFF_ENTRY_REQUESTED	(1 << 4)

#define INNO_MODE(nm, t, c, hd, hss, hse, ht, hsk, vd, vss, vse, vt, vs, f) \
	.name = nm, .status = 0, .type = (t), .clock = (c), \
	.hdisplay = (hd), .hsync_start = (hss), .hsync_end = (hse), \
	.htotal = (ht), .hskew = (hsk), .vdisplay = (vd), \
	.vsync_start = (vss), .vsync_end = (vse), .vtotal = (vt), \
	.vscan = (vs), .flags = (f),
struct inno_dp_compliance_data {
	unsigned long edid;
	uint8_t video_pattern;
	uint16_t hdisplay, vdisplay;
	uint8_t bpc;
	uint8_t phy_pattern;
};

struct inno_dp_compliance {
	unsigned long test_type;
	struct inno_dp_compliance_data test_data;
	bool test_active;
	uint8_t test_link_rate;
	uint8_t test_lane_count;
};

struct aux_cfg {
	uint32_t aux_cmd;
	uint32_t dpcd_addr;
	uint32_t *wr_buff;
	uint32_t *rd_buff;
	uint32_t length;
	uint32_t rd_print;
	uint32_t read;
};

struct dp_chip_t {
	unsigned char lane_rate;
	unsigned char lane_count;
	unsigned char phy_rate;
	unsigned char phy_lanes;
	unsigned char enhance_mode;
	unsigned char dpcd_rev;
	unsigned int lane_swing[4];
	unsigned int lane_emphasis[4];

	/* Displayport compliance testing */
	struct inno_dp_compliance compliance;

	/* sink rates as reported by DP_MAX_LINK_RATE/DP_SUPPORTED_LINK_RATES */
	int max_sink_rates;
	int num_sink_rates;
	int sink_rates[DP_MAX_SUPPORTED_RATES];
	unsigned char cdr_delay;
	unsigned char eq_delay;

	uint8_t dpcd[DP_RECEIVER_CAP_SIZE];
	bool connected;

	uint32_t (*dp_aux_channel_run)(struct aux_cfg *aux_get_rx, struct dp_chip_t *chip);

	void (*dp_training_pattern_set)(struct dp_chip_t *chip, uint32_t pattern);

	void (*dp_link_config)(struct dp_chip_t *chip);

	void (*dp_phy_pattern_update)(struct dp_chip_t *chip, uint8_t *pattern);

	void (*dp_source_swing_adjust)(struct dp_chip_t *chip, int param);

	int (*irq_handle)(struct dp_chip_t *chip);

	void *priv;
};

void inno_dp_compliance_config(struct dp_chip_t *chip);
int inno_dp_read_edid(struct dp_chip_t *chip, uint8_t *buff);
int inno_dp_check_sink_connection(struct dp_chip_t *chip);
int inno_dp_dpcd_write(struct dp_chip_t *chip, unsigned int offset, void *buffer, int size);
int inno_dp_dpcd_read(struct dp_chip_t *chip, unsigned int offset, void *buffer, int size);
int inno_dp_sink_power_ctrl(struct dp_chip_t *chip, bool power_on);
void inno_dp_link_train(struct dp_chip_t *chip);
void inno_dp_link_start(struct dp_chip_t *chip);

#endif /* __INNODP_COMMON_H__ */
