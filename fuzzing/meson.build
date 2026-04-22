# SPDX-License-Identifier: MIT

fuzzer_c_args = []
fuzzer_link_args = []
if cc.get_id() == 'clang'
    fuzzer_c_args += ['-fsanitize=fuzzer', '-DBC_FUZZ_LIBFUZZER']
    fuzzer_link_args += ['-fsanitize=fuzzer']
endif

fuzz_targets = [
    'bc_runtime_cli_parse',
    'bc_runtime_error_collector_append',
    'bc_runtime_config_load_buffer',
]

foreach name : fuzz_targets
    executable('fuzz_' + name,
        'fuzz_' + name + '.c',
        include_directories : [bc_runtime_public_inc, bc_runtime_internal_inc],
        dependencies : [bc_runtime_dep],
        c_args : fuzzer_c_args,
        link_args : fuzzer_link_args,
    )
endforeach
