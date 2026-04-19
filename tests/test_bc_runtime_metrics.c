// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
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

static void stderr_capture_stop(struct stderr_capture* capture)
{
    dup2(capture->saved_stderr_fd, STDERR_FILENO);
    close(capture->saved_stderr_fd);
    close(capture->pipe_fds[1]);
    close(capture->pipe_fds[0]);
}

static void test_get_metrics_state_after_create(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .memory_tracking_enabled = true,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_metrics_t metrics = {0};
    bool got_metrics = bc_runtime_get_metrics(application, &metrics);
    assert_true(got_metrics);
    assert_int_equal(metrics.state, BC_RUNTIME_STATE_CREATED);

    bc_runtime_destroy(application);
}

static bool noop_run_callback(const bc_runtime_t* application, void* user_data)
{
    (void)application;
    (void)user_data;
    return true;
}

static void test_get_metrics_state_after_run(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .memory_tracking_enabled = true,
    };
    bc_runtime_callbacks_t callbacks = {
        .run = noop_run_callback,
    };
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bool run_result = bc_runtime_run(application);
    assert_true(run_result);

    bc_runtime_metrics_t metrics = {0};
    bool got_metrics = bc_runtime_get_metrics(application, &metrics);
    assert_true(got_metrics);
    assert_int_equal(metrics.state, BC_RUNTIME_STATE_STOPPED);

    bc_runtime_destroy(application);
}

static void test_get_metrics_config_entries_count(void** state)
{
    (void)state;

    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "/tmp/bc_test_metrics_config_XXXXXX");
    int fd = mkstemp(temp_path);
    assert_true(fd >= 0);
    const char* content = "alpha = one\nbeta = two\ngamma = three\n";
    write(fd, content, strlen(content));
    close(fd);

    bc_runtime_config_t config = {
        .memory_tracking_enabled = true,
        .config_file_path = temp_path,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_metrics_t metrics = {0};
    bool got_metrics = bc_runtime_get_metrics(application, &metrics);
    assert_true(got_metrics);
    assert_int_equal(metrics.config_entries_count, application->config_store->entry_count);
    assert_true(metrics.config_entries_count >= 3);

    bc_runtime_destroy(application);
    unlink(temp_path);
}

static void test_get_metrics_log_messages_written(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .memory_tracking_enabled = true,
        .log_level = BC_RUNTIME_LOG_LEVEL_DEBUG,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    struct stderr_capture capture;
    stderr_capture_start(&capture);

    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_ERROR, "message one");
    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_WARN, "message two");
    bc_runtime_log(application, BC_RUNTIME_LOG_LEVEL_INFO, "message three");

    stderr_capture_stop(&capture);

    bc_runtime_metrics_t metrics = {0};
    bool got_metrics = bc_runtime_get_metrics(application, &metrics);
    assert_true(got_metrics);
    assert_int_equal(metrics.log_messages_written, 3);

    bc_runtime_destroy(application);
}

static void test_get_metrics_memory_stats_coherent(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .memory_tracking_enabled = true,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_metrics_t metrics = {0};
    bool got_metrics = bc_runtime_get_metrics(application, &metrics);
    assert_true(got_metrics);
    assert_true(metrics.memory_stats.pool_alloc_count > 0);

    bc_runtime_destroy(application);
}

static void test_get_metrics_parallel_stats_coherent(void** state)
{
    (void)state;

    bc_runtime_config_t config = {
        .memory_tracking_enabled = true,
    };
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* application = NULL;

    bool created = bc_runtime_create(&config, &callbacks, NULL, &application);
    assert_true(created);

    bc_runtime_metrics_t metrics = {0};
    bool got_metrics = bc_runtime_get_metrics(application, &metrics);
    assert_true(got_metrics);
    assert_true(metrics.parallel_thread_count > 0);

    bc_runtime_destroy(application);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_get_metrics_state_after_create),    cmocka_unit_test(test_get_metrics_state_after_run),
        cmocka_unit_test(test_get_metrics_config_entries_count),  cmocka_unit_test(test_get_metrics_log_messages_written),
        cmocka_unit_test(test_get_metrics_memory_stats_coherent), cmocka_unit_test(test_get_metrics_parallel_stats_coherent),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
