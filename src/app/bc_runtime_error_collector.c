// SPDX-License-Identifier: MIT

#include "bc_runtime_error_collector.h"

#include "bc_allocators_pool.h"
#include "bc_allocators_typed_array.h"
#include "bc_core.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define BC_RUNTIME_ERROR_COLLECTOR_PATH_CAPACITY ((size_t)512)

typedef struct bc_runtime_error_record {
    char path[BC_RUNTIME_ERROR_COLLECTOR_PATH_CAPACITY];
    const char* stage;
    int errno_value;
} bc_runtime_error_record_t;

BC_TYPED_ARRAY_DEFINE(bc_runtime_error_record_t, bc_runtime_error_records)

struct bc_runtime_error_collector {
    bc_runtime_error_records_t records;
    atomic_size_t total_count;
    atomic_flag append_lock;
};

static void bc_runtime_error_collector_lock(bc_runtime_error_collector_t* collector)
{
    while (atomic_flag_test_and_set_explicit(&collector->append_lock, memory_order_acquire)) {
    }
}

static void bc_runtime_error_collector_unlock(bc_runtime_error_collector_t* collector)
{
    atomic_flag_clear_explicit(&collector->append_lock, memory_order_release);
}

bool bc_runtime_error_collector_create(bc_allocators_context_t* memory_context, bc_runtime_error_collector_t** out_collector)
{
    bc_runtime_error_collector_t* collector = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(*collector), (void**)&collector)) {
        return false;
    }
    bc_core_zero(collector, sizeof(*collector));
    atomic_store_explicit(&collector->total_count, 0, memory_order_relaxed);
    atomic_flag_clear_explicit(&collector->append_lock, memory_order_relaxed);
    *out_collector = collector;
    return true;
}

void bc_runtime_error_collector_destroy(bc_allocators_context_t* memory_context, bc_runtime_error_collector_t* collector)
{
    bc_runtime_error_records_destroy(memory_context, &collector->records);
    bc_allocators_pool_free(memory_context, collector);
}

bool bc_runtime_error_collector_append(bc_runtime_error_collector_t* collector, bc_allocators_context_t* memory_context, const char* path,
                                       const char* stage, int errno_value)
{
    bc_runtime_error_record_t record;
    bc_core_zero(&record, sizeof(record));
    size_t path_length = 0;
    if (!bc_core_length(path, '\0', &path_length)) {
        return false;
    }
    if (path_length >= sizeof(record.path)) {
        path_length = sizeof(record.path) - 1;
    }
    bc_core_copy(record.path, path, path_length);
    record.path[path_length] = '\0';
    record.stage = stage;
    record.errno_value = errno_value;

    bc_runtime_error_collector_lock(collector);
    bool push_ok = bc_runtime_error_records_push(memory_context, &collector->records, record);
    if (push_ok) {
        atomic_fetch_add_explicit(&collector->total_count, 1, memory_order_relaxed);
    }
    bc_runtime_error_collector_unlock(collector);
    return push_ok;
}

size_t bc_runtime_error_collector_count(const bc_runtime_error_collector_t* collector)
{
    return atomic_load_explicit(&collector->total_count, memory_order_relaxed);
}

void bc_runtime_error_collector_flush_to_stderr(const bc_runtime_error_collector_t* collector, const char* program_name)
{
    size_t record_count = bc_runtime_error_records_length(&collector->records);
    const bc_runtime_error_record_t* records = bc_runtime_error_records_data(&collector->records);
    const char* prefix = program_name != NULL ? program_name : "";
    for (size_t index = 0; index < record_count; index++) {
        const char* reason = records[index].errno_value != 0 ? strerror(records[index].errno_value) : "error";
        if (records[index].stage != NULL) {
            fprintf(stderr, "%s: %s: %s: %s\n", prefix, records[index].stage, records[index].path, reason);
        } else {
            fprintf(stderr, "%s: %s: %s\n", prefix, records[index].path, reason);
        }
    }
}
