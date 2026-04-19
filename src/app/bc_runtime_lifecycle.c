// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include "bc_core.h"
#include "bc_allocators_pool.h"

#include <signal.h>
#include <stdatomic.h>

bool bc_runtime_create(const bc_runtime_config_t* config, const bc_runtime_callbacks_t* callbacks, void* user_data,
                       bc_runtime_t** out_application)
{
    bc_allocators_context_config_t memory_config = {
        .max_pool_memory = config->max_pool_memory,
        .tracking_enabled = config->memory_tracking_enabled,
    };
    bc_allocators_context_t* memory_context = NULL;
    if (!bc_allocators_context_create(&memory_config, &memory_context)) {
        return false;
    }

    bc_runtime_t* application = NULL;

    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_runtime_t), (void**)&application)) {
        goto fail_memory_context;
    }

    bc_core_zero(application, sizeof(bc_runtime_t));
    application->memory_context = memory_context;

    if (!bc_concurrency_signal_handler_create(memory_context, &application->signal_handler)) {
        goto fail_application;
    }

    if (!bc_concurrency_signal_handler_install(application->signal_handler, SIGINT)) {
        goto fail_signal_handler;
    }
    if (!bc_concurrency_signal_handler_install(application->signal_handler, SIGTERM)) {
        goto fail_signal_handler;
    }

    if (!bc_runtime_config_store_create(memory_context, &application->config_store)) {
        goto fail_signal_handler;
    }

    if (config->config_file_path != NULL) {
        if (!bc_runtime_config_load_file(application->config_store, memory_context, config->config_file_path)) {
            goto fail_config_store;
        }
    }

    if (!bc_runtime_config_load_environment(application->config_store)) {
        goto fail_config_store;
    }

    if (config->argument_count > 0 && config->argument_values != NULL) {
        if (!bc_runtime_config_load_arguments(application->config_store, config->argument_count, config->argument_values)) {
            goto fail_config_store;
        }
    }

    if (!bc_runtime_config_store_sort(application->config_store)) {
        goto fail_config_store;
    }

    if (!bc_concurrency_create(memory_context, config->parallel_config, &application->parallel_context)) {
        goto fail_config_store;
    }

    application->callbacks = *callbacks;
    application->user_data = user_data;
    application->state = BC_RUNTIME_STATE_CREATED;
    atomic_store_explicit(&application->log_level, (int)config->log_level, memory_order_relaxed);
    atomic_store_explicit(&application->log_messages_written, 0, memory_order_relaxed);

    *out_application = application;
    return true;

fail_config_store:
    bc_runtime_config_store_destroy(memory_context, application->config_store);
fail_signal_handler:
    bc_concurrency_signal_handler_destroy(application->signal_handler);
fail_application:
    bc_allocators_pool_free(memory_context, application);
fail_memory_context:
    bc_allocators_context_destroy(memory_context);
    return false;
}

bool bc_runtime_run(bc_runtime_t* application)
{
    if (application->state != BC_RUNTIME_STATE_CREATED) {
        return false;
    }

    application->state = BC_RUNTIME_STATE_INITIALIZED;

    if (application->callbacks.init != NULL) {
        bool init_succeeded = application->callbacks.init(application, application->user_data);
        if (!init_succeeded) {
            application->state = BC_RUNTIME_STATE_STOPPING;
            if (application->callbacks.cleanup != NULL) {
                application->callbacks.cleanup(application, application->user_data);
            }
            application->state = BC_RUNTIME_STATE_STOPPED;
            return false;
        }
    }

    application->state = BC_RUNTIME_STATE_RUNNING;

    bool run_succeeded;
    if (application->callbacks.run != NULL) {
        run_succeeded = application->callbacks.run(application, application->user_data);
    } else {
        run_succeeded = true;
    }

    application->state = BC_RUNTIME_STATE_STOPPING;

    if (application->callbacks.cleanup != NULL) {
        application->callbacks.cleanup(application, application->user_data);
    }

    application->state = BC_RUNTIME_STATE_STOPPED;
    return run_succeeded;
}

void bc_runtime_destroy(bc_runtime_t* application)
{
    bc_allocators_context_t* memory_context = application->memory_context;

    if (application->parallel_context != NULL) {
        bc_concurrency_destroy(application->parallel_context);
    }

    if (application->signal_handler != NULL) {
        bc_concurrency_signal_handler_destroy(application->signal_handler);
    }

    if (application->config_store != NULL) {
        bc_runtime_config_store_destroy(memory_context, application->config_store);
    }

    bc_allocators_pool_free(memory_context, application);
    bc_allocators_context_destroy(memory_context);
}
