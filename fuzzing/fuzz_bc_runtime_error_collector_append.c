// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_runtime_error_collector.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUZZ_STAGE_CAPACITY 128
#define FUZZ_PATH_CAPACITY 1024

static bool decode_record(const uint8_t* data, size_t size, size_t* position, int* out_errno_value, char* stage_buffer, size_t stage_capacity,
                          char* path_buffer, size_t path_capacity, bool* out_has_stage)
{
    if (*position + 7 > size) {
        return false;
    }
    const uint32_t errno_bits = (uint32_t)data[*position] | ((uint32_t)data[*position + 1] << 8) | ((uint32_t)data[*position + 2] << 16)
                                | ((uint32_t)data[*position + 3] << 24);
    *out_errno_value = (int)errno_bits;
    *position += 4;

    uint8_t stage_length = data[(*position)++];
    if ((size_t)stage_length >= stage_capacity) {
        stage_length = (uint8_t)(stage_capacity - 1);
    }
    if (*position + stage_length > size) {
        return false;
    }
    memcpy(stage_buffer, &data[*position], stage_length);
    stage_buffer[stage_length] = '\0';
    *position += stage_length;
    *out_has_stage = stage_length > 0;

    if (*position + 2 > size) {
        return false;
    }
    uint16_t path_length = (uint16_t)((uint16_t)data[*position] | ((uint16_t)data[*position + 1] << 8));
    *position += 2;
    if ((size_t)path_length >= path_capacity) {
        path_length = (uint16_t)(path_capacity - 1);
    }
    if (*position + path_length > size) {
        return false;
    }
    memcpy(path_buffer, &data[*position], path_length);
    path_buffer[path_length] = '\0';
    *position += path_length;
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    bc_allocators_context_t* memory_context = NULL;
    if (!bc_allocators_context_create(NULL, &memory_context)) {
        return 0;
    }
    bc_runtime_error_collector_t* collector = NULL;
    if (!bc_runtime_error_collector_create(memory_context, &collector)) {
        bc_allocators_context_destroy(memory_context);
        return 0;
    }

    char stage_buffer[FUZZ_STAGE_CAPACITY];
    char path_buffer[FUZZ_PATH_CAPACITY];
    size_t position = 0;
    for (;;) {
        int errno_value = 0;
        bool has_stage = false;
        if (!decode_record(data, size, &position, &errno_value, stage_buffer, sizeof(stage_buffer), path_buffer, sizeof(path_buffer), &has_stage)) {
            break;
        }
        const char* stage_ptr = has_stage ? stage_buffer : NULL;
        (void)bc_runtime_error_collector_append(collector, memory_context, path_buffer, stage_ptr, errno_value);
    }
    (void)bc_runtime_error_collector_count(collector);

    bc_runtime_error_collector_destroy(memory_context, collector);
    bc_allocators_context_destroy(memory_context);
    return 0;
}

#ifndef BC_FUZZ_LIBFUZZER
int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <iterations> [seed]\n", argv[0]);
        return 2;
    }
    const unsigned long iterations = strtoul(argv[1], NULL, 10);
    const unsigned long seed = (argc >= 3) ? strtoul(argv[2], NULL, 10) : 0;
    srand((unsigned int)seed);

    uint8_t buffer[4096];
    for (unsigned long i = 0; i < iterations; i++) {
        const size_t length = (size_t)(rand() % (int)sizeof(buffer));
        for (size_t j = 0; j < length; j++) {
            buffer[j] = (uint8_t)(rand() & 0xFF);
        }
        LLVMFuzzerTestOneInput(buffer, length);
    }
    return 0;
}
#endif
