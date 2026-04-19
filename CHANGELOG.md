# Changelog

All notable changes to bc-runtime are documented here.

## [1.0.0]

Initial public release.

### Added

- **Application lifecycle** (`bc_runtime.h`): `bc_runtime_app_*`
  init/run/cleanup flow, configuration loading, metric collection,
  and structured logging.
- **CLI framework** (`bc_runtime_cli.h`): argv parsing, subcommand
  dispatch, help / usage rendering.
- **Config**: layered configuration with typed accessors and
  environment-variable overrides.
- **Log**: configurable log sink with level filtering.
- **Metrics**: counter / gauge / histogram primitives.

### Quality

- Unit tests under `tests/`, built with cmocka.
- Sanitizers (asan / tsan / ubsan / memcheck) pass.
- cppcheck clean on the project sources.
- MIT-licensed, static `.a` published as Debian `.deb` via GitHub
  Releases.

[1.0.0]: https://github.com/Unmanaged-Bytes/bc-runtime/releases/tag/v1.0.0
