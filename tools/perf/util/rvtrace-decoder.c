/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright(C) 2015-2018 Linaro Limited.
 *
 * Author: Tor Jeremiassen <tor@ti.com>
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/types.h>
#include <sys/mman.h>

#include <stdlib.h>

#include "auxtrace.h"
#include "color.h"
#include "debug.h"
#include "evlist.h"
#include "intlist.h"
#include "machine.h"
#include "map.h"
#include "perf.h"
#include "session.h"
#include "tool.h"
#include "thread.h"
#include "thread_map.h"
#include "thread-stack.h"
#include "util.h"
#include "dso.h"
#include "addr_location.h"
#include <inttypes.h>
#include "util/synthetic-events.h"
#include "rvtrace.h"
#include "nexus-rv-decoder/nexus-rv-decoder.h"
#include "../../arch/riscv/include/asm/insn.h"

#define MAX_TIMESTAMP (~0ULL)

struct rvtrace_auxtrace {
	struct auxtrace auxtrace;
	struct auxtrace_queues queues;
	struct auxtrace_heap heap;
	struct itrace_synth_opts synth_opts;
	struct perf_session *session;
	struct machine *machine;
	struct thread *unknown_thread;

	u8 timeless_decoding;
	u8 snapshot_mode;
	u8 data_queued;

	int num_cpu;
	u32 auxtrace_type;
	u64 branches_sample_type;
	u64 branches_id;
	u64 **metadata;
	u64 kernel_start;
	unsigned int pmu_type;
};

struct rvtrace_queue {
	struct rvtrace_auxtrace *rvtrace;
	struct thread *thread;
	struct nexus_rv_insn_decoder *decoder;
	struct auxtrace_buffer *buffer;
	union perf_event *event_buf;
	unsigned int queue_nr;
	pid_t pid, tid;
	int cpu;
	u64 time;
	u64 timestamp;
	u64 offset;
};

static void rvtrace_set_pid_tid_cpu(struct rvtrace_auxtrace *rvtrace,
				    struct auxtrace_queue *queue)
{
	struct rvtrace_queue *rvtraceq = queue->priv;

	/* CPU-wide tracing isn't supported yet */
	if (queue->tid == -1)
		return;

	if ((!rvtraceq->thread) && (rvtraceq->tid != -1))
		rvtraceq->thread = machine__find_thread(rvtrace->machine, -1, rvtraceq->tid);

	if (rvtraceq->thread) {
		rvtraceq->pid = rvtraceq->thread->pid_;
		if (queue->cpu == -1)
			rvtraceq->cpu = rvtraceq->thread->cpu;
	}
}

static u32 rvtrace_devmem_access(u64 address, size_t size, u8 *buffer)
{
	int fd;
	void *map_base, *virt_addr;
	u64 page_size = 4096, mapped_size = 4096;
	u64 page_base = address & ~(page_size - 1);
	u64 offset_in_page = address - page_base;
	unsigned int width = 8 * size;

	if (offset_in_page + width > page_size)
		mapped_size *= 2;

	fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0)
		return 0;

	map_base = mmap(NULL, mapped_size, PROT_READ, MAP_SHARED, fd, (off_t)(page_base));
	if (map_base == MAP_FAILED) {
		pr_debug("failed to mmap device address 0x%lx\n", address);
		close(fd);
		return 0;
	}

	virt_addr = (char*)map_base + offset_in_page;

	memcpy(buffer, virt_addr, size);

	munmap(map_base, mapped_size);
	close(fd);
	return size;
}

static u32 rvtrace_mem_access(void *data, u64 address, enum riscv_privilege_mode prv,
			      size_t size, u8 *buffer)
{
	u8 cpumode;
	u64 offset;
	int len;
	struct machine *machine;
	struct addr_location al;
	struct dso *dso;
	int ret = 0;

	struct rvtrace_queue *rvtraceq = data;
	if (!rvtraceq)
		goto out;

	addr_location__init(&al);

	/* If the riscv_privilege_mode is machine mode, access physical address by /dev/mem */
	if (prv == RISCV_PRIV_MACHINE_MODE)
		return rvtrace_devmem_access(address, size, buffer);

	machine = rvtraceq->rvtrace->machine;
	if (address >= machine__kernel_start(machine))
		cpumode = PERF_RECORD_MISC_KERNEL;
	else
		cpumode = PERF_RECORD_MISC_USER;

	if (!thread__find_map(rvtraceq->thread, cpumode, address, &al))
		goto out;

	dso = map__dso(al.map);
	if (!dso)
		goto out;

	if (dso__data(dso)->status == DSO_DATA_STATUS_ERROR &&
	    dso__data_status_seen(dso, DSO_DATA_STATUS_SEEN_ITRACE))
		goto out;

	offset = map__map_ip(al.map, address);

	map__load(al.map);

	len = dso__data_read_offset(dso, machine, offset, buffer, size);
	if (len <= 0) {
		ui__warning_once("RISC-V Nexus Trace: Missing DSO. Use 'perf archive' or debuginfod to export data from the traced system.\n"
				 "                  Enable CONFIG_PROC_KCORE or use option '-k /path/to/vmlinux' for kernel symbols.\n");
		if (!dso__auxtrace_warned(dso)) {
			pr_err("RISC-V Nexus Trace: Debug data not found for address %#"PRIx64" in %s\n",
				address,
				dso__long_name(dso) ? dso__long_name(dso) : "Unknown");
			dso__set_auxtrace_warned(dso);
		}
		goto out;
	}
	ret = len;
out:
	addr_location__exit(&al);
	return ret;
}


static u8 rvtrace_cpu_mode(enum riscv_privilege_mode prv)
{
	u8 cpumode;
	switch (prv) {
	case RISCV_PRIV_USER_MODE:
		cpumode = PERF_RECORD_MISC_USER;
		break;
	case RISCV_PRIV_SUPERVISOR_MODE:
	case RISCV_PRIV_MACHINE_MODE:
	default:
		cpumode = PERF_RECORD_MISC_KERNEL;
		break;
	}
	return cpumode;
}

static void rvtrace_synth_copy_insn(struct rvtrace_queue *rvtraceq,
				    struct nexus_rv_packet *packet,
				    struct perf_sample *sample)
{
	int ret;
	u32 insn;

	ret = rvtrace_mem_access(rvtraceq, sample->ip, packet->prv, sizeof(insn), (u8 *)&insn);
	if (!ret)
		return;

	sample->insn_len = riscv_insn_is_c(insn) ? 2 : 4;
	memcpy(sample->insn, &insn, sample->insn_len);
}

static int rvtrace_synth_branch_sample(struct rvtrace_queue *rvtraceq,
				       struct nexus_rv_packet *packet)
{
	int ret = 0;
	struct rvtrace_auxtrace *rvtrace = rvtraceq->rvtrace;
	struct perf_sample sample;
	union perf_event *event = rvtraceq->event_buf;

	perf_sample__init(&sample, /*all=*/true);
	event->sample.header.type = PERF_RECORD_SAMPLE;
	event->sample.header.misc = rvtrace_cpu_mode(packet->prv);
	event->sample.header.size = sizeof(struct perf_event_header);

	sample.ip = packet->start_addr;
	sample.pid = rvtraceq->pid;
	sample.tid = rvtraceq->tid;
	sample.addr = packet->end_addr;
	sample.insn_cnt = packet->insn_cnt;
	sample.id = rvtraceq->rvtrace->branches_id;
	sample.stream_id = rvtraceq->rvtrace->branches_id;
	sample.period = 1;
	sample.cpu = packet->cpu;
	sample.flags = 0;
	sample.cpumode = event->sample.header.misc;

	rvtrace_synth_copy_insn(rvtraceq, packet, &sample);

	ret = perf_session__deliver_synth_event(rvtrace->session, event, &sample);
	if (ret)
		pr_err(
		"RISC-V Trace: failed to deliver instruction event, error %d\n",
		ret);
	perf_sample__exit(&sample);

	return ret;
}

static int rvtrace_synth_events(struct rvtrace_auxtrace *rvtrace,
				struct perf_session *session)
{
	struct evlist *evlist = session->evlist;
	struct evsel *evsel;
	struct perf_event_attr attr;
	bool found = false;
	u64 id;
	int err;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.type == rvtrace->pmu_type) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_debug("No selected events with RISC-V Trace data\n");
		return 0;
	}

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.size = sizeof(struct perf_event_attr);
	attr.type = PERF_TYPE_HARDWARE;
	attr.sample_type = evsel->core.attr.sample_type & PERF_SAMPLE_MASK;
	attr.sample_type |= PERF_SAMPLE_IP | PERF_SAMPLE_TID |
			    PERF_SAMPLE_PERIOD;
	if (rvtrace->timeless_decoding)
		attr.sample_type &= ~(u64)PERF_SAMPLE_TIME;
	else
		attr.sample_type |= PERF_SAMPLE_TIME;

	attr.exclude_user = evsel->core.attr.exclude_user;
	attr.exclude_kernel = evsel->core.attr.exclude_kernel;
	attr.exclude_hv = evsel->core.attr.exclude_hv;
	attr.exclude_host = evsel->core.attr.exclude_host;
	attr.exclude_guest = evsel->core.attr.exclude_guest;
	attr.sample_id_all = evsel->core.attr.sample_id_all;
	attr.read_format = evsel->core.attr.read_format;

	/* create new id val to be a fixed offset from evsel id */
	id = evsel->core.id[0] + 1000000000;
	if (!id)
		id = 1;

	if (rvtrace->synth_opts.branches) {
		attr.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
		attr.sample_period = 1;
		attr.sample_type |= PERF_SAMPLE_ADDR;

		err = perf_session__deliver_synth_attr_event(session, &attr, id);
		if (err)
			return err;
		rvtrace->branches_sample_type = attr.sample_type;
		rvtrace->branches_id = id;
		id += 1;
		attr.sample_type &= ~(u64)PERF_SAMPLE_ADDR;
	}

	return 0;
}

static int rvtrace_run_decoder(struct rvtrace_queue *rvtraceq)
{
	int ret;
	struct rvtrace_auxtrace *rvtrace = rvtraceq->rvtrace;
	struct nexus_rv_packet_buffer *packet_buffer = &rvtraceq->decoder->packet_buffer;

	if (!rvtrace->kernel_start)
		rvtrace->kernel_start = machine__kernel_start(rvtrace->machine);

	ret = nexus_rv_insn_decode(rvtraceq->decoder);
	if (ret)
		return ret;

	for (int i = 0; i < packet_buffer->size; i++) {
		struct nexus_rv_packet packet = packet_buffer->packets[i];
		if (packet.sample_type == RVTRACE_RANGE)
			rvtrace_synth_branch_sample(rvtraceq, &packet);
	}

	return 0;
}

static int rvtrace_process_timeless_queues(struct rvtrace_auxtrace *rvtrace,
						  pid_t tid, u64 time_)
{
	unsigned int i;
	struct auxtrace_queues *queues = &rvtrace->queues;
	for (i = 0; i < queues->nr_queues; i++) {
		struct auxtrace_queue *queue = &rvtrace->queues.queue_array[i];
		struct rvtrace_queue *rvtraceq = queue->priv;

		if (rvtraceq && ((tid == -1) || (rvtraceq->tid == tid))) {
			rvtraceq->time = time_;
			rvtrace_set_pid_tid_cpu(rvtrace, queue);
			rvtrace_run_decoder(rvtraceq);
		}
	}

	return 0;
}


static int rvtrace_flush_events(struct perf_session *session __maybe_unused,
				const struct perf_tool *tool __maybe_unused)
{
	return 0;
}

static void rvtrace_free_queue(void *priv)
{
	struct rvtrace_queue *rvtraceq = priv;

	if (!rvtraceq)
		return;

	thread__zput(rvtraceq->thread);
	nexus_rv_insn_decoder_free(rvtraceq->decoder);
	zfree(&rvtraceq->event_buf);
	free(rvtraceq);
}

static void rvtrace_free_events(struct perf_session *session)
{
	unsigned int i;
	struct rvtrace_auxtrace *rvtrace = container_of(session->auxtrace,
						    struct rvtrace_auxtrace,
						    auxtrace);
	struct auxtrace_queues *queues = &rvtrace->queues;

	for (i = 0; i < queues->nr_queues; i++) {
		rvtrace_free_queue(queues->queue_array[i].priv);
		queues->queue_array[i].priv = NULL;
	}

	auxtrace_queues__free(queues);
}

static void rvtrace_free(struct perf_session *session)
{
	struct rvtrace_auxtrace *rvtrace = container_of(session->auxtrace,
							struct rvtrace_auxtrace,
							auxtrace);
	rvtrace_free_events(session);
	session->auxtrace = NULL;
	for (int i = 0; i < rvtrace->num_cpu; i++)
		zfree(&rvtrace->metadata[i]);

	zfree(&rvtrace->metadata);
	zfree(&rvtrace);
}

static bool rvtrace_evsel_is_auxtrace(struct perf_session *session,
					     struct evsel *evsel)
{
	struct rvtrace_auxtrace *aux = container_of(session->auxtrace,
						    struct rvtrace_auxtrace,
						    auxtrace);

	return evsel->core.attr.type == aux->pmu_type;
}

static int rvtrace_get_trace(struct nexus_rv_buffer *buffer, void *data)
{
	struct rvtrace_queue *rvtraceq = data;
	struct auxtrace_buffer *aux_buffer = rvtraceq->buffer;
	struct auxtrace_buffer *old_buffer = aux_buffer;
	struct auxtrace_queue *queue;

	queue = &rvtraceq->rvtrace->queues.queue_array[rvtraceq->queue_nr];

	aux_buffer = auxtrace_buffer__next(queue, aux_buffer);

	/* If no more data, drop the previous auxtrace_buffer and return */
	if (!aux_buffer) {
		if (old_buffer)
			auxtrace_buffer__drop_data(old_buffer);
		buffer->len = 0;
		return 0;
	}

	rvtraceq->buffer = aux_buffer;

	/* If the aux_buffer doesn't have data associated, try to load it */
	if (!aux_buffer->data) {
		/* get the file desc associated with the perf data file */
		int fd = perf_data__fd(rvtraceq->rvtrace->session->data);

		aux_buffer->data = auxtrace_buffer__get_data(aux_buffer, fd);
		if (!aux_buffer->data)
			return -ENOMEM;
	}

	/* If valid, drop the previous buffer */
	if (old_buffer)
		auxtrace_buffer__drop_data(old_buffer);

	buffer->len = aux_buffer->size;
	buffer->buf = aux_buffer->data;

	return 0;
}

static struct rvtrace_queue *rvtrace_alloc_queue(struct rvtrace_auxtrace *rvtrace,
                                               unsigned int queue_nr)
{
	struct nexus_rv_insn_decoder_params params;
	struct rvtrace_queue *rvtraceq;

	rvtraceq = zalloc(sizeof(*rvtraceq));
	if (!rvtraceq)
		return NULL;

	rvtraceq->event_buf = malloc(PERF_SAMPLE_MAX_SIZE);
	if (!rvtraceq->event_buf)
		goto out_free;

	rvtraceq->rvtrace = rvtrace;
	rvtraceq->queue_nr = queue_nr;
	rvtraceq->pid = -1;
	rvtraceq->tid = -1;
	rvtraceq->cpu = -1;

	params.get_trace = rvtrace_get_trace;
	params.mem_access = rvtrace_mem_access;
	params.data = rvtraceq;
	params.formatted = true;
	if (!rvtrace->metadata[0][RVTRACE_ENCODER_INHB_SRC])
		params.src_bits = rvtrace->metadata[0][RVTRACE_ENCODER_SRCBITS];

	rvtraceq->decoder = nexus_rv_insn_decoder_new(&params);
	if (!rvtraceq->decoder)
		goto out_free;

	rvtraceq->offset = 0;

	return rvtraceq;

out_free:
	zfree(&rvtraceq->event_buf);
	free(rvtraceq);

	return NULL;
}

static int rvtrace_setup_queue(struct rvtrace_auxtrace *rvtrace,
                              struct auxtrace_queue *queue,
                              unsigned int queue_nr)
{
	struct rvtrace_queue *rvtraceq = queue->priv;

	if (list_empty(&queue->head) || rvtraceq)
		return 0;

	rvtraceq = rvtrace_alloc_queue(rvtrace, queue_nr);

	if (!rvtraceq)
		return -ENOMEM;

	queue->priv = rvtraceq;

	if (queue->cpu != -1)
		rvtraceq->cpu = queue->cpu;

	rvtraceq->tid = queue->tid;

	return 0;
}

static int rvtrace_setup_queues(struct rvtrace_auxtrace *rvtrace)
{
	unsigned int i;
	int ret;

	for (i = 0; i < rvtrace->queues.nr_queues; i++) {
		ret = rvtrace_setup_queue(rvtrace, &rvtrace->queues.queue_array[i], i);
		if (ret)
			return ret;
	}

	return 0;
}

static int rvtrace_update_queues(struct rvtrace_auxtrace *rvtrace)
{
	if (rvtrace->queues.new_data) {
		rvtrace->queues.new_data = false;
		return rvtrace_setup_queues(rvtrace);
	}

	return 0;
}

static int rvtrace_process_event(struct perf_session *session,
				 union perf_event *event,
				 struct perf_sample *sample,
				 const struct perf_tool *tool)
{
	int err = 0;
	u64 timestamp;
	struct rvtrace_auxtrace *rvtrace = container_of(session->auxtrace,
							struct rvtrace_auxtrace,
							auxtrace);

	if (dump_trace)
		return 0;

	if (!tool->ordered_events) {
		pr_err("RISCV Trace requires ordered events\n");
		return -EINVAL;
	}

	if (!rvtrace->timeless_decoding)
		return -EINVAL;

	if (sample->time && (sample->time != (u64) -1))
		timestamp = sample->time;
	else
		timestamp = 0;

	if (timestamp || rvtrace->timeless_decoding) {
		err = rvtrace_update_queues(rvtrace);
		if (err)
			return err;
	}

	if (event->header.type == PERF_RECORD_EXIT)
		return rvtrace_process_timeless_queues(rvtrace, event->fork.tid, sample->time);

	return 0;
}

static void rvtrace_dump(struct rvtrace_auxtrace *rvtrace,
			 unsigned char *buf, size_t len)
{
	int ret;
	struct nexus_rv_pkt_decoder_params params;
	struct nexus_rv_pkt_decoder *decoder;
	const char *color = PERF_COLOR_BLUE;

	// TODO: Determine whether the trace data contains a CoreSight formatter frame
	params.formatted = true;
	if (!rvtrace->metadata[0][RVTRACE_ENCODER_INHB_SRC])
		params.src_bits = rvtrace->metadata[0][RVTRACE_ENCODER_SRCBITS];

	decoder = nexus_rv_pkt_decoder_new(&params);
	if (!decoder) {
		color_fprintf(stdout, color, " Faild to create decoder\n");
		return;
	}

	color_fprintf(stdout, color,
			". ... Trace Encoder Trace data: size %#zx bytes\n",
			len);

	ret = nexus_rv_pkt_desc(decoder, buf, len);
	if (ret)
		color_fprintf(stdout, color, " Bad packet!\n");

	nexus_rv_pkt_decoder_free(decoder);
}

static void rvtrace_dump_event(struct rvtrace_auxtrace *rvtrace,
			       unsigned char *buf, size_t len)
{
	printf(".\n");
	rvtrace_dump(rvtrace, buf, len);
}

static int rvtrace_process_auxtrace_event(struct perf_session *session,
					  union perf_event *event,
					  const struct perf_tool *tool __maybe_unused)
{
	struct rvtrace_auxtrace *rvtrace = container_of(session->auxtrace,
								 struct rvtrace_auxtrace,
								 auxtrace);
	if (!rvtrace->data_queued) {
		struct auxtrace_buffer *buffer;
		off_t  data_offset;
		int fd = perf_data__fd(session->data);
		bool is_pipe = perf_data__is_pipe(session->data);
		int err;

		if (is_pipe)
			data_offset = 0;
		else {
			data_offset = lseek(fd, 0, SEEK_CUR);
			if (data_offset == -1)
				return -errno;
		}

		err = auxtrace_queues__add_event(&rvtrace->queues, session,
						 event, data_offset, &buffer);

		if (err)
			return err;

		if (dump_trace)
			if (auxtrace_buffer__get_data(buffer, fd)) {
				rvtrace_dump_event(rvtrace, buffer->data, buffer->size);
				auxtrace_buffer__put_data(buffer);
			}
	}


	return 0;
}

static bool rvtrace_is_timeless_decoding(struct rvtrace_auxtrace *rvtrace)
{
	struct evsel *evsel;
	struct evlist *evlist = rvtrace->session->evlist;
	bool timeless_decoding = true;

	/*
	 * Circle through the list of event and complain if we find one
	 * with the time bit set.
	 */
	evlist__for_each_entry(evlist, evsel) {
		if ((evsel->core.attr.sample_type & PERF_SAMPLE_TIME))
			timeless_decoding = false;
	}

	return timeless_decoding;
}

static const char * const rvtrace_global_header_fmts[] = {
	[RVTRACE_PMU_TYPE_CPUS]		    = "	    PMU type/num cpus	    %llx\n",
};

static const char * const rvtrace_encoder_priv_fmts[] = {
	[RVTRACE_ENCODER_CPU]		    = "	    CPU			    %lld\n",
	[RVTRACE_ENCODER_NR_TRC_PARAMS]	    = "	    NR_TRC_PARAMS	    %lld\n",
	[RVTRACE_ENCODER_FORMAT]	    = "	    FORMAT		    %lld\n",
	[RVTRACE_ENCODER_CONTEXT]	    = "	    CONTEXT		    %lld\n",
	[RVTRACE_ENCODER_INHB_SRC]	    = "	    INHB_SRC		    %lld\n",
	[RVTRACE_ENCODER_SRCBITS]	    = "	    SRCBITS		    %lld\n",
	[RVTRACE_ENCODER_SRCID]		    = "	    SRCID		    %lld\n",
};

static void rvtrace_print_auxtrace_info(u64 *val, int num_cpu)
{
	int i, j, cpu = 0, nr_params = 0, fmt_offset = 0;

	for (i = 0; i < RVTRACE_HEADER_MAX; i++)
		fprintf(stdout, rvtrace_global_header_fmts[i], val[i]);

	for (i = RVTRACE_HEADER_MAX; cpu < num_cpu; cpu++) {
		fprintf(stdout, rvtrace_encoder_priv_fmts[RVTRACE_ENCODER_CPU], val[i++]);
		nr_params = val[i++];
		fmt_offset = RVTRACE_ENCODER_FORMAT;
		for (j = fmt_offset; j < nr_params + fmt_offset; j++, i++)
			fprintf(stdout, rvtrace_encoder_priv_fmts[j], val[i]);
	}
}

int rvtrace_process_auxtrace_info(union perf_event *event,
				  struct perf_session *session)
{
	struct perf_record_auxtrace_info *auxtrace_info = &event->auxtrace_info;
	struct rvtrace_auxtrace *rvtrace = NULL;
	int err = 0;
	int i;
	int num_cpu = 0;
	u64 *ptr = NULL;
	u64 **metadata = NULL;

	/* First the global part */
	ptr = (u64 *) auxtrace_info->priv;
	num_cpu = ptr[RVTRACE_PMU_TYPE_CPUS] & 0xffffffff;
	metadata = zalloc(sizeof(*metadata) * num_cpu);
	if (!metadata)
		err = -ENOMEM;

	/* Start parsing after the common part of the header */
	i = RVTRACE_HEADER_MAX;

	for (int j = 0; j < num_cpu; j++) {
		metadata[j] = zalloc(sizeof(*metadata[j]) * RVTRACE_ENCODER_PRIV_MAX);
		if (!metadata[j]) {
			err = -ENOMEM;
			goto err_free_metadata;
		}

		for (int k = 0; k < RVTRACE_ENCODER_PRIV_MAX; k++)
			metadata[j][k] = ptr[i + k];
		i += RVTRACE_ENCODER_PRIV_MAX;
	}

	rvtrace = zalloc(sizeof(*rvtrace));
	if (!rvtrace) {
		err = -ENOMEM;
		goto err_free_metadata;
	}

	err = auxtrace_queues__init(&rvtrace->queues);
	if (err)
		goto err_free_rvtrace;

	if (session->itrace_synth_opts->set) {
		rvtrace->synth_opts = *session->itrace_synth_opts;
	} else {
		itrace_synth_opts__set_default(&rvtrace->synth_opts,
			session->itrace_synth_opts->default_no_sample);
		rvtrace->synth_opts.callchain = false;
	}

	rvtrace->session = session;
	rvtrace->machine = &session->machines.host;
	rvtrace->num_cpu = num_cpu;
	rvtrace->pmu_type = (unsigned int) ((ptr[RVTRACE_PMU_TYPE_CPUS] >> 32) & 0xffffffff);
	rvtrace->metadata = metadata;

	rvtrace->auxtrace_type = auxtrace_info->type;
	rvtrace->timeless_decoding = rvtrace_is_timeless_decoding(rvtrace);

	rvtrace->auxtrace.process_event = rvtrace_process_event;
	rvtrace->auxtrace.process_auxtrace_event = rvtrace_process_auxtrace_event;
	rvtrace->auxtrace.flush_events = rvtrace_flush_events;
	rvtrace->auxtrace.free_events = rvtrace_free_events;
	rvtrace->auxtrace.free = rvtrace_free;
	rvtrace->auxtrace.evsel_is_auxtrace = rvtrace_evsel_is_auxtrace;
	session->auxtrace = &rvtrace->auxtrace;

	if (dump_trace) {
		rvtrace_print_auxtrace_info(ptr, num_cpu);
		return 0;
	}

	err = rvtrace_synth_events(rvtrace, session);
	if (err)
		goto err_free_queues;

	err = auxtrace_queues__process_index(&rvtrace->queues, session);
	if (err)
		goto err_free_queues;

	rvtrace->data_queued = rvtrace->queues.populated;

	return 0;

err_free_queues:
	auxtrace_queues__free(&rvtrace->queues);
	session->auxtrace = NULL;
err_free_rvtrace:
	zfree(&rvtrace);
err_free_metadata:
	for (int j = 0; j < num_cpu; j++)
		zfree(&metadata[j]);
	zfree(&metadata);

	return -EINVAL;
}
