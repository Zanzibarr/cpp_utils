# benchmarking

> All symbols below live in `namespace utilz`; examples assume `using namespace utilz;`.

**Files:** [`benchmarking/benchmark.hxx`](../benchmarking/benchmark.hxx), [`benchmarking/bench_main.hpp`](../benchmarking/bench_main.hpp)
**Dependencies:** [ansi_colors](ansi_colors.md)

Lightweight micro-benchmark framework. Each benchmark case runs a user-provided loop body for a configurable number of iterations, records per-iteration timing, and prints color-coded statistics (mean, median, stddev, min, max) with auto-scaling time units.

## Registration Macros

```cpp
BENCH_SUITE("Suite name")                        // sets the current suite label

BENCH_CASE("Case name") { ... }                  // 1000 iterations, 10 warmup
BENCH_CASE_N("Case name", iters) { ... }         // explicit iteration count
BENCH_CASE_NW("Case name", iters, warmup) { ... } // explicit warmup count
```

Cases are registered at static init time and run in registration order.

## The `bench_state` Loop

Inside a benchmark body, iterate with a range-for over `state`. Each iteration records one timing sample:

```cpp
BENCH_CASE_N("vector push_back", 100'000) {
    for (auto _ : state) {
        std::vector<int> v;
        v.push_back(42);
        benchmark::DoNotOptimize(v.size());
    }
}
```

Each pass through the loop body is timed individually and the samples are aggregated into statistics.

## `DoNotOptimize()`

Prevents the compiler from eliminating computations as dead code:

```cpp
benchmark::DoNotOptimize(result);   // pass any value
```

Uses inline asm on GCC/Clang, volatile store on MSVC.

## Running Benchmarks

Use `bench_main.hpp` to provide a `main()` that runs all registered cases:

```cpp
// bench_main.cpp (or add to your benchmark file)
#include "../benchmarking/bench_main.hpp"
```

Or run manually:

```cpp
#include "benchmark.hxx"
int main() {
    benchmark::bench_registry::instance().run_all();
}
```

## Example

```cpp
#include "../benchmarking/bench_main.hpp"
#include "my_container.hxx"
#include <vector>

BENCH_SUITE("MyContainer vs std::vector")

BENCH_CASE_N("MyContainer::insert", 200'000) {
    for (auto _ : state) {
        MyContainer c;
        c.insert(42);
        benchmark::DoNotOptimize(c.size());
    }
}

BENCH_CASE_N("std::vector::push_back", 200'000) {
    for (auto _ : state) {
        std::vector<int> v;
        v.push_back(42);
        benchmark::DoNotOptimize(v.size());
    }
}
```

Compile and run:

```bash
g++ -std=c++20 -O2 bench.cpp -o bench && ./bench
```

## Output

```
Suite: MyContainer vs std::vector
  MyContainer::insert           mean: 12 ns  median: 11 ns  stddev: 2 ns  min: 10 ns  max: 18 ns
  std::vector::push_back        mean:  9 ns  median:  9 ns  stddev: 1 ns  min:  8 ns  max: 14 ns
```

Time units scale automatically (ns / µs / ms / s).

## Warmup

Warmup iterations run before sampling begins, allowing CPU frequency scaling and branch predictor warm-up. Default is 10 warmup iterations; use `BENCH_CASE_NW` to control both counts explicitly.
