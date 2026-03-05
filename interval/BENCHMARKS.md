# interval — Benchmarks

**Source:** [`benchmarks.cpp`](benchmarks.cpp)
**Full doc:** [`docs/interval.md`](../docs/interval.md)

Measures the cost of every `Interval<T>` operation: construction, queries, arithmetic operations, normalization, and equality. This answers the question: *how much does each interval operation actually cost, and is the `std::optional` return from `intersect()` free?*

## Build and Run

```bash
g++ -std=c++20 -O2 benchmarks.cpp -o benchmarks && ./benchmarks
```

## A Note on Measurement Resolution

On Apple Silicon (M-series), `steady_clock::now()` costs ~42 ns per call, creating a **~18–42 ns measurement floor**. All `Interval<T>` operations complete in sub-tick time (mean 28–37 ns, min 0 ns). For these cases the mean is an upper bound, not the exact cost — the operations themselves are cheaper than one timer tick.

## Suite Overview

| Suite | What is measured |
|-------|-----------------|
| 1 · Construction | `Interval(a,b)`, `make_empty()`, `make_universe()` |
| 2 · Queries | `contains(value)`, `contains(Interval)`, `overlaps()`, `is_empty()`, `length()`, `center()` |
| 3 · Operations | `clamp()`, `merge()`, `intersect()`, `expand()`, `translate()` |
| 4 · Normalization | `normalize()`, `denormalize()`, round-trip |
| 5 · Operators | `operator==`, `operator!=` |
| 6 · Mixed | Realistic sequences combining construction, queries, and operations |

## Reference Results

Measured with `-O2` on an Apple M-series CPU (2026-03-05). All operations are floor-dominated.

| Operation | Mean | Min |
|-----------|------|-----|
| `Interval<int>(a, b)` construction | ~35 ns | 0 ns |
| `Interval<double>(a, b)` construction | ~33 ns | 0 ns |
| `Interval<int>::make_empty()` | ~29 ns | 0 ns |
| `Interval<int>::make_universe()` | ~29 ns | 0 ns |
| `contains(int value)` | ~30–31 ns | 0 ns |
| `contains(Interval)` | ~29 ns | 0 ns |
| `overlaps()` | ~29–37 ns | 0 ns |
| `is_empty()` | ~29 ns | 0 ns |
| `length()` | ~29 ns | 0 ns |
| `center()` | ~29 ns | 0 ns |
| `clamp()` | ~29 ns | 0 ns |
| `merge()` | ~29–32 ns | 0 ns |
| `intersect()` — returns `Some` | ~30 ns | 0 ns |
| `intersect()` — returns `nullopt` | ~30 ns | 0 ns |
| `expand()` | ~30 ns | 0 ns |
| `translate()` | ~30 ns | 0 ns |
| `normalize()` | ~29 ns | 0 ns |
| `denormalize()` | ~29 ns | 0 ns |
| normalize + denormalize round-trip | ~29 ns | 0 ns |
| `operator==` | ~29 ns | 0 ns |
| `operator!=` | ~29 ns | 0 ns |
| construct + contains + clamp | ~30 ns | 0 ns |
| merge three intervals | ~29 ns | 0 ns |
| intersect chain (double) | ~36 ns | 0 ns |

## Interpretation

Every `Interval<T>` operation is a handful of arithmetic comparisons on two values stored inline. There is no heap allocation, no virtual dispatch, and no branching beyond the comparisons themselves. The compiler inlines all operations and elides the `std::optional` wrapper for `intersect()` in most cases.

The measurement floor (~42 ns timer tick on Apple Silicon) means we cannot distinguish operations from one another at this scale — they all complete in under one tick. The actual cost is in the **low single-digit nanosecond range**.

`intersect()` returning `std::optional<Interval<T>>` has **no measurable overhead** compared to plain accessors: the optional is typically returned in registers and the compiler elides any copy.
