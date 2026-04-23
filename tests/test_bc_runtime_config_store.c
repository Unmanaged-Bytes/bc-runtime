// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "bc_allocators.h"
#include <stdlib.h>
#include "bc_runtime_internal.h"

#include <stdio.h>
#include <string.h>

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

static void test_config_store_create_destroy(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    bool created = bc_runtime_config_store_create(fixture->memory_context, &store);
    assert_true(created);
    assert_non_null(store);

    bc_runtime_config_store_destroy(fixture->memory_context, store);

    bc_allocators_stats_t stats;
    bool got_stats = bc_allocators_context_get_stats(fixture->memory_context, &stats);
    assert_true(got_stats);
    assert_int_equal(stats.pool_alloc_count, stats.pool_free_count);
}

static void test_config_store_set_and_lookup(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "alpha", "value_a"));
    assert_true(bc_runtime_config_store_set(store, "beta", "value_b"));
    assert_true(bc_runtime_config_store_set(store, "gamma", "value_c"));

    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;

    assert_true(bc_runtime_config_store_lookup(store, "alpha", &result));
    assert_int_equal(0, strcmp(result, "value_a"));

    assert_true(bc_runtime_config_store_lookup(store, "beta", &result));
    assert_int_equal(0, strcmp(result, "value_b"));

    assert_true(bc_runtime_config_store_lookup(store, "gamma", &result));
    assert_int_equal(0, strcmp(result, "value_c"));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_lookup_absent_key(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "exists", "yes"));
    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_false(bc_runtime_config_store_lookup(store, "missing", &result));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_overwrite_existing_key(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "key", "first"));
    assert_true(bc_runtime_config_store_set(store, "key", "second"));

    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;
    assert_true(bc_runtime_config_store_lookup(store, "key", &result));
    assert_int_equal(0, strcmp(result, "second"));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_sort_and_binary_search(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    assert_true(bc_runtime_config_store_set(store, "zebra", "z"));
    assert_true(bc_runtime_config_store_set(store, "mango", "m"));
    assert_true(bc_runtime_config_store_set(store, "apple", "a"));
    assert_true(bc_runtime_config_store_set(store, "banana", "b"));
    assert_true(bc_runtime_config_store_set(store, "cherry", "c"));

    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;

    assert_true(bc_runtime_config_store_lookup(store, "apple", &result));
    assert_int_equal(0, strcmp(result, "a"));

    assert_true(bc_runtime_config_store_lookup(store, "banana", &result));
    assert_int_equal(0, strcmp(result, "b"));

    assert_true(bc_runtime_config_store_lookup(store, "cherry", &result));
    assert_int_equal(0, strcmp(result, "c"));

    assert_true(bc_runtime_config_store_lookup(store, "mango", &result));
    assert_int_equal(0, strcmp(result, "m"));

    assert_true(bc_runtime_config_store_lookup(store, "zebra", &result));
    assert_int_equal(0, strcmp(result, "z"));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

static void test_config_store_reallocation_beyond_capacity(void** state)
{
    struct test_fixture* fixture = *state;

    bc_runtime_config_store_t* store = NULL;
    assert_true(bc_runtime_config_store_create(fixture->memory_context, &store));

    char key_buffer[32];
    char value_buffer[32];
    size_t total_entries = 1100;

    for (size_t i = 0; i < total_entries; i++) {
        snprintf(key_buffer, sizeof(key_buffer), "key_%06zu", i);
        snprintf(value_buffer, sizeof(value_buffer), "val_%06zu", i);
        assert_true(bc_runtime_config_store_set(store, key_buffer, value_buffer));
    }

    assert_true(bc_runtime_config_store_sort(store));

    const char* result = NULL;

    assert_true(bc_runtime_config_store_lookup(store, "key_000000", &result));
    assert_int_equal(0, strcmp(result, "val_000000"));

    assert_true(bc_runtime_config_store_lookup(store, "key_000550", &result));
    assert_int_equal(0, strcmp(result, "val_000550"));

    assert_true(bc_runtime_config_store_lookup(store, "key_001099", &result));
    assert_int_equal(0, strcmp(result, "val_001099"));

    bc_runtime_config_store_destroy(fixture->memory_context, store);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_store_create_destroy),         cmocka_unit_test(test_config_store_set_and_lookup),
        cmocka_unit_test(test_config_store_lookup_absent_key),      cmocka_unit_test(test_config_store_overwrite_existing_key),
        cmocka_unit_test(test_config_store_sort_and_binary_search), cmocka_unit_test(test_config_store_reallocation_beyond_capacity),
    };

    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
