// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static bool noop_init_callback(const bc_runtime_t* application, void* user_data)
{
    (void)application;
    (void)user_data;
    return true;
}

static bool noop_run_callback(const bc_runtime_t* application, void* user_data)
{
    (void)application;
    (void)user_data;
    return true;
}

static void noop_cleanup_callback(const bc_runtime_t* application, void* user_data)
{
    (void)application;
    (void)user_data;
}

struct accessor_fixture {
    bc_runtime_t* application;
};

static int group_setup(void** state)
{
    struct accessor_fixture* fixture = test_calloc(1, sizeof(*fixture));

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {
        .init = noop_init_callback,
        .run = noop_run_callback,
        .cleanup = noop_cleanup_callback,
    };

    if (!bc_runtime_create(&config, &callbacks, NULL, &fixture->application)) {
        test_free(fixture);
        return -1;
    }

    *state = fixture;
    return 0;
}

static int group_teardown(void** state)
{
    struct accessor_fixture* fixture = *state;
    bc_runtime_destroy(fixture->application);
    test_free(fixture);
    return 0;
}

static void test_memory_context_returns_valid_pointer(void** state)
{
    const struct accessor_fixture* fixture = *state;

    bc_allocators_context_t* memory_context = NULL;
    bool result = bc_runtime_memory_context(fixture->application, &memory_context);
    assert_true(result);
    assert_non_null(memory_context);
}

static void test_parallel_context_returns_valid_pointer(void** state)
{
    const struct accessor_fixture* fixture = *state;

    bc_concurrency_context_t* parallel_context = NULL;
    bool result = bc_runtime_parallel_context(fixture->application, &parallel_context);
    assert_true(result);
    assert_non_null(parallel_context);
}

static void test_should_stop_false_by_default(void** state)
{
    const struct accessor_fixture* fixture = *state;

    bool should_stop = true;
    bool result = bc_runtime_should_stop(fixture->application, &should_stop);
    assert_true(result);
    assert_false(should_stop);
}

static void test_current_state_created_after_create(void** state)
{
    const struct accessor_fixture* fixture = *state;

    bc_runtime_state_t current_state = BC_RUNTIME_STATE_STOPPED;
    bool result = bc_runtime_current_state(fixture->application, &current_state);
    assert_true(result);
    assert_int_equal(current_state, BC_RUNTIME_STATE_CREATED);
}

static void test_current_state_stopped_after_run(void** state)
{
    (void)state;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {
        .init = noop_init_callback,
        .run = noop_run_callback,
        .cleanup = noop_cleanup_callback,
    };

    bc_runtime_t* application = NULL;
    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bool run_result = bc_runtime_run(application);
    assert_true(run_result);

    bc_runtime_state_t current_state = BC_RUNTIME_STATE_CREATED;
    bool got_state = bc_runtime_current_state(application, &current_state);
    assert_true(got_state);
    assert_int_equal(current_state, BC_RUNTIME_STATE_STOPPED);

    bc_runtime_destroy(application);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_memory_context_returns_valid_pointer), cmocka_unit_test(test_parallel_context_returns_valid_pointer),
        cmocka_unit_test(test_should_stop_false_by_default),         cmocka_unit_test(test_current_state_created_after_create),
        cmocka_unit_test(test_current_state_stopped_after_run),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
