// SPDX-License-Identifier: MIT
#include "bc_allocators.h"
#include "bc_runtime_internal.h"
#include "bc_core.h"

#include <string.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>

struct test_fixture {
    bc_allocators_context_t* memory_context;
    bc_runtime_config_store_t* config_store;
    bc_runtime_t application;
};

static int group_setup(void** state)
{
    struct test_fixture* fixture = test_calloc(1, sizeof(*fixture));
    bc_allocators_context_config_t config = {.tracking_enabled = true};
    if (!bc_allocators_context_create(&config, &fixture->memory_context)) {
        test_free(fixture);
        return -1;
    }

    if (!bc_runtime_config_store_create(fixture->memory_context, &fixture->config_store)) {
        bc_allocators_context_destroy(fixture->memory_context);
        test_free(fixture);
        return -1;
    }

    bc_runtime_config_store_set(fixture->config_store, "name", "alice");
    bc_runtime_config_store_set(fixture->config_store, "count", "42");
    bc_runtime_config_store_set(fixture->config_store, "negative", "-17");
    bc_runtime_config_store_set(fixture->config_store, "zero", "0");
    bc_runtime_config_store_set(fixture->config_store, "invalid_int", "abc");
    bc_runtime_config_store_set(fixture->config_store, "overflow_int", "99999999999999999999");
    bc_runtime_config_store_set(fixture->config_store, "empty_val", "");
    bc_runtime_config_store_set(fixture->config_store, "bool_true", "true");
    bc_runtime_config_store_set(fixture->config_store, "bool_one", "1");
    bc_runtime_config_store_set(fixture->config_store, "bool_yes", "yes");
    bc_runtime_config_store_set(fixture->config_store, "bool_TRUE", "TRUE");
    bc_runtime_config_store_set(fixture->config_store, "bool_True", "True");
    bc_runtime_config_store_set(fixture->config_store, "bool_false", "false");
    bc_runtime_config_store_set(fixture->config_store, "bool_zero", "0");
    bc_runtime_config_store_set(fixture->config_store, "bool_no", "no");
    bc_runtime_config_store_set(fixture->config_store, "bool_FALSE", "FALSE");
    bc_runtime_config_store_set(fixture->config_store, "bool_False", "False");
    bc_runtime_config_store_set(fixture->config_store, "bool_invalid", "maybe");

    bc_runtime_config_store_sort(fixture->config_store);

    bc_core_zero(&fixture->application, sizeof(fixture->application));
    fixture->application.config_store = fixture->config_store;

    *state = fixture;
    return 0;
}

static int group_teardown(void** state)
{
    struct test_fixture* fixture = *state;
    bc_runtime_config_store_destroy(fixture->memory_context, fixture->config_store);
    bc_allocators_context_destroy(fixture->memory_context);
    test_free(fixture);
    return 0;
}

static void test_config_get_string_found(void** state)
{
    struct test_fixture* fixture = *state;
    const char* value = NULL;

    assert_true(bc_runtime_config_get_string(&fixture->application, "name", &value));
    assert_int_equal(0, strcmp(value, "alice"));
}

static void test_config_get_string_not_found(void** state)
{
    struct test_fixture* fixture = *state;
    const char* value = NULL;

    assert_false(bc_runtime_config_get_string(&fixture->application, "missing", &value));
}

static void test_config_get_integer_valid(void** state)
{
    struct test_fixture* fixture = *state;
    long value = 0;

    assert_true(bc_runtime_config_get_integer(&fixture->application, "count", &value));
    assert_int_equal(42, value);
}

static void test_config_get_integer_negative(void** state)
{
    struct test_fixture* fixture = *state;
    long value = 0;

    assert_true(bc_runtime_config_get_integer(&fixture->application, "negative", &value));
    assert_int_equal(-17, value);
}

static void test_config_get_integer_zero(void** state)
{
    struct test_fixture* fixture = *state;
    long value = -1;

    assert_true(bc_runtime_config_get_integer(&fixture->application, "zero", &value));
    assert_int_equal(0, value);
}

static void test_config_get_integer_invalid(void** state)
{
    struct test_fixture* fixture = *state;
    long value = 0;

    assert_false(bc_runtime_config_get_integer(&fixture->application, "invalid_int", &value));
}

static void test_config_get_integer_overflow(void** state)
{
    struct test_fixture* fixture = *state;
    long value = 0;

    assert_false(bc_runtime_config_get_integer(&fixture->application, "overflow_int", &value));
}

static void test_config_get_integer_empty(void** state)
{
    struct test_fixture* fixture = *state;
    long value = 0;

    assert_false(bc_runtime_config_get_integer(&fixture->application, "empty_val", &value));
}

static void test_config_get_boolean_true_variants(void** state)
{
    struct test_fixture* fixture = *state;
    bool value = false;

    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_true", &value));
    assert_true(value);

    value = false;
    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_one", &value));
    assert_true(value);

    value = false;
    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_yes", &value));
    assert_true(value);

    value = false;
    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_TRUE", &value));
    assert_true(value);

    value = false;
    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_True", &value));
    assert_true(value);
}

static void test_config_get_boolean_false_variants(void** state)
{
    struct test_fixture* fixture = *state;
    bool value = true;

    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_false", &value));
    assert_false(value);

    value = true;
    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_zero", &value));
    assert_false(value);

    value = true;
    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_no", &value));
    assert_false(value);

    value = true;
    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_FALSE", &value));
    assert_false(value);

    value = true;
    assert_true(bc_runtime_config_get_boolean(&fixture->application, "bool_False", &value));
    assert_false(value);
}

static void test_config_get_boolean_invalid(void** state)
{
    struct test_fixture* fixture = *state;
    bool value = false;

    assert_false(bc_runtime_config_get_boolean(&fixture->application, "bool_invalid", &value));
}

static void test_config_get_boolean_not_found(void** state)
{
    struct test_fixture* fixture = *state;
    bool value = false;

    assert_false(bc_runtime_config_get_boolean(&fixture->application, "nonexistent", &value));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_get_string_found),          cmocka_unit_test(test_config_get_string_not_found),
        cmocka_unit_test(test_config_get_integer_valid),         cmocka_unit_test(test_config_get_integer_negative),
        cmocka_unit_test(test_config_get_integer_zero),          cmocka_unit_test(test_config_get_integer_invalid),
        cmocka_unit_test(test_config_get_integer_overflow),      cmocka_unit_test(test_config_get_integer_empty),
        cmocka_unit_test(test_config_get_boolean_true_variants), cmocka_unit_test(test_config_get_boolean_false_variants),
        cmocka_unit_test(test_config_get_boolean_invalid),       cmocka_unit_test(test_config_get_boolean_not_found),
    };

    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
