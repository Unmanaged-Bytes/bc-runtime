// SPDX-License-Identifier: MIT

#include "bc_runtime.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TASK_COUNT ((size_t)1000000)
#define BATCH_SIZE ((size_t)1024)

static uint64_t timespec_to_ns(struct timespec t)
{
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static void noop_task(void* argument)
{
    (void)argument;
}

struct overhead_user_data {
    uint64_t elapsed_ns;
};

static bool overhead_run(const bc_runtime_t* application, void* user_data)
{
    struct overhead_user_data* data = user_data;

    bc_concurrency_context_t* parallel = NULL;
    bc_runtime_parallel_context(application, &parallel);

    void* arguments[BATCH_SIZE];
    for (size_t i = 0; i < BATCH_SIZE; i++) {
        arguments[i] = NULL;
    }

    /* warmup */
    for (size_t w = TASK_COUNT / 10; w > 0;) {
        size_t batch = w < BATCH_SIZE ? w : BATCH_SIZE;
        bc_concurrency_submit_batch(parallel, noop_task, (void* const*)arguments, batch);
        w -= batch;
    }
    bc_concurrency_dispatch_and_wait(parallel);

    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    size_t remaining = TASK_COUNT;
    while (remaining > 0) {
        size_t batch = remaining < BATCH_SIZE ? remaining : BATCH_SIZE;
        bc_concurrency_submit_batch(parallel, noop_task, (void* const*)arguments, batch);
        remaining -= batch;
    }
    bc_concurrency_dispatch_and_wait(parallel);

    clock_gettime(CLOCK_MONOTONIC, &end);
    data->elapsed_ns = timespec_to_ns(end) - timespec_to_ns(start);

    return true;
}

int main(void)
{
    printf("bench_application_overhead: %zu noop tasks via bc_runtime vs bare bc_concurrency\n\n", TASK_COUNT);

    /* Via bc_runtime */
    struct overhead_user_data app_data = {0};
    bc_runtime_config_t config = {
        .max_pool_memory = 0,
        .memory_tracking_enabled = false,
        .log_level = BC_RUNTIME_LOG_LEVEL_ERROR,
    };
    bc_runtime_callbacks_t callbacks = {.run = overhead_run};

    bc_runtime_t* app = NULL;
    bc_runtime_create(&config, &callbacks, &app_data, &app);
    bc_runtime_run(app);

    bc_concurrency_context_t* app_parallel = NULL;
    bc_runtime_parallel_context(app, &app_parallel);
    size_t app_thread_count = bc_concurrency_thread_count(app_parallel);

    bc_runtime_destroy(app);

    double app_tps = (double)TASK_COUNT / ((double)app_data.elapsed_ns / 1e9);
    uint64_t app_per_task = app_data.elapsed_ns / TASK_COUNT;

    /* Via bare bc_concurrency */
    bc_allocators_context_t* memory = NULL;
    bc_allocators_context_config_t mem_config = {.max_pool_memory = 0, .tracking_enabled = false};
    bc_allocators_context_create(&mem_config, &memory);

    bc_concurrency_context_t* parallel = NULL;
    bc_concurrency_create(memory, NULL, &parallel);

    size_t bare_thread_count = bc_concurrency_thread_count(parallel);

    void* arguments[BATCH_SIZE];
    for (size_t i = 0; i < BATCH_SIZE; i++) {
        arguments[i] = NULL;
    }

    for (size_t w = TASK_COUNT / 10; w > 0;) {
        size_t batch = w < BATCH_SIZE ? w : BATCH_SIZE;
        bc_concurrency_submit_batch(parallel, noop_task, (void* const*)arguments, batch);
        w -= batch;
    }
    bc_concurrency_dispatch_and_wait(parallel);

    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    size_t remaining = TASK_COUNT;
    while (remaining > 0) {
        size_t batch = remaining < BATCH_SIZE ? remaining : BATCH_SIZE;
        bc_concurrency_submit_batch(parallel, noop_task, (void* const*)arguments, batch);
        remaining -= batch;
    }
    bc_concurrency_dispatch_and_wait(parallel);

    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64_t bare_ns = timespec_to_ns(end) - timespec_to_ns(start);
    double bare_tps = (double)TASK_COUNT / ((double)bare_ns / 1e9);
    uint64_t bare_per_task = bare_ns / TASK_COUNT;

    bc_concurrency_destroy(parallel);
    bc_allocators_context_destroy(memory);

    printf("  bc_runtime (%zu threads):  %lu ns/task  %.0f tasks/sec\n", app_thread_count, (unsigned long)app_per_task, app_tps);
    printf("  bare bc_concurrency (%zu threads): %lu ns/task  %.0f tasks/sec\n", bare_thread_count, (unsigned long)bare_per_task, bare_tps);

    double overhead_pct = bare_tps > 0 ? (1.0 - app_tps / bare_tps) * 100.0 : 0.0;
    printf("  framework overhead: %.1f%%\n", overhead_pct);

    return 0;
}
