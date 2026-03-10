# stats_registry — Benchmarks

**Source:** [`benchmarks.cpp`](benchmarks.cpp)
**Full doc:** [`docs/stats_registry.md`](../docs/stats_registry.md)

Comprehensive benchmarks for every primitive in `TimerRegistry` and `StatsRegistry` across single-threaded, multi-threaded, and combined hot-path scenarios. Use these numbers to make informed decisions about annotation overhead.

## Build and Run

```bash
# Change MAX_REGISTRIES (in timer.hxx) to 256 (see the 'Per-thread storage design' section)
g++ -std=c++20 -O2 -pthread benchmarks.cpp -o benchmarks && ./benchmarks
```

## A Note on Measurement Resolution

On Apple Silicon (M-series), `std::chrono::steady_clock::now()` costs ~47 ns per call (median ~42 ns). This creates a **~18–24 ns mean measurement floor** in the benchmark framework: operations faster than one clock tick show up as a mean near that range (quantised between 0 and 41 ns per sample). The median for these cases is 0 ns.

Operations that clearly register above the floor (timers, ScopedTimer, report generation) have more meaningful absolute numbers. For sub-tick operations, read the mean as an upper bound, not the exact cost.

## Per-thread storage design

`TimerRegistry` uses a `thread_local std::array<std::unique_ptr<ThreadLocal>, MAX_REGISTRIES>` indexed by a per-registry integer ID (assigned once at construction). This replaces the previous `unordered_map<const TimerRegistry*, ThreadLocal>` that was probed on every `start()`/`stop()` call. The hot-path overhead is now a single indexed pointer load instead of a hash + probe. Up to `MAX_REGISTRIES` (default 256 - to pass tests that uses many different registries...) independent `TimerRegistry` or `StatsRegistry` instances are supported across the program's lifetime.

## Suite Overview

| Suite | What is measured |
|-------|-----------------|
| 0 · Baselines | Raw primitives: `steady_clock::now`, Welford, `atomic::fetch_add`, `mutex` |
| 1 · Timer (standalone) | `Timer` class: start/stop/elapsed/reset |
| 2 · TimerRegistry (single thread) | Named start/stop, handle-based stop, cached slot |
| 3 · make_scoped_timer (RAII) | Construct + destruct, with body |
| 4 · ScopedTimer standalone | Print-on-destruct, no registry |
| 5 · Counters (single thread) | inc/dec/set/get/ref/reset |
| 5b · make_scoped_counter (RAII) | inc on ctor, dec on dtor |
| 6 · Gauges (single thread) | record/reset |
| 7 · Histograms (single thread) | in-range/underflow/overflow/boundary/reset |
| 8a · Counters multi-threaded | Counter/scoped-counter under 4-thread contention |
| 8b · Gauges multi-threaded | Gauge under same-key vs distinct-key contention |
| 8c · Histograms multi-threaded | Histogram under 4-thread contention |
| 8d · Timers multi-threaded | Timer hot path + cold path (new thread) |
| 9 · Report generation | get_*_report() cost |
| 11 · Series | series_push throughput, series_get merge+sort latency |

## Reference Results — Single Thread

Measured with `-O2` on an Apple M-series CPU. Mean reported; see note on measurement floor above.

### Suite 0 — Baselines (underlying primitive cost)

| Primitive | Mean | Median | Note |
|-----------|------|--------|------|
| `steady_clock::now()` | ~47 ns | 42 ns | One clock call cost on Apple Silicon |
| Two clock calls + ns subtract | ~63 ns | 42 ns | Minimum timer start+stop cost |
| Two clocks + Welford record (manual) | ~63 ns | 83 ns | |
| `atomic::fetch_add` relaxed | ~23 ns | 41 ns | Sub-tick, floor-dominated |
| Uncontended `mutex` lock + unlock | ~25 ns | 41 ns | Sub-tick, floor-dominated |
| Mutex + Welford record (manual gauge baseline) | ~25 ns | 41 ns | Sub-tick, floor-dominated |

### Suite 1 — Timer (standalone)

| Operation | Mean | Median |
|-----------|------|--------|
| `Timer::start + stop` | ~49 ns | 42 ns |
| `Timer::start + stop + elapsed_ms` | ~51 ns | 42 ns |
| `Timer::elapsed_ms` while running | ~36 ns | 42 ns |
| `Timer::reset` | ~18 ns | 0 ns |

### Suite 2 — TimerRegistry (single thread)

| Operation | Mean | Median |
|-----------|------|--------|
| `start<n> + stop<n>` (array lookup both sides) | ~55 ns | 42 ns |
| `start<n> + stop(Slot*)` (no lookup on stop) | ~54 ns | 42 ns |
| Direct `timer.start() + stop()` via cached `Slot*` | ~57 ns | 42 ns |
| `stats<n>` — copy accumulated stats | ~19 ns | 0 ns |
| `elapsed<n>` — stopped timer | ~19 ns | 0 ns |
| `is_running<n>` — single lookup + bool load | ~19 ns | 0 ns |
| `reset<n>` — slow path (global mutex) | ~27 ns | 41 ns |

Note: `start<n>` + `stop<n>` and the cached-slot variant both measure ~55 ns — the 2 clock calls dominate the total cost. The per-thread lookup overhead (one indexed pointer load from the TLS array) is unmeasurable relative to the ~42 ns clock call cost.

### Suite 3 — make_scoped_timer (RAII)

| Operation | Mean | Median |
|-----------|------|--------|
| Construct + destruct | ~54 ns | 42 ns |
| With volatile body | ~55 ns | 42 ns |

RAII wrapper adds no measurable overhead over direct `start<n> + stop(Slot*)`.

### Suite 4 — ScopedTimer standalone (no registry)

Destructor prints elapsed time to stdout. Output suppressed in benchmark to avoid I/O skew. These are significantly heavier than registry-backed timers due to string formatting and I/O.

| Operation | Mean | Median |
|-----------|------|--------|
| `ScopedTimer<ms>` construct + destruct | ~182 ns | 167 ns |
| `ScopedTimer<ns>` construct + destruct | ~172 ns | 167 ns |

### Suite 5 — Counters

| Operation | Mean | Median |
|-----------|------|--------|
| `counter_inc<n>` | ~22 ns | 41 ns |
| `counter_inc<n>(delta)` — explicit delta | ~22 ns | 41 ns |
| `counter_dec<n>` | ~22 ns | 41 ns |
| `counter_set<n>` | ~19 ns | 0 ns |
| `counter_get<n>` | ~18 ns | 0 ns |
| `counter_ref*` raw `fetch_add` (no lookup) | ~21 ns | 0 ns |
| `counter_reset<n>` | ~18 ns | 0 ns |
| `make_scoped_counter` construct + destruct | ~23 ns | 41 ns |
| `nested make_scoped_counter` x2 | ~24 ns | 41 ns |

The `counter_ref*` benchmark shows similar mean to `counter_inc<n>` because both are sub-tick — the actual cost difference is absorbed into the measurement floor. In a real tight loop, `counter_ref*` does save the array lookup, but the benefit is only visible at the CPU-cycle level.

### Suite 6 — Gauges

| Operation | Mean | Median |
|-----------|------|--------|
| `gauge_record<n>` (varying value) | ~24 ns | 41 ns |
| `gauge_record<n>` (constant value, best-case Welford) | ~24 ns | 41 ns |
| `gauge_reset<n>` | ~23 ns | 41 ns |

### Suite 7 — Histograms

| Operation | Mean | Median |
|-----------|------|--------|
| `histogram_record<n>` (in-range) | ~24 ns | 41 ns |
| `histogram_record<n>` (underflow) | ~23 ns | 41 ns |
| `histogram_record<n>` (overflow) | ~24 ns | 41 ns |
| `histogram_record<n>` (boundary value at low) | ~23 ns | 41 ns |
| `histogram_reset<n>` | ~25 ns | 41 ns |

All histogram paths (in-range, under/overflow, boundary) have identical cost — the mutex dominates; no branch on value range is measurable.

### Suite 9 — Report Generation

These are **not** hot-path operations, but it is useful to know how often you can safely call them.

| Report | Mean |
|--------|------|
| `get_counter_report()` | ~210 ns |
| `get_gauge_report()` | ~194 ns |
| `get_histogram_report()` | ~527 ns |
| `get_stats_report()` (merges all thread Welford accumulators) | ~29.3 µs |
| `get_thread_report()` (graveyard + live threads) | ~51.2 µs |

Counter/gauge/histogram reports are in the hundreds-of-nanoseconds range. The timer stats report is expensive (~29 µs) because it acquires a global lock and merges per-thread Welford accumulators. Safe to call once per second.

### Suite 11 — Series

| Operation | Mean | Median | Note |
|-----------|------|--------|------|
| `series_push<n>` single thread | ~36 ns | 42 ns | Lock-free hot path; floor-dominated |
| `series_push<n>` 4 threads (same name) | ~41 µs | ~39 µs | Batch of 4×200 iters; dominated by thread creation — see note below |
| `series_get<n>` merge+sort 1 000 points | ~1.16 µs | 1.17 µs | Includes cross-thread merge and sort |

`series_push` on the hot path is lock-free after the first call and costs ~36 ns — comparable to a counter increment. `series_get` triggers a merge and sort across all contributing threads; at 1 000 points this takes ~1.2 µs.

**Note on the 4-thread row:** `series_push` uses per-thread-local storage, so there is zero inter-thread contention. The 4-thread batch time (~41 µs) reflects thread lifecycle overhead, not push cost. For persistent-thread tight loops the per-call cost equals the single-thread result (~36 ns).

## Multi-threaded Behaviour

> **Caveat on tight-loop use:** every multi-threaded benchmark spawns and joins 4 fresh OS threads per outer iteration. At ~12 µs per thread (see cold-path row below), that is ~48 µs of fixed overhead per measurement — comparable to or larger than the operation cost itself. This has different implications per primitive:
>
> - **Timers and series_push** use thread-local storage with zero inter-thread contention. The multi-thread numbers add no information — tight-loop cost equals the single-thread result from Suites 2–3 and 11.
> - **Counters** involve a shared atomic. Cache-line bouncing between cores is real but cannot be isolated from thread-creation noise in this benchmark. Treat the absolute counter numbers as upper bounds.
> - **Gauges and histograms** (per-entry mutex): the **same-key vs distinct-key delta** survives the thread-creation noise because both rows carry identical overhead. That ~20 µs difference over 4×500 ops (≈ ~10 ns/op) is the best signal for the serialisation cost under contention.

### Counters under contention

| Scenario | Batch time (mean) | Operations | Note |
|----------|-------------------|-----------|------|
| `counter_inc<n>` — 4 threads, same key | ~37.5 µs | 4×500 | Atomic contention only (no lock) |
| `counter_ref*` fetch_add — 4 threads, cached pointer | ~29.8 µs | 4×500 | No CtStatID lookup inside threads |
| `make_scoped_counter` — 4 threads, inc+dec per iter | ~36.0 µs | 4×500 | inc+dec round-trip per iteration |

### Timers under contention

Each thread has its **own thread-local `Slot`** — there is **zero contention** on the timer hot path.

| Scenario | Batch time (mean) | Operations | Note |
|----------|-------------------|-----------|------|
| `start<n>+stop<n>` — 4 threads, same key | ~55.5 µs | 4×500 | Thread-local; no hot-path contention |
| `make_scoped_timer` — 4 threads | ~54.9 µs | 4×500 | Same cost as raw start/stop |
| Cold path (new thread per iteration) | ~12.1 µs | 1 | Thread creation + first-call mutex registration |

### Gauges and histograms under contention

Per-entry mutex. Same-key workloads are fully serialised; distinct-key workloads run in parallel.

| Scenario | Batch time (mean) | Operations | Note |
|----------|-------------------|-----------|------|
| `gauge_record<n>` — 4 threads, same key | ~61.4 µs | 4×500 | Serialised by per-entry mutex |
| `gauge_record<n>` — 4 threads, distinct keys | ~40.9 µs | 4×500 | Parallel, no contention |
| `histogram_record<n>` — 4 threads, same key | ~56.8 µs | 4×500 | Serialised by per-entry mutex |

The same-key vs distinct-key gap for gauges (~61 µs vs ~41 µs) is the most actionable number: the ~20 µs difference over 4×500 ops cancels out thread-creation overhead and isolates the mutex serialisation cost (≈ ~10 ns/op extra under 4-core contention).

## Overhead vs Raw Primitives

| Registry primitive | Mean | Raw primitive | Mean | Overhead |
|-------------------|------|--------------|------|---------|
| `counter_inc<n>` | ~22 ns | `atomic::fetch_add` | ~23 ns | ≈ 0 (both floor-dominated) |
| `gauge_record<n>` | ~24 ns | `mutex + Welford` | ~25 ns | ≈ 0 (both floor-dominated) |
| `histogram_record<n>` | ~24 ns | — | — | Similar to gauge |
| `start<n> + stop<n>` | ~55 ns | `2× clock + Welford` | ~63 ns | ~0 overhead (lookup cheaper than extra clock) |

The lookup overhead (one TLS array index) is sub-tick and does not show up as significant cost relative to the clock call cost (~42 ns each) on this platform.
