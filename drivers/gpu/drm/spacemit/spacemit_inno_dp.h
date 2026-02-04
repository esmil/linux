/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef __SPACEMIT_INNO_DP_H__
#define __SPACEMIT_INNO_DP_H__

// SOC_DPTX_CORE_CONTROL: 0x0018
#define SOC_DPTX_SCRAMBLER_DISABLE                    0x0018,31,31 // Scramble_disable;0: Scramble enabled;1: Disable scramble
#define SOC_DPTX_ENHANCE_FRAMING_EN                   0x0018,30,30 // Enhance_framing_enable;0:disable;1:enable
#define SOC_DPTX_DEFAULT_FAST_LINK_TRAIN_EN           0x0018,29,29 // DEFAULT_FAST_LINK_TRAIN_EN;Default fast link training state.
#define SOC_DPTX_SCALE_DOWN_MODE                      0x0018,28,28 // SCALE_DOWN_MODE;scaling hpd counter for fast simulation;0: normal;1: scaling
#define SOC_DPTX_FORCE_HPD                            0x0018,27,27 // FORCE_HPD;Force HPD to 1'b1.
#define SOC_DPTX_ENABLE_EDP                           0x0018,4,4   // ENABLE_EDP;EDP mode Enable.

// SOC_DPTX_SOFT_RESET: 0x001c
#define SOC_DPTX_CONTROLLER_RESET                     0x001c,31,31 // CONTROLLER_RESET;0: normal;1: reset
#define SOC_DPTX_PHY_RESET                            0x001c,30,30 // PHY_RESET;0: normal;1: reset
#define SOC_DPTX_HDCP_RESET                           0x001c,29,29 // HDCP_RESET;0: normal;1: reset
#define SOC_DPTX_AUX_RESET                            0x001c,27,27 // AUX_RESET;0: normal;1:reset
#define SOC_DPTX_VIDEO_RESET                          0x001c,3,0   // VIDEO_RESET;video soft reset,up to 4 streams.;0: noraml;1:reset

// SOC_DPTX_CLK_INV_MUX: 0x0028
#define SOC_DPTX_REG_VID_CLK_SEL                      0x0028,0,0   // REG_VID_CLK_SEL;0: external pixel clock;1: internal pixel clock

// SOC_DPTX_GENERAL_INTERRUPT: 0x0080
#define SOC_DPTX_AUX_REPLY_EVENT_INT_STA              0x0080,16,16 // AUX__REPLY_EVENT_INT_STA;AUX reply event,can be clear by RWite 1’b1

// SOC_DPTX_GENERAL_INTERRUPT_MASK: 0x0084
#define SOC_DPTX_HPD_INT_STA_MSK                      0x0084,17,17 // HPD_INT_STA_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_AUX_REPLY_EVENT_INT_STA_MSK          0x0084,16,16 // AUX_REPLY_EVENT_INT_STA_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_HDCP_INT_STA_MSK                     0x0084,15,15 // HDCP_INT_STA_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_ILLEGAL_AUX_CMD_INT_STA_MSK          0x0084,14,14 // ILLEGAL_AUX_CMD_INT_STA_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_TYPE_C_EVENT_MSK                     0x0084,13,13 // TYPE_C_EVENT_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_DSC_EVENT_MSK                        0x0084,12,12 // DSC_EVENT_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_SDP_INT_STA_S3_MSK                   0x0084,11,11 // SDP_INT_STA_S3_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_SDP_INT_STA_S2_MSK                   0x0084,10,10 // SDP_INT_STA_S2_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_SDP_INT_STA_S1_MSK                   0x0084,9,9   // SDP_INT_STA_S1_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_SDP_INT_STA_S0_MSK                   0x0084,8,8   // SDP_INT_STA_S0_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_VIDEO_FIFO_OVERFLOW_INT_STA_S3_MSK   0x0084,7,7   // VIDEO_FIFO_OVERFLOW_INT_STA_S3_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_VIDEO_FIFO_OVERFLOW_INT_STA_S2_MSK   0x0084,6,6   // VIDEO_FIFO_OVERFLOW_INT_STA_S2_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_VIDEO_FIFO_OVERFLOW_INT_STA_S1_MSK   0x0084,5,5   // VIDEO_FIFO_OVERFLOW_INT_STA_S1_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_VIDEO_FIFO_OVERFLOW_INT_STA_S0_MSK   0x0084,4,4   // VIDEO_FIFO_OVERFLOW_INT_STA_S0_MSK;0: disable interrupt;1: enable interrupt

// SOC_DPTX_HPD_INTERRUPT_STATUS: 0x0088
#define SOC_DPTX_HOT_PLUG_EVENT                       0x0088,29,29 // HOT_PLUG_EVENT;HPD Plug interrupt status,can be clear by RWite 1’b1.
#define SOC_DPTX_HOT_UNPLUG_EVENT                     0x0088,28,28 // HOT_UNPLUG_EVENT;HPD unplug interrupt status,can be clear by RWite 1'b1.
#define SOC_DPTX_HPD_IN_STATUS                        0x0088,26,26 // HPD_IN_STATUS;HPD plug in status.

// SOC_DPTX_HPD_INTERRUPT_ENABLE: 0x008c
#define SOC_DPTX_SINK_IRQ_EVENT_MSK                   0x008c,31,31 // SINK_IRQ_EVENT_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_HOT_PLUG_EVENT_MSK                   0x008c,29,29 // HOT_PLUG_EVENT_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_HOT_UNPLUG_EVENT_MSK                 0x008c,28,28 // HOT_UNPLUG_EVENT_MSK;0: disable interrupt;1: enable interrupt
#define SOC_DPTX_SINK_UNPLUG_ERROR_EVENT_MSK          0x008c,27,27 // SINK_UNPLUG_ERROR_EVENT_MSK;0: disable interrupt;1: enable interrupt

// SOC_DPTX_PHYIF_CTRL_ADDR: 0x0100
#define SOC_DPTX_PHY_BUSY_BYP                         0x0100,31,31 // PHY_BUSY_BYP;lane phybusy status bypass.
#define SOC_DPTX_TPS_SEL                              0x0100,28,25 // TPS_SEL;selects the training pattern.;0: No training pattern. Normal stream data is transmitted instead.;1: TPS1;2: TPS2;3: TPS3
#define SOC_DPTX_XMIT_ENABLE                          0x0100,20,17 // Transmit enable;enable transmitter on the per lane.;Bit17:lane 0;Bit18:lane 1;Bit19:lane 2;Bit20:lane 3
#define SOC_DPTX_PHY_POWERDOWN                        0x0100,16,13 // PHY_POWERDOWN;0x0: Powered on;0xc: PHY is powered down,reference clock can be stopped.
#define SOC_DPTX_PHY_SSC_DIS                          0x0100,8,8   // PHY_SSC_DIS;PHY SSC disable.;0: SSC enable;1: SSC disable
#define SOC_DPTX_PHY_NUM_LANES                        0x0100,6,5   // PHY_NUM_LANES;number of lanes active :;2’b00:1 lane;2’b01:2 lanes;2’b10:4 lanes
#define SOC_DPTX_PHY_RATE                             0x0100,1,0   // PHY_RATE;rate set for the phy.;2’b00:RBR 1.62G;2’b01:HBR 2.7G;2’b10: HBR2 5.4G;2’b11: Reserved

// SOC_DPTX_PHYIF_TX_EQ_ADDR: 0x0104
#define SOC_DPTX_PHY_LANE3_TX_VSWING                  0x0104,21,20 // PHY_LANE3_TX_VSWING;lane3 vswing.
#define SOC_DPTX_PHY_LANE3_TX_PREEMP                  0x0104,19,18 // PHY_LANE3_TX_PREEMP;lane3 preemp.
#define SOC_DPTX_PHY_LANE2_TX_VSWING                  0x0104,15,14 // PHY_LANE2_TX_VSWING;lane2 vswing.
#define SOC_DPTX_PHY_LANE2_TX_PREEMP                  0x0104,13,12 // PHY_LANE2_TX_PREEMP;lane2 preemp.
#define SOC_DPTX_PHY_LANE1_TX_VSWING                  0x0104,9,8   // PHY_LANE1_TX_VSWING;lane1 vswing.
#define SOC_DPTX_PHY_LANE1_TX_PREEMP                  0x0104,7,6   // PHY_LANE1_TX_PREEMP;lane1 preemp.
#define SOC_DPTX_PHY_LANE0_TX_VSWING                  0x0104,3,2   // PHY_LANE0_TX_VSWING;lan0 vswing.
#define SOC_DPTX_PHY_LANE0_TX_PREEMP                  0x0104,1,0   // PHY_LANE0_TX_PREEMP;lane0 preemp.

// SOC_DPTX_MPLL_CTRL0: 0x0180
#define SOC_DPTX_ANA_MPLL_FBDIV_LBIT                  0x0180,31,24 // ANA_MPLL_FBDIV_LBIT;reg_mpll_fbdiv[7:0],The integer part of CORE PLL feedback divide value
#define SOC_DPTX_ANA_MPLL_FBDIV_HBIT                  0x0180,19,16 // ANA_MPLL_FBDIV_HBIT;reg_mpll_fbdiv[11:8],The integer part of CORE PLL feedback divide value
#define SOC_DPTX_ANA_MPLL_PREDIV                      0x0180,13,8  // ANA_MPLL_PREDIV;CORE PLL reference divide value
#define SOC_DPTX_AD_LOCK_COREPLL                      0x0180,7,7   // ad_lock_corepll;1: core pll is locked;0: core pll is not locked
#define SOC_DPTX_ANA_MPLL_DACPD                       0x0180,5,5   // da_mpll_frac_pd[1];Fractional divider control register
#define SOC_DPTX_ANA_MPLL_DSMPD                       0x0180,4,4   // da_mpll_frac_pd[0];Fractional divider control register
#define SOC_DPTX_ANA_MPLL_PD                          0x0180,0,0   // ANA_MPLL_PD;0: power up;1: power down

// SOC_DPTX_MPLL_CTRL1: 0x0184
#define SOC_DPTX_ANA_MPLL_FRAC_LBIT                   0x0184,23,16 // ANA_MPLL_FRAC_LBIT;reg_mpll_frac[7:0],The fractional part of CORE PLL feedback divide value
#define SOC_DPTX_ANA_MPLL_FRAC_MBIT                   0x0184,15,8  // ANA_MPLL_FRAC_MBIT;reg_mpll_frac[15:8],The fractional part of CORE PLL feedback divide value
#define SOC_DPTX_ANA_MPLL_FRAC_HBIT                   0x0184,7,0   // ANA_MPLL_FRAC_HBIT;reg_mpll_frac[23:16],The fractional part of CORE PLL feedback divide value

// SOC_DPTX_MPLL_CTRL2: 0x0188
#define SOC_DPTX_ANA_MPLL_VCOCLK_DIV8_EN              0x0188,16,16 // ANA_MPLL_VCOCLK_DIV8_EN
#define SOC_DPTX_ANA_MPLL_POSTDIVEN                   0x0188,11,11 // ANA_MPLL_POSTDIVEN
#define SOC_DPTX_ANA_MPLL_POSTDIV                     0x0188,10,8  // ANA_MPLL_POSTDIV;CORE PLL reference divide value;3’bxx0: Divided by 2;3’b001: Divided by 4;3’b011: Divided by 8;3’b101: Divided by 16;3’b111: Divided by 32

// SOC_DPTX_PREPLL_CTRL0: 0x0190
#define SOC_DPTX_ANA_PREPLL_FBDIV2_LBIT               0x0190,31,24 // ANA_PREPLL_FBDIV2_LBIT;da_prepll_fbdiv2[7:0],The integer part of PIXEL PLL feedback divide value
#define SOC_DPTX_ANA_PREPLL_FBDIV2_HBIT               0x0190,19,16 // ANA_PREPLL_FBDIV2_HBIT;da_prepll_fbdiv2[11:8],The integer part of PIXEL PLL feedback divide value
#define SOC_DPTX_ANA_PREPLL_PREDIV                    0x0190,13,8  // ANA_PREPLL_PREDIV;PIXEL PLL reference divide value
#define SOC_DPTX_AD_LOCK_PIXELPLL                     0x0190,7,7   // ad_lock_pixelpll ;1: pixel pll is locked;0: pixel pll is not locked
#define SOC_DPTX_ANA_PREPLL_DACPD                     0x0190,5,5   // da_prepll_frac_pd[1];Fractional divider control register
#define SOC_DPTX_ANA_PREPLL_DSMPD                     0x0190,4,4   // da_prepll_frac_pd[0];Fractional divider control register
#define SOC_DPTX_REG_PCLK_OUTPUT_NORMAL               0x0190,1,1   // REG_PCLK_OUTPUT_NORMAL;1: pin_vid_clk_out output is from prepll;0: pin_vid_clk_out output is pin_ref_clk
#define SOC_DPTX_ANA_PREPLL_PD                        0x0190,0,0   // ANA_PREPLL_PD;0: power up;1: power down pixel pll


// SOC_DPTX_PREPLL_CTRL1: 0x0194
#define SOC_DPTX_ANA_PREPLL_PRECLK_DIVM               0x0194,17,16 // ANA_PREPLL_PRECLK_DIVM;PIXEL PLL main clock divider control:;2’b00: Divided by 1;2’b01: Divided by 2;2’b10: Divided by 3;2’b11: Divided by 5
#define SOC_DPTX_ANA_PREPLL_PRECLK_DIVAUX             0x0194,12,8  // ANA_PREPLL_PRECLK_DIVAUX;PIXEL PLL auxiliary clock divider control:;5’h00: Divided by 1;5’h01: Divided by 1;5’h02: Divided by 2;...;5’h3f: Divided by 31

// SOC_DPTX_PREPLL_CTRL2: 0x0198
#define SOC_DPTX_ANA_PREPLL_PCLKDIV5_EN               0x0198,31,31 // ANA_PREPLL_PCLKDIV5_EN;PREPLL pclkdiv5 enable.;0:disable;1:enable
#define SOC_DPTX_ANA_PREPLL_PCLK_DIVAUX               0x0198,28,24 // ANA_PREPLL_PCLK_DIVAUX;PIXEL PLL pixel clock divider control:;5’h00: Divided by 1;5’h01: Divided by 1;5’h02: Divided by 2;...;5’h3f: Divided by 31
#define SOC_DPTX_ANA_PREPLL_LOCK_BYPEN                0x0198,17,17 // ANA_PREPLL_LOCK_BYPEN;prepll lock bypass.;0: prepll lock.;1: bypass,lock always 1.
#define SOC_DPTX_ANA_PREPLL_LOWFRE_EN                 0x0198,16,16 // ANA_PREPLL_LOWFRE_EN
#define SOC_DPTX_ANA_PREPLL_TESTEN                    0x0198,15,15 // ANA_PREPLL_TESTEN
#define SOC_DPTX_ANA_PREPLL_TESTSEL                   0x0198,14,12 // ANA_PREPLL_TESTSEL
#define SOC_DPTX_ANA_PREPLL_HDMI_EN                   0x0198,9,9   // ANA_PREPLL_HDMI_EN
#define SOC_DPTX_ANA_PREPLL_DP_EN                     0x0198,8,8   // ANA_PREPLL_DP_EN

// SOC_DPTX_PREPLL_SSC_CTRL: 0x019c
#define SOC_DPTX_ANA_PREPLL_FRAC2_LBIT                0x019c,23,16 // ANA_PREPLL_FRAC2_LBIT;da_prepll_frac2[7:0],The fractional part of PIXEL PLL feedback divide value
#define SOC_DPTX_ANA_PREPLL_FRAC2_MBIT                0x019c,15,8  // ANA_PREPLL_FRAC2_MBIT;da_prepll_frac2[15:8],The fractional part of PIXEL PLL feedback divide value
#define SOC_DPTX_ANA_PREPLL_FRAC2_HBIT                0x019c,7,0   // ANA_PREPLL_FRAC2_HBIT;da_prepll_frac2[23:16],The fractional part of PIXEL PLL feedback divide value

// SOC_DPTX_ANA_TX_CTRL0: 0x01a0
#define SOC_DPTX_ANA_MPLL_CLKDIV_16M                  0x01a0,13,8  // ANA_MPLL_CLKDIV_16M;MPLL CLK_16M postdiv.

// SOC_DPTX_ANA_TX_ENABLE: 0x01a4
#define SOC_DPTX_ANA_TX_ISEL_DRV_D3                   0x01a4,31,28 // da_tx_isel_drv_d3[3:0];Current bias control register of lane3.;Ibias = 160uA + 40uA * isel[3:0]
#define SOC_DPTX_ANA_TX_ISEL_DRV_D2                   0x01a4,27,24 // da_tx_isel_drv_d2[3:0];Current bias control register of lane2.;Ibias = 160uA + 40uA * isel[3:0]

// SOC_DPTX_ANA_TX_CTRL1: 0x01a8
#define SOC_DPTX_ANA_TX_MAINSEL_D2                    0x01a8,28,24 // da_tx_mainsel_d2[4:0];Output voltage level control registers of lane2.;Imain = Ibias * mainsel[4:0]
#define SOC_DPTX_ANA_TX_MAINSEL_D3                    0x01a8,20,16 // da_tx_mainsel_d3[4:0];Output voltage level control registers of lane3.;Imain = Ibias * mainsel[4:0]
#define SOC_DPTX_ANA_TX_ISEL_DRV_D1                   0x01a8,7,4   // da_tx_isel_drv_d1[3:0];Current bias control register of lane1.;Ibias = 160uA + 40uA * isel[3:0]
#define SOC_DPTX_ANA_TX_ISEL_DRV_D0                   0x01a8,3,0   // da_tx_isel_drv_d0[3:0];Current bias control register of lane0.;Ibias = 160uA + 40uA * isel[3:0]

// SOC_DPTX_ANA_TX_CTRL2: 0x01ac
#define SOC_DPTX_ANA_TX_POSTSEL_D1                    0x01ac,31,28 // da_tx_postsel_d1[3:0];Post-cursor pre-emphasis level control registers of lane1.;Ipost = Ibias * postsel[3:0]
#define SOC_DPTX_ANA_TX_POSTSEL_D0                    0x01ac,27,24 // da_tx_postsel_d0[3:0];Post-cursor pre-emphasis level control registers of lane0.;Ipost = Ibias * postsel[3:0]
#define SOC_DPTX_ANA_TX_POSTSEL_D3                    0x01ac,23,20 // da_tx_postsel_d3[3:0];Post-cursor pre-emphasis level control registers of lane3.;Ipost = Ibias * postsel[3:0]
#define SOC_DPTX_ANA_TX_POSTSEL_D2                    0x01ac,19,16 // da_tx_postsel_d2[3:0];Post-cursor pre-emphasis level control registers of lane2.;Ipost = Ibias * postsel[3:0]
#define SOC_DPTX_DA_TX_MAINSEL_D0_4_0                 0x01ac,12,8  // da_tx_mainsel_d0[4:0];Output voltage level control registers of lane0.;Imain = Ibias * mainsel[4:0]
#define SOC_DPTX_ANA_TX_MAINSEL_D1                    0x01ac,4,0   // da_tx_mainsel_d1[4:0];Output voltage level control registers of lane1.;Imain = Ibias * mainsel[4:0]

// SOC_DPTX_ANA_TX_CTRL3: 0x01b0
#define SOC_DPTX_ANA_TX_MODE_D3                       0x01b0,27,27 // reg_tx_mode_d3/2/1/0;Output driver mode control of 4 data lanes;1’b0: select voltage mode driver;1’b1: enable current mode driver;
#define SOC_DPTX_ANA_TX_MODE_D2                       0x01b0,26,26 //
#define SOC_DPTX_ANA_TX_MODE_D1                       0x01b0,25,25 //
#define SOC_DPTX_ANA_TX_MODE_D0                       0x01b0,24,24 //
#define SOC_DPTX_ANA_TX_PRESEL_D1                     0x01b0,14,12 // da_tx_presel_d1[2:0];Pre-cursor pre-emphasis level control registers of lane1.;Ipre = Ibias * presel[2:0]
#define SOC_DPTX_ANA_TX_PRESEL_D0                     0x01b0,10,8  // da_tx_presel_d0[2:0];Pre-cursor pre-emphasis level control registers of lane0.;Ipre = Ibias * presel[2:0]
#define SOC_DPTX_ANA_TX_PRESEL_D3                     0x01b0,6,4   // da_tx_presel_d3[2:0];Pre-cursor pre-emphasis level control registers of lane3.;Ipre = Ibias * presel[2:0]
#define SOC_DPTX_ANA_TX_PRESEL_D2                     0x01b0,2,0   // da_tx_presel_d2[2:0];Pre-cursor pre-emphasis level control registers of lan2.;Ipre = Ibias * presel[2:0]

// SOC_DPTX_ANA_TX_CTRL7: 0x01c0
#define SOC_DPTX_ANA_BG_RCAL_SEL                      0x01c0,26,25 // ANA_BG_RCAL_SEL;The resistance calibration,adjust the termination resistor to approach the off-chip reference resistor.;W: software configure value.only ANA_RTCAL_BYPASS is 1'b1 valid.;R: analog real value,software configure or hardware generate.
#define SOC_DPTX_ANA_RTCAL_FREQDIV_LBIT               0x01c0,23,16 // ANA_RTCAL_FREQDIV_LBIT;reg_rtcal_freqdiv[7:0],The resistance calibration clock divider.
#define SOC_DPTX_ANA_RTCAL_BYPASS                     0x01c0,15,15 // ANA_RTCAL_BYPASS;Control the configuration method for termination resistance.;0: hardware adjust.;1: bypass,software configure.
#define SOC_DPTX_ANA_RTCAL_FREQDIV_HBIT               0x01c0,14,8  // ANA_RTCAL_FREQDIV_HBIT;reg_rtcal_freqdiv[14:8],The resistance calibration clock divider.

// SOC_DPTX_ANA_TX_CTRL8: 0x01c4
#define SOC_DPTX_ANA_TX_RTM_D3                        0x01c4,29,24 // ANA_TX_RTM_D3;Differential termination resistance control registers of lane3,RT = 4000 / rtm[5:0];W: software configure value.only ANA_RTCAL_BYPASS is 1'b1 valid.;R: analog real value,software configure or hardware generate.
#define SOC_DPTX_ANA_TX_RTM_D2                        0x01c4,21,16 // reg_tx_rtm_d2[5:0];Differential termination resistance control registers of lane2,RT = 4000 / rtm[5:0];W: software configure value.only ANA_RTCAL_BYPASS is 1'b1 valid.;R: analog real value,software configure or hardware generate.
#define SOC_DPTX_ANA_TX_RTM_D1                        0x01c4,13,8  // reg_tx_rtm_d1[5:0];Differential termination resistance control registers of lane1,RT = 4000 / rtm[5:0];W: software configure value.only ANA_RTCAL_BYPASS is 1'b1 valid.;R: analog real value,software configure or hardware generate.
#define SOC_DPTX_ANA_TX_RTM_D0                        0x01c4,5,0   // ANA_TX_RTM_D0;Differential termination resistance control registers of lane0,RT = 4000 / rtm[5:0];W: software configure value.only ANA_RTCAL_BYPASS is 1'b1 valid.;R: analog real value,software configure or hardware generate.

// SOC_DPTX_ANA_TX_AUX_CTRL: 0x01d0
#define SOC_DPTX_ANA_TX_AUX_RX_VSEL                   0x01d0,13,12 //

// SOC_DPTX_ANA_LANE_EMPH: 0x01dc
#define SOC_DPTX_ANA_TX_POSTSEL_PRE_D3                0x01dc,23,20 //
#define SOC_DPTX_ANA_TX_POSTSEL_PRE_D2                0x01dc,19,16 //
#define SOC_DPTX_ANA_TX_POSTSEL_PRE_D1                0x01dc,15,12 //
#define SOC_DPTX_ANA_TX_POSTSEL_PRE_D0                0x01dc,11,8  //
#define SOC_DPTX_ANA_TX_MODE_DE_D3                    0x01dc,7,7   //
#define SOC_DPTX_ANA_TX_MODE_DE_D2                    0x01dc,6,6   //
#define SOC_DPTX_ANA_TX_MODE_DE_D1                    0x01dc,5,5   //
#define SOC_DPTX_ANA_TX_MODE_DE_D0                    0x01dc,4,4   //
#define SOC_DPTX_ANA_TX_MODE_PRE_D3                   0x01dc,3,3   //
#define SOC_DPTX_ANA_TX_MODE_PRE_D2                   0x01dc,2,2   //
#define SOC_DPTX_ANA_TX_MODE_PRE_D1                   0x01dc,1,1   //
#define SOC_DPTX_ANA_TX_MODE_PRE_D0                   0x01dc,0,0   //

// SOC_DPTX_VIDEO_VSAMPLE_REG: 0x0200
#define SOC_DPTX_VIDEO_STREAM_ENABLE                  0x0200,28,28 // Video_stream_en;0:video stream disable;1: video stream enable
#define SOC_DPTX_VIDEO_MAPPING                        0x0200,26,22 // VIDEO_MAPPING;the bit width of each color;0: RGB 6bits;1: RGB 8bits;2: RGB 10bits;3: RGB 12bits;4: RGB 16bits;5: YCbCR4:4:4 8bits;6: YCbCr4:4:4 10bitss;7: YCbCr4:4:4 12bits;8: YCbCr4:4:4 16bits;9: YCbCR4:2:2 8bits;10: YCbCr4:2:2 10bits;11: YCbCr4:2:2 12bits;12: YCbCr4:2:2 16bits;13: YCbCr 4:2:0 8bits ;14: YCbCr 4:2:0 10bits ;15: YCbCr 4:2:0 12bits ;16: YCbCr 4:2:0 16bits;others: Reserved
#define SOC_DPTX_STREAM_ENC_EN                        0x0200,18,18 // STREAM_ENC_EN;alternate_scrambler_reset_capable,a setting of 1 indicates that this is an DP device that can use the DP alternate scrambler reset value of FFFEh

// SOC_DPTX_VINPUT_POLARITY: 0x020c
#define SOC_DPTX_HSYNC_IN_POLARITY                    0x020c,29,29 // reg_tx_polarity[1:0];polarity of hsync and vsync.;bit1 : Vsync polarity ;bit0 : Hsync polarity;if the vsync/hsync of simulation video source is active low,then the corresponding bit in reg_tx_polarity should set 0; or active high to set 1.
#define SOC_DPTX_VSYNC_IN_POLARITY                    0x020c,28,28 //

// SOC_DPTX_VIDEO_CONFIG2: 0x0210
#define SOC_DPTX_VACTIVE                              0x0210,31,16 // VACTIVE;reg_tx_vactive [15:0],Active vertical pixels per line
#define SOC_DPTX_VBLANK                               0x0210,15,0  // VBLANK;reg_tx_vblank [15:0],vtotal – vactive

// SOC_DPTX_VIDEO_CONFIG1: 0x0214
#define SOC_DPTX_HBLANK                               0x0214,29,16 // HBLANK;reg_tx_hblank [13:0],htotal – hactive
#define SOC_DPTX_HACTIVE                              0x0214,15,0  // HACTIVE;reg_tx_hactive [15:0],Active horizontal pixels per

// SOC_DPTX_VIDEO_CONFIG5: 0x0218
#define SOC_DPTX_AVERAGE_BYTES_PER_TU                 0x0218,31,25 // reg_avg_per_tu_int;integer portion of average valid symbol per Transfer Unit,calculate formula refer to DP spec.;For example,pixel clock is 27M RGB888,@2.7Gbps 4lanes,then average valid symbol per Transfer Unit  = 4.8,configure reg_avg_per_tu_int to 4
#define SOC_DPTX_AVERAGE_BYTES_PER_TU_FRAC            0x0218,24,21 // reg_avg_per_tu_frac;fractional portion of average valid symbol per Transfer Unit,calculate formula refer to DP spec.;For example,pixel clock is 27M RGB888,@2.7Gbps 4lanes,then average valid symbol per Transfer Unit  = 4.8,configure reg_avg_per_tu_frac to 8
#define SOC_DPTX_INIT_THRESHOLD                       0x0218,18,10 // reg_line_rd_thres;An asynchronous fifo needed to deal with video data from pixel clock domain to link clock domain ,reg_line_rd_thres is related to read enable of fifo. According to video format and main link rate,this value may changed as  following description:;reg_line_rd_thres = ; (average valid symbol per TU)  < 6 ? 7'd32 :;(hblank < 80) ? 7'd12 : 7'd16;

// SOC_DPTX_VIDEO_CONFIG3: 0x021c
#define SOC_DPTX_H_FRONT_PORCH                        0x021c,31,16 // H_FRONT_PORCH;reg_tx_h_front_porch [15:0],htotal – hactive – hstart
#define SOC_DPTX_H_SYNC_WIDTH                         0x021c,15,0  // H_SYNC_WIDTH;reg_tx_hswidth [15:0],Hsync width

// SOC_DPTX_VIDEO_CONFIG4: 0x0220
#define SOC_DPTX_V_FRONT_PORCH                        0x0220,31,16 // V_FRONT_PORCH;reg_tx_v_front_porch [15:0],vtotal – vactive – vstart
#define SOC_DPTX_V_SYNC_WIDTH                         0x0220,15,0  // V_SYNC_WIDTH;reg_tx_vswidth [15:0],Vsync width

// SOC_DPTX_VIDEO_MSA1: 0x0224
#define SOC_DPTX_HSTART                               0x0224,31,16 // reg_tx_hstart [15:0];Hsync width + Hback porch width
#define SOC_DPTX_VSTART                               0x0224,15,0  // reg_tx_vstart[15:0];Vsync width + Vback porch width

// SOC_DPTX_VIDEO_MSA2: 0x0228
#define SOC_DPTX_MISC0                                0x0228,7,0   // reg_tx_msa_misc0[7:0];MSA Miscellaneous0 field;bit0: 1’b0,IP Link clock and main video stream clock asynchronous. ;tips: the bit must set 1’b0.;bit1~bit7: color encoding format ,please refer to DP spec;for example: 6’b0000000,6’b 0010000,;6’b 0100000,6’b 0110000,6’b 1000000 ;(6,8,10,12,16 bits/color respectively)

// SOC_DPTX_VIDEO_MSA3: 0x022c
#define SOC_DPTX_NVID                                 0x022c,31,8  // NVID
#define SOC_DPTX_MISC1                                0x022c,7,0   // reg_tx_msa_misc1[7:0];MSA Miscellaneous1 field;please refer to DP spec

// SOC_DPTX_VIDEO_HBLANK_INTERVAL: 0x0230
#define SOC_DPTX_HBLANK_INTERVAL                      0x0230,31,16 // hblank_link_cyc;How many link clock cycles of hblank .;hblank *link_clk/pixel_clk,;For example,pixel clock is 27M hblank 138,@2.7Gbps 4lanes,hblank_link_cyc; = 138*67.5/27. link_clk = symbol clock /4

// SOC_DPTX_VIDEO_COLORBAR_CFG: 0x0238
#define SOC_DPTX_VID_BIST_EN                          0x0238,0,0   // VID_BIST_EN;video bist mode enable.

// SOC_DPTX_AUX_STS_REG: 0x0400
#define SOC_DPTX_AUX_STATUS                           0x0400,31,24 // Aux_reply_cmd;4’b0000:ack;4’b0001:nack
#define SOC_DPTX_AUX_REPLY_ERR_CODE                   0x0400,6,4   // AUX_REPLY_ERR_CODE;0: no error;1: AUX DATA PAYLOAD detect less than 8.;4: AUX STOP_L STATE detect greater than 2.;5: AUX SYNC_H STATE detect greater than 2.;6: AUX SYNC_L STATE detect greater than 2.;7: AUX STOP_H STATE detect greater than 2.;others: reserved

// SOC_DPTX_AUX_CMD_REG: 0x0404
#define SOC_DPTX_AUX_LENGTH                           0x0404,27,24 // AUX_LENGTH;length,the number of bytes to be transfer by AUX requester,equal to (bytes of data )-1,please refer to Native AUX syntax of spec.
#define SOC_DPTX_AUX_ADDR                             0x0404,23,4  // AUX_ADDR;addr[19:0],the address to be transfer by AUX requester,please refer to Native AUX syntax of spec.
#define SOC_DPTX_AUX_CMD_TYPE                         0x0404,3,0   // AUX_CMD_TYPE;AUX cmd[3:0],the command to be transfer by AUX requester.;4’b1000->Native AUX Wite; ;4’b1001->Native AUX read; ;Please refer to Native AUX syntax of DP spec.

// SOC_DPTX_AUX_START_REG: 0x0408
#define SOC_DPTX_AUX_START                            0x0408,31,31 // AUX_START;the trigger signal to start a Wite/read request by AUX requester(Source Devive),wirte to 1.

// SOC_DPTX_AUX_DATA4_REG: 0x040c
#define SOC_DPTX_AUX_DATA4                            0x040c,31,0  // cfg_phy_aux_data[127:96],cfg_phy_aux_data composed of 0x0418,0x0414,0x0410,0x040C,totally 16bytes,including data(8bits*16),payload for Wite request or read request. ;tips: for a RWite request ,filled attached data needs to be transmitted;for a read reply ,stored the replied data.

// SOC_DPTX_AUX_DATA3_REG: 0x0410
#define SOC_DPTX_AUX_DATA3                            0x0410,31,0  // cfg_phy_aux_data[95:64],cfg_phy_aux_data composed of 0x0418,0x0414,0x0410,0x040C,totally 16bytes,including data(8bits*16),payload for RWite request or read request.;tips: for a RWite request ,filled attached data needs to be transmitted;for a read reply ,stored the replied data.

// SOC_DPTX_AUX_DATA2_REG: 0x0414
#define SOC_DPTX_AUX_DATA2                            0x0414,31,0  // cfg_phy_aux_data[63:32],cfg_phy_aux_data composed of 0x0418,0x0414,0x0410,0x040C,totally 16bytes,including data(8bits*16),payload for RWite request or read request.;tips: for a RWite request ,filled attached data needs to be transmitted;for a read reply ,stored the replied data.

// SOC_DPTX_AUX_DATA1_REG: 0x0418
#define SOC_DPTX_AUX_DATA1                            0x0418,31,0  // cfg_phy_aux_data[31:0],cfg_phy_aux_data composed of 0x0418,0x0414,0x0410,0x040C,totally 16bytes,including data(8bits*16),payload for RWite request or read request.;tips: for a RWite request ,filled attached data needs to be transmitted;for a read reply ,stored the replied data.

#endif /* __SPACEMIT_INNO_DP_H__ */
