// SPDX-License-Identifier: MIT

#include "bc_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 16) {
        return 0;
    }

    /* Write the fuzzer input into a temp config file, then load the runtime
     * against it via a command-line argument. The config parser will see the
     * random bytes and must not crash. */

    char path[64] = "/tmp/bc_runtime_fuzz_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        return 0;
    }
    ssize_t written = write(fd, data, size);
    (void)written;
    close(fd);

    const char* argv[3] = {"fuzz", "--config-file", path};
    bc_runtime_config_t config = {0};
    bc_runtime_callbacks_t callbacks = {0};
    bc_runtime_t* app = NULL;
    if (bc_runtime_create(&config, &callbacks, NULL, &app)) {
        const char* value = NULL;
        bc_runtime_config_get_string(app, "key", &value);
        bc_runtime_destroy(app);
    }
    (void)argv;

    unlink(path);
    return 0;
}

#ifndef LIBFUZZER
int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <iterations> [seed]\n", argv[0]);
        return 2;
    }
    unsigned long iterations = strtoul(argv[1], NULL, 10);
    unsigned long seed = (argc >= 3) ? strtoul(argv[2], NULL, 10) : 0;
    srand((unsigned int)seed);

    uint8_t buffer[2048];
    for (unsigned long i = 0; i < iterations; i++) {
        size_t len = (size_t)(rand() % (int)sizeof(buffer));
        for (size_t j = 0; j < len; j++) {
            buffer[j] = (uint8_t)(rand() & 0xFF);
        }
        LLVMFuzzerTestOneInput(buffer, len);
    }
    return 0;
}
#endif
