// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2026 Spacemit Limited. All rights reserved.
 * Author: liangzhen <zhen.liang@spacemit.com>
 */

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

#define BUFFER_SIZE	1024

#define RV_REGNO_ZERO   0
#define RV_REGNO_RA	1

static struct nexus_rv_defmt_buf* nexus_rv_get_buf(struct nexus_rv_defmt_buf defmt_bufs[], unsigned char id)
{
	if (defmt_bufs[id].buf != NULL)
		return &defmt_bufs[id];

	defmt_bufs[id].buf = (unsigned char*)malloc(BUFFER_SIZE);
	if (!defmt_bufs[id].buf)
		return NULL;

	defmt_bufs[id].size = 0;
	defmt_bufs[id].capacity = BUFFER_SIZE;

	return &defmt_bufs[id];
}

static int nexus_rv_defmt_buf_append(struct nexus_rv_defmt_buf *defmt_buf, FILE *nexus,
				     unsigned char data)
{
	if (defmt_buf->size == defmt_buf->capacity) {
		defmt_buf->capacity *= 2;
		defmt_buf->buf = realloc(defmt_buf->buf, defmt_buf->capacity);
		if (!defmt_buf->buf)
			return -ENOMEM;
	}
	defmt_buf->buf[defmt_buf->size++] = data;

	if ((data & 0x3) == 0x3) {
		size_t n = fwrite(defmt_buf->buf, defmt_buf->size, 1, nexus);
		if (n != 1) {
		    pr_err("Encoder: failed to write nexus data\n");
		    return -EINVAL;
		}
		defmt_buf->size = 0;
	}

	return 0;
}

static size_t skip_frame_syncs(const unsigned char *buf, size_t len, size_t *fsync_count) {
	const uint32_t FSYNC_PATTERN = 0x7FFFFFFF;    // LE host pattern for Frame SYNC
	size_t skipped_bytes = 0;

	while (skipped_bytes + 4 <= len) {
		if (*((uint32_t *)(buf + skipped_bytes)) == FSYNC_PATTERN) {
			skipped_bytes += 4;
			(*fsync_count)++;
		} else {
			break;
		}
	}

	return skipped_bytes;
}

// remove coresight formatter frame
static int nexus_rv_pkt_defmt(struct nexus_rv_defmt_buf defmt_bufs[], FILE *nexus, const unsigned char *buf, size_t len)
{
	int err;
	uint8_t flag_bit;
	unsigned char data = 0;
	int cur_id = -1, old_id = -1, new_id = -1;

	/* 16 bytes is output by coresight trace formatter */
	while (len >= 16) {
		/* some linux drivers (e.g. for perf) will insert FSYNCS to pad or differentiate
		 * between blocks of aligned data, always in frame aligned complete 16 byte frames.
		 * we need to skip past these frames, resetting as we go.
		 */
		size_t fsync_count = 0;
		size_t fsync_bytes = skip_frame_syncs(buf, len, &fsync_count);
		if (fsync_bytes > 0) {
			if (fsync_count % 4 == 0) {
				cur_id = -1;
				old_id = -1;
				new_id = -1;
			} else {
				pr_err("Incorrect FSYNC reset pattern\n");
				return -EINVAL;
			}
			buf += fsync_bytes;
			len -= fsync_bytes;
			continue;
		}

		flag_bit = 0x1;
		for (int i = 0; i < 15; i += 2) {
			if (buf[i] & 0x1) {
				/* it's id */
				old_id = new_id;
				new_id = buf[i] >> 1; /* get new_id */
				cur_id = (flag_bit & buf[15]) ? old_id : new_id;
			} else {
				/* it's data */
				data = buf[i] | ((flag_bit & buf[15]) ? 0x1 : 0x0);
			}

			if (IS_VALID_ID(cur_id)) {
				struct nexus_rv_defmt_buf *defmt_buf = nexus_rv_get_buf(defmt_bufs, cur_id);
				if (!defmt_buf)
					return -ENOMEM;

				/* it's data */
				if ((buf[i] & 0x1) == 0) {
					err = nexus_rv_defmt_buf_append(defmt_buf, nexus, data);
					if (err)
						return err;
				}

				/* buf[i+1] is alway data when i != 14 */
				if (i != 14) {
					err = nexus_rv_defmt_buf_append(defmt_buf, nexus, buf[i + 1]);
					if (err)
						return err;
				}
			}

			if (cur_id != new_id)
				cur_id = new_id;

			flag_bit <<= 1;
		}
		buf += 16;
		len -= 16;
	}
	return 0;
}

// dump all nexus messages (from nexus file)
static int nexus_rv_pkt_dump(struct nexus_rv_pkt_decoder *decoder, FILE *nexus)
{
	const char *color = PERF_COLOR_BLUE;
	int fld_def  = -1;
	int fld_bits = 0;
	u64 fld_val = 0;

	int msg_cnt    = 0;
	int msg_bytes  = 0;
	int msg_errors = 0;
	int idle_cnt   = 0;

	bool find_next_package = false;
	int error_msgs = 0;

	unsigned int tcode = 0;
	unsigned int correlation_hist = 0;
	unsigned int resourcefull_hrepeat = 0;

	unsigned char msg_byte = 0;
	unsigned char prev_byte = 0;
	unsigned int mdo = 0;
	unsigned int mseo = 0;
	for (;;) {
		prev_byte = msg_byte;
		if (fread(&msg_byte, 1, 1, nexus) != 1) break;  // EOF

		if (find_next_package) {
			if ((prev_byte & 0x3) == 0x3) { //last byte
				error_msgs++;
				find_next_package = false;

				//reinit some val
				fld_def = -1;
				fld_bits = 0;
				fld_val = 0;
			} else {
				continue;
			}
		}

		// This will skip long sequnece of idles (visible in true captures ...)
		if (msg_byte == 0xFF && prev_byte == 0xFF)
			continue;

		mdo  = msg_byte >> 2;
		mseo = msg_byte & 0x3;

		if (mseo == 0x2) {
			pr_err(" ERROR: At offset %d: MSEO='10' is not allowed\n",
				msg_bytes + idle_cnt);
			find_next_package = true;
			continue;
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
				pr_err(" ERROR: At offset %d: Message must start from MSEO='00'\n",
					      msg_bytes + idle_cnt);
				find_next_package = true;
				continue;
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
				pr_err(" ERROR: At offset %d: Message with TCODE=%d is not defined for N-Trace\n",
					msg_bytes + idle_cnt, mdo);
				find_next_package = true;
				continue;
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
			color_fprintf(stdout, color, " %s[%d]=0x%lx",
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
			color_fprintf(stdout, color, " %s[%d]=0x%lx",
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
			pr_err(" ERROR: At offset %d: Not enough bits for non-variable field\n",
				      msg_bytes + idle_cnt);
			find_next_package = true;
			continue;
		}
	}

	color_fprintf(stdout, color,
		      "\nStat: %d bytes, %d idles, %d messages, %d error messages, %d invalid messages",
		      msg_bytes, idle_cnt, msg_cnt, msg_errors, error_msgs);
	if (msg_cnt > 0)
		color_fprintf(stdout, color, "%.2lf bytes/message", ((double)msg_bytes) / msg_cnt);

	printf("\n");

	return 0;
}

struct nexus_rv_pkt_decoder *nexus_rv_pkt_decoder_new(struct nexus_rv_pkt_decoder_params *params)
{
	struct nexus_rv_pkt_decoder *decoder;

	if (!params)
		return NULL;

	decoder = zalloc(sizeof(struct nexus_rv_pkt_decoder));
	if (!decoder)
		return NULL;

	decoder->formatted = params->formatted;
	decoder->src_bits = params->src_bits;

	return decoder;
}

void nexus_rv_pkt_decoder_free(struct nexus_rv_pkt_decoder *decoder)
{
	for (int i = 0; i < MAX_ID; ++i)
		free(decoder->defmt_bufs[i].buf);

	free(decoder);
}

int nexus_rv_pkt_desc(struct nexus_rv_pkt_decoder *decoder, const unsigned char *buf, size_t len)
{
	int err;
	char filename[PATH_MAX];
	FILE *nexus;
	char *dir = getenv("PERF_BUILDID_DIR");

	/* TODO: The filename here is always trace.bin, which results in
	 * only the last Nexus trace data being retained. A corresponding
	 * Nexus filename should be generated for each AUX data.
	 */
	snprintf(filename, sizeof(filename), "%s/trace.bin", dir);
	nexus = fopen(filename, "w+");

	if (decoder->formatted) {
		err = nexus_rv_pkt_defmt(decoder->defmt_bufs, nexus, buf, len);
		if (err) {
			pr_err("Encoder: failed to remove coresight trace formatter\n");
			return err;
		}
	} else {
		size_t n = fwrite(buf, len, 1, nexus);
		if (n != 1) {
			pr_err("Encoder: failed to write nexus data\n");
			fclose(nexus);
			return -EINVAL;
		}
	}

	fseek(nexus, 0, SEEK_SET);
	err = nexus_rv_pkt_dump(decoder, nexus);

	fclose(nexus);

	return err;
}

static int nexus_rv_init_packet_buffer(struct nexus_rv_packet_buffer *packet_buffer)
{
	if (!packet_buffer->packets) {
		packet_buffer->packets = calloc(BUFFER_SIZE, sizeof(struct nexus_rv_packet));
		if (!packet_buffer->packets)
			return -ENOMEM;
	}

	packet_buffer->size = 0;
	packet_buffer->capacity = BUFFER_SIZE;

	return 0;
}

static int nexus_rv_packet_buffer_append(struct nexus_rv_packet_buffer *packet_buffer,
					 struct nexus_rv_packet packet)
{
	if (packet_buffer->size == packet_buffer->capacity) {
		packet_buffer->capacity *= 2;
		packet_buffer->packets = realloc(packet_buffer->packets,
			sizeof(struct nexus_rv_packet) * packet_buffer->capacity);
		if (!packet_buffer->packets)
			return -ENOMEM;
	}
	packet_buffer->packets[packet_buffer->size++] = packet;

	return 0;
}

static void nexus_rv_free_packet_buffer(struct nexus_rv_packet_buffer *packet_buffer)
{
	free(packet_buffer->packets);
}

static int nexus_rv_init_stack(struct nexus_rv_stack *stack)
{
	if (!stack->data) {
		stack->data = (u64 *)malloc(sizeof(u64) * BUFFER_SIZE);
		if (!stack->data)
			return -ENOMEM;
	}

	stack->top = -1;
	stack->capacity = BUFFER_SIZE;

	return 0;
}

static int nexus_rv_stack_push(struct nexus_rv_stack *stack, u64 value)
{
	if (stack->top == stack->capacity - 1) {
		stack->capacity *= 2;
		stack->data = (u64 *)realloc(stack->data, sizeof(u64) * stack->capacity);
		if (!stack->data)
			return -ENOMEM;
	}
	stack->data[++stack->top] = value;

	return 0;
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

static int handle_error_msg(struct nexus_rv_insn_decoder *decoder, const char *err) {
	pr_err("\nERROR: %s\n", err);
	decoder->nexdeco_pc = 1; // 1 means, that last address is unknown
	nexus_rv_init_stack(&decoder->stack);
	return -EINVAL;
}

static int nexus_rv_insn_info_get(struct nexus_rv_insn_decoder *decoder, u8 *info, u64 *dest)
{
	u32 insn;
	u64 addr = decoder->nexdeco_pc;
	if (!decoder->mem_access(decoder->data, addr, decoder->prv, decoder->context, sizeof(insn), (u8 *)&insn))
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

	pr_debug2(".nexdeco_pc=0x%lx .insn=0x%x\n", addr, ((*info & INFO_4) ? insn : (u16)insn));
	return 0;
}

static int nexus_rv_emit_icnt(struct nexus_rv_insn_decoder *decoder, int n, u32 hist)
{
	u64 a = 0;
	u32 hist_mask;
	u8 info;
	int done_icnt = 0; // Number of done instruction count

	if (decoder->nexdeco_pc & 1) return 0;  // Not synchronized ...

	// Adjust ICNT by what was handled by ResourceFull message[s] before this message (message with normal ICNT field)
	//  NOTE: decoder->resourcefull_icnt maybe positive or negative!
	if (n >= 0 && decoder->resourcefull_icnt != 0) {
		n += decoder->resourcefull_icnt;
		if (n < 0)
			return handle_error_msg(decoder, "ICNT adjustment ERROR");

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
		decoder->current_pc = decoder->nexdeco_pc;
		if (nexus_rv_insn_info_get(decoder, &info, &a))
			return handle_error_msg(decoder, "failed to get insn info");

		if (n > 0) {
			n -= (info & INFO_4) ? 2 : 1;
			if (n < 0)
				return handle_error_msg(decoder, "ICNT too small");
			done_icnt += 1;
		}

		if (info & INFO_CALL) {
			u64 ret = decoder->nexdeco_pc + ((info & INFO_4) ? 4 : 2);
			if (nexus_rv_stack_push(&decoder->stack, ret))
				return handle_error_msg(decoder, "failed to push value to stack");
		}

		if (info & INFO_INDIRECT) { // Cannot continue over indirect...
			if (info & INFO_RET) {
				u64 ret = nexus_rv_stack_pop(&decoder->stack);
				decoder->nexdeco_pc = ret;
				if (n != 0) {
					if (ret == 1)
						return handle_error_msg(decoder, "Not enough entires on callstack");
					continue;
				}
			}

			if (n > 0)
				return handle_error_msg(decoder, "indirect address encountered in ICNT");

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

	return done_icnt;
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
	int ret = 0;
	u64 addr;
	struct nexus_rv_packet packet;
	int n = 0;
	int cpu = 0;
	u64 start_addr = decoder->nexdeco_pc;
	int TCODE = decoder->msg_fields[0];

	switch (TCODE) {
	case NEXUS_TCODE_Ownership:
		pr_debug2("********MSG - Ownership TCODE=%d SRC=%ld FORMAT=%ld PRV=%ld V=%ld CONTEXT=%ld TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(FORMAT), NEX_FLDGET(PRV), NEX_FLDGET(V), NEX_FLDGET(CONTEXT), NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->prv = NEX_FLDGET(PRV);
		decoder->v = NEX_FLDGET(V);
		decoder->timestamp += NEX_FLDGET(TSTAMP);
		if (NEX_FLDGET(FORMAT))
			decoder->context = NEX_FLDGET(CONTEXT);
		break;

	case NEXUS_TCODE_DirectBranch:
		pr_debug2("********MSG - DirectBranch TCODE=%d SRC=%ld ICNT=%ld TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(ICNT), NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->timestamp += NEX_FLDGET(TSTAMP);
		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);
		break;

	case NEXUS_TCODE_IndirectBranch:
		pr_debug2("********MSG - IndirectBranch TCODE=%d SRC=%ld BTYPE=%ld ICNT=%ld UADDR=0x%lx TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(BTYPE), NEX_FLDGET(ICNT), NEX_FLDGET(UADDR), NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->timestamp += NEX_FLDGET(TSTAMP);
		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);

		addr = NEX_FLDGET(UADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 0, decoder->nexdeco_lastaddr);
		pr_debug2(".nexdeco_lastaddr=0x%lx\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;
		break;

	case NEXUS_TCODE_ProgTraceSync:
		pr_debug2("********MSG - ProgTraceSync TCODE=%d SRC=%ld SYNC=%ld ICNT=%ld FADDR=0x%lx TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(SYNC), NEX_FLDGET(ICNT), NEX_FLDGET(FADDR), NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->timestamp = NEX_FLDGET(TSTAMP);
		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);

		addr = NEX_FLDGET(FADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 1, decoder->nexdeco_lastaddr);
		pr_debug2(".nexdeco_lastaddr=0x%lx\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;
		break;

	case NEXUS_TCODE_DirectBranchSync:
		pr_debug2("********MSG - DirectBranchSync TCODE=%d SRC=%ld SYNC=%ld ICNT=%ld FADDR=0x%lx TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(SYNC), NEX_FLDGET(ICNT), NEX_FLDGET(FADDR), NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->timestamp = NEX_FLDGET(TSTAMP);
		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);

		addr = NEX_FLDGET(FADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 1, decoder->nexdeco_lastaddr);
		pr_debug2(".nexdeco_lastaddr=0x%lx\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;

		break;

	case NEXUS_TCODE_IndirectBranchSync:
		pr_debug2("********MSG - IndirectBranchSync TCODE=%d SRC=%ld SYNC=%ld BTYPE=%ld ICNT=%ld FADDR=0x%lx TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(SYNC), NEX_FLDGET(BTYPE), NEX_FLDGET(ICNT), NEX_FLDGET(FADDR), NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->timestamp = NEX_FLDGET(TSTAMP);
		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, 0x0);

		addr = NEX_FLDGET(FADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 1, decoder->nexdeco_lastaddr);
		pr_debug2(".nexdeco_lastaddr=0x%lx\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;

		break;

	case NEXUS_TCODE_IndirectBranchHist:
		pr_debug2("********MSG - IndirectBranchHist TCODE=%d SRC=%ld BTYPE=%ld ICNT=%ld UADDR=0x%lx HIST=%ld TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(BTYPE), NEX_FLDGET(ICNT), NEX_FLDGET(UADDR), NEX_FLDGET(HIST), NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->timestamp += NEX_FLDGET(TSTAMP);
		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, NEX_FLDGET(HIST));

		addr = NEX_FLDGET(UADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 0, decoder->nexdeco_lastaddr);
		pr_debug2(".nexdeco_lastaddr=0x%lx\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;

		break;

	case NEXUS_TCODE_IndirectBranchHistSync:
		pr_debug2("********MSG - IndirectBranchHistSync TCODE=%d SRC=%ld SYNC=%ld BTYPE=%ld CANCEL=%ld ICNT=%ld FADDR=0x%lx HIST=%ld TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(SYNC), NEX_FLDGET(BTYPE), NEX_FLDGET(CANCEL), NEX_FLDGET(ICNT), NEX_FLDGET(FADDR), NEX_FLDGET(HIST), NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->timestamp = NEX_FLDGET(TSTAMP);
		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, NEX_FLDGET(HIST));

		addr = NEX_FLDGET(FADDR);
		decoder->nexdeco_lastaddr = nexus_rv_calculate_addr(addr, 1, decoder->nexdeco_lastaddr);
		pr_debug2(".nexdeco_lastaddr=0x%lx\n", decoder->nexdeco_lastaddr);
		decoder->nexdeco_pc = decoder->nexdeco_lastaddr;

		break;

	case NEXUS_TCODE_ResourceFull:
		// Determine repeat count (for RCODE=2)
		int hrepeat = 0;
		int rcode = NEX_FLDGET(RCODE);
		if (rcode == 2)
			hrepeat = NEX_FLDGET(HREPEAT);

		pr_debug2("********MSG - ResourceFull TCODE=%d SRC=%ld RCODE=%ld RDATA=%ld HREPEAT=%d TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(RCODE), NEX_FLDGET(RDATA), hrepeat, NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->timestamp += NEX_FLDGET(TSTAMP);
		if (rcode == 1 || rcode == 2) {
			int rdata = NEX_FLDGET(RDATA);
			if (rdata > 1) {
				// Special calling to emit HIST only ...
				if (decoder->disp_hist_repeat) {
				    pr_debug2("RepeatHIST,0x%x,%d\n", rdata, decoder->disp_hist_repeat);
					decoder->disp_hist_repeat = 0;
				}
				do {
					// ICNT is unknown (-1), what will process only HIST bits
					ret = nexus_rv_emit_icnt(decoder, -1, rdata);
					if (ret < 0)
						break;

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

		pr_debug2("********MSG - ProgTraceCorrelation TCODE=%d SRC=%ld EVCODE=%ld CDF=%ld ICNT=%ld HIST=%d TSTAMP=%ld\n",
				TCODE, NEX_FLDGET(SRC), NEX_FLDGET(EVCODE), NEX_FLDGET(CDF), NEX_FLDGET(ICNT), hist, NEX_FLDGET(TSTAMP));

		cpu = NEX_FLDGET(SRC);
		decoder->timestamp += NEX_FLDGET(TSTAMP);
		n = NEX_FLDGET(ICNT);
		ret = nexus_rv_emit_icnt(decoder, n, hist);

		break;

	case NEXUS_TCODE_Error:
		pr_debug2("********MSG - Error TCODE=%d SRC=%ld ETYPE=%ld PAD=%ld TSTAMP=%ld\n",
			      TCODE, NEX_FLDGET(SRC), NEX_FLDGET(ETYPE), NEX_FLDGET(PAD), NEX_FLDGET(TSTAMP));
		cpu = NEX_FLDGET(SRC);
		break;

	case NEXUS_TCODE_RepeatBranch:  // Handled differently!
	default:
		return -EINVAL;
	}

	if (ret >= 0) {
		packet.sample_type = (!ret) ? RVTRACE_EMPTY : RVTRACE_RANGE;
		packet.start_addr = start_addr;
		packet.end_addr = decoder->current_pc;
		packet.insn_cnt = ret;
		packet.cpu = cpu;
		packet.prv = decoder->prv;
		packet.v = decoder->v;
		packet.context = decoder->context;
		packet.timestamp = decoder->timestamp;
	} else {
		packet.sample_type = RVTRACE_ERROR;
	}

	ret = nexus_rv_packet_buffer_append(&decoder->packet_buffer, packet);

	return ret;
}

static int nexus_rv_insn_decode(struct nexus_rv_insn_decoder *decoder, FILE *nexus)
{
	int fld_def = -1;
	int fld_bits = 0;
	u64 fld_val = 0;

	int msg_cnt = 0;
	int msg_bytes = 0;
	int msg_errors = 0;

	bool find_next_package = false;

	unsigned char msg_byte = 0;
	unsigned char prev_byte = 0;

	unsigned int mdo = 0;
	unsigned int mseo = 0;

	decoder->msg_field_cnt = 0;  // No fields

	for (;;) {
		prev_byte = msg_byte;
		if (fread(&msg_byte, 1, 1, nexus) != 1)
			break;	// EOF

		if (find_next_package) {
			if ((prev_byte & 0x3) == 0x3) { //last byte
				find_next_package = false;

				//reinit some val
				fld_def = -1;
				fld_bits = 0;
				fld_val = 0;
			} else {
				continue;
			}
		}

		// This will skip long sequnece of idles (visible in true captures ...)
		if (msg_byte == 0xFF && prev_byte == 0xFF)
			continue;

		mdo = msg_byte >> 2;
		mseo = msg_byte & 0x3;

		if (mseo == 0x2) {
			pr_err("ERROR: MSEO='10' is not allowed\n");
			find_next_package = true;
			continue;
		}

		if (fld_def < 0) {
			if (mseo == 0x3)
				continue;   // skip idle

			if (mseo != 0x0) {
				pr_err("ERROR: Message must start from MSEO='00'\n");
				find_next_package = true;
				continue;
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
				pr_err("ERROR: Message with TCODE=%d is not defined for RISC-V\n",
					      mdo);
				find_next_package = true;
				continue;
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
						return err;
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
			find_next_package = true;
			continue;
		}
	}

	return 0;
}

struct nexus_rv_insn_decoder *nexus_rv_insn_decoder_new(struct nexus_rv_insn_decoder_params *params)
{
	int err;
	struct nexus_rv_insn_decoder *decoder;

	if (!params)
		return NULL;

	decoder = zalloc(sizeof(struct nexus_rv_insn_decoder));
	if (!decoder)
		return NULL;

	decoder->mem_access = params->mem_access;
	decoder->data = params->data;
	decoder->formatted = params->formatted;
	decoder->src_bits = params->src_bits;
	decoder->nexdeco_pc = 1;
	decoder->nexdeco_lastaddr = 1;
	decoder->context = -1;

	err = nexus_rv_init_stack(&decoder->stack);
	if (err)
		goto err_out;

	err = nexus_rv_init_packet_buffer(&decoder->packet_buffer);
	if (err)
		goto err_out;

	return decoder;

err_out:
	nexus_rv_free_stack(&decoder->stack);
	nexus_rv_free_packet_buffer(&decoder->packet_buffer);
	free(decoder);
	return NULL;
}

void nexus_rv_insn_decoder_free(struct nexus_rv_insn_decoder *decoder)
{
	for (int i = 0; i < MAX_ID; ++i)
		free(decoder->defmt_bufs[i].buf);

	nexus_rv_free_packet_buffer(&decoder->packet_buffer);
	nexus_rv_free_stack(&decoder->stack);
	free(decoder);
}

int nexus_rv_insn_decoder_reset(struct nexus_rv_insn_decoder *decoder)
{
	int err;
	decoder->nexdeco_pc = 1;
	decoder->nexdeco_lastaddr = 1;
	decoder->context = -1;

	err = nexus_rv_init_stack(&decoder->stack);
	if (err)
		return err;

	err = nexus_rv_init_packet_buffer(&decoder->packet_buffer);
	if (err)
		return err;

	return 0;
}

int nexus_rv_insn_decode_data_block(struct nexus_rv_insn_decoder *decoder,
				    const unsigned char *buf, size_t size)
{
	int err;
	char filename[PATH_MAX];
	FILE *nexus;
	char *dir = getenv("PERF_BUILDID_DIR");

	/* TODO: The filename here is always trace.bin, which results in
	 * only the last Nexus trace data being retained. A corresponding
	 * Nexus filename should be generated for each AUX data.
	 */
	snprintf(filename, sizeof(filename), "%s/trace.bin", dir);
	nexus = fopen(filename, "w+");

	if (decoder->formatted) {
		err = nexus_rv_pkt_defmt(decoder->defmt_bufs, nexus, buf, size);
		if (err) {
			pr_err("Encoder: failed to remove coresight trace formatter\n");
			fclose(nexus);
			return err;
		}
	} else {
		size_t n = fwrite(buf, size, 1, nexus);
		if (n != 1) {
			pr_err("Encoder: failed to write nexus data\n");
			fclose(nexus);
			return -EINVAL;
		}
	}

	fseek(nexus, 0, SEEK_SET);
	err = nexus_rv_insn_decode(decoder, nexus);

	fclose(nexus);

	return err;
}
