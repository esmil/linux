/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for SPACEMIT K3 DMA channel map table.
 *
 * Copyright (C) 2025 Spacemit
 */

#ifndef __DTS_K3_DMAC_H
#define __DTS_K3_DMAC_H

/* UART DMA channels */
#define DMA_UART0_TX	3
#define DMA_UART0_RX	4
#define DMA_UART2_TX	5
#define DMA_UART2_RX	6
#define DMA_UART3_TX	7
#define DMA_UART3_RX	8
#define DMA_UART4_TX	9
#define DMA_UART4_RX	10
#define DMA_UART5_TX	25
#define DMA_UART5_RX	26
#define DMA_UART6_TX	27
#define DMA_UART6_RX	28
#define DMA_UART7_TX	29
#define DMA_UART7_RX	30
#define DMA_UART8_TX	31
#define DMA_UART8_RX	32
#define DMA_UART9_TX	33
#define DMA_UART9_RX	34
#define DMA_UART10_TX	53
#define DMA_UART10_RX	54

/* I2C DMA channels */
#define DMA_I2C0_TX	11
#define DMA_I2C0_RX	12
#define DMA_I2C1_TX	13
#define DMA_I2C1_RX	14
#define DMA_I2C2_TX	15
#define DMA_I2C2_RX	16
#define DMA_I2C4_TX	17
#define DMA_I2C4_RX	18
#define DMA_I2C5_TX	35
#define DMA_I2C5_RX	36
#define DMA_I2C6_TX	37
#define DMA_I2C6_RX	38
#define DMA_I2C8_TX	41
#define DMA_I2C8_RX	42

/* SSP/SPI DMA channels */
#define DMA_SSP3_TX	19
#define DMA_SSP3_RX	20
#define DMA_SSPA0_TX	21
#define DMA_SSPA0_RX	22
#define DMA_SSPA1_TX	23
#define DMA_SSPA1_RX	24
#define DMA_SSPA2_TX	56
#define DMA_SSPA2_RX	57
#define DMA_SSPA3_TX	58
#define DMA_SSPA3_RX	59
#define DMA_SSPA4_TX	60
#define DMA_SSPA4_RX	61
#define DMA_SSPA5_TX	62
#define DMA_SSPA5_RX	63

/* CAN DMA channels */
#define DMA_CAN0_RX	43
#define DMA_CAN1_RX	44
#define DMA_CAN2_RX	51
#define DMA_CAN3_RX	52

/* SSP0/1 DMA channels (extended address space 0x1000) */
#define DMA_SSP0_TX	64	/* DRCMR at 0x1000 + 0x0 */
#define DMA_SSP0_RX	65	/* DRCMR at 0x1000 + 0x4 */
#define DMA_SSP1_TX	66	/* DRCMR at 0x1000 + 0x8 */
#define DMA_SSP1_RX	67	/* DRCMR at 0x1000 + 0xc */

/* QSPI DMA channels (extended address space 0x1000) */
#define DMA_QSPI_RX	84	/* DRCMR at 0x1000 + 0x50 */
#define DMA_QSPI_TX	85	/* DRCMR at 0x1000 + 0x54 */

/*
 * Secure APBC2 DMA channels (secure domain, PDMA1)
 */
#define DMA_SEC_UART1_RX	3
#define DMA_SEC_UART1_TX	4
#define DMA_SEC_SSP2_RX	        5
#define DMA_SEC_SSP2_TX	        6
#define DMA_SEC_I2C3_TX	        7
#define DMA_SEC_I2C3_RX	        8

#endif /* __DTS_K3_DMAC_H */
