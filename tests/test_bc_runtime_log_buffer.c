// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include <fcntl.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

struct stderr_capture {
    int saved_stderr_fd;
    int pipe_fds[2];
};

static void stderr_capture_start(struct stderr_capture* capture)
{
    capture->saved_stderr_fd = dup(STDERR_FILENO);
    pipe2(capture->pipe_fds, O_NONBLOCK);
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

static bc_runtime_t* create_test_application(void)
{
    bc_runtime_config_t config = {
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;
    bc_runtime_create(&config, &callbacks, NULL, &application);
    return application;
}

static void test_log_buffer_create_destroy(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_buffer_t* buffer = NULL;
    bool created = bc_runtime_log_buffer_create(application, 4096, &buffer);
    assert_true(created);
    assert_non_null(buffer);

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_to_buffer_writes_formatted(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_buffer_t* buffer = NULL;
    bc_runtime_log_buffer_create(application, 4096, &buffer);
    assert_non_null(buffer);

    bool result = bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_INFO, "test buffer message");
    assert_true(result);
    assert_true(buffer->write_position > 0);
    assert_int_equal(buffer->entry_count, 1);

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_to_buffer_filtered(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_buffer_t* buffer = NULL;
    bc_runtime_log_buffer_create(application, 4096, &buffer);
    assert_non_null(buffer);

    atomic_store_explicit(&buffer->log_level, (int)BC_RUNTIME_LOG_LEVEL_ERROR, memory_order_relaxed);

    bool result = bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_INFO, "should be filtered");
    assert_true(result);
    assert_int_equal(buffer->entry_count, 0);
    assert_int_equal(buffer->write_position, 0);

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_to_buffer_overflow(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_buffer_t* buffer = NULL;
    bc_runtime_log_buffer_create(application, 256, &buffer);
    assert_non_null(buffer);

    for (int i = 0; i < 20; i++) {
        bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR,
                                 "overflow test message that is long enough to fill the buffer quickly");
    }

    assert_true(buffer->overflow_count > 0);

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_drain_writes_to_stderr(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_buffer_t* buffer = NULL;
    bc_runtime_log_buffer_create(application, 4096, &buffer);
    assert_non_null(buffer);

    bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR, "drain test message");

    bc_runtime_log_buffer_t* const buffers[] = {buffer};

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bool drained = bc_runtime_log_drain(application, buffers, 1);

    char captured_output[8192];
    size_t captured_length = stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_true(drained);
    assert_true(captured_length > 0);
    assert_non_null(strstr(captured_output, "drain test message"));

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_drain_overflow_warning(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_buffer_t* buffer = NULL;
    bc_runtime_log_buffer_create(application, 256, &buffer);
    assert_non_null(buffer);

    for (int i = 0; i < 20; i++) {
        bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR, "fill up the buffer until it overflows");
    }
    assert_true(buffer->overflow_count > 0);

    bc_runtime_log_buffer_t* const buffers[] = {buffer};

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bc_runtime_log_drain(application, buffers, 1);

    char captured_output[8192];
    stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_non_null(strstr(captured_output, "lost"));

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_drain_resets_buffer(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_buffer_t* buffer = NULL;
    bc_runtime_log_buffer_create(application, 4096, &buffer);
    assert_non_null(buffer);

    bc_runtime_log_to_buffer(buffer, BC_RUNTIME_LOG_LEVEL_ERROR, "will be drained");

    bc_runtime_log_buffer_t* const buffers[] = {buffer};

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bc_runtime_log_drain(application, buffers, 1);

    char captured_output[4096];
    stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_int_equal(buffer->write_position, 0);
    assert_int_equal(buffer->entry_count, 0);
    assert_int_equal(buffer->overflow_count, 0);

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

static void test_log_drain_multiple_buffers(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_buffer_t* buffer_first = NULL;
    bc_runtime_log_buffer_t* buffer_second = NULL;
    bc_runtime_log_buffer_create(application, 4096, &buffer_first);
    bc_runtime_log_buffer_create(application, 4096, &buffer_second);
    assert_non_null(buffer_first);
    assert_non_null(buffer_second);

    bc_runtime_log_to_buffer(buffer_first, BC_RUNTIME_LOG_LEVEL_ERROR, "from first buffer");
    bc_runtime_log_to_buffer(buffer_second, BC_RUNTIME_LOG_LEVEL_ERROR, "from second buffer");

    bc_runtime_log_buffer_t* const buffers[] = {buffer_first, buffer_second};

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bool drained = bc_runtime_log_drain(application, buffers, 2);

    char captured_output[8192];
    stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_true(drained);
    assert_non_null(strstr(captured_output, "from first buffer"));
    assert_non_null(strstr(captured_output, "from second buffer"));

    bc_runtime_log_buffer_destroy(buffer_first);
    bc_runtime_log_buffer_destroy(buffer_second);
    bc_runtime_destroy(application);
}

static void test_log_drain_empty_buffer(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_buffer_t* buffer = NULL;
    bc_runtime_log_buffer_create(application, 4096, &buffer);
    assert_non_null(buffer);

    bc_runtime_log_buffer_t* const buffers[] = {buffer};

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bool drained = bc_runtime_log_drain(application, buffers, 1);

    char captured_output[4096];
    size_t captured_length = stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_true(drained);
    assert_int_equal(captured_length, 0);

    bc_runtime_log_buffer_destroy(buffer);
    bc_runtime_destroy(application);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_log_buffer_create_destroy),  cmocka_unit_test(test_log_to_buffer_writes_formatted),
        cmocka_unit_test(test_log_to_buffer_filtered),     cmocka_unit_test(test_log_to_buffer_overflow),
        cmocka_unit_test(test_log_drain_writes_to_stderr), cmocka_unit_test(test_log_drain_overflow_warning),
        cmocka_unit_test(test_log_drain_resets_buffer),    cmocka_unit_test(test_log_drain_multiple_buffers),
        cmocka_unit_test(test_log_drain_empty_buffer),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
