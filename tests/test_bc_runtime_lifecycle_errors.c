// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static int wrap_memory_context_create_fail = 0;
static int wrap_pool_allocate_call_count = 0;
static int wrap_pool_allocate_fail_at = -1;
static int wrap_signal_handler_create_fail = 0;
static int wrap_signal_handler_install_call_count = 0;
static int wrap_signal_handler_install_fail_at = -1;
static int wrap_config_store_create_fail = 0;
static int wrap_load_environment_fail = 0;
static int wrap_config_store_sort_fail = 0;
static int wrap_parallel_context_create_fail = 0;
static int wrap_load_arguments_fail = 0;

bool __real_bc_allocators_context_create(const bc_allocators_context_config_t* config, bc_allocators_context_t** out_ctx);
bool __wrap_bc_allocators_context_create(const bc_allocators_context_config_t* config, bc_allocators_context_t** out_ctx)
{
    if (wrap_memory_context_create_fail) {
        return false;
    }
    return __real_bc_allocators_context_create(config, out_ctx);
}

bool __real_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr);
bool __wrap_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr)
{
    wrap_pool_allocate_call_count++;
    if (wrap_pool_allocate_call_count == wrap_pool_allocate_fail_at) {
        return false;
    }
    return __real_bc_allocators_pool_allocate(ctx, size, out_ptr);
}

bool __real_bc_concurrency_signal_handler_create(bc_allocators_context_t* memory_context,
                                                 bc_concurrency_signal_handler_t** out_signal_handler);
bool __wrap_bc_concurrency_signal_handler_create(bc_allocators_context_t* memory_context,
                                                 bc_concurrency_signal_handler_t** out_signal_handler)
{
    if (wrap_signal_handler_create_fail) {
        return false;
    }
    return __real_bc_concurrency_signal_handler_create(memory_context, out_signal_handler);
}

bool __real_bc_concurrency_signal_handler_install(bc_concurrency_signal_handler_t* signal_handler, int signal_number);
bool __wrap_bc_concurrency_signal_handler_install(bc_concurrency_signal_handler_t* signal_handler, int signal_number)
{
    wrap_signal_handler_install_call_count++;
    if (wrap_signal_handler_install_call_count == wrap_signal_handler_install_fail_at) {
        return false;
    }
    return __real_bc_concurrency_signal_handler_install(signal_handler, signal_number);
}

bool __real_bc_runtime_config_store_create(bc_allocators_context_t* memory_context, bc_runtime_config_store_t** out_store);
bool __wrap_bc_runtime_config_store_create(bc_allocators_context_t* memory_context, bc_runtime_config_store_t** out_store)
{
    if (wrap_config_store_create_fail) {
        return false;
    }
    return __real_bc_runtime_config_store_create(memory_context, out_store);
}

bool __real_bc_runtime_config_load_environment(bc_runtime_config_store_t* store);
bool __wrap_bc_runtime_config_load_environment(bc_runtime_config_store_t* store)
{
    if (wrap_load_environment_fail) {
        return false;
    }
    return __real_bc_runtime_config_load_environment(store);
}

bool __real_bc_runtime_config_store_sort(bc_runtime_config_store_t* store);
bool __wrap_bc_runtime_config_store_sort(bc_runtime_config_store_t* store)
{
    if (wrap_config_store_sort_fail) {
        return false;
    }
    return __real_bc_runtime_config_store_sort(store);
}

bool __real_bc_runtime_config_load_arguments(bc_runtime_config_store_t* store, int argument_count, const char* const* argument_values);
bool __wrap_bc_runtime_config_load_arguments(bc_runtime_config_store_t* store, int argument_count, const char* const* argument_values)
{
    if (wrap_load_arguments_fail) {
        return false;
    }
    return __real_bc_runtime_config_load_arguments(store, argument_count, argument_values);
}

bool __real_bc_concurrency_create(bc_allocators_context_t* memory_context, const bc_concurrency_config_t* config,
                                  bc_concurrency_context_t** out_context);
bool __wrap_bc_concurrency_create(bc_allocators_context_t* memory_context, const bc_concurrency_config_t* config,
                                  bc_concurrency_context_t** out_context)
{
    if (wrap_parallel_context_create_fail) {
        return false;
    }
    return __real_bc_concurrency_create(memory_context, config, out_context);
}

static int test_setup(void** state)
{
    (void)state;
    wrap_memory_context_create_fail = 0;
    wrap_pool_allocate_call_count = 0;
    wrap_pool_allocate_fail_at = -1;
    wrap_signal_handler_create_fail = 0;
    wrap_signal_handler_install_call_count = 0;
    wrap_signal_handler_install_fail_at = -1;
    wrap_config_store_create_fail = 0;
    wrap_load_environment_fail = 0;
    wrap_config_store_sort_fail = 0;
    wrap_parallel_context_create_fail = 0;
    wrap_load_arguments_fail = 0;
    return 0;
}

static int test_teardown(void** state)
{
    (void)state;
    return 0;
}

static void test_create_fails_memory_context(void** state)
{
    (void)state;

    wrap_memory_context_create_fail = 1;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_create_fails_pool_allocate(void** state)
{
    (void)state;

    wrap_pool_allocate_fail_at = 1;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_create_fails_signal_handler(void** state)
{
    (void)state;

    wrap_signal_handler_create_fail = 1;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_create_fails_signal_install_sigint(void** state)
{
    (void)state;

    wrap_signal_handler_install_fail_at = 1;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_create_fails_signal_install_sigterm(void** state)
{
    (void)state;

    wrap_signal_handler_install_fail_at = 2;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_create_fails_config_store(void** state)
{
    (void)state;

    wrap_config_store_create_fail = 1;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_create_fails_load_environment(void** state)
{
    (void)state;

    wrap_load_environment_fail = 1;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_create_fails_config_sort(void** state)
{
    (void)state;

    wrap_config_store_sort_fail = 1;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_create_fails_parallel_context(void** state)
{
    (void)state;

    wrap_parallel_context_create_fail = 1;

    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

static void test_create_fails_load_arguments(void** state)
{
    (void)state;

    wrap_load_arguments_fail = 1;

    const char* argv[] = {"prog", "--key=value"};
    bc_runtime_config_t config = {
        .argument_count = 2,
        .argument_values = argv,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool result = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_false(result);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_create_fails_memory_context, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_fails_pool_allocate, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_fails_signal_handler, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_fails_signal_install_sigint, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_fails_signal_install_sigterm, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_fails_config_store, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_fails_load_environment, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_fails_config_sort, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_fails_parallel_context, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_create_fails_load_arguments, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
