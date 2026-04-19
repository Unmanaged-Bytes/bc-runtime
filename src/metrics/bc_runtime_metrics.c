// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include <stdatomic.h>

bool bc_runtime_get_metrics(const bc_runtime_t* application, bc_runtime_metrics_t* out_metrics)
{
    if (!bc_allocators_context_get_stats(application->memory_context, &out_metrics->memory_stats)) {
        return false;
    }

    out_metrics->parallel_thread_count = bc_concurrency_thread_count(application->parallel_context);

    out_metrics->state = application->state;
    out_metrics->log_messages_written = atomic_load_explicit(&application->log_messages_written, memory_order_relaxed);
    out_metrics->config_entries_count = application->config_store->entry_count;

    return true;
}
