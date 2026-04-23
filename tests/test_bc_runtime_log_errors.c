// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

static bool wrap_clock_gettime_fail = false;
static bool wrap_write_fail = false;
static int wrap_commun_length_call_count = 0;
static int wrap_commun_length_fail_at = -1;
static int wrap_pool_allocate_call_count = 0;
static int wrap_pool_allocate_fail_at = -1;

int __real_clock_gettime(clockid_t clk_id, struct timespec* tp);
int __wrap_clock_gettime(clockid_t clk_id, struct timespec* tp)
{
    if (wrap_clock_gettime_fail) {
        errno = EINVAL;
        return -1;
    }
    return __real_clock_gettime(clk_id, tp);
}

ssize_t __real_write(int fd, const void* buf, size_t count);
ssize_t __wrap_write(int fd, const void* buf, size_t count)
{
    if (wrap_write_fail && fd == STDERR_FILENO) {
        errno = EIO;
        return -1;
    }
    return __real_write(fd, buf, count);
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

bool __real_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr);
bool __wrap_bc_allocators_pool_allocate(bc_allocators_context_t* ctx, size_t size, void** out_ptr)
{
    wrap_pool_allocate_call_count++;
    if (wrap_pool_allocate_call_count == wrap_pool_allocate_fail_at) {
        return false;
    }
    return __real_bc_allocators_pool_allocate(ctx, size, out_ptr);
}

static int test_setup(void** state)
{
    (void)state;
    wrap_clock_gettime_fail = false;
    wrap_write_fail = false;
    wrap_commun_length_call_count = 0;
    wrap_commun_length_fail_at = -1;
    wrap_pool_allocate_call_count = 0;
    wrap_pool_allocate_fail_at = -1;
    return 0;
}

static int test_teardown(void** state)
{
    (void)state;
    return 0;
}

static void test_log_fails_clock_gettime(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    wrap_clock_gettime_fail = true;

    bool result = bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_ERROR, "should fail timestamp");
    assert_false(result);

    wrap_clock_gettime_fail = false;

    bc_runtime_destroy(application);
}

static void test_log_fails_write(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    wrap_write_fail = true;

    bool result = bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_ERROR, "should fail write");
    assert_false(result);

    wrap_write_fail = false;

    bc_runtime_destroy(application);
}

static void test_log_to_buffer_fails_clock_gettime(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_log_buffer_t* buffer = NULL;
    bool buffer_created = bc_runtime_log_buffer_create(application, 8192, &buffer);
    assert_true(buffer_created);

    wrap_clock_gettime_fail = true;

    bool result = bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR, "should fail timestamp");
    assert_false(result);

    wrap_clock_gettime_fail = false;

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_drain_fails_write(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_log_buffer_t* buffer = NULL;
    bool buffer_created = bc_runtime_log_buffer_create(application, 8192, &buffer);
    assert_true(buffer_created);

    bool logged = bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR, "message in buffer");
    assert_true(logged);
    assert_true(buffer->write_position > 0);

    wrap_write_fail = true;

    bc_runtime_log_buffer_t* buffers[] = {buffer};
    bool drain_result = bc_runtime_log_drain(application, buffers, 1);
    assert_false(drain_result);

    wrap_write_fail = false;

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_drain_overflow_fails_write(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_log_buffer_t* buffer = NULL;
    bool buffer_created = bc_runtime_log_buffer_create(application, 16, &buffer);
    assert_true(buffer_created);

    bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR, "this message is way too long for a 16 byte buffer and will overflow");
    assert_true(buffer->overflow_count > 0);

    wrap_write_fail = true;

    bc_runtime_log_buffer_t* buffers[] = {buffer};
    bool drain_result = bc_runtime_log_drain(application, buffers, 1);
    assert_false(drain_result);

    wrap_write_fail = false;

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_drain_overflow_fails_clock_gettime(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_log_buffer_t* buffer = NULL;
    bool buffer_created = bc_runtime_log_buffer_create(application, 16, &buffer);
    assert_true(buffer_created);

    bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR, "this message is way too long for a 16 byte buffer and will overflow");
    assert_true(buffer->overflow_count > 0);

    wrap_clock_gettime_fail = true;

    bc_runtime_log_buffer_t* buffers[] = {buffer};
    bool drain_result = bc_runtime_log_drain(application, buffers, 1);
    assert_false(drain_result);

    wrap_clock_gettime_fail = false;

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_commun_length_fails(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    wrap_commun_length_fail_at = wrap_commun_length_call_count + 1;

    bool result = bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_ERROR, "should fail length");
    assert_false(result);

    bc_runtime_destroy(application);
}

static void test_log_to_buffer_commun_length_fails(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_log_buffer_t* buffer = NULL;
    bool buffer_created = bc_runtime_log_buffer_create(application, 8192, &buffer);
    assert_true(buffer_created);

    wrap_commun_length_fail_at = wrap_commun_length_call_count + 1;

    bool result = bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR, "should fail length");
    assert_false(result);

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_buffer_create_pool_allocate_buffer_struct_fails(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    wrap_pool_allocate_fail_at = wrap_pool_allocate_call_count + 1;

    bc_runtime_log_buffer_t* buffer = NULL;
    bool result = bc_runtime_log_buffer_create(application, 4096, &buffer);
    assert_false(result);

    bc_runtime_destroy(application);
}

static void test_log_buffer_create_pool_allocate_data_fails(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    wrap_pool_allocate_fail_at = wrap_pool_allocate_call_count + 2;

    bc_runtime_log_buffer_t* buffer = NULL;
    bool result = bc_runtime_log_buffer_create(application, 4096, &buffer);
    assert_false(result);

    bc_runtime_destroy(application);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_log_fails_clock_gettime, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_fails_write, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_to_buffer_fails_clock_gettime, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_drain_fails_write, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_drain_overflow_fails_write, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_drain_overflow_fails_clock_gettime, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_commun_length_fails, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_to_buffer_commun_length_fails, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_buffer_create_pool_allocate_buffer_struct_fails, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_buffer_create_pool_allocate_data_fails, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
