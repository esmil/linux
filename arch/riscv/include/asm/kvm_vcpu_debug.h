/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 SiFive
 *
 * Authors:
 *	Yong-Xuan Wang <yongxuan.wang@sifive.com>
 */

#ifndef __KVM_VCPU_RISCV_DEBUG_H
#define __KVM_VCPU_RISCV_DEBUG_H

#include <linux/types.h>

void kvm_riscv_debug_vcpu_swap_in_guest_context(struct kvm_vcpu *vcpu);
void kvm_riscv_debug_vcpu_swap_in_host_context(struct kvm_vcpu *vcpu);

#endif
