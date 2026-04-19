bench_deps = [bc_runtime_dep]

benchmark('bench_bc_runtime_lifecycle',
  executable('bench_bc_runtime_lifecycle', 'bench_bc_runtime_lifecycle.c',
    include_directories : bc_runtime_public_inc,
    dependencies : bench_deps,
  ),
  timeout : 120,
)

benchmark('bench_bc_runtime_overhead',
  executable('bench_bc_runtime_overhead', 'bench_bc_runtime_overhead.c',
    include_directories : bc_runtime_public_inc,
    dependencies : bench_deps,
  ),
  timeout : 120,
)

benchmark('bench_bc_runtime_logging',
  executable('bench_bc_runtime_logging', 'bench_bc_runtime_logging.c',
    include_directories : bc_runtime_public_inc,
    dependencies : bench_deps,
  ),
  timeout : 120,
)
