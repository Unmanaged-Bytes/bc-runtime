// SPDX-License-Identifier: MIT

#include "bc_runtime.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define LOG_ITERATIONS ((size_t)100000)
#define BUFFER_CAPACITY ((size_t)(4 * 1024 * 1024))

static uint64_t timespec_to_ns(struct timespec t)
{
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

struct log_bench_data {
    uint64_t direct_ns;
    uint64_t buffer_ns;
    uint64_t drain_ns;
    uint64_t suppressed_ns;
};

static bool log_bench_run(const bc_runtime_t* application, void* user_data)
{
    struct log_bench_data* data = user_data;
    struct timespec start;
    struct timespec end;

    /* Redirect stderr to /dev/null for direct log benchmark */
    int saved_stderr = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDERR_FILENO);
    close(devnull);

    /* Benchmark 1: direct bc_runtime_log (writes to stderr) */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < LOG_ITERATIONS; i++) {
        bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_INFO, "benchmark log message for throughput measurement");
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    data->direct_ns = timespec_to_ns(end) - timespec_to_ns(start);

    /* Restore stderr */
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);

    /* Benchmark 2: log_to_buffer (no syscall) */
    bc_runtime_log_buffer_t* buffer = NULL;
    bc_runtime_log_buffer_create(application, BUFFER_CAPACITY, &buffer);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < LOG_ITERATIONS; i++) {
        bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_INFO, "benchmark log message for throughput measurement");
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    data->buffer_ns = timespec_to_ns(end) - timespec_to_ns(start);

    /* Benchmark 3: drain (single write syscall for all buffered messages) */
    saved_stderr = dup(STDERR_FILENO);
    devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDERR_FILENO);
    close(devnull);

    bc_runtime_log_buffer_t* const buffers[] = {buffer};
    clock_gettime(CLOCK_MONOTONIC, &start);
    bc_runtime_log_drain(application, buffers, 1);
    clock_gettime(CLOCK_MONOTONIC, &end);
    data->drain_ns = timespec_to_ns(end) - timespec_to_ns(start);

    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);

    bc_runtime_log_buffer_destroy(buffer);

    /* Benchmark 4: suppressed by level (log_level = ERROR, writing INFO) */
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t i = 0; i < LOG_ITERATIONS; i++) {
        bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_DEBUG, "suppressed message");
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    data->suppressed_ns = timespec_to_ns(end) - timespec_to_ns(start);

    return true;
}

int main(void)
{
    printf("bench_application_logging: %zu messages\n\n", LOG_ITERATIONS);

    struct log_bench_data data = {0};
    bc_runtime_config_t config = {
        .max_pool_memory = 0,
        .memory_tracking_enabled = false,
        .log_level = BC_RUNTIME_LOG_LEVEL_INFO,
    };
    bc_runtime_callbacks_t callbacks = {.run = log_bench_run};

    bc_runtime_t* app = NULL;
    bc_runtime_create(&config, &callbacks, &data, &app);
    bc_runtime_run(app);
    bc_runtime_destroy(app);

    printf("  direct log (write syscall):    %lu ns/msg  (%.0f msg/sec)\n", (unsigned long)(data.direct_ns / LOG_ITERATIONS),
           (double)LOG_ITERATIONS / ((double)data.direct_ns / 1e9));

    printf("  log_to_buffer (no syscall):    %lu ns/msg  (%.0f msg/sec)\n", (unsigned long)(data.buffer_ns / LOG_ITERATIONS),
           (double)LOG_ITERATIONS / ((double)data.buffer_ns / 1e9));

    printf("  drain %zu buffered msgs:       %lu ns total  (%.0f msg/sec effective)\n", LOG_ITERATIONS, (unsigned long)data.drain_ns,
           (double)LOG_ITERATIONS / ((double)(data.buffer_ns + data.drain_ns) / 1e9));

    printf("  suppressed (level filter):     %lu ns/msg  (%.0f msg/sec)\n", (unsigned long)(data.suppressed_ns / LOG_ITERATIONS),
           (double)LOG_ITERATIONS / ((double)data.suppressed_ns / 1e9));

    return 0;
}
