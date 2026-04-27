// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include "bc_core.h"
#include "bc_allocators_pool.h"

#include <stdio.h>

bool bc_runtime_config_store_create(bc_allocators_context_t* memory_context, bc_runtime_config_store_t** out_store)
{
    bc_runtime_config_store_t* store = NULL;
    if (!bc_allocators_pool_allocate(memory_context, sizeof(bc_runtime_config_store_t), (void**)&store)) {
        return false;
    }

    if (!bc_allocators_arena_create(memory_context, BC_RUNTIME_CONFIG_STORE_ARENA_CAPACITY, &store->arena)) {
        bc_allocators_pool_free(memory_context, store);
        return false;
    }

    size_t entries_size = BC_RUNTIME_CONFIG_STORE_INITIAL_CAPACITY * sizeof(bc_runtime_config_entry_t);
    if (!bc_allocators_pool_allocate(memory_context, entries_size, (void**)&store->entries)) {
        bc_allocators_arena_destroy(store->arena);
        bc_allocators_pool_free(memory_context, store);
        return false;
    }

    store->memory_context = memory_context;
    store->entry_count = 0;
    store->entry_capacity = BC_RUNTIME_CONFIG_STORE_INITIAL_CAPACITY;
    store->sorted = false;

    *out_store = store;
    return true;
}

void bc_runtime_config_store_destroy(bc_allocators_context_t* memory_context, bc_runtime_config_store_t* store)
{
    bc_allocators_arena_destroy(store->arena);
    bc_allocators_pool_free(memory_context, store->entries);
    bc_allocators_pool_free(memory_context, store);
}

bool bc_runtime_config_store_set(bc_runtime_config_store_t* store, const char* key, const char* value)
{
    size_t key_length = 0;
    if (!bc_core_length(key, 0, &key_length)) {
        return false;
    }

    for (size_t i = 0; i < store->entry_count; i++) {
        if (store->entries[i].key_length != key_length) {
            continue;
        }
        bool keys_equal = false;
        if (!bc_core_equal(store->entries[i].key, key, key_length, &keys_equal)) {
            return false;
        }
        if (keys_equal) {
            const char* copied_value = NULL;
            if (!bc_allocators_arena_copy_string(store->arena, value, &copied_value)) {
                return false;
            }
            store->entries[i].value = copied_value;
            return true;
        }
    }

    if (store->entry_count == store->entry_capacity) {
        size_t new_capacity = store->entry_capacity * 2;
        size_t new_size = new_capacity * sizeof(bc_runtime_config_entry_t);
        bc_runtime_config_entry_t* new_entries = NULL;
        if (!bc_allocators_pool_reallocate(store->memory_context, store->entries, new_size, (void**)&new_entries)) {
            return false;
        }
        store->entries = new_entries;
        store->entry_capacity = new_capacity;
    }

    const char* copied_key = NULL;
    if (!bc_allocators_arena_copy_string(store->arena, key, &copied_key)) {
        return false;
    }

    const char* copied_value = NULL;
    if (!bc_allocators_arena_copy_string(store->arena, value, &copied_value)) {
        return false;
    }

    store->entries[store->entry_count].key = copied_key;
    store->entries[store->entry_count].key_length = key_length;
    store->entries[store->entry_count].value = copied_value;
    store->entry_count++;
    store->sorted = false;

    return true;
}

bool bc_runtime_config_store_append(bc_runtime_config_store_t* store, const char* key, const char* value, char separator)
{
    size_t key_length = 0;
    if (!bc_core_length(key, 0, &key_length)) {
        return false;
    }

    size_t value_length = 0;
    if (!bc_core_length(value, 0, &value_length)) {
        return false;
    }

    for (size_t i = 0; i < store->entry_count; i++) {
        if (store->entries[i].key_length != key_length) {
            continue;
        }
        bool keys_equal = false;
        if (!bc_core_equal(store->entries[i].key, key, key_length, &keys_equal)) {
            return false;
        }
        if (!keys_equal) {
            continue;
        }
        const char* existing = store->entries[i].value;
        size_t existing_length = 0;
        if (!bc_core_length(existing, 0, &existing_length)) {
            return false;
        }
        size_t combined_length = existing_length + 1 + value_length;
        char* combined_buffer = NULL;
        if (!bc_allocators_pool_allocate(store->memory_context, combined_length + 1, (void**)&combined_buffer)) {
            return false;
        }
        bc_core_copy(combined_buffer, existing, existing_length);
        combined_buffer[existing_length] = separator;
        bc_core_copy(combined_buffer + existing_length + 1, value, value_length);
        combined_buffer[combined_length] = '\0';

        const char* copied_value = NULL;
        if (!bc_allocators_arena_copy_string(store->arena, combined_buffer, &copied_value)) {
            bc_allocators_pool_free(store->memory_context, combined_buffer);
            return false;
        }
        bc_allocators_pool_free(store->memory_context, combined_buffer);
        store->entries[i].value = copied_value;
        return true;
    }

    return bc_runtime_config_store_set(store, key, value);
}

static int config_entry_compare(const bc_runtime_config_entry_t* entry_a, const bc_runtime_config_entry_t* entry_b)
{
    size_t min_length = entry_a->key_length;
    if (entry_b->key_length < min_length) {
        min_length = entry_b->key_length;
    }

    int comparison_result = 0;
    bc_core_compare(entry_a->key, entry_b->key, min_length, &comparison_result);

    if (comparison_result != 0) {
        return comparison_result;
    }

    if (entry_a->key_length < entry_b->key_length) {
        return -1;
    }
    if (entry_a->key_length > entry_b->key_length) {
        return 1;
    }
    return 0;
}

static void merge_entries(bc_runtime_config_entry_t* entries, bc_runtime_config_entry_t* temp_buffer, size_t left_start, size_t right_start,
                          size_t right_end)
{
    size_t left_index = left_start;
    size_t right_index = right_start;
    size_t write_index = left_start;

    while (left_index < right_start && right_index < right_end) {
        int comparison = config_entry_compare(&entries[left_index], &entries[right_index]);
        if (comparison <= 0) {
            temp_buffer[write_index] = entries[left_index];
            left_index++;
        } else {
            temp_buffer[write_index] = entries[right_index];
            right_index++;
        }
        write_index++;
    }

    while (left_index < right_start) {
        temp_buffer[write_index] = entries[left_index];
        left_index++;
        write_index++;
    }

    while (right_index < right_end) {
        temp_buffer[write_index] = entries[right_index];
        right_index++;
        write_index++;
    }
}

bool bc_runtime_config_store_sort(bc_runtime_config_store_t* store)
{
    if (store->entry_count <= 1) {
        store->sorted = true;
        return true;
    }

    size_t temp_size = store->entry_count * sizeof(bc_runtime_config_entry_t);
    bc_runtime_config_entry_t* temp_buffer = NULL;
    if (!bc_allocators_pool_allocate(store->memory_context, temp_size, (void**)&temp_buffer)) {
        return false;
    }

    size_t entry_count = store->entry_count;

    for (size_t width = 1; width < entry_count; width *= 2) {
        for (size_t i = 0; i < entry_count; i += 2 * width) {
            size_t right_start = i + width;
            if (right_start > entry_count) {
                right_start = entry_count;
            }
            size_t right_end = i + 2 * width;
            if (right_end > entry_count) {
                right_end = entry_count;
            }
            merge_entries(store->entries, temp_buffer, i, right_start, right_end);
        }
        bc_core_copy(store->entries, temp_buffer, temp_size);
    }

    bc_allocators_pool_free(store->memory_context, temp_buffer);

    store->sorted = true;
    return true;
}

bool bc_runtime_config_store_lookup(const bc_runtime_config_store_t* store, const char* key, const char** out_value)
{
    if (!store->sorted) {
        return false;
    }

    size_t key_length = 0;
    if (!bc_core_length(key, 0, &key_length)) {
        return false;
    }

    size_t low = 0;
    size_t high = store->entry_count;

    while (low < high) {
        size_t mid = low + (high - low) / 2;
        const bc_runtime_config_entry_t* entry = &store->entries[mid];

        size_t min_length = entry->key_length;
        if (key_length < min_length) {
            min_length = key_length;
        }

        int comparison = 0;
        bc_core_compare(entry->key, key, min_length, &comparison);

        if (comparison == 0) {
            if (entry->key_length < key_length) {
                comparison = -1;
            } else if (entry->key_length > key_length) {
                comparison = 1;
            }
        }

        if (comparison == 0) {
            *out_value = entry->value;
            return true;
        }

        if (comparison < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return false;
}

static void parse_config_line(bc_runtime_config_store_t* store, const char* line_buffer, size_t line_length)
{
    size_t start = 0;
    while (start < line_length && (line_buffer[start] == ' ' || line_buffer[start] == '\t')) {
        start++;
    }

    if (start == line_length) {
        return;
    }

    if (line_buffer[start] == '#') {
        return;
    }

    size_t remaining_length = line_length - start;
    const char* remaining = line_buffer + start;

    size_t equals_offset = 0;
    if (!bc_core_find_byte(remaining, remaining_length, '=', &equals_offset)) {
        return;
    }

    size_t key_end = equals_offset;
    while (key_end > 0 && (remaining[key_end - 1] == ' ' || remaining[key_end - 1] == '\t')) {
        key_end--;
    }

    if (key_end == 0) {
        return;
    }

    size_t value_start = equals_offset + 1;
    while (value_start < remaining_length && (remaining[value_start] == ' ' || remaining[value_start] == '\t')) {
        value_start++;
    }

    size_t value_end = remaining_length;
    while (value_end > value_start && (remaining[value_end - 1] == ' ' || remaining[value_end - 1] == '\t')) {
        value_end--;
    }

    char key_buffer[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];
    char value_buffer[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];

    size_t value_length = value_end - value_start;

    bc_core_copy(key_buffer, remaining, key_end);
    key_buffer[key_end] = '\0';

    bc_core_copy(value_buffer, remaining + value_start, value_length);
    value_buffer[value_length] = '\0';

    bc_runtime_config_store_set(store, key_buffer, value_buffer);
}

bool bc_runtime_config_load_from_buffer(bc_runtime_config_store_t* store, const char* data, size_t size)
{
    char line_buffer[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];
    size_t line_buffer_length = 0;

    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\n') {
            if (line_buffer_length > 0) {
                parse_config_line(store, line_buffer, line_buffer_length);
            }
            line_buffer_length = 0;
        } else {
            if (line_buffer_length < sizeof(line_buffer) - 1) {
                line_buffer[line_buffer_length] = data[i];
                line_buffer_length++;
            }
        }
    }

    if (line_buffer_length > 0) {
        parse_config_line(store, line_buffer, line_buffer_length);
    }

    return true;
}

bool bc_runtime_config_load_file(bc_runtime_config_store_t* store, bc_allocators_context_t* memory_context, const char* file_path)
{
    FILE* file = fopen(file_path, "rb");
    if (file == NULL) {
        return false;
    }

    size_t buffer_capacity = BC_RUNTIME_CONFIG_STORE_ARENA_CAPACITY;
    char* file_buffer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, buffer_capacity, (void**)&file_buffer)) {
        fclose(file);
        return false;
    }

    size_t total_size = 0;
    for (;;) {
        if (total_size == buffer_capacity) {
            size_t new_capacity = buffer_capacity * 2;
            char* new_buffer = NULL;
            if (!bc_allocators_pool_reallocate(memory_context, file_buffer, new_capacity, (void**)&new_buffer)) {
                bc_allocators_pool_free(memory_context, file_buffer);
                fclose(file);
                return false;
            }
            file_buffer = new_buffer;
            buffer_capacity = new_capacity;
        }
        size_t read_now = fread(file_buffer + total_size, 1, buffer_capacity - total_size, file);
        if (read_now == 0) {
            break;
        }
        total_size += read_now;
    }

    fclose(file);

    if (total_size > 0) {
        bc_runtime_config_load_from_buffer(store, file_buffer, total_size);
    }

    bc_allocators_pool_free(memory_context, file_buffer);
    return true;
}

bool bc_runtime_config_load_environment(bc_runtime_config_store_t* store)
{
    extern char** environ;

    for (size_t i = 0; environ[i] != NULL; i++) {
        const char* entry = environ[i];
        size_t entry_length = 0;
        if (!bc_core_length(entry, 0, &entry_length)) {
            continue;
        }

        if (entry_length < 8) {
            continue;
        }

        if (entry[0] != 'B' || entry[1] != 'C' || entry[2] != '_' || entry[3] != 'A' || entry[4] != 'P' || entry[5] != 'P' ||
            entry[6] != '_') {
            continue;
        }

        size_t equals_offset = 0;
        if (!bc_core_find_byte(entry, entry_length, '=', &equals_offset)) {
            continue;
        }

        size_t key_start = 7;
        size_t key_length = equals_offset - key_start;

        if (key_length == 0) {
            continue;
        }

        char key_buffer[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];
        if (key_length >= sizeof(key_buffer)) {
            key_length = sizeof(key_buffer) - 1;
        }

        for (size_t j = 0; j < key_length; j++) {
            char character = entry[key_start + j];
            if (character == '_') {
                key_buffer[j] = '.';
            } else if (character >= 'A' && character <= 'Z') {
                key_buffer[j] = (char)(character | 0x20);
            } else {
                key_buffer[j] = character;
            }
        }
        key_buffer[key_length] = '\0';

        const char* value = entry + equals_offset + 1;

        if (!bc_runtime_config_store_set(store, key_buffer, value)) {
            return false;
        }
    }

    return true;
}

bool bc_runtime_config_load_arguments(bc_runtime_config_store_t* store, int argument_count, const char* const* argument_values)
{
    for (int i = 1; i < argument_count; i++) {
        const char* argument = argument_values[i];

        size_t argument_length = 0;
        if (!bc_core_length(argument, 0, &argument_length)) {
            continue;
        }

        bool has_prefix = false;
        bc_core_starts_with(argument, argument_length, "--", 2, &has_prefix);
        if (!has_prefix) {
            continue;
        }

        const char* after_prefix = argument + 2;
        size_t after_prefix_length = argument_length - 2;

        size_t equals_offset = 0;
        if (!bc_core_find_byte(after_prefix, after_prefix_length, '=', &equals_offset)) {
            continue;
        }

        size_t key_length = equals_offset;
        if (key_length == 0) {
            continue;
        }

        char key_buffer[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];
        if (key_length >= sizeof(key_buffer)) {
            key_length = sizeof(key_buffer) - 1;
        }

        bc_core_copy(key_buffer, after_prefix, key_length);
        key_buffer[key_length] = '\0';

        const char* value = after_prefix + equals_offset + 1;

        if (!bc_runtime_config_store_set(store, key_buffer, value)) {
            return false;
        }
    }

    return true;
}

bool bc_runtime_config_get_string(const bc_runtime_t* application, const char* key, const char** out_value)
{
    return bc_runtime_config_store_lookup(application->config_store, key, out_value);
}

bool bc_runtime_config_get_integer(const bc_runtime_t* application, const char* key, long* out_value)
{
    const char* string_value = NULL;
    if (!bc_runtime_config_get_string(application, key, &string_value)) {
        return false;
    }

    size_t string_length = 0;
    if (!bc_core_length(string_value, 0, &string_length)) {
        return false;
    }

    if (string_length == 0) {
        return false;
    }

    size_t index = 0;
    bool is_negative = false;

    if (string_value[0] == '-') {
        is_negative = true;
        index = 1;
    } else if (string_value[0] == '+') {
        index = 1;
    }

    if (index == string_length) {
        return false;
    }

    size_t result = 0;

    for (size_t i = index; i < string_length; i++) {
        char character = string_value[i];
        if (character < '0' || character > '9') {
            return false;
        }

        size_t digit = (size_t)(character - '0');
        size_t multiplied = 0;
        if (!bc_core_safe_multiply(result, 10, &multiplied)) {
            return false;
        }
        size_t added = 0;
        if (!bc_core_safe_add(multiplied, digit, &added)) {
            return false;
        }

        if (is_negative) {
            if (added > (size_t)__LONG_MAX__ + 1UL) {
                return false;
            }
        } else {
            if (added > (size_t)__LONG_MAX__) {
                return false;
            }
        }

        result = added;
    }

    if (is_negative) {
        *out_value = (long)(0UL - result);
    } else {
        *out_value = (long)result;
    }

    return true;
}

bool bc_runtime_config_get_boolean(const bc_runtime_t* application, const char* key, bool* out_value)
{
    const char* string_value = NULL;
    if (!bc_runtime_config_get_string(application, key, &string_value)) {
        return false;
    }

    size_t string_length = 0;
    if (!bc_core_length(string_value, 0, &string_length)) {
        return false;
    }

    bool equals_true = false;
    bool equals_one = false;
    bool equals_yes = false;
    if (!bc_core_equal_case_insensitive_ascii(string_value, string_length, "true", 4, &equals_true) ||
        !bc_core_equal_case_insensitive_ascii(string_value, string_length, "1", 1, &equals_one) ||
        !bc_core_equal_case_insensitive_ascii(string_value, string_length, "yes", 3, &equals_yes)) {
        return false;
    }
    if (equals_true || equals_one || equals_yes) {
        *out_value = true;
        return true;
    }

    bool equals_false = false;
    bool equals_zero = false;
    bool equals_no = false;
    if (!bc_core_equal_case_insensitive_ascii(string_value, string_length, "false", 5, &equals_false) ||
        !bc_core_equal_case_insensitive_ascii(string_value, string_length, "0", 1, &equals_zero) ||
        !bc_core_equal_case_insensitive_ascii(string_value, string_length, "no", 2, &equals_no)) {
        return false;
    }
    if (equals_false || equals_zero || equals_no) {
        *out_value = false;
        return true;
    }

    return false;
}

bool bc_runtime_config_store_get_string(const bc_runtime_config_store_t* store, const char* key, const char** out_value)
{
    return bc_runtime_config_store_lookup(store, key, out_value);
}

bool bc_runtime_config_store_get_integer(const bc_runtime_config_store_t* store, const char* key, long* out_value)
{
    const char* string_value = NULL;
    if (!bc_runtime_config_store_lookup(store, key, &string_value)) {
        return false;
    }

    size_t string_length = 0;
    if (!bc_core_length(string_value, 0, &string_length)) {
        return false;
    }

    if (string_length == 0) {
        return false;
    }

    size_t index = 0;
    bool is_negative = false;

    if (string_value[0] == '-') {
        is_negative = true;
        index = 1;
    } else if (string_value[0] == '+') {
        index = 1;
    }

    if (index == string_length) {
        return false;
    }

    size_t result = 0;

    for (size_t i = index; i < string_length; i++) {
        char character = string_value[i];
        if (character < '0' || character > '9') {
            return false;
        }

        size_t digit = (size_t)(character - '0');
        size_t multiplied = 0;
        if (!bc_core_safe_multiply(result, 10, &multiplied)) {
            return false;
        }
        size_t added = 0;
        if (!bc_core_safe_add(multiplied, digit, &added)) {
            return false;
        }

        if (is_negative) {
            if (added > (size_t)__LONG_MAX__ + 1UL) {
                return false;
            }
        } else {
            if (added > (size_t)__LONG_MAX__) {
                return false;
            }
        }

        result = added;
    }

    if (is_negative) {
        *out_value = (long)(0UL - result);
    } else {
        *out_value = (long)result;
    }

    return true;
}

bool bc_runtime_config_store_get_boolean(const bc_runtime_config_store_t* store, const char* key, bool* out_value)
{
    const char* string_value = NULL;
    if (!bc_runtime_config_store_lookup(store, key, &string_value)) {
        return false;
    }

    size_t string_length = 0;
    if (!bc_core_length(string_value, 0, &string_length)) {
        return false;
    }

    bool equals_true = false;
    bool equals_one = false;
    bool equals_yes = false;
    if (!bc_core_equal_case_insensitive_ascii(string_value, string_length, "true", 4, &equals_true) ||
        !bc_core_equal_case_insensitive_ascii(string_value, string_length, "1", 1, &equals_one) ||
        !bc_core_equal_case_insensitive_ascii(string_value, string_length, "yes", 3, &equals_yes)) {
        return false;
    }
    if (equals_true || equals_one || equals_yes) {
        *out_value = true;
        return true;
    }

    bool equals_false = false;
    bool equals_zero = false;
    bool equals_no = false;
    if (!bc_core_equal_case_insensitive_ascii(string_value, string_length, "false", 5, &equals_false) ||
        !bc_core_equal_case_insensitive_ascii(string_value, string_length, "0", 1, &equals_zero) ||
        !bc_core_equal_case_insensitive_ascii(string_value, string_length, "no", 2, &equals_no)) {
        return false;
    }
    if (equals_false || equals_zero || equals_no) {
        *out_value = false;
        return true;
    }

    return false;
}
