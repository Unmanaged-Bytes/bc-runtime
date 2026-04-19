// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "bc_allocators.h"
#include "bc_runtime_internal.h"
#include "bc_core.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

struct test_fixture {
    bc_allocators_context_t* memory_context;
    bc_runtime_config_store_t* config_store;
    bc_runtime_t application;
};

static int group_setup(void** state)
{
    struct test_fixture* fixture = test_calloc(1, sizeof(*fixture));
    bc_allocators_context_config_t config = {.tracking_enabled = true};
    if (!bc_allocators_context_create(&config, &fixture->memory_context)) {
        test_free(fixture);
        return -1;
    }

    if (!bc_runtime_config_store_create(fixture->memory_context, &fixture->config_store)) {
        bc_allocators_context_destroy(fixture->memory_context);
        test_free(fixture);
        return -1;
    }

    *state = fixture;
    return 0;
}

static int group_teardown(void** state)
{
    struct test_fixture* fixture = *state;
    bc_runtime_config_store_destroy(fixture->memory_context, fixture->config_store);
    bc_allocators_context_destroy(fixture->memory_context);
    test_free(fixture);
    return 0;
}

static void test_parse_config_line_empty_key_after_trim(void** state)
{
    struct test_fixture* fixture = *state;

    const char* data = "  = value\n";
    bc_runtime_config_load_from_buffer(fixture->config_store, data, strlen(data));

    bc_runtime_config_store_sort(fixture->config_store);

    const char* result = NULL;
    assert_false(bc_runtime_config_store_lookup(fixture->config_store, "", &result));
    assert_false(bc_runtime_config_store_lookup(fixture->config_store, "value", &result));
}

static void test_load_from_buffer_line_exceeds_buffer_truncated(void** state)
{
    struct test_fixture* fixture = *state;

    char long_line[8300];
    bc_core_fill(long_line, 4200, (unsigned char)'K');
    long_line[4200] = '=';
    bc_core_copy(long_line + 4201, "value\n", 6);
    long_line[4207] = '\0';

    size_t initial_count = fixture->config_store->entry_count;
    bc_runtime_config_load_from_buffer(fixture->config_store, long_line, strlen(long_line));

    assert_int_equal(fixture->config_store->entry_count, initial_count);
}

static void test_load_from_buffer_value_fills_line_buffer(void** state)
{
    struct test_fixture* fixture = *state;

    char long_line[4100];
    bc_core_copy(long_line, "k=", 2);
    bc_core_fill(long_line + 2, 4090, (unsigned char)'V');
    long_line[4092] = '\n';
    long_line[4093] = '\0';

    size_t initial_count = fixture->config_store->entry_count;
    bc_runtime_config_load_from_buffer(fixture->config_store, long_line, strlen(long_line));

    assert_true(fixture->config_store->entry_count > initial_count);
}

static void test_load_from_buffer_last_line_no_newline(void** state)
{
    struct test_fixture* fixture = *state;

    const char* data = "last_line_key = last_line_value";
    bc_runtime_config_load_from_buffer(fixture->config_store, data, strlen(data));

    bc_runtime_config_store_sort(fixture->config_store);

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(fixture->config_store, "last_line_key", &result));
    assert_int_equal(0, strcmp(result, "last_line_value"));
}

static void test_config_get_integer_sign_only_plus(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_set(fixture->config_store, "sign_plus", "+");
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "sign_plus", &value));
}

static void test_config_get_integer_sign_only_minus(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_set(fixture->config_store, "sign_minus", "-");
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "sign_minus", &value));
}

static void test_config_get_integer_overflow_positive(void** state)
{
    struct test_fixture* fixture = *state;

    char huge_number[64];
    snprintf(huge_number, sizeof(huge_number), "%zu9", (size_t)__LONG_MAX__);

    bc_runtime_config_store_set(fixture->config_store, "huge_pos", huge_number);
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "huge_pos", &value));
}

static void test_config_get_integer_overflow_negative(void** state)
{
    struct test_fixture* fixture = *state;

    char huge_neg[64];
    snprintf(huge_neg, sizeof(huge_neg), "-%zu9", (size_t)__LONG_MAX__);

    bc_runtime_config_store_set(fixture->config_store, "huge_neg", huge_neg);
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "huge_neg", &value));
}

static void test_config_get_integer_exact_long_max(void** state)
{
    struct test_fixture* fixture = *state;

    char exact_max[64];
    snprintf(exact_max, sizeof(exact_max), "%ld", __LONG_MAX__);

    bc_runtime_config_store_set(fixture->config_store, "exact_max", exact_max);
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_true(bc_runtime_config_get_integer(&fixture->application, "exact_max", &value));
    assert_int_equal(value, __LONG_MAX__);
}

static void test_config_get_integer_exact_long_min(void** state)
{
    struct test_fixture* fixture = *state;

    char exact_min[64];
    snprintf(exact_min, sizeof(exact_min), "%ld", LONG_MIN);

    bc_runtime_config_store_set(fixture->config_store, "exact_min", exact_min);
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_true(bc_runtime_config_get_integer(&fixture->application, "exact_min", &value));
    assert_true(value == LONG_MIN);
}

static void test_config_get_integer_with_plus_prefix(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_set(fixture->config_store, "plus_val", "+42");
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_true(bc_runtime_config_get_integer(&fixture->application, "plus_val", &value));
    assert_int_equal(value, 42);
}

static void test_config_load_environment_key_empty(void** state)
{
    struct test_fixture* fixture = *state;

    setenv("BC_APP_", "empty_key_value", 1);

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    bool result = bc_runtime_config_load_environment(store);
    assert_true(result);

    bc_runtime_config_store_sort(store);

    const char* lookup_result = NULL;
    assert_false(bc_runtime_config_store_lookup(store, "", &lookup_result));

    unsetenv("BC_APP_");
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_arguments_key_empty(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    const char* argv[] = {"prog", "--=value"};
    int argc = 2;

    assert_true(bc_runtime_config_load_arguments(store, argc, argv));

    bc_runtime_config_store_sort(store);

    const char* result = NULL;
    assert_false(bc_runtime_config_store_lookup(store, "", &result));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_arguments_key_too_long(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char long_arg[8300];
    bc_core_copy(long_arg, "--", 2);
    bc_core_fill(long_arg + 2, 4200, (unsigned char)'K');
    long_arg[4202] = '=';
    bc_core_copy(long_arg + 4203, "val", 3);
    long_arg[4206] = '\0';

    const char* argv[] = {"prog", long_arg};
    int argc = 2;

    assert_true(bc_runtime_config_load_arguments(store, argc, argv));

    assert_true(store->entry_count > 0);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_environment_key_too_long(void** state)
{
    struct test_fixture* fixture = *state;

    char env_name[8300];
    bc_core_copy(env_name, "BC_APP_", 7);
    bc_core_fill(env_name + 7, 4200, (unsigned char)'A');
    env_name[4207] = '\0';

    setenv(env_name, "long_key_value", 1);

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    bool result = bc_runtime_config_load_environment(store);
    assert_true(result);

    assert_true(store->entry_count > 0);

    unsetenv(env_name);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_sort_keys_reverse_prefix_order(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_set(fixture->config_store, "abcdef", "longest");
    bc_runtime_config_store_set(fixture->config_store, "abc", "medium");
    bc_runtime_config_store_set(fixture->config_store, "a", "short");

    bc_runtime_config_store_sort(fixture->config_store);

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(fixture->config_store, "a", &result));
    assert_int_equal(0, strcmp(result, "short"));
    assert_true(bc_runtime_config_store_lookup(fixture->config_store, "abc", &result));
    assert_int_equal(0, strcmp(result, "medium"));
    assert_true(bc_runtime_config_store_lookup(fixture->config_store, "abcdef", &result));
    assert_int_equal(0, strcmp(result, "longest"));
}

static void test_config_load_environment_digit_in_key(void** state)
{
    struct test_fixture* fixture = *state;

    setenv("BC_APP_PORT2_BIND", "8080", 1);

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_load_environment(store));
    bc_runtime_config_store_sort(store);

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "port2.bind", &result));
    assert_int_equal(0, strcmp(result, "8080"));

    unsetenv("BC_APP_PORT2_BIND");
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_get_integer_non_digit_character(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_set(fixture->config_store, "bad_int", "12x34");
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "bad_int", &value));
}

static void test_config_get_integer_empty_value(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_set(fixture->config_store, "empty_int", "");
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "empty_int", &value));
}

static void test_config_load_file_large_triggers_realloc(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = test_calloc(1, 256);
    snprintf(path, 256, "/tmp/bc_test_cfg_large_XXXXXX");
    int fd = mkstemp(path);
    assert_true(fd >= 0);

    char padding[512];
    bc_core_fill(padding, sizeof(padding) - 1, (unsigned char)'X');
    padding[sizeof(padding) - 1] = '\0';

    char line[600];
    for (size_t i = 0; i < 100; i++) {
        int line_length = snprintf(line, sizeof(line), "# %s padding line %06zu\n", padding, i);
        ssize_t written = write(fd, line, (size_t)line_length);
        (void)written;
    }

    const char* final_line = "realloc_test_key = realloc_test_value\n";
    ssize_t written = write(fd, final_line, strlen(final_line));
    (void)written;
    close(fd);

    bool loaded = bc_runtime_config_load_file(store, fixture->memory_context, path);
    assert_true(loaded);

    bc_runtime_config_store_sort(store);

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "realloc_test_key", &result));
    assert_string_equal(result, "realloc_test_value");

    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_merge_sort_left_remainder(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    bc_runtime_config_store_set(store, "cherry", "3");
    bc_runtime_config_store_set(store, "apple", "1");
    bc_runtime_config_store_set(store, "banana", "2");

    bc_runtime_config_store_sort(store);

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "apple", &result));
    assert_string_equal(result, "1");
    assert_true(bc_runtime_config_store_lookup(store, "banana", &result));
    assert_string_equal(result, "2");
    assert_true(bc_runtime_config_store_lookup(store, "cherry", &result));
    assert_string_equal(result, "3");

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_merge_sort_five_entries(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    bc_runtime_config_store_set(store, "echo", "5");
    bc_runtime_config_store_set(store, "delta", "4");
    bc_runtime_config_store_set(store, "charlie", "3");
    bc_runtime_config_store_set(store, "bravo", "2");
    bc_runtime_config_store_set(store, "alpha", "1");

    bc_runtime_config_store_sort(store);

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "alpha", &result));
    assert_string_equal(result, "1");
    assert_true(bc_runtime_config_store_lookup(store, "echo", &result));
    assert_string_equal(result, "5");

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_get_integer_safe_multiply_overflow(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_set(fixture->config_store, "mul_overflow", "1844674407370955162");
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    bool result = bc_runtime_config_get_integer(&fixture->application, "mul_overflow", &value);
    (void)result;
}

static void test_config_get_integer_safe_add_overflow(void** state)
{
    struct test_fixture* fixture = *state;

    char number[32];
    snprintf(number, sizeof(number), "%ld8", __LONG_MAX__);

    bc_runtime_config_store_set(fixture->config_store, "add_overflow", number);
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "add_overflow", &value));
}

static void test_config_load_arguments_no_equals_skipped(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    const char* argv[] = {"prog", "--no-equals", "--valid=yes"};
    int argc = 3;

    assert_true(bc_runtime_config_load_arguments(store, argc, argv));

    bc_runtime_config_store_sort(store);

    const char* result = NULL;
    assert_false(bc_runtime_config_store_lookup(store, "no-equals", &result));
    assert_true(bc_runtime_config_store_lookup(store, "valid", &result));
    assert_string_equal(result, "yes");

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_arguments_no_prefix_skipped(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    const char* argv[] = {"prog", "positional", "-single", "--valid=yes"};
    int argc = 4;

    assert_true(bc_runtime_config_load_arguments(store, argc, argv));

    bc_runtime_config_store_sort(store);

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "valid", &result));
    assert_string_equal(result, "yes");

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_get_integer_nonexistent_key(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "definitely_absent", &value));
}

static void test_config_get_integer_safe_add_overflow_exact(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_set(fixture->config_store, "add_exact", "18446744073709551616");
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "add_exact", &value));
}

static void test_config_get_integer_negative_long_min_minus_one(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_set(fixture->config_store, "neg_lmin_m1", "-9223372036854775810");
    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    long value = 0;
    assert_false(bc_runtime_config_get_integer(&fixture->application, "neg_lmin_m1", &value));
}

static void test_config_load_environment_entry_without_equals(void** state)
{
    struct test_fixture* fixture = *state;
    extern char** environ;

    char** saved_environ = environ;
    char malformed[] = "BC_APP_NO_EQUALS_ENTRY";
    char entry_regular[] = "BC_APP_REGULAR=value";
    char* fake_environ[3];
    fake_environ[0] = malformed;
    fake_environ[1] = entry_regular;
    fake_environ[2] = NULL;

    environ = fake_environ;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    bool result = bc_runtime_config_load_environment(store);

    environ = saved_environ;

    assert_true(result);

    bc_runtime_config_store_sort(store);

    const char* lookup_result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "regular", &lookup_result));
    assert_string_equal(lookup_result, "value");

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_sort_duplicate_identical_keys(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    bc_runtime_config_store_set(store, "bravo", "2");
    bc_runtime_config_store_set(store, "alpha", "1");

    const char* duplicate_key = NULL;
    assert_true(bc_allocators_arena_copy_string(store->arena, "alpha", &duplicate_key));

    const char* duplicate_value = NULL;
    assert_true(bc_allocators_arena_copy_string(store->arena, "1-bis", &duplicate_value));

    store->entries[store->entry_count].key = duplicate_key;
    store->entries[store->entry_count].key_length = 5;
    store->entries[store->entry_count].value = duplicate_value;
    store->entry_count++;
    store->sorted = false;

    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "alpha", &result));
    assert_true(bc_runtime_config_store_lookup(store, "bravo", &result));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_parse_config_line_empty_key_after_trim),
        cmocka_unit_test(test_load_from_buffer_line_exceeds_buffer_truncated),
        cmocka_unit_test(test_load_from_buffer_value_fills_line_buffer),
        cmocka_unit_test(test_load_from_buffer_last_line_no_newline),
        cmocka_unit_test(test_config_get_integer_sign_only_plus),
        cmocka_unit_test(test_config_get_integer_sign_only_minus),
        cmocka_unit_test(test_config_get_integer_overflow_positive),
        cmocka_unit_test(test_config_get_integer_overflow_negative),
        cmocka_unit_test(test_config_get_integer_exact_long_max),
        cmocka_unit_test(test_config_get_integer_exact_long_min),
        cmocka_unit_test(test_config_get_integer_with_plus_prefix),
        cmocka_unit_test(test_config_load_environment_key_empty),
        cmocka_unit_test(test_config_load_arguments_key_empty),
        cmocka_unit_test(test_config_load_arguments_key_too_long),
        cmocka_unit_test(test_config_load_environment_key_too_long),
        cmocka_unit_test(test_config_sort_keys_reverse_prefix_order),
        cmocka_unit_test(test_config_load_environment_digit_in_key),
        cmocka_unit_test(test_config_get_integer_non_digit_character),
        cmocka_unit_test(test_config_get_integer_empty_value),
        cmocka_unit_test(test_config_load_file_large_triggers_realloc),
        cmocka_unit_test(test_config_merge_sort_left_remainder),
        cmocka_unit_test(test_config_merge_sort_five_entries),
        cmocka_unit_test(test_config_get_integer_safe_multiply_overflow),
        cmocka_unit_test(test_config_get_integer_safe_add_overflow),
        cmocka_unit_test(test_config_load_arguments_no_equals_skipped),
        cmocka_unit_test(test_config_load_arguments_no_prefix_skipped),
        cmocka_unit_test(test_config_get_integer_nonexistent_key),
        cmocka_unit_test(test_config_get_integer_safe_add_overflow_exact),
        cmocka_unit_test(test_config_get_integer_negative_long_min_minus_one),
        cmocka_unit_test(test_config_load_environment_entry_without_equals),
        cmocka_unit_test(test_config_sort_duplicate_identical_keys),
    };

    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
