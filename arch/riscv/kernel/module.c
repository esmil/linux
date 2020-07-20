// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Copyright (C) 2017 Zihao Yu
 *  Copyright (C) 2020 Emil Renner Berthing
 */

#include <linux/elf.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/moduleloader.h>
#include <linux/vmalloc.h>
#include <linux/sizes.h>
#include <linux/pgtable.h>
#include <asm/insn.h>
#include <asm/sections.h>

static int riscv_insn_rmw(u8 *location, u32 mask, u32 value)
{
	u16 *parcel = (u16 *)location;
	u32 insn = (u32)parcel[0] | (u32)parcel[1] << 16;

	insn &= ~mask;
	insn |= value;

	parcel[0] = insn;
	parcel[1] = insn >> 16;
	return 0;
}

static int riscv_insn_rvc_rmw(u8 *location, u16 mask, u16 value)
{
	u16 *parcel = (u16 *)location;

	*parcel = (*parcel & ~mask) | value;
	return 0;
}

static int apply_r_riscv_32_rela(struct module *me, u8 *location, Elf_Addr v)
{
	if (v != (u32)v) {
		pr_err("%s: value %016llx out of range for 32-bit field\n",
		       me->name, (long long)v);
		return -EINVAL;
	}
	*(u32 *)location = v;
	return 0;
}

static int apply_r_riscv_64_rela(struct module *me, u8 *location, Elf_Addr v)
{
	*(u64 *)location = v;
	return 0;
}

static int apply_r_riscv_branch_rela(struct module *me, u8 *location,
				     Elf_Addr v)
{
	ptrdiff_t offset = (u8 *)v - location;

	return riscv_insn_rmw(location,
			RISCV_INSN_B_IMM_MASK,
			riscv_insn_b_imm(offset));
}

static int apply_r_riscv_jal_rela(struct module *me, u8 *location,
				  Elf_Addr v)
{
	ptrdiff_t offset = (u8 *)v - location;

	return riscv_insn_rmw(location,
			RISCV_INSN_J_IMM_MASK,
			riscv_insn_j_imm(offset));
}

static int apply_r_riscv_rvc_branch_rela(struct module *me, u8 *location,
					 Elf_Addr v)
{
	ptrdiff_t offset = (u8 *)v - location;

	return riscv_insn_rvc_rmw(location,
			RISCV_INSN_CB_IMM_MASK,
			riscv_insn_rvc_branch_imm(offset));
}

static int apply_r_riscv_rvc_jump_rela(struct module *me, u8 *location,
				       Elf_Addr v)
{
	ptrdiff_t offset = (u8 *)v - location;

	return riscv_insn_rvc_rmw(location,
			RISCV_INSN_CJ_IMM_MASK,
			riscv_insn_rvc_jump_imm(offset));
}

static int apply_r_riscv_pcrel_hi20_rela(struct module *me, u8 *location,
					 Elf_Addr v)
{
	ptrdiff_t offset = (u8 *)v - location;

	if (!riscv_insn_valid_32bit_offset(offset)) {
		pr_err(
		  "%s: target %016llx can not be addressed by the 32-bit offset from PC = %p\n",
		  me->name, (long long)v, location);
		return -EINVAL;
	}

	return riscv_insn_rmw(location,
			RISCV_INSN_U_IMM_MASK,
			riscv_insn_u_imm(offset + 0x800));
}

static int apply_r_riscv_pcrel_lo12_i_rela(struct module *me, u8 *location,
					   Elf_Addr v)
{
	/* v is already the relative offset */
	return riscv_insn_rmw(location,
			RISCV_INSN_I_IMM_MASK,
			riscv_insn_i_imm(v));
}

static int apply_r_riscv_pcrel_lo12_s_rela(struct module *me, u8 *location,
					   Elf_Addr v)
{
	/* v is already the relative offset */
	return riscv_insn_rmw(location,
			RISCV_INSN_S_IMM_MASK,
			riscv_insn_s_imm(v));
}

static int apply_r_riscv_hi20_rela(struct module *me, u8 *location,
				   Elf_Addr v)
{
	if (IS_ENABLED(CONFIG_CMODEL_MEDLOW)) {
		pr_err(
		  "%s: target %016llx can not be addressed by the 32-bit offset from PC = %p\n",
		  me->name, (long long)v, location);
		return -EINVAL;
	}

	return riscv_insn_rmw(location,
			RISCV_INSN_U_IMM_MASK,
			riscv_insn_u_imm(v + 0x800));
}

static int apply_r_riscv_lo12_i_rela(struct module *me, u8 *location,
				     Elf_Addr v)
{
	/* Skip medlow checking because of filtering by HI20 already */
	return riscv_insn_rmw(location,
			RISCV_INSN_I_IMM_MASK,
			riscv_insn_i_imm(v));
}

static int apply_r_riscv_lo12_s_rela(struct module *me, u8 *location,
				     Elf_Addr v)
{
	/* Skip medlow checking because of filtering by HI20 already */
	return riscv_insn_rmw(location,
			RISCV_INSN_S_IMM_MASK,
			riscv_insn_s_imm(v));
}

static int apply_r_riscv_got_hi20_rela(struct module *me, u8 *location,
				       Elf_Addr v)
{
	ptrdiff_t offset = (u8 *)v - location;

	/* Always emit the got entry */
	if (IS_ENABLED(CONFIG_MODULE_SECTIONS)) {
		unsigned long entry = module_emit_got_entry(me, v);
		offset = (u8 *)entry - location;
	} else {
		pr_err(
		  "%s: can not generate the GOT entry for symbol = %016llx from PC = %p\n",
		  me->name, (long long)v, location);
		return -EINVAL;
	}

	return riscv_insn_rmw(location,
			RISCV_INSN_U_IMM_MASK,
			riscv_insn_u_imm(offset + 0x800));
}

static int apply_r_riscv_call_plt_rela(struct module *me, u8 *location,
				       Elf_Addr v)
{
	ptrdiff_t offset = (u8 *)v - location;

	if (!riscv_insn_valid_32bit_offset(offset)) {
		/* Only emit the plt entry if offset over 32-bit range */
		if (IS_ENABLED(CONFIG_MODULE_SECTIONS)) {
			unsigned long entry = module_emit_plt_entry(me, v);
			offset = (u8 *)entry - location;
		} else {
			pr_err(
			  "%s: target %016llx can not be addressed by the 32-bit offset from PC = %p\n",
			  me->name, (long long)v, location);
			return -EINVAL;
		}
	}

	riscv_insn_rmw(location,
			RISCV_INSN_U_IMM_MASK,
			riscv_insn_u_imm(offset + 0x800));
	return riscv_insn_rmw(location + 4,
			RISCV_INSN_I_IMM_MASK,
			riscv_insn_i_imm(offset));
}

static int apply_r_riscv_call_rela(struct module *me, u8 *location,
				   Elf_Addr v)
{
	ptrdiff_t offset = (u8 *)v - location;

	if (!riscv_insn_valid_32bit_offset(offset)) {
		pr_err(
		  "%s: target %016llx can not be addressed by the 32-bit offset from PC = %p\n",
		  me->name, (long long)v, location);
		return -EINVAL;
	}

	riscv_insn_rmw(location,
			RISCV_INSN_U_IMM_MASK,
			riscv_insn_u_imm(offset + 0x800));
	return riscv_insn_rmw(location + 4,
			RISCV_INSN_I_IMM_MASK,
			riscv_insn_i_imm(offset));
}

static int apply_r_riscv_relax_rela(struct module *me, u8 *location,
				    Elf_Addr v)
{
	return 0;
}

static int apply_r_riscv_align_rela(struct module *me, u8 *location,
				    Elf_Addr v)
{
	pr_err(
	  "%s: The unexpected relocation type 'R_RISCV_ALIGN' from PC = %p\n",
	  me->name, location);
	return -EINVAL;
}

static int apply_r_riscv_add32_rela(struct module *me, u8 *location,
				    Elf_Addr v)
{
	*(u32 *)location += (u32)v;
	return 0;
}

static int apply_r_riscv_add64_rela(struct module *me, u8 *location,
				    Elf_Addr v)
{
	*(u64 *)location += (u64)v;
	return 0;
}

static int apply_r_riscv_sub32_rela(struct module *me, u8 *location,
				    Elf_Addr v)
{
	*(u32 *)location -= (u32)v;
	return 0;
}

static int apply_r_riscv_sub64_rela(struct module *me, u8 *location,
				    Elf_Addr v)
{
	*(u64 *)location -= (u64)v;
	return 0;
}

static int (*reloc_handlers_rela[])(struct module *me, u8 *location,
				Elf_Addr v) = {
	[R_RISCV_32]			= apply_r_riscv_32_rela,
	[R_RISCV_64]			= apply_r_riscv_64_rela,
	[R_RISCV_BRANCH]		= apply_r_riscv_branch_rela,
	[R_RISCV_JAL]			= apply_r_riscv_jal_rela,
	[R_RISCV_RVC_BRANCH]		= apply_r_riscv_rvc_branch_rela,
	[R_RISCV_RVC_JUMP]		= apply_r_riscv_rvc_jump_rela,
	[R_RISCV_PCREL_HI20]		= apply_r_riscv_pcrel_hi20_rela,
	[R_RISCV_PCREL_LO12_I]		= apply_r_riscv_pcrel_lo12_i_rela,
	[R_RISCV_PCREL_LO12_S]		= apply_r_riscv_pcrel_lo12_s_rela,
	[R_RISCV_HI20]			= apply_r_riscv_hi20_rela,
	[R_RISCV_LO12_I]		= apply_r_riscv_lo12_i_rela,
	[R_RISCV_LO12_S]		= apply_r_riscv_lo12_s_rela,
	[R_RISCV_GOT_HI20]		= apply_r_riscv_got_hi20_rela,
	[R_RISCV_CALL_PLT]		= apply_r_riscv_call_plt_rela,
	[R_RISCV_CALL]			= apply_r_riscv_call_rela,
	[R_RISCV_RELAX]			= apply_r_riscv_relax_rela,
	[R_RISCV_ALIGN]			= apply_r_riscv_align_rela,
	[R_RISCV_ADD32]			= apply_r_riscv_add32_rela,
	[R_RISCV_ADD64]			= apply_r_riscv_add64_rela,
	[R_RISCV_SUB32]			= apply_r_riscv_sub32_rela,
	[R_RISCV_SUB64]			= apply_r_riscv_sub64_rela,
};

int apply_relocate_add(Elf_Shdr *sechdrs, const char *strtab,
		       unsigned int symindex, unsigned int relsec,
		       struct module *me)
{
	Elf_Rela *rel = (Elf_Rela *)sechdrs[relsec].sh_addr;
	unsigned int entries = sechdrs[relsec].sh_size / sizeof(*rel);
	unsigned int i;

	pr_debug("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);

	for (i = 0; i < entries; i++) {
		Elf_Sym *sym = (Elf_Sym *)sechdrs[symindex].sh_addr
			+ ELF_RISCV_R_SYM(rel[i].r_info);
		Elf_Addr loc = sechdrs[sechdrs[relsec].sh_info].sh_addr
			+ rel[i].r_offset;
		unsigned int type = ELF_RISCV_R_TYPE(rel[i].r_info);
		int (*handler)(struct module *me, u8 *location, Elf_Addr v);
		Elf_Addr v;
		int res;

		if (IS_ERR_VALUE(sym->st_value)) {
			/* Ignore unresolved weak symbol */
			if (ELF_ST_BIND(sym->st_info) == STB_WEAK)
				continue;
			pr_warn("%s: Unknown symbol %s\n",
				me->name, strtab + sym->st_name);
			return -ENOENT;
		}

		if (type < ARRAY_SIZE(reloc_handlers_rela))
			handler = reloc_handlers_rela[type];
		else
			handler = NULL;

		if (!handler) {
			pr_err("%s: Unknown relocation type %u\n",
			       me->name, type);
			return -EINVAL;
		}

		v = sym->st_value + rel[i].r_addend;

		if (type == R_RISCV_PCREL_LO12_I || type == R_RISCV_PCREL_LO12_S) {
			unsigned int j;

			/* find the corresponding HI20 entry */
			for (j = 0; j < entries; j++) {
				Elf_Sym *hi20_sym = (Elf_Sym *)sechdrs[symindex].sh_addr
					+ ELF_RISCV_R_SYM(rel[j].r_info);
				Elf_Addr hi20_loc = sechdrs[sechdrs[relsec].sh_info].sh_addr
					+ rel[j].r_offset;
				unsigned int hi20_type = ELF_RISCV_R_TYPE(rel[j].r_info);

				if (hi20_loc != sym->st_value ||
						(hi20_type != R_RISCV_PCREL_HI20 &&
						 hi20_type != R_RISCV_GOT_HI20))
					continue;

				/* calculate relative offset */
				v = hi20_sym->st_value + rel[j].r_addend;

				if (IS_ENABLED(CONFIG_MODULE_SECTIONS) &&
						hi20_type == R_RISCV_GOT_HI20)
					v = module_emit_got_entry(me, v);

				v -= hi20_loc;
				goto handle_reloc;
			}

			pr_err("%s: Cannot find HI20 relocation information\n",
					me->name);
			return -EINVAL;
		}
handle_reloc:
		res = handler(me, (u8 *)loc, v);
		if (res)
			return res;
	}

	return 0;
}

#if defined(CONFIG_MMU) && defined(CONFIG_64BIT)
#define VMALLOC_MODULE_START \
	 max(PFN_ALIGN((unsigned long)&_end - SZ_2G), VMALLOC_START)
void *module_alloc(unsigned long size)
{
	return __vmalloc_node_range(size, 1, VMALLOC_MODULE_START,
				    VMALLOC_END, GFP_KERNEL,
				    PAGE_KERNEL_EXEC, 0, NUMA_NO_NODE,
				    __builtin_return_address(0));
}
#endif
