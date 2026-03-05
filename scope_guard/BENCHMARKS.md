# scope_guard — Benchmarks

**Source:** [`benchmarks.cpp`](benchmarks.cpp)
**Full doc:** [`docs/scope_guard.md`](../docs/scope_guard.md)

Measures the overhead of `ScopeGuard` across all three strategies (EXIT/SUCCESS/FAIL), the `dismiss()` path, exception unwinding, lambda capture size, and multiple simultaneous guards.

## Build and Run

```bash
g++ -std=c++20 -O2 benchmarks.cpp -o benchmarks && ./benchmarks
```

## A Note on Measurement Resolution

On Apple Silicon (M-series), `steady_clock::now()` costs ~42 ns per call. Most `ScopeGuard` operations have a median of 42 ns and a min of 0 ns — they complete in sub-tick time on the hot path. Means in the 55–70 ns range are dominated by measurement noise, not by the guard itself. The exception-path benchmarks use BENCH_CASE_N with 500 iterations so that throw/catch overhead is reliably measurable.

## Suite Overview

| Suite | What is measured |
|-------|-----------------|
| 1 · EXIT strategy | `on_scope_exit` and direct `ScopeGuard` construction on normal exit |
| 2 · SUCCESS strategy | `on_scope_success` callable runs (no exception) |
| 3 · FAIL strategy | `on_scope_fail` callable is skipped (normal exit) |
| 4 · Dismiss | Guard dismissed before destruction — callable never runs |
| 5 · Exception path | Guard executes (or is skipped) during `throw`/`catch` unwinding |
| 6 · Callable capture size | One int by ref vs four ints by value |
| 7 · Multiple guards | LIFO destruction ordering for 2 and 4 EXIT guards |

## Reference Results

Measured with `-O2` on an Apple M-series CPU (2026-03-05). Results will vary by hardware.

### Suites 1–4 — Normal path (floor-dominated)

All normal-path costs are dominated by the measurement floor (~42 ns).

| Operation | Mean | Median |
|-----------|------|--------|
| `on_scope_exit` — empty lambda, normal exit | ~62 ns | 42 ns |
| `ScopeGuard EXIT` — direct construction | ~69 ns | 42 ns |
| `on_scope_exit` — no-capture lambda | ~59 ns | 42 ns |
| `on_scope_success` — callable runs | ~59 ns | 42 ns |
| `ScopeGuard SUCCESS` — direct construction | ~61 ns | 42 ns |
| `on_scope_fail` — callable skipped | ~58 ns | 42 ns |
| `ScopeGuard FAIL` — direct construction | ~58 ns | 42 ns |
| `on_scope_exit` — dismissed | ~56 ns | 42 ns |
| `on_scope_success` — dismissed | ~62 ns | 42 ns |
| `on_scope_fail` — dismissed | ~56 ns | 42 ns |

The median of 42 ns for all these cases confirms they complete in sub-tick time. The means are slightly above the floor due to occasional OS interrupts and cache effects.

### Suite 5 — Exception path

Each iteration includes a `throw std::runtime_error` and a `catch(...)`.

| Operation | Mean |
|-----------|------|
| `on_scope_fail` — callable runs during unwinding | ~5.79 µs |
| `on_scope_success` — callable skipped during unwinding | ~5.15 µs |
| `on_scope_exit` — callable runs during unwinding | ~4.78 µs |

The ~5 µs cost is dominated by C++ exception machinery (stack unwinding, RTTI, exception object allocation), not by the guard itself. The guard's contribution (an extra `std::uncaught_exceptions()` call and conditional branch) is lost in the noise of exception handling.

### Suite 6 — Callable capture size

| Operation | Mean |
|-----------|------|
| EXIT guard — one int by ref | ~43 ns |
| EXIT guard — four ints by value | ~41 ns |

Capture size has **no measurable impact**. The callable is stored inline in the `ScopeGuard` object (no heap allocation), and the compiler optimizes the lambda body regardless of how many values are captured.

### Suite 7 — Multiple guards (LIFO)

| Operation | Mean |
|-----------|------|
| Two EXIT guards — LIFO destruction | ~44 ns |
| Four EXIT guards — LIFO destruction | ~58 ns |

Each additional guard adds at most one extra `std::uncaught_exceptions()` read and one callable invocation. The cost scales linearly and remains in the sub-tick range.

## Interpretation

`ScopeGuard` on the normal (non-exception) path is **essentially free** — the median cost is a single timer tick (42 ns), indicating the guard construction, condition check, and callable invocation all complete in under one tick. There is no heap allocation and no virtual dispatch.

On the exception path, the ~5 µs cost is due entirely to C++ exception machinery, not to `ScopeGuard`. Using `on_scope_fail` for rollback adds no measurable overhead over not using a guard at all.

Lambda capture size (small vs large) and the number of simultaneous guards (up to 4) have no meaningful impact on performance.
