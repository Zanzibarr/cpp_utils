# stats_registry

> All symbols below live in `namespace utilz`; examples assume `using namespace utilz;`.

**File:** [`stats_registry/stats_registry.hxx`](../stats_registry/stats_registry.hxx)
**Dependencies:** [timer](timer.md) → [ct_string](ct_string.md)
**Benchmarks:** [stats_registry/BENCHMARKS.md](../stats_registry/BENCHMARKS.md)

Extends `TimerRegistry` with atomic counters, Welford gauges, and histograms. All primitives are named via compile-time strings and designed for low overhead in multi-threaded hot paths.

## Inheritance

`StatsRegistry : TimerRegistry`

All timer API (`start`, `stop`, `make_scoped_timer`, `print_stats_report`, etc.) is available unchanged.

## Counters

Atomic, lock-free on every operation.

```cpp
reg.counter_inc<"requests">();          // fetch_add(1)
reg.counter_inc<"bytes_sent">(n);       // fetch_add(n)
reg.counter_dec<"in_flight">();         // fetch_sub(1)
reg.counter_set<"connections">(42);     // atomic store
int64_t v = reg.counter_get<"requests">(); // atomic load

// Cached pointer — no lookup overhead in the loop
std::atomic<int64_t>* ref = reg.counter_ref<"requests">();
ref->fetch_add(1, std::memory_order_relaxed);  // raw atomic

reg.counter_reset<"requests">();        // store 0
```

### RAII scoped counter

```cpp
// Increments on construction, decrements on destruction
auto in_flight = make_scoped_counter<"in_flight">(reg);
```

## Gauges

Welford accumulator protected by a per-entry mutex. Use for recording distributions of floating-point values (sizes, durations, latencies).

```cpp
reg.gauge_record<"payload_bytes">(128.0);
reg.gauge_reset<"payload_bytes">();
```

Report includes: count, total, mean, min, max, stddev.

## Histograms

Equal-width bins with underflow/overflow tracking.

```cpp
// Create once (at startup / first use)
reg.histogram_create<"latency_ms">(0.0, 500.0, 10);  // 10 buckets [0, 500)

// Record values anywhere
reg.histogram_record<"latency_ms">(42.3);
reg.histogram_record<"latency_ms">(1000.0);  // goes to overflow bucket

reg.histogram_reset<"latency_ms">();
```

Report includes bucket counts and an ASCII bar chart.

## Series

Records an ordered sequence of `(timestamp_ns, double)` samples — the full history is preserved so you can inspect or plot how a value evolved over time (e.g. MIP solver incumbents, lower bounds, loss curves).

```cpp
// Append a timestamped value — lock-free after first call per thread/name.
reg.series_push<"incumbent">(1823.4);
reg.series_push<"incumbent">(1501.2);

// Retrieve all points for a single name (sorted by wall-clock time).
// Returns std::vector<stats_detail::SeriesPoint> where each point has:
//   int64_t timestamp_ns   — steady_clock nanoseconds since epoch
//   double  value
auto pts = reg.series_get<"incumbent">();
for (auto& p : pts) { /* plot p.timestamp_ns, p.value */ }

// Clear all stored points and start fresh.
reg.series_reset<"incumbent">();
```

### Report

```cpp
reg.print_series_report();           // compact preview for each series

// Programmatic access: one SeriesRow per name.
for (auto& row : reg.get_series_report()) {
    // row.name   — std::string
    // row.points — std::vector<SeriesPoint>, sorted by timestamp_ns
}
```

**Constraint:** do not call `series_get` / `get_series_report` while another thread is concurrently calling `series_push` for the same name. The typical pattern — push phase finishes, then report — is fully safe.

### Thread safety

| Operation | Lock held |
|---|---|
| `series_push` (first call per thread/name) | `stats_mutex_` once |
| `series_push` (subsequent calls) | **none** — fully lock-free |
| `series_get` / `get_series_report` | `stats_mutex_` |
| `series_reset` | `stats_mutex_` |
| Thread exit (flush to archive) | `stats_mutex_` |

## Reports

```cpp
reg.print_counter_report();          // all counters
reg.print_gauge_report();            // all gauges (Welford stats)
reg.print_histogram_report();        // all histograms + bar charts
reg.print_series_report();           // all series (timestamped sequences)
reg.print_stats_report();            // all timers (inherited)
```

## Global Registry

```cpp
StatsRegistry& global_stats();
#define STATS global_stats()

STATS.counter_inc<"requests">();
STATS.print_counter_report();
```

**Limit:** up to 128 entries each for timers, counters, gauges, histograms, and series.

## Thread Safety

| Primitive | Mechanism | Hot-path contention |
|-----------|-----------|-------------------|
| Counters | `std::atomic` | None — lock-free |
| Timers | thread-local slots | None on hot path |
| Gauges | per-entry `std::mutex` | Serialised per key |
| Histograms | per-entry `std::mutex` | Serialised per key |
| Series | thread-local `std::vector` | None — lock-free after first push |

For high-frequency gauge/histogram recording under contention, use distinct key names per thread. Series push is always lock-free on the hot path.

## Performance

See [BENCHMARKS.md](../stats_registry/BENCHMARKS.md) for full measurements. Reference numbers (single thread, after warm-up):

| Operation | Cost |
|-----------|------|
| `counter_inc<n>` | ~17 ns |
| `gauge_record<n>` | ~20 ns |
| `histogram_record<n>` (in-range) | ~18–19 ns |
| `start<n>` + `stop<n>` | ~45–50 ns |
| `series_push<n>` (hot path, single-thread) | ~37 ns |
| `series_get<n>` (merge + sort 1 000 pts) | ~860 ns |
| Combined hot path (timer + counter + gauge + histogram) | ~99 ns |

## Typical Usage Pattern

```cpp
#include "stats_registry.hxx"

StatsRegistry stats;
stats.histogram_create<"req_ms">(0.0, 1000.0, 20);

void handle_request() {
    auto total   = make_scoped_timer<"req_total">(stats);
    auto flight  = make_scoped_counter<"in_flight">(stats);

    // ... do work ...
    stats.counter_inc<"requests">();
    stats.gauge_record<"payload">(payload_size);
    stats.histogram_record<"req_ms">(elapsed_ms);
}

// Periodically
stats.print_counter_report();
stats.print_gauge_report();
stats.print_histogram_report();
stats.print_stats_report();
```
