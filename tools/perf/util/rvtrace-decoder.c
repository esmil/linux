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

int rvtrace_process_auxtrace_info(union perf_event *event,
				  struct perf_session *session)
{
	struct perf_record_auxtrace_info *auxtrace_info = &event->auxtrace_info;
	struct rvtrace_auxtrace *rvtrace = NULL;
	int err = 0;

	rvtrace = zalloc(sizeof(*rvtrace));

	if (!rvtrace)
		err = -ENOMEM;

	err = auxtrace_queues__init(&rvtrace->queues);
	if (err)
		goto err_free_rvtrace;

	rvtrace->session = session;
	rvtrace->machine = &session->machines.host;

	rvtrace->auxtrace_type = auxtrace_info->type;
	rvtrace->timeless_decoding = rvtrace_is_timeless_decoding(rvtrace);

	rvtrace->auxtrace.process_event = rvtrace_process_event;
	rvtrace->auxtrace.process_auxtrace_event = rvtrace_process_auxtrace_event;
	rvtrace->auxtrace.flush_events = rvtrace_flush_events;
	rvtrace->auxtrace.free_events = rvtrace_free_events;
	rvtrace->auxtrace.free = rvtrace_free;
	rvtrace->auxtrace.evsel_is_auxtrace = rvtrace_evsel_is_auxtrace;
	session->auxtrace = &rvtrace->auxtrace;

	if (dump_trace)
		return 0;

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

	return -EINVAL;
}
