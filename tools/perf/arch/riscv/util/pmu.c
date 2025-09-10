#include <string.h>
#include <linux/perf_event.h>

#include "../../../util/pmu.h"

#define RVTRACE_PMU_NAME "rvtrace"

void perf_pmu__arch_init(struct perf_pmu *pmu)
{
#ifdef HAVE_AUXTRACE_SUPPORT
       if (!strcmp(pmu->name, RVTRACE_PMU_NAME)) {
               pmu->auxtrace = true;
               pmu->selectable = true;
       }
#endif
}

