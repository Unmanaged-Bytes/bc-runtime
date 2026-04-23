// SPDX-License-Identifier: MIT
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include "bc_core.h"
#include "bc_allocators_pool.h"

#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

static const char* const LEVEL_PREFIXES[] = {
    "[ERROR]",
    "[WARN ]",
    "[INFO ]",
    "[DEBUG]",
};

#define LEVEL_PREFIX_LENGTH 7

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

static bool format_record_into_buffer(bc_runtime_log_level_t level, const char* message,
                                      char* buffer, size_t capacity, size_t* out_length)
{
    size_t position = 0;

    size_t timestamp_length = 0;
    if (!bc_runtime_log_format_timestamp(buffer, capacity, &timestamp_length)) {
        return false;
    }
    position = timestamp_length;

    if (position + 1U + LEVEL_PREFIX_LENGTH + 1U + 1U > capacity) {
        return false;
    }

    buffer[position++] = ' ';
    if (!bc_core_copy(buffer + position, LEVEL_PREFIXES[(int)level], LEVEL_PREFIX_LENGTH)) {
        return false;
    }
    position += LEVEL_PREFIX_LENGTH;
    buffer[position++] = ' ';

    size_t message_length = 0;
    if (!bc_core_length(message, 0, &message_length)) {
        return false;
    }

    size_t max_message = capacity - position - 1U;
    if (message_length > max_message) {
        message_length = max_message;
    }
    if (!bc_core_copy(buffer + position, message, message_length)) {
        return false;
    }
    position += message_length;
    buffer[position++] = '\n';

    *out_length = position;
    return true;
}

bool bc_runtime_log(const bc_runtime_t* application, bc_runtime_log_level_t level, const char* message)
{
    int current_level = atomic_load_explicit(&application->log_level, memory_order_relaxed);
    if ((int)level > current_level) {
        return true;
    }

    char record[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];
    size_t record_length = 0;
    if (!format_record_into_buffer(level, message, record, sizeof(record), &record_length)) {
        return false;
    }

    char scratch[8];
    bc_core_writer_t writer;
    if (!bc_core_writer_init(&writer, STDERR_FILENO, scratch, sizeof(scratch))) {
        return false;
    }
    bool ok = bc_core_writer_write_bytes(&writer, record, record_length);
    if (!bc_core_writer_destroy(&writer) || !ok) {
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

    char record[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];
    size_t record_length = 0;
    if (!format_record_into_buffer(level, message, record, sizeof(record), &record_length)) {
        return false;
    }

    if (buffer->write_position + record_length > buffer->capacity) {
        buffer->overflow_count++;
        return true;
    }

    if (!bc_core_copy(buffer->data + buffer->write_position, record, record_length)) {
        return false;
    }
    buffer->write_position += record_length;
    buffer->entry_count++;

    return true;
}

static bool drain_write_overflow_warning(size_t overflow_count)
{
    char record[BC_RUNTIME_LOG_BUFFER_STACK_SIZE];
    bc_core_writer_t writer;
    if (!bc_core_writer_init(&writer, STDERR_FILENO, record, sizeof(record))) {
        return false;
    }

    char timestamp[32];
    size_t timestamp_length = 0;
    if (!bc_runtime_log_format_timestamp(timestamp, sizeof(timestamp), &timestamp_length)) {
        return false;
    }

    if (!bc_core_writer_write_bytes(&writer, timestamp, timestamp_length)) {
        return false;
    }
    if (!bc_core_writer_write_char(&writer, ' ')) {
        return false;
    }
    if (!bc_core_writer_write_bytes(&writer, LEVEL_PREFIXES[(int)BC_RUNTIME_LOG_LEVEL_WARN], LEVEL_PREFIX_LENGTH)) {
        return false;
    }
    if (!bc_core_writer_write_char(&writer, ' ')) {
        return false;
    }
    if (!bc_core_writer_write_uint64_dec(&writer, (uint64_t)overflow_count)) {
        return false;
    }
    if (!BC_CORE_WRITER_PUTS(&writer, " log messages lost (buffer full)\n")) {
        return false;
    }
    return bc_core_writer_destroy(&writer);
}

static bool drain_dump_buffer(const char* data, size_t length)
{
    char scratch[8];
    bc_core_writer_t writer;
    if (!bc_core_writer_init(&writer, STDERR_FILENO, scratch, sizeof(scratch))) {
        return false;
    }
    bool ok = bc_core_writer_write_bytes(&writer, data, length);
    if (!bc_core_writer_destroy(&writer) || !ok) {
        return false;
    }
    return true;
}

bool bc_runtime_log_drain(const bc_runtime_t* application, bc_runtime_log_buffer_t* const* buffers, size_t buffer_count)
{
    bool all_succeeded = true;

    for (size_t buffer_index = 0; buffer_index < buffer_count; buffer_index++) {
        bc_runtime_log_buffer_t* buffer = buffers[buffer_index];

        if (buffer->overflow_count > 0) {
            if (!drain_write_overflow_warning(buffer->overflow_count)) {
                all_succeeded = false;
            }
        }

        if (buffer->write_position > 0) {
            if (!drain_dump_buffer(buffer->data, buffer->write_position)) {
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
