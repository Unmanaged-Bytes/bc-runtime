bc_runtime_lib = static_library('bc-runtime',
    'app/bc_runtime_lifecycle.c',
    'app/bc_runtime_accessors.c',
    'cli/bc_runtime_cli.c',
    'config/bc_runtime_config.c',
    'log/bc_runtime_log.c',
    'metrics/bc_runtime_metrics.c',
    include_directories : [bc_runtime_public_inc, bc_runtime_internal_inc],
    dependencies : [bc_core_dep, bc_allocators_dep, bc_concurrency_dep],
    c_args : lto_c_args,
    link_args : lto_link_args,
    install : true,
)

bc_runtime_dep = declare_dependency(
    link_with : bc_runtime_lib,
    include_directories : bc_runtime_public_inc,
    dependencies : [bc_core_dep, bc_allocators_dep, bc_concurrency_dep],
)
