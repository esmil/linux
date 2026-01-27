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
#include "rvtrace.h"

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
	u8 sample_branches;

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
	struct auxtrace_buffer *buffer;
	union perf_event *event_buf;
	unsigned int queue_nr;
	pid_t pid, tid;
	int cpu;
	u64 time;
	u64 timestamp;
	u64 offset;
};

static int rvtrace_flush_events(struct perf_session *session __maybe_unused,
				const struct perf_tool *tool __maybe_unused)
{
	return 0;
}

static void rvtrace_free_queue(void *priv)
{
	struct rvtrace_queue *rvtraceq = priv;

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

static int rvtrace_process_event(struct perf_session *session __maybe_unused,
				 union perf_event *event __maybe_unused,
				 struct perf_sample *sample __maybe_unused,
				 const struct perf_tool *tool __maybe_unused)
{
	return 0;
}

static int rvtrace_process_auxtrace_event(struct perf_session *session __maybe_unused,
					  union perf_event *event __maybe_unused,
					  const struct perf_tool *tool __maybe_unused)
{
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

	rvtrace->session = session;
	rvtrace->machine = &session->machines.host;
	rvtrace->num_cpu = num_cpu;
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
