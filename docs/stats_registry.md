# stats_registry

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

## Reports

```cpp
reg.print_counter_report();          // all counters
reg.print_gauge_report();            // all gauges (Welford stats)
reg.print_histogram_report();        // all histograms + bar charts
reg.print_stats_report();            // all timers (inherited)
```

## Global Registry

```cpp
StatsRegistry& global_stats();
#define STATS global_stats()

STATS.counter_inc<"requests">();
STATS.print_counter_report();
```

**Limit:** up to 128 entries each for timers, counters, gauges, histograms.

## Thread Safety

| Primitive | Mechanism | Hot-path contention |
|-----------|-----------|-------------------|
| Counters | `std::atomic` | None — lock-free |
| Timers | thread-local slots | None on hot path |
| Gauges | per-entry `std::mutex` | Serialised per key |
| Histograms | per-entry `std::mutex` | Serialised per key |

For high-frequency gauge/histogram recording under contention, use distinct key names per thread.

## Performance

See [BENCHMARKS.md](../stats_registry/BENCHMARKS.md) for full measurements. Reference numbers (single thread, after warm-up):

| Operation | Cost |
|-----------|------|
| `counter_inc<n>` | ~17 ns |
| `gauge_record<n>` | ~20 ns |
| `histogram_record<n>` (in-range) | ~18–19 ns |
| `start<n>` + `stop<n>` | ~45–50 ns |
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
