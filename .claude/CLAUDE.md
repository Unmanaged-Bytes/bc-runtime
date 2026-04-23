# bc-runtime — project context

Application-lifecycle runtime for the `bc-*` ecosystem: init/run/
cleanup flow, layered configuration, structured logging, metrics
(counter / gauge / histogram), and a CLI framework for argv
parsing and subcommand dispatch. The top-of-stack library that
tool binaries (like bc-hash) consume directly.


## Invariants (do not break)

- **No comments in `.c` files** — code names itself. Public `.h`
  may carry one-line contracts if the signature is insufficient.
- **No defensive null-checks at function entry.** Return `false`
  on legitimate failure; never assert in production paths.
- **SPDX-License-Identifier: MIT** header on every `.c` and `.h`.
- **Strict C11** with `-Wall -Wextra -Wpedantic -Werror`.
- **Sanitizers (asan/tsan/ubsan/memcheck) stay green** in CI.
- **cppcheck stays clean**; never edit `cppcheck-suppressions.txt`
  to hide real findings.
- **Log sink is a `bc_core_writer_t*`** — `bc_runtime_log`,
  `bc_runtime_log_drain`, and `bc_runtime_error_collector_flush_to_stderr`
  all build records through `bc_core_writer_t` (Phase 2.5 of the audit
  2026-04-23). Each record is assembled in a 4 KB stack buffer (equals
  `BC_RUNTIME_LOG_BUFFER_STACK_SIZE = PIPE_BUF` on Linux) and flushed
  with a single `write(2)` syscall — atomic against concurrent writers
  per POSIX. Log helpers never call `fprintf`/`snprintf` directly; the
  writer and `bc_core_fmt_*` primitives are the only path. A future
  non-stderr sink will be introduced by letting the caller inject a
  `bc_core_writer_t*` into the runtime.
