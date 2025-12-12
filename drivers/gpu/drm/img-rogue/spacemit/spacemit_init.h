/* SPDX-License-Identifier: GPL-2.0-only OR MIT
 *
 *	@File
 *	@Title          System Configuration
 *	@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 *	@Description    System Configuration functions
 *	@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined(__DOVE_INIT__)
#define __DOVE_INIT__

#include "img_types.h"
#include "pvrsrv_error.h"
#include "pvrsrv_device.h"
#include "servicesext.h"
#include <linux/version.h>

struct st_context {
	PVRSRV_DEVICE_CONFIG    *dev_config;
	struct clk          *gpu_clk;
	/* mutex protect for set power state */
	struct mutex set_power_state;
	IMG_BOOL            gpu_active;
	IMG_BOOL            bEnablePd;
};

struct st_context * RgxStInit(PVRSRV_DEVICE_CONFIG* psDevConfig);
void RgxStUnInit(struct st_context *platform);
void RgxResume(struct st_context *platform);
void RgxSuspend(struct st_context *platform);
PVRSRV_ERROR STPrePowerState(IMG_HANDLE hSysData,
							 PVRSRV_SYS_POWER_STATE eNewPowerState,
							 PVRSRV_SYS_POWER_STATE eCurrentPowerState,
							 PVRSRV_POWER_FLAGS ePwrFlags);
PVRSRV_ERROR STPostPowerState(IMG_HANDLE hSysData,
							  PVRSRV_SYS_POWER_STATE eNewPowerState,
							  PVRSRV_SYS_POWER_STATE eCurrentPowerState,
							  PVRSRV_POWER_FLAGS ePwrFlags);
void stSetFrequency(IMG_HANDLE hSysData, IMG_UINT32 ui32Frequency);
void stSetVoltage(IMG_HANDLE hSysData, IMG_UINT32 ui32Voltage);
#endif
