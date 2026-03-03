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
| Sync / string | `LOG_INFO_S(msg)` — string API, sync mode |
| Sync / stream | `LOG_INFO << msg` — stream API, sync mode |
| Filter / string | `LOG_INFO_S` when min level is ERROR (INFO silenced) |
| Filter / stream | `LOG_INFO <<` when min level is ERROR (INFO silenced) |

## Reference Results

Measured on an Apple M-series CPU (2026-03-02):

```
[baseline       ] total:   2.94 ms   (1,000,000 iterations)
[sync  / string ] total: 187.83 ms   overhead: 184.88 ms   per-call: 187.83 ns
[sync  / stream ] total: 325.79 ms   overhead: 322.85 ms   per-call: 325.79 ns
[filter/ string ] total:  27.97 ms   overhead:  25.03 ms   per-call:  25.08 ns
[filter/ stream ] total:   6.28 ms   overhead:   3.34 ms   per-call:   3.34 ns
```

## Interpretation

### Sync mode overhead

- **String API (`LOG_INFO_S`):** ~188 ns — includes mutex acquisition, timestamp formatting (manual char-array), level check, write to `ostream`.
- **Stream API (`LOG_INFO <<`):** ~326 ns — adds `log_stream` construction and `operator<<` chaining on top of the above.

Prefer the string API (`_S` macros) when the message is already formatted, and the stream API when you need to compose from multiple values.

### Filtered-out messages

When `set_min_level(ERROR)` silences `INFO`:

- **Filtered string:** ~25 ns — one level comparison and return; no allocation, no formatting.
- **Filtered stream:** ~3–4 ns — `log_stream` destructs immediately when it detects the level is filtered; the compiler can often eliminate the entire expression.

This means you can leave `LOG_DEBUG` calls in production code with a WARNING-or-above filter and pay only 3–25 ns per filtered call — well within budget for most hot paths.

### Async mode

Async mode (not measured here) offloads the `ostream` write to a background thread. The calling-thread cost drops to a queue push (~50–80 ns on an uncontended queue), making async preferable for latency-sensitive paths that log frequently.

## Choosing Sync vs Async

| | Sync | Async |
|--|------|-------|
| Call cost (active) | ~188–326 ns | ~50–80 ns (queue push) |
| Call cost (filtered) | ~3–25 ns | Same |
| Output order | Strict | FIFO via queue |
| Simplicity | Higher | Requires `flush()` at exit |
| Use case | Debugging, low-frequency logging | High-frequency logging, latency-sensitive |

## Comparison vs `printf` / `std::cout`

A raw `printf` or `std::cout` write to `/dev/null` typically costs 50–150 ns with no formatting overhead. The logger's ~188 ns sync cost includes mutex, timestamp, level prefix, and color codes — comparable to a formatted `printf` with a mutex guard.

For reference, `spdlog` (a popular C++ logging library) reports ~50–100 ns in sync mode on similar hardware. The logger here is in the same order of magnitude with no external dependencies.
