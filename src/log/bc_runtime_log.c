// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include "bc_core.h"
#include "bc_allocators_pool.h"

#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

bool bc_runtime_log_format_timestamp(char* buffer, size_t buffer_size, size_t* out_length)
{
    if (buffer_size < 29) {
        return false;
    }

    struct timespec timestamp;
    if (clock_gettime(CLOCK_REALTIME, &timestamp) != 0) {
        return false;
    }

    struct tm time_parts;
    if (localtime_r(&timestamp.tv_sec, &time_parts) == NULL) {
        return false;
    }

    buffer[0] = '[';

    int year = time_parts.tm_year + 1900;
    buffer[1] = (char)('0' + year / 1000);
    buffer[2] = (char)('0' + (year / 100) % 10);
    buffer[3] = (char)('0' + (year / 10) % 10);
    buffer[4] = (char)('0' + year % 10);
    buffer[5] = '-';

    int month = time_parts.tm_mon + 1;
    buffer[6] = (char)('0' + month / 10);
    buffer[7] = (char)('0' + month % 10);
    buffer[8] = '-';

    buffer[9] = (char)('0' + time_parts.tm_mday / 10);
    buffer[10] = (char)('0' + time_parts.tm_mday % 10);
    buffer[11] = 'T';

    buffer[12] = (char)('0' + time_parts.tm_hour / 10);
    buffer[13] = (char)('0' + time_parts.tm_hour % 10);
    buffer[14] = ':';

    buffer[15] = (char)('0' + time_parts.tm_min / 10);
    buffer[16] = (char)('0' + time_parts.tm_min % 10);
    buffer[17] = ':';

    buffer[18] = (char)('0' + time_parts.tm_sec / 10);
    buffer[19] = (char)('0' + time_parts.tm_sec % 10);
    buffer[20] = '.';

    long microseconds = timestamp.tv_nsec / 1000;
    buffer[21] = (char)('0' + (microseconds / 100000) % 10);
    buffer[22] = (char)('0' + (microseconds / 10000) % 10);
    buffer[23] = (char)('0' + (microseconds / 1000) % 10);
    buffer[24] = (char)('0' + (microseconds / 100) % 10);
    buffer[25] = (char)('0' + (microseconds / 10) % 10);
    buffer[26] = (char)('0' + microseconds % 10);
    buffer[27] = ']';

    *out_length = 28;
    return true;
}

bool bc_runtime_log(const bc_runtime_t* application, bc_runtime_log_level_t level, const char* message)
{
    int current_level = atomic_load_explicit(&application->log_level, memory_order_relaxed);
    if ((int)level > current_level) {
        return true;
    }

    char formatted[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];
    size_t position = 0;

    size_t timestamp_length = 0;
    if (!bc_runtime_log_format_timestamp(formatted, sizeof(formatted), &timestamp_length)) {
        return false;
    }
    position = timestamp_length;

    formatted[position++] = ' ';

    static const char* const level_prefixes[] = {
        "[ERROR]",
        "[WARN ]",
        "[INFO ]",
        "[DEBUG]",
    };
    const char* prefix = level_prefixes[(int)level];
    bc_core_copy(formatted + position, prefix, 7);
    position += 7;

    formatted[position++] = ' ';

    size_t message_length = 0;
    if (!bc_core_length(message, 0, &message_length)) {
        return false;
    }

    size_t max_message = sizeof(formatted) - position - 1;
    if (message_length > max_message) {
        message_length = max_message;
    }
    bc_core_copy(formatted + position, message, message_length);
    position += message_length;

    formatted[position++] = '\n';

    ssize_t written = write(STDERR_FILENO, formatted, position);
    if (written < 0 || (size_t)written != position) {
        return false;
    }

    atomic_fetch_add_explicit(&((bc_runtime_t*)application)->log_messages_written, 1, memory_order_relaxed);
    return true;
}

bool bc_runtime_log_set_level(bc_runtime_t* application, bc_runtime_log_level_t level)
{
    atomic_store_explicit(&application->log_level, (int)level, memory_order_relaxed);
    return true;
}

bool bc_runtime_log_buffer_create(const bc_runtime_t* application, size_t capacity, bc_runtime_log_buffer_t** out_buffer)
{
    bc_runtime_log_buffer_t* buffer = NULL;
    if (!bc_allocators_pool_allocate(application->memory_context, sizeof(bc_runtime_log_buffer_t), (void**)&buffer)) {
        return false;
    }

    char* data = NULL;
    if (!bc_allocators_pool_allocate(application->memory_context, capacity, (void**)&data)) {
        bc_allocators_pool_free(application->memory_context, buffer);
        return false;
    }

    buffer->memory_context = application->memory_context;
    buffer->data = data;
    buffer->capacity = capacity;
    buffer->write_position = 0;
    buffer->entry_count = 0;
    buffer->overflow_count = 0;
    atomic_store_explicit(&buffer->log_level, atomic_load_explicit(&application->log_level, memory_order_relaxed), memory_order_relaxed);

    *out_buffer = buffer;
    return true;
}

bool bc_runtime_log_to_buffer(bc_runtime_log_buffer_t* buffer, bc_runtime_log_level_t level, const char* message)
{
    int current_level = atomic_load_explicit(&buffer->log_level, memory_order_relaxed);
    if ((int)level > current_level) {
        return true;
    }

    char formatted[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];
    size_t position = 0;

    size_t timestamp_length = 0;
    if (!bc_runtime_log_format_timestamp(formatted, sizeof(formatted), &timestamp_length)) {
        return false;
    }
    position = timestamp_length;

    formatted[position++] = ' ';

    static const char* const level_prefixes[] = {
        "[ERROR]",
        "[WARN ]",
        "[INFO ]",
        "[DEBUG]",
    };
    bc_core_copy(formatted + position, level_prefixes[(int)level], 7);
    position += 7;

    formatted[position++] = ' ';

    size_t message_length = 0;
    if (!bc_core_length(message, 0, &message_length)) {
        return false;
    }
    size_t max_message = sizeof(formatted) - position - 1;
    if (message_length > max_message) {
        message_length = max_message;
    }
    bc_core_copy(formatted + position, message, message_length);
    position += message_length;

    formatted[position++] = '\n';

    if (buffer->write_position + position > buffer->capacity) {
        buffer->overflow_count++;
        return true;
    }

    bc_core_copy(buffer->data + buffer->write_position, formatted, position);
    buffer->write_position += position;
    buffer->entry_count++;

    return true;
}

bool bc_runtime_log_drain(const bc_runtime_t* application, bc_runtime_log_buffer_t* const* buffers, size_t buffer_count)
{
    bool all_succeeded = true;

    for (size_t i = 0; i < buffer_count; i++) {
        bc_runtime_log_buffer_t* buffer = buffers[i];

        if (buffer->overflow_count > 0) {
            char warning[BC_RUNTIME_LOG_BUFFER_STACK_SIZE] = {0};
            size_t warning_position = 0;

            size_t timestamp_length = 0;
            if (!bc_runtime_log_format_timestamp(warning, sizeof(warning), &timestamp_length)) {
                all_succeeded = false;
                continue;
            }
            warning_position = timestamp_length;
            warning[warning_position++] = ' ';

            bc_core_copy(warning + warning_position, "[WARN ]", 7);
            warning_position += 7;
            warning[warning_position++] = ' ';

            size_t remaining = buffer->overflow_count;
            char count_digits[21];
            size_t count_digit_length = 0;
            while (remaining > 0) {
                count_digits[count_digit_length++] = (char)('0' + (remaining % 10));
                remaining /= 10;
            }
            for (size_t left = 0, right = count_digit_length - 1; left < right; left++, right--) {
                char temporary = count_digits[left];
                count_digits[left] = count_digits[right];
                count_digits[right] = temporary;
            }

            bc_core_copy(warning + warning_position, count_digits, count_digit_length);
            warning_position += count_digit_length;

            const char suffix[] = " log messages lost (buffer full)\n";
            size_t suffix_length = sizeof(suffix) - 1;
            bc_core_copy(warning + warning_position, suffix, suffix_length);
            warning_position += suffix_length;

            ssize_t written = write(STDERR_FILENO, warning, warning_position);
            if (written < 0 || (size_t)written != warning_position) {
                all_succeeded = false;
            }
        }

        if (buffer->write_position > 0) {
            ssize_t written = write(STDERR_FILENO, buffer->data, buffer->write_position);
            if (written < 0 || (size_t)written != buffer->write_position) {
                all_succeeded = false;
            }

            atomic_fetch_add_explicit(&((bc_runtime_t*)application)->log_messages_written, buffer->entry_count, memory_order_relaxed);
        }

        buffer->write_position = 0;
        buffer->entry_count = 0;
        buffer->overflow_count = 0;
    }

    return all_succeeded;
}

void bc_runtime_log_buffer_destroy(bc_runtime_log_buffer_t* buffer)
{
    bc_allocators_context_t* memory_context = buffer->memory_context;
    bc_allocators_pool_free(memory_context, buffer->data);
    bc_allocators_pool_free(memory_context, buffer);
}
