/******************************************************************************
 *
 * Copyright(c) 2007 - 2021 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#define _SDIO_OPS_LINUX_C_

#include <drv_types.h>
#include <rtw_io_records.h>

static bool rtw_sdio_claim_host_needed(struct sdio_func *func)
{
	struct dvobj_priv *dvobj = sdio_get_drvdata(func);
	struct sdio_data *sdio_data = dvobj_to_sdio(dvobj);

	if (sdio_data->sys_sdio_irq_thd && sdio_data->sys_sdio_irq_thd == current)
		return _FALSE;
	return _TRUE;
}

#ifdef CONFIG_RTW_IO_RECORDS

enum sdio_type {
	SDIO_CMDT_F0_52_READ,
	SDIO_CMDT_F0_52_WRITE,
	SDIO_CMDT_52_READ,
	SDIO_CMDT_52_WRITE,
	SDIO_CMDT_53_READ,
	SDIO_CMDT_53_WRITE,
	SDIO_CMDT_NUM,
};

static const char *sdio_type_str[] = {
	[SDIO_CMDT_F0_52_READ]	= "F052R",
	[SDIO_CMDT_F0_52_WRITE]	= "F052W",
	[SDIO_CMDT_52_READ]	= "52R",
	[SDIO_CMDT_52_WRITE]	= "52W",
	[SDIO_CMDT_53_READ]	= "53R",
	[SDIO_CMDT_53_WRITE]	= "53W",
};

#define SDIO_R_TITLE_FMT "%-9s %-17s %-11s %-5s %-7s %-5s %-4s"
#define SDIO_R_TITLE_TFMT "%s\t%s\t%s\t%s\t%s\t%s\t%s"
#define SDIO_R_TITLE_ARG , "seq", "stime", "api_time", "type", "addr", "cnt", "err"
#define SDIO_R_VALUE_FMT "%9zu %7lld.%09lld %lld.%09lld %-5s 0x%05x %5u %4d"
#define SDIO_R_VALUE_TFMT "%zu\t%lld.%09lld\t%lld.%09lld\t%s\t0x%05x\t%u\t%d"
#define SDIO_R_VALUE_ARG \
		, seq \
		, rtw_division64(rtw_sptime_to_ns(r->stime), 1000000000), rtw_modular64(rtw_sptime_to_ns(r->stime), 1000000000) \
		, rtw_division64(rtw_sptime_diff_ns(r->stime, r->etime), 1000000000), rtw_modular64(rtw_sptime_diff_ns(r->stime, r->etime), 1000000000) \
		, sdio_type_str[r->type] \
		, r->addr, r->cnt, r->err

#if DBG_RTW_IO_RECORD_DATA_LEN
#define SDIO_R_TITLE_FMT_DATA " %-2s %-2s %-2s %-2s"
#define SDIO_R_TITLE_TFMT_DATA "\t%s\t%s\t%s\t%s"
#define SDIO_R_TITLE_ARG_DATA , "d0", "d1", "d2", "d3"
#define SDIO_R_VALUE_FMT_DATA " %02x %02x %02x %02x"
#define SDIO_R_VALUE_TFMT_DATA "\t%02x\t%02x\t%02x\t%02x"
#define SDIO_R_VALUE_ARG_DATA  , r->cnt > 0 ? r->data[0] : 0, r->cnt > 1 ? r->data[1] : 0, r->cnt > 2 ? r->data[2] : 0, r->cnt > 3 ? r->data[3] : 0
#else
#define SDIO_R_TITLE_FMT_DATA ""
#define SDIO_R_TITLE_TFMT_DATA ""
#define SDIO_R_TITLE_ARG_DATA
#define SDIO_R_VALUE_FMT_DATA ""
#define SDIO_R_VALUE_TFMT_DATA ""
#define SDIO_R_VALUE_ARG_DATA
#endif

#if DBG_RTW_IO_RECORD_TASK_INFO
#define SDIO_R_TITLE_FMT_TASK_INFO " %-15s"
#define SDIO_R_TITLE_TFMT_TASK_INFO "\t%s"
#define SDIO_R_TITLE_ARG_TASK_INFO , "task_comm"
#define SDIO_R_VALUE_FMT_TASK_INFO " %-15s"
#define SDIO_R_VALUE_TFMT_TASK_INFO "\t%s"
#define SDIO_R_VALUE_ARG_TASK_INFO , r->task_comm
#else
#define SDIO_R_TITLE_FMT_TASK_INFO ""
#define SDIO_R_TITLE_TFMT_TASK_INFO ""
#define SDIO_R_TITLE_ARG_TASK_INFO
#define SDIO_R_VALUE_FMT_TASK_INFO ""
#define SDIO_R_VALUE_TFMT_TASK_INFO ""
#define SDIO_R_VALUE_ARG_TASK_INFO
#endif

#if DBG_RTW_IO_RECORD_CPU_INFO
#define SDIO_R_TITLE_FMT_CPU_INFO " %-3s"
#define SDIO_R_TITLE_TFMT_CPU_INFO "\t%s"
#define SDIO_R_TITLE_ARG_CPU_INFO , "cpu"
#define SDIO_R_VALUE_FMT_CPU_INFO " %-3d"
#define SDIO_R_VALUE_TFMT_CPU_INFO "\t%d"
#define SDIO_R_VALUE_ARG_CPU_INFO , r->cpu
#else
#define SDIO_R_TITLE_FMT_CPU_INFO ""
#define SDIO_R_TITLE_TFMT_CPU_INFO ""
#define SDIO_R_TITLE_ARG_CPU_INFO
#define SDIO_R_VALUE_FMT_CPU_INFO ""
#define SDIO_R_VALUE_TFMT_CPU_INFO ""
#define SDIO_R_VALUE_ARG_CPU_INFO
#endif

#define SDIO_R_TITLE_FMT_REL " %-12s %-15s %-15s"
#define SDIO_R_TITLE_TFMT_REL "\t%s\t%s\t%s"
#define SDIO_R_TITLE_ARG_REL , "from_last_io", "from_last_rx_io", "from_last_tx_io"
#define SDIO_R_VALUE_FMT_REL " %2lld.%09lld %5lld.%09lld %5lld.%09lld"
#define SDIO_R_VALUE_TFMT_REL "\t%lld.%09lld\t%lld.%09lld\t%lld.%09lld"
#define SDIO_R_VALUE_ARG_REL \
		, last_io ? rtw_division64(rtw_sptime_diff_ns(last_io->etime, r->stime), 1000000000) : 0 \
		, last_io ? rtw_modular64(rtw_sptime_diff_ns(last_io->etime, r->stime), 1000000000) : 0 \
		, last_rx_io ? rtw_division64(rtw_sptime_diff_ns(last_rx_io->etime, r->stime), 1000000000) : 0 \
		, last_rx_io ? rtw_modular64(rtw_sptime_diff_ns(last_rx_io->etime, r->stime), 1000000000) : 0 \
		, last_tx_io ? rtw_division64(rtw_sptime_diff_ns(last_tx_io->etime, r->stime), 1000000000) : 0 \
		, last_tx_io ? rtw_modular64(rtw_sptime_diff_ns(last_tx_io->etime, r->stime), 1000000000) : 0

static void sdio_record_title_dump_tab(void *sel)
{
	RTW_PRINT_SEL(sel, SDIO_R_TITLE_TFMT
		SDIO_R_TITLE_TFMT_DATA
		SDIO_R_TITLE_TFMT_TASK_INFO
		SDIO_R_TITLE_TFMT_CPU_INFO
		SDIO_R_TITLE_TFMT_REL
		"\n"
		SDIO_R_TITLE_ARG
		SDIO_R_TITLE_ARG_DATA
		SDIO_R_TITLE_ARG_TASK_INFO
		SDIO_R_TITLE_ARG_CPU_INFO
		SDIO_R_TITLE_ARG_REL
		);
}

static void sdio_record_value_dump_tab(void *sel, struct rtw_io_record *r, size_t seq
	, struct rtw_io_record *last_io
	, struct rtw_io_record *last_rx_io
	, struct rtw_io_record *last_tx_io)
{
	RTW_PRINT_SEL(sel, SDIO_R_VALUE_TFMT
		SDIO_R_VALUE_TFMT_DATA
		SDIO_R_VALUE_TFMT_TASK_INFO
		SDIO_R_VALUE_TFMT_CPU_INFO
		SDIO_R_VALUE_TFMT_REL
		"\n"
		SDIO_R_VALUE_ARG
		SDIO_R_VALUE_ARG_DATA
		SDIO_R_VALUE_ARG_TASK_INFO
		SDIO_R_VALUE_ARG_CPU_INFO
		SDIO_R_VALUE_ARG_REL
	);
}


static void sdio_record_title_dump(void *sel)
{
	RTW_PRINT_SEL(sel, SDIO_R_TITLE_FMT
		SDIO_R_TITLE_FMT_DATA
		SDIO_R_TITLE_FMT_TASK_INFO
		SDIO_R_TITLE_FMT_CPU_INFO
		SDIO_R_TITLE_FMT_REL
		"\n"
		SDIO_R_TITLE_ARG
		SDIO_R_TITLE_ARG_DATA
		SDIO_R_TITLE_ARG_TASK_INFO
		SDIO_R_TITLE_ARG_CPU_INFO
		SDIO_R_TITLE_ARG_REL
		);
}

static void sdio_record_value_dump(void *sel, struct rtw_io_record *r, size_t seq
	, struct rtw_io_record *last_io
	, struct rtw_io_record *last_rx_io
	, struct rtw_io_record *last_tx_io)
{
	RTW_PRINT_SEL(sel, SDIO_R_VALUE_FMT
		SDIO_R_VALUE_FMT_DATA
		SDIO_R_VALUE_FMT_TASK_INFO
		SDIO_R_VALUE_FMT_CPU_INFO
		SDIO_R_VALUE_FMT_REL
		"\n"
		SDIO_R_VALUE_ARG
		SDIO_R_VALUE_ARG_DATA
		SDIO_R_VALUE_ARG_TASK_INFO
		SDIO_R_VALUE_ARG_CPU_INFO
		SDIO_R_VALUE_ARG_REL
	);
}

typedef void (*sdio_rec_title_dump)(void *);
typedef void (*sdio_rec_value_dump)(void *, struct rtw_io_record *, size_t, struct rtw_io_record *, struct rtw_io_record *, struct rtw_io_record *);

static bool rtw_sdio_record_is_himr(struct rtw_io_record *r)
{
	/* TODO: judge by HAL */
	return r->addr == 0x01100;
}

static bool rtw_sdio_record_is_hisr(struct rtw_io_record *r)
{
	/* TODO: judge by HAL */
	return r->addr == 0x01104;
}

static bool rtw_sdio_record_is_rx(struct rtw_io_record *r)
{
	/* TODO: judge by HAL */
	return r->addr == 0x01f00;
}

static bool rtw_sdio_record_is_rx_aval(struct rtw_io_record *r)
{
	/* TODO: judge by HAL */
	return r->addr == 0x01108;
}

static bool rtw_sdio_record_is_tx(struct rtw_io_record *r)
{
	/* TODO: judge by HAL */
	return (r->addr & BIT16) && r->cnt > 4;
}

static bool rtw_sdio_record_is_tx_aval(struct rtw_io_record *r)
{
	/* TODO: judge by HAL */
	return r->addr == 0x01110;
}

void rtw_sdio_records_dump_title(void *sel, bool tab)
{
	sdio_rec_title_dump title_dump = tab ? sdio_record_title_dump_tab : sdio_record_title_dump;

	title_dump(sel);
}

void rtw_sdio_records_dump_value_by_seq(void *sel, bool tab, size_t seq)
{
	size_t oldest_pos = dbg_rtw_io_records.record[dbg_rtw_io_records.pos].valid ? dbg_rtw_io_records.pos : 0;
	struct rtw_io_record *record = &dbg_rtw_io_records.record[(oldest_pos + seq) % dbg_rtw_io_records.record_num];

	if (record->valid) {
		sdio_rec_value_dump value_dump = tab ? sdio_record_value_dump_tab : sdio_record_value_dump;
		bool rx_io = rtw_sdio_record_is_rx(record);
		bool tx_io = rtw_sdio_record_is_tx(record);
		struct rtw_io_record *last_io = NULL;
		struct rtw_io_record *last_rx_io = NULL;
		struct rtw_io_record *last_tx_io = NULL;

		if (seq != 0) {
			last_io = &dbg_rtw_io_records.record[(oldest_pos + seq - 1) % dbg_rtw_io_records.record_num];

			if (rx_io || tx_io) {
				struct rtw_io_record *r;
				size_t i;

				/* find last_rx_io or last_tx_io */
				for (i = 1; i <= seq ; i++) {
					r = &dbg_rtw_io_records.record[(oldest_pos + seq - i) % dbg_rtw_io_records.record_num];
					if (rx_io && rtw_sdio_record_is_rx(r)) {
						last_rx_io = r;
						break;
					}
					if (tx_io && rtw_sdio_record_is_tx(r)) {
						last_tx_io = r;
						break;
					}
				}
			}
		}
		value_dump(sel, record, seq, last_io, rx_io ? last_rx_io : NULL, tx_io ? last_tx_io : NULL);
	}
}

void rtw_sdio_records_dump(void *sel, bool tab)
{
	struct rtw_io_record *record;
	struct rtw_io_record *last_io = NULL;
	struct rtw_io_record *last_rx_io = NULL;
	struct rtw_io_record *last_tx_io = NULL;
	sdio_rec_title_dump title_dump = tab ? sdio_record_title_dump_tab : sdio_record_title_dump;
	sdio_rec_value_dump value_dump = tab ? sdio_record_value_dump_tab : sdio_record_value_dump;
	bool rx_io, tx_io;
	size_t oldest_pos = dbg_rtw_io_records.record[dbg_rtw_io_records.pos].valid ? dbg_rtw_io_records.pos : 0;
	size_t i;

	title_dump(sel);

	for (i = 0; i < dbg_rtw_io_records.record_num; i++) {
		record = &dbg_rtw_io_records.record[(oldest_pos + i) % dbg_rtw_io_records.record_num];
		if (!record->valid)
			break;
		rx_io = rtw_sdio_record_is_rx(record);
		tx_io = rtw_sdio_record_is_tx(record);
		value_dump(sel, record, i, last_io, rx_io ? last_rx_io : NULL, tx_io ? last_tx_io : NULL);
		last_io = record;
		if (rx_io)
			last_rx_io = record;
		if (tx_io)
			last_tx_io = record;
	}
}

struct sdio_records_summary {
	sysptime stime;
	sysptime etime;

	size_t io_num;
	size_t himr_io_num;
	size_t hisr_io_num;
	size_t rx_io_num;
	size_t rx_aval_io_num;
	size_t tx_io_num;
	size_t tx_aval_io_num;
	size_t other_io_num;

	sysptime io_time; /* the total IO time */
	sysptime himr_io_time;
	sysptime hisr_io_time;
	sysptime rx_io_time;
	sysptime rx_aval_io_time;
	sysptime tx_io_time;
	sysptime tx_aval_io_time;
	sysptime other_io_time;

	sysptime rx_io_int_total;
	sysptime rx_io_int_min;
	sysptime rx_io_int_max;
	sysptime rx_io_int_avg;
	sysptime tx_io_int_total;
	sysptime tx_io_int_min;
	sysptime tx_io_int_max;
	sysptime tx_io_int_avg;

	u64 rx_io_len_total;
	u32 rx_io_len_min;
	u32 rx_io_len_max;
	u64 rx_io_len_avg;
	u64 tx_io_len_total;
	u32 tx_io_len_min;
	u32 tx_io_len_max;
	u64 tx_io_len_avg;
};

void rtw_sdio_records_summary_dump(void *sel)
{
	struct sdio_records_summary data;
	struct rtw_io_record *record;
	struct rtw_io_record *last_io = NULL;
	struct rtw_io_record *last_rx_io = NULL;
	struct rtw_io_record *last_tx_io = NULL;
	bool himr_io, hisr_io, rx_io, rx_aval_io, tx_io, tx_aval_io;
	size_t oldest_pos = dbg_rtw_io_records.record[dbg_rtw_io_records.pos].valid ? dbg_rtw_io_records.pos : 0;
	size_t i;

	_rtw_memset(&data, 0, sizeof(data));
	data.io_time = rtw_sptime_zero();
	data.himr_io_time = rtw_sptime_zero();
	data.hisr_io_time = rtw_sptime_zero();
	data.rx_io_time = rtw_sptime_zero();
	data.tx_io_time = rtw_sptime_zero();
	data.rx_aval_io_time = rtw_sptime_zero();
	data.tx_aval_io_time = rtw_sptime_zero();
	data.rx_io_int_total = rtw_sptime_zero();
	data.tx_io_int_total = rtw_sptime_zero();

	for (i = 0; i < dbg_rtw_io_records.record_num; i++) {
		record = &dbg_rtw_io_records.record[(oldest_pos + i) % dbg_rtw_io_records.record_num];
		if (!record->valid)
			break;
		himr_io = rtw_sdio_record_is_himr(record);
		hisr_io = rtw_sdio_record_is_hisr(record);
		rx_io = rtw_sdio_record_is_rx(record);
		rx_aval_io = rtw_sdio_record_is_rx_aval(record);
		tx_io = rtw_sdio_record_is_tx(record);
		tx_aval_io = rtw_sdio_record_is_tx_aval(record);

		if (i == 0)
			data.stime = record->stime;
		data.etime = record->etime;

		data.io_num++;
		data.io_time = rtw_sptime_add(data.io_time, rtw_sptime_sub(record->etime, record->stime));
		last_io = record;
		if (himr_io) {
			data.himr_io_num++;
			data.himr_io_time = rtw_sptime_add(data.himr_io_time, rtw_sptime_sub(record->etime, record->stime));
		}
		if (hisr_io) {
			data.hisr_io_num++;
			data.hisr_io_time = rtw_sptime_add(data.hisr_io_time, rtw_sptime_sub(record->etime, record->stime));
		}
		if (rx_io) {
			data.rx_io_len_total += record->cnt;
			if (data.rx_io_num == 0 || data.rx_io_len_min > record->cnt)
				data.rx_io_len_min = record->cnt;
			if (data.rx_io_num == 0 || data.rx_io_len_max < record->cnt)
				data.rx_io_len_max = record->cnt;
			data.rx_io_time = rtw_sptime_add(data.rx_io_time, rtw_sptime_sub(record->etime, record->stime));
			if (last_rx_io) {
				data.rx_io_int_total = rtw_sptime_add(data.rx_io_int_total, rtw_sptime_sub(record->stime, last_rx_io->etime));
				if (data.rx_io_num == 1 || rtw_sptime_after(data.rx_io_int_min, rtw_sptime_sub(record->stime, last_rx_io->etime)))
					data.rx_io_int_min = rtw_sptime_sub(record->stime, last_rx_io->etime);
				if (data.rx_io_num == 1 || rtw_sptime_before(data.rx_io_int_max, rtw_sptime_sub(record->stime, last_rx_io->etime)))
					data.rx_io_int_max = rtw_sptime_sub(record->stime, last_rx_io->etime);
			}
			data.rx_io_num++;
			last_rx_io = record;
		}
		if (rx_aval_io) {
			data.rx_aval_io_time = rtw_sptime_add(data.rx_aval_io_time, rtw_sptime_sub(record->etime, record->stime));
			data.rx_aval_io_num++;
		}
		if (tx_io) {
			data.tx_io_len_total += record->cnt;
			if (data.tx_io_num == 0 || data.tx_io_len_min > record->cnt)
				data.tx_io_len_min = record->cnt;
			if (data.tx_io_num == 0 || data.tx_io_len_max < record->cnt)
				data.tx_io_len_max = record->cnt;
			data.tx_io_time = rtw_sptime_add(data.tx_io_time, rtw_sptime_sub(record->etime, record->stime));
			if (last_tx_io) {
				data.tx_io_int_total = rtw_sptime_add(data.tx_io_int_total, rtw_sptime_sub(record->stime, last_tx_io->etime));
				if (data.tx_io_num == 1 || rtw_sptime_after(data.tx_io_int_min, rtw_sptime_sub(record->stime, last_tx_io->etime)))
					data.tx_io_int_min = rtw_sptime_sub(record->stime, last_tx_io->etime);
				if (data.tx_io_num == 1 || rtw_sptime_before(data.tx_io_int_max, rtw_sptime_sub(record->stime, last_tx_io->etime)))
					data.tx_io_int_max = rtw_sptime_sub(record->stime, last_tx_io->etime);
			}
			data.tx_io_num++;
			last_tx_io = record;
		}
		if (tx_aval_io) {
			data.tx_aval_io_time = rtw_sptime_add(data.tx_aval_io_time, rtw_sptime_sub(record->etime, record->stime));
			data.tx_aval_io_num++;
		}
	}

	data.other_io_num = data.io_num;
	data.other_io_num = data.other_io_num - data.himr_io_num;
	data.other_io_num = data.other_io_num - data.hisr_io_num;
	data.other_io_num = data.other_io_num - data.rx_io_num;
	data.other_io_num = data.other_io_num - data.tx_io_num;
	data.other_io_num = data.other_io_num - data.rx_aval_io_num;
	data.other_io_num = data.other_io_num - data.tx_aval_io_num;

	data.other_io_time = data.io_time;
	data.other_io_time = rtw_sptime_sub(data.other_io_time, data.himr_io_time);
	data.other_io_time = rtw_sptime_sub(data.other_io_time, data.hisr_io_time);
	data.other_io_time = rtw_sptime_sub(data.other_io_time, data.rx_io_time);
	data.other_io_time = rtw_sptime_sub(data.other_io_time, data.tx_io_time);
	data.other_io_time = rtw_sptime_sub(data.other_io_time, data.rx_aval_io_time);
	data.other_io_time = rtw_sptime_sub(data.other_io_time, data.tx_aval_io_time);

	data.rx_io_int_avg = data.rx_io_num ? rtw_ns_to_sptime(rtw_division64(rtw_sptime_to_ns(data.rx_io_int_total), (data.rx_io_num - 1))) : rtw_sptime_zero();
	data.tx_io_int_avg = data.tx_io_num ? rtw_ns_to_sptime(rtw_division64(rtw_sptime_to_ns(data.tx_io_int_total), (data.tx_io_num - 1))) : rtw_sptime_zero();

	data.rx_io_len_avg = rtw_division64(data.rx_io_len_total * 1000, rtw_sptime_diff_ms(data.stime, data.etime));
	data.tx_io_len_avg = rtw_division64(data.tx_io_len_total * 1000, rtw_sptime_diff_ms(data.stime, data.etime));

#define SEC_FMT "%lld.%09lld"
#define SEC_ARG(ns) rtw_division64(ns, 1000000000), rtw_modular64(ns, 1000000000)
#define PCT2_FMT "%2u.%02u%%"
#define PCT2_ARG(n, d) (u8)rtw_division64((n) * 100, d), (u8)rtw_modular64(rtw_division64((n) * 10000, d), 100)

	RTW_PRINT_SEL(sel,
		"         io_num: %zu\n"
		"    himr_io_num: %zu\n"
		"    hisr_io_num: %zu\n"
		"      rx_io_num: %zu\n"
		"      tx_io_num: %zu\n"
		" rx_aval_io_num: %zu\n"
		" tx_aval_io_num: %zu\n"
		"   other_io_num: %zu\n"
		"           time: "SEC_FMT"\n"
		"        io_time: "SEC_FMT"\n"
		"   himr_io_time: "SEC_FMT" ("PCT2_FMT")\n"
		"   hisr_io_time: "SEC_FMT" ("PCT2_FMT")\n"
		"     rx_io_time: "SEC_FMT" ("PCT2_FMT")\n"
		"     tx_io_time: "SEC_FMT" ("PCT2_FMT")\n"
		"rx_aval_io_time: "SEC_FMT" ("PCT2_FMT")\n"
		"tx_aval_io_time: "SEC_FMT" ("PCT2_FMT")\n"
		"  other_io_time: "SEC_FMT" ("PCT2_FMT")\n"
		"rx_io_int total: "SEC_FMT"\n"
		"            min: "SEC_FMT"\n"
		"            max: "SEC_FMT"\n"
		"            avg: "SEC_FMT"\n"
		"tx_io_int total: "SEC_FMT"\n"
		"            min: "SEC_FMT"\n"
		"            max: "SEC_FMT"\n"
		"            avg: "SEC_FMT"\n"
		"rx_io_len total: %llu\n"
		"            min: %u\n"
		"            max: %u\n"
		"            avg: %llu\n"
		"tx_io_len total: %llu\n"
		"            min: %u\n"
		"            max: %u\n"
		"            avg: %llu\n"
		"          rx_tp: %8llu Kbps\n"
		"          tx_tp: %8llu Kbps\n"
		, data.io_num
		, data.himr_io_num
		, data.hisr_io_num
		, data.rx_io_num
		, data.tx_io_num
		, data.rx_aval_io_num
		, data.tx_aval_io_num
		, data.other_io_num

		, SEC_ARG(rtw_sptime_diff_ns(data.stime, data.etime))
		, SEC_ARG(rtw_sptime_to_ns(data.io_time))
		, SEC_ARG(rtw_sptime_to_ns(data.himr_io_time)), PCT2_ARG(rtw_sptime_to_ns(data.himr_io_time), rtw_sptime_to_ns(data.io_time))
		, SEC_ARG(rtw_sptime_to_ns(data.hisr_io_time)), PCT2_ARG(rtw_sptime_to_ns(data.hisr_io_time), rtw_sptime_to_ns(data.io_time))
		, SEC_ARG(rtw_sptime_to_ns(data.rx_io_time)), PCT2_ARG(rtw_sptime_to_ns(data.rx_io_time), rtw_sptime_to_ns(data.io_time))
		, SEC_ARG(rtw_sptime_to_ns(data.tx_io_time)), PCT2_ARG(rtw_sptime_to_ns(data.tx_io_time), rtw_sptime_to_ns(data.io_time))
		, SEC_ARG(rtw_sptime_to_ns(data.rx_aval_io_time)), PCT2_ARG(rtw_sptime_to_ns(data.rx_aval_io_time), rtw_sptime_to_ns(data.io_time))
		, SEC_ARG(rtw_sptime_to_ns(data.tx_aval_io_time)), PCT2_ARG(rtw_sptime_to_ns(data.tx_aval_io_time), rtw_sptime_to_ns(data.io_time))
		, SEC_ARG(rtw_sptime_to_ns(data.other_io_time)), PCT2_ARG(rtw_sptime_to_ns(data.other_io_time), rtw_sptime_to_ns(data.io_time))

		, SEC_ARG(rtw_sptime_to_ns(data.rx_io_int_total))
		, SEC_ARG(rtw_sptime_to_ns(data.rx_io_int_min))
		, SEC_ARG(rtw_sptime_to_ns(data.rx_io_int_max))
		, SEC_ARG(rtw_sptime_to_ns(data.rx_io_int_avg))

		, SEC_ARG(rtw_sptime_to_ns(data.tx_io_int_total))
		, SEC_ARG(rtw_sptime_to_ns(data.tx_io_int_min))
		, SEC_ARG(rtw_sptime_to_ns(data.tx_io_int_max))
		, SEC_ARG(rtw_sptime_to_ns(data.tx_io_int_avg))

		, data.rx_io_len_total
		, data.rx_io_len_min
		, data.rx_io_len_max
		, rtw_division64(data.rx_io_len_total, data.rx_io_num)

		, data.tx_io_len_total
		, data.tx_io_len_min
		, data.tx_io_len_max
		, rtw_division64(data.tx_io_len_total, data.tx_io_num)

		, rtw_division64(data.rx_io_len_avg * 8, 1024)
		, rtw_division64(data.tx_io_len_avg * 8, 1024)
		);
}

void rtw_sdio_records_claim_and_enable(struct dvobj_priv *d, bool enable)
{
	struct sdio_func *func;
	bool claim_needed;

#if !CONFIG_RTW_IO_RECORDS_STATIC
	if (!dbg_rtw_io_records.record) {
		rtw_warn_on(1);
		return;
	}
#endif

	func = dvobj_to_sdio_func(d);
	claim_needed = rtw_sdio_claim_host_needed(func);
	if (claim_needed)
		sdio_claim_host(func);

	RTW_INFO("%s enable:%d\n", __func__, enable);
	dbg_rtw_io_records.enable = enable;

	if (claim_needed)
		sdio_release_host(func);
}
#endif /* CONFIG_RTW_IO_RECORDS */

/*#define RTW_SDIO_DUMP*/
#ifdef RTW_SDIO_DUMP
#define DUMP_LEN_LMT	0	/* buffer dump size limit */
				/* unit: byte, 0 for no limit */
#else
#define DUMP_LEN_LMT	32
#endif
#define GET_DUMP_LEN(len)	(DUMP_LEN_LMT ? rtw_min(len, DUMP_LEN_LMT) : len)

#ifdef DBG_SDIO
#if (DBG_SDIO >= 1)
static void sdio_dump_reg_by_cmd52(struct dvobj_priv *d,
				   u32 addr, size_t len, u8 *buf)
{
	struct sdio_func *func;
	size_t i;
	u8 val;
	u8 str[80], used = 0;
	u8 read_twice = 0;
	int error;


	if (buf)
		_rtw_memset(buf, 0xAE, len);
	func = dvobj_to_sdio_func(d);
	/*
	 * When register is WLAN IOREG,
	 * read twice to guarantee the result is correct.
	 */
	if (addr & 0x10000)
		read_twice = 1;

	_rtw_memset(str, 0, 80);
	used = 0;
	if (addr & 0xF) {
		used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%*s", (addr&0xF)*5, "");
	}
	for (i = 0; i < len; i++, addr++) {
		val = sdio_readb(func, addr, &error);
		if (read_twice)
			val = sdio_readb(func, addr, &error);
		if (error)
			break;

		if (buf)
			buf[i] = val;

		if (!(addr & 0xF))
			used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%02x ", val);
		if (((i + 1) < len) && ((addr + 1) & 0xF) == 0) {
			dev_err(&func->dev, "%s", str);
			_rtw_memset(str, 0, 80);
			used = 0;
		}
	}

	if (used) {
		dev_err(&func->dev, "%s", str);
		_rtw_memset(str, 0, 80);
		used = 0;
	}

	if (error)
		dev_err(&func->dev, "rtw_sdio_dbg: READ 0x%02x FAIL!", addr);
}

static void sdio_dump_cia(struct dvobj_priv *d, u32 addr, size_t len, u8 *buf)
{
	struct sdio_func *func;
	size_t i;
	u8 val;
	u8 str[80], used = 0;
	int error;


	if (buf)
		_rtw_memset(buf, 0xAE, len);
	func = dvobj_to_sdio_func(d);

	_rtw_memset(str, 0, 80);
	used = 0;
	if (addr & 0xF) {
		used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%*s", (addr&0xF)*5, "");
	}
	for (i = 0; i < len; i++, addr++) {
		val = sdio_f0_readb(func, addr, &error);
		if (error)
			break;

		if (buf)
			buf[i] = val;

		if (!(addr & 0xF))
			used += snprintf(str+used, (80-used), "0x%02x:\t", addr&~0xF);
		used += snprintf(str+used, (80-used), "%02x ", val);
		if (((i + 1) < len) && ((addr + 1) & 0xF) == 0) {
			dev_err(&func->dev, "%s", str);
			_rtw_memset(str, 0, 80);
			used = 0;
		}
	}

	if (used) {
		dev_err(&func->dev, "%s", str);
		_rtw_memset(str, 0, 80);
		used = 0;
	}

	if (error)
		dev_err(&func->dev, "rtw_sdio_dbg: READ CIA 0x%02x FAIL!",
			addr);
}

#if (DBG_SDIO >= 2)
void rtw_sdio_dbg_reg_alloc(struct dvobj_priv *d);
#endif /* DBG_SDIO >= 2 */

/*
 * Dump register when CMD53 fail
 */
static void sdio_dump_dbg_reg(struct dvobj_priv *d, u8 write,
			      unsigned int addr, size_t len)
{
	struct sdio_data *sdio;
	struct sdio_func *func;
	u8 *buf = NULL;
#if (DBG_SDIO >= 2)
	u8 *msg;
#endif /* DBG_SDIO >= 2 */


	sdio = dvobj_to_sdio(d);
	if (sdio->reg_dump_mark)
		return;
	func = dvobj_to_sdio_func(d);

	sdio->reg_dump_mark = sdio->cmd53_err_cnt;

#if (DBG_SDIO >= 2)
	if (!sdio->dbg_msg) {
		msg = rtw_zmalloc(80);
		if (msg) {
			sdio->dbg_msg = msg;
			sdio->dbg_msg_size = 80;
		}
	}
	if (sdio->dbg_msg_size) {
		snprintf(sdio->dbg_msg, sdio->dbg_msg_size,
			 "CMD53 %s 0x%05x, %zu bytes FAIL "
			 "at err_cnt=%d",
			 write?"WRITE":"READ",
			 addr, len, sdio->reg_dump_mark);
	}

	rtw_sdio_dbg_reg_alloc(d);
#endif /* DBG_SDIO >= 2 */

	/* MAC register */
	dev_err(&func->dev, "MAC register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_mac;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_reg_by_cmd52(d, 0x10000, 0x800, buf);
	dev_err(&func->dev, "MAC Extend register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_mac_ext;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_reg_by_cmd52(d, 0x11000, 0x800, buf);

	/* SDIO local register */
	dev_err(&func->dev, "SDIO Local register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_local;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_reg_by_cmd52(d, 0x0, 0x100, buf);

	/* F0 */
	dev_err(&func->dev, "f0 register:");
#if (DBG_SDIO >= 2)
	buf = sdio->reg_cia;
#endif /* DBG_SDIO >= 2 */
	sdio_dump_cia(d, 0x0, 0x200, buf);
}
#endif /* DBG_SDIO >= 1 */
#endif /* DBG_SDIO */

/**
 *	Returns driver error code,
 *	0	no error
 *	-1	Level 1 error, critical error and can't be recovered
 *	-2	Level 2 error, normal error, retry to recover is possible
 */
static int linux_io_err_to_drv_err(int err)
{
	if (!err)
		return 0;

	/* critical error */
	if ((err == -ESHUTDOWN) ||
	    (err == -ENODEV) ||
	    (err == -ENOMEDIUM))
		return -1;

	/* other error */
	return -2;
}

/**
 *	rtw_sdio_raw_read - Read from SDIO device
 *	@d: driver object private data
 *	@addr: address to read
 *	@buf: buffer to store the data
 *	@len: number of bytes to read
 *	@fixed:
 *
 *	Reads from the address space of a SDIO device.
 *	Return value indicates if the transfer succeeded or not.
 */
int __must_check rtw_sdio_raw_read(struct dvobj_priv *d, unsigned int addr,
				   void *buf, size_t len, bool fixed)
{
	int error = -EPERM;
	bool f0, cmd52;
	struct sdio_func *func;
	bool claim_needed;
	u32 offset, i;
	struct sdio_data *sdio;
	u8 *tmpbuf = NULL;
	DECLARE_DBG_RTW_IO_RECORD_P(r);

	func = dvobj_to_sdio_func(d);
	claim_needed = rtw_sdio_claim_host_needed(func);
	f0 = RTW_SDIO_ADDR_F0_CHK(addr);
	cmd52 = RTW_SDIO_ADDR_CMD52_CHK(addr);
	sdio = dvobj_to_sdio(d);

	/*
	 * Mask addr to remove driver defined bit and
	 * make sure addr is in valid range
	 */
	if (f0)
		addr &= 0xFFF;
	else
		addr &= 0x1FFFF;

#ifdef RTW_SDIO_DUMP
	if (f0)
		dev_dbg(&func->dev, "RTW_SDIO: READ F0\n");
	else if (cmd52)
		dev_dbg(&func->dev, "RTW_SDIO: READ use CMD52\n");
	else
		dev_dbg(&func->dev, "RTW_SDIO: READ use CMD53\n");

	dev_dbg(&func->dev, "RTW_SDIO: READ from 0x%05x\n", addr);
#endif /* RTW_SDIO_DUMP */

	if (claim_needed)
		sdio_claim_host(func);

	if (f0) {
		offset = addr;
		for (i = 0; i < len; i++, offset++) {
			dbg_rtw_io_record_get_and_advance(r);
			dbg_rtw_io_record_fill_stime(r);
			((u8 *)buf)[i] = sdio_f0_readb(func, offset, &error);
			dbg_rtw_io_record_fill_1(r, SDIO_CMDT_F0_52_READ, offset, error, ((u8 *)buf) + i);
			if (error)
				break;
#if 0
			dev_info(&func->dev, "%s: sdio f0 read 52 addr 0x%x, byte 0x%02x\n",
				 __func__, offset, ((u8 *)buf)[i]);
#endif
		}
	} else {
		if (cmd52) {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio read 52 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			offset = addr;
			for (i = 0; i < len; i++) {
				dbg_rtw_io_record_get_and_advance(r);
				dbg_rtw_io_record_fill_stime(r);
				((u8 *)buf)[i] = sdio_readb(func, offset, &error);
				dbg_rtw_io_record_fill_1(r, SDIO_CMDT_52_READ, offset, error, ((u8 *)buf) + i);
				if (error)
					break;
#if 0
				dev_info(&func->dev, "%s: sdio read 52 addr 0x%x, byte 0x%02x\n",
					 __func__, offset, ((u8 *)buf)[i]);
#endif
				if (!fixed)
					offset++;
			}
		} else {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio read 53 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			if (len <= sdio->tmpbuf_sz) {
				tmpbuf = buf;
				buf = sdio->tmpbuf;
			}
			if (fixed) {
				dbg_rtw_io_record_get_and_advance(r);
				dbg_rtw_io_record_fill_stime(r);
				error = sdio_readsb(func, buf, addr, len);
				dbg_rtw_io_record_fill_n(r, SDIO_CMDT_53_READ, addr, len, error, buf);
			} else {
				dbg_rtw_io_record_get_and_advance(r);
				dbg_rtw_io_record_fill_stime(r);
				error = sdio_memcpy_fromio(func, buf, addr, len);
				dbg_rtw_io_record_fill_n(r, SDIO_CMDT_53_READ, addr, len, error, buf);
			}
			if (!error && tmpbuf)
				_rtw_memcpy(tmpbuf, buf, len);
		}
	}

#ifdef DBG_SDIO
#if (DBG_SDIO >= 3)
	if (!error && !f0 && !cmd52
	    && (sdio->dbg_enable
		&& sdio->err_test && !sdio->err_test_triggered
		&& ((addr & 0x10000)
		    || (!(addr & 0xE000)
			&& !((addr >= 0x40) && (addr < 0x48)))))) {
		sdio->err_test_triggered = 1;
		error = -ETIMEDOUT;
		dev_warn(&func->dev, "Simulate error(%d) READ addr=0x%05x %zu bytes",
			 error, addr, len);
	}
#endif /* DBG_SDIO >= 3 */

	if (error) {
		if (f0 || cmd52) {
			sdio->cmd52_err_cnt++;
		} else {
			sdio->cmd53_err_cnt++;
#if (DBG_SDIO >= 1)
			sdio_dump_dbg_reg(d, 0, addr, len);
#endif /* DBG_SDIO >= 1 */
		}
	}
#endif /* DBG_SDIO */

	if (claim_needed)
		sdio_release_host(func);

#ifdef RTW_SDIO_DUMP
	print_hex_dump(KERN_DEBUG, "RTW_SDIO: READ ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       buf, GET_DUMP_LEN(len), false);
#endif /* RTW_SDIO_DUMP */

	if (WARN_ON(error)) {
		dev_err(&func->dev, "%s: sdio read failed (%d)\n", __func__, error);
#ifndef RTW_SDIO_DUMP
		if (f0)
			dev_err(&func->dev, "RTW_SDIO: READ F0\n");
		if (cmd52)
			dev_err(&func->dev, "RTW_SDIO: READ use CMD52\n");
		else
			dev_err(&func->dev, "RTW_SDIO: READ use CMD53\n");
		dev_err(&func->dev, "RTW_SDIO: READ from 0x%05x, %zu bytes\n", addr, len);
		print_hex_dump(KERN_ERR, "RTW_SDIO: READ ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buf, GET_DUMP_LEN(len), false);
#endif /* !RTW_SDIO_DUMP */
	}

	return linux_io_err_to_drv_err(error);
}

/**
 *	rtw_sdio_raw_write - Write to SDIO device
 *	@d: driver object private data
 *	@addr: address to write
 *	@buf: buffer that contains the data to write
 *	@len: number of bytes to write
 *	@fixed: address is fixed(FIFO) or incremented
 *
 *	Writes to the address space of a SDIO device.
 *	Return value indicates if the transfer succeeded or not.
 */
int __must_check rtw_sdio_raw_write(struct dvobj_priv *d, unsigned int addr,
				    void *buf, size_t len, bool fixed)
{
	int error = -EPERM;
	bool f0, cmd52;
	struct sdio_func *func;
	bool claim_needed;
	u32 offset, i;
	struct sdio_data *sdio;
	DECLARE_DBG_RTW_IO_RECORD_P(r);

	func = dvobj_to_sdio_func(d);
	claim_needed = rtw_sdio_claim_host_needed(func);
	f0 = RTW_SDIO_ADDR_F0_CHK(addr);
	cmd52 = RTW_SDIO_ADDR_CMD52_CHK(addr);
	sdio = dvobj_to_sdio(d);

	/*
	 * Mask addr to remove driver defined bit and
	 * make sure addr is in valid range
	 */
	if (f0)
		addr &= 0xFFF;
	else
		addr &= 0x1FFFF;

#ifdef RTW_SDIO_DUMP
	if (f0)
		dev_dbg(&func->dev, "RTW_SDIO: WRITE F0\n");
	else if (cmd52)
		dev_dbg(&func->dev, "RTW_SDIO: WRITE use CMD52\n");
	else
		dev_dbg(&func->dev, "RTW_SDIO: WRITE use CMD53\n");
	dev_dbg(&func->dev, "RTW_SDIO: WRITE to 0x%05x\n", addr);
	print_hex_dump(KERN_DEBUG, "RTW_SDIO: WRITE ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       buf, GET_DUMP_LEN(len), false);
#endif /* RTW_SDIO_DUMP */

	if (claim_needed)
		sdio_claim_host(func);

	if (f0) {
		offset = addr;
		for (i = 0; i < len; i++, offset++) {
			dbg_rtw_io_record_get_and_advance(r);
			dbg_rtw_io_record_fill_stime(r);
			sdio_f0_writeb(func, ((u8 *)buf)[i], offset, &error);
			dbg_rtw_io_record_fill_1(r, SDIO_CMDT_F0_52_WRITE, offset, error, ((u8 *)buf) + i);
			if (error)
				break;
#if 0
			dev_info(&func->dev, "%s: sdio f0 write 52 addr 0x%x, byte 0x%02x\n",
				 __func__, offset, ((u8 *)buf)[i]);
#endif
		}
	} else {
		if (cmd52) {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio write 52 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			offset = addr;
			for (i = 0; i < len; i++) {
				dbg_rtw_io_record_get_and_advance(r);
				dbg_rtw_io_record_fill_stime(r);
				sdio_writeb(func, ((u8 *)buf)[i], offset, &error);
				dbg_rtw_io_record_fill_1(r, SDIO_CMDT_52_WRITE, offset, error, ((u8 *)buf) + i);
				if (error)
					break;
#if 0
				dev_info(&func->dev, "%s: sdio write 52 addr 0x%x, byte 0x%02x\n",
					 __func__, offset, ((u8 *)buf)[i]);
#endif
				if (!fixed)
					offset++;
			}
		} else {
#ifdef RTW_SDIO_IO_DBG
			dev_info(&func->dev, "%s: sdio write 53 addr 0x%x, %zu bytes\n",
				 __func__, addr, len);
#endif
			if (len <= sdio->tmpbuf_sz) {
				_rtw_memcpy(sdio->tmpbuf, buf, len);
				buf = sdio->tmpbuf;
			}
			if (fixed) {
				dbg_rtw_io_record_get_and_advance(r);
				dbg_rtw_io_record_fill_stime(r);
				error = sdio_writesb(func, addr, buf, len);
				dbg_rtw_io_record_fill_n(r, SDIO_CMDT_53_WRITE, addr, len, error, buf);
			} else {
				dbg_rtw_io_record_get_and_advance(r);
				dbg_rtw_io_record_fill_stime(r);
				error = sdio_memcpy_toio(func, addr, buf, len);
				dbg_rtw_io_record_fill_n(r, SDIO_CMDT_53_WRITE, addr, len, error, buf);
			}
		}
	}

#ifdef DBG_SDIO
	if (error) {
		if (f0 || cmd52) {
			sdio->cmd52_err_cnt++;
		} else {
			sdio->cmd53_err_cnt++;
#if (DBG_SDIO >= 1)
			sdio_dump_dbg_reg(d, 1, addr, len);
#endif /* DBG_SDIO >= 1 */
		}
	}
#endif /* DBG_SDIO */

	if (claim_needed)
		sdio_release_host(func);

	if (WARN_ON(error)) {
		dev_err(&func->dev, "%s: sdio write failed (%d)\n", __func__, error);
#ifndef RTW_SDIO_DUMP
		if (f0)
			dev_err(&func->dev, "RTW_SDIO: WRITE F0\n");
		if (cmd52)
			dev_err(&func->dev, "RTW_SDIO: WRITE use CMD52\n");
		else
			dev_err(&func->dev, "RTW_SDIO: WRITE use CMD53\n");
		dev_err(&func->dev, "RTW_SDIO: WRITE to 0x%05x, %zu bytes\n", addr, len);
		print_hex_dump(KERN_ERR, "RTW_SDIO: WRITE ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       buf, GET_DUMP_LEN(len), false);
#endif /* !RTW_SDIO_DUMP */
	}

	return linux_io_err_to_drv_err(error);
}
