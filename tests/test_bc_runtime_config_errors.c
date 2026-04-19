// SPDX-License-Identifier: MIT
#include <stdlib.h>

#include "bc_runtime.h"
#include "bc_core.h"
#include "bc_runtime_internal.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>

static int wrap_pool_allocate_call_count = 0;
static int wrap_pool_allocate_fail_at = -1;
static int wrap_arena_create_fail = 0;
static int wrap_arena_copy_string_call_count = 0;
static int wrap_arena_copy_string_fail_at = -1;
static int wrap_pool_reallocate_fail = 0;
static int wrap_commun_length_call_count = 0;
static int wrap_commun_length_fail_at = -1;

bool __real_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr);
bool __wrap_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr)
{
    wrap_pool_allocate_call_count++;
    if (wrap_pool_allocate_call_count == wrap_pool_allocate_fail_at) {
        return false;
    }
    return __real_bc_allocators_pool_allocate(ctx, size, out_ptr);
}

bool __real_bc_allocators_arena_create(bc_allocators_context_t* ctx, size_t capacity, bc_allocators_arena_t** out_arena);
bool __wrap_bc_allocators_arena_create(bc_allocators_context_t* ctx, size_t capacity, bc_allocators_arena_t** out_arena)
{
    if (wrap_arena_create_fail) {
        return false;
    }
    return __real_bc_allocators_arena_create(ctx, capacity, out_arena);
}

bool __real_bc_allocators_arena_copy_string(bc_allocators_arena_t* arena, const char* string, const char** out_copy);
bool __wrap_bc_allocators_arena_copy_string(bc_allocators_arena_t* arena, const char* string, const char** out_copy)
{
    wrap_arena_copy_string_call_count++;
    if (wrap_arena_copy_string_call_count == wrap_arena_copy_string_fail_at) {
        return false;
    }
    return __real_bc_allocators_arena_copy_string(arena, string, out_copy);
}

bool __real_bc_allocators_pool_reallocate(bc_allocators_context_t* ctx, void* old_ptr, size_t new_size, void** out_new_ptr);
bool __wrap_bc_allocators_pool_reallocate(bc_allocators_context_t* ctx, void* old_ptr, size_t new_size, void** out_new_ptr)
{
    if (wrap_pool_reallocate_fail) {
        return false;
    }
    return __real_bc_allocators_pool_reallocate(ctx, old_ptr, new_size, out_new_ptr);
}

bool __real_bc_core_length(const char* string, size_t limit, size_t* out_length);
bool __wrap_bc_core_length(const char* string, size_t limit, size_t* out_length)
{
    wrap_commun_length_call_count++;
    if (wrap_commun_length_call_count == wrap_commun_length_fail_at) {
        return false;
    }
    return __real_bc_core_length(string, limit, out_length);
}

static int wrap_commun_equal_call_count = 0;
static int wrap_commun_equal_fail_at = -1;

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

static int test_setup(void** state)
{
    (void)state;
    wrap_pool_allocate_call_count = 0;
    wrap_pool_allocate_fail_at = -1;
    wrap_arena_create_fail = 0;
    wrap_arena_copy_string_call_count = 0;
    wrap_arena_copy_string_fail_at = -1;
    wrap_pool_reallocate_fail = 0;
    wrap_commun_length_call_count = 0;
    wrap_commun_length_fail_at = -1;
    wrap_commun_equal_call_count = 0;
    wrap_commun_equal_fail_at = -1;
    return 0;
}

static int test_teardown(void** state)
{
    (void)state;
    return 0;
}

static void test_config_store_create_pool_allocate_store_fail(void** state)
{
    struct test_fixture* fixture = *state;

    wrap_pool_allocate_fail_at = 1;

    bc_runtime_config_store_t* store = NULL;
    bool result = bc_runtime_config_store_create(fixture->memory_context, &store);
    assert_false(result);
}

static void test_config_store_create_arena_create_fail(void** state)
{
    struct test_fixture* fixture = *state;

    wrap_arena_create_fail = 1;

    bc_runtime_config_store_t* store = NULL;
    bool result = bc_runtime_config_store_create(fixture->memory_context, &store);
    assert_false(result);
}

static void test_config_store_create_entries_allocate_fail(void** state)
{
    struct test_fixture* fixture = *state;

    wrap_pool_allocate_fail_at = 2;

    bc_runtime_config_store_t* store = NULL;
    bool result = bc_runtime_config_store_create(fixture->memory_context, &store);
    assert_false(result);
}

static void test_config_store_set_commun_length_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    wrap_commun_length_fail_at = wrap_commun_length_call_count + 1;

    bool result = bc_runtime_config_store_set(store, "key", "value");
    assert_false(result);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_set_arena_copy_string_overwrite_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "key", "first_value"));

    wrap_arena_copy_string_fail_at = wrap_arena_copy_string_call_count + 1;

    bool result = bc_runtime_config_store_set(store, "key", "second_value");
    assert_false(result);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_set_arena_copy_string_new_key_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    wrap_arena_copy_string_fail_at = wrap_arena_copy_string_call_count + 1;

    bool result = bc_runtime_config_store_set(store, "new_key", "value");
    assert_false(result);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_set_arena_copy_string_new_value_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    wrap_arena_copy_string_fail_at = wrap_arena_copy_string_call_count + 2;

    bool result = bc_runtime_config_store_set(store, "new_key", "value");
    assert_false(result);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_set_pool_reallocate_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char key_buffer[32];
    char value_buffer[32];
    for (size_t i = 0; i < store->entry_capacity; i++) {
        snprintf(key_buffer, sizeof(key_buffer), "key_%06zu", i);
        snprintf(value_buffer, sizeof(value_buffer), "val_%06zu", i);
        assert_true(bc_runtime_config_store_set(store, key_buffer, value_buffer));
    }

    wrap_pool_reallocate_fail = 1;

    bool result = bc_runtime_config_store_set(store, "overflow_key", "overflow_value");
    assert_false(result);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_sort_pool_allocate_temp_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "bravo", "b"));
    assert_true(bc_runtime_config_store_set(store, "alpha", "a"));

    wrap_pool_allocate_fail_at = wrap_pool_allocate_call_count + 1;

    bool result = bc_runtime_config_store_sort(store);
    assert_false(result);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_lookup_unsorted(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "key", "value"));

    const char* result = NULL;
    assert_false(bc_runtime_config_store_lookup(store, "key", &result));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_lookup_commun_length_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "key", "value"));
    assert_true(bc_runtime_config_store_sort(store));

    wrap_commun_length_fail_at = wrap_commun_length_call_count + 1;

    const char* result = NULL;
    assert_false(bc_runtime_config_store_lookup(store, "key", &result));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_lookup_key_length_mismatch(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "a", "short"));
    assert_true(bc_runtime_config_store_set(store, "abc", "longer"));
    assert_true(bc_runtime_config_store_set(store, "abcdef", "longest"));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "abc", &result));
    assert_int_equal(0, strcmp(result, "longer"));

    assert_false(bc_runtime_config_store_lookup(store, "ab", &result));
    assert_false(bc_runtime_config_store_lookup(store, "abcd", &result));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static char* create_temp_file(const char* content)
{
    char* path = test_calloc(1, 256);
    snprintf(path, 256, "/tmp/bc_test_config_err_XXXXXX");
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

static void test_config_load_file_pool_allocate_buffer_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = create_temp_file("key = value\n");
    assert_non_null(path);

    wrap_pool_allocate_fail_at = wrap_pool_allocate_call_count + 1;

    bool result = bc_runtime_config_load_file(store, fixture->memory_context, path);
    assert_false(result);

    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_file_pool_reallocate_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char* path = test_calloc(1, 256);
    snprintf(path, 256, "/tmp/bc_test_cfg_realloc_XXXXXX");
    int fd = mkstemp(path);
    assert_true(fd >= 0);

    char line[128];
    for (size_t i = 0; i < 2000; i++) {
        int line_length = snprintf(line, sizeof(line), "large_config_key_%06zu = large_config_value_%06zu_padding_data\n", i, i);
        ssize_t written = write(fd, line, (size_t)line_length);
        (void)written;
    }
    close(fd);

    wrap_pool_reallocate_fail = 1;

    bool result = bc_runtime_config_load_file(store, fixture->memory_context, path);
    assert_false(result);

    wrap_pool_reallocate_fail = 0;

    unlink(path);
    test_free(path);
    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

bool __real_bc_core_equal(const char* first, const char* second, size_t length, bool* out_equal);

bool __wrap_bc_core_equal(const char* first, const char* second, size_t length, bool* out_equal)
{
    wrap_commun_equal_call_count++;
    if (wrap_commun_equal_call_count == wrap_commun_equal_fail_at) {
        return false;
    }
    return __real_bc_core_equal(first, second, length, out_equal);
}

static void test_config_store_set_commun_equal_fail_during_scan(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "abc", "first"));

    wrap_commun_equal_call_count = 0;
    wrap_commun_equal_fail_at = 1;

    bool result = bc_runtime_config_store_set(store, "abc", "second");
    assert_false(result);

    wrap_commun_equal_fail_at = -1;

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_get_integer_commun_length_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "int_key", "42"));
    assert_true(bc_runtime_config_store_sort(store));

    bc_runtime_t application;
    bc_core_zero(&application, sizeof(application));
    application.config_store = store;

    wrap_commun_length_fail_at = wrap_commun_length_call_count + 2;

    long value = 0;
    bool result = bc_runtime_config_get_integer(&application, "int_key", &value);
    assert_false(result);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_get_boolean_commun_length_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "bool_key", "true"));
    assert_true(bc_runtime_config_store_sort(store));

    bc_runtime_t application;
    bc_core_zero(&application, sizeof(application));
    application.config_store = store;

    wrap_commun_length_fail_at = wrap_commun_length_call_count + 2;

    bool value = false;
    bool result = bc_runtime_config_get_boolean(&application, "bool_key", &value);
    assert_false(result);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_environment_store_set_fail(void** state)
{
    struct test_fixture* fixture = *state;

    setenv("BC_APP_TESTFAIL", "value", 1);

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    wrap_arena_copy_string_fail_at = wrap_arena_copy_string_call_count + 1;

    bool result = bc_runtime_config_load_environment(store);
    assert_false(result);

    unsetenv("BC_APP_TESTFAIL");

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_load_arguments_store_set_fail(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    const char* argv[] = {"prog", "--key=value"};

    wrap_arena_copy_string_fail_at = wrap_arena_copy_string_call_count + 1;

    bool result = bc_runtime_config_load_arguments(store, 2, argv);
    assert_false(result);

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_config_store_create_pool_allocate_store_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_create_arena_create_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_create_entries_allocate_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_set_commun_length_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_set_arena_copy_string_overwrite_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_set_arena_copy_string_new_key_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_set_arena_copy_string_new_value_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_set_pool_reallocate_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_sort_pool_allocate_temp_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_lookup_unsorted, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_lookup_commun_length_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_lookup_key_length_mismatch, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_load_file_pool_allocate_buffer_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_load_file_pool_reallocate_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_store_set_commun_equal_fail_during_scan, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_get_integer_commun_length_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_get_boolean_commun_length_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_load_environment_store_set_fail, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_config_load_arguments_store_set_fail, test_setup, test_teardown),
    };

    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
