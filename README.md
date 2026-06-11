# cpp_utils

[![CI](https://github.com/Zanzibarr/cpp_utils/actions/workflows/ci.yml/badge.svg)](https://github.com/Zanzibarr/cpp_utils/actions/workflows/ci.yml)

Header-only C++20 utilities for performance-sensitive projects. Each utility lives in its own folder, has no external dependencies, and can be used standalone or alongside others.

## Namespace

All symbols live in `namespace utilz` (e.g. `utilz::ThreadPool`, `utilz::Logger`, `utilz::testing`, `utilz::benchmark`). Convenience macros (`PARAMS`, `STATS_REG`, `TIMER_REG`, `TEST_CASE`, `BENCH_CASE`, ...) are fully qualified internally and work from any scope.

```cpp
#include "thread_pool/thread_pool.hxx"

utilz::ThreadPool pool{4};          // qualified
using namespace utilz;              // or import everything
```

## Utilities

| Utility | What it does | Deps | Doc |
|---------|-------------|------|-----|
| [ansi_colors](utilities/ansi_colors.hxx) | ANSI escape codes, TTY-aware color helpers | — | [doc](docs/ansi_colors.md) |
| [ct_string](utilities/ct_string.hxx) | Compile-time string NTTP, FNV-1a hash | — | [doc](docs/ct_string.md) |
| [parameters](parameters/parameters.hxx) | O(1) named parameter registry | ct_string | [doc](docs/parameters.md) |
| [timer](timer/timer.hxx) | High-res timer, Welford stats, thread-safe registry | ct_string | [doc](docs/timer.md) |
| [stats_registry](stats_registry/stats_registry.hxx) | Counters, gauges, histograms + timers | timer | [doc](docs/stats_registry.md) |
| [logger](logger/logger.hxx) | Thread-safe logger, sync/async, level filtering | ansi_colors | [doc](docs/logger.md) |
| [benchmarking](benchmarking/benchmark.hxx) | Micro-benchmark framework with statistics | ansi_colors | [doc](docs/benchmarking.md) |
| [testing](testing/test_framework.hxx) | Unit-test framework with fluent expectations | ansi_colors | [doc](docs/testing.md) |
| [argparser](argparser/argparser.hxx) | CLI argument parser with TOML config overlay | parameters | [doc](docs/argparser.md) |
| [binary_set](binary_set/binary_set.hxx) | Packed bitset, O(1) ops, word-at-a-time algebra | — | [doc](docs/binary_set.md) |
| [interval](interval/interval.hxx) | Closed interval \[lo, hi\] for arithmetic types | — | [doc](docs/interval.md) |
| [scope_guard](scope_guard/scope_guard.hxx) | RAII scope-exit/fail/success guard | — | [doc](docs/scope_guard.md) |
| [limits](limits/limits.hxx) | Cooperative time and memory limits | — | [doc](docs/limits.md) |
| [thread_pool](thread_pool/thread_pool.hxx) | Fixed-size thread pool, work-stealing, priority, cooperative cancellation | — | [doc](docs/thread_pool.md) |

## Dependency Graph

```
ct_string ──┬──→ parameters ──→ argparser
            └──→ timer ──→ stats_registry

ansi_colors ──┬──→ logger
              ├──→ benchmarking
              └──→ testing

binary_set, interval, scope_guard, limits, thread_pool   (no dependencies)
```

Transitive dependency summary:

| Utility | Files needed |
|---------|-------------|
| ansi_colors | `ansi_colors.hxx` |
| ct_string | `ct_string.hxx` |
| parameters | `parameters.hxx`, `ct_string.hxx` |
| timer | `timer.hxx`, `ct_string.hxx` |
| stats_registry | `stats_registry.hxx`, `timer.hxx`, `ct_string.hxx` |
| logger | `logger.hxx`, `ansi_colors.hxx` |
| benchmarking | `benchmark.hxx`, `bench_main.hpp`, `ansi_colors.hxx` |
| testing | `test_framework.hxx`, `test_main.hpp`, `ansi_colors.hxx` |
| argparser | `argparser.hxx`, `parameters.hxx`, `ct_string.hxx` |
| binary_set | `binary_set.hxx` |
| interval | `interval.hxx` |
| scope_guard | `scope_guard.hxx` |
| limits | `limits.hxx` |
| thread_pool | `thread_pool.hxx` |

## Copying Headers into Your Project

`copy_util.py` copies a utility and all its transitive dependencies into a flat destination folder, rewriting cross-project `#include` paths so everything works from a single directory.

```
python copy_util.py <project> [<project> ...] <dest>
```

**Examples:**

```bash
# Copy just the logger (also copies ansi_colors automatically)
python copy_util.py logger ~/my_project/include

# Copy argparser (also copies parameters + ct_string)
python copy_util.py argparser /tmp/headers

# Copy multiple utilities at once (shared deps are copied once)
python copy_util.py stats_registry logger ~/my_project/include

# Copy a standalone utility
python copy_util.py binary_set ~/my_project/include
```

After copying, include headers directly — the rewritten paths are flat:

```cpp
#include "logger.hxx"       // was: #include "../logger/logger.hxx"
#include "ansi_colors.hxx"  // automatically included, path rewritten
```

The script resolves transitive dependencies automatically and deduplicates them, so copying `stats_registry` does not re-copy `ct_string.hxx` if it was already collected.

Destination files are **always overwritten** — re-running the script is safe and updates existing headers to the current version.

## Build Requirements

- **Standard:** C++20 — all library headers compile under C++20
- **Compiler:** a toolchain with `<format>` support is required by `argparser` and `parameters` — GCC ≥ 13, Clang ≥ 17 (libc++) / Clang + libstdc++ 13, AppleClang ≥ 15
- **Dependencies:** standard library only — no external packages
- **Threads:** `-pthread` required for `logger` (async mode), `timer`, `stats_registry` and `thread_pool` 

```bash
# Typical compile flags
g++ -std=c++20 -O2 -pthread your_file.cpp -o output
```

Headers are self-contained — just drop them in your include path and `#include` them.

## Development

Every module ships its own `test.cpp` built on the in-repo [testing](docs/testing.md) framework (the two `utilities/` headers share `utilities/test.cpp`). To run a module's tests:

```bash
g++ -std=c++20 -O2 -pthread <module>/test.cpp -o <module>/test && ./<module>/test
```

CI ([ci.yml](.github/workflows/ci.yml)) builds and runs all tests with GCC and Clang on Linux and AppleClang on macOS, compiles every demo and benchmark, and enforces formatting via the repo's [.clang-format](.clang-format) — run `clang-format -i` on touched files before committing.

A local-only dev dashboard (`dashboard/server.py`, not tracked in git) compiles and runs tests, benchmarks and LLVM coverage with a browser UI:

```bash
cd dashboard && python3 server.py   # opens http://localhost:8080
```
