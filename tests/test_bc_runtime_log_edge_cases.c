// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_core.h"
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

static bool wrap_localtime_r_fail = false;

struct tm* __real_localtime_r(const time_t* timep, struct tm* result);
struct tm* __wrap_localtime_r(const time_t* timep, struct tm* result)
{
    if (wrap_localtime_r_fail) {
        return NULL;
    }
    return __real_localtime_r(timep, result);
}

static int test_setup(void** state)
{
    (void)state;
    wrap_localtime_r_fail = false;
    return 0;
}

static int test_teardown(void** state)
{
    (void)state;
    return 0;
}

struct stderr_capture {
    int saved_stderr_fd;
    int pipe_fds[2];
};

static void stderr_capture_start(struct stderr_capture* capture)
{
    capture->saved_stderr_fd = dup(STDERR_FILENO);
    int _pfd_rc = pipe2(capture->pipe_fds, O_NONBLOCK); (void)_pfd_rc;
    dup2(capture->pipe_fds[1], STDERR_FILENO);
}

static size_t stderr_capture_stop(struct stderr_capture* capture, char* buffer, size_t buffer_size)
{
    dup2(capture->saved_stderr_fd, STDERR_FILENO);
    close(capture->saved_stderr_fd);
    close(capture->pipe_fds[1]);
    ssize_t bytes_read = read(capture->pipe_fds[0], buffer, buffer_size - 1);
    close(capture->pipe_fds[0]);
    if (bytes_read < 0) {
        bytes_read = 0;
    }
    buffer[bytes_read] = '\0';
    return (size_t)bytes_read;
}

static void test_log_message_truncation(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    char long_message[5000];
    bc_core_fill(long_message, sizeof(long_message) - 1, (unsigned char)'X');
    long_message[sizeof(long_message) - 1] = '\0';

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bool result = bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_ERROR, long_message);
    assert_true(result);

    char captured_output[8192];
    size_t captured_length = stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_true(captured_length > 0);
    assert_true(captured_length <= 4096);

    bc_runtime_destroy(application);
}

static void test_log_to_buffer_message_truncation(void** state)
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
    bool buffer_created = bc_runtime_log_buffer_create(application, 65536, &buffer);
    assert_true(buffer_created);

    char long_message[5000];
    bc_core_fill(long_message, sizeof(long_message) - 1, (unsigned char)'Y');
    long_message[sizeof(long_message) - 1] = '\0';

    bool result = bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR, long_message);
    assert_true(result);
    assert_true(buffer->write_position > 0);
    assert_true(buffer->write_position <= 4096);

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_format_timestamp_buffer_too_small(void** state)
{
    (void)state;

    char small_buffer[10];
    size_t out_length = 0;

    bool result = bc_runtime_log_format_timestamp(small_buffer, sizeof(small_buffer), &out_length);
    assert_false(result);
}

static void test_log_format_timestamp_localtime_r_fail(void** state)
{
    (void)state;

    wrap_localtime_r_fail = true;

    char buffer[64];
    size_t out_length = 0;

    bool result = bc_runtime_log_format_timestamp(buffer, sizeof(buffer), &out_length);
    assert_false(result);

    wrap_localtime_r_fail = false;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_log_message_truncation, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_to_buffer_message_truncation, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_format_timestamp_buffer_too_small, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_log_format_timestamp_localtime_r_fail, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
