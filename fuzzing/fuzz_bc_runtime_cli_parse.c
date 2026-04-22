// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_runtime.h"
#include "bc_runtime_cli.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_CLI_MAX_ARGS 16
#define FUZZ_CLI_MAX_ARG_LENGTH 64
#define FUZZ_CLI_BUFFER_SIZE (FUZZ_CLI_MAX_ARGS * FUZZ_CLI_MAX_ARG_LENGTH)

static const char* const algorithm_values[] = {"crc32", "sha256", "xxh3", NULL};
static const char* const format_values[] = {"simple", "table", "json", NULL};

static const bc_runtime_cli_option_spec_t global_options[] = {
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
        .help_summary = "verbose",
    },
};

static const bc_runtime_cli_option_spec_t hash_options[] = {
    {
        .long_name = "type",
        .type = BC_RUNTIME_CLI_OPTION_ENUM,
        .allowed_values = algorithm_values,
        .required = true,
        .value_placeholder = "ALGO",
        .help_summary = "algorithm",
    },
    {
        .long_name = "format",
        .type = BC_RUNTIME_CLI_OPTION_ENUM,
        .allowed_values = format_values,
        .default_value = "simple",
        .value_placeholder = "FMT",
        .help_summary = "format",
    },
    {
        .long_name = "force",
        .type = BC_RUNTIME_CLI_OPTION_BOOLEAN,
        .default_value = "false",
        .help_summary = "force",
    },
    {
        .long_name = "exclude",
        .type = BC_RUNTIME_CLI_OPTION_LIST,
        .value_placeholder = "GLOB",
        .help_summary = "exclude",
    },
};

static const bc_runtime_cli_command_spec_t commands[] = {
    {
        .name = "hash",
        .summary = "hash files",
        .options = hash_options,
        .option_count = sizeof(hash_options) / sizeof(hash_options[0]),
        .positional_usage = "<path>...",
        .positional_min = 1,
        .positional_max = 0,
    },
};

static const bc_runtime_cli_program_spec_t program_spec = {
    .program_name = "fuzz-cli",
    .version = "1.0.0",
    .summary = "cli parse fuzz target",
    .global_options = global_options,
    .global_option_count = sizeof(global_options) / sizeof(global_options[0]),
    .commands = commands,
    .command_count = sizeof(commands) / sizeof(commands[0]),
};

static size_t build_argv_from_fuzz_buffer(const uint8_t* data, size_t size, char* buffer, const char** argv, size_t argv_capacity)
{
    argv[0] = "fuzz-cli";
    size_t argument_count = 1;
    size_t buffer_position = 0;
    size_t data_position = 0;

    while (data_position < size && argument_count < argv_capacity && buffer_position < FUZZ_CLI_BUFFER_SIZE - 1) {
        const size_t argument_start = buffer_position;
        while (data_position < size && buffer_position < FUZZ_CLI_BUFFER_SIZE - 1) {
            const uint8_t byte = data[data_position++];
            if (byte == 0) {
                break;
            }
            buffer[buffer_position++] = (char)byte;
            if (buffer_position - argument_start >= FUZZ_CLI_MAX_ARG_LENGTH - 1) {
                break;
            }
        }
        buffer[buffer_position++] = '\0';
        argv[argument_count++] = &buffer[argument_start];
    }
    return argument_count;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    char argv_buffer[FUZZ_CLI_BUFFER_SIZE];
    const char* argv[FUZZ_CLI_MAX_ARGS];
    const size_t argument_count = build_argv_from_fuzz_buffer(data, size, argv_buffer, argv, FUZZ_CLI_MAX_ARGS);

    bc_allocators_context_t* memory_context = NULL;
    if (!bc_allocators_context_create(NULL, &memory_context)) {
        return 0;
    }
    bc_runtime_config_store_t* config_store = NULL;
    if (!bc_runtime_config_store_create(memory_context, &config_store)) {
        bc_allocators_context_destroy(memory_context);
        return 0;
    }

    bc_runtime_cli_parsed_t parsed = {0};
    FILE* error_stream = fopen("/dev/null", "w");
    (void)bc_runtime_cli_parse(&program_spec, (int)argument_count, argv, config_store, &parsed, error_stream);
    if (error_stream != NULL) {
        fclose(error_stream);
    }

    bc_runtime_config_store_destroy(memory_context, config_store);
    bc_allocators_context_destroy(memory_context);
    return 0;
}

#ifndef BC_FUZZ_LIBFUZZER
int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <iterations> [seed]\n", argv[0]);
        return 2;
    }
    const unsigned long iterations = strtoul(argv[1], NULL, 10);
    const unsigned long seed = (argc >= 3) ? strtoul(argv[2], NULL, 10) : 0;
    srand((unsigned int)seed);

    uint8_t buffer[2048];
    for (unsigned long i = 0; i < iterations; i++) {
        const size_t length = (size_t)(rand() % (int)sizeof(buffer));
        for (size_t j = 0; j < length; j++) {
            buffer[j] = (uint8_t)(rand() & 0xFF);
        }
        LLVMFuzzerTestOneInput(buffer, length);
    }
    return 0;
}
#endif
