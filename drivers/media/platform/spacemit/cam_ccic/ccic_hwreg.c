// SPDX-License-Identifier: GPL-2.0
/*
 * Spacemit CCIC driver
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */

#include <media/v4l2-dev.h>
#include "ccic_drv.h"
#include "ccic_hwreg.h"

#if 0
/* TODO: ccic config for raw dump on ctest, hard coding */
int mipi_sensor_TASK(struct ccic_dev *dev, uint8_t RAW_type,
		uint32_t sensor_width, uint32_t sensor_height)
{
	uint32_t ipe1_yuvinfmt, ipe1_yuvoutend, ipe1_yuvoutfmt;
	uint32_t ipe1_rgboutend, ipe1_rgbinoutfmt, ipe1_rgbendfmt, ipe1_doutfmt, ipe1_dinfmt, ipe1_420sp;
	uint32_t ipe1_sifmode, sensor_pclk, sensor_vsync, sensor_hsync, sensor_vsync_edge;
	uint32_t ipe1_image_width, ipe1_image_height, ipe1_y_mem_pitch, ipe1_uv_mem_pitch;

	uint32_t ipe1_donesel, ipe1_widsel, ipe1_srampdwn, ipe1_pwrdnen, ipe1_dropsel420, ipe1_dmaburstsel,
		 dma_burst_length_sel, ipe1_hsynccnt_ctrl, ipe1_sensorclkgate;

	uint32_t lgcy, frame_done_irq_sel, ipe1_linebufnum, ipe1_linebufen ;

	/* 0xb4: 0x00000030 */
	ccic_reg_write(dev, 0xb4, 0x30);

	/* 0x34: 0x05a00c80 */
	if (RAW_type == 12) {
		ipe1_image_width = sensor_width * 3 / 2; /* Image Width in bytes, raw12 */
	} else if (RAW_type == 10) {
		ipe1_image_width = sensor_width * 5 / 4; /* Image Width in bytes, raw10 */
	} else {
		pr_err("%s: invalid raw type %d\n", __func__, RAW_type);
		return -EINVAL;
	}
	ipe1_image_height = sensor_height << 16; /* Image Length in scanline */
	ccic_reg_write(dev, REG_IMGSIZE, ipe1_image_width | ipe1_image_height);

	/* 0x24: 0x00000c80 */
	ipe1_y_mem_pitch = ipe1_image_width;  /* This field is the distance between two vertical adjacent pixels, need 8-bytes aligned */
	ipe1_uv_mem_pitch = 0 << 16; /* This field is the distance between two vertical adjacent pixels, need 8-bytes aligned */
	ccic_reg_write(dev, REG_IMGPITCH, ipe1_y_mem_pitch | ipe1_uv_mem_pitch);

	/* 0x38: 0x00000000 */
	ccic_reg_write(dev, REG_IMGOFFSET, 0x0);

	/* 0x3c: 0x00008001 */
	ipe1_sifmode = 0 << 30; /* Master mode with or without hsync/vsync */
	sensor_pclk = 0 << 26; /* Parallel CMOS Sensor VCLK Polarity */
	sensor_vsync = 0 << 25; /* Parallel CMOS Sensor VSYNC Polarity */
	sensor_hsync = 0 << 24; /* Parallel CMOS Sensor HSYNC Polarity */
	sensor_vsync_edge = 0 << 23; /* Vsync Edge Control */
	ipe1_yuvinfmt = 0 << 18; /* YUV Format */
	ipe1_yuvoutend = 0 << 16; /* YCbCr Endianness Format */
	ipe1_yuvoutfmt = 4 << 13; /* YCbCr Output Format */
	ipe1_rgboutend = 0 << 12; /* don't care */
	ipe1_rgbinoutfmt = 0 << 9; /* don't care */
	ipe1_doutfmt = 1 << 8; /* CCIC's Output Data Format */
	ipe1_dinfmt = 1 << 6; /* CCIC's Input Data Format */
	ipe1_420sp = 0 << 4;
	ipe1_rgbendfmt = 0 << 2; /* don't care */
	ccic_reg_write(dev, REG_CTRL0,
			ipe1_sifmode | sensor_pclk | sensor_vsync | sensor_hsync |
			sensor_vsync_edge | ipe1_yuvinfmt | ipe1_yuvoutend |
			ipe1_yuvoutfmt | ipe1_rgboutend | ipe1_rgbinoutfmt |
			ipe1_rgbendfmt | ipe1_doutfmt | ipe1_dinfmt | 1 << 21);

	/* 0x40: 0x8402003c */
	ipe1_donesel = 1 << 31; /* 0: WLAST_dly; 1: bvalid_reg */
	ipe1_widsel = 0 << 30; /* 0: always posted; 1: non-posted at fe_to_fs */
	ipe1_widsel = 0 << 30; /* 0: always posted; 1: non-posted at fe_to_fs */
	ipe1_srampdwn = 0 << 29; /* SRAM Power Down */
	ipe1_pwrdnen = 0 << 28; /* Power Down Enable */
	ipe1_dmaburstsel = 2 << 25; /* 0: 64-byte; 1: 128-byte; 2: 256-byte */
	ipe1_dropsel420 = 0 << 24;
	dma_burst_length_sel = 1 << 17;
	ipe1_hsynccnt_ctrl =
		0x1e << 1; /* controls line_end indication when sensorclkgate = 1 */
	ipe1_sensorclkgate =
		0; /* 0: parallel sensor output clock is always running; 1: parallel sensor output clock is gated when not used */
	ccic_reg_write(dev, REG_CTRL1,
			ipe1_donesel | ipe1_widsel | ipe1_srampdwn | ipe1_pwrdnen |
			ipe1_dmaburstsel | ipe1_dropsel420 |
			dma_burst_length_sel | ipe1_hsynccnt_ctrl |
			ipe1_sensorclkgate);

	/* 0x44: 0xc0000000 */
	lgcy = 0x3 << 30;
	frame_done_irq_sel = 0 << 29;
	ipe1_linebufnum = 0 << 17;
	ipe1_linebufen = 0 << 16;
	ccic_reg_write(dev, REG_CTRL2, lgcy | frame_done_irq_sel | ipe1_linebufnum | ipe1_linebufen);

	/* 0x48: 0xc0000000 */
	ccic_reg_write(dev, REG_CTRL3, 0xc0000000);

	/* 0x88: 0x00000003 */
	ccic_reg_write(dev, 0x88, 0x3);

	/* 0x3c: enable ccic dma */
	ccic_reg_set_bit(dev, REG_CTRL0, BIT(0));

	return 0;
}
#endif
#if 0
	int ccic_csi2_config_dphy(struct ccic_dev *ccic_dev, int lanes,
				int enable)
	{
	unsigned int dphy2_val = 0xa2848888;
	unsigned int dphy3_val = 0x00001500;
	/* unsigned int dphy4_val = 0x00000000; */
	unsigned int dphy5_val = 0x000000ff; /* 4lanes */
	unsigned int dphy6_val = 0x1001;

	if (!enable) {
		ccic_reg_write(ccic_dev, REG_CSI2_DPHY5, 0x00);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_DPHY1,
					CSI2_DHPY1_ANA_PU); /* analog power off */
		return 0;
	}

	if (lanes < 1 || lanes > 4)
		return -EINVAL;

	dphy5_val = CSI2_DPHY5_LANE_ENA(lanes);
	dphy5_val = dphy5_val | (dphy5_val << CSI2_DPHY5_LANE_RESC_ENA_SHIFT);

	ccic_reg_write(ccic_dev, REG_CSI2_DPHY2, dphy2_val);
	ccic_reg_write(ccic_dev, REG_CSI2_DPHY3, dphy3_val);
	/* ccic_reg_write(ccic_dev, REG_CSI2_DPHY4, dphy4_val); */
	ccic_reg_write(ccic_dev, REG_CSI2_DPHY5, dphy5_val);
	ccic_reg_write(ccic_dev, REG_CSI2_DPHY6, dphy6_val);
	ccic_reg_set_bit(ccic_dev, REG_CSI2_DPHY1,
				CSI2_DHPY1_ANA_PU); /* analog power on */

	return 0;
	}
#endif
int ccic_csi2_lanes_enable(struct ccic_dev *ccic_dev, int lanes)
{
	unsigned int ctrl0_val = 0;

	if (lanes < 0 || lanes > 4)
		return -EINVAL;

	if (!lanes) { /* Disable MIPI CSI2 Interface */
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL0,
				   CSI2_C0_ENABLE); /* csi off */
		return 0;
	}

	ctrl0_val = ccic_reg_read(ccic_dev, REG_CSI2_CTRL0);
	ctrl0_val &= ~(CSI2_C0_LANE_NUM_MASK);
	ctrl0_val |= CSI2_C0_LANE_NUM(lanes);
	ctrl0_val |= CSI2_C0_ENABLE;
	ctrl0_val &= ~(CSI2_C0_VLEN_MASK);
	ctrl0_val |= CSI2_C0_VLEN;

	ccic_reg_write(ccic_dev, REG_CSI2_CTRL0, ctrl0_val);

	return 0;
}

void ccic_set_path_vc(struct ccic_dev *ccic_dev, int path_id, u32 vc)
{
	if (path_id == 0) {
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL, vc << 14,
				    CSI2_VCCTRL_VC0_MASK);
	} else if (path_id == 1) {
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL, vc << 22,
				    CSI2_VCCTRL_VC1_MASK);
	} else if (path_id == 2) {
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL2, vc << 6,
				    CSI2_VCCTRL_VC2_MASK);
	} else if (path_id == 3) {
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL2, vc << 14,
				    CSI2_VCCTRL_VC3_MASK);
	}
}

int ccic_csi2_vc_ctrl(struct ccic_dev *ccic_dev, int md, unsigned int dt_en)
{
	int ret = 0;

	switch (md) {
	case CCIC_CSI2VC_NM: /* Normal mode */
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL,
				    CSI2_VCCTRL_MD_NORMAL, CSI2_VCCTRL_MD_MASK);
		ccic_en_dt_match_pass_mode(ccic_dev, 0);
		break;
	case CCIC_CSI2VC_VC: /* Virtual Channel mode */
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL,
				    CSI2_VCCTRL_MD_VC, CSI2_VCCTRL_MD_MASK);
		ccic_en_dt_match_pass_mode(ccic_dev, dt_en);
		break;
	case CCIC_CSI2VC_DT: /* TODO: Data-Type Interleaving */
		ccic_reg_write_mask(ccic_dev, REG_CSI2_VCCTRL,
				    CSI2_VCCTRL_MD_DT, CSI2_VCCTRL_MD_MASK);
		ccic_en_dt_match_pass_mode(ccic_dev, dt_en);
		pr_info("csi2 vc mode %d todo\n", md);
		break;
	default:
		pr_err("%s: invalid csi2 vc mode %d\n", __func__, md);
		ret = -EINVAL;
	}

	return ret;
}

void ccic_en_dt_match_pass_mode(struct ccic_dev *ccic_dev, int en)
{
	if (en) {
		ccic_reg_set_bit(ccic_dev, REG_CSI2_VCCTRL,
				 CSI2_VCCTRL_DT_PASS_MASK);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_VCCTRL,
				   CSI2_VCCTRL_DT_PASS_MASK);
	}
}

void ccic_set_path0_dt_filter(struct ccic_dev *ccic_dev, int flt0_en,
			      uint32_t flt0_data, int flt1_en,
			      uint32_t flt1_data)
{
	if (flt0_en) {
		ccic_reg_set_bit(ccic_dev, REG_CSI2_DT_FLT,
				 CSI2_DT_FLT0_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT, flt0_data,
				    CSI2_DT_FLT0_PATTERN_MASK);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_DT_FLT,
				   CSI2_DT_FLT0_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT, 0,
				    CSI2_DT_FLT0_PATTERN_MASK);
	}

	if (flt1_en) {
		ccic_reg_set_bit(ccic_dev, REG_CSI2_DT_FLT,
				 CSI2_DT_FLT1_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT, (flt1_data << 8),
				    CSI2_DT_FLT1_PATTERN_MASK);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_DT_FLT,
				   CSI2_DT_FLT1_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT, 0,
				    CSI2_DT_FLT1_PATTERN_MASK);
	}
}

void ccic_set_path1_dt_filter(struct ccic_dev *ccic_dev, int flt0_en,
			      uint32_t flt0_data, int flt1_en,
			      uint32_t flt1_data)
{
	if (flt0_en) {
		ccic_reg_set_bit(ccic_dev, REG_CSI2_DT_FLT,
				 CSI2_DT_FLT2_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT,
				    (flt0_data << 16),
				    CSI2_DT_FLT2_PATTERN_MASK);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_DT_FLT,
				   CSI2_DT_FLT2_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT, 0,
				    CSI2_DT_FLT2_PATTERN_MASK);
	}

	if (flt1_en) {
		ccic_reg_set_bit(ccic_dev, REG_CSI2_DT_FLT,
				 CSI2_DT_FLT3_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT,
				    (flt1_data << 24),
				    CSI2_DT_FLT3_PATTERN_MASK);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_DT_FLT,
				   CSI2_DT_FLT3_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT, 0,
				    CSI2_DT_FLT3_PATTERN_MASK);
	}
}

void ccic_set_path2_dt_filter(struct ccic_dev *ccic_dev, int flt0_en,
			      uint32_t flt0_data, int flt1_en,
			      uint32_t flt1_data)
{
	if (flt0_en) {
		ccic_reg_set_bit(ccic_dev, REG_CSI2_DT_FLT2,
				 CSI2_DT_FLT4_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT2, flt0_data,
				    CSI2_DT_FLT4_PATTERN_MASK);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_DT_FLT2,
				   CSI2_DT_FLT4_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT2, 0,
				    CSI2_DT_FLT4_PATTERN_MASK);
	}

	if (flt1_en) {
		ccic_reg_set_bit(ccic_dev, REG_CSI2_DT_FLT2,
				 CSI2_DT_FLT5_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT2,
				    (flt1_data << 8),
				    CSI2_DT_FLT5_PATTERN_MASK);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_DT_FLT2,
				   CSI2_DT_FLT5_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT2, 0,
				    CSI2_DT_FLT5_PATTERN_MASK);
	}
}

void ccic_set_path3_dt_filter(struct ccic_dev *ccic_dev, int flt0_en,
			      uint32_t flt0_data, int flt1_en,
			      uint32_t flt1_data)
{
	if (flt0_en) {
		ccic_reg_set_bit(ccic_dev, REG_CSI2_DT_FLT2,
				 CSI2_DT_FLT6_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT2,
				    (flt0_data << 16),
				    CSI2_DT_FLT6_PATTERN_MASK);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_DT_FLT2,
				   CSI2_DT_FLT6_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT2, 0,
				    CSI2_DT_FLT6_PATTERN_MASK);
	}

	if (flt1_en) {
		ccic_reg_set_bit(ccic_dev, REG_CSI2_DT_FLT2,
				 CSI2_DT_FLT7_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT2,
				    (flt1_data << 24),
				    CSI2_DT_FLT7_PATTERN_MASK);
	} else {
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_DT_FLT2,
				   CSI2_DT_FLT7_EN_MASK);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_DT_FLT2, 0,
				    CSI2_DT_FLT7_PATTERN_MASK);
	}
}

int ccic_dma_set_burst(struct ccic_dev *ccic_dev)
{
	u32 dma_burst;

	/* setup the DMA burst */
	switch (ccic_dev->dma_burst) {
	case 128:
		dma_burst = C1_DMAB128;
		break;
	case 256:
		dma_burst = C1_DMAB256;
		break;
	default:
		dma_burst = C1_DMAB64;
		break;
	}
	ccic_reg_write_mask(ccic_dev, REG_CTRL1, dma_burst, C1_DMAB_MASK);
	ccic_reg_set_bit(ccic_dev, REG_CTRL1, C1_DMAB_LENSEL);
	ccic_reg_set_bit(ccic_dev, REG_CTRL1, BIT(31));

	/* ccic_reg_set_bit(ccic_dev, REG_CTRL2, C2_LGCY_LNNUM); */
	/* ccic_reg_set_bit(ccic_dev, REG_CTRL2, C2_LGCY_HBLANK); */
	return 0;
}

int ccic_csi2idi_src_sel(struct ccic_dev *ccic_dev, int sel)
{
	switch (sel) {
	case CCIC_IDI_SEL_NONE:
		/* ccic_reg_clear_bit(ccic_dev, REG_IDI_CTRL, IDI_RELEASE_RESET); */
		ccic_reg_set_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_REPACK_RST);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL2,
				   CSI2_C2_REPACK_ENA);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_DPCM_ENA);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_VCCTRL,
				   CSI2_VCCTRL_MD_VC);
		ccic_reg_write_mask(ccic_dev, REG_CSI2_CTRL2,
				    CSI2_C2_MUX_SEL_LOCAL_MAIN,
				    CSI2_C2_MUX_SEL_MASK);
		break;
	case CCIC_IDI_SEL_REPACK:
		ccic_reg_write_mask(ccic_dev, REG_IDI_CTRL, IDI_SEL_DPCM_REPACK,
				    IDI_SEL_MASK);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL2,
				   CSI2_C2_IDI_MUX_SEL_DPCM);
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL2,
				   CSI2_C2_REPACK_RST);
		ccic_reg_set_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_REPACK_ENA);
		break;
	case CCIC_IDI_SEL_DPCM:
		ccic_reg_write_mask(ccic_dev, REG_IDI_CTRL, IDI_SEL_DPCM_REPACK,
				    IDI_SEL_MASK);
		ccic_reg_set_bit(ccic_dev, REG_CSI2_CTRL2,
				 CSI2_C2_IDI_MUX_SEL_DPCM);
		ccic_reg_set_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_DPCM_ENA);
		break;
	case CCIC_IDI_SEL_PARALLEL:
		ccic_reg_write_mask(ccic_dev, REG_IDI_CTRL, IDI_SEL_PARALLEL,
				    IDI_SEL_MASK);
		break;
	default:
		pr_err("%s: IDI source is error %d\n", __func__, sel);
		return -EINVAL;
	}

	return 0;
}

void ccic_csi2idi_reset(struct ccic_dev *ccic_dev, int reset)
{
	if (reset) {
		ccic_reg_clear_bit(ccic_dev, REG_IDI_CTRL, IDI_RELEASE_RESET);
		/* assert reset to Repack module */
		ccic_reg_set_bit(ccic_dev, REG_CSI2_CTRL2, CSI2_C2_REPACK_RST);
	} else {
		ccic_reg_set_bit(ccic_dev, REG_IDI_CTRL, IDI_RELEASE_RESET);
		/* Deassert reset to Repack module */
		ccic_reg_clear_bit(ccic_dev, REG_CSI2_CTRL2,
				   CSI2_C2_REPACK_RST);
	}
}

/* dump register in order to debug */
void ccic_hw_dump_regs(struct ccic_dev *ccic_dev)
{
	unsigned int ret;

	pr_info("CCIC%d regs dump:\n", ccic_dev->index);
	/*
	 * CCIC IRQ REG
	 */
	ret = ccic_reg_read(ccic_dev, REG_IRQSTAT);
	pr_info("CCIC: REG_IRQSTAT[0x%02x] is 0x%08x\n", REG_IRQSTAT, ret);
	ret = ccic_reg_read(ccic_dev, REG_IRQSTATRAW);
	pr_info("CCIC: REG_IRQSTATRAW[0x%02x] is 0x%08x\n", REG_IRQSTATRAW,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_IRQMASK);
	pr_info("CCIC: REG_IRQMASK[0x%02x] is 0x%08x\n\n", REG_IRQMASK, ret);

	/*
	 * CCIC IMG REG
	 */
	ret = ccic_reg_read(ccic_dev, REG_IMGPITCH);
	pr_info("CCIC: REG_IMGPITCH[0x%02x] is 0x%08x\n", REG_IMGPITCH, ret);
	ret = ccic_reg_read(ccic_dev, REG_IMGSIZE);
	pr_info("CCIC: REG_IMGSIZE[0x%02x] is 0x%08x\n", REG_IMGSIZE, ret);
	ret = ccic_reg_read(ccic_dev, REG_IMGOFFSET);
	pr_info("CCIC: REG_IMGOFFSET[0x%02x] is 0x%08x\n\n", REG_IMGOFFSET,
		ret);

	/*
	 * CCIC CTRL REG
	 */
	ret = ccic_reg_read(ccic_dev, REG_CTRL0);
	pr_info("CCIC: REG_CTRL0[0x%02x] is 0x%08x\n", REG_CTRL0, ret);
	ret = ccic_reg_read(ccic_dev, REG_CTRL1);
	pr_info("CCIC: REG_CTRL1[0x%02x] is 0x%08x\n", REG_CTRL1, ret);
	ret = ccic_reg_read(ccic_dev, REG_CTRL2);
	pr_info("CCIC: REG_CTRL2[0x%02x] is 0x%08x\n", REG_CTRL2, ret);
	ret = ccic_reg_read(ccic_dev, REG_CTRL3);
	pr_info("CCIC: REG_CTRL3[0x%02x] is 0x%08x\n", REG_CTRL3, ret);
	ret = ccic_reg_read(ccic_dev, REG_IDI_CTRL);
	pr_info("CCIC: REG_IDI_CTRL[0x%02x] is 0x%08x\n\n", REG_IDI_CTRL, ret);
	ret = ccic_reg_read(ccic_dev, REG_CSI2_VCCTRL);
	pr_info("CCIC: REG_CSI2_VCCTRL[0x%02x] is 0x%08x\n\n", REG_CSI2_VCCTRL,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_LNNUM);
	pr_info("CCIC: REG_LNNUM[0x%02x] is 0x%08x\n", REG_LNNUM, ret);
	ret = ccic_reg_read(ccic_dev, REG_FRAME_CNT);
	pr_info("CCIC: REG_FRAME_CNT[0x%02x] is 0x%08x\n", REG_FRAME_CNT, ret);

	/*
	 * CCIC CSI2 REG
	 */
	ret = ccic_reg_read(ccic_dev, REG_CSI2_DPHY1);
	pr_info("CCIC: REG_CSI2_DPHY1[0x%02x] is 0x%08x\n", REG_CSI2_DPHY1,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_CSI2_DPHY2);
	pr_info("CCIC: REG_CSI2_DPHY2[0x%02x] is 0x%08x\n", REG_CSI2_DPHY2,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_CSI2_DPHY3);
	pr_info("CCIC: REG_CSI2_DPHY3[0x%02x] is 0x%08x\n", REG_CSI2_DPHY3,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_CSI2_DPHY4);
	pr_info("CCIC: REG_CSI2_DPHY4[0x%02x] is 0x%08x\n", REG_CSI2_DPHY4,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_CSI2_DPHY5);
	pr_info("CCIC: REG_CSI2_DPHY5[0x%02x] is 0x%08x\n", REG_CSI2_DPHY5,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_CSI2_DPHY6);
	pr_info("CCIC: REG_CSI2_DPHY6[0x%02x] is 0x%08x\n", REG_CSI2_DPHY6,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_CSI2_CTRL0);
	pr_info("CCIC: REG_CSI2_CTRL0[0x%02x] is 0x%08x\n", REG_CSI2_CTRL0,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_CSI2_CTRL2);
	pr_info("CCIC: REG_CSI2_CTRL2[0x%02x] is 0x%08x\n", REG_CSI2_CTRL2,
		ret);
	ret = ccic_reg_read(ccic_dev, REG_CSI2_CTRL3);
	pr_info("CCIC: REG_CSI2_CTRL3[0x%02x] is 0x%08x\n", REG_CSI2_CTRL3,
		ret);

	/*
	 * CCIC YUV REG
	 */
	ret = ccic_reg_read(ccic_dev, REG_Y0BAR);
	pr_info("CCIC: REG_Y0BAR[0x%02x] 0x%08x\n", REG_Y0BAR, ret);
	ret = ccic_reg_read(ccic_dev, REG_U0BAR);
	pr_info("CCIC: REG_U0BAR[0x%02x] 0x%08x\n", REG_U0BAR, ret);
	ret = ccic_reg_read(ccic_dev, REG_V0BAR);
	pr_info("CCIC: REG_V0BAR[0x%02x] 0x%08x\n\n", REG_V0BAR, ret);

#if 0
	/*
	 * CCIC APMU REG
	 */
	ret = __raw_readl(get_apmu_base_va() + REG_CLK_CCIC_RES);
	pr_info("CCIC: APMU_CCIC_RES[0x%02x] is 0x%08x\n", REG_CLK_CCIC_RES, ret);
	ret = __raw_readl(get_apmu_base_va() + REG_CLK_CCIC2_RES);
	pr_info("CCIC: APMU_CCIC2_RES[0x%02x] is 0x%08x\n", REG_CLK_CCIC2_RES, ret);
#endif
}
