// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

struct lifecycle_fixture {
    int init_called;
    int run_called;
    int cleanup_called;
    bool init_return;
    bool run_return;
};

static bool fixture_init_callback(const bc_runtime_t* application, void* user_data)
{
    (void)application;
    struct lifecycle_fixture* fixture = user_data;
    fixture->init_called++;
    return fixture->init_return;
}

static bool fixture_run_callback(const bc_runtime_t* application, void* user_data)
{
    (void)application;
    struct lifecycle_fixture* fixture = user_data;
    fixture->run_called++;
    return fixture->run_return;
}

static void fixture_cleanup_callback(const bc_runtime_t* application, void* user_data)
{
    (void)application;
    struct lifecycle_fixture* fixture = user_data;
    fixture->cleanup_called++;
}

static void test_create_destroy(void** state)
{
    (void)state;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);
    assert_non_null(application);

    bc_runtime_state_t current_state = BC_RUNTIME_STATE_STOPPED;
    bool got_state = bc_runtime_current_state(application, &current_state);
    assert_true(got_state);
    assert_int_equal(current_state, BC_RUNTIME_STATE_CREATED);

    bc_runtime_destroy(application);
}

static void test_create_run_destroy(void** state)
{
    (void)state;

    struct lifecycle_fixture fixture = {
        .init_called = 0,
        .run_called = 0,
        .cleanup_called = 0,
        .init_return = true,
        .run_return = true,
    };

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {
        .init = fixture_init_callback,
        .run = fixture_run_callback,
        .cleanup = fixture_cleanup_callback,
    };

    bc_runtime_t* application = NULL;
    bool created = bc_runtime_create(&config, &callbacks, &fixture, &application);
    assert_true(created);

    bool run_result = bc_runtime_run(application);
    assert_true(run_result);
    assert_int_equal(fixture.init_called, 1);
    assert_int_equal(fixture.run_called, 1);
    assert_int_equal(fixture.cleanup_called, 1);

    bc_runtime_state_t current_state = BC_RUNTIME_STATE_CREATED;
    bool got_state = bc_runtime_current_state(application, &current_state);
    assert_true(got_state);
    assert_int_equal(current_state, BC_RUNTIME_STATE_STOPPED);

    bc_runtime_destroy(application);
}

static void test_init_fail_calls_cleanup(void** state)
{
    (void)state;

    struct lifecycle_fixture fixture = {
        .init_called = 0,
        .run_called = 0,
        .cleanup_called = 0,
        .init_return = false,
        .run_return = true,
    };

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {
        .init = fixture_init_callback,
        .run = fixture_run_callback,
        .cleanup = fixture_cleanup_callback,
    };

    bc_runtime_t* application = NULL;
    bool created = bc_runtime_create(&config, &callbacks, &fixture, &application);
    assert_true(created);

    bool run_result = bc_runtime_run(application);
    assert_false(run_result);
    assert_int_equal(fixture.init_called, 1);
    assert_int_equal(fixture.run_called, 0);
    assert_int_equal(fixture.cleanup_called, 1);

    bc_runtime_destroy(application);
}

static void test_run_fail_calls_cleanup(void** state)
{
    (void)state;

    struct lifecycle_fixture fixture = {
        .init_called = 0,
        .run_called = 0,
        .cleanup_called = 0,
        .init_return = true,
        .run_return = false,
    };

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {
        .init = fixture_init_callback,
        .run = fixture_run_callback,
        .cleanup = fixture_cleanup_callback,
    };

    bc_runtime_t* application = NULL;
    bool created = bc_runtime_create(&config, &callbacks, &fixture, &application);
    assert_true(created);

    bool run_result = bc_runtime_run(application);
    assert_false(run_result);
    assert_int_equal(fixture.run_called, 1);
    assert_int_equal(fixture.cleanup_called, 1);

    bc_runtime_destroy(application);
}

static void test_double_run_refused(void** state)
{
    (void)state;

    struct lifecycle_fixture fixture = {
        .init_called = 0,
        .run_called = 0,
        .cleanup_called = 0,
        .init_return = true,
        .run_return = true,
    };

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {
        .init = fixture_init_callback,
        .run = fixture_run_callback,
        .cleanup = fixture_cleanup_callback,
    };

    bc_runtime_t* application = NULL;
    bool created = bc_runtime_create(&config, &callbacks, &fixture, &application);
    assert_true(created);

    bool first_run = bc_runtime_run(application);
    assert_true(first_run);

    bool second_run = bc_runtime_run(application);
    assert_false(second_run);

    bc_runtime_destroy(application);
}

static void test_null_callbacks_succeed(void** state)
{
    (void)state;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};

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

static void test_destroy_after_create_only(void** state)
{
    (void)state;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};

    bc_runtime_t* application = NULL;
    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_destroy(application);
}

static void test_create_fails_nonexistent_config_file(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .config_file_path = "/nonexistent/path/config.txt",
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_init_fail_without_cleanup_callback(void** state)
{
    (void)state;

    struct lifecycle_fixture fixture = {
        .init_return = false,
    };

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {
        .init = fixture_init_callback,
        .cleanup = NULL,
    };

    bc_runtime_t* application = NULL;
    bool created = bc_runtime_create(&config, &callbacks, &fixture, &application);
    assert_true(created);

    bool run_result = bc_runtime_run(application);
    assert_false(run_result);
    assert_int_equal(fixture.init_called, 1);

    bc_runtime_destroy(application);
}

static void test_create_with_argument_count_positive_but_values_null(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .argument_count = 2,
        .argument_values = NULL,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_destroy(application);
}

static void test_run_without_cleanup_callback(void** state)
{
    (void)state;

    struct lifecycle_fixture fixture = {
        .init_return = true,
        .run_return = true,
    };

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {
        .init = fixture_init_callback,
        .run = fixture_run_callback,
        .cleanup = NULL,
    };

    bc_runtime_t* application = NULL;
    bool created = bc_runtime_create(&config, &callbacks, &fixture, &application);
    assert_true(created);

    bool run_result = bc_runtime_run(application);
    assert_true(run_result);
    assert_int_equal(fixture.init_called, 1);
    assert_int_equal(fixture.run_called, 1);

    bc_runtime_destroy(application);
}

static void test_run_without_init_callback(void** state)
{
    (void)state;

    struct lifecycle_fixture fixture = {
        .run_return = true,
    };

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {
        .init = NULL,
        .run = fixture_run_callback,
        .cleanup = fixture_cleanup_callback,
    };

    bc_runtime_t* application = NULL;
    bool created = bc_runtime_create(&config, &callbacks, &fixture, &application);
    assert_true(created);

    bool run_result = bc_runtime_run(application);
    assert_true(run_result);
    assert_int_equal(fixture.init_called, 0);
    assert_int_equal(fixture.run_called, 1);
    assert_int_equal(fixture.cleanup_called, 1);

    bc_runtime_destroy(application);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_run_destroy),
        cmocka_unit_test(test_init_fail_calls_cleanup),
        cmocka_unit_test(test_run_fail_calls_cleanup),
        cmocka_unit_test(test_double_run_refused),
        cmocka_unit_test(test_null_callbacks_succeed),
        cmocka_unit_test(test_destroy_after_create_only),
        cmocka_unit_test(test_create_fails_nonexistent_config_file),
        cmocka_unit_test(test_init_fail_without_cleanup_callback),
        cmocka_unit_test(test_create_with_argument_count_positive_but_values_null),
        cmocka_unit_test(test_run_without_cleanup_callback),
        cmocka_unit_test(test_run_without_init_callback),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
