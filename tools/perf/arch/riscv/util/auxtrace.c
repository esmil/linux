// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/zalloc.h>
#include <linux/string.h>
#include <time.h>

#include <internal/lib.h> // page_size
#include "../../../util/auxtrace.h"
#include "../../../util/cpumap.h"
#include "../../../util/debug.h"
#include "../../../util/event.h"
#include "../../../util/evlist.h"
#include "../../../util/evsel.h"
#include "../../../util/rvtrace.h"
#include "../../../util/pmu.h"
#include "../../../util/record.h"
#include "../../../util/session.h"
#include "../../../util/tsc.h"
#include "../../../util/evsel_config.h"

#define RVTRACE_PMU_NAME "rvtrace"
#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)

static const char *metadata_encoder_ro[] = {
	[RVTRACE_ENCODER_FORMAT]    = "control/format",
	[RVTRACE_ENCODER_CONTEXT]   = "control/context",
	[RVTRACE_ENCODER_INHB_SRC]  = "control/inhb_src",
	[RVTRACE_ENCODER_SRCBITS]   = "features/srcb",
	[RVTRACE_ENCODER_SRCID]	    = "features/srcid"
};

struct rvtrace_recording {
	struct auxtrace_record	itr;
	struct perf_pmu *rvtrace_pmu;
	struct evlist *evlist;
	bool snapshot_mode;
	size_t snapshot_size;
};

static int rvtrace_parse_snapshot_options(struct auxtrace_record *itr,
					  struct record_opts *opts,
					  const char *str)
{
	struct rvtrace_recording *ptr =
				container_of(itr, struct rvtrace_recording, itr);
	unsigned long long snapshot_size = 0;
	char *endptr;

	if (str) {
		snapshot_size = strtoull(str, &endptr, 0);
		if (*endptr || snapshot_size > SIZE_MAX)
			return -1;
	}

	opts->auxtrace_snapshot_mode = true;
	opts->auxtrace_snapshot_size = snapshot_size;
	ptr->snapshot_size = snapshot_size;

	return 0;
}

static size_t rvtrace_info_priv_size(struct auxtrace_record *itr __maybe_unused,
				     struct evlist *evlist __maybe_unused)
{
	int encoder;
	struct perf_cpu_map *event_cpus = evlist->core.user_requested_cpus;
	struct perf_cpu_map *intersect_cpus;

	if (!perf_cpu_map__has_any_cpu(event_cpus)) {
		/* cpu map is not "any" CPU , we have specific CPUs to work with */
		struct perf_cpu_map *online_cpus = perf_cpu_map__new_online_cpus();

		intersect_cpus = perf_cpu_map__intersect(event_cpus, online_cpus);
		perf_cpu_map__put(online_cpus);
	} else {
		/* Event can be "any" CPU so count all online CPUs. */
		intersect_cpus = perf_cpu_map__new_online_cpus();
	}

	encoder = perf_cpu_map__nr(intersect_cpus);
	perf_cpu_map__put(intersect_cpus);

	return (RVTRACE_HEADER_SIZE + encoder * RVTRACE_ENCODER_PRIV_SIZE);
}

static int rvtrace_get_ro(struct perf_pmu *pmu, struct perf_cpu cpu, const char *path, __u64 *val)
{
	char pmu_path[PATH_MAX];
	int scan;

	/* Get RO metadata from sysfs */
	snprintf(pmu_path, PATH_MAX, "cpu%d/%s", cpu.cpu, path);

	scan = perf_pmu__scan_file(pmu, pmu_path, "%llx", val);
	if (scan != 1) {
		pr_err("%s: error reading: %s\n", __func__, pmu_path);
		return -EINVAL;
	}

	return 0;
}

static void rvtrace_get_metadata(struct perf_cpu cpu, u32 *offset,
				 struct auxtrace_record *itr,
				 struct perf_record_auxtrace_info *info)
{
	struct rvtrace_recording *ptr = container_of(itr, struct rvtrace_recording, itr);
	struct perf_pmu *rvtrace_pmu = ptr->rvtrace_pmu;

	info->priv[*offset + RVTRACE_ENCODER_CPU] = cpu.cpu;
	info->priv[*offset + RVTRACE_ENCODER_NR_TRC_PARAMS] = RVTRACE_ENCODER_NR_TRC_PARAMS_LENGTH;

	/* Get read-only information from sysFS */
	rvtrace_get_ro(rvtrace_pmu, cpu, metadata_encoder_ro[RVTRACE_ENCODER_FORMAT],
			&info->priv[*offset + RVTRACE_ENCODER_FORMAT]);
	rvtrace_get_ro(rvtrace_pmu, cpu, metadata_encoder_ro[RVTRACE_ENCODER_CONTEXT],
			&info->priv[*offset + RVTRACE_ENCODER_CONTEXT]);
	rvtrace_get_ro(rvtrace_pmu, cpu, metadata_encoder_ro[RVTRACE_ENCODER_INHB_SRC],
			&info->priv[*offset + RVTRACE_ENCODER_INHB_SRC]);
	rvtrace_get_ro(rvtrace_pmu, cpu, metadata_encoder_ro[RVTRACE_ENCODER_SRCBITS],
			&info->priv[*offset + RVTRACE_ENCODER_SRCBITS]);
	rvtrace_get_ro(rvtrace_pmu, cpu, metadata_encoder_ro[RVTRACE_ENCODER_SRCID],
			&info->priv[*offset + RVTRACE_ENCODER_SRCID]);

	/* Where the next CPU entry should start from */
	*offset += RVTRACE_ENCODER_PRIV_MAX;
}

static int rvtrace_info_fill(struct auxtrace_record *itr, struct perf_session *session,
			     struct perf_record_auxtrace_info *auxtrace_info, size_t priv_size)
{
	int i;
	u32 offset;
	u64 nr_cpu, type;
	struct perf_cpu_map *cpu_map;
	struct perf_cpu_map *event_cpus = session->evlist->core.user_requested_cpus;
	struct perf_cpu_map *online_cpus = perf_cpu_map__new_online_cpus();
	struct rvtrace_recording *ptr = container_of(itr, struct rvtrace_recording, itr);
	struct perf_pmu *rvtrace_pmu = ptr->rvtrace_pmu;
	struct perf_cpu cpu;

	if (priv_size != rvtrace_info_priv_size(itr, session->evlist))
		return -EINVAL;

	if (!session->evlist->core.nr_mmaps)
		return -EINVAL;

	/* If the cpu_map has the "any" CPU all online CPUs are involved */
	if (perf_cpu_map__has_any_cpu(event_cpus)) {
		cpu_map = online_cpus;
	} else {
		/* Make sure all specified CPUs are online */
		perf_cpu_map__for_each_cpu(cpu, i, event_cpus) {
			if (!perf_cpu_map__has(online_cpus, cpu))
				return -EINVAL;
		}

		cpu_map = event_cpus;
	}

	nr_cpu = perf_cpu_map__nr(cpu_map);
	type = rvtrace_pmu->type;

	/* First fill out the session header */
	auxtrace_info->type = PERF_AUXTRACE_RISCV_TRACE;
	auxtrace_info->priv[RVTRACE_PMU_TYPE_CPUS] = type << 32;
	auxtrace_info->priv[RVTRACE_PMU_TYPE_CPUS] |= nr_cpu;

	offset = RVTRACE_HEADER_MAX;

	perf_cpu_map__for_each_cpu(cpu, i, cpu_map) {
		assert(offset < priv_size);
		rvtrace_get_metadata(cpu, &offset, itr, auxtrace_info);
	}

	perf_cpu_map__put(online_cpus);

	return 0;
}

static int rvtrace_set_sink_attr(struct perf_pmu *pmu,
				 struct evsel *evsel)
{
	char msg[BUFSIZ], path[PATH_MAX], *sink;
	struct evsel_config_term *term;
	int ret = -EINVAL;
	u32 hash;

	if (evsel->core.attr.config2 & GENMASK(31, 0))
		return 0;

	list_for_each_entry(term, &evsel->config_terms, list) {
		if (term->type != EVSEL__CONFIG_TERM_DRV_CFG)
			continue;

		sink = term->val.str;
		snprintf(path, PATH_MAX, "sinks/%s", sink);

		ret = perf_pmu__scan_file(pmu, path, "%x", &hash);
		if (ret != 1) {
			if (errno == ENOENT)
				pr_err("Couldn't find sink \"%s\" on event %s\n"
				       "Missing kernel or device support?\n\n"
				       "Hint: An appropriate sink will be picked automatically if one isn't specified.\n",
				       sink, evsel__name(evsel));
			else
				pr_err("Failed to set sink \"%s\" on event %s with %d (%s)\n",
				       sink, evsel__name(evsel), errno,
				       str_error_r(errno, msg, sizeof(msg)));
			return ret;
		}

		evsel->core.attr.config2 |= hash;
		return 0;
	}

	/*
	 * No sink was provided on the command line - allow the CoreSight
	 * system to look for a default
	 */
	return 0;
}

static int rvtrace_recording_options(struct auxtrace_record *itr, struct evlist *evlist,
				     struct record_opts *opts)
{
	struct rvtrace_recording *ptr = container_of(itr, struct rvtrace_recording, itr);
	struct perf_pmu *rvtrace_pmu = ptr->rvtrace_pmu;
	struct evsel *evsel, *rvtrace_evsel = NULL;
	struct perf_cpu_map *cpus = evlist->core.user_requested_cpus;
	bool privileged = perf_event_paranoid_check(-1);
	struct evsel *tracking_evsel;
	int err;

	ptr->evlist = evlist;
	ptr->snapshot_mode = opts->auxtrace_snapshot_mode;
	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.type == rvtrace_pmu->type) {
			if (rvtrace_evsel) {
				pr_err("There may be only one " RVTRACE_PMU_NAME "x event\n");
				return -EINVAL;
			}
			evsel->core.attr.freq = 0;
			evsel->core.attr.sample_period = 1;
			evsel->needs_auxtrace_mmap = true;
			rvtrace_evsel = evsel;
			opts->full_auxtrace = true;
		}
	}

	if (!opts->full_auxtrace)
		return 0;

	err = rvtrace_set_sink_attr(rvtrace_pmu, rvtrace_evsel);
	if (err)
		return err;

	/* we are in snapshot mode */
	if (opts->auxtrace_snapshot_mode) {
		/*
		 * No size were given to '-S' or '-m,', so go with
		 * the default
		 */
		if (!opts->auxtrace_snapshot_size && !opts->auxtrace_mmap_pages) {
			if (privileged) {
				opts->auxtrace_mmap_pages = MiB(4) / page_size;
			} else {
				opts->auxtrace_mmap_pages = KiB(128) / page_size;
				if (opts->mmap_pages == UINT_MAX)
					opts->mmap_pages = KiB(256) / page_size;
			}
		} else if (!opts->auxtrace_mmap_pages && !privileged &&
						opts->mmap_pages == UINT_MAX) {
			opts->mmap_pages = KiB(256) / page_size;
		}

		/*
		 * '-m,xyz' was specified but no snapshot size, so make the
		 * snapshot size as big as the auxtrace mmap area.
		 */
		if (!opts->auxtrace_snapshot_size) {
			opts->auxtrace_snapshot_size =
				opts->auxtrace_mmap_pages * (size_t)page_size;
		}

		/*
		 * -Sxyz was specified but no auxtrace mmap area, so make the
		 * auxtrace mmap area big enough to fit the requested snapshot
		 * size.
		 */
		if (!opts->auxtrace_mmap_pages) {
			size_t sz = opts->auxtrace_snapshot_size;

			sz = round_up(sz, page_size) / page_size;
			opts->auxtrace_mmap_pages = roundup_pow_of_two(sz);
		}

		/* Snapshot size can't be bigger than the auxtrace area */
		if (opts->auxtrace_snapshot_size >
				opts->auxtrace_mmap_pages * (size_t)page_size) {
			pr_err("Snapshot size %zu must not be greater than AUX area tracing mmap size %zu\n",
			       opts->auxtrace_snapshot_size,
			       opts->auxtrace_mmap_pages * (size_t)page_size);
			return -EINVAL;
		}

		/* Something went wrong somewhere - this shouldn't happen */
		if (!opts->auxtrace_snapshot_size || !opts->auxtrace_mmap_pages) {
			pr_err("Failed to calculate default snapshot size and/or AUX area tracing mmap pages\n");
			return -EINVAL;
		}

		pr_debug2("%s snapshot size: %zu\n", RVTRACE_PMU_NAME,
			  opts->auxtrace_snapshot_size);
	}

	/* Buffer sizes weren't specified with '-m,xyz' so give some defaults */
	if (!opts->auxtrace_mmap_pages) {
		if (privileged) {
			opts->auxtrace_mmap_pages = MiB(4) / page_size;
		} else {
			opts->auxtrace_mmap_pages = KiB(128) / page_size;
			if (opts->mmap_pages == UINT_MAX)
				opts->mmap_pages = KiB(256) / page_size;
		}
	}

	/* Validate auxtrace_mmap_pages */
	if (opts->auxtrace_mmap_pages) {
		size_t sz = opts->auxtrace_mmap_pages * (size_t)page_size;
		size_t min_sz;

		if (opts->auxtrace_snapshot_mode)
			min_sz = KiB(4);
		else
			min_sz = KiB(8);

		if (sz < min_sz || !is_power_of_2(sz)) {
			pr_err("Invalid mmap size for Intel Processor Trace: must be at least %zuKiB and a power of 2\n",
			       min_sz / 1024);
			return -EINVAL;
		}
	}

	/*
	 * To obtain the auxtrace buffer file descriptor, the auxtrace event
	 * must come first.
	 */
	evlist__to_front(evlist, rvtrace_evsel);

	/*
	 * get the CPU on the sample - need it to associate trace ID in the
	 * AUX_OUTPUT_HW_ID event, and the AUX event for per-cpu mmaps.
	 */
	evsel__set_sample_bit(rvtrace_evsel, CPU);

	/* Add dummy event to keep tracking */
	err = parse_event(evlist, "dummy:u");
	if (err)
		return err;

	tracking_evsel = evlist__last(evlist);
	evlist__set_tracking_event(evlist, tracking_evsel);

	tracking_evsel->core.attr.freq = 0;
	tracking_evsel->core.attr.sample_period = 1;

	/* In per-cpu case, always need the time of mmap events etc */
	if (!perf_cpu_map__is_any_cpu_or_is_empty(cpus))
		evsel__set_sample_bit(tracking_evsel, TIME);

	return 0;
}

static int rvtrace_snapshot_start(struct auxtrace_record *itr)
{
	struct rvtrace_recording *ptr =
			container_of(itr, struct rvtrace_recording, itr);
	struct evsel *evsel;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel->core.attr.type == ptr->rvtrace_pmu->type)
			return evsel__disable(evsel);
	}
	return -EINVAL;
}

static int rvtrace_snapshot_finish(struct auxtrace_record *itr)
{
	struct rvtrace_recording *ptr =
			container_of(itr, struct rvtrace_recording, itr);
	struct evsel *evsel;

	evlist__for_each_entry(ptr->evlist, evsel) {
		if (evsel->core.attr.type == ptr->rvtrace_pmu->type)
			return evsel__enable(evsel);
	}
	return -EINVAL;
}

static u64 rvtrace_reference(struct auxtrace_record *itr __maybe_unused)
{
	return rdtsc();
}

static void rvtrace_recording_free(struct auxtrace_record *itr)
{
	struct rvtrace_recording *ptr =
			container_of(itr, struct rvtrace_recording, itr);

	free(ptr);
}

static struct auxtrace_record *rvtrace_recording_init(int *err, struct perf_pmu *rvtrace_pmu)
{
	struct rvtrace_recording *ptr;

	if (!rvtrace_pmu) {
		*err = -ENODEV;
		return NULL;
	}

	ptr = zalloc(sizeof(*ptr));
	if (!ptr) {
		*err = -ENOMEM;
		return NULL;
	}

	ptr->rvtrace_pmu = rvtrace_pmu;
	ptr->itr.parse_snapshot_options = rvtrace_parse_snapshot_options;
	ptr->itr.recording_options = rvtrace_recording_options;
	ptr->itr.info_priv_size = rvtrace_info_priv_size;
	ptr->itr.info_fill = rvtrace_info_fill;
	ptr->itr.snapshot_start = rvtrace_snapshot_start;
	ptr->itr.snapshot_finish = rvtrace_snapshot_finish;
	ptr->itr.free = rvtrace_recording_free;
	ptr->itr.reference = rvtrace_reference;
	ptr->itr.read_finish = auxtrace_record__read_finish;
	ptr->itr.alignment = 0;

	*err = 0;
	return &ptr->itr;
}

static struct perf_pmu *find_pmu_for_event(struct perf_pmu **pmus,
					   int pmu_nr, struct evsel *evsel)
{
	int i;

	if (!pmus)
		return NULL;

	for (i = 0; i < pmu_nr; i++) {
		if (evsel->core.attr.type == pmus[i]->type)
			return pmus[i];
	}

	return NULL;
}

struct auxtrace_record *auxtrace_record__init(struct evlist *evlist, int *err)
{
	struct perf_pmu	*rvtrace_pmu = NULL;
	struct perf_pmu *found_rvtrace = NULL;
	struct evsel *evsel;

	if (!evlist)
		return NULL;

	rvtrace_pmu = perf_pmus__find(RVTRACE_PMU_NAME);
	evlist__for_each_entry(evlist, evsel) {
		if (rvtrace_pmu && !found_rvtrace)
			found_rvtrace = find_pmu_for_event(&rvtrace_pmu, 1, evsel);
	}

	if (found_rvtrace)
		return rvtrace_recording_init(err, rvtrace_pmu);

	*err = 0;
	return NULL;
}
