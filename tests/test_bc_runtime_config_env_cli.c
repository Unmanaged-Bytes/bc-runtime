// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "bc_allocators.h"
#include "bc_runtime_internal.h"

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
};

static int group_setup(void** state)
{
    struct test_fixture* fixture = test_calloc(1, sizeof(*fixture));
    bc_allocators_context_config_t config = {.tracking_enabled = true};
    if (!bc_allocators_context_create(&config, &fixture->memory_context)) {
        test_free(fixture);
        return -1;
    }
    *state = fixture;
    return 0;
}

static int group_teardown(void** state)
{
    struct test_fixture* fixture = *state;
    bc_allocators_context_destroy(fixture->memory_context);
    test_free(fixture);
    return 0;
}

static void test_config_load_environment_basic(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    setenv("BC_APP_MY_KEY", "myvalue", 1);

    assert_true(bc_runtime_config_load_environment(store));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "my.key", &result));
    assert_int_equal(0, strcmp(result, "myvalue"));

    unsetenv("BC_APP_MY_KEY");
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_environment_key_transform(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    setenv("BC_APP_THREAD_COUNT", "8", 1);

    assert_true(bc_runtime_config_load_environment(store));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "thread.count", &result));
    assert_int_equal(0, strcmp(result, "8"));

    unsetenv("BC_APP_THREAD_COUNT");
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_arguments_basic(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    const char* argv[] = {"prog", "--key=value"};
    int argc = 2;

    assert_true(bc_runtime_config_load_arguments(store, argc, argv));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "key", &result));
    assert_int_equal(0, strcmp(result, "value"));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_arguments_ignore_no_prefix(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    const char* argv[] = {"prog", "noprefixarg"};
    int argc = 2;

    assert_true(bc_runtime_config_load_arguments(store, argc, argv));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_false(bc_runtime_config_store_lookup(store, "noprefixarg", &result));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_arguments_ignore_no_equals(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    const char* argv[] = {"prog", "--keyonly"};
    int argc = 2;

    assert_true(bc_runtime_config_load_arguments(store, argc, argv));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_false(bc_runtime_config_store_lookup(store, "keyonly", &result));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static char* create_temp_file(const char* content)
{
    char* path = test_calloc(1, 256);
    snprintf(path, 256, "/tmp/bc_test_env_cli_XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) {
        test_free(path);
        return NULL;
    }
    if (content != NULL) {
        size_t length = strlen(content);
        if (length > 0) {
            ssize_t written = write(fd, content, length);
            (void)written;
        }
    }
    close(fd);
    return path;
}

static void test_config_priority_cli_over_env_over_file(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = create_temp_file("key = file_val\n");
    assert_non_null(path);
    assert_true(bc_runtime_config_load_file(store, fixture->memory_context, path));

    setenv("BC_APP_KEY", "env_val", 1);
    assert_true(bc_runtime_config_load_environment(store));

    const char* argv[] = {"prog", "--key=cli_val"};
    int argc = 2;
    assert_true(bc_runtime_config_load_arguments(store, argc, argv));

    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "key", &result));
    assert_int_equal(0, strcmp(result, "cli_val"));

    unsetenv("BC_APP_KEY");
    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_load_environment_basic),
        cmocka_unit_test(test_config_load_environment_key_transform),
        cmocka_unit_test(test_config_load_arguments_basic),
        cmocka_unit_test(test_config_load_arguments_ignore_no_prefix),
        cmocka_unit_test(test_config_load_arguments_ignore_no_equals),
        cmocka_unit_test(test_config_priority_cli_over_env_over_file),
    };

    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
