/* SPDX-License-Identifier: GPL-2.0 */

#ifndef INCLUDE__NEXUS_RV_DECODER_H__
#define INCLUDE__NEXUS_RV_DECODER_H__

#define MAX_ID 112  // Values of 0x00 and 0x70-0x7F are reserved by the ATB specification

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

struct nexus_rv_pkt_decoder *nexus_rv_pkt_decoder_new(struct nexus_rv_pkt_decoder_params *params);

void nexus_rv_pkt_decoder_free(struct nexus_rv_pkt_decoder *decoder);

int nexus_rv_pkt_desc(struct nexus_rv_pkt_decoder *decoder, const unsigned char *buf, size_t len);

#endif
