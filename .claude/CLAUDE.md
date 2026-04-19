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
- **Log sink is callback-based** — no direct writes to stderr
  from library code.
