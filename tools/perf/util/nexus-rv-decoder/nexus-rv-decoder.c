// SPDX-License-Identifier: GPL-2.0

#include <linux/err.h>
#include <linux/zalloc.h>
#include <linux/types.h>
#include <errno.h>
#include <api/fs/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../debug.h"
#include "../color.h"
#include "nexus-rv-decoder.h"
#include "nexus-rv-msg.h"
#include "../../../arch/riscv/include/asm/insn.h"

#define DEFMT_BUF_SIZE 32
#define STACK_SIZE 1024

#define RV_REGNO_ZERO   0
#define RV_REGNO_RA	1

static struct nexus_rv_defmt_buf* nexus_rv_get_buf(struct nexus_rv_defmt_buf defmt_bufs[], unsigned char id)
{
	if (defmt_bufs[id].buf != NULL)
		return &defmt_bufs[id];

	defmt_bufs[id].buf = (unsigned char*)malloc(DEFMT_BUF_SIZE);
	if (!defmt_bufs[id].buf)
		return NULL;

	defmt_bufs[id].size = 0;
	defmt_bufs[id].capacity = DEFMT_BUF_SIZE;

	return &defmt_bufs[id];
}

// remove coresight formatter frame
static int nexus_rv_pkt_defmt(struct nexus_rv_defmt_buf defmt_bufs[], FILE *nexus, const unsigned char *buf, size_t len)
{
	unsigned char data_byte, flag_byte;
	int is_id;
	unsigned char cur_id = 0, old_id, new_id, data;

	// 16 bytes is output by coresight trace formatter
	while (len >= 16) {
		flag_byte = buf[15];
		for (int i = 0; i < 15; i++) {
			data_byte = buf[i];
			if ((i & 1) == 0) {
				is_id = data_byte & 1;
				if (is_id) {
					old_id = new_id;
					new_id = data_byte >> 1; // get new_id
					if ((flag_byte >> (i / 2)) & 1) {
						// 1 = next byte corresponds to the old_id
						cur_id = old_id;
						if (i == 14) {
							pr_err("Encoder: last id byte must with flag=0");
							return -EINVAL;
						}
					} else {
						// 0 = next byte corresponds to the old_id
						cur_id = new_id;
					}
				} else {
					// get data when data_byte[0] is clear
					data = data_byte | ((flag_byte >> (i / 2)) & 1);
				}
			} else {
				is_id = 0;
				data = data_byte;
			}

			// handle data
			if (!is_id) {
				struct nexus_rv_defmt_buf *defmt_buf = nexus_rv_get_buf(defmt_bufs, cur_id);
				if (!defmt_buf)
					return -ENOMEM;

				if (defmt_buf->size >= defmt_buf->capacity) {
					defmt_buf->capacity *= 2;
					defmt_buf->buf = realloc(defmt_buf->buf, defmt_buf->capacity);
					if (!defmt_buf->buf)
						return -ENOMEM;
				}

				defmt_buf->buf[defmt_buf->size++] = data; // write to buffer
				// If this is end byte for NEXUS MSG
				if ((data & 3) == 0x3) {
					size_t n = fwrite(defmt_buf->buf, defmt_buf->size, 1, nexus);
					if (n != 1) {
						pr_err("Encoder: failed to write nexus data\n");
						return -EINVAL;
					}
					defmt_buf->size = 0;
				}
				if (cur_id != new_id)
					cur_id = new_id;
			}
		}
		buf += 16;
		len -= 16;
	}
	return 0;
}

// dump all nexus messages (from nexus file)
static int nexus_rv_pkt_dump(struct nexus_rv_pkt_decoder *decoder)
{
	const char *color = PERF_COLOR_BLUE;
	int fld_def  = -1;
	int fld_bits = 0;
	u64 fld_val = 0;

	int msg_cnt    = 0;
	int msg_bytes  = 0;
	int msg_errors = 0;
	int idle_cnt   = 0;

	unsigned int tcode = 0;
	unsigned int correlation_hist = 0;
	unsigned int resourcefull_hrepeat = 0;

	unsigned char msg_byte = 0;
	unsigned int mdo = 0;
	unsigned int mseo = 0;
	for (;;) {
		if (fread(&msg_byte, 1, 1, decoder->nexus) != 1) break;  // EOF

#if 0
		if (msg_cnt > 0 && fld_def < 0)
			printf("\n");
		color_fprintf(stdout, color, "0x%02X ", msg_byte);
		for (int b = 0x80; b != 0; b >>= 1) {
			if (b == 0x2)
				color_fprintf(stdout, color, "_");
			if (msg_byte & b)
				color_fprintf(stdout, color, "1");
			else
				color_fprintf(stdout, color, "0");
		}
		color_fprintf(stdout, color, ":");
#endif

		mdo  = msg_byte >> 2;
		mseo = msg_byte & 0x3;

		if (mseo == 0x2) {
			color_fprintf(stdout, color,
				      " ERROR: At offset %d: MSEO='10' is not allowed\n",
				      msg_bytes + idle_cnt);
			return -EINVAL;  // Error return
		}

		if (fld_def < 0) {
			if (mseo == 0x3)  {
				color_fprintf(stdout, color, "MSG #%d +%d - IDLE\n", msg_cnt, msg_bytes);
				msg_cnt++;
				msg_bytes++;
				idle_cnt++;
				continue;
			}

			if (mseo != 0x0) {
				color_fprintf(stdout, color,
					      " ERROR: At offset %d: Message must start from MSEO='00'\n",
					      msg_bytes + idle_cnt);
				return -EINVAL;  // Error return
			}

			for (int d = 0; NEXUS_MSG_DEF[d].def != 0; d++)
			{
				if ((NEXUS_MSG_DEF[d].def & 0x100) == 0)
					continue;
				if ((NEXUS_MSG_DEF[d].def & 0xFF) == mdo) {
					fld_def = d; // Found TCODE
					tcode = mdo;
					break;
				}
			}

			if (fld_def < 0) {
				color_fprintf(stdout, color,
					      " ERROR: At offset %d: Message with TCODE=%d is not defined for N-Trace\n",
					      msg_bytes + idle_cnt, mdo);
				return -EINVAL;
			}

			color_fprintf(stdout, color, "MSG #%d +%d - %s TCODE[6]=%d",
				      msg_cnt, msg_bytes, NEXUS_MSG_DEF[fld_def].name, mdo);
			msg_cnt++;
			msg_bytes++;

			if (mdo == NEXUS_TCODE_Error)
				msg_errors++;

			fld_def++;
			fld_bits = 0;
			fld_val  = 0;
			continue;
		}

		// Accumulate 'mdo' to field value
		fld_val  |= (((u64)mdo) << fld_bits);
		fld_bits += 6;

		msg_bytes++;

		// Process fixed size fields (there may be more than one in one MDO record)
		while (NEXUS_MSG_DEF[fld_def].def & 0x200)
		{
			int fld_size = NEXUS_MSG_DEF[fld_def].def & 0xFF;
			if (fld_size & 0x80) {
				// Size of this field is defined by parameter ...
				fld_size = decoder->src_bits;
			}
			if (fld_bits < fld_size)
				break;  // Not enough bits for this field
			color_fprintf(stdout, color, " %s[%d]=0x%lX",
				      NEXUS_MSG_DEF[fld_def].name, fld_size, fld_val & ((((u64)1) << fld_size) - 1));
			fld_def++;
			fld_val >>= fld_size;
			fld_bits -= fld_size;
		}

		if (mseo == 0x0)
			continue;

		if (NEXUS_MSG_DEF[fld_def].def & 0x400)
		{
			// Process ResourceFull cfg HREPEAT field
			if (tcode == NEXUS_TCODE_ResourceFull) {
				if (!strcmp(NEXUS_MSG_DEF[fld_def].name, "RCODE") && fld_val == 0x2)
					resourcefull_hrepeat = 1;

				if (!strcmp(NEXUS_MSG_DEF[fld_def].name, "HREPEAT") && resourcefull_hrepeat != 1) {
					// no HREPEAT field
					fld_def++;
					resourcefull_hrepeat = 0;
				}
			}

			// Process ProgTraceCorrelation cfg HIST field
			if (tcode == NEXUS_TCODE_ProgTraceCorrelation) {
				if (!strcmp(NEXUS_MSG_DEF[fld_def].name, "CDF") && fld_val == 0x2)
					correlation_hist = 1;

				if (!strcmp(NEXUS_MSG_DEF[fld_def].name, "HIST") && correlation_hist != 1) {
					// no HIST field
					fld_def++;
					correlation_hist = 0;
				}
			}

			// Variable size field
			color_fprintf(stdout, color, " %s[%d]=0x%lX",
				      NEXUS_MSG_DEF[fld_def].name, fld_bits, fld_val);

			if (mseo == 3) {
				printf("\n");
				fld_def = -1;
			} else {
				fld_def++;
			}
			fld_bits = 0;
			fld_val  = 0;
			continue;
		}

		if (fld_bits > 0) {
			color_fprintf(stdout, color,
				      " ERROR: At offset %d: Not enough bits for non-variable field\n",
				      msg_bytes + idle_cnt);
			return -EINVAL;
		}
	}

	color_fprintf(stdout, color,
		      "\nStat: %d bytes, %d idles, %d messages, %d error messages",
		      msg_bytes, idle_cnt, msg_cnt, msg_errors);
	if (msg_cnt > 0)
		color_fprintf(stdout, color, "%.2lf bytes/message", ((double)msg_bytes) / msg_cnt);

	printf("\n");

	return 0;
}

struct nexus_rv_pkt_decoder *nexus_rv_pkt_decoder_new(struct nexus_rv_pkt_decoder_params *params)
{
	struct nexus_rv_pkt_decoder *decoder;
	char *dir;
	char filename[PATH_MAX];
	FILE *f;

	if (!params)
		return NULL;

	decoder = zalloc(sizeof(struct nexus_rv_pkt_decoder));
	if (!decoder)
		return NULL;

	decoder->formatted = params->formatted;
	decoder->src_bits = params->src_bits;

	dir = getenv("PERF_BUILDID_DIR");
	snprintf(filename, sizeof(filename), "%s/trace.bin", dir);
	f = fopen(filename, "w+");
	decoder->nexus = f;

	return decoder;
}

void nexus_rv_pkt_decoder_free(struct nexus_rv_pkt_decoder *decoder)
{
	for (int i = 0; i < MAX_ID; ++i)
		free(decoder->defmt_bufs[i].buf);

	fclose(decoder->nexus);
	free(decoder);
}

int nexus_rv_pkt_desc(struct nexus_rv_pkt_decoder *decoder, const unsigned char *buf, size_t len)
{
	int err;

	if (decoder->formatted) {
		err = nexus_rv_pkt_defmt(decoder->defmt_bufs, decoder->nexus, buf, len);
		if (err) {
			pr_err("Encoder: failed to remove coresight trace formatter\n");
			return err;
		}
	}

	fseek(decoder->nexus, 0, SEEK_SET);
	err = nexus_rv_pkt_dump(decoder);

	return err;
}

static void nexus_rv_init_stack(struct nexus_rv_stack *stack, int capacity)
{
	if (stack->data == NULL)
		stack->data = (u64 *)malloc(sizeof(u64) * capacity);
	stack->top = -1;
	stack->capacity = capacity;
}

static void nexus_rv_stack_push(struct nexus_rv_stack *stack, u64 value)
{
	if (stack->top == stack->capacity - 1) {
		stack->capacity *= 2;
		stack->data = (u64 *)realloc(stack->data, sizeof(u64) * stack->capacity);
	}

	stack->data[++stack->top] = value;
}

static int nexus_rv_stack_pop(struct nexus_rv_stack *stack)
{
	u64 value;

	if (stack->top == -1)
		return 1;   // Empty

	value = stack->data[stack->top--];
	return value;
}

static void nexus_rv_free_stack(struct nexus_rv_stack *stack)
{
	free(stack->data);
}

static int emit_error_msg(const char *err) {
	printf("\nERROR: %s\n", err);
	return -EINVAL;
}

static int nexus_rv_insn_info_get(struct nexus_rv_insn_decoder *decoder, u8 *info, u64 *dest)
{
	u32 insn;
	u64 addr = decoder->nexdeco_pc;
	if (!decoder->mem_access(decoder->data, addr, decoder->prv, sizeof(insn), (u8 *)&insn))
		return -EINVAL;

	*info = INFO_LINEAR;
	if (riscv_insn_is_c(insn)) {
		if (riscv_insn_is_c_beqz(insn) || riscv_insn_is_c_bnez(insn)) {
			*info = INFO_BRANCH;
			*dest = addr + riscv_insn_extract_cbtype_imm(insn);
		} else if (riscv_insn_is_c_j(insn)) {
			// c.j offset
			*info = INFO_JUMP;
			*dest = addr + riscv_insn_extract_cjtype_imm(insn);
		} else if (riscv_insn_is_c_jr(insn)) {
			*info = INFO_JUMP | INFO_INDIRECT;
			// ret => c.jr x1
			if (riscv_insn_extract_rs1_reg(insn) == RV_REGNO_RA)
				*info |= INFO_RET;
		} else if (riscv_insn_is_c_jalr(insn)) {
			*info = INFO_JUMP | INFO_CALL | INFO_INDIRECT;
		} else {
			*info = INFO_LINEAR;
		}
	} else {
		if (riscv_insn_is_branch(insn)) {
			*info = INFO_BRANCH;
			*dest = addr + riscv_insn_extract_btype_imm(insn);
		} else if (riscv_insn_is_jalr(insn)) {
			*info = INFO_JUMP | INFO_INDIRECT;
			if (riscv_insn_extract_rd_reg(insn) == RV_REGNO_ZERO && riscv_insn_extract_jtype_imm(insn) == 0) {
				// ret => jalr x0,x1,0
				if (riscv_insn_extract_rs1_reg(insn) == RV_REGNO_RA)
					*info |= INFO_RET;
				else
				// jr rs1 => jalr x0,rs1,0
					*info |= INFO_CALL;
			}
		} else if (riscv_insn_is_jal(insn)) {
			*info = INFO_JUMP;
			// j offset => jal x0,offset
			if (riscv_insn_extract_rd_reg(insn) != RV_REGNO_ZERO)
				*info |= INFO_CALL;
			*dest = addr + riscv_insn_extract_jtype_imm(insn);
		} else if (riscv_insn_is_sret(insn) || riscv_insn_is_mret(insn)) {
			*info = INFO_JUMP | INFO_INDIRECT | INFO_RET;;
		} else if (riscv_insn_is_ecall(insn)) {
			*info = INFO_JUMP | INFO_INDIRECT | INFO_CALL;
		} else {
			*info = INFO_LINEAR;
		}
		*info |= INFO_4;
	}

	return 0;
}

static int nexus_rv_emit_icnt(struct nexus_rv_insn_decoder *decoder, int n, u32 hist)
{
	u64 a;
	u32 hist_mask;
	u8 info;

	if (decoder->nexdeco_pc & 1) return 0;  // Not synchronized ...

	// Adjust ICNT by what was handled by ResourceFull message[s] before this message (message with normal ICNT field)
	//  NOTE: decoder->resourcefull_icnt maybe positive or negative!
	if (n >= 0 && decoder->resourcefull_icnt != 0) {
		n += decoder->resourcefull_icnt;
		if (n < 0)
			return emit_error_msg("ICNT adjustment ERROR");

		decoder->resourcefull_icnt = 0;    // Make adjustment 'consumed'
	}

	hist_mask = 0;  // MSB is first in history, so we need sliding mask
	if (hist != 0) {
		if (hist & (1 << (sizeof(u32) * 8 -1))) {
			hist_mask = (1 << (sizeof(u32) * 8 - 2));
		} else {
			hist_mask = 0x1;
			while (hist_mask <= hist)
				hist_mask <<= 1;
			hist_mask >>= 2;
		}
	}

	while (n != 0) {
		printf("0x%lX\n", decoder->nexdeco_pc);
		if (nexus_rv_insn_info_get(decoder, &info, &a)) {
			decoder->nexdeco_pc = 1; // 1 means, that last address is unknown
			return emit_error_msg("failed to get insn info");
		}

		if (n > 0) {
			if (info & INFO_4) n -= 2; else n -= 1;
			if (n < 0) return emit_error_msg("ICNT too small");
		}

		if (info & INFO_CALL) {
			u64 ret = decoder->nexdeco_pc + ((info & INFO_4) ? 4 : 2);
			nexus_rv_stack_push(&decoder->stack, ret);
		}

		if (info & INFO_INDIRECT) { // Cannot continue over indirect...
			if (info & INFO_RET) {
				u64 ret = nexus_rv_stack_pop(&decoder->stack);
				decoder->nexdeco_pc = ret;
				if (n != 0) {
					if (ret == 1)
						return emit_error_msg("Not enough entires on callstack");
					continue;
				}
			}

			if (n > 0)
				return emit_error_msg("indirect address encountered in ICNT");

			break;
		}

		if (info & INFO_BRANCH) {
			if (hist == 0) {
				// This is calling as DirectBranch
				if (n == 0)
					info |= INFO_JUMP;  // Force PC change below
			} else {
				if (hist_mask & hist)
					info |= INFO_JUMP;  // Force PC change below
				hist_mask >>= 1;
				if (hist_mask == 0 && n < 0)
					n = 0;
			}
		}

		if (info & INFO_JUMP)
			decoder->nexdeco_pc = a;   // Direct jump/call/branch
		else if (info & INFO_4)
			decoder->nexdeco_pc += 4;
		else
			decoder->nexdeco_pc += 2;

	}

	return 0;
}

static u64 nexus_rv_field_get(struct nexus_rv_insn_decoder *decoder, const char *name)
{
	for (int d = decoder->msg_field_pos; NEXUS_MSG_DEF[d].def != 1; d++) {
		if (strcmp(NEXUS_MSG_DEF[d].name, name) == 0) {
			int fi = d - decoder->msg_field_pos;
			if (fi <= decoder->msg_field_cnt)
				return decoder->msg_fields[fi];
			return 0;
		}
	}
	return 0;
}

#define NEX_FLDGET(n) nexus_rv_field_get(decoder, #n)

static u64 nexus_rv_calculate_addr(u64 fu_addr, int full, u64 prev_addr)
{
	fu_addr <<= 1; // LSB bit is never sent
	if ((fu_addr >> 32) & 0x10000)	    // Perform MSB extension
		fu_addr |= 0xFFFF000000000000UL;
	// Update (NEW or XOR)
	if (!full) {
		if (prev_addr & 1) // Not Sync
			fu_addr = prev_addr;
		else
			fu_addr ^= prev_addr;
	}
	return fu_addr;
}

static int nexus_rv_msg_handle(struct nexus_rv_insn_decoder *decoder)
{
	int ret;
	u64 addr;
	int n = 0;
	const char *color = PERF_COLOR_BLUE;
	int TCODE = decoder->msg_fields[0];

	switch (TCODE) {
	case NEXUS_TCODE_Ownership:
		color_fprintf(stdout, color, "********MSG - Ownership TCODE=%d SRC=%ld FORMAT=%ld PRV=%ld V=%ld CONTEXT=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(FORMAT), NEX_FLDGET(PRV), NEX_FLDGET(V), NEX_FLDGET(CONTEXT));
		decoder->prv = NEX_FLDGET(PRV);
		decoder->v = NEX_FLDGET(V);
		if (NEX_FLDGET(FORMAT))
			decoder->context = NEX_FLDGET(CONTEXT);
		break;

	case NEXUS_TCODE_DirectBranch:
		color_fprintf(stdout, color, "********MSG - DirectBranch TCODE=%d SRC=%ld ICNT=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(ICNT));
		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);
		if (ret < 0)
			return ret;
		break;

	case NEXUS_TCODE_IndirectBranch:
		color_fprintf(stdout, color, "********MSG - IndirectBranch TCODE=%d SRC=%ld BTYPE=%ld ICNT=%ld UADDR=0x%lX\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(BTYPE), NEX_FLDGET(ICNT), NEX_FLDGET(UADDR));

		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);
		if (ret < 0)
			return ret;

		addr = NEX_FLDGET(UADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 0, decoder->nexdeco_lastaddr);
		color_fprintf(stdout, color, ".nexdeco_lastaddr=0x%lX\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;
		break;

	case NEXUS_TCODE_ProgTraceSync:
		color_fprintf(stdout, color, "********MSG - ProgTraceSync TCODE=%d SRC=%ld SYNC=%ld ICNT=%ld FADDR=0x%lX\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(SYNC), NEX_FLDGET(ICNT), NEX_FLDGET(FADDR));

		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);
		if (ret < 0)
			return ret;

		addr = NEX_FLDGET(FADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 1, decoder->nexdeco_lastaddr);
		color_fprintf(stdout, color, ".nexdeco_lastaddr=0x%lX\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;
		break;

	case NEXUS_TCODE_DirectBranchSync:
		color_fprintf(stdout, color, "********MSG - DirectBranchSync TCODE=%d SRC=%ld SYNC=%ld ICNT=%ld FADDR=0x%lX\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(SYNC), NEX_FLDGET(ICNT), NEX_FLDGET(FADDR));

		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);
		if (ret < 0)
			return ret;

		addr = NEX_FLDGET(FADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 1, decoder->nexdeco_lastaddr);
		color_fprintf(stdout, color, ".nexdeco_lastaddr=0x%lX\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;

		break;

	case NEXUS_TCODE_IndirectBranchSync:
		color_fprintf(stdout, color, "********MSG - IndirectBranchSync TCODE=%d SRC=%ld SYNC=%ld BTYPE=%ld ICNT=%ld FADDR=0x%lX\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(SYNC), NEX_FLDGET(BTYPE), NEX_FLDGET(ICNT), NEX_FLDGET(FADDR));

		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);
		if (ret < 0)
			return ret;

		addr = NEX_FLDGET(FADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 1, decoder->nexdeco_lastaddr);
		color_fprintf(stdout, color, ".nexdeco_lastaddr=0x%lX\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;

		break;

	case NEXUS_TCODE_IndirectBranchHist:
		color_fprintf(stdout, color, "********MSG - IndirectBranchHist TCODE=%d SRC=%ld BTYPE=%ld ICNT=%ld UADDR=0x%lX HIST=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(BTYPE), NEX_FLDGET(ICNT), NEX_FLDGET(UADDR), NEX_FLDGET(HIST));

		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, NEX_FLDGET(HIST));
		if (ret < 0)
			return ret;

		addr = NEX_FLDGET(UADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 0, decoder->nexdeco_lastaddr);
		color_fprintf(stdout, color, ".nexdeco_lastaddr=0x%lX\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;

		break;

	case NEXUS_TCODE_IndirectBranchHistSync:
		color_fprintf(stdout, color, "********MSG - IndirectBranchHistSync TCODE=%d SRC=%ld SYNC=%ld BTYPE=%ld CANCEL=%ld ICNT=%ld FADDR=0x%lX HIST=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(SYNC), NEX_FLDGET(BTYPE), NEX_FLDGET(CANCEL), NEX_FLDGET(ICNT), NEX_FLDGET(FADDR), NEX_FLDGET(HIST));

		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, NEX_FLDGET(HIST));
		if (ret < 0)
			return ret;

		addr = NEX_FLDGET(FADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 1, decoder->nexdeco_lastaddr);
		color_fprintf(stdout, color, ".nexdeco_lastaddr=0x%lX\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;

		break;

	case NEXUS_TCODE_ResourceFull:
		// Determine repeat count (for RCODE=2)
		int hrepeat = 0;
		int rcode = NEX_FLDGET(RCODE);
		if (rcode == 2)
			hrepeat = NEX_FLDGET(HREPEAT);

		color_fprintf(stdout, color, "********MSG - ResourceFull TCODE=%d SRC=%ld RCODE=%ld RDATA=%ld HREPEAT=%d\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(RCODE), NEX_FLDGET(RDATA), hrepeat);

		if (rcode == 1 || rcode == 2) {
			int rdata = NEX_FLDGET(RDATA);
			if (rdata > 1) {
				// Special calling to emit HIST only ...
				if (decoder->disp_hist_repeat) {
				    color_fprintf(stdout, color, "RepeatHIST,0x%X,%d\n", rdata, decoder->disp_hist_repeat);
					decoder->disp_hist_repeat = 0;
				}
				do {
					// ICNT is unknown (-1), what will process only HIST bits
					ret = nexus_rv_emit_icnt(decoder, -1, rdata);
					if (ret < 0)
						return ret;

					decoder->resourcefull_icnt -= ret; // Consume, so next time ICNT will be adjusted
					hrepeat--;
				} while (hrepeat > 0);
			}
		} else if (rcode == 0) {
			decoder->resourcefull_icnt += NEX_FLDGET(RDATA);
		}

		break;

	case NEXUS_TCODE_ProgTraceCorrelation:
		int hist = 0;
		int cdf = NEX_FLDGET(CDF);
		if (cdf == 1)
			hist = NEX_FLDGET(HIST);

		color_fprintf(stdout, color, "********MSG - ProgTraceCorrelation TCODE=%d SRC=%ld EVCODE=%ld CDF=%ld ICNT=%ld HIST=%d\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(EVCODE), NEX_FLDGET(CDF), NEX_FLDGET(ICNT), hist);

		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, hist);
		if (ret < 0)
			return ret;

		break;

	case NEXUS_TCODE_Error:
		color_fprintf(stdout, color, "********MSG - Error TCODE=%d SRC=%ld ETYPE=%ld PAD=%ld\n",
			      TCODE, NEX_FLDGET(SRC), NEX_FLDGET(ETYPE), NEX_FLDGET(PAD));
		break;

	case NEXUS_TCODE_RepeatBranch:  // Handled differently!
	default:
		return -EINVAL;
	}

	return 0;
}

static int nexus_rv_insn_dump(struct nexus_rv_insn_decoder *decoder)
{
	const char *color = PERF_COLOR_BLUE;
	int fld_def = -1;
	int fld_bits = 0;
	u64 fld_val = 0;

	int msg_cnt = 0;
	int msg_bytes = 0;
	int msg_errors = 0;

	unsigned char msg_byte = 0;
	unsigned char prev_byte = 0;

	unsigned int mdo = 0;
	unsigned int mseo = 0;

	decoder->msg_field_cnt = 0;  // No fields

	nexus_rv_init_stack(&decoder->stack, STACK_SIZE);

	for (;;) {
		prev_byte = msg_byte;
		if (fread(&msg_byte, 1, 1, decoder->nexus) != 1)
			break;	// EOF

		// This will skip long sequnece of idles (visible in true captures ...)
		if (msg_byte == 0xFF && prev_byte == 0xFF)
			continue;

		mdo = msg_byte >> 2;
		mseo = msg_byte & 0x3;

		if (mseo == 0x2) {
			color_fprintf(stdout, color,
				      "ERROR: MSEO='10' is not allowed\n");
			return -EINVAL;
		}

		if (fld_def < 0) {
			if (mseo == 0x3)
				continue;   // skip idle

			if (mseo != 0x0) {
				color_fprintf(stdout, color,
					      "ERROR: Message must start from MSEO='00'\n");
				return -EINVAL;
			}

			for (int d = 0; NEXUS_MSG_DEF[d].def != 0; d++) {
				if ((NEXUS_MSG_DEF[d].def & 0x100) == 0)
					continue;
				if ((NEXUS_MSG_DEF[d].def & 0xFF) == mdo) {
					fld_def = d; // Found TCODE
					break;
				}
			}

			if (fld_def < 0) {
				color_fprintf(stdout, color,
					      "ERROR: Message with TCODE=%d is not defined for RISC-V\n",
					      mdo);
				return -EINVAL;
			}

			// Special handling for RepeatBranch message.
			// We want to preserve previous packet, so we can
			// repeat it at end of RepeatBranch handling.
			if (mdo == NEXUS_TCODE_RepeatBranch) {
				// Save previous message fields
				decoder->saved_fields[0] = decoder->msg_field_pos;
				decoder->saved_fields[1] = decoder->msg_field_cnt;
				decoder->saved_fields[2] = decoder->msg_fields[0];
				decoder->saved_fields[3] = decoder->msg_fields[1];
				decoder->saved_fields[4] = decoder->msg_fields[2];
				decoder->saved_fields[5] = decoder->msg_fields[3];
				decoder->saved_fields[6] = decoder->msg_fields[4];
			}

			// Save to allow later decoding
			decoder->msg_field_pos = fld_def;
			decoder->msg_field_cnt = 0;
			decoder->msg_fields[decoder->msg_field_cnt++] = mdo;

			msg_cnt++;
			msg_bytes++;

			if (mdo == NEXUS_TCODE_Error)
				msg_errors++;

			fld_def++;
			fld_bits = 0;
			fld_val = 0;
			continue;
		}

		// Accumulate 'mdo' to field value
		fld_val |= (((u64)mdo) << fld_bits);
		fld_bits += 6;

		msg_bytes++;

		// Process fixed size fields (there may be more than one in one MDO record)
		while (NEXUS_MSG_DEF[fld_def].def & 0x200) {
			int fld_size = NEXUS_MSG_DEF[fld_def].def & 0xFF;
			if (fld_size & 0x80)
				fld_size = decoder->src_bits;
			if (fld_bits < fld_size)
				break;	// Not enough bits for this field

			decoder->msg_fields[decoder->msg_field_cnt++] = fld_val & ((((u64)1) << fld_size) - 1); // Save field

			fld_def++;
			fld_val >>= fld_size;
			fld_bits -= fld_size;
		}

		if (mseo == 0x0)
			continue;

		if (NEXUS_MSG_DEF[fld_def].def & 0x400) {
			// Variable size field
			decoder->msg_fields[decoder->msg_field_cnt++] = fld_val; // Save field

			if (mseo == 3) {
				int cnt = 1;

				decoder->disp_hist_repeat = 0;

				if (decoder->msg_fields[0] == NEXUS_TCODE_RepeatBranch) {
					// Special handling for repeat branch (which only has 1 field!)
					cnt = decoder->msg_fields[1]; // Counter set in RepeatBranch message
					decoder->msg_field_pos = decoder->saved_fields[0];
					decoder->msg_field_cnt = decoder->saved_fields[1];
					decoder->msg_fields[0] = decoder->saved_fields[2];
					decoder->msg_fields[1] = decoder->saved_fields[3];
					decoder->msg_fields[2] = decoder->saved_fields[4];
					decoder->msg_fields[3] = decoder->saved_fields[5];
					decoder->msg_fields[4] = decoder->saved_fields[6];
					decoder->disp_hist_repeat = cnt;
				}

				while (cnt > 0) { // Handle (1 or many times ...)
					int err = nexus_rv_msg_handle(decoder);
					if (err < 0)
						nexus_rv_init_stack(&decoder->stack, STACK_SIZE);
						//return err;
					cnt--;
				}

				fld_def = -1;
			} else {
				fld_def++;
			}
			fld_bits = 0;
			fld_val = 0;
			continue;
		}

		if (fld_bits > 0) {
			pr_err("Decode: Not enough bits for non-variable field\n");
			return -EINVAL;
		}
	}

	return 0;
}

struct nexus_rv_insn_decoder *nexus_rv_insn_decoder_new(struct nexus_rv_insn_decoder_params *params)
{
	struct nexus_rv_insn_decoder *decoder;
	char *dir;
	char filename[PATH_MAX];
	FILE *f;

	if (!params)
		return NULL;

	decoder = zalloc(sizeof(struct nexus_rv_insn_decoder));
	if (!decoder)
		return NULL;

	decoder->get_trace = params->get_trace;
	decoder->mem_access = params->mem_access;
	decoder->data = params->data;
	decoder->formatted = params->formatted;
	decoder->src_bits = params->src_bits;

	dir = getenv("PERF_BUILDID_DIR");
	snprintf(filename, sizeof(filename), "%s/trace.bin", dir);
	f = fopen(filename, "w+");
	decoder->nexus = f;

	decoder->nexdeco_pc = 1;
	decoder->nexdeco_lastaddr = 1;

	return decoder;
}

void nexus_rv_insn_decoder_free(struct nexus_rv_insn_decoder *decoder)
{
	for (int i = 0; i < MAX_ID; ++i)
		free(decoder->defmt_bufs[i].buf);

	nexus_rv_free_stack(&decoder->stack);
	fclose(decoder->nexus);
	free(decoder);
}

int nexus_rv_insn_decode(struct nexus_rv_insn_decoder *decoder)
{
	int err;
	struct nexus_rv_buffer buffer = { .buf = 0, };

	err = decoder->get_trace(&buffer, decoder->data);
	if (err)
		return err;

	if (decoder->formatted) {
		err = nexus_rv_pkt_defmt(decoder->defmt_bufs, decoder->nexus, buffer.buf, buffer.len);
		if (err) {
			pr_err("Encoder: failed to remove coresight trace formatter\n");
			return err;
		}
	}

	fseek(decoder->nexus, 0, SEEK_SET);
	return nexus_rv_insn_dump(decoder);
}
