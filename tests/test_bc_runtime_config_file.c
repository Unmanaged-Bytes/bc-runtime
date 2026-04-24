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

static char* create_temp_file(const char* content)
{
    char* path = test_calloc(1, 256);
    snprintf(path, 256, "/tmp/bc_test_config_XXXXXX");
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

static void test_config_load_file_normal(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = create_temp_file("key1 = value1\nkey2 = value2\n");
    assert_non_null(path);

    assert_true(bc_runtime_config_load_file(store, fixture->memory_context, path));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "key1", &result));
    assert_int_equal(0, strcmp(result, "value1"));

    assert_true(bc_runtime_config_store_lookup(store, "key2", &result));
    assert_int_equal(0, strcmp(result, "value2"));

    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_file_comments(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = create_temp_file("# comment\nkey = value\n");
    assert_non_null(path);

    assert_true(bc_runtime_config_load_file(store, fixture->memory_context, path));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "key", &result));
    assert_int_equal(0, strcmp(result, "value"));

    assert_false(bc_runtime_config_store_lookup(store, "# comment", &result));

    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_file_empty_lines(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = create_temp_file("\n\nkey = value\n\n");
    assert_non_null(path);

    assert_true(bc_runtime_config_load_file(store, fixture->memory_context, path));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "key", &result));
    assert_int_equal(0, strcmp(result, "value"));

    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_file_no_equals(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = create_temp_file("invalid_line\nkey = value\n");
    assert_non_null(path);

    assert_true(bc_runtime_config_load_file(store, fixture->memory_context, path));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "key", &result));
    assert_int_equal(0, strcmp(result, "value"));

    assert_false(bc_runtime_config_store_lookup(store, "invalid_line", &result));

    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_file_empty_file(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = create_temp_file("");
    assert_non_null(path);

    assert_true(bc_runtime_config_load_file(store, fixture->memory_context, path));
    assert_true(bc_runtime_config_store_sort(store));

    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_file_trim_spaces(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = create_temp_file("  key  =  value  \n");
    assert_non_null(path);

    assert_true(bc_runtime_config_load_file(store, fixture->memory_context, path));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "key", &result));
    assert_int_equal(0, strcmp(result, "value"));

    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_file_nonexistent(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_false(bc_runtime_config_load_file(store, fixture->memory_context, "/tmp/bc_test_config_nonexistent_file_path"));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_load_file_normal),      cmocka_unit_test(test_config_load_file_comments),
        cmocka_unit_test(test_config_load_file_empty_lines), cmocka_unit_test(test_config_load_file_no_equals),
        cmocka_unit_test(test_config_load_file_empty_file),  cmocka_unit_test(test_config_load_file_trim_spaces),
        cmocka_unit_test(test_config_load_file_nonexistent),
    };

    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
