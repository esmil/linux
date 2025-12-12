/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef RDMA_PATH_REG_H
#define RDMA_PATH_REG_H

typedef union {
	struct {
	//REGISTER saturn_dma_path_reg_0
	UINT32 layer_mode             : 2;
	UINT32 outstanding_num        : 5;
	UINT32:1;
	UINT32 req_conti_num          : 5;
	UINT32 layer_cmpsr_id         : 3;
	UINT32 axi_port_sel           : 2;
	UINT32:6;
	UINT32 rdma_burst_len         : 8;

	//REGISTER saturn_dma_path_reg_1
	UINT32 is_two_layers          : 1;
	UINT32 is_offline             : 1;
	UINT32 hdr_osd_num            : 4;
	UINT32:26;

	//REGISTER saturn_dma_path_reg_2
	UINT32 cmpsr_y_offset0        : 16;
	UINT32:16;

	//REGISTER saturn_dma_path_reg_3
	struct {
	UINT32:32;
	} saturn_dma_path_reg_3[6];

	//REGISTER saturn_dma_path_reg_9
	UINT32 base_addr0_low_ly0     : 32;

	//REGISTER saturn_dma_path_reg_10
	UINT32 base_addr0_high_ly0    : 2;
	UINT32:30;

	//REGISTER saturn_dma_path_reg_11
	UINT32 base_addr1_low_ly0     : 32;

	//REGISTER saturn_dma_path_reg_12
	UINT32 base_addr1_high_ly0    : 2;
	UINT32:30;

	//REGISTER saturn_dma_path_reg_13
	UINT32 base_addr2_low_ly0     : 32;

	//REGISTER saturn_dma_path_reg_14
	UINT32 base_addr2_high_ly0    : 2;
	UINT32:30;

	//REGISTER saturn_dma_path_reg_15
	UINT32 rdma_stride0_layer0    : 16;
	UINT32 rdma_stride1_layer0    : 16;

	//REGISTER saturn_dma_path_reg_16
	UINT32 img_width_ly0          : 16;
	UINT32 img_height_ly0         : 16;

	//REGISTER saturn_dma_path_reg_17
	UINT32 bbox_start_x_ly0       : 16;
	UINT32 bbox_start_y_ly0       : 16;

	//REGISTER saturn_dma_path_reg_18
	UINT32 bbox_end_x_ly0         : 16;
	UINT32 bbox_end_y_ly0         : 16;

	//REGISTER saturn_dma_path_reg_19
	struct {
	UINT32:32;
	} saturn_dma_path_reg_19[10];

	//REGISTER saturn_dma_path_reg_29
	UINT32 pixel_format           : 6;
	UINT32 uv_swap                : 1;
	UINT32:1;
	UINT32 rot_mode_ly0           : 3;
	UINT32 rot_mode_ly1           : 3;
	UINT32:18;

	//REGISTER saturn_dma_path_reg_30
	UINT32 fbc_split_mode         : 1;
	UINT32 fbc_yuv_transform      : 1;
	UINT32 fbc_sb_layout          : 1;
	UINT32 fbc_tile_type          : 1;
	UINT32:28;

	//REGISTER saturn_dma_path_reg_31
	UINT32 fbc_mem_size           : 16;
	UINT32 fbc_mem_base_addr      : 12;
	UINT32 fbc_mem_map            : 1;
	UINT32 dec_line_num_sw        : 1;
	UINT32 sw_dec_line_num        : 2;

	//REGISTER saturn_dma_path_reg_32
	UINT32 nsaid                  : 4;
	UINT32:28;

	//REGISTER saturn_dma_path_reg_33
	UINT32 csc_matrix00           : 14;
	UINT32:2;
	UINT32 csc_matrix01           : 14;
	UINT32:2;

	//REGISTER saturn_dma_path_reg_34
	UINT32 csc_matrix02           : 14;
	UINT32:2;
	UINT32 csc_matrix03           : 14;
	UINT32:2;

	//REGISTER saturn_dma_path_reg_35
	UINT32 csc_matrix10           : 14;
	UINT32:2;
	UINT32 csc_matrix11           : 14;
	UINT32:2;

	//REGISTER saturn_dma_path_reg_36
	UINT32 csc_matrix12           : 14;
	UINT32:2;
	UINT32 csc_matrix13           : 14;
	UINT32:2;

	//REGISTER saturn_dma_path_reg_37
	UINT32 csc_matrix20           : 14;
	UINT32:2;
	UINT32 csc_matrix21           : 14;
	UINT32:2;

	//REGISTER saturn_dma_path_reg_38
	UINT32 csc_matrix22           : 14;
	UINT32:2;
	UINT32 csc_matrix23           : 14;
	UINT32:2;

	//REGISTER saturn_dma_path_reg_39
	UINT32 alpha0_ly0             : 12;
	UINT32:4;
	UINT32 alpha1_ly0             : 12;
	UINT32:4;

	//REGISTER saturn_dma_path_reg_40
	UINT32 alpha2_ly0             : 12;
	UINT32:4;
	UINT32 alpha3_ly0             : 12;
	UINT32:4;

	//REGISTER saturn_dma_path_reg_41
	UINT32 rdma_urgent_up         : 8;
	UINT32 rdma_urgent_down       : 8;
	UINT32:16;

	//REGISTER saturn_dma_path_reg_42
	UINT32 sw_qos_rsv0            : 1;
	UINT32 sw_urgent_en           : 1;
	UINT32 sw_urgent              : 1;
	UINT32:5;
	UINT32 offl_prefetch_lines    : 8;
	UINT32:16;

	//REGISTER saturn_dma_path_reg_43
	UINT32 rdma_dbg_irq_raw       : 32;

	//REGISTER saturn_dma_path_reg_44
	UINT32 rdma_dbg_irq_mask      : 32;

	//REGISTER saturn_dma_path_reg_45
	UINT32 rdma_dbg_irq_status    : 32;

	//REGISTER saturn_dma_path_reg_46
	UINT32 prefetch_osd_num       : 5;
	UINT32:3;
	UINT32 prefetch_hdr_osd_num   : 4;
	UINT32 osd_sub_step           : 2;
	UINT32:2;
	UINT32 osd_num_min            : 5;
	UINT32:3;
	UINT32 hdr_osd_num_min        : 4;
	UINT32 pp_buf_switch_num      : 3;
	UINT32:1;

	//REGISTER saturn_dma_path_reg_47
	UINT32 rdma_dbg_info0         : 32;

	//REGISTER saturn_dma_path_reg_48
	UINT32 rdma_dbg_info1         : 32;

	//REGISTER saturn_dma_path_reg_49
	UINT32 rdma_dbg_info2         : 32;

	//REGISTER saturn_dma_path_reg_50
	UINT32 rdma_dbg_info3         : 32;

	//REGISTER saturn_dma_path_reg_51
	UINT32 rdma_dbg_info4         : 32;

	//REGISTER saturn_dma_path_reg_52
	UINT32 secu_en                : 1;
	UINT32:31;

	//REGISTER saturn_dma_path_reg_53
	UINT32 force_update_en        : 1;
	UINT32 vsync_update_en        : 1;
	UINT32 shadow_read_en         : 1;
	UINT32:29;

	//REGISTER saturn_dma_path_reg_54
	UINT32 force_update_pulse     : 1;
	UINT32:31;

	//REGISTER saturn_dma_path_reg_55
	UINT32 force_update_en_se     : 1;
	UINT32 vsync_update_en_se     : 1;
	UINT32 shadow_read_en_se      : 1;
	UINT32:29;

	//REGISTER saturn_dma_path_reg_56
	UINT32 force_update_pulse_se  : 1;
	UINT32:31;

	//REGISTER saturn_dma_path_reg_57
	UINT32 icg_override           : 1;
	UINT32:31;

	//REGISTER saturn_dma_path_reg_58
	UINT32 regbank_reset          : 1;
	UINT32:31;

	//REGISTER saturn_dma_path_reg_59
	UINT32 regbank_reset2         : 1;
	UINT32:31;

	};

	INT32 value32[60];

} RDMA_PATH_REG;

#endif

typedef RDMA_PATH_REG RDMA_PATH_X_REG;
