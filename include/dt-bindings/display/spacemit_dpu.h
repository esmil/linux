// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef SPACEMIT_DT_BINDINGS_DISPLAY_DPU_H
#define SPACEMIT_DT_BINDINGS_DISPLAY_DPU_H

/* online/offline path id */
#define WB0		(1<<0)
#define WB1		(1<<1)
#define PP0		(1<<6)
#define PP1		(1<<7)
#define PP2		(1<<8)
#define SCALER0		(1<<9)
#define SCALER1		(1<<10)
#define SCALER2		(1<<11)
#define SCALER3		(1<<12)
#define SCALER4		(1<<13)
#define ACAD0		(1<<14)
#define ACAD1		(1<<15)
#define ACAD2		(1<<16)
#define LUT3D0		(1<<17)
#define LUT3D1		(1<<18)
#define LUT3D2		(1<<19)

#define ONLINE0		0
#define ONLINE1		1
#define ONLINE2		2
#define OFFLINE0	3
#define OFFLINE1	4

#define COMPOSER0	0
#define COMPOSER1	1
#define COMPOSER2	2
#define COMPOSER3	3
#define COMPOSER4	4

#define DOVE_Z1		(1)
#define DOVE_A0		(2)

#define OUTFMT_RGB121212	0
#define OUTFMT_RGB101010	1
#define OUTFMT_RGB888		2
#define OUTFMT_RGB666		12
#define OUTFMT_RGB565		13

#endif /* SPACEMIT_DT_BINDINGS_DISPLAY_DPU_H */

