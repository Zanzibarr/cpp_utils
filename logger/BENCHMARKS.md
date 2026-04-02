# logger — Benchmarks

**Source:** [`benchmarks.cpp`](benchmarks.cpp)
**Full doc:** [`docs/logger.md`](../docs/logger.md)

Measures the per-call cost of logging at different levels and modes, compared against a baseline empty loop. Output is suppressed via `rdbuf` swap so terminal I/O does not inflate the numbers.

## Build and Run

```bash
g++ -std=c++20 -O2 -pthread benchmarks.cpp -o benchmarks && ./benchmarks
```

## What Is Measured

1,000,000 iterations per variant.

| Variant | Description |
|---------|-------------|
| Baseline | Empty loop with `volatile int sink` accumulator |
| Sync / string | `lg.info(msg)` — string API, sync mode |
| Sync / stream | `lg[INFO] << msg` — stream API, sync mode |
| Filter / string | `lg.info(msg)` when min level is ERROR (INFO silenced) |
| Filter / stream | `lg[INFO] << msg` when min level is ERROR (INFO silenced) |

## Reference Results

### 2026-04-02 (after stream-path optimisation)

Measured on an Apple M-series CPU:

```
[baseline       ] total:   2.37 ms   (1,000,000 iterations)
[sync  / string ] total: 101.26 ms   overhead:  98.89 ms   per-call: 101.26 ns
[sync  / stream ] total:  93.66 ms   overhead:  91.29 ms   per-call:  93.66 ns
[filter/ string ] total:   3.04 ms   overhead:   0.67 ms   per-call:   3.04 ns
[filter/ stream ] total:   1.49 ms   overhead:  ~0.00 ms   per-call:   1.49 ns
```

### 2026-03-02 (baseline, before optimisation)

```
[baseline       ] total:   2.94 ms   (1,000,000 iterations)
[sync  / string ] total: 187.83 ms   overhead: 184.88 ms   per-call: 187.83 ns
[sync  / stream ] total: 325.79 ms   overhead: 322.85 ms   per-call: 325.79 ns
[filter/ string ] total:  27.97 ms   overhead:  25.03 ms   per-call:  27.97 ns
[filter/ stream ] total:   6.28 ms   overhead:   3.34 ms   per-call:   6.28 ns
```

## Interpretation

### Sync mode overhead

Both APIs now have essentially the same per-call cost (~95–105 ns), dominated by the shared work: mutex acquisition, `steady_clock::now()`, timestamp formatting, and the `ostream` write.

- **String API:** passes a `string_view` into `emit()`, which allocates and copies it into the log record.
- **Stream API:** accumulates into a thread-local buffer (no allocation), then **moves** that buffer into the log record — no copy.

The two costs are in the same ballpark; on any given run either may measure marginally faster due to noise.

Previously the stream API was ~1.7–2× slower because it allocated a fresh `std::ostringstream` per call and copied the buffer twice. That overhead is now gone.

### Filtered-out messages

When `set_min_level(ERROR)` silences `INFO`:

- **String (`lg.info(msg)`):** ~3 ns — one atomic level comparison and return; no allocation.
- **Stream (`lg.info() << msg`):** ~1–2 ns — `log_stream` destructs immediately when it detects the level is filtered; the compiler often eliminates the expression entirely.

Both filtered costs are negligible. `LOG_DEBUG` calls can be left in production code with a WARNING-or-above filter at effectively zero overhead.

### Async mode

Async mode (not measured here) offloads the `ostream` write to a background thread. The calling-thread cost drops to a queue push (~50–80 ns on an uncontended queue), making async preferable for latency-sensitive paths that log frequently.

## Choosing Sync vs Async

| | Sync | Async |
|--|------|-------|
| Call cost (active) | ~95–105 ns | ~50–80 ns (queue push) |
| Call cost (filtered) | ~1–3 ns | Same |
| Output order | Strict | FIFO via queue |
| Simplicity | Higher | Requires `flush()` at exit |
| Use case | Debugging, low-frequency logging | High-frequency logging, latency-sensitive |

## Comparison vs `printf` / `std::cout`

A raw `printf` or `std::cout` write to `/dev/null` typically costs 50–150 ns with no formatting overhead. The logger's ~100 ns sync cost includes mutex, timestamp, level prefix, and color codes — comparable to a formatted `printf` with a mutex guard.

For reference, `spdlog` (a popular C++ logging library) reports ~50–100 ns in sync mode on similar hardware. The logger here is in the same order of magnitude with no external dependencies.
