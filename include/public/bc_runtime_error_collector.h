// SPDX-License-Identifier: MIT

#ifndef BC_RUNTIME_ERROR_COLLECTOR_H
#define BC_RUNTIME_ERROR_COLLECTOR_H

#include "bc_allocators_context.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct bc_runtime_error_collector bc_runtime_error_collector_t;

bool bc_runtime_error_collector_create(bc_allocators_context_t* memory_context, bc_runtime_error_collector_t** out_collector);

void bc_runtime_error_collector_destroy(bc_allocators_context_t* memory_context, bc_runtime_error_collector_t* collector);

bool bc_runtime_error_collector_append(bc_runtime_error_collector_t* collector, bc_allocators_context_t* memory_context, const char* path,
                                       const char* stage, int errno_value);

size_t bc_runtime_error_collector_count(const bc_runtime_error_collector_t* collector);

void bc_runtime_error_collector_flush_to_stderr(const bc_runtime_error_collector_t* collector, const char* program_name);

#endif /* BC_RUNTIME_ERROR_COLLECTOR_H */
