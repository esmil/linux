// SPDX-License-Identifier: GPL-2.0+
/*
 * Fairchild FUSB301 Type-C DRP Port Controller Driver
 *
 * Copyright (C) 2025 Spacemit Corp.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/pm_wakeup.h>
#include <linux/version.h>

#include <linux/workqueue.h>
#include <linux/usb/typec.h>
#include <linux/usb/role.h>

/* Register Map */
#define FUSB301_REG_DEVICEID			0x01
#define FUSB301_REG_MODES			0x02
#define FUSB301_REG_CONTROL			0x03
#define FUSB301_REG_MANUAL			0x04
#define FUSB301_REG_RESET			0x05
#define FUSB301_REG_MASK			0x10
#define FUSB301_REG_STATUS			0x11
#define FUSB301_REG_TYPE			0x12
#define FUSB301_REG_INTERRUPT			0x13

/* Register Bits */
#define FUSB301_DEVICE_ID_VERSION_ID		GENMASK(7, 4)
#define FUSB301_DEVICE_ID_REVISON_ID		GENMASK(3, 0)

#define FUSB301_MODES_MASK			GENMASK(5, 0)
#define   FUSB301_MODES_DRP_ACC			BIT(5)
#define   FUSB301_MODES_DRP			BIT(4)
#define   FUSB301_MODES_SNK_ACC			BIT(3)
#define   FUSB301_MODES_SNK			BIT(2)
#define   FUSB301_MODES_SRC_ACC			BIT(1)
#define   FUSB301_MODES_SRC			BIT(0)

#define FUSB301_CONTROL_TGL_MASK		GENMASK(5, 4)
#define   FUSB301_CONTROL_TGL_35MS		0
#define   FUSB301_CONTROL_TGL_30MS		1
#define   FUSB301_CONTROL_TGL_25MS		2
#define   FUSB301_CONTROL_TGL_20MS		3
#define FUSB301_CONTROL_HOST_CUR_MASK		GENMASK(2, 1)
#define   FUSB301_HOST_CUR_0			0
#define   FUSB301_HOST_CUR_DEFAULT		1
#define   FUSB301_HOST_CUR_1500MA		2
#define   FUSB301_HOST_CUR_3000MA		3
#define FUSB301_CONTROL_INT_MASK		BIT(0)

#define FUSB301_MANUAL_MASK			GENMASK(3, 0)
#define FUSB301_MANUAL_UNATT_SNK		BIT(3)
#define FUSB301_MANUAL_UNATT_SRC		BIT(2)
#define FUSB301_MANUAL_DISABLED			BIT(1)
#define FUSB301_MANUAL_ERR_RECOVERY		BIT(0)

#define FUSB301_RESET_SW_RES			BIT(0)

#define FUSB301_MASK_M_ACC_CH			BIT(3)
#define FUSB301_MASK_M_BCLVL			BIT(2)
#define FUSB301_MASK_M_DETACH			BIT(1)
#define FUSB301_MASK_M_ATTACH			BIT(0)

#define FUSB301_STATUS_ORIENT_MASK		GENMASK(5, 4)
#define   FUSB301_STATUS_ORIENT_FAULT_CC	3
#define   FUSB301_STATUS_ORIENT_CC2		2
#define   FUSB301_STATUS_ORIENT_CC1		1
#define   FUSB301_STATUS_ORIENT_NO_CONN		0
#define FUSB301_STATUS_VBUS_OK			BIT(3)
#define   FUSB301_STATUS_BC_LVL_MASK		GENMASK(2, 1)
#define   FUSB301_STATUS_SNK_0MA		0
#define   FUSB301_STATUS_SNK_DEFAULT		1
#define   FUSB301_STATUS_SNK_1500MA		2
#define   FUSB301_STATUS_SNK_3000MA		3
#define FUSB301_STATUS_ATTACH			BIT(0)

#define FUSB301_TYPE_SNK			BIT(4)
#define FUSB301_TYPE_SRC			BIT(3)
#define FUSB301_TYPE_PWR_ACC			BIT(2)
#define FUSB301_TYPE_DBG_ACC			BIT(1)
#define FUSB301_TYPE_AUD_ACC			BIT(0)
#define FUSB301_TYPE_PWR_DBG_ACC		(FUSB301_TYPE_PWR_ACC | FUSB301_TYPE_DBG_ACC)
#define FUSB301_TYPE_PWR_AUD_ACC		(FUSB301_TYPE_PWR_ACC | FUSB301_TYPE_AUD_ACC)
#define FUSB301_TYPE_INVALID			0

#define FUSB301_INT_ACC				BIT(3)
#define FUSB301_INT_BCLVL			BIT(2)
#define FUSB301_INT_DETACH			BIT(1)
#define FUSB301_INT_ATTACH			BIT(0)

#define FUSB301_REV10				0x10
#define FUSB301_REV11				0x11
#define FUSB301_REV12				0x12

#define FUSB301_MAX_TRY_COUNT			10

#define FUSB301_TRY_TIMEOUT			600
#define FUSB301_CC_DEBOUNCE_TIMEOUT		200

struct fusb301_chip {
	struct device *dev;
	struct regmap *regmap;
	struct workqueue_struct  *cc_wq;
	int irq;

	unsigned int ufp_power;
	unsigned int mode;
	unsigned int dev_id;
	unsigned int type;
	unsigned int state;
	enum typec_orientation orient;
	unsigned int bc_lvl;
	unsigned int pwr_mode;
	unsigned int dttime;
	bool try_snk_emulation;
	bool triedsnk;
	bool try_src_emulation;
	bool triedsrc;
	unsigned int try_attcnt;

	struct work_struct dwork;
	struct delayed_work twork;
	struct mutex mlock;

	struct typec_port *port;
	struct typec_capability cap;
	struct typec_partner *partner;
	struct usb_role_switch *role_sw;

	bool suspend_vbus_off;
};

enum fusb301_state {
	FUSB_STATE_DISABLED,
	FUSB_STATE_ERROR_RECOVERY,
	FUSB_STATE_UNATTACHED_SNK,
	FUSB_STATE_UNATTACHED_SRC,
	FUSB_STATE_ATTACHWAIT_SNK,
	FUSB_STATE_ATTACHWAIT_SRC,
	FUSB_STATE_ATTACHED_SNK,
	FUSB_STATE_ATTACHED_SRC,
	FUSB_STATE_AUDIO_ACCESSORY,
	FUSB_STATE_DEBUG_ACCESSORY,
	FUSB_STATE_TRY_SNK,
	FUSB_STATE_TRYWAIT_SRC,
	FUSB_STATE_TRY_SRC,
	FUSB_STATE_TRYWAIT_SNK,
};

static const char * const fusb301_pwr_mode_name[] = {
	[FUSB301_HOST_CUR_0]		= "none",
	[FUSB301_HOST_CUR_DEFAULT]	= "default",
	[FUSB301_HOST_CUR_1500MA]	= "1.5A",
	[FUSB301_HOST_CUR_3000MA]	= "3.0A",
};

static const char *const fusb301_toggle_name[] = {
	[FUSB301_CONTROL_TGL_35MS]	= "Toggle_35ms",
	[FUSB301_CONTROL_TGL_30MS]	= "Toggle_30ms",
	[FUSB301_CONTROL_TGL_25MS]	= "Toggle_25ms",
	[FUSB301_CONTROL_TGL_20MS]	= "Toggle_20ms",
};

static const char *const fusb301_mode_name[] = {
	[FUSB301_MODES_DRP_ACC]		= "Drp_Acc",
	[FUSB301_MODES_DRP]		= "Drp",
	[FUSB301_MODES_SNK_ACC]		= "Snk_Acc",
	[FUSB301_MODES_SNK]		= "Snk",
	[FUSB301_MODES_SRC_ACC]		= "Src_Acc",
	[FUSB301_MODES_SRC]		= "Src",
};

static const char *const fusb301_state_name[] = {
	[FUSB_STATE_DISABLED]		= "Disabled",
	[FUSB_STATE_ERROR_RECOVERY]	= "Error_Recovery",
	[FUSB_STATE_UNATTACHED_SNK]	= "Unattached_Snk",
	[FUSB_STATE_UNATTACHED_SRC]	= "Unattached_Src",
	[FUSB_STATE_ATTACHWAIT_SNK]	= "AttachWait_Snk",
	[FUSB_STATE_ATTACHWAIT_SRC]	= "AttachWait_Src",
	[FUSB_STATE_ATTACHED_SNK]	= "Attached_Snk",
	[FUSB_STATE_ATTACHED_SRC]	= "Attached_Src",
	[FUSB_STATE_AUDIO_ACCESSORY]	= "Audio_Accessory",
	[FUSB_STATE_DEBUG_ACCESSORY]	= "Debug_Accessory",
	[FUSB_STATE_TRY_SNK]		= "Try_Snk",
	[FUSB_STATE_TRYWAIT_SRC]	= "TryWait_Src",
	[FUSB_STATE_TRY_SRC]		= "Try_Src",
	[FUSB_STATE_TRYWAIT_SNK]	= "TryWait_Snk",
};

#define fusb_update_state(chip, st) \
	if (chip && (st <= FUSB_STATE_TRYWAIT_SNK)) { \
		chip->state = st; \
		dev_info(chip->dev, "%s: %s\n", __func__, fusb301_state_name[st]); \
	}

static void fusb301_detach(struct fusb301_chip *chip);
static void fusb301_set_data_role(struct fusb301_chip *chip,
				  enum typec_data_role data_role,
				  bool attached);

static int fusb301_check_device_id(struct fusb301_chip *chip)
{
	unsigned int device_id;
	int ret = 0;

	ret = regmap_read(chip->regmap, FUSB301_REG_DEVICEID, &device_id);
	if (ret) {
		dev_err(chip->dev, "Failed to read device id: %d\n", ret);
		return ret;
	}
	dev_info(chip->dev, "Device ID = 0x%02x\n", device_id);

	if ((device_id != FUSB301_REV10) &&
	    (device_id != FUSB301_REV11) &&
	    (device_id != FUSB301_REV12))
		return dev_err_probe(chip->dev, -ENODEV,
				     "Device ID not correct 0x%02x\n", device_id);

	chip->dev_id = device_id;
	return 0;
}

static int fusb301_update_status(struct fusb301_chip *chip)
{
	unsigned int ctrl, mode;
	int ret = 0;

	ret = regmap_read(chip->regmap, FUSB301_REG_MODES, &mode);
	if (ret)
		goto out;

	ret = regmap_read(chip->regmap, FUSB301_REG_CONTROL, &ctrl);
	if (ret)
		goto out;

	chip->mode = FIELD_GET(FUSB301_MODES_MASK, mode);
	chip->pwr_mode = FIELD_GET(FUSB301_CONTROL_HOST_CUR_MASK, ctrl);
	chip->dttime = FIELD_GET(FUSB301_CONTROL_TGL_MASK, ctrl);

	dev_info(chip->dev, "mode[0x%02x], host_cur[0x%02x], dttime[0x%02x]\n",
			chip->mode, chip->pwr_mode, chip->dttime);
out:
	return ret;
}

/*
 * spec lets transitioning to below states from any state
 *  FUSB_STATE_DISABLED
 *  FUSB_STATE_ERROR_RECOVERY
 *  FUSB_STATE_UNATTACHED_SNK
 *  FUSB_STATE_UNATTACHED_SRC
 */
static int fusb301_set_chip_state(struct fusb301_chip *chip, enum fusb301_state state)
{
	unsigned int manual;
	int ret;

	switch (state) {
	case FUSB_STATE_DISABLED:
		manual = FUSB301_MANUAL_DISABLED;
		break;
	case FUSB_STATE_ERROR_RECOVERY:
		manual = FUSB301_MANUAL_ERR_RECOVERY;
		break;
	case FUSB_STATE_UNATTACHED_SNK:
		manual = FUSB301_MANUAL_UNATT_SNK;
		break;
	case FUSB_STATE_UNATTACHED_SRC:
		manual = FUSB301_MANUAL_UNATT_SRC;
		break;
	default:
		dev_err(chip->dev, "unexpected state: 0x%02x\n", state);
		manual = FUSB301_MANUAL_ERR_RECOVERY;
		break;
	}
	ret = regmap_write_bits(chip->regmap, FUSB301_REG_MANUAL,
				FUSB301_MANUAL_MASK,
				manual);
	if (ret)
		return ret;

	chip->state = state;
	dev_info(chip->dev, "fusb301 set state: %s\n", fusb301_state_name[state]);

	return 0;
}

static int fusb301_set_mode(struct fusb301_chip *chip, unsigned int mode)
{
	int ret;

	switch (mode) {
	case FUSB301_MODES_DRP_ACC:
	case FUSB301_MODES_DRP:
	case FUSB301_MODES_SNK_ACC:
	case FUSB301_MODES_SNK:
	case FUSB301_MODES_SRC_ACC:
	case FUSB301_MODES_SRC:
		break;
	default:
		dev_err(chip->dev, "unexpected mode: 0x%02x\n", mode);
		return -EINVAL;
	}
	ret = regmap_write_bits(chip->regmap, FUSB301_REG_MODES,
				FUSB301_MODES_MASK,
				mode);
	if (ret)
		return ret;

	chip->mode = mode;
	dev_info(chip->dev, "fusb301 set mode: %s\n", fusb301_mode_name[mode]);

	return 0;
}

/* Set output current indicator */
static int fusb301_set_pwr_mode(struct fusb301_chip *chip, unsigned int pwr_mode)
{
	int ret;

	switch (pwr_mode) {
	case FUSB301_HOST_CUR_0:
	case FUSB301_HOST_CUR_DEFAULT:
	case FUSB301_HOST_CUR_1500MA:
	case FUSB301_HOST_CUR_3000MA:
		break;
	default:
		dev_err(chip->dev, "unexpected pwr mode: 0x%02x\n", pwr_mode);
		return -EINVAL;
	}

	ret = regmap_write_bits(chip->regmap, FUSB301_REG_CONTROL,
				FUSB301_CONTROL_HOST_CUR_MASK,
				FIELD_PREP(FUSB301_CONTROL_HOST_CUR_MASK,
				pwr_mode));
	if (ret)
		return ret;

	chip->pwr_mode = pwr_mode;
	dev_info(chip->dev, "fusb301 set pwr_mode: %s\n", fusb301_pwr_mode_name[pwr_mode]);

	return ret;
}

static int fusb301_set_toggle_time(struct fusb301_chip *chip, unsigned int toggle_time)
{
	int ret;

	switch (toggle_time) {
	case FUSB301_CONTROL_TGL_35MS:
	case FUSB301_CONTROL_TGL_30MS:
	case FUSB301_CONTROL_TGL_25MS:
	case FUSB301_CONTROL_TGL_20MS:
		break;
	default:
		dev_err(chip->dev, "unexpected toggle_time: 0x%02x\n", toggle_time);
		return -EINVAL;
	}

	ret = regmap_write_bits(chip->regmap, FUSB301_REG_CONTROL,
				FUSB301_CONTROL_TGL_MASK,
				FIELD_PREP(FUSB301_CONTROL_TGL_MASK,
				toggle_time));
	if (ret)
		return ret;

	chip->dttime = toggle_time;
	dev_info(chip->dev, "fusb301 set toggle time: %s\n", fusb301_toggle_name[toggle_time]);

	return 0;
}

static int fusb301_init_reg(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;
	int ret;

	/* change current */
	ret = fusb301_set_pwr_mode(chip, FUSB301_HOST_CUR_1500MA);
	if (ret)
		dev_err(cdev, "%s: failed to force dfp power\n",
				__func__);

	/* change toggle time */
	ret = fusb301_set_toggle_time(chip, FUSB301_CONTROL_TGL_35MS);
	if (ret)
		dev_err(cdev, "%s: failed to set toggle time\n",
				__func__);

	/* change mode */
	ret = fusb301_set_mode(chip, FUSB301_MODES_DRP_ACC);
	if (ret)
		dev_err(cdev, "%s: failed to set mode\n",
				__func__);

	/* set error recovery state */
	ret = fusb301_set_chip_state(chip, FUSB_STATE_ERROR_RECOVERY);
	if (ret)
		dev_err(cdev, "%s: failed to set error recovery state\n",
				__func__);
	return ret;
}

static int fusb301_reset_device(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;
	int ret;

	ret = regmap_write(chip->regmap, FUSB301_REG_RESET, FUSB301_RESET_SW_RES);
	if (ret)
		return ret;

	msleep(10);

	ret = fusb301_init_reg(chip);
	if (ret)
		dev_err(cdev, "failed to init reg\n");

	fusb301_detach(chip);

	/* clear global interrupt mask */
	ret = regmap_write_bits(chip->regmap, FUSB301_REG_CONTROL,
				FUSB301_CONTROL_INT_MASK,
				0);
	if (ret) {
		dev_err(cdev, "failed to clear int mask\n");
		return ret;
	}

	ret = fusb301_update_status(chip);
	if (ret)
		dev_err(cdev, "failed to read status\n");

	return ret;
}

static void fusb301_bclvl_changed(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;
	unsigned int status, type;

	if (regmap_read(chip->regmap, FUSB301_REG_STATUS, &status) ||
	    regmap_read(chip->regmap, FUSB301_REG_TYPE, &type)) {
		dev_err(cdev, "%s: failed to read status and type\n", __func__);
		if (fusb301_reset_device(chip))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	dev_dbg(cdev, "sts[0x%02x], type[0x%02x]\n", status, type);
	if (type == FUSB301_TYPE_SRC ||
	    type == FUSB301_TYPE_PWR_AUD_ACC ||
	    type == FUSB301_TYPE_PWR_DBG_ACC ||
	    type == FUSB301_TYPE_PWR_ACC) {
		chip->bc_lvl = status & 0x06;
		chip->bc_lvl = (status & 0x06) >> 1;
	}
}

static void fusb301_acc_changed(struct fusb301_chip *chip)
{
	/* TODO */
	/* implement acc changed work */
}

static void fusb301_src_detected(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;

	if (chip->mode & (FUSB301_MODES_SRC | FUSB301_MODES_SRC_ACC)) {
		dev_err(cdev, "not support in source mode\n");
		if (fusb301_reset_device(chip))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/* SW Try.SRC Workaround below Rev 1.2 */
	if ((!chip->triedsrc) && (chip->mode & (FUSB301_MODES_DRP | FUSB301_MODES_DRP_ACC))) {
		if (fusb301_set_mode(chip, FUSB301_MODES_SRC) ||
		    fusb301_set_chip_state(chip, FUSB_STATE_UNATTACHED_SRC)) {
			dev_err(cdev, "%s: failed to config trySrc\n", __func__);
			if (fusb301_reset_device(chip))
				dev_err(cdev, "%s: failed to reset\n", __func__);
			return;
		}
		fusb_update_state(chip, FUSB_STATE_TRY_SRC);
		chip->triedsrc = true;
		queue_delayed_work(chip->cc_wq, &chip->twork,
				   msecs_to_jiffies(FUSB301_TRY_TIMEOUT));
	} else {
		if (chip->state == FUSB_STATE_TRYWAIT_SNK)
			cancel_delayed_work(&chip->twork);
		fusb_update_state(chip, FUSB_STATE_ATTACHED_SNK);
		fusb301_set_data_role(chip, TYPEC_DEVICE, true);
		chip->type = FUSB301_TYPE_SRC;
	}
}

static void fusb301_snk_detected(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;

	if (chip->mode & (FUSB301_MODES_SNK | FUSB301_MODES_SNK_ACC)) {
		dev_err(cdev, "not support in sink mode\n");
		if (fusb301_reset_device(chip))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/* SW Try.SNK Workaround below Rev 1.2 */
	if ((!chip->triedsnk) && (chip->mode & (FUSB301_MODES_DRP | FUSB301_MODES_DRP_ACC))) {
		if (fusb301_set_mode(chip, FUSB301_MODES_SNK) ||
		    fusb301_set_chip_state(chip, FUSB_STATE_UNATTACHED_SNK)) {
			dev_err(cdev, "%s: failed to config trySnk\n", __func__);
			if (fusb301_reset_device(chip))
				dev_err(cdev, "%s: failed to reset\n", __func__);
			return;
		}
		fusb_update_state(chip, FUSB_STATE_TRY_SNK);
		chip->triedsnk = true;
		queue_delayed_work(chip->cc_wq, &chip->twork,
				   msecs_to_jiffies(FUSB301_TRY_TIMEOUT));
	} else {
		/*
		 * chip->triedsnk == true
		 * or
		 * mode == FUSB301_MODES_SRC/FUSB301_MODES_SRC_ACC
		 */
		fusb301_set_pwr_mode(chip, FUSB301_HOST_CUR_DEFAULT);
		if (chip->state == FUSB_STATE_TRYWAIT_SRC)
			cancel_delayed_work(&chip->twork);
		fusb_update_state(chip, FUSB_STATE_ATTACHED_SRC);
		fusb301_set_data_role(chip, TYPEC_HOST, true);
		chip->type = FUSB301_TYPE_SNK;
	}
}

static void fusb301_dbg_acc_detected(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;

	if (chip->mode & (FUSB301_MODES_SRC | FUSB301_MODES_SNK | FUSB301_MODES_DRP)) {
		dev_err(cdev, "not support accessory mode\n");
		if (fusb301_reset_device(chip))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/*
	 * TODO
	 * need to implement
	 */
	fusb_update_state(chip, FUSB_STATE_DEBUG_ACCESSORY);
}

static void fusb301_aud_acc_detected(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;

	if (chip->mode & (FUSB301_MODES_SRC | FUSB301_MODES_SNK | FUSB301_MODES_DRP)) {
		dev_err(cdev, "not support accessory mode\n");
		if (fusb301_reset_device(chip))
			dev_err(cdev, "%s: failed to reset\n", __func__);
		return;
	}

	/*
	 * TODO
	 * need to implement
	 */
	fusb_update_state(chip, FUSB_STATE_AUDIO_ACCESSORY);
}

static void fusb301_timer_try_expired(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;

	if (chip->state == FUSB_STATE_TRY_SNK) {
		if (fusb301_set_mode(chip, FUSB301_MODES_SRC) ||
		    fusb301_set_chip_state(chip, FUSB_STATE_UNATTACHED_SRC)) {
			dev_err(cdev, "%s: failed to config tryWaitSrc\n", __func__);
			if (fusb301_reset_device(chip))
				dev_err(cdev, "%s: failed to reset\n", __func__);
			return;
		}

		fusb_update_state(chip, FUSB_STATE_TRYWAIT_SRC);
		queue_delayed_work(chip->cc_wq, &chip->twork,
				   msecs_to_jiffies(FUSB301_CC_DEBOUNCE_TIMEOUT));
	} else if (chip->state == FUSB_STATE_TRY_SRC) {
		if (fusb301_set_mode(chip, FUSB301_MODES_SNK) ||
		    fusb301_set_chip_state(chip, FUSB_STATE_UNATTACHED_SNK)) {
			dev_err(cdev, "%s: failed to config tryWaitSnk\n", __func__);
			if (fusb301_reset_device(chip))
				dev_err(cdev, "%s: failed to reset\n", __func__);
			return;
		}

		fusb_update_state(chip, FUSB_STATE_TRYWAIT_SNK);
		queue_delayed_work(chip->cc_wq, &chip->twork,
				   msecs_to_jiffies(FUSB301_CC_DEBOUNCE_TIMEOUT));
	}
}

static void fusb301_detach(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;

	dev_dbg(cdev, "%s: type[0x%02x] chipstate[0x%02x]\n",
		__func__, chip->type, chip->state);

	switch (chip->state) {
	case FUSB_STATE_ATTACHED_SRC:
		fusb301_set_pwr_mode(chip, FUSB301_HOST_CUR_1500MA);
		fusb301_set_data_role(chip, TYPEC_DEVICE, false);
		break;
	case FUSB_STATE_ATTACHED_SNK:
		fusb301_set_data_role(chip, TYPEC_HOST, false);
		break;
	case FUSB_STATE_DEBUG_ACCESSORY:
	case FUSB_STATE_AUDIO_ACCESSORY:
		break;
	case FUSB_STATE_TRY_SNK:
	case FUSB_STATE_TRYWAIT_SRC:
		cancel_delayed_work(&chip->twork);
		break;
	case FUSB_STATE_DISABLED:
	case FUSB_STATE_ERROR_RECOVERY:
		break;
	case FUSB_STATE_TRY_SRC:
	case FUSB_STATE_TRYWAIT_SNK:
		cancel_delayed_work(&chip->twork);
		break;
	default:
		dev_err(cdev, "%s: Invalid chipstate[0x%02x]\n", __func__, chip->state);
		break;
	}

	if ((chip->triedsnk && chip->try_snk_emulation) ||
	    (chip->triedsrc && chip->try_src_emulation)) {
		if (fusb301_set_mode(chip, FUSB301_MODES_DRP_ACC) ||
		    fusb301_set_chip_state(chip, FUSB_STATE_ERROR_RECOVERY)) {
			dev_err(cdev, "%s: failed to set init mode\n", __func__);
		}
	}
	chip->type = FUSB301_TYPE_INVALID;
	chip->bc_lvl = FUSB301_STATUS_SNK_0MA;
	chip->ufp_power = 0;
	chip->triedsnk = !chip->try_snk_emulation;
	chip->triedsrc = !chip->try_src_emulation;
	chip->try_attcnt = 0;
	fusb_update_state(chip, FUSB_STATE_ERROR_RECOVERY);
}

static bool fusb301_is_vbus_off(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;
	unsigned int status;
	int ret;

	ret = regmap_read(chip->regmap, FUSB301_REG_STATUS, &status);
	if (ret) {
		dev_err(cdev, "%s: failed to read status\n", __func__);
		return false;
	}

	return !((status & FUSB301_STATUS_ATTACH) && (ret & FUSB301_STATUS_VBUS_OK));
}

static bool fusb301_is_vbus_on(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;
	unsigned int status;
	int ret;

	ret = regmap_read(chip->regmap, FUSB301_REG_STATUS, &status);
	if (ret) {
		dev_err(cdev, "%s: failed to read status\n", __func__);
		return false;
	}
	return !!(status & FUSB301_STATUS_VBUS_OK);
}

/* workaround BC Level detection plugging slowly with C ot A on Rev1.0 */
static bool fusb301_bclvl_detect_wa(struct fusb301_chip *chip,
				    unsigned int status, unsigned int type)
{
	struct device *cdev = chip->dev;
	int ret;

	if (((type == FUSB301_TYPE_SRC) ||
		((type == FUSB301_TYPE_INVALID) && (status & FUSB301_STATUS_VBUS_OK))) &&
		!(status & FUSB301_STATUS_BC_LVL_MASK) &&
		(chip->try_attcnt < FUSB301_MAX_TRY_COUNT)) {
		ret = fusb301_set_chip_state(chip, FUSB_STATE_ERROR_RECOVERY);
		if (ret) {
			dev_err(cdev, "%s: failed to set error recovery state\n",
					__func__);
			goto err;
		}
		chip->try_attcnt++;
		msleep(100);
		/*
		 * when cable is unplug during bc level workaournd,
		 * detach interrupt does not occur
		 */
		if (fusb301_is_vbus_off(chip)) {
			chip->try_attcnt = 0;
			dev_info(cdev, "%s: vbus is off\n", __func__);
		}
		return true;
	}
err:
	chip->try_attcnt = 0;
	return false;
}

static int fusb301_get_cc_orientation(struct fusb301_chip *chip)
{
	enum typec_orientation orientation;
	unsigned int status;
	int ret;

	ret = regmap_read(chip->regmap, FUSB301_REG_STATUS, &status);
	if (ret)
		return ret;
	switch (FIELD_GET(FUSB301_STATUS_ORIENT_MASK, status)) {
	case FUSB301_STATUS_ORIENT_CC1:
		orientation = TYPEC_ORIENTATION_NORMAL;
		break;
	case FUSB301_STATUS_ORIENT_CC2:
		orientation = TYPEC_ORIENTATION_REVERSE;
		break;
	default:
		orientation = TYPEC_ORIENTATION_NONE;
		break;
	}

	typec_set_orientation(chip->port, orientation);
	chip->orient = orientation;
	dev_info(chip->dev, "get orientation: %d\n", orientation);
	return ret;
}

static void fusb301_attach(struct fusb301_chip *chip)
{
	struct device *cdev = chip->dev;
	int ret;
	unsigned int status, type;

	/* get status and type */
	ret = regmap_read(chip->regmap, FUSB301_REG_STATUS, &status) ||
	      regmap_read(chip->regmap, FUSB301_REG_TYPE, &type);
	if (ret) {
		dev_err(cdev, "%s: failed to read status and type\n", __func__);
		return;
	}

	dev_info(cdev, "sts[0x%02x], type[0x%02x]\n", status, type);

	if ((chip->state != FUSB_STATE_ERROR_RECOVERY) &&
	    (chip->state != FUSB_STATE_TRY_SNK) &&
	    (chip->state != FUSB_STATE_TRYWAIT_SRC) &&
	    (chip->state != FUSB_STATE_TRY_SRC) &&
	    (chip->state != FUSB_STATE_TRYWAIT_SNK)) {
		dev_err(cdev, "%s: Invalid chipstate[0x%02x]\n", __func__, chip->state);
		ret = fusb301_set_chip_state(chip, FUSB_STATE_ERROR_RECOVERY);
		if (ret)
			dev_err(cdev, "%s: failed to set error recovery\n",
					__func__);
		fusb301_detach(chip);
		return;
	}

	ret = fusb301_get_cc_orientation(chip);
	if (ret) {
		dev_err(cdev, "%s: failed to get cc orientation\n", __func__);
		return;
	}

	if ((chip->dev_id == FUSB301_REV10) &&
		fusb301_bclvl_detect_wa(chip, status, type)) {
		return;
	}

	switch (type) {
	case FUSB301_TYPE_SRC:
		fusb301_src_detected(chip);
		break;
	case FUSB301_TYPE_SNK:
		fusb301_snk_detected(chip);
		break;
	case FUSB301_TYPE_PWR_ACC:
		/*
		 * just power without functional dbg/aud determination
		 * ideally should not happen
		 */
		chip->type = type;
		break;
	case FUSB301_TYPE_DBG_ACC:
	case FUSB301_TYPE_PWR_DBG_ACC:
		fusb301_dbg_acc_detected(chip);
		chip->type = type;
		break;
	case FUSB301_TYPE_AUD_ACC:
	case FUSB301_TYPE_PWR_AUD_ACC:
		fusb301_aud_acc_detected(chip);
		chip->type = type;
		break;
	case FUSB301_TYPE_INVALID:
		fusb301_detach(chip);
		dev_err(cdev, "%s: Invalid type[0x%02x]\n", __func__, type);
		break;
	default:
		ret = fusb301_set_chip_state(chip, FUSB_STATE_ERROR_RECOVERY);
		if (ret)
			dev_err(cdev, "%s: failed to set error recovery\n", __func__);
		fusb301_detach(chip);
		dev_err(cdev, "%s: Unknwon type[0x%02x]\n", __func__, type);
		break;
	}
}

static void fusb301_timer_work_handler(struct work_struct *work)
{
	struct fusb301_chip *chip = container_of(work, struct fusb301_chip, twork.work);
	struct device *cdev = chip->dev;
	unsigned int type;

	mutex_lock(&chip->mlock);
	if (chip->state == FUSB_STATE_TRY_SNK) {
		if (fusb301_is_vbus_on(chip)) {
			if (fusb301_set_mode(chip, FUSB301_MODES_DRP_ACC))
				dev_err(cdev, "%s: failed to set init mode\n", __func__);
			chip->triedsnk = !chip->try_snk_emulation;
			mutex_unlock(&chip->mlock);
			return;
		}
		fusb301_timer_try_expired(chip);
	} else if (chip->state == FUSB_STATE_TRY_SRC) {
		if (regmap_read(chip->regmap, FUSB301_REG_TYPE, &type)) {
			dev_err(cdev, "%s: failed to read type\n", __func__);
			mutex_unlock(&chip->mlock);
			return;
		}
		if (type & FUSB301_TYPE_SNK) {
			if (fusb301_set_mode(chip, FUSB301_MODES_DRP_ACC))
				dev_err(cdev, "%s: failed to set init mode\n", __func__);
			chip->triedsrc = !chip->try_src_emulation;
			mutex_unlock(&chip->mlock);
			return;
		}
		fusb301_timer_try_expired(chip);
	} else if (chip->state == FUSB_STATE_TRYWAIT_SRC ||
		   chip->state == FUSB_STATE_TRYWAIT_SNK) {
		fusb301_detach(chip);
	}
	mutex_unlock(&chip->mlock);
}

static void fusb301_work_handler(struct work_struct *work)
{
	struct fusb301_chip *chip = container_of(work, struct fusb301_chip, dwork);
	struct device *cdev = chip->dev;
	int ret;
	unsigned int int_sts;

	mutex_lock(&chip->mlock);

	ret = regmap_read(chip->regmap, FUSB301_REG_INTERRUPT, &int_sts);
	if (ret) {
		dev_err(cdev, "%s: fusb301 failed to read REG_INT\n", __func__);
		goto unlock;
	}
	dev_info(cdev, "%s: int_sts[0x%02x]\n", __func__, int_sts);
	if (int_sts & FUSB301_INT_DETACH) {
		fusb301_detach(chip);
	} else {
		if (int_sts & FUSB301_INT_ATTACH)
			fusb301_attach(chip);
		if (int_sts & FUSB301_INT_BCLVL)
			fusb301_bclvl_changed(chip);
		if (int_sts & FUSB301_INT_ACC)
			fusb301_acc_changed(chip);
	}
unlock:
	mutex_unlock(&chip->mlock);
}

static irqreturn_t fusb301_irq_handler(int irq, void *data)
{
	struct fusb301_chip *chip = (struct fusb301_chip *)data;

	queue_work(chip->cc_wq, &chip->dwork);
	return IRQ_HANDLED;
}

static void fusb301_set_data_role(struct fusb301_chip *chip,
				  enum typec_data_role data_role,
				  bool attached)
{
	enum usb_role usb_role = USB_ROLE_NONE;

	if (attached) {
		if (data_role == TYPEC_HOST)
			usb_role = USB_ROLE_HOST;
		else
			usb_role = USB_ROLE_DEVICE;
	}

	usb_role_switch_set_role(chip->role_sw, usb_role);
	typec_set_data_role(chip->port, data_role);
}

static int fusb301_dr_set(struct typec_port *port, enum typec_data_role role)
{
	struct fusb301_chip *chip = typec_get_drvdata(port);

	fusb301_set_data_role(chip, role, true);

	return 0;
}

static const struct typec_operations fusb301_ops = {
	.dr_set = fusb301_dr_set
};

static int fusb301_typec_port_probe(struct fusb301_chip *chip)
{
	struct typec_capability *cap = &chip->cap;
	struct fwnode_handle *connector, *ep;
	struct device *dev = chip->dev;
	int ret;

	connector = device_get_named_child_node(dev, "connector");
	if (connector) {
		chip->role_sw = fwnode_usb_role_switch_get(connector);
	} else {
		ep = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
		if (!ep)
			return -ENODEV;
		connector = fwnode_graph_get_remote_port_parent(ep);
		fwnode_handle_put(ep);
		if (!connector)
			return -ENODEV;
		chip->role_sw = fwnode_usb_role_switch_get(connector);
	}

	if (IS_ERR(chip->role_sw)) {
		dev_err(dev, "fail to get role sw\n");
		ret = PTR_ERR(chip->role_sw);
		goto err_put_fwnode;
	}

	ret = typec_get_fw_cap(cap, connector);
	if (ret)
		goto err_put_role;

	cap->revision = USB_TYPEC_REV_1_1;
	cap->ops = &fusb301_ops;
	cap->driver_data = chip;

	chip->port = typec_register_port(dev, cap);
	if (IS_ERR(chip->port)) {
		dev_err(dev, "fail to register typec port\n");
		ret = PTR_ERR(chip->port);
		goto err_put_role;
	}

	fwnode_handle_put(connector);
	return 0;

err_put_role:
	usb_role_switch_put(chip->role_sw);
err_put_fwnode:
	fwnode_handle_put(connector);
	return ret;
}

static void fusb301_get_gpio_irq(struct fusb301_chip *chip)
{
	struct gpio_desc *irq_gpiod;

	irq_gpiod = devm_gpiod_get(chip->dev, "irq", GPIOD_IN);
	if (IS_ERR_OR_NULL(irq_gpiod)) {
		dev_err(chip->dev, "no interrupt gpio property\n");
		return;
	}

	chip->irq = gpiod_to_irq(irq_gpiod);
	if (chip->irq < 0)
		dev_err(chip->dev, "failed to get GPIO IRQ\n");
}

static const struct regmap_config config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xFF,
};

static int fusb301_probe(struct i2c_client *client)
{
	struct fusb301_chip *chip;
	struct device *cdev = &client->dev;
	int ret;

	chip = devm_kzalloc(cdev, sizeof(struct fusb301_chip), GFP_KERNEL);
	if (!chip) {
		dev_err(cdev, "can't alloc fusb301_chip\n");
		return -ENOMEM;
	}

	chip->dev = cdev;
	i2c_set_clientdata(client, chip);

	chip->regmap = devm_regmap_init_i2c(client, &config);
	if (IS_ERR(chip->regmap)) {
		dev_err(cdev, "fail to init i2c regmap.\n");
		return PTR_ERR(chip->regmap);
	}

	ret = fusb301_check_device_id(chip);
	if (ret < 0) {
		dev_err(cdev, "fusb301 not found\n");
		return -ENODEV;
	}

	ret = fusb301_typec_port_probe(chip);
	if (ret) {
		dev_err(cdev, "fail to probe typec property.\n");
		return ret;
	}
	chip->suspend_vbus_off = device_property_read_bool(cdev, "suspend-vbus-off");

	chip->type = FUSB301_TYPE_INVALID;
	chip->state = FUSB_STATE_ERROR_RECOVERY;
	chip->bc_lvl = FUSB301_STATUS_SNK_0MA;
	chip->ufp_power = 0;

	if (chip->cap.prefer_role == TYPEC_SOURCE) {
		chip->try_src_emulation = true;
		chip->triedsrc = !chip->try_src_emulation;
	} else {
		chip->try_snk_emulation = true;
		chip->triedsnk = !chip->try_snk_emulation;
	}

	chip->try_attcnt = 0;
	chip->cc_wq = alloc_ordered_workqueue("fusb301-wq", WQ_HIGHPRI);
	if (!chip->cc_wq)
		goto unregister_port;

	INIT_WORK(&chip->dwork, fusb301_work_handler);
	INIT_DELAYED_WORK(&chip->twork, fusb301_timer_work_handler);
	mutex_init(&chip->mlock);

	chip->irq = client->irq;
	if (!chip->irq)
		fusb301_get_gpio_irq(chip);

	if (!chip->irq) {
		dev_err(cdev, "fail to get interrupt IRQ\n");
		goto unregister_port;
	}

	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
					fusb301_irq_handler,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT | IRQF_NO_SUSPEND,
					"fusb301_int_irq", chip);
	if (ret) {
		dev_err(cdev, "fail to request IRQ %d: %d\n",
			chip->irq, ret);
		goto destroy_workqueue;
	}

	ret = fusb301_reset_device(chip);
	if (ret) {
		dev_err(cdev, "failed to initialize\n");
		goto destroy_workqueue;
	}

	return 0;

destroy_workqueue:
	destroy_workqueue(chip->cc_wq);
unregister_port:
	typec_unregister_port(chip->port);
	usb_role_switch_put(chip->role_sw);
	return ret;
}

static void fusb301_remove(struct i2c_client *client)
{
	struct fusb301_chip *chip = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&chip->twork);
	cancel_work_sync(&chip->dwork);
	destroy_workqueue(chip->cc_wq);
	typec_unregister_port(chip->port);
	usb_role_switch_put(chip->role_sw);
}

static int __maybe_unused fusb301_pm_suspend(struct device *dev)
{
	struct fusb301_chip *chip = dev_get_drvdata(dev);

	flush_work(&chip->dwork);
	flush_delayed_work(&chip->twork);

	if (chip->suspend_vbus_off)
		fusb301_set_chip_state(chip, FUSB_STATE_DISABLED);

	return 0;
}

static int __maybe_unused fusb301_pm_resume(struct device *dev)
{
	struct fusb301_chip *chip = dev_get_drvdata(dev);

	if (chip->suspend_vbus_off)
		fusb301_set_chip_state(chip, FUSB_STATE_ERROR_RECOVERY);

	schedule_work(&chip->dwork);

	return 0;
}

static const struct dev_pm_ops fusb301_dev_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(fusb301_pm_suspend, fusb301_pm_resume)
};

static const struct of_device_id fusb301_match_table[] = {
	{ .compatible = "onsemi,fusb301", },
	{ },
};
MODULE_DEVICE_TABLE(of, fusb301_match_table);

static struct i2c_driver fusb301_i2c_driver = {
	.driver = {
		.name = "fusb301",
		.owner = THIS_MODULE,
		.of_match_table = fusb301_match_table,
		.pm = &fusb301_dev_pm_ops,
	},
	.probe = fusb301_probe,
	.remove = fusb301_remove,
};

module_i2c_driver(fusb301_i2c_driver)

MODULE_DESCRIPTION("I2C bus driver for fusb301 USB Type-C");
MODULE_LICENSE("GPL v2");
