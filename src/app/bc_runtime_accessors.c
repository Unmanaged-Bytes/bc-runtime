// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include "bc_concurrency_signal.h"

bool bc_runtime_memory_context(const bc_runtime_t* application, bc_allocators_context_t** out_memory_context)
{
    *out_memory_context = application->memory_context;
    return true;
}

bool bc_runtime_parallel_context(const bc_runtime_t* application, bc_concurrency_context_t** out_parallel_context)
{
    *out_parallel_context = application->parallel_context;
    return true;
}

bool bc_runtime_should_stop(const bc_runtime_t* application, bool* out_should_stop)
{
    return bc_concurrency_signal_handler_should_stop(application->signal_handler, out_should_stop);
}

bool bc_runtime_signal_handler(const bc_runtime_t* application, bc_concurrency_signal_handler_t** out_signal_handler)
{
    *out_signal_handler = application->signal_handler;
    return true;
}

bool bc_runtime_current_state(const bc_runtime_t* application, bc_runtime_state_t* out_state)
{
    *out_state = application->state;
    return true;
}
