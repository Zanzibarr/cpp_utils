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

On Apple Silicon (M-series), `std::chrono::steady_clock::now()` costs ~42 ns per call. This creates a **~18–21 ns mean measurement floor** in the benchmark framework: operations faster than one clock tick show up as a mean near 18–21 ns (quantised between 0 and 41 ns per sample). The median for these cases is 0 ns.

Operations that clearly register above the floor (timers, set algebra, combined hot path) have more meaningful absolute numbers. For sub-tick operations, read the mean as an upper bound, not the exact cost.

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
| 7 · Histograms (single thread) | in-range/underflow/overflow/reset |
| 8 · Multi-threaded | Counter/gauge/histogram/timer under 4-thread contention |
| 9 · Report generation | get_*_report() cost |
| 10 · Combined hot path | Realistic "annotated function body" |

## Reference Results — Single Thread

Measured with `-O2` on an Apple M-series CPU. Mean reported; see note on measurement floor above.

### Suite 0 — Baselines (underlying primitive cost)

| Primitive | Mean | Median | Note |
|-----------|------|--------|------|
| `steady_clock::now()` | ~71 ns | 83 ns | One clock call cost on Apple Silicon |
| Two clock calls + ns subtract | ~88 ns | 83 ns | Minimum timer start+stop cost |
| Two clocks + Welford record (manual) | ~77 ns | 83 ns | |
| `atomic::fetch_add` relaxed | ~26 ns | 41 ns | Sub-tick, floor-dominated |
| Uncontended `mutex` lock + unlock | ~27 ns | 41 ns | Sub-tick, floor-dominated |
| Mutex + Welford record (manual gauge baseline) | ~27 ns | 41 ns | Sub-tick, floor-dominated |

### Suite 1 — Timer (standalone)

| Operation | Mean | Median |
|-----------|------|--------|
| `Timer::start + stop` | ~53 ns | 42 ns |
| `Timer::start + stop + elapsed_ms` | ~53 ns | 42 ns |
| `Timer::elapsed_ms` while running | ~36 ns | 42 ns |
| `Timer::reset` | ~18 ns | 0 ns |

### Suite 2 — TimerRegistry (single thread)

| Operation | Mean | Median |
|-----------|------|--------|
| `start<n> + stop<n>` (array lookup both sides) | ~55 ns | 42 ns |
| `start<n> + stop(Slot*)` (no lookup on stop) | ~54 ns | 42 ns |
| Direct `timer.start() + stop()` via cached `Slot*` | ~56 ns | 42 ns |
| `stats<n>` — copy accumulated stats | ~19 ns | 0 ns |
| `elapsed<n>` — stopped timer | ~19 ns | 0 ns |
| `is_running<n>` — single lookup + bool load | ~19 ns | 0 ns |
| `reset<n>` — slow path (global mutex) | ~26 ns | 41 ns |

Note: `start<n>` + `stop<n>` and the cached-slot variant both measure ~55 ns — the 2 clock calls dominate the total cost. The per-thread lookup overhead (one indexed pointer load from the TLS array) is unmeasurable relative to the ~42 ns clock call cost.

### Suite 3 — make_scoped_timer (RAII)

| Operation | Mean | Median |
|-----------|------|--------|
| Construct + destruct | ~54 ns | 42 ns |
| With volatile body | ~53 ns | 42 ns |

RAII wrapper adds no measurable overhead over direct `start<n> + stop(Slot*)`.

### Suite 5 — Counters

| Operation | Mean | Median |
|-----------|------|--------|
| `counter_inc<n>` | ~22 ns | 41 ns |
| `counter_dec<n>` | ~22 ns | 41 ns |
| `counter_set<n>` | ~19 ns | 0 ns |
| `counter_get<n>` | ~19 ns | 0 ns |
| `counter_ref*` raw `fetch_add` (no lookup) | ~21 ns | 0 ns |
| `counter_reset<n>` | ~18 ns | 0 ns |
| `make_scoped_counter` construct + destruct | ~22 ns | 41 ns |

The `counter_ref*` benchmark shows similar mean to `counter_inc<n>` because both are sub-tick — the actual cost difference is absorbed into the measurement floor. In a real tight loop, `counter_ref*` does save the array lookup, but the benefit is only visible at the CPU-cycle level.

### Suite 6 — Gauges

| Operation | Mean | Median |
|-----------|------|--------|
| `gauge_record<n>` | ~24 ns | 41 ns |
| `gauge_reset<n>` | ~25 ns | 41 ns |

### Suite 7 — Histograms

| Operation | Mean | Median |
|-----------|------|--------|
| `histogram_record<n>` (in-range) | ~26 ns | 41 ns |
| `histogram_record<n>` (underflow / overflow) | ~26–27 ns | 41 ns |
| `histogram_reset<n>` | ~27 ns | 41 ns |

### Suite 9 — Report Generation

These are **not** hot-path operations, but it is useful to know how often you can safely call them.

| Report | Mean |
|--------|------|
| `get_counter_report()` | ~216 ns |
| `get_gauge_report()` | ~199 ns |
| `get_histogram_report()` | ~547 ns |
| `get_stats_report()` (merges all thread Welford accumulators) | ~27.9 µs |
| `get_stats_report_per_thread()` | ~51.3 µs |

Counter/gauge/histogram reports are in the hundreds-of-nanoseconds range. The timer stats report is expensive (~28 µs) because it acquires a global lock and merges per-thread Welford accumulators. Safe to call once per second.

### Suite 10 — Combined Hot Path

Simulates a fully annotated function: `make_scoped_timer` + handle-based timer + `make_scoped_counter` + `gauge_record` + `histogram_record`:

| Scenario | Mean |
|----------|------|
| Single thread | ~105 ns |
| 4 threads (same keys) | ~320 µs per batch of 4×500 iters |

## Multi-threaded Behaviour

### Counters under contention

4 threads, same atomic key: ~38.5 µs per batch of 500 iterations each (≈ 19 ns/iter amortised). Using a cached `counter_ref*` pointer reduces this to ~29.7 µs per batch.

### Timers under contention

Each thread has its **own thread-local `Slot`** — there is **zero contention** on the timer hot path. 4 threads running 500 iterations each: ~52 µs per batch (≈ 26 ns/iter, same cost as single-thread).

### Gauges and histograms under contention

Per-entry mutex. 4 threads on the **same key**: ~61 µs / ~57 µs per batch (fully serialised). 4 threads on **distinct keys**: ~40 µs per batch (parallel, no contention — same as single-thread).

## Overhead vs Raw Primitives

| Registry primitive | Mean | Raw primitive | Mean | Overhead |
|-------------------|------|--------------|------|---------|
| `counter_inc<n>` | ~22 ns | `atomic::fetch_add` | ~26 ns | ≈ 0 (both floor-dominated) |
| `gauge_record<n>` | ~24 ns | `mutex + Welford` | ~27 ns | ≈ 0 (both floor-dominated) |
| `histogram_record<n>` | ~26 ns | — | — | Similar to gauge |
| `start<n> + stop<n>` | ~55 ns | `2× clock + Welford` | ~77 ns | ~0 overhead (lookup cheaper than extra clock) |

The lookup overhead (one TLS array index) is sub-tick and does not show up as significant cost relative to the clock call cost (~42 ns each) on this platform.
