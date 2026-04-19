// SPDX-License-Identifier: MIT
#ifndef BC_RUNTIME_CLI_H
#define BC_RUNTIME_CLI_H

#include "bc_runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum {
    BC_RUNTIME_CLI_OPTION_STRING,
    BC_RUNTIME_CLI_OPTION_INTEGER,
    BC_RUNTIME_CLI_OPTION_BOOLEAN,
    BC_RUNTIME_CLI_OPTION_ENUM,
    BC_RUNTIME_CLI_OPTION_FLAG,
    BC_RUNTIME_CLI_OPTION_LIST,
} bc_runtime_cli_option_type_t;

#define BC_RUNTIME_CLI_LIST_SEPARATOR '\n'

typedef struct bc_runtime_cli_option_spec {
    const char* long_name;
    bc_runtime_cli_option_type_t type;
    const char* const* allowed_values;
    const char* default_value;
    bool required;
    const char* value_placeholder;
    const char* help_summary;
} bc_runtime_cli_option_spec_t;

typedef struct bc_runtime_cli_command_spec {
    const char* name;
    const char* summary;
    const bc_runtime_cli_option_spec_t* options;
    size_t option_count;
    const char* positional_usage;
    size_t positional_min;
    size_t positional_max;
} bc_runtime_cli_command_spec_t;

typedef struct bc_runtime_cli_program_spec {
    const char* program_name;
    const char* version;
    const char* summary;
    const bc_runtime_cli_option_spec_t* global_options;
    size_t global_option_count;
    const bc_runtime_cli_command_spec_t* commands;
    size_t command_count;
} bc_runtime_cli_program_spec_t;

typedef enum {
    BC_RUNTIME_CLI_PARSE_OK,
    BC_RUNTIME_CLI_PARSE_HELP_GLOBAL,
    BC_RUNTIME_CLI_PARSE_HELP_COMMAND,
    BC_RUNTIME_CLI_PARSE_VERSION,
    BC_RUNTIME_CLI_PARSE_ERROR,
} bc_runtime_cli_parse_status_t;

typedef struct bc_runtime_cli_parsed {
    const bc_runtime_cli_command_spec_t* command;
    const char* const* positional_values;
    size_t positional_count;
} bc_runtime_cli_parsed_t;

bc_runtime_cli_parse_status_t bc_runtime_cli_parse(const bc_runtime_cli_program_spec_t* spec, int argument_count,
                                                   const char* const* argument_values, bc_runtime_config_store_t* config_store,
                                                   bc_runtime_cli_parsed_t* out_parsed, FILE* error_stream);

void bc_runtime_cli_print_help_global(const bc_runtime_cli_program_spec_t* spec, FILE* stream);

void bc_runtime_cli_print_help_command(const bc_runtime_cli_program_spec_t* spec, const bc_runtime_cli_command_spec_t* command, FILE* stream);

void bc_runtime_cli_print_version(const bc_runtime_cli_program_spec_t* spec, FILE* stream);

#endif // BC_RUNTIME_CLI_H
