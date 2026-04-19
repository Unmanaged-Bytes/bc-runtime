// SPDX-License-Identifier: MIT

#include "bc_runtime.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define ITERATIONS ((size_t)10000)

static uint64_t timespec_to_ns(struct timespec t)
{
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

static bool noop_run(const bc_runtime_t* application, void* user_data)
{
    (void)application;
    (void)user_data;
    return true;
}

int main(void)
{
    printf("bench_application_lifecycle: create + run(noop) + destroy\n\n");

    bc_runtime_config_t config = {
        .max_pool_memory = 0,
        .memory_tracking_enabled = false,
        .log_level = BC_RUNTIME_LOG_LEVEL_ERROR,
    };

    bc_runtime_callbacks_t callbacks_noop = {.run = noop_run};
    bc_runtime_callbacks_t callbacks_empty = {0};

    /* Warmup */
    for (size_t i = 0; i < 100; i++) {
        bc_runtime_t* app = NULL;
        bc_runtime_create(&config, &callbacks_noop, NULL, &app);
        bc_runtime_run(app);
        bc_runtime_destroy(app);
    }

    /* Measure create + run(noop) + destroy */
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < ITERATIONS; i++) {
        bc_runtime_t* app = NULL;
        bc_runtime_create(&config, &callbacks_noop, NULL, &app);
        bc_runtime_run(app);
        bc_runtime_destroy(app);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64_t elapsed_ns = timespec_to_ns(end) - timespec_to_ns(start);
    uint64_t per_cycle_ns = elapsed_ns / ITERATIONS;
    printf("  create+run(noop)+destroy:  %lu ns/cycle  (%.0f cycles/sec)\n", (unsigned long)per_cycle_ns,
           (double)ITERATIONS / ((double)elapsed_ns / 1e9));

    /* Measure create + destroy (no run) */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < ITERATIONS; i++) {
        bc_runtime_t* app = NULL;
        bc_runtime_create(&config, &callbacks_empty, NULL, &app);
        bc_runtime_destroy(app);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    elapsed_ns = timespec_to_ns(end) - timespec_to_ns(start);
    per_cycle_ns = elapsed_ns / ITERATIONS;
    printf("  create+destroy (no run):   %lu ns/cycle  (%.0f cycles/sec)\n", (unsigned long)per_cycle_ns,
           (double)ITERATIONS / ((double)elapsed_ns / 1e9));

    return 0;
}
