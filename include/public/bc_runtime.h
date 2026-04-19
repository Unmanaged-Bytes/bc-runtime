// SPDX-License-Identifier: MIT
#ifndef BC_RUNTIME_H
#define BC_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>

#include "bc_allocators.h"
#include "bc_concurrency.h"
#include "bc_concurrency_signal.h"

typedef struct bc_runtime bc_runtime_t;
typedef struct bc_runtime_log_buffer bc_runtime_log_buffer_t;
typedef struct bc_runtime_config_store bc_runtime_config_store_t;

typedef enum {
    BC_RUNTIME_STATE_CREATED,
    BC_RUNTIME_STATE_INITIALIZED,
    BC_RUNTIME_STATE_RUNNING,
    BC_RUNTIME_STATE_STOPPING,
    BC_RUNTIME_STATE_STOPPED,
} bc_runtime_state_t;

typedef enum {
    BC_RUNTIME_LOG_LEVEL_ERROR,
    BC_RUNTIME_LOG_LEVEL_WARN,
    BC_RUNTIME_LOG_LEVEL_INFO,
    BC_RUNTIME_LOG_LEVEL_DEBUG,
} bc_runtime_log_level_t;

typedef bool (*bc_runtime_init_callback_t)(const bc_runtime_t* application, void* user_data);
typedef bool (*bc_runtime_run_callback_t)(const bc_runtime_t* application, void* user_data);
typedef void (*bc_runtime_cleanup_callback_t)(const bc_runtime_t* application, void* user_data);

typedef struct bc_runtime_callbacks {
    bc_runtime_init_callback_t init;
    bc_runtime_cleanup_callback_t cleanup;
    bc_runtime_run_callback_t run;
} bc_runtime_callbacks_t;

typedef struct bc_runtime_config {
    size_t max_pool_memory;
    bool memory_tracking_enabled;
    bc_runtime_log_level_t log_level;
    const char* config_file_path;
    int argument_count;
    const char* const* argument_values;
    const bc_concurrency_config_t* parallel_config;
} bc_runtime_config_t;

typedef struct bc_runtime_metrics {
    bc_allocators_stats_t memory_stats;
    size_t parallel_thread_count;
    bc_runtime_state_t state;
    size_t log_messages_written;
    size_t config_entries_count;
} bc_runtime_metrics_t;

bool bc_runtime_create(const bc_runtime_config_t* config, const bc_runtime_callbacks_t* callbacks, void* user_data,
                       bc_runtime_t** out_application);

bool bc_runtime_run(bc_runtime_t* application);

void bc_runtime_destroy(bc_runtime_t* application);

bool bc_runtime_memory_context(const bc_runtime_t* application, bc_allocators_context_t** out_memory_context);

bool bc_runtime_parallel_context(const bc_runtime_t* application, bc_concurrency_context_t** out_parallel_context);

bool bc_runtime_should_stop(const bc_runtime_t* application, bool* out_should_stop);

bool bc_runtime_signal_handler(const bc_runtime_t* application, bc_concurrency_signal_handler_t** out_signal_handler);

bool bc_runtime_current_state(const bc_runtime_t* application, bc_runtime_state_t* out_state);

bool bc_runtime_config_get_string(const bc_runtime_t* application, const char* key, const char** out_value);

bool bc_runtime_config_get_integer(const bc_runtime_t* application, const char* key, long* out_value);

bool bc_runtime_config_get_boolean(const bc_runtime_t* application, const char* key, bool* out_value);

bool bc_runtime_config_store_create(bc_allocators_context_t* memory_context, bc_runtime_config_store_t** out_store);

void bc_runtime_config_store_destroy(bc_allocators_context_t* memory_context, bc_runtime_config_store_t* store);

bool bc_runtime_config_store_get_string(const bc_runtime_config_store_t* store, const char* key, const char** out_value);

bool bc_runtime_config_store_get_integer(const bc_runtime_config_store_t* store, const char* key, long* out_value);

bool bc_runtime_config_store_get_boolean(const bc_runtime_config_store_t* store, const char* key, bool* out_value);

bool bc_runtime_log(const bc_runtime_t* application, bc_runtime_log_level_t level, const char* message);

bool bc_runtime_log_set_level(bc_runtime_t* application, bc_runtime_log_level_t level);

bool bc_runtime_log_buffer_create(const bc_runtime_t* application, size_t capacity, bc_runtime_log_buffer_t** out_buffer);

bool bc_runtime_log_to_buffer(bc_runtime_log_buffer_t* buffer, bc_runtime_log_level_t level, const char* message);

bool bc_runtime_log_drain(const bc_runtime_t* application, bc_runtime_log_buffer_t* const* buffers, size_t buffer_count);

void bc_runtime_log_buffer_destroy(bc_runtime_log_buffer_t* buffer);

bool bc_runtime_get_metrics(const bc_runtime_t* application, bc_runtime_metrics_t* out_metrics);

#endif // BC_RUNTIME_H
