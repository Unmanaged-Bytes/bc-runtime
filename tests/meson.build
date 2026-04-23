cmocka_dep = dependency('cmocka')

pub_inc = bc_runtime_public_inc
int_inc = bc_runtime_internal_inc

simple_tests = [
    'config_store', 'config_file', 'config_env_cli', 'config_getters',
    'lifecycle', 'accessors',
    'log', 'log_buffer',
    'metrics',
    'config_edge_cases',
    'cli',
    'error_collector',
]

foreach name : simple_tests
    test('bc_runtime_' + name,
        executable('test_bc_runtime_' + name,
            'test_bc_runtime_' + name + '.c',
            include_directories : [pub_inc, int_inc],
            dependencies : [cmocka_dep, bc_runtime_dep],
        ),
        timeout : 30,
    )
endforeach

bc_runtime_sources_for_wrap = [
    '../src/app/bc_runtime_lifecycle.c',
    '../src/app/bc_runtime_accessors.c',
    '../src/config/bc_runtime_config.c',
    '../src/log/bc_runtime_log.c',
    '../src/metrics/bc_runtime_metrics.c',
]

bc_runtime_wrap_deps = [cmocka_dep, bc_core_dep, bc_allocators_dep, bc_concurrency_dep]

test_lifecycle_errors = executable('test_bc_runtime_lifecycle_errors',
    'test_bc_runtime_lifecycle_errors.c',
    bc_runtime_sources_for_wrap,
    include_directories : [pub_inc, int_inc],
    dependencies : bc_runtime_wrap_deps,
    c_args : ['-fno-inline'],
    link_args : [
        '-Wl,--wrap=bc_allocators_context_create',
        '-Wl,--wrap=bc_allocators_pool_allocate',
        '-Wl,--wrap=bc_concurrency_signal_handler_create',
        '-Wl,--wrap=bc_concurrency_signal_handler_install',
        '-Wl,--wrap=bc_runtime_config_store_create',
        '-Wl,--wrap=bc_runtime_config_load_environment',
        '-Wl,--wrap=bc_runtime_config_load_arguments',
        '-Wl,--wrap=bc_runtime_config_store_sort',
        '-Wl,--wrap=bc_concurrency_create',
    ],
)
test('bc_runtime_lifecycle_errors', test_lifecycle_errors, timeout : 30)

test_metrics_errors = executable('test_bc_runtime_metrics_errors',
    'test_bc_runtime_metrics_errors.c',
    bc_runtime_sources_for_wrap,
    include_directories : [pub_inc, int_inc],
    dependencies : bc_runtime_wrap_deps,
    c_args : ['-fno-inline'],
    link_args : ['-Wl,--wrap=bc_allocators_context_get_stats'],
)
test('bc_runtime_metrics_errors', test_metrics_errors, timeout : 30)

test_log_errors = executable('test_bc_runtime_log_errors',
    'test_bc_runtime_log_errors.c',
    bc_runtime_sources_for_wrap,
    include_directories : [pub_inc, int_inc],
    dependencies : bc_runtime_wrap_deps,
    c_args : ['-fno-inline'],
    link_args : [
        '-Wl,--wrap=clock_gettime',
        '-Wl,--wrap=write',
        '-Wl,--wrap=bc_core_length',
        '-Wl,--wrap=bc_allocators_pool_allocate',
    ],
)
test('bc_runtime_log_errors', test_log_errors, timeout : 30)

test_config_errors = executable('test_bc_runtime_config_errors',
    'test_bc_runtime_config_errors.c',
    bc_runtime_sources_for_wrap,
    include_directories : [pub_inc, int_inc],
    dependencies : bc_runtime_wrap_deps,
    c_args : ['-fno-inline'],
    link_args : [
        '-Wl,--wrap=bc_allocators_pool_allocate',
        '-Wl,--wrap=bc_allocators_arena_create',
        '-Wl,--wrap=bc_allocators_arena_copy_string',
        '-Wl,--wrap=bc_allocators_pool_reallocate',
        '-Wl,--wrap=bc_core_length',
        '-Wl,--wrap=bc_core_equal',
    ],
)
test('bc_runtime_config_errors', test_config_errors, timeout : 30)

test_log_edge_cases = executable('test_bc_runtime_log_edge_cases',
    'test_bc_runtime_log_edge_cases.c',
    bc_runtime_sources_for_wrap,
    include_directories : [pub_inc, int_inc],
    dependencies : bc_runtime_wrap_deps,
    c_args : ['-fno-inline'],
    link_args : ['-Wl,--wrap=localtime_r'],
)
test('bc_runtime_log_edge_cases', test_log_edge_cases, timeout : 30)
