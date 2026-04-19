// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static bool wrap_memory_get_stats_fail = false;

bool __real_bc_allocators_context_get_stats(const bc_allocators_context_t* ctx, bc_allocators_stats_t* out_stats);
bool __wrap_bc_allocators_context_get_stats(const bc_allocators_context_t* ctx, bc_allocators_stats_t* out_stats)
{
    if (wrap_memory_get_stats_fail) {
        return false;
    }
    return __real_bc_allocators_context_get_stats(ctx, out_stats);
}

static int test_setup(void** state)
{
    (void)state;
    wrap_memory_get_stats_fail = false;
    return 0;
}

static int test_teardown(void** state)
{
    (void)state;
    return 0;
}

static void test_get_metrics_fails_memory_stats(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .memory_tracking_enabled = true,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    wrap_memory_get_stats_fail = true;

    bc_runtime_metrics_t metrics = {0};
    bool got_metrics = bc_runtime_get_metrics(application, &metrics);
    assert_false(got_metrics);

    wrap_memory_get_stats_fail = false;

    bc_runtime_destroy(application);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_get_metrics_fails_memory_stats, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
