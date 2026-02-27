/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SATURN_REG_MAP_hee_H_
#define _SATURN_REG_MAP_hee_H_

/* Base address, then bit definitions */

#ifdef ZYNC_FPGA
#define SATURN_REG_BASE 0x80000000
#define DSI_A_BASE 0x8421A000
#define I2C_NO	1
#else
#define SATURN_REG_BASE 0xc0340000
#define DSI_A_BASE 0xd421A000
#define I2C_NO 0
#endif

#define SATURN_REG_SIZE 0x60000

#define DPU_TOP_BASE_ADDR 0x0
#define DPU_CTRL_BASE_ADDR 0x3c0 /* dpu_ctl_top start address */
#define CMDLIST_BASE_ADDR 0x500
#define DPU_SCENE_CTRL1_BASE_ADDR 0x340
#define DPU_SCENE_CTRL2_BASE_ADDR 0x380
#define DPU_INT_BASE_ADDR 0x700
#define DPU_DSC_ENC_TOP_BASE_ADDR 0x41100

#define DPU_ONLINE_IRQ_MSK	(3 * 4)
#define DPU_ONLINE_IRQ_STS	(12 * 4)
#define DPU_OFFLINE_IRQ_MSK	(6 * 4)
#define DPU_OFFLINE_IRQ_STS	(15 * 4)

#define RDMA_SIZE 0x400
#define DMA_TOP_BASE_ADDR (RDMA0_BASE_ADDR + 0x280)

#define RDMA0_BASE_ADDR (0x00001000)
#define RDMA1_BASE_ADDR (RDMA0_BASE_ADDR + 0x1000)
#define RDMA2_BASE_ADDR (RDMA0_BASE_ADDR + 0xa000)
#define RDMA3_BASE_ADDR (RDMA0_BASE_ADDR + 0xb000)
// #define RDMA4_BASE_ADDR (RDMA0_BASE_ADDR + 0xc000)
// #define RDMA5_BASE_ADDR (RDMA0_BASE_ADDR + 0xd000)

// #define LP_SIZE 0x600
// #define LP0_BASE_ADDR (0x1000 + 0x0400 )
// #define LP1_BASE_ADDR (0x1000 + 0x1400 )
// #define LP2_BASE_ADDR (0x1000 + 0xa400 )
// #define LP3_BASE_ADDR (0x1000 + 0xb400 )
// #define LP4_BASE_ADDR (0x1000 + 0xc400 )
// #define LP5_BASE_ADDR (0x1000 + 0xd400 )

#define PREPIPE1_USR_GAMMA 0x2600
#define PREPIPE3_USR_GAMMA 0xb600

//TODO MMU NOT checked
#define MMU_BASE_ADDR (0x1100)
#define MMU_TOP_BASE_ADDR (MMU_BASE_ADDR + 0x100)
#define MMU_TBU_BASE_ADDR (MMU_BASE_ADDR)

#define TBU0_ADDR 0x1100
#define TBU1_ADDR 0x1180
#define TBU2_ADDR 0x2100
#define TBU3_ADDR 0x2180
#define TBU4_ADDR 0xb100
#define TBU5_ADDR 0xb180
#define TBU6_ADDR 0xc100
#define TBU7_ADDR 0xc180
#define WB_TBU_ADDR 0x50400

extern unsigned int tbu_offset_array[];
extern unsigned int tbu_top_offset_array[];
#define TBU_NUM 13
#define TBU_CMDLIST_NUM 13  /*Number of tpus available for cmdList configuration*/
#define EXTRA_ENTRY 60
#define TBU_TOP_NUM 1
#define TBU_CTRL 0x0
#define TBU_VALIDE_SIZE (sizeof(MMU_TBU_X_REG))
#define MMU_TOP_VALID_SIZE (sizeof(MMU_TOP_REG))
/* return the ith TBU offset comparing to TBU base */
#define TBU_OFFSET(i) \
	tbu_offset_array[i]
#define WB0_HANDLE_POS 12
#define WB1_HANDLE_POS 23
#define TBU_RDMA_START 0
#define TBU_RDMA_END   7
#define TBU_WB_POSITION_START  12
#define TBU_WB_POSITION_END    12
#define TBU_QOS                4
#define TBU_TOP_ADDR(i)  tbu_top_offset_array[i]
#define TBU_BASE_ADDR0_HIGH_MSK 0xFFF
#define TBU_BASE_ADDR0_LOW_MSK 0xFFFFFFFF
#define TBU_BASE_ADDR1_HIGH_MSK 0xFFF
#define TBU_BASE_ADDR1_LOW_MSK 0xFFFFFFFF
#define TBU_BASE_ADDR2_HIGH_MSK 0xFFF
#define TBU_BASE_ADDR2_LOW_MSK 0xFFFFFFFF
#define TBU_VA0_MSK 0xFFFFFFFF
#define TBU_VA1_MSK 0xFFFFFFFF
#define TBU_VA2_MSK 0xFFFFFFFF

#define CMP_SIZE 0x18000
#define CMP0_BASE_ADDR 0x00018000
#define CMP1_BASE_ADDR (CMP0_BASE_ADDR + 0x18000)
#define CMP2_BASE_ADDR (CMP0_BASE_ADDR + 0x30000)

#define PREPIPE_SCAL0_BASE_ADDR 0x14000
#define PREPIPE_SCAL1_BASE_ADDR 0x14400
#define PREPIPE_SCAL2_BASE_ADDR 0x14800
#define PREPIPE_SCAL3_BASE_ADDR 0x14c00

#define POST_PQ_TOP_ADDR (CMP0_BASE_ADDR + 0x400)
#define POST_PIPE_ADDR (CMP1_BASE_ADDR + 0x300)

#define GAMMA_BASE_ADDR (CMP1_BASE_ADDR + 0x8900)
#define LTM_BASE_ADDR (CMP1_BASE_ADDR + 0x600)

#define RC_BASE 0x39100

#define PP1_BASE_ADDR LTM_BASE_ADDR

#define WB_CORE_VALID_SIZE (sizeof(WB_ASIC_REG))
#define WB_TOP_BASE_ADDR (0x00050000 + 0x100)
#define WB0_TOP_BASE_ADDR (0x00050000 + 0x200)
#define WB0_SCALER_BASE_ADDR (WB0_TOP_BASE_ADDR + 0x500)
#define WB1_TOP_BASE_ADDR (0x00050800 + 0x200)
#define WB1_SCALER_BASE_ADDR (WB1_TOP_BASE_ADDR + 0x500)

#define DPU_SCENE_CTL_BASE_ADDR (0x300)
#define DPU_SCENE_CTL_SIZE 0x40
#define DPU_SCENE_CTL_ADDR(i) (DPU_SCENE_CTL_BASE_ADDR + i * DPU_SCENE_CTL_SIZE)

#define TMG0_BASE_ADDR (0x00051000 + 0x200)
#define TMG1_BASE_ADDR (0x00054000 + 0x200)
#define TMG2_BASE_ADDR (0x00057000)

#define POST_LUT3D_ADDR (CMP1_BASE_ADDR + 0xf00)
#define ACAD_ADDR (CMP1_BASE_ADDR + 0xa00)
#define EE_ADDR (CMP1_BASE_ADDR + 0xd00)

//#define OUTCTL_SIZE 0x8800

/* for onl0 */
#define INT_CTRL0_FUNC_STS 0x24
#define INT_CTRL0_FUNC_RAW 0x48
/* for cmb */
#define INT_CTRL1_FUNC_STS 0x30
#define INT_CTRL1_FUNC_RAW 0x58
/* for offline */
#define INT_CTRL2_FUNC_STS 0x3c
#define INT_CTRL2_FUNC_RAW 0x68

#define ONLINE_EOF BIT(1)
#define ONLINE_FRM_TIMING_CFG_EOF (BIT(2) | BIT(3))
#define ONLINE_WB0_FRAME_DONE BIT(6)
#define ONLINE_WB1_FRAME_DONE BIT(7)
#define ONLINE_FRAME_TIMING_UNFLOW BIT(8)
#define ONLINE_CMDLIST_CH_FRM_CFG_DONE (0x1ff << 11)

#define OFFLINE_WB0_FRAME_DONE BIT(1)
#define OFFLINE_WB1_FRAME_DONE BIT(2)
#define CMB_WB_SLICE_DONE  (BIT(24) | BIT(25))
#define OFF_WB_SLICE_DONE  (BIT(3) | BIT(4))
#define OFFLINE_CMDLIST_CH_FRM_CFG_DONE (0x1ff << 5)

#define LUT3D_CFG_DONE (0x4)

#define WB_ADDR_HIGH_BIT_MASK 0xFFF
#define BG_COLOR_SHIFT 2

#define CMDLIST_CH_Y                 0x188
#define CMDLIST_CH_START_CMPS_Y      0x90
#define CMDLIST_CFG_READY            0x34

#define OFFLINE_COMPOSE_WB_POS 22

typedef enum {
	E_DPU_TOP_REG = 0,
	E_DPU_SCENE_CTRL1_REG,
	E_DPU_SCENE_CTRL2_REG,
	E_DPU_CMDLIST_REG,
	E_DPU_INT_REG,
	E_DMA_TOP_CTRL_REG,
	E_RDMA_LAYER0_REG,
	E_RDMA_LAYER1_REG,
	E_RDMA_LAYER2_REG,
	E_RDMA_LAYER3_REG,
	E_RDMA_LAYER4_REG,
	E_RDMA_LAYER5_REG,
	E_MMU_TBU0_REG,
	E_MMU_TBU1_REG,
	E_MMU_TBU2_REG,
	E_MMU_TBU3_REG,
	E_MMU_TBU4_REG,
	E_MMU_TBU5_REG,
	E_MMU_TBU6_REG,
	E_MMU_TBU7_REG,
	E_MMU_TBU8_REG,
	E_MMU_TBU9_REG,
	E_MMU_TBU10_REG,
	E_MMU_TBU11_REG,
	E_MMU_TBU12_REG,
	E_MMU_TBU13_REG,
	E_MMU_TOP_REG,
	E_LP0_REG,
	E_LP1_REG,
	E_LP2_REG,
	E_LP3_REG,
	E_LP4_REG,
	E_LP5_REG,
	E_COMPOSER1_REG,
	E_COMPOSER2_REG,
	E_SCALER0_REG,
	E_SCALER2_REG,
	E_SCALER3_REG,
	E_SCALER1_REG,
	E_OUTCTRL0_REG,
	E_PP0_REG,
	E_WB_TOP_0_REG,
	E_DPU_DUMP_ALL
} dpu_hee_reg_enum;


typedef struct dpu_reg_dump {
	dpu_hee_reg_enum	index;
	u8 *module_name;
	uint32_t	module_offset;
	uint32_t	dump_reg_num;
} dpu_hee_reg_dump_t;

#include "dpu_ctl_top.h"
#include "dpu_scene_ctl.h"
#include "dpu_int.h"
#include "dpu_top.h"
#include "mmu_tbu_x.h"
#include "mmu_top.h"
#include "ltm.h" /* outctrl_proc_x.h in dove */
#include "usr_gma.h"
#include "tmg.h"
#include "rdma_path.h"
#include "lut_3d.h"
#include "acad.h"

#include "composer_x.h"

#include "cmdlist_top.h"
#include "ee.h"
#include "postpipe.h"
#include "prepipe_x.h"
#include "rc.h"
#include "rdma_top.h"
#include "scale_x.h"
#include "wb.h"

#endif
