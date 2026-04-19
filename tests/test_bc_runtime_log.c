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

static void test_log_filtered_below_level(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_set_level(application, BC_RUNTIME_LOG_LEVEL_ERROR);

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bool result = bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_INFO, "should be filtered");

    char captured_output[4096];
    size_t captured_length = stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_true(result);
    assert_int_equal(captured_length, 0);
    assert_int_equal(atomic_load_explicit(&application->log_messages_written, memory_order_relaxed), 0);

    bc_runtime_destroy(application);
}

static void test_log_not_filtered_at_level(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_set_level(application, BC_RUNTIME_LOG_LEVEL_INFO);

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bool result = bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_INFO, "should pass through");

    char captured_output[4096];
    size_t captured_length = stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_true(result);
    assert_true(captured_length > 0);
    assert_int_equal(atomic_load_explicit(&application->log_messages_written, memory_order_relaxed), 1);

    bc_runtime_destroy(application);
}

static void test_log_format_contains_level_tag(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_set_level(application, BC_RUNTIME_LOG_LEVEL_ERROR);

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_ERROR, "test error message");

    char captured_output[4096];
    stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_non_null(strstr(captured_output, "[ERROR]"));
    assert_non_null(strstr(captured_output, "test error message"));

    bc_runtime_destroy(application);
}

static void test_log_format_contains_timestamp(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_set_level(application, BC_RUNTIME_LOG_LEVEL_DEBUG);

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_DEBUG, "timestamp test");

    char captured_output[4096];
    stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_non_null(strstr(captured_output, "[20"));

    bc_runtime_destroy(application);
}

static void test_log_set_level_changes_filtering(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_set_level(application, BC_RUNTIME_LOG_LEVEL_ERROR);

    struct stderr_capture capture_first;
    stderr_capture_start(&capture_first);

    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_DEBUG, "should be filtered");

    char first_output[4096];
    size_t first_length = stderr_capture_stop(&capture_first, first_output, sizeof(first_output));
    assert_int_equal(first_length, 0);

    bc_runtime_log_set_level(application, BC_RUNTIME_LOG_LEVEL_DEBUG);

    struct stderr_capture capture_second;
    stderr_capture_start(&capture_second);

    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_DEBUG, "should pass now");

    char second_output[4096];
    size_t second_length = stderr_capture_stop(&capture_second, second_output, sizeof(second_output));
    assert_true(second_length > 0);

    bc_runtime_destroy(application);
}

static void test_log_messages_written_counter(void** state)
{
    (void)state;

    bc_runtime_t* application = create_test_application();
    assert_non_null(application);

    bc_runtime_log_set_level(application, BC_RUNTIME_LOG_LEVEL_DEBUG);

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_ERROR, "message one");
    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_WARN, "message two");
    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_INFO, "message three");

    char captured_output[8192];
    stderr_capture_stop(&capture, captured_output, sizeof(captured_output));

    assert_int_equal(atomic_load_explicit(&application->log_messages_written, memory_order_relaxed), 3);

    bc_runtime_destroy(application);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_log_filtered_below_level),        cmocka_unit_test(test_log_not_filtered_at_level),
        cmocka_unit_test(test_log_format_contains_level_tag),   cmocka_unit_test(test_log_format_contains_timestamp),
        cmocka_unit_test(test_log_set_level_changes_filtering), cmocka_unit_test(test_log_messages_written_counter),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
