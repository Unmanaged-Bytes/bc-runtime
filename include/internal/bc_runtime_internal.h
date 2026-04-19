// SPDX-License-Identifier: MIT
#ifndef BC_RUNTIME_INTERNAL_H
#define BC_RUNTIME_INTERNAL_H

#include "bc_runtime.h"
#include "bc_allocators_arena.h"
#include "bc_concurrency_signal.h"

#include <stdatomic.h>

typedef struct bc_runtime_config_entry {
    const char* key;
    size_t key_length;
    const char* value;
} bc_runtime_config_entry_t;

typedef struct bc_runtime_config_store {
    bc_allocators_context_t* memory_context;
    bc_allocators_arena_t* arena;
    bc_runtime_config_entry_t* entries;
    size_t entry_count;
    size_t entry_capacity;
    bool sorted;
} bc_runtime_config_store_t;

struct bc_runtime {
    bc_allocators_context_t* memory_context;
    bc_concurrency_signal_handler_t* signal_handler;
    bc_concurrency_context_t* parallel_context;
    bc_runtime_config_store_t* config_store;
    _Atomic int log_level;
    bc_runtime_state_t state;
    bc_runtime_callbacks_t callbacks;
    void* user_data;
    _Atomic size_t log_messages_written;
};

struct bc_runtime_log_buffer {
    bc_allocators_context_t* memory_context;
    char* data;
    size_t capacity;
    size_t write_position;
    size_t entry_count;
    size_t overflow_count;
    _Atomic int log_level;
};

#define BC_RUNTIME_CONFIG_STORE_INITIAL_CAPACITY 1024
#define BC_RUNTIME_CONFIG_STORE_ARENA_CAPACITY 32768
#define BC_RUNTIME_LOG_BUFFER_STACK_SIZE 4096

bool bc_runtime_config_store_create(bc_allocators_context_t* memory_context, bc_runtime_config_store_t** out_store);

void bc_runtime_config_store_destroy(bc_allocators_context_t* memory_context, bc_runtime_config_store_t* store);

bool bc_runtime_config_store_set(bc_runtime_config_store_t* store, const char* key, const char* value);

bool bc_runtime_config_store_append(bc_runtime_config_store_t* store, const char* key, const char* value, char separator);

bool bc_runtime_config_store_sort(bc_runtime_config_store_t* store);

bool bc_runtime_config_store_lookup(const bc_runtime_config_store_t* store, const char* key, const char** out_value);

bool bc_runtime_config_load_file(bc_runtime_config_store_t* store, bc_allocators_context_t* memory_context, const char* file_path);

bool bc_runtime_config_load_environment(bc_runtime_config_store_t* store);

bool bc_runtime_config_load_arguments(bc_runtime_config_store_t* store, int argument_count, const char* const* argument_values);

bool bc_runtime_config_load_from_buffer(bc_runtime_config_store_t* store, const char* data, size_t size);

bool bc_runtime_log_format_timestamp(char* buffer, size_t buffer_size, size_t* out_length);

#endif // BC_RUNTIME_INTERNAL_H
