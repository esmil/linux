// SPDX-License-Identifier: GPL-2.0

#include <linux/err.h>
#include <linux/zalloc.h>
#include <errno.h>
#include <api/fs/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../debug.h"
#include "../color.h"
#include "nexus-rv-decoder.h"
#include "nexus-rv-msg.h"

#define DEFMT_BUF_SIZE 32

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
