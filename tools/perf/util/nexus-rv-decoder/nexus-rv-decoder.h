/* SPDX-License-Identifier: GPL-2.0 */

#ifndef INCLUDE__NEXUS_RV_DECODER_H__
#define INCLUDE__NEXUS_RV_DECODER_H__

#define INFO_LINEAR   0x1   // Linear (plain instruction or not taken BRANCH)
#define INFO_4        0x2   // If not 4, it must be 2 on RISC-V
//                    0x4   // Reserved (for exception or so ...)
#define INFO_INDIRECT 0x8   // Possible for most types above
#define INFO_BRANCH   0x10  // Always direct on RISC-V (may have LINEAR too)
#define INFO_JUMP     0x20  // Direct or indirect
#define INFO_CALL     0x40  // Direct or indirect (and always a jump)
#define INFO_RET      0x80  // Return (always indirect and always a jump)

#define MAX_ID 112  // Values of 0x00 and 0x70-0x7F are reserved by the ATB specification
#define MSGFIELDS_MAX 10
#define INSN_SZ       16

/* check an ID is in the valid range */
#define IS_VALID_ID(id)	    \
	((id > 0) && (id < MAX_ID))

struct rvtrace_queue;

struct nexus_rv_defmt_buf {
	unsigned char *buf;
	size_t size;
	size_t capacity;
};

struct nexus_rv_pkt_decoder {
	bool formatted;
	struct nexus_rv_defmt_buf defmt_bufs[MAX_ID];
	u32 src_bits;
	FILE *nexus;
};

struct nexus_rv_pkt_decoder_params {
	bool formatted;
	u32 src_bits;
};

struct nexus_rv_stack {
	u64 *data;
	int top;
	int capacity;
};

struct nexus_rv_buffer {
	const unsigned char *buf;
	size_t len;
};

enum rvtrace_sample_type {
	RVTRACE_EMPTY,
	RVTRACE_RANGE,
	RVTRACE_ERROR,
};

enum riscv_privilege_mode {
       RISCV_PRIV_USER_MODE,
       RISCV_PRIV_SUPERVISOR_MODE,
       RISCV_PRIV_MACHINE_MODE = 3,
};

struct nexus_rv_packet {
	enum rvtrace_sample_type sample_type;
	u64 start_addr;
	u64 end_addr;
	u32 insn_cnt;
	int cpu;
	enum riscv_privilege_mode prv;
	bool v;
	int context;
};

struct nexus_rv_packet_buffer {
	struct nexus_rv_packet *packets;
	int size;
	int capacity;
};

struct nexus_rv_insn_decoder {
	int (*get_trace)(struct nexus_rv_buffer *buffer, void *data);
	u32 (*mem_access)(void *, u64, enum riscv_privilege_mode, size_t, u8 *);
	void *data;
	bool formatted;
	struct nexus_rv_defmt_buf defmt_bufs[MAX_ID];
	u32 src_bits;
	struct nexus_rv_stack stack;
	int msg_field_pos;
	u64 msg_fields[MSGFIELDS_MAX];
	u64 saved_fields[MSGFIELDS_MAX];
	int msg_field_cnt;
	u64 nexdeco_pc;
	u64 nexdeco_lastaddr;
	u64 current_pc;
	int disp_hist_repeat;
	enum riscv_privilege_mode prv;
	bool v;
	int context;
	int resourcefull_icnt;
	struct nexus_rv_packet_buffer packet_buffer;
};

struct nexus_rv_insn_decoder_params {
	int (*get_trace)(struct nexus_rv_buffer *buffer, void *data);
	u32 (*mem_access)(void *, u64, enum riscv_privilege_mode, size_t, u8 *);
	void *data;
	bool formatted;
	u32 src_bits;
};

struct nexus_rv_pkt_decoder *nexus_rv_pkt_decoder_new(struct nexus_rv_pkt_decoder_params *params);

void nexus_rv_pkt_decoder_free(struct nexus_rv_pkt_decoder *decoder);

int nexus_rv_pkt_desc(struct nexus_rv_pkt_decoder *decoder, const unsigned char *buf, size_t len);

struct nexus_rv_insn_decoder *nexus_rv_insn_decoder_new(struct nexus_rv_insn_decoder_params *params);

void nexus_rv_insn_decoder_free(struct nexus_rv_insn_decoder *decoder);

int nexus_rv_insn_decode(struct nexus_rv_insn_decoder *decoder);

#endif
