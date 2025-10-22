// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include "inno_dp_common.h"
#include "inno_edid.h"
#include "inno_utils.h"
#include "inno_conn.h"

static int inno_dp_aux_msg(struct dp_chip_t *chip, struct aux_cfg *aux_msg)
{
	int ret = 0;
	uint32_t retry = 0, max_retry = 7;

	if (!chip || !chip->dp_aux_channel_run || !aux_msg)
		return 1;

	for (retry = 0; retry < max_retry; retry++) {
		ret = chip->dp_aux_channel_run(aux_msg, chip);
		if (ret != DP_AUX_REPLY_DEFER)
			break;
		osal_msleep(2);
	}

	return ret;
}

static int inno_dp_aux_transfer(struct dp_chip_t *chip, uint8_t request,
	unsigned int offset, void *buffer, int size)
{
	int ret = 0, j = 0;
	uint32_t wr_buf[4] = {0};
	uint32_t rd_buf[4] = {0};
	struct aux_cfg aux_link;

	if (!chip || size > DP_AUX_MAX_PAYLOAD_BYTES)
		return -1;

	osal_memset(&aux_link, 0, sizeof(aux_link));

	aux_link.wr_buff = wr_buf;
	aux_link.rd_buff = rd_buf;
	aux_link.aux_cmd = request;
	aux_link.dpcd_addr = offset;
	aux_link.length = size ? size - 1 : 0x10;

	switch (request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		aux_link.read = 0;
		if (size && buffer) {
			for (j = 0; j < size; j++)
				aux_link.wr_buff[j / 4] |=
					(((u8 *)buffer)[j] << ((j % 4) * 8));
		}
		ret = inno_dp_aux_msg(chip, &aux_link);
		break;
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		aux_link.read = 1;
		ret = inno_dp_aux_msg(chip, &aux_link);
		if (size && buffer) {
			for (j = 0; j < size; j++)
				((uint8_t *)buffer)[j] =
					(aux_link.rd_buff[j / 4] >> (8 * (j % 4))) & 0xff;
		}
		break;
	default:
		ret = -1;
		break;
	}

	if (ret)
		return -1;
	else
		return size;
}

static int inno_dp_dpcd_access(struct dp_chip_t *chip, uint8_t request,
			       unsigned int offset, void *buffer, int size)
{
	void *msg_buf = NULL;
	int transfer_size = DP_AUX_MAX_PAYLOAD_BYTES;
	int i = 0, msg_size = 0, msg_offset = 0, ret = 0;

	if (!chip || !buffer)
		return -1;

	for (i = 0; i < size; i += msg_size) {
		msg_buf = buffer + i;
		msg_size = min(size - i, transfer_size);
		msg_offset = offset + i;
		ret += inno_dp_aux_transfer(chip, request, msg_offset, msg_buf, msg_size);
	}

	return ret;
}

int inno_dp_dpcd_write(struct dp_chip_t *chip, unsigned int offset, void *buffer, int size)
{
	return inno_dp_dpcd_access(chip, DP_AUX_NATIVE_WRITE, offset, buffer, size);
}

int inno_dp_dpcd_read(struct dp_chip_t *chip, unsigned int offset, void *buffer, int size)
{
	return inno_dp_dpcd_access(chip, DP_AUX_NATIVE_READ, offset, buffer, size);
}

int inno_dp_dpcd_readb(struct dp_chip_t *chip, unsigned int offset, uint8_t *valuep);
int inno_dp_dpcd_readb(struct dp_chip_t *chip, unsigned int offset, uint8_t *valuep)
{
	return inno_dp_dpcd_read(chip, offset, valuep, 1);
}

int inno_dp_dpcd_writeb(struct dp_chip_t *chip, unsigned int offset, uint8_t value);
int inno_dp_dpcd_writeb(struct dp_chip_t *chip, unsigned int offset, uint8_t value)
{
	return inno_dp_dpcd_write(chip, offset, &value, 1);
}

static int inno_dp_i2c_msg_set_request(const struct i2c_msg *i2c_msg)
{
	int request = 0;

	request = (i2c_msg->flags & I2C_M_RD) ?
		DP_AUX_I2C_READ : DP_AUX_I2C_WRITE;
	if (!(i2c_msg->flags & I2C_M_STOP))
		request |= DP_AUX_I2C_MOT;

	return request;
}

static int inno_dp_i2c_xfer(struct dp_chip_t *chip, struct i2c_msg *msgs, int num)
{
	int err = 0;
	void *msg_buf = NULL;
	int transfer_size = DP_AUX_MAX_PAYLOAD_BYTES;
	int i = 0, j = 0, msg_size = 0, msg_address = 0, msg_request = 0;

	for (i = 0; i < num; i++) {
		/* Send a bare address packet to start the transaction.
		 * Zero sized messages specify an address only (bare
		 * address) transaction.
		 */
		msg_address = msgs[i].addr;
		msg_request = inno_dp_i2c_msg_set_request(&msgs[i]);
		msg_buf = NULL;
		msg_size = 0;
		err = inno_dp_aux_transfer(chip, msg_request, msg_address,
					   msg_buf, msg_size);

		if (err < 0)
			break;

		/* We want each transaction to be as large as possible, but
		 * we'll go to smaller sizes if the hardware gives us a
		 * short reply.
		 */
		for (j = 0; j < msgs[i].len; j += msg_size) {
			msg_buf = msgs[i].buf + j;
			msg_size = min(transfer_size, msgs[i].len - j);

			err = inno_dp_aux_transfer(chip, msg_request,
						   msg_address, msg_buf,
						   msg_size);

			if (err < 0)
				break;
		}
		if (err < 0)
			break;
	}

	if (err >= 0)
		err = num;
	/* Send a bare address packet to close out the transaction.
	 * Zero sized messages specify an address only (bare
	 * address) transaction.
	 */
	msg_request &= ~DP_AUX_I2C_MOT;
	msg_buf = NULL;
	msg_size = 0;
	err = inno_dp_aux_transfer(chip, msg_request, msg_address, msg_buf, msg_size);

	return err;
}

static uint8_t dp_link_status(const uint8_t link_status[DP_LINK_STATUS_SIZE], int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

static uint8_t dp_get_lane_status(const uint8_t link_status[DP_LINK_STATUS_SIZE],
				  int lane)
{
	int i = DP_LANE0_1_STATUS + (lane >> 1);
	int s = (lane & 1) * 4;
	uint8_t l = dp_link_status(link_status, i);

	return (l >> s) & 0xf;
}

static uint8_t dp_get_adjust_request_voltage(const uint8_t link_status[DP_LINK_STATUS_SIZE],
				     int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
		 DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	uint8_t l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_VOLTAGE_SWING_SHIFT;
}

static uint8_t dp_get_adjust_request_pre_emphasis(const uint8_t link_status[DP_LINK_STATUS_SIZE],
					  int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
		 DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	uint8_t l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_PRE_EMPHASIS_SHIFT;
}

static bool dp_clock_recovery_ok(const uint8_t link_status[DP_LINK_STATUS_SIZE],
			      int lane_count)
{
	int lane;
	uint8_t lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_LANE_CR_DONE) == 0)
			return false;
	}
	return true;
}

static bool dp_channel_eq_ok(const uint8_t link_status[DP_LINK_STATUS_SIZE],
			  int lane_count)
{
	uint8_t lane_align;
	uint8_t lane_status;
	int lane;

	lane_align = dp_link_status(link_status,
				    DP_LANE_ALIGN_STATUS_UPDATED);
	if ((lane_align & DP_INTERLANE_ALIGN_DONE) == 0)
		return false;
	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_CHANNEL_EQ_BITS) != DP_CHANNEL_EQ_BITS)
			return false;
	}
	return true;
}

static inline bool
dp_enhanced_frame_cap(const uint8_t dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DPCD_REV] >= 0x11 &&
		(dpcd[DP_MAX_LANE_COUNT] & DP_ENHANCED_FRAME_CAP);
}

static void inno_dp_get_adjust_train(struct dp_chip_t *chip,
		const uint8_t link_status[DP_LINK_STATUS_SIZE])
{
	int lane = 0;

	if (!chip)
		return;

	for (lane = 0; lane < chip->lane_count; lane++) {
		chip->lane_swing[lane] = dp_get_adjust_request_voltage(link_status, lane);
		chip->lane_emphasis[lane] = dp_get_adjust_request_pre_emphasis(link_status, lane);
	}
}

static void inno_dp_voltage_swing_adjust(struct dp_chip_t *chip)
{
	int j = 0;
	uint8_t train_set[4];

	if (!chip)
		return;

	for (j = 0; j < chip->lane_count; j++) {
		train_set[j] = chip->lane_swing[j];
		if (chip->lane_swing[j] >= DP_TRAIN_VOLTAGE_SWING_LEVEL_2)
			train_set[j] |= DP_TRAIN_MAX_SWING_REACHED;

		train_set[j] |= (chip->lane_emphasis[j]);
		if (chip->lane_emphasis[j] >= DP_TRAIN_PRE_EMPH_LEVEL_2)
			train_set[j] |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
	}

	inno_dp_dpcd_write(chip, DP_TRAINING_LANE0_SET,
		train_set, chip->lane_count);
}

int inno_dp_sink_power_ctrl(struct dp_chip_t *chip, bool power_on)
{
	uint8_t value = 0;
	int err = 0;

	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (!chip || chip->dpcd[DP_DPCD_REV] < DP_REV_11)
		return 0;

	err = inno_dp_dpcd_read(chip, DP_SET_POWER, &value, 1);
	if (err < 0)
		return err;

	value &= ~DP_SET_POWER_MASK;
	/* Sink - (State3 Sleep):
	 * 1. Hpd asserted
	 * 2. Aux enabled for differential signal monitoring,
	 * 3. Main-link Rx disabled
	 */

	/* Sink - (State 2 standby):
	 * 1. Hpd asserted
	 * 2. Aux enabled for differential signal monitoring,
	 * 3. Main-link Rx enabled
	 */

	value |= (power_on ? DP_SET_POWER_D0 : DP_SET_POWER_D3);

	err = inno_dp_dpcd_write(chip, DP_SET_POWER, &value, 1);
	if (err < 0)
		return err;

	/* According to the DP 1.1 specification, a "Sink Device must exit the
	 * power saving state within 1 ms" (Section 2.5.3.1, Table 5-52, "Sink
	 * Control Field" (register 0x600).
	 */
	if (power_on) {
		/* For an embedded connection, a Sink device may take up to
		 * 20 ms from a power-save mode until it is ready to reply
		 * to an AUX request transaction
		 */
		if (chip->max_sink_rates)
			osal_msleep(20);
		else
			osal_msleep(1);
	}

	return 0;
}

int inno_dp_check_sink_connection(struct dp_chip_t *chip)
{
	uint8_t dpcd_rev = 0x0;
	int i = 0, ret = 0;

	for (i = 0; i < DP_AUX_CHECK_TIME; i++) {
		ret = inno_dp_dpcd_read(chip, DP_DPCD_REV, &dpcd_rev, 1);
		/* Aux reply timeout */
		if (ret <= 0)
			break;

		if (dpcd_rev >= DP_REV_10 && dpcd_rev <= DP_REV_14)
			break;
		ret = -1;

		osal_msleep(DP_AUX_CHECK_GAP_MS);
	}

	osal_printf_func("check %s wait time:%dms ret:%d\n",
			 i >= DP_AUX_CHECK_TIME ? "timeout" : "success",
			 i * DP_AUX_CHECK_GAP_MS, ret);

	return ret;
}

static int inno_bw_table[] = {
	INNODP_LINK_BW_1_62,
	INNODP_LINK_BW_2_7,
	INNODP_LINK_BW_5_4,
	INNODP_LINK_BW_8_1,
};

static int inno_dp_rate_index(const int *rates, int len, int rate)
{
	int i;

	for (i = 0; i < len; i++)
		if (rate == rates[i])
			return i;

	return -1;
}

static int inno_dp_cdr_training(struct dp_chip_t *chip)
{
	int i = 0;
	//uint8_t link_config[9] = {0x0a, 0x82, 0x21, 0x03, 0x03, 0x03, 0x03, 0x0, 0x01};
	uint8_t link_config[2];
	uint8_t link_status[DP_LINK_STATUS_SIZE];

	if (!chip || !chip->connected)
		return false;

	if (chip->dp_training_pattern_set)
		chip->dp_training_pattern_set(chip, DP_TRAINING_PATTERN_1);

	/* Write the link configuration data */
	link_config[0] = chip->lane_rate; /* DP_LINK_BW_SET */
	link_config[1] = chip->lane_count | (chip->enhance_mode << 7); /* DP_LANE_COUNT_SET */
	inno_dp_dpcd_write(chip, DP_LINK_BW_SET, link_config, 2);

	link_config[0] = 0;
	link_config[1] = DP_SET_ANSI_8B10B;
	inno_dp_dpcd_write(chip, DP_DOWNSPREAD_CTRL, link_config, 2);

	link_config[0] = DP_TRAINING_PATTERN_1; /* DP_TRAINING_PATTERN_SET */
	inno_dp_dpcd_write(chip, DP_TRAINING_PATTERN_SET, link_config, 1);

	if (chip->cdr_delay <= 0)
		chip->cdr_delay = 4;
	else
		chip->cdr_delay *= 4;

	for (i = 0; i < 5; i++) {
		osal_msleep(chip->cdr_delay);

		if (inno_dp_dpcd_read(chip, DP_LANE0_1_STATUS, link_status,
				      DP_LINK_STATUS_SIZE) !=
				      DP_LINK_STATUS_SIZE) {
			osal_printf_func("[BAD]failed to get CDR link status\n");
			return false;
		}

		osal_printf_func("CDRlink lane0-1 status:%#.2x lane2-3 status:%#.2x\n",
				 link_status[0], link_status[1]);

		if (dp_clock_recovery_ok(link_status, chip->lane_count)) {
			osal_printf_func("CDR Training Succeeded at %d Loop.\n", i + 1);
			break;
		}

		inno_dp_get_adjust_train(chip, link_status);
		inno_dp_voltage_swing_adjust(chip);
	}

	return true;
}

static int inno_dp_eq_training(struct dp_chip_t *chip)
{
	int i = 0;
	//uint8_t link_config[9] = {0x0a, 0x82, 0x22, 0x03, 0x03, 0x03, 0x03, 0x0, 0x01};
	uint8_t link_config[2];
	uint8_t link_status[DP_LINK_STATUS_SIZE];

	if (!chip || !chip->connected)
		return false;

	if (chip->dp_training_pattern_set)
		chip->dp_training_pattern_set(chip, DP_TRAINING_PATTERN_2);

	/* Write the link configuration data */
	link_config[0] = chip->lane_rate; /* DP_LINK_BW_SET */
	link_config[1] = chip->lane_count | (chip->enhance_mode << 7); /* DP_LANE_COUNT_SET */
	inno_dp_dpcd_write(chip, DP_LINK_BW_SET, link_config, 2);

	link_config[0] = DP_TRAINING_PATTERN_2; /* DP_TRAINING_PATTERN_SET */
	inno_dp_dpcd_write(chip, DP_TRAINING_PATTERN_SET, link_config, 1);

	if (chip->eq_delay <= 0)
		chip->eq_delay = 4;
	else
		chip->eq_delay *= 4;

	for (i = 0; i < 5; i++) {

		osal_msleep(chip->eq_delay);

		if (inno_dp_dpcd_read(chip, DP_LANE0_1_STATUS, link_status,
				      DP_LINK_STATUS_SIZE) != DP_LINK_STATUS_SIZE) {
			osal_printf_func("[BAD]failed to get EQ link status\n");
			return false;
		}
		osal_printf_func("EQ link lane0-1 status:%#.2x lane2-3 status:%#.2x\n",
				 link_status[0], link_status[1]);

		if (dp_channel_eq_ok(link_status, chip->lane_count)) {
			osal_printf_func("EQ Training Succeeded at %d Loop.\n", i + 1);
			break;
		}

		inno_dp_get_adjust_train(chip, link_status);
		inno_dp_voltage_swing_adjust(chip);
	}

	return true;
}

void inno_dp_link_start(struct dp_chip_t *chip)
{
	uint8_t link_config[9] = {0x0a, 0x82, 0x0, 0x03, 0x03, 0x03, 0x03, 0x0, 0x01};

	if (!chip || !chip->connected)
		return;

	if (chip->dp_training_pattern_set)
		chip->dp_training_pattern_set(chip, DP_TRAINING_PATTERN_DISABLE);

	/* Write the link configuration data */
	link_config[0] = chip->lane_rate; /* DP_LINK_BW_SET */
	link_config[1] = chip->lane_count | (chip->enhance_mode << 7); /* DP_LANE_COUNT_SET */
	link_config[2] = DP_TRAINING_PATTERN_DISABLE;
	inno_dp_dpcd_write(chip, DP_LINK_BW_SET, link_config, 9);
}

static bool inno_dp_rate_valid(int lane_rate, int lane_count, int clock, int bpc)
{
	unsigned long requirement, capacity;

	/* bandwidth: rate * lane_count
	 * rate = lane_rate * 27 unit(GBps)
	 * For more detailed information see the DP specification
	 */

	capacity = (unsigned long)lane_rate * 27 * 1000 * 8 * lane_count;
	requirement = (unsigned long)clock * bpc * 3;

	if (capacity >= requirement)
		return true;

	return false;
}

static int inno_dp_rate_adjust(struct dp_chip_t *chip)
{
	struct inno_conn_t *conn = (struct inno_conn_t *)chip->priv;
	struct drm_display_mode *mode = &conn->out_mode;
	uint8_t reduce_lane_rate = 0;
	uint8_t reduce_phy_rate = 0;
	int i = 0;

	i = inno_dp_rate_index(inno_bw_table, ARRAY_SIZE(inno_bw_table), chip->lane_rate);
	if (i > 0 && i < ARRAY_SIZE(inno_bw_table)) {
		reduce_lane_rate = inno_bw_table[i - 1];
		reduce_phy_rate = i - 1;
	} else {
		return -1;
	}

	if (inno_dp_rate_valid(reduce_lane_rate, chip->lane_count,
			       mode->clock, 8) && i >= 0) {
		chip->lane_rate = reduce_lane_rate;
		chip->phy_rate = reduce_phy_rate;
		return 0;
	}

	return -1;
}

static int sink_lane_status_get(struct dp_chip_t *chip, uint8_t status[DP_LINK_STATUS_SIZE])
{
	if (inno_dp_dpcd_read(chip, DP_LANE0_1_STATUS, status,
			      DP_LINK_STATUS_SIZE) == DP_LINK_STATUS_SIZE)
		return 0;

	return -1;
}

void inno_dp_link_train(struct dp_chip_t *chip)
{
	uint8_t link_status[DP_LINK_STATUS_SIZE];

	if (!chip || !chip->connected)
		return;

retry:
	inno_dp_cdr_training(chip);
	inno_dp_eq_training(chip);
	inno_dp_link_start(chip);

	if (!sink_lane_status_get(chip, link_status) &&
		(!dp_clock_recovery_ok(link_status, chip->lane_count) ||
				       !dp_channel_eq_ok(link_status, chip->lane_count))) {
		osal_printf_func("Status not normaly after link start.\n");
		if (chip->dp_link_config && !inno_dp_rate_adjust(chip)) {
			chip->dp_link_config(chip);
			goto retry;
		}
	}
}

static void inno_dp_link_caps_reset(struct dp_chip_t *chip)
{
	if (!chip)
		return;

	chip->cdr_delay = 0;
	chip->eq_delay = 0;
	chip->dpcd_rev = 0;
	chip->enhance_mode = 1;
	chip->max_sink_rates = 0;
	chip->num_sink_rates = 0;
	chip->lane_count = 4;
	chip->lane_rate = INNODP_LINK_BW_5_4;
	chip->connected = false;
}

void inno_dp_compliance_config(struct dp_chip_t *chip)
{
	int ret = 0;
	uint32_t retry = 0, i = 0;
	struct inno_conn_t *conn = (struct inno_conn_t *)chip->priv;

	if (!chip)
		return;

	inno_dp_link_caps_reset(chip);

	for (retry = 0; retry <= 3; retry++) {
		ret = inno_dp_dpcd_read(chip, DP_DPCD_REV, chip->dpcd, DP_RECEIVER_CAP_SIZE);
		if (ret == DP_RECEIVER_CAP_SIZE &&
		    chip->dpcd[DP_DPCD_REV] >= DP_REV_10 &&
		    chip->dpcd[DP_DPCD_REV] <= DP_REV_14) {

			chip->connected = true;
			chip->dpcd_rev = chip->dpcd[DP_DPCD_REV];

			if (chip->compliance.test_type == DP_TEST_LINK_TRAINING) {
				chip->lane_rate = chip->compliance.test_link_rate;
				chip->lane_count = chip->compliance.test_lane_count;
			} else {
				chip->lane_rate = chip->dpcd[DP_MAX_LINK_RATE];
				chip->lane_count = chip->dpcd[DP_MAX_LANE_COUNT] &
							DP_MAX_LANE_COUNT_MASK;
			}

			chip->enhance_mode = dp_enhanced_frame_cap(chip->dpcd) ? 1 : 0;
			chip->cdr_delay = chip->dpcd[DP_TRAINING_AUX_RD_INTERVAL] & 0x7f;
			chip->eq_delay = chip->dpcd[DP_TRAINING_AUX_RD_INTERVAL] & 0x7f;
			break;
		} else if (ret < 0) {
			break;
		}

		osal_msleep(5);
	}

	osal_printf_func("dp sink cap lane rate:%#x, lane count:%#x, enhance_mode:%d\n",
			chip->lane_rate, chip->lane_count, chip->enhance_mode);

	if ((conn->lane_rate == INNODP_LINK_BW_2_7 || conn->lane_rate == INNODP_LINK_BW_1_62 ||
	     conn->lane_rate == INNODP_LINK_BW_5_4) && (conn->lane_rate < chip->lane_rate)) {
		chip->lane_rate  = conn->lane_rate;
	}

	i = inno_dp_rate_index(inno_bw_table, ARRAY_SIZE(inno_bw_table), chip->lane_rate);
	if (i >= 0) {
		chip->phy_rate = i;
	} else {
		chip->lane_rate = INNODP_LINK_BW_5_4;
		chip->phy_rate = 0x2;
	}

	if (chip->lane_count >= 0x1 && chip->lane_count <= 0x4) {
		chip->phy_lanes = chip->lane_count - 1;
	} else {
		chip->lane_count = 0x4;
		chip->phy_lanes = chip->lane_count - 1;
	}

	/* set phy lanes config */
	if (conn->lane_count >= 0x1 && conn->lane_count <= 0x4) {
		chip->phy_lanes = conn->lane_count - 1;
		chip->lane_count = conn->lane_count;
	}
}

static int inno_dp_edid_i2c_aux(struct dp_chip_t *chip, uint8_t *buff)
{
	int ret = 0;
	uint8_t offset = 0;

	struct i2c_msg msg[] = {
		{0x50, 0, 1, &offset},
		{0x50, I2C_M_RD, 256, buff},
	};

	inno_dp_i2c_xfer(chip, msg, ARRAY_SIZE(msg));
	if (inno_edid_is_valid((struct edid *)buff))
		ret = DP_AUX_REPLY_ACK;
	else
		ret = -1;

	return ret;
}

int inno_dp_read_edid(struct dp_chip_t *chip, uint8_t *buff)
{
	int retry = 0;

	if (!chip || !buff)
		return -1;

	for (retry = 0; retry < 2; retry++) {
		if (inno_dp_edid_i2c_aux(chip, buff) == DP_AUX_REPLY_ACK)
			return 0;
	}

	return -1;
}
