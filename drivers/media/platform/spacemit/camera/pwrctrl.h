/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 SPACEMIT Micro Limited
 * All Rights Reserved.
 */

#ifndef PWRCTRL_H_CAE28HFR
#define PWRCTRL_H_CAE28HFR

#define CONFIG_CAMERA_OF_CLOCK (1)

int bare_cpp_power_on(void);
int bare_cpp_power_off(void);
int bare_isp_power_on(void);
int bare_isp_power_off(void);
int bare_ccic_power_on(void);
int bare_ccic_power_off(void);
int bare_sensor_mclk_enable(int clkId, unsigned long rate);
int bare_sensor_mclk_disable(int clkId);

#endif /* end of include guard: PWRCTRL_H_CAE28HFR */
