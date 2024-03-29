// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 SiFive
 */

#include <linux/kvm_host.h>
#include <asm/switch_to.h>

DEFINE_SPINLOCK(hcontext_lock);
unsigned long *hcontext_bitmap;
unsigned long hcontext_bitmap_len;

static __always_inline bool has_hcontext(void)
{
	return static_branch_likely(&use_hcontext);
}

void kvm_riscv_debug_init(void)
{
	/*
	 * As from riscv-debug-spec, Chapter 5.7.9:
	 * If the H extension is implemented, it’s recommended to
	 * implement no more than 7 bits on RV32 and 14 on RV64.
	 * Allocating bit array according to spec size.
	 */
#if __riscv_xlen > 32
	unsigned long tmp = atomic_long_read(&hcontext_id_share) & GENMASK(13, 0);
#else
	unsigned long tmp = atomic_long_read(&hcontext_id_share) & GENMASK(6, 0);
#endif
	if (has_hcontext()) {
		while (tmp) {
			kvm_info("hcontext: try to allocate 0x%lx-bit array\n", tmp);
			hcontext_bitmap_len = tmp + 1;
			hcontext_bitmap = bitmap_zalloc(tmp, 0);
			if (hcontext_bitmap)
				break;
			tmp = tmp >> 1;
		}

		if (tmp == 0) {
			/* We can't allocate any space for hcontext bitmap */
			static_branch_disable(&use_hcontext);
		} else {
			/* ID 0 is hypervisor */
			set_bit(0, hcontext_bitmap);
		}
	}
}

void kvm_riscv_debug_exit(void)
{
	if (has_hcontext()) {
		static_branch_disable(&use_hcontext);
		kfree(hcontext_bitmap);
	}
}

void kvm_riscv_debug_get_hcontext_id(struct kvm *kvm)
{
	if (has_hcontext()) {
		unsigned long free_id;

		spin_lock(&hcontext_lock);
		free_id = find_first_zero_bit(hcontext_bitmap, hcontext_bitmap_len);

		/* share the maximum ID when we run out of the hcontext ID */
		if (free_id <= hcontext_bitmap_len)
			set_bit(free_id, hcontext_bitmap);
		else
			free_id -= 1;

		kvm->arch.hcontext = free_id;
		spin_unlock(&hcontext_lock);
	}
}

void kvm_riscv_debug_return_hcontext_id(struct kvm *kvm)
{
	if (has_hcontext()) {
		spin_lock(&hcontext_lock);
		clear_bit(kvm->arch.hcontext, hcontext_bitmap);
		spin_unlock(&hcontext_lock);
	}
}

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
