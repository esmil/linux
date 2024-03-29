// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 SiFive
 */

#include <linux/kvm_host.h>
#include <asm/switch_to.h>

void kvm_riscv_debug_vcpu_swap_in_guest_context(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_sdtrig_csr *csr = &vcpu->arch.sdtrig_csr;
	unsigned long hcontext = vcpu->kvm->arch.hcontext;

	if (has_hcontext())
		csr_write(CSR_HCONTEXT, hcontext);
	if (has_scontext())
		vcpu->arch.host_scontext = csr_swap(CSR_SCONTEXT, csr->scontext);
}

void kvm_riscv_debug_vcpu_swap_in_host_context(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_sdtrig_csr *csr = &vcpu->arch.sdtrig_csr;

	/* Hypervisor uses the hcontext ID 0 */
	if (has_hcontext())
		csr_write(CSR_HCONTEXT, 0);
	if (has_scontext())
		csr->scontext = csr_swap(CSR_SCONTEXT, vcpu->arch.host_scontext);
}
