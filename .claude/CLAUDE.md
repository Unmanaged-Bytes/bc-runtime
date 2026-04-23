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
- **Log sink is a `bc_core_writer_t*`** — the callback-based sink
  planned in the audit 2026-04-23 roadmap has been superseded by
  the typed writer abstraction introduced in Phase 2.5 of the
  audit plan. Until that phase lands, `bc_runtime_log` /
  `bc_runtime_log_drain` write directly to `STDERR_FILENO` via a
  single `write(2)` syscall per log record (size bounded by
  `BC_RUNTIME_LOG_BUFFER_STACK_SIZE = 4096`, which equals
  `PIPE_BUF` on Linux — writes are atomic against concurrent
  writers per POSIX). Consumers that need a non-stderr sink must
  wait for the writer injection API.
- **`bc_runtime_error_collector_flush_to_stderr`** is the one
  exception — it is named explicitly for stderr, used only as a
  last-resort end-of-process flush, and stays on `fprintf(stderr, …)`.
