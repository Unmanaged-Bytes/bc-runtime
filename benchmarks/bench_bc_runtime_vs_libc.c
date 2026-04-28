// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_runtime.h"
#include "bc_runtime_cli.h"
#include "bc_runtime_error_collector.h"

#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t median_u64(uint64_t* values, size_t count)
{
    for (size_t i = 1; i < count; i++) {
        uint64_t key = values[i];
        size_t j = i;
        while (j > 0 && values[j - 1] > key) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = key;
    }
    return values[count / 2];
}

static void use_ptr(void* p)
{
    __asm__ volatile("" : "+r"(p));
}

static const char* const algo_values[] = {"crc32", "sha256", "xxh3", NULL};
static const char* const format_values[] = {"simple", "table", "json", NULL};

static const bc_runtime_cli_option_spec_t bench_global_options[] = {
    {
        .long_name = "threads",
        .type = BC_RUNTIME_CLI_OPTION_INTEGER,
        .default_value = "0",
        .value_placeholder = "N",
        .help_summary = "worker count",
    },
    {
        .long_name = "verbose",
        .type = BC_RUNTIME_CLI_OPTION_FLAG,
        .help_summary = "enable verbose logging",
    },
    {
        .long_name = "quiet",
        .type = BC_RUNTIME_CLI_OPTION_FLAG,
        .help_summary = "suppress non-error output",
    },
    {
        .long_name = "config",
        .type = BC_RUNTIME_CLI_OPTION_STRING,
        .value_placeholder = "PATH",
        .help_summary = "config file",
    },
};

static const bc_runtime_cli_option_spec_t bench_run_options[] = {
    {
        .long_name = "type",
        .type = BC_RUNTIME_CLI_OPTION_ENUM,
        .allowed_values = algo_values,
        .default_value = "sha256",
        .value_placeholder = "ALGO",
        .help_summary = "hash algorithm",
    },
    {
        .long_name = "format",
        .type = BC_RUNTIME_CLI_OPTION_ENUM,
        .allowed_values = format_values,
        .default_value = "simple",
        .value_placeholder = "FMT",
        .help_summary = "output format",
    },
    {
        .long_name = "force",
        .type = BC_RUNTIME_CLI_OPTION_BOOLEAN,
        .default_value = "false",
        .help_summary = "force overwrite",
    },
    {
        .long_name = "exclude",
        .type = BC_RUNTIME_CLI_OPTION_LIST,
        .value_placeholder = "GLOB",
        .help_summary = "exclude pattern",
    },
    {
        .long_name = "depth",
        .type = BC_RUNTIME_CLI_OPTION_INTEGER,
        .default_value = "0",
        .value_placeholder = "N",
        .help_summary = "max depth",
    },
};

static const bc_runtime_cli_command_spec_t bench_commands[] = {
    {
        .name = "run",
        .summary = "run benchmark",
        .options = bench_run_options,
        .option_count = sizeof(bench_run_options) / sizeof(bench_run_options[0]),
        .positional_usage = "<path>...",
        .positional_min = 0,
        .positional_max = 0,
    },
};

static const bc_runtime_cli_program_spec_t bench_program_spec = {
    .program_name = "bench",
    .version = "1.0.0",
    .summary = "bench harness",
    .global_options = bench_global_options,
    .global_option_count = sizeof(bench_global_options) / sizeof(bench_global_options[0]),
    .commands = bench_commands,
    .command_count = sizeof(bench_commands) / sizeof(bench_commands[0]),
};

static size_t build_argv_5(const char** argv, char* scratch, size_t scratch_size)
{
    (void)scratch;
    (void)scratch_size;
    argv[0] = "bench";
    argv[1] = "--threads=8";
    argv[2] = "run";
    argv[3] = "--type=sha256";
    argv[4] = "/tmp/path";
    return 5;
}

static size_t build_argv_20(const char** argv, char* scratch, size_t scratch_size)
{
    (void)scratch;
    (void)scratch_size;
    argv[0] = "bench";
    argv[1] = "--threads=8";
    argv[2] = "--verbose";
    argv[3] = "--config=/etc/bench.conf";
    argv[4] = "run";
    argv[5] = "--type=sha256";
    argv[6] = "--format=json";
    argv[7] = "--force=true";
    argv[8] = "--depth=4";
    argv[9] = "--exclude=*.tmp";
    argv[10] = "--exclude=*.bak";
    argv[11] = "--exclude=*.log";
    argv[12] = "/tmp/p1";
    argv[13] = "/tmp/p2";
    argv[14] = "/tmp/p3";
    argv[15] = "/tmp/p4";
    argv[16] = "/tmp/p5";
    argv[17] = "/tmp/p6";
    argv[18] = "/tmp/p7";
    argv[19] = "/tmp/p8";
    return 20;
}

static size_t build_argv_100(const char** argv, char* scratch, size_t scratch_size)
{
    size_t cursor = 0;
    argv[0] = "bench";
    argv[1] = "--threads=16";
    argv[2] = "--verbose";
    argv[3] = "--quiet";
    argv[4] = "--config=/etc/bench.conf";
    argv[5] = "run";
    argv[6] = "--type=xxh3";
    argv[7] = "--format=table";
    argv[8] = "--force=true";
    argv[9] = "--depth=8";
    size_t idx = 10;
    for (size_t i = 0; i < 20; i++) {
        int written = snprintf(scratch + cursor, scratch_size - cursor, "--exclude=pat_%zu_xyz", i);
        argv[idx++] = scratch + cursor;
        cursor += (size_t)written + 1;
    }
    for (size_t i = 0; i < 70; i++) {
        int written = snprintf(scratch + cursor, scratch_size - cursor, "/data/bench/path_%04zu", i);
        argv[idx++] = scratch + cursor;
        cursor += (size_t)written + 1;
    }
    return idx;
}

typedef size_t (*build_argv_fn_t)(const char** argv, char* scratch, size_t scratch_size);

typedef struct {
    const char* label;
    build_argv_fn_t build;
} argv_workload_t;

static double bench_bc_cli_parse(bc_allocators_context_t* memory, const char** argv, size_t argc, size_t reps)
{
    uint64_t samples[5];
    for (size_t s = 0; s < 5; s++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) {
            bc_runtime_config_store_t* store = NULL;
            bc_runtime_config_store_create(memory, &store);
            FILE* sink = fopen("/dev/null", "w");
            bc_runtime_cli_parsed_t parsed;
            bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&bench_program_spec, (int)argc, argv, store, &parsed, sink);
            use_ptr(&status);
            fclose(sink);
            bc_runtime_config_store_destroy(memory, store);
        }
        samples[s] = now_ns() - t0;
    }
    uint64_t med = median_u64(samples, 5);
    return (double)med / (double)reps;
}

static const struct option getopt_long_opts[] = {
    {"threads", required_argument, 0, 't'}, {"verbose", no_argument, 0, 'v'},
    {"quiet", no_argument, 0, 'q'},         {"config", required_argument, 0, 'c'},
    {"type", required_argument, 0, 'T'},    {"format", required_argument, 0, 'F'},
    {"force", required_argument, 0, 'f'},   {"exclude", required_argument, 0, 'x'},
    {"depth", required_argument, 0, 'd'},   {0, 0, 0, 0},
};

static double bench_getopt_long_parse(const char** argv, size_t argc, size_t reps)
{
    uint64_t samples[5];
    for (size_t s = 0; s < 5; s++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) {
            optind = 1;
            opterr = 0;
            int seen_threads = 0;
            int seen_verbose = 0;
            int seen_quiet = 0;
            int seen_config = 0;
            int seen_type = 0;
            int seen_format = 0;
            int seen_force = 0;
            int seen_depth = 0;
            int n_excludes = 0;
            for (;;) {
                int idx = 0;
                int c = getopt_long((int)argc, (char* const*)argv, "", getopt_long_opts, &idx);
                if (c == -1) {
                    break;
                }
                switch (c) {
                case 't':
                    seen_threads++;
                    use_ptr((void*)optarg);
                    break;
                case 'v':
                    seen_verbose++;
                    break;
                case 'q':
                    seen_quiet++;
                    break;
                case 'c':
                    seen_config++;
                    use_ptr((void*)optarg);
                    break;
                case 'T':
                    seen_type++;
                    use_ptr((void*)optarg);
                    break;
                case 'F':
                    seen_format++;
                    use_ptr((void*)optarg);
                    break;
                case 'f':
                    seen_force++;
                    use_ptr((void*)optarg);
                    break;
                case 'x':
                    n_excludes++;
                    use_ptr((void*)optarg);
                    break;
                case 'd':
                    seen_depth++;
                    use_ptr((void*)optarg);
                    break;
                default:
                    break;
                }
            }
            int positional_count = (int)argc - optind;
            use_ptr(&seen_threads);
            use_ptr(&seen_verbose);
            use_ptr(&seen_quiet);
            use_ptr(&seen_config);
            use_ptr(&seen_type);
            use_ptr(&seen_format);
            use_ptr(&seen_force);
            use_ptr(&seen_depth);
            use_ptr(&n_excludes);
            use_ptr(&positional_count);
        }
        samples[s] = now_ns() - t0;
    }
    uint64_t med = median_u64(samples, 5);
    return (double)med / (double)reps;
}

static void run_cli_workloads(bc_allocators_context_t* memory)
{
    static const argv_workload_t workloads[] = {
        {"argc=5", build_argv_5},
        {"argc=20", build_argv_20},
        {"argc=100", build_argv_100},
    };
    static const size_t reps_per_workload[] = {200000, 100000, 20000};

    printf("--- cli parse: bc_runtime_cli_parse vs getopt_long ---\n");
    for (size_t w = 0; w < 3; w++) {
        const char* argv[128];
        char scratch[8192];
        size_t argc = workloads[w].build(argv, scratch, sizeof(scratch));
        size_t reps = reps_per_workload[w];

        double bc_ns = bench_bc_cli_parse(memory, argv, argc, reps);
        double gnu_ns = bench_getopt_long_parse(argv, argc, reps);

        printf("  %-9s  bc_runtime=%9.0f ns/parse  getopt_long=%9.0f ns/parse  ratio=%.2fx\n", workloads[w].label, bc_ns, gnu_ns,
               gnu_ns / bc_ns);
    }
}

typedef struct {
    size_t iterations;
    uint64_t bc_static_ns;
    uint64_t bc_buffered_ns;
    uint64_t bc_buffered_drain_ns;
    uint64_t fprintf_static_ns;
    uint64_t bc_suppressed_ns;
    uint64_t fprintf_suppressed_ns;
} log_results_t;

static int redirect_stderr_to_devnull(void)
{
    int saved = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
    return saved;
}

static void restore_stderr(int saved)
{
    dup2(saved, STDERR_FILENO);
    close(saved);
}

static bool log_run(const bc_runtime_t* application, void* user_data)
{
    log_results_t* results = (log_results_t*)user_data;
    size_t n = results->iterations;

    int saved = redirect_stderr_to_devnull();

    uint64_t t0 = now_ns();
    for (size_t i = 0; i < n; i++) {
        bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_INFO, "benchmark log message for throughput measurement");
    }
    results->bc_static_ns = now_ns() - t0;

    t0 = now_ns();
    for (size_t i = 0; i < n; i++) {
        fprintf(stderr, "%s\n", "benchmark log message for throughput measurement");
    }
    fflush(stderr);
    results->fprintf_static_ns = now_ns() - t0;

    bc_runtime_log_buffer_t* buffer = NULL;
    bc_runtime_log_buffer_create(application, 8 * 1024 * 1024, &buffer);
    t0 = now_ns();
    for (size_t i = 0; i < n; i++) {
        bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_INFO, "benchmark log message for throughput measurement");
    }
    results->bc_buffered_ns = now_ns() - t0;

    bc_runtime_log_buffer_t* const buffers[] = {buffer};
    t0 = now_ns();
    bc_runtime_log_drain(application, buffers, 1);
    results->bc_buffered_drain_ns = now_ns() - t0;
    bc_runtime_log_buffer_destroy(buffer);

    t0 = now_ns();
    for (size_t i = 0; i < n; i++) {
        bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_DEBUG, "suppressed message");
    }
    results->bc_suppressed_ns = now_ns() - t0;

    int suppressed_level = BC_RUNTIME_LOG_LEVEL_INFO;
    int debug_level = BC_RUNTIME_LOG_LEVEL_DEBUG;
    t0 = now_ns();
    for (size_t i = 0; i < n; i++) {
        if (debug_level <= suppressed_level) {
            fprintf(stderr, "%s\n", "suppressed message");
        }
    }
    results->fprintf_suppressed_ns = now_ns() - t0;

    restore_stderr(saved);
    return true;
}

static void run_log_workload(size_t iterations)
{
    log_results_t results = {.iterations = iterations};
    bc_runtime_config_t config = {
        .max_pool_memory = 0,
        .memory_tracking_enabled = false,
        .log_level = BC_RUNTIME_LOG_LEVEL_INFO,
    };
    bc_runtime_callbacks_t callbacks = {.run = log_run};
    bc_runtime_t* app = NULL;
    bc_runtime_create(&config, &callbacks, &results, &app);
    bc_runtime_run(app);
    bc_runtime_destroy(app);

    double bc_ns_per = (double)results.bc_static_ns / (double)iterations;
    double bc_buf_ns_per = (double)results.bc_buffered_ns / (double)iterations;
    double bc_drain_total_ns_per = (double)(results.bc_buffered_ns + results.bc_buffered_drain_ns) / (double)iterations;
    double fp_ns_per = (double)results.fprintf_static_ns / (double)iterations;
    double bc_supp_ns_per = (double)results.bc_suppressed_ns / (double)iterations;
    double fp_supp_ns_per = (double)results.fprintf_suppressed_ns / (double)iterations;

    printf("  N=%-7zu  direct       bc_runtime_log=%7.0f ns/msg  fprintf=%7.0f ns/msg  ratio=%.2fx\n", iterations, bc_ns_per, fp_ns_per,
           fp_ns_per / bc_ns_per);
    printf("  N=%-7zu  buffered     bc_log_to_buffer=%5.0f ns/msg  (drain amortized=%5.0f ns/msg)  vs fprintf=%7.0f ns/msg  ratio=%.2fx\n",
           iterations, bc_buf_ns_per, bc_drain_total_ns_per, fp_ns_per, fp_ns_per / bc_drain_total_ns_per);
    printf("  N=%-7zu  filtered     bc_runtime_log=%7.0f ns/msg  if-fprintf=%7.0f ns/msg  ratio=%.2fx\n", iterations, bc_supp_ns_per,
           fp_supp_ns_per, fp_supp_ns_per / bc_supp_ns_per);
}

static void run_log_workloads(void)
{
    printf("\n--- log throughput: bc_runtime_log vs fprintf(stderr) ---\n");
    run_log_workload(10000);
    run_log_workload(100000);
}

static double bench_bc_error_collector(bc_allocators_context_t* memory, size_t errors_per_run, size_t reps)
{
    uint64_t samples[5];
    for (size_t s = 0; s < 5; s++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) {
            bc_runtime_error_collector_t* collector = NULL;
            bc_runtime_error_collector_create(memory, &collector);
            for (size_t i = 0; i < errors_per_run; i++) {
                bc_runtime_error_collector_append(collector, memory, "/some/path/that/does/not/exist", "open", 2);
            }
            size_t count = bc_runtime_error_collector_count(collector);
            use_ptr(&count);
            bc_runtime_error_collector_destroy(memory, collector);
        }
        samples[s] = now_ns() - t0;
    }
    uint64_t med = median_u64(samples, 5);
    return (double)med / (double)(reps * errors_per_run);
}

typedef struct {
    char* path;
    char* stage;
    int errno_value;
} libc_error_t;

static double bench_libc_error_chain(size_t errors_per_run, size_t reps)
{
    uint64_t samples[5];
    for (size_t s = 0; s < 5; s++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) {
            libc_error_t* errors = (libc_error_t*)malloc(sizeof(libc_error_t) * errors_per_run);
            for (size_t i = 0; i < errors_per_run; i++) {
                size_t path_len = strlen("/some/path/that/does/not/exist") + 1;
                size_t stage_len = strlen("open") + 1;
                errors[i].path = (char*)malloc(path_len);
                memcpy(errors[i].path, "/some/path/that/does/not/exist", path_len);
                errors[i].stage = (char*)malloc(stage_len);
                memcpy(errors[i].stage, "open", stage_len);
                errors[i].errno_value = 2;
            }
            use_ptr(errors);
            for (size_t i = 0; i < errors_per_run; i++) {
                free(errors[i].path);
                free(errors[i].stage);
            }
            free(errors);
        }
        samples[s] = now_ns() - t0;
    }
    uint64_t med = median_u64(samples, 5);
    return (double)med / (double)(reps * errors_per_run);
}

static void run_error_collector_workload(bc_allocators_context_t* memory)
{
    printf("\n--- error_collector: bc_runtime_error_collector vs malloc-based chain ---\n");
    static const size_t per_run[] = {16, 256};
    static const size_t reps_per_run[] = {20000, 2000};
    for (size_t i = 0; i < 2; i++) {
        double bc_ns = bench_bc_error_collector(memory, per_run[i], reps_per_run[i]);
        double libc_ns = bench_libc_error_chain(per_run[i], reps_per_run[i]);
        printf("  errors=%-4zu  bc_collector=%7.0f ns/error  malloc-chain=%7.0f ns/error  ratio=%.2fx\n", per_run[i], bc_ns, libc_ns,
               libc_ns / bc_ns);
    }
}

int main(void)
{
    printf("bench_bc_runtime_vs_libc\n\n");

    bc_allocators_context_config_t mem_config = {.max_pool_memory = 0, .tracking_enabled = false};
    bc_allocators_context_t* memory = NULL;
    bc_allocators_context_create(&mem_config, &memory);

    run_cli_workloads(memory);
    run_log_workloads();
    run_error_collector_workload(memory);

    bc_allocators_context_destroy(memory);
    return 0;
}
