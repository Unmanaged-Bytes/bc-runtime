// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_cli.h"
#include "bc_runtime_internal.h"

#include "bc_core.h"
#include "bc_core_cpu.h"
#include "bc_core_format.h"
#include "bc_core_io.h"

#include <stdio.h>

#define BC_RUNTIME_CLI_KEY_BUFFER_SIZE 256
#define BC_RUNTIME_CLI_GLOBAL_PREFIX "global."
#define BC_RUNTIME_CLI_GLOBAL_PREFIX_LENGTH 7
#define BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE 4096

static bool bc_runtime_cli_string_equal(const char* left, const char* right)
{
    size_t index = 0;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return false;
        }
        index++;
    }
    return left[index] == right[index];
}

static bool bc_runtime_cli_string_equal_n(const char* left, const char* right, size_t length)
{
    for (size_t index = 0; index < length; index++) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static size_t bc_runtime_cli_string_length(const char* value)
{
    size_t length = 0;
    while (value[length] != '\0') {
        length++;
    }
    return length;
}

static const bc_runtime_cli_option_spec_t* bc_runtime_cli_find_option(const bc_runtime_cli_option_spec_t* options, size_t option_count,
                                                                      const char* name, size_t name_length)
{
    for (size_t index = 0; index < option_count; index++) {
        const bc_runtime_cli_option_spec_t* option = &options[index];
        size_t candidate_length = bc_runtime_cli_string_length(option->long_name);
        if (candidate_length != name_length) {
            continue;
        }
        if (bc_runtime_cli_string_equal_n(option->long_name, name, name_length)) {
            return option;
        }
    }
    return NULL;
}

static const bc_runtime_cli_command_spec_t* bc_runtime_cli_find_command(const bc_runtime_cli_program_spec_t* spec, const char* name)
{
    for (size_t index = 0; index < spec->command_count; index++) {
        if (bc_runtime_cli_string_equal(spec->commands[index].name, name)) {
            return &spec->commands[index];
        }
    }
    return NULL;
}

static bool bc_runtime_cli_value_is_allowed(const bc_runtime_cli_option_spec_t* option, const char* value)
{
    if (option->allowed_values == NULL) {
        return true;
    }
    for (size_t index = 0; option->allowed_values[index] != NULL; index++) {
        if (bc_runtime_cli_string_equal(option->allowed_values[index], value)) {
            return true;
        }
    }
    return false;
}

static bool bc_runtime_cli_value_is_integer(const char* value)
{
    size_t index = 0;
    if (value[0] == '-' || value[0] == '+') {
        index = 1;
    }
    if (value[index] == '\0') {
        return false;
    }
    while (value[index] != '\0') {
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
        index++;
    }
    return true;
}

static bool bc_runtime_cli_value_is_boolean(const char* value)
{
    return bc_runtime_cli_string_equal(value, "true") || bc_runtime_cli_string_equal(value, "false") ||
           bc_runtime_cli_string_equal(value, "1") || bc_runtime_cli_string_equal(value, "0") ||
           bc_runtime_cli_string_equal(value, "yes") || bc_runtime_cli_string_equal(value, "no");
}

static bool bc_runtime_cli_validate_value(const bc_runtime_cli_option_spec_t* option, const char* value)
{
    switch (option->type) {
    case BC_RUNTIME_CLI_OPTION_STRING:
        return true;
    case BC_RUNTIME_CLI_OPTION_ENUM:
        return bc_runtime_cli_value_is_allowed(option, value);
    case BC_RUNTIME_CLI_OPTION_INTEGER:
        return bc_runtime_cli_value_is_integer(value);
    case BC_RUNTIME_CLI_OPTION_BOOLEAN:
        return bc_runtime_cli_value_is_boolean(value);
    case BC_RUNTIME_CLI_OPTION_LIST:
        return true;
    case BC_RUNTIME_CLI_OPTION_FLAG:
        return false;
    }
    return false;
}

static bool bc_runtime_cli_build_key(char* buffer, size_t buffer_size, const char* prefix, const char* name)
{
    size_t prefix_length = bc_runtime_cli_string_length(prefix);
    size_t name_length = bc_runtime_cli_string_length(name);
    if (prefix_length + name_length + 1 > buffer_size) {
        return false;
    }
    for (size_t index = 0; index < prefix_length; index++) {
        buffer[index] = prefix[index];
    }
    for (size_t index = 0; index < name_length; index++) {
        buffer[prefix_length + index] = name[index];
    }
    buffer[prefix_length + name_length] = '\0';
    return true;
}

static void bc_runtime_cli_drain_writer_to_stream(bc_core_writer_t* writer, FILE* stream)
{
    const char* data = NULL;
    size_t length = 0;
    if (!bc_core_writer_buffer_data(writer, &data, &length)) {
        return;
    }
    if (length > 0 && stream != NULL) {
        (void)fwrite(data, 1, length, stream);
        (void)fflush(stream);
    }
    (void)bc_core_writer_destroy(writer);
}

static bool bc_runtime_cli_apply_defaults(const bc_runtime_cli_option_spec_t* options, size_t option_count, const char* prefix,
                                          bc_runtime_config_store_t* store, const bool* seen_flags, const char* program_name,
                                          FILE* error_stream)
{
    for (size_t index = 0; index < option_count; index++) {
        const bc_runtime_cli_option_spec_t* option = &options[index];
        if (seen_flags[index]) {
            continue;
        }
        if (option->required) {
            char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
            bc_core_writer_t writer;
            if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                (void)bc_core_writer_write_cstring(&writer, program_name);
                (void)bc_core_writer_write_cstring(&writer, ": missing required option --");
                (void)bc_core_writer_write_cstring(&writer, option->long_name);
                (void)bc_core_writer_write_char(&writer, '\n');
                bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
            }
            return false;
        }
        if (option->default_value == NULL) {
            continue;
        }
        char key_buffer[BC_RUNTIME_CLI_KEY_BUFFER_SIZE];
        if (!bc_runtime_cli_build_key(key_buffer, sizeof(key_buffer), prefix, option->long_name)) {
            char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
            bc_core_writer_t writer;
            if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                (void)bc_core_writer_write_cstring(&writer, program_name);
                (void)bc_core_writer_write_cstring(&writer, ": option name too long: ");
                (void)bc_core_writer_write_cstring(&writer, option->long_name);
                (void)bc_core_writer_write_char(&writer, '\n');
                bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
            }
            return false;
        }
        if (!bc_runtime_config_store_set(store, key_buffer, option->default_value)) {
            char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
            bc_core_writer_t writer;
            if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                (void)bc_core_writer_write_cstring(&writer, program_name);
                (void)bc_core_writer_write_cstring(&writer, ": failed to store default for --");
                (void)bc_core_writer_write_cstring(&writer, option->long_name);
                (void)bc_core_writer_write_char(&writer, '\n');
                bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
            }
            return false;
        }
    }
    return true;
}

static bool bc_runtime_cli_parse_assignment(const char* argument, const char** out_name, size_t* out_name_length, const char** out_value,
                                            bool* out_has_value)
{
    size_t argument_length = bc_runtime_cli_string_length(argument);
    if (argument_length < 3 || argument[0] != '-' || argument[1] != '-') {
        return false;
    }
    const char* after_prefix = argument + 2;
    size_t after_prefix_length = argument_length - 2;

    size_t equals_offset = 0;
    bool found_equals = false;
    for (size_t index = 0; index < after_prefix_length; index++) {
        if (after_prefix[index] == '=') {
            equals_offset = index;
            found_equals = true;
            break;
        }
    }

    *out_name = after_prefix;
    if (found_equals) {
        *out_name_length = equals_offset;
        *out_value = after_prefix + equals_offset + 1;
        *out_has_value = true;
    } else {
        *out_name_length = after_prefix_length;
        *out_value = NULL;
        *out_has_value = false;
    }
    return true;
}

typedef struct bc_runtime_cli_parse_context {
    const bc_runtime_cli_program_spec_t* spec;
    bc_runtime_config_store_t* store;
    FILE* error_stream;
    bool* global_seen;
    bool* command_seen;
} bc_runtime_cli_parse_context_t;

static bool bc_runtime_cli_handle_assignment(bc_runtime_cli_parse_context_t* context, const bc_runtime_cli_option_spec_t* options,
                                             size_t option_count, bool* seen_flags, const char* prefix, const char* name,
                                             size_t name_length, const char* value, bool has_value)
{
    const bc_runtime_cli_option_spec_t* option = bc_runtime_cli_find_option(options, option_count, name, name_length);
    if (option == NULL) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, context->spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": unknown option --");
            (void)bc_core_writer_write_bytes(&writer, name, name_length);
            (void)bc_core_writer_write_char(&writer, '\n');
            bc_runtime_cli_drain_writer_to_stream(&writer, context->error_stream);
        }
        return false;
    }

    size_t option_index = (size_t)(option - options);

    if (option->type == BC_RUNTIME_CLI_OPTION_FLAG) {
        if (has_value) {
            char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
            bc_core_writer_t writer;
            if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                (void)bc_core_writer_write_cstring(&writer, context->spec->program_name);
                (void)bc_core_writer_write_cstring(&writer, ": option --");
                (void)bc_core_writer_write_cstring(&writer, option->long_name);
                (void)bc_core_writer_write_cstring(&writer, " does not take a value\n");
                bc_runtime_cli_drain_writer_to_stream(&writer, context->error_stream);
            }
            return false;
        }
        char key_buffer[BC_RUNTIME_CLI_KEY_BUFFER_SIZE];
        if (!bc_runtime_cli_build_key(key_buffer, sizeof(key_buffer), prefix, option->long_name)) {
            char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
            bc_core_writer_t writer;
            if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                (void)bc_core_writer_write_cstring(&writer, context->spec->program_name);
                (void)bc_core_writer_write_cstring(&writer, ": option name too long: ");
                (void)bc_core_writer_write_cstring(&writer, option->long_name);
                (void)bc_core_writer_write_char(&writer, '\n');
                bc_runtime_cli_drain_writer_to_stream(&writer, context->error_stream);
            }
            return false;
        }
        if (!bc_runtime_config_store_set(context->store, key_buffer, "true")) {
            char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
            bc_core_writer_t writer;
            if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                (void)bc_core_writer_write_cstring(&writer, context->spec->program_name);
                (void)bc_core_writer_write_cstring(&writer, ": failed to store --");
                (void)bc_core_writer_write_cstring(&writer, option->long_name);
                (void)bc_core_writer_write_char(&writer, '\n');
                bc_runtime_cli_drain_writer_to_stream(&writer, context->error_stream);
            }
            return false;
        }
        seen_flags[option_index] = true;
        return true;
    }

    if (!has_value) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, context->spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": option --");
            (void)bc_core_writer_write_cstring(&writer, option->long_name);
            (void)bc_core_writer_write_cstring(&writer, " requires a value\n");
            bc_runtime_cli_drain_writer_to_stream(&writer, context->error_stream);
        }
        return false;
    }

    if (!bc_runtime_cli_validate_value(option, value)) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, context->spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": invalid value for --");
            (void)bc_core_writer_write_cstring(&writer, option->long_name);
            (void)bc_core_writer_write_cstring(&writer, ": '");
            (void)bc_core_writer_write_cstring(&writer, value);
            (void)bc_core_writer_write_cstring(&writer, "'\n");
            bc_runtime_cli_drain_writer_to_stream(&writer, context->error_stream);
        }
        return false;
    }

    char key_buffer[BC_RUNTIME_CLI_KEY_BUFFER_SIZE];
    if (!bc_runtime_cli_build_key(key_buffer, sizeof(key_buffer), prefix, option->long_name)) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, context->spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": option name too long: ");
            (void)bc_core_writer_write_cstring(&writer, option->long_name);
            (void)bc_core_writer_write_char(&writer, '\n');
            bc_runtime_cli_drain_writer_to_stream(&writer, context->error_stream);
        }
        return false;
    }
    if (option->type == BC_RUNTIME_CLI_OPTION_LIST) {
        if (!bc_runtime_config_store_append(context->store, key_buffer, value, BC_RUNTIME_CLI_LIST_SEPARATOR)) {
            char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
            bc_core_writer_t writer;
            if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                (void)bc_core_writer_write_cstring(&writer, context->spec->program_name);
                (void)bc_core_writer_write_cstring(&writer, ": failed to store --");
                (void)bc_core_writer_write_cstring(&writer, option->long_name);
                (void)bc_core_writer_write_char(&writer, '\n');
                bc_runtime_cli_drain_writer_to_stream(&writer, context->error_stream);
            }
            return false;
        }
    } else if (!bc_runtime_config_store_set(context->store, key_buffer, value)) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, context->spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": failed to store --");
            (void)bc_core_writer_write_cstring(&writer, option->long_name);
            (void)bc_core_writer_write_char(&writer, '\n');
            bc_runtime_cli_drain_writer_to_stream(&writer, context->error_stream);
        }
        return false;
    }
    seen_flags[option_index] = true;
    return true;
}

static bool bc_runtime_cli_check_builtin(const char* name, size_t name_length, const char* literal)
{
    size_t literal_length = bc_runtime_cli_string_length(literal);
    if (literal_length != name_length) {
        return false;
    }
    return bc_runtime_cli_string_equal_n(literal, name, name_length);
}

bc_runtime_cli_parse_status_t bc_runtime_cli_parse(const bc_runtime_cli_program_spec_t* spec, int argument_count,
                                                   const char* const* argument_values, bc_runtime_config_store_t* store,
                                                   bc_runtime_cli_parsed_t* out_parsed, FILE* error_stream)
{
    out_parsed->command = NULL;
    out_parsed->positional_values = NULL;
    out_parsed->positional_count = 0;

    bool global_seen_stack[64];
    bool command_seen_stack[64];
    for (size_t index = 0; index < 64; index++) {
        global_seen_stack[index] = false;
        command_seen_stack[index] = false;
    }

    if (spec->global_option_count > 64 || (spec->commands != NULL && spec->command_count > 0)) {
        for (size_t index = 0; index < spec->command_count; index++) {
            if (spec->commands[index].option_count > 64) {
                char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
                bc_core_writer_t writer;
                if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
                    (void)bc_core_writer_write_cstring(&writer, ": internal error: too many options in command '");
                    (void)bc_core_writer_write_cstring(&writer, spec->commands[index].name);
                    (void)bc_core_writer_write_cstring(&writer, "'\n");
                    bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
                }
                return BC_RUNTIME_CLI_PARSE_ERROR;
            }
        }
    }
    if (spec->global_option_count > 64) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": internal error: too many global options\n");
            bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
        }
        return BC_RUNTIME_CLI_PARSE_ERROR;
    }

    bc_runtime_cli_parse_context_t context = {
        .spec = spec,
        .store = store,
        .error_stream = error_stream,
        .global_seen = global_seen_stack,
        .command_seen = command_seen_stack,
    };

    int index = 1;
    const bc_runtime_cli_command_spec_t* command = NULL;
    bool positional_started = false;
    int positional_start_index = -1;

    while (index < argument_count) {
        const char* current = argument_values[index];
        size_t current_length = bc_runtime_cli_string_length(current);

        if (current_length >= 2 && current[0] == '-' && current[1] == '-' && current_length == 2) {
            positional_started = true;
            positional_start_index = index + 1;
            break;
        }

        if (current_length >= 2 && current[0] == '-' && current[1] == '-') {
            const char* name = NULL;
            size_t name_length = 0;
            const char* value = NULL;
            bool has_value = false;
            if (!bc_runtime_cli_parse_assignment(current, &name, &name_length, &value, &has_value)) {
                char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
                bc_core_writer_t writer;
                if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
                    (void)bc_core_writer_write_cstring(&writer, ": malformed option '");
                    (void)bc_core_writer_write_cstring(&writer, current);
                    (void)bc_core_writer_write_cstring(&writer, "'\n");
                    bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
                }
                return BC_RUNTIME_CLI_PARSE_ERROR;
            }

            if (bc_runtime_cli_check_builtin(name, name_length, "help")) {
                if (command != NULL) {
                    out_parsed->command = command;
                    return BC_RUNTIME_CLI_PARSE_HELP_COMMAND;
                }
                return BC_RUNTIME_CLI_PARSE_HELP_GLOBAL;
            }
            if (bc_runtime_cli_check_builtin(name, name_length, "version")) {
                return BC_RUNTIME_CLI_PARSE_VERSION;
            }

            if (command == NULL) {
                if (!bc_runtime_cli_handle_assignment(&context, spec->global_options, spec->global_option_count, global_seen_stack,
                                                      BC_RUNTIME_CLI_GLOBAL_PREFIX, name, name_length, value, has_value)) {
                    return BC_RUNTIME_CLI_PARSE_ERROR;
                }
            } else {
                char command_prefix[BC_RUNTIME_CLI_KEY_BUFFER_SIZE];
                size_t command_name_length = bc_runtime_cli_string_length(command->name);
                if (command_name_length + 2 > sizeof(command_prefix)) {
                    char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
                    bc_core_writer_t writer;
                    if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                        (void)bc_core_writer_write_cstring(&writer, spec->program_name);
                        (void)bc_core_writer_write_cstring(&writer, ": command name too long\n");
                        bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
                    }
                    return BC_RUNTIME_CLI_PARSE_ERROR;
                }
                for (size_t pi = 0; pi < command_name_length; pi++) {
                    command_prefix[pi] = command->name[pi];
                }
                command_prefix[command_name_length] = '.';
                command_prefix[command_name_length + 1] = '\0';

                if (!bc_runtime_cli_handle_assignment(&context, command->options, command->option_count, command_seen_stack, command_prefix,
                                                      name, name_length, value, has_value)) {
                    return BC_RUNTIME_CLI_PARSE_ERROR;
                }
            }
            index += 1;
            continue;
        }

        if (command == NULL) {
            command = bc_runtime_cli_find_command(spec, current);
            if (command == NULL) {
                char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
                bc_core_writer_t writer;
                if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
                    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
                    (void)bc_core_writer_write_cstring(&writer, ": unknown command '");
                    (void)bc_core_writer_write_cstring(&writer, current);
                    (void)bc_core_writer_write_cstring(&writer, "'\n");
                    bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
                }
                return BC_RUNTIME_CLI_PARSE_ERROR;
            }
            index += 1;
            continue;
        }

        positional_start_index = index;
        positional_started = true;
        break;
    }

    if (command == NULL) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": missing command\n");
            bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
        }
        return BC_RUNTIME_CLI_PARSE_ERROR;
    }

    if (!bc_runtime_cli_apply_defaults(spec->global_options, spec->global_option_count, BC_RUNTIME_CLI_GLOBAL_PREFIX, store,
                                       global_seen_stack, spec->program_name, error_stream)) {
        return BC_RUNTIME_CLI_PARSE_ERROR;
    }

    char command_prefix[BC_RUNTIME_CLI_KEY_BUFFER_SIZE];
    size_t command_name_length = bc_runtime_cli_string_length(command->name);
    for (size_t pi = 0; pi < command_name_length; pi++) {
        command_prefix[pi] = command->name[pi];
    }
    command_prefix[command_name_length] = '.';
    command_prefix[command_name_length + 1] = '\0';

    if (!bc_runtime_cli_apply_defaults(command->options, command->option_count, command_prefix, store, command_seen_stack,
                                       spec->program_name, error_stream)) {
        return BC_RUNTIME_CLI_PARSE_ERROR;
    }

    size_t positional_count = 0;
    const char* const* positional_values = NULL;
    if (positional_started && positional_start_index >= 0 && positional_start_index < argument_count) {
        positional_count = (size_t)(argument_count - positional_start_index);
        positional_values = argument_values + positional_start_index;
    }

    if (positional_count < command->positional_min) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": command '");
            (void)bc_core_writer_write_cstring(&writer, command->name);
            (void)bc_core_writer_write_cstring(&writer, "' requires at least ");
            (void)bc_core_writer_write_unsigned_integer_64_decimal(&writer, (uint64_t)command->positional_min);
            (void)bc_core_writer_write_cstring(&writer, " positional argument(s)\n");
            bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
        }
        return BC_RUNTIME_CLI_PARSE_ERROR;
    }
    if (command->positional_max > 0 && positional_count > command->positional_max) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": command '");
            (void)bc_core_writer_write_cstring(&writer, command->name);
            (void)bc_core_writer_write_cstring(&writer, "' accepts at most ");
            (void)bc_core_writer_write_unsigned_integer_64_decimal(&writer, (uint64_t)command->positional_max);
            (void)bc_core_writer_write_cstring(&writer, " positional argument(s)\n");
            bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
        }
        return BC_RUNTIME_CLI_PARSE_ERROR;
    }

    if (!bc_runtime_config_store_sort(store)) {
        char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
        bc_core_writer_t writer;
        if (bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
            (void)bc_core_writer_write_cstring(&writer, spec->program_name);
            (void)bc_core_writer_write_cstring(&writer, ": failed to sort config store\n");
            bc_runtime_cli_drain_writer_to_stream(&writer, error_stream);
        }
        return BC_RUNTIME_CLI_PARSE_ERROR;
    }

    out_parsed->command = command;
    out_parsed->positional_values = positional_values;
    out_parsed->positional_count = positional_count;

    BC_UNUSED(context);
    return BC_RUNTIME_CLI_PARSE_OK;
}

static void bc_runtime_cli_print_option_line(const bc_runtime_cli_option_spec_t* option, bc_core_writer_t* writer)
{
    (void)bc_core_writer_write_cstring(writer, "  --");
    (void)bc_core_writer_write_cstring(writer, option->long_name);
    if (option->type != BC_RUNTIME_CLI_OPTION_FLAG) {
        const char* placeholder = option->value_placeholder != NULL ? option->value_placeholder : "VALUE";
        (void)bc_core_writer_write_char(writer, '=');
        (void)bc_core_writer_write_cstring(writer, placeholder);
    }
    (void)bc_core_writer_write_char(writer, '\n');
    if (option->help_summary != NULL) {
        (void)bc_core_writer_write_cstring(writer, "      ");
        (void)bc_core_writer_write_cstring(writer, option->help_summary);
        (void)bc_core_writer_write_char(writer, '\n');
    }
    if (option->allowed_values != NULL) {
        (void)bc_core_writer_write_cstring(writer, "      values:");
        for (size_t index = 0; option->allowed_values[index] != NULL; index++) {
            (void)bc_core_writer_write_char(writer, ' ');
            (void)bc_core_writer_write_cstring(writer, option->allowed_values[index]);
            if (option->allowed_values[index + 1] != NULL) {
                (void)bc_core_writer_write_char(writer, ',');
            }
        }
        (void)bc_core_writer_write_char(writer, '\n');
    }
    if (option->required) {
        (void)bc_core_writer_write_cstring(writer, "      (required)\n");
    } else if (option->default_value != NULL) {
        (void)bc_core_writer_write_cstring(writer, "      (default: ");
        (void)bc_core_writer_write_cstring(writer, option->default_value);
        (void)bc_core_writer_write_cstring(writer, ")\n");
    }
    if (option->type == BC_RUNTIME_CLI_OPTION_LIST) {
        (void)bc_core_writer_write_cstring(writer, "      (repeatable)\n");
    }
}

void bc_runtime_cli_print_help_global(const bc_runtime_cli_program_spec_t* spec, FILE* stream)
{
    char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
    bc_core_writer_t writer;
    if (!bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
        return;
    }

    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
    if (spec->summary != NULL) {
        (void)bc_core_writer_write_cstring(&writer, " - ");
        (void)bc_core_writer_write_cstring(&writer, spec->summary);
    }
    (void)bc_core_writer_write_cstring(&writer, "\n\n");

    (void)bc_core_writer_write_cstring(&writer, "USAGE\n");
    (void)bc_core_writer_write_cstring(&writer, "  ");
    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
    (void)bc_core_writer_write_cstring(&writer, " [global options] <command> [command options] [arguments...]\n\n");

    if (spec->global_option_count > 0) {
        (void)bc_core_writer_write_cstring(&writer, "GLOBAL OPTIONS\n");
        for (size_t index = 0; index < spec->global_option_count; index++) {
            bc_runtime_cli_print_option_line(&spec->global_options[index], &writer);
        }
        (void)bc_core_writer_write_cstring(&writer, "  --help\n      print this help\n");
        (void)bc_core_writer_write_cstring(&writer, "  --version\n      print version and exit\n");
        (void)bc_core_writer_write_char(&writer, '\n');
    } else {
        (void)bc_core_writer_write_cstring(&writer, "GLOBAL OPTIONS\n");
        (void)bc_core_writer_write_cstring(&writer, "  --help\n      print this help\n");
        (void)bc_core_writer_write_cstring(&writer, "  --version\n      print version and exit\n\n");
    }

    (void)bc_core_writer_write_cstring(&writer, "COMMANDS\n");
    for (size_t index = 0; index < spec->command_count; index++) {
        const bc_runtime_cli_command_spec_t* command = &spec->commands[index];
        (void)bc_core_writer_write_cstring(&writer, "  ");
        (void)bc_core_writer_write_cstring(&writer, command->name);
        if (command->summary != NULL) {
            (void)bc_core_writer_write_cstring(&writer, " - ");
            (void)bc_core_writer_write_cstring(&writer, command->summary);
        }
        (void)bc_core_writer_write_char(&writer, '\n');
    }
    (void)bc_core_writer_write_cstring(&writer, "\nRun '");
    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
    (void)bc_core_writer_write_cstring(&writer, " <command> --help' for details on a command.\n");

    bc_runtime_cli_drain_writer_to_stream(&writer, stream);
}

void bc_runtime_cli_print_help_command(const bc_runtime_cli_program_spec_t* spec, const bc_runtime_cli_command_spec_t* command,
                                       FILE* stream)
{
    char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
    bc_core_writer_t writer;
    if (!bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
        return;
    }

    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
    (void)bc_core_writer_write_char(&writer, ' ');
    (void)bc_core_writer_write_cstring(&writer, command->name);
    if (command->summary != NULL) {
        (void)bc_core_writer_write_cstring(&writer, " - ");
        (void)bc_core_writer_write_cstring(&writer, command->summary);
    }
    (void)bc_core_writer_write_cstring(&writer, "\n\n");

    (void)bc_core_writer_write_cstring(&writer, "USAGE\n");
    (void)bc_core_writer_write_cstring(&writer, "  ");
    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
    (void)bc_core_writer_write_cstring(&writer, " [global options] ");
    (void)bc_core_writer_write_cstring(&writer, command->name);
    (void)bc_core_writer_write_cstring(&writer, " [options]");
    if (command->positional_usage != NULL) {
        (void)bc_core_writer_write_char(&writer, ' ');
        (void)bc_core_writer_write_cstring(&writer, command->positional_usage);
    }
    (void)bc_core_writer_write_cstring(&writer, "\n\n");

    if (command->option_count > 0) {
        (void)bc_core_writer_write_cstring(&writer, "OPTIONS\n");
        for (size_t index = 0; index < command->option_count; index++) {
            bc_runtime_cli_print_option_line(&command->options[index], &writer);
        }
        (void)bc_core_writer_write_cstring(&writer, "  --help\n      print this help\n\n");
    } else {
        (void)bc_core_writer_write_cstring(&writer, "OPTIONS\n  --help\n      print this help\n\n");
    }

    (void)bc_core_writer_write_cstring(&writer, "Run '");
    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
    (void)bc_core_writer_write_cstring(&writer, " --help' for global options.\n");

    bc_runtime_cli_drain_writer_to_stream(&writer, stream);
}

void bc_runtime_cli_print_version(const bc_runtime_cli_program_spec_t* spec, FILE* stream)
{
    char buffer[BC_RUNTIME_CLI_OUTPUT_BUFFER_SIZE] BC_CACHE_LINE_ALIGNED;
    bc_core_writer_t writer;
    if (!bc_core_writer_init_buffer_only(&writer, buffer, sizeof(buffer))) {
        return;
    }
    const char* version = spec->version != NULL ? spec->version : "unknown";
    (void)bc_core_writer_write_cstring(&writer, spec->program_name);
    (void)bc_core_writer_write_char(&writer, ' ');
    (void)bc_core_writer_write_cstring(&writer, version);
    (void)bc_core_writer_write_char(&writer, '\n');
    bc_runtime_cli_drain_writer_to_stream(&writer, stream);
}
