// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include "post_process_hee.h"
#include "../spacemit_dpu_reg.h"
#include "saturn_regs/reg_map_hee.h"

void saturn_hee_conf_dpuctrl_color_matrix(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct drm_crtc_state *state = a_crtc->crtc.state;
	struct spacemit_crtc_state *spacemit_state = to_spacemit_crtc_state(state);
	struct drm_property_blob *blob = spacemit_state->color_matrix_blob_prop;
	int *color_matrix;

	/*
	 * For color matrix, if no update from user space,
	 * we keep the original configuration, do not change the value of any color matrix register
	 */
	if (blob) {
		color_matrix = (int *)blob->data;

		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_npost_proc_en, 1);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_nendmatrix_en, 1);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_ngain_to_full_en, 0);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_nmatrix_en, 0);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_nfront_tmootf_en, 0);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_nend_tmootf_en, 0);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_neotf_en, 0);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_noetf_en, 0);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_table0, color_matrix[0]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_table1, color_matrix[1]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_table2, color_matrix[2]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_table3, color_matrix[3]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_table4, color_matrix[4]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_table5, color_matrix[5]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_table6, color_matrix[6]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_table7, color_matrix[7]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_table8, color_matrix[8]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_offset0, color_matrix[9]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_offset1, color_matrix[10]);
		dpu_write_reg(hwdev, LTM_REG, LTM_BASE_ADDR, m_pendmatrix_offset2, color_matrix[11]);
	}

}

int saturn_hee_check_end_matrix(struct drm_crtc_state *state)
{
	struct spacemit_crtc_state *ac = to_spacemit_crtc_state(state);
	struct drm_property_blob *blob = ac->color_matrix_blob_prop;
	int *data;
	int n;

	if (blob) {
		data = (int *)blob->data;
		for (n = 0; n < 9; n++) {
			if ((data[n] > 8191) || (data[n] < -8192)) {
				DRM_DEBUG("The value of color matrix coeffs is invalid: value %d, n %d\n", data[n], n);
				return -EINVAL;
			}
			data[n] = data[n] & 0xFFFF;
		}

		for (n = 9; n < 12; n++) {
			//due to silicon limitation, LARK L offset value should be -1024 ~ 1023
			if (data[n] > 1023) {
				DRM_DEBUG("The value of color matrix offset : value %d, n %d\n", data[n], n);
				data[n] = 1023;
			}
			if ((data[n] < -1024)) {
				DRM_DEBUG("The value of color matrix offset : value %d, n %d\n", data[n], n);
				data[n] = -1024;
			}
			//13 bit REG fihee for LARK M, althogh the range is from -1024 ~ 1023
			data[n] = data[n] & 0x1FFF;
		}
	}

	return 0;
}

void saturn_hee_conf_dpuctrl_ee(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct drm_crtc_state *state = a_crtc->crtc.state;
	struct spacemit_crtc_state *spacemit_state = to_spacemit_crtc_state(state);
	struct drm_property_blob *blob = spacemit_state->ee_blob_prop;
	int i = 0;
	uint32_t *ee_coef;

	if (blob) {
		ee_coef = (uint32_t *)blob->data;

		for (i = 0; i < 45; i++)
			dpu_write_reg(hwdev, EE_REG, EE_ADDR, value32[i], ee_coef[i]);
	}
}

void saturn_hee_conf_dpuctrl_acad(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct drm_crtc_state *state = a_crtc->crtc.state;
	struct spacemit_crtc_state *spacemit_state = to_spacemit_crtc_state(state);
	struct drm_property_blob *blob = spacemit_state->pp_acad_blob_prop;
	u32 acad_base = ACAD_ADDR;
	u32 value;
	int i;
	struct cmdlist_regs *ad_cl = NULL;
	int *acad_coef = NULL;

	if (a_crtc->is_offline_mode == 1)
		return;

	ad_cl = alloc_cmdlist_regs(ACAD_REG);
	if (hwdev->is_acad_on) {
		if (blob) {
			acad_coef = (int *)blob->data;
			for (i = 0; i < hwdev->acad_num; i++) {
				value = acad_coef[i];
				dpu_write(hwdev, ACAD_REG, acad_base, value32[i], value, ad_cl, i);
			}
		}
	} else {
		dpu_write(hwdev, ACAD_REG, acad_base, acad_en, 0, ad_cl, 0);
	}

	cmdlist_regs_packing(crtc_to_cl(&a_crtc->crtc), CMDLIST_MOD_COMP, ad_cl);
	free_cmdlist_regs(ad_cl);

}

void saturn_hee_conf_dpuctrl_pp_gamma(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	int i, j;
	struct drm_crtc *crtc = &a_crtc->crtc;
	struct drm_crtc_state *state = crtc->state;
	struct spacemit_crtc_state *spacemit_state = to_spacemit_crtc_state(state);
	struct drm_property_blob *blob = spacemit_state->gamma_table_blob_prop;
	u32 value;
	u32 pp_base = PP1_BASE_ADDR;
	u32 gamma_base = GAMMA_BASE_ADDR;
	u32 base = 0x8;

	if (blob) {
		//first frame, gamma table is set
		if (!hwdev->gamma_table) {
			hwdev->gamma_table = kzalloc(blob->length, GFP_KERNEL);
			if (!hwdev->gamma_table)
				return;
			memcpy(hwdev->gamma_table, blob->data, blob->length);
		}
	}

	//set hwdev->gamma_table when power on
	if (!hwdev->gamma_table)
		return;

	dpu_write_reg(hwdev, LTM_REG, pp_base, m_npost_proc_en, 1);
	dpu_write_reg(hwdev, USR_GMA_REG, gamma_base, usr_gma_en, 1);
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 256; j += 2) {
			value = ((hwdev->gamma_table[i * 257 + j] >> 2) & 0xFFF) |
					(((hwdev->gamma_table[i * 257 + j + 1] >> 2) & 0xFFF) << 16);
			dpu_write_reg(hwdev, USR_GMA_REG, gamma_base, value32[(base + j * 2 + i * 0x204) / 4], value);
		}
		value = (hwdev->gamma_table[i * 257 + 256] >> 2) & 0xFFF;
		dpu_write_reg(hwdev, USR_GMA_REG, gamma_base, value32[(base + 256 * 2 + i * 0x204) / 4], value);
	}

	dpu_write_reg(hwdev, USR_GMA_REG, gamma_base, usr_gma_cfg_done, 1);
}

void saturn_hee_dpuctrl_color_temp(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state)
{
	struct spacemit_drm_private *priv = a_crtc->crtc.dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct drm_crtc_state *state = a_crtc->crtc.state;
	struct spacemit_crtc_state *spacemit_state = to_spacemit_crtc_state(state);
	struct drm_property_blob *blob = spacemit_state->pp_color_temperature_blob_property;
	int *color_temp;
	struct cmdlist_regs *cmd_regs = NULL;

	if (blob) {
		color_temp = (int *)blob->data;

		cmd_regs = alloc_cmdlist_regs(LTM_REG);

		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_npost_proc_en, 1, cmd_regs, 0);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_nmatrix_en, 1, cmd_regs, 1);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_ngain_to_full_en, 0, cmd_regs, 2);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_nendmatrix_en, 0, cmd_regs, 3);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_nfront_tmootf_en, 1, cmd_regs, 4);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_nend_tmootf_en, 0, cmd_regs, 5);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_neotf_en, 0, cmd_regs, 6);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_noetf_en, 0, cmd_regs, 7);

		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_table0, color_temp[0] & 0xFFFF, cmd_regs, 65);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_table1, color_temp[1] & 0xFFFF, cmd_regs, 66);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_table2, color_temp[2] & 0xFFFF, cmd_regs, 67);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_table3, color_temp[3] & 0xFFFF, cmd_regs, 68);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_table4, color_temp[4] & 0xFFFF, cmd_regs, 69);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_table5, color_temp[5] & 0xFFFF, cmd_regs, 70);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_table6, color_temp[6] & 0xFFFF, cmd_regs, 71);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_table7, color_temp[7] & 0xFFFF, cmd_regs, 72);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_table8, color_temp[8] & 0xFFFF, cmd_regs, 73);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_offset0, color_temp[9] & 0x1FFFFFF, cmd_regs, 74);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_offset1, color_temp[10] & 0x1FFFFFF, cmd_regs, 75);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_pmatrix_offset2, color_temp[11] & 0x1FFFFFF, cmd_regs, 76);

		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_tmootf_weightYr, color_temp[12] & 0xFFF, cmd_regs, 157);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_tmootf_weightYg, color_temp[13] & 0xFFF, cmd_regs, 157);
		dpu_write(hwdev, LTM_REG, LTM_BASE_ADDR, m_tmootf_weightYb, color_temp[14] & 0xFFF, cmd_regs, 158);

		cmdlist_regs_packing(crtc_to_cl(&a_crtc->crtc), CMDLIST_MOD_COMP, cmd_regs);

		free_cmdlist_regs(cmd_regs);
	}

	return;
}
