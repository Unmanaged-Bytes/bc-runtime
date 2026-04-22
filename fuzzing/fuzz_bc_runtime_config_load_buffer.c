// SPDX-License-Identifier: MIT

#include "bc_allocators.h"
#include "bc_runtime.h"
#include "bc_runtime_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    bc_allocators_context_t* memory_context = NULL;
    if (!bc_allocators_context_create(NULL, &memory_context)) {
        return 0;
    }
    bc_runtime_config_store_t* store = NULL;
    if (!bc_runtime_config_store_create(memory_context, &store)) {
        bc_allocators_context_destroy(memory_context);
        return 0;
    }

    (void)bc_runtime_config_load_from_buffer(store, (const char*)data, size);
    (void)bc_runtime_config_store_sort(store);

    const char* value = NULL;
    (void)bc_runtime_config_store_lookup(store, "key", &value);
    (void)bc_runtime_config_store_lookup(store, "threads", &value);

    bc_runtime_config_store_destroy(memory_context, store);
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
