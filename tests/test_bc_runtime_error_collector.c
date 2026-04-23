// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_runtime_error_collector.h"

#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmocka.h>

typedef struct collector_fixture {
    bc_allocators_context_t* memory_context;
    bc_runtime_error_collector_t* collector;
} collector_fixture_t;

static int setup_collector(void** state)
{
    collector_fixture_t* fixture = calloc(1, sizeof(*fixture));
    assert_non_null(fixture);
    bc_allocators_context_config_t memory_config = {0};
    assert_true(bc_allocators_context_create(&memory_config, &fixture->memory_context));
    assert_true(bc_runtime_error_collector_create(fixture->memory_context, &fixture->collector));
    *state = fixture;
    return 0;
}

static int teardown_collector(void** state)
{
    collector_fixture_t* fixture = *state;
    bc_runtime_error_collector_destroy(fixture->memory_context, fixture->collector);
    bc_allocators_context_destroy(fixture->memory_context);
    free(fixture);
    return 0;
}

static void test_count_is_zero_on_fresh_collector(void** state)
{
    const collector_fixture_t* fixture = *state;
    assert_int_equal(bc_runtime_error_collector_count(fixture->collector), 0U);
}

static void test_append_single_record_increments_count_to_one(void** state)
{
    const collector_fixture_t* fixture = *state;
    assert_true(bc_runtime_error_collector_append(
        fixture->collector, fixture->memory_context, "/tmp/foo", "open", ENOENT));
    assert_int_equal(bc_runtime_error_collector_count(fixture->collector), 1U);
}

static void test_append_multiple_records_increments_count(void** state)
{
    const collector_fixture_t* fixture = *state;
    for (size_t index = 0U; index < 17U; ++index) {
        char path[32];
        snprintf(path, sizeof(path), "/tmp/file_%zu", index);
        assert_true(bc_runtime_error_collector_append(
            fixture->collector, fixture->memory_context, path, "read", EIO));
    }
    assert_int_equal(bc_runtime_error_collector_count(fixture->collector), 17U);
}

static void test_append_truncates_excessively_long_path(void** state)
{
    const collector_fixture_t* fixture = *state;
    char very_long_path[4096];
    memset(very_long_path, 'x', sizeof(very_long_path) - 1U);
    very_long_path[sizeof(very_long_path) - 1U] = '\0';
    assert_true(bc_runtime_error_collector_append(
        fixture->collector, fixture->memory_context, very_long_path, "stat", EACCES));
    assert_int_equal(bc_runtime_error_collector_count(fixture->collector), 1U);
}

static void test_append_accepts_null_stage(void** state)
{
    const collector_fixture_t* fixture = *state;
    assert_true(bc_runtime_error_collector_append(
        fixture->collector, fixture->memory_context, "/tmp/path", NULL, ENOENT));
    assert_int_equal(bc_runtime_error_collector_count(fixture->collector), 1U);
}

static void test_flush_to_stderr_emits_one_line_per_record(void** state)
{
    const collector_fixture_t* fixture = *state;
    assert_true(bc_runtime_error_collector_append(
        fixture->collector, fixture->memory_context, "/a", "open", ENOENT));
    assert_true(bc_runtime_error_collector_append(
        fixture->collector, fixture->memory_context, "/b", "read", EIO));

    int pipe_fds[2];
    assert_int_equal(pipe(pipe_fds), 0);
    int saved_stderr = dup(STDERR_FILENO);
    assert_true(saved_stderr >= 0);
    assert_true(dup2(pipe_fds[1], STDERR_FILENO) >= 0);
    close(pipe_fds[1]);

    bc_runtime_error_collector_flush_to_stderr(fixture->collector, "testprog");
    fflush(stderr);

    assert_true(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);

    char buffer[4096] = {0};
    ssize_t read_count = read(pipe_fds[0], buffer, sizeof(buffer) - 1U);
    assert_true(read_count > 0);
    close(pipe_fds[0]);

    size_t newline_count = 0U;
    for (ssize_t i = 0; i < read_count; ++i) {
        if (buffer[i] == '\n') {
            ++newline_count;
        }
    }
    assert_int_equal(newline_count, 2U);
    assert_non_null(strstr(buffer, "testprog"));
    assert_non_null(strstr(buffer, "/a"));
    assert_non_null(strstr(buffer, "/b"));
}

#define CONCURRENT_APPEND_THREAD_COUNT 8
#define CONCURRENT_APPEND_PER_THREAD   64

typedef struct concurrent_context {
    bc_runtime_error_collector_t* collector;
    bc_allocators_context_t* memory_context;
    pthread_barrier_t* barrier;
} concurrent_context_t;

static void* append_entry(void* argument)
{
    concurrent_context_t* context = (concurrent_context_t*)argument;
    pthread_barrier_wait(context->barrier);
    for (size_t index = 0U; index < CONCURRENT_APPEND_PER_THREAD; ++index) {
        bc_runtime_error_collector_append(
            context->collector, context->memory_context, "/concurrent", "append", EAGAIN);
    }
    return NULL;
}

static void test_concurrent_append_is_race_free(void** state)
{
    const collector_fixture_t* fixture = *state;
    pthread_barrier_t barrier;
    assert_int_equal(pthread_barrier_init(&barrier, NULL, CONCURRENT_APPEND_THREAD_COUNT), 0);

    pthread_t threads[CONCURRENT_APPEND_THREAD_COUNT];
    concurrent_context_t contexts[CONCURRENT_APPEND_THREAD_COUNT];
    for (size_t thread_index = 0U; thread_index < CONCURRENT_APPEND_THREAD_COUNT; ++thread_index) {
        contexts[thread_index].collector = fixture->collector;
        contexts[thread_index].memory_context = fixture->memory_context;
        contexts[thread_index].barrier = &barrier;
        assert_int_equal(
            pthread_create(&threads[thread_index], NULL, append_entry, &contexts[thread_index]),
            0);
    }
    for (size_t thread_index = 0U; thread_index < CONCURRENT_APPEND_THREAD_COUNT; ++thread_index) {
        assert_int_equal(pthread_join(threads[thread_index], NULL), 0);
    }
    pthread_barrier_destroy(&barrier);

    assert_int_equal(
        bc_runtime_error_collector_count(fixture->collector),
        (size_t)(CONCURRENT_APPEND_THREAD_COUNT * CONCURRENT_APPEND_PER_THREAD));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            test_count_is_zero_on_fresh_collector, setup_collector, teardown_collector),
        cmocka_unit_test_setup_teardown(
            test_append_single_record_increments_count_to_one, setup_collector, teardown_collector),
        cmocka_unit_test_setup_teardown(
            test_append_multiple_records_increments_count, setup_collector, teardown_collector),
        cmocka_unit_test_setup_teardown(
            test_append_truncates_excessively_long_path, setup_collector, teardown_collector),
        cmocka_unit_test_setup_teardown(
            test_append_accepts_null_stage, setup_collector, teardown_collector),
        cmocka_unit_test_setup_teardown(
            test_flush_to_stderr_emits_one_line_per_record, setup_collector, teardown_collector),
        cmocka_unit_test_setup_teardown(
            test_concurrent_append_is_race_free, setup_collector, teardown_collector),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
