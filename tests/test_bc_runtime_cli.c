// SPDX-License-Identifier: MIT
#include "bc_allocators.h"
#include "bc_runtime.h"
#include "bc_runtime_cli.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

struct fixture {
    bc_allocators_context_t* memory_context;
    bc_runtime_config_store_t* store;
    FILE* error_stream;
    char error_buffer[4096];
};

static int setup(void** state)
{
    struct fixture* fixture = test_calloc(1, sizeof(*fixture));
    bc_allocators_context_config_t config = {.tracking_enabled = true};
    if (!bc_allocators_context_create(&config, &fixture->memory_context)) {
        test_free(fixture);
        return -1;
    }
    if (!bc_runtime_config_store_create(fixture->memory_context, &fixture->store)) {
        bc_allocators_context_destroy(fixture->memory_context);
        test_free(fixture);
        return -1;
    }
    fixture->error_stream = fmemopen(fixture->error_buffer, sizeof(fixture->error_buffer), "w");
    *state = fixture;
    return 0;
}

static int teardown(void** state)
{
    struct fixture* fixture = *state;
    if (fixture->error_stream != NULL) {
        fclose(fixture->error_stream);
    }
    bc_runtime_config_store_destroy(fixture->memory_context, fixture->store);
    bc_allocators_context_destroy(fixture->memory_context);
    test_free(fixture);
    return 0;
}

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
        .help_summary = "enable verbose logging",
    },
};

static const bc_runtime_cli_option_spec_t hash_options[] = {
    {
        .long_name = "type",
        .type = BC_RUNTIME_CLI_OPTION_ENUM,
        .allowed_values = algorithm_values,
        .required = true,
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
    .program_name = "bc-test",
    .version = "1.0.0",
    .summary = "test program",
    .global_options = global_options,
    .global_option_count = sizeof(global_options) / sizeof(global_options[0]),
    .commands = commands,
    .command_count = sizeof(commands) / sizeof(commands[0]),
};

static void test_parse_happy_path(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "--threads=8", "hash", "--type=sha256", "/path1", "/path2"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 6, argv, fixture->store, &parsed, fixture->error_stream);

    assert_int_equal(BC_RUNTIME_CLI_PARSE_OK, status);
    assert_non_null(parsed.command);
    assert_string_equal("hash", parsed.command->name);
    assert_int_equal(2, parsed.positional_count);
    assert_string_equal("/path1", parsed.positional_values[0]);
    assert_string_equal("/path2", parsed.positional_values[1]);

    const char* value = NULL;
    assert_true(bc_runtime_config_store_get_string(fixture->store, "global.threads", &value));
    assert_string_equal("8", value);
    assert_true(bc_runtime_config_store_get_string(fixture->store, "hash.type", &value));
    assert_string_equal("sha256", value);
    assert_true(bc_runtime_config_store_get_string(fixture->store, "hash.format", &value));
    assert_string_equal("simple", value);
    assert_true(bc_runtime_config_store_get_string(fixture->store, "hash.force", &value));
    assert_string_equal("false", value);
}

static void test_parse_missing_command(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "--threads=4"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 2, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_ERROR, status);
}

static void test_parse_unknown_command(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "checksum", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 3, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_ERROR, status);
}

static void test_parse_unknown_global_option(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "--nope=1", "hash", "--type=sha256", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 5, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_ERROR, status);
}

static void test_parse_unknown_command_option(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=sha256", "--nope=x", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 5, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_ERROR, status);
}

static void test_parse_missing_required(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 3, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_ERROR, status);
}

static void test_parse_invalid_enum(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=md5", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 4, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_ERROR, status);
}

static void test_parse_invalid_integer(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "--threads=auto", "hash", "--type=sha256", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 5, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_ERROR, status);
}

static void test_parse_boolean_accepted_values(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=sha256", "--force=yes", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 5, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_OK, status);
    const char* value = NULL;
    assert_true(bc_runtime_config_store_get_string(fixture->store, "hash.force", &value));
    assert_string_equal("yes", value);
}

static void test_parse_flag_presence(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "--verbose", "hash", "--type=sha256", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 5, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_OK, status);
    const char* value = NULL;
    assert_true(bc_runtime_config_store_get_string(fixture->store, "global.verbose", &value));
    assert_string_equal("true", value);
}

static void test_parse_flag_rejects_value(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "--verbose=true", "hash", "--type=sha256", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 5, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_ERROR, status);
}

static void test_parse_defaults_applied(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=sha256", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 4, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_OK, status);
    const char* value = NULL;
    assert_true(bc_runtime_config_store_get_string(fixture->store, "global.threads", &value));
    assert_string_equal("0", value);
    assert_true(bc_runtime_config_store_get_string(fixture->store, "hash.format", &value));
    assert_string_equal("simple", value);
}

static void test_parse_positional_min_violation(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=sha256"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 3, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_ERROR, status);
}

static void test_parse_double_dash_separator(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=sha256", "--", "--weird-path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 5, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_OK, status);
    assert_int_equal(1, parsed.positional_count);
    assert_string_equal("--weird-path", parsed.positional_values[0]);
}

static void test_parse_help_global(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "--help"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 2, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_HELP_GLOBAL, status);
}

static void test_parse_help_command(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--help"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 3, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_HELP_COMMAND, status);
    assert_non_null(parsed.command);
    assert_string_equal("hash", parsed.command->name);
}

static void test_parse_version(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "--version"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 2, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_VERSION, status);
}

static void test_parse_user_overrides_default(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=sha256", "--format=json", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 5, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_OK, status);
    const char* value = NULL;
    assert_true(bc_runtime_config_store_get_string(fixture->store, "hash.format", &value));
    assert_string_equal("json", value);
}

static void test_parse_list_single_value(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=sha256", "--exclude=*.tmp", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 5, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_OK, status);
    const char* value = NULL;
    assert_true(bc_runtime_config_store_get_string(fixture->store, "hash.exclude", &value));
    assert_string_equal("*.tmp", value);
}

static void test_parse_list_multiple_values_accumulate(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=sha256", "--exclude=*.tmp", "--exclude=.git", "--exclude=node_modules", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 7, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_OK, status);
    const char* value = NULL;
    assert_true(bc_runtime_config_store_get_string(fixture->store, "hash.exclude", &value));
    assert_string_equal("*.tmp\n.git\nnode_modules", value);
}

static void test_parse_list_absent_not_in_store(void** state)
{
    struct fixture* fixture = *state;
    const char* argv[] = {"bc-test", "hash", "--type=sha256", "/path"};
    bc_runtime_cli_parsed_t parsed;

    bc_runtime_cli_parse_status_t status = bc_runtime_cli_parse(&program_spec, 4, argv, fixture->store, &parsed, fixture->error_stream);
    assert_int_equal(BC_RUNTIME_CLI_PARSE_OK, status);
    const char* value = NULL;
    assert_false(bc_runtime_config_store_get_string(fixture->store, "hash.exclude", &value));
}

static void test_print_help_global_includes_command(void** state)
{
    struct fixture* fixture = *state;
    char buffer[4096];
    FILE* stream = fmemopen(buffer, sizeof(buffer), "w");
    bc_runtime_cli_print_help_global(&program_spec, stream);
    fclose(stream);

    assert_non_null(strstr(buffer, "bc-test"));
    assert_non_null(strstr(buffer, "COMMANDS"));
    assert_non_null(strstr(buffer, "hash"));
    assert_non_null(strstr(buffer, "--threads"));
    assert_non_null(strstr(buffer, "--verbose"));
    BC_UNUSED(fixture);
}

static void test_print_help_command_includes_options(void** state)
{
    struct fixture* fixture = *state;
    char buffer[4096];
    FILE* stream = fmemopen(buffer, sizeof(buffer), "w");
    bc_runtime_cli_print_help_command(&program_spec, &commands[0], stream);
    fclose(stream);

    assert_non_null(strstr(buffer, "--type"));
    assert_non_null(strstr(buffer, "--format"));
    assert_non_null(strstr(buffer, "crc32"));
    assert_non_null(strstr(buffer, "(required)"));
    assert_non_null(strstr(buffer, "(default: simple)"));
    BC_UNUSED(fixture);
}

static void test_print_version(void** state)
{
    struct fixture* fixture = *state;
    char buffer[256];
    FILE* stream = fmemopen(buffer, sizeof(buffer), "w");
    bc_runtime_cli_print_version(&program_spec, stream);
    fclose(stream);

    assert_non_null(strstr(buffer, "bc-test"));
    assert_non_null(strstr(buffer, "1.0.0"));
    BC_UNUSED(fixture);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_parse_happy_path, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_missing_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_unknown_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_unknown_global_option, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_unknown_command_option, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_missing_required, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_invalid_enum, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_invalid_integer, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_boolean_accepted_values, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_flag_presence, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_flag_rejects_value, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_defaults_applied, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_positional_min_violation, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_double_dash_separator, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_help_global, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_help_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_version, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_user_overrides_default, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_list_single_value, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_list_multiple_values_accumulate, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parse_list_absent_not_in_store, setup, teardown),
        cmocka_unit_test_setup_teardown(test_print_help_global_includes_command, setup, teardown),
        cmocka_unit_test_setup_teardown(test_print_help_command_includes_options, setup, teardown),
        cmocka_unit_test_setup_teardown(test_print_version, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
