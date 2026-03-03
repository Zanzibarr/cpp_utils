# binary_set — Benchmarks

**Source:** [`benchmarks.cpp`](benchmarks.cpp)
**Full doc:** [`docs/binary_set.md`](../docs/binary_set.md)

Every operation is compared against the most natural STL alternative: `std::set<unsigned int>`, `std::unordered_set<unsigned int>`, and `std::vector<bool>`. The goal is to answer: *what do I gain or lose by using `BinarySet` instead of a standard container?*

## Build and Run

```bash
g++ -std=c++20 -O2 -march=native benchmarks.cpp -o benchmarks && ./benchmarks
```

`-march=native` is recommended — it enables `std::countr_zero` hardware intrinsics (BSF/TZCNT) used in the iterator and set algebra.

## A Note on Measurement Resolution

On Apple Silicon (M-series), `steady_clock::now()` costs ~42 ns per call, creating a **~18–20 ns mean measurement floor**. Operations faster than one timer tick show a mean of ~18 ns (quantised between 0 and 41 ns per sample) with a median of 0 ns. For these cases, the mean is an upper bound, not the exact cost. Operations above ~40 ns are more reliably measured.

## Test Fixtures

Three capacity sizes are benchmarked at two fill densities each:

| Size | Capacity | Sparse (~10% fill) | Dense (~90% fill) |
|------|----------|-------------------|------------------|
| Small | 64 | ~6 elements | ~57 elements |
| Medium | 256 | ~26 elements | ~230 elements |
| Large | 1024 | ~103 elements | ~922 elements |

Same random seed (12345) every run for reproducibility.

## Suite Overview

| Suite | What is measured |
|-------|-----------------|
| 0 · Baselines | STL insert single element (orientation numbers) |
| 1 · Construction | Empty, full, copy at small and large sizes |
| 2 · Single-element ops | `add` / `remove` / `contains` vs STL equivalents |
| 3 · Bulk conversion | `sparse()` vs copying `std::set` to `vector` |
| 4 · Set algebra (non-mutating) | `\|`, `&`, `-`, `^`, `!` vs STL algorithms |
| 5 · In-place algebra | `\|=`, `&=`, `-=`, `^=` |
| 6 · Subset / superset tests | `subset_of`, `superset_of`, `intersects` vs `std::includes` |
| 7 · Iteration | Range-for vs `std::set` / `unordered_set` iteration |

## Reference Results

Measured with `-O2 -march=native` on an Apple M-series CPU.

### Suite 0 — Baselines

| Operation | Mean |
|-----------|------|
| `std::set::insert` single element (small, sparse) | ~338 ns |
| `std::unordered_set::insert` single element (small, sparse) | ~216 ns |
| `std::vector<bool>` set single bit (small) | ~67 ns |
| **`BinarySet::add` single element (small, sparse)** | **~34 ns** |

### Suite 1 — Construction

| Operation | Mean |
|-----------|------|
| `BinarySet(64)` empty | ~19 ns |
| `BinarySet(64, true)` full | ~18 ns |
| `BinarySet(64)` copy | ~19 ns |
| `std::vector<bool>(64, false)` | ~18 ns |
| `BinarySet(1024)` empty | ~18 ns |
| `BinarySet(1024)` copy sparse | ~18 ns |
| `BinarySet(1024)` copy dense | ~18 ns |
| `std::vector<bool>(1024, false)` | ~18 ns |
| **`std::set<uint>` copy (sparse, cap=1024, ~102 elements)** | **~2.92 µs** |

All `BinarySet` and `vector<bool>` construction costs are at the measurement floor (~18 ns) — they complete in sub-tick time. `std::set` copy is ~2.92 µs, making `BinarySet` copy **~160× faster** for large sparse sets.

### Suite 2 — Single-element operations

All operations below are at or near the measurement floor (~18 ns) — they are all sub-tick on this platform.

| Operation | Mean |
|-----------|------|
| `BinarySet::contains` (small, dense or sparse) | ~18 ns |
| `BinarySet::add` (small, any density) | ~34 ns |
| `BinarySet::remove` (small) | ~34 ns |
| `std::set::count` (small, dense) | ~18 ns |
| `std::unordered_set::count` (small, dense) | ~18 ns |
| `std::vector<bool> operator[]` (small) | ~18 ns |

For single-element lookups, all containers are floor-dominated and indistinguishable at this scale. `BinarySet::add`/`remove` are ~34 ns because they also copy the fixture set at the start of each iteration.

### Suite 3 — Bulk conversion

| Operation | Mean |
|-----------|------|
| `BinarySet::sparse()` medium sparse (~26 elements) | ~55 ns |
| `BinarySet::sparse()` medium dense (~230 elements) | ~307 ns |
| `std::set` copy into vector, medium sparse | ~96 ns |
| `std::unordered_set` copy into vector, medium sparse | ~45 ns |
| `BinarySet::sparse()` large sparse (~103 elements) | ~129 ns |
| `BinarySet::sparse()` large dense (~922 elements) | ~1.08 µs |
| `std::set` copy into vector, large sparse | ~409 ns |

`BinarySet::sparse()` for sparse sets is **1.7× faster** than `std::set` copy (55 ns vs 96 ns, medium). For dense sets it is slower than `unordered_set` because it must scan the full word array — cost is proportional to capacity, not element count.

### Suite 4 — Set algebra (non-mutating)

BinarySet algebra cost is **independent of density** — it processes all words in the capacity regardless of fill.

#### Small (cap=64)

| Operation | BinarySet | std::set algorithm | vector<bool> loop |
|-----------|----------|--------------------|------------------|
| Intersection (`&`) sparse | **~36 ns** | ~68 ns | ~82 ns |
| Union (`\|`) sparse | **~35 ns** | ~295 ns | — |
| Difference (`-`) sparse | **~36 ns** | ~158 ns | — |
| Symmetric diff (`^`) sparse | **~36 ns** | ~263 ns | — |
| Complement (`!`) sparse | **~36 ns** | N/A | — |

At small capacity BinarySet is **2–8× faster** than STL algorithms. `std::set_intersection` is closest (2×) because with only ~6 sparse elements the merge loop is short.

#### Medium (cap=256)

| Operation | BinarySet | std::set algorithm | vector<bool> loop |
|-----------|----------|--------------------|------------------|
| Intersection (`&`) sparse | **~32 ns** | ~252 ns | ~460 ns |
| Intersection (`&`) dense | **~34 ns** | ~8.9 µs | — |
| Union (`\|`) dense | **~35 ns** | ~10.3 µs | — |
| Difference (`-`) dense | **~34 ns** | — | — |
| Complement (`!`) dense | **~34 ns** | — | — |

At medium capacity BinarySet is **8–300× faster** than STL set algorithms. Note that `std::vector<bool>` element-wise AND is **~460 ns** at medium capacity — `vector<bool>`'s bit-proxy abstraction makes element-by-element loops very slow. BinarySet's word-at-a-time operations are 14× faster.

#### Large (cap=1024)

| Operation | BinarySet | std::set algorithm | vector<bool> loop |
|-----------|----------|--------------------|------------------|
| Intersection (`&`) sparse | **~41 ns** | ~1.34 µs | ~1.80 µs |
| Intersection (`&`) dense | **~39 ns** | ~36.6 µs | — |
| Union (`\|`) dense | **~42 ns** | — | — |
| Symmetric diff (`^`) dense | **~39 ns** | — | — |
| Complement (`!`) dense | **~39 ns** | — | — |

At large capacity BinarySet is **30–900× faster** than `std::set` algorithms and **40× faster** than `vector<bool>` loops. BinarySet cost stays nearly flat as capacity grows (41 ns at 1024 vs 36 ns at 64) because it is limited by memory bandwidth, not element count.

### Suite 5 — In-place algebra (medium, cap=256)

| Operation | Mean |
|-----------|------|
| `BinarySet &=` sparse | ~34 ns |
| `BinarySet \|=` sparse | ~34 ns |
| `BinarySet -=` sparse | ~38 ns |
| `BinarySet ^=` sparse | ~37 ns |
| `BinarySet &=` dense | ~35 ns |
| `BinarySet \|=` dense | ~34 ns |
| `vector<bool>` element-wise `&=` loop, sparse | ~986 ns |

In-place operators avoid allocating a result object. `vector<bool>` manual loop is ~29× slower due to bit-proxy overhead.

### Suite 6 — Subset / superset / intersects

#### Medium (cap=256)

| Operation | Mean |
|-----------|------|
| `BinarySet::subset_of` sparse ⊆ dense | ~19 ns |
| `BinarySet::subset_of` sparse ⊆ sparse2 | ~19 ns |
| `BinarySet::superset_of` dense ⊇ sparse | ~19 ns |
| `BinarySet::intersects` sparse & sparse2 | ~18 ns |
| `BinarySet::intersects` sparse & dense | ~18 ns |
| `std::includes` sparse ⊆ dense (std::set) | ~428 ns |
| `vector<bool>` manual subset loop | ~83 ns |

`BinarySet::subset_of` is **22× faster** than `std::includes` and **4× faster** than the manual `vector<bool>` loop. All `BinarySet` membership tests are at the measurement floor (~18 ns).

#### Large (cap=1024)

| Operation | Mean |
|-----------|------|
| `BinarySet::subset_of` sparse ⊆ dense | ~19 ns |
| `BinarySet::intersects` sparse & dense | ~19 ns |
| `BinarySet::intersects` sparse & sparse2 | ~18 ns |
| `std::includes` sparse ⊆ dense (std::set) | ~69 ns |

Even for large sets, `BinarySet::subset_of` stays at ~19 ns (floor-dominated) because the word-at-a-time loop finishes in under one clock tick.

### Suite 7 — Iteration

| Operation | Mean |
|-----------|------|
| `BinarySet` range-for small sparse (~6 elements) | ~19 ns |
| `BinarySet` range-for small dense (~57 elements) | ~36 ns |
| `std::set` range-for small sparse | ~19 ns |
| `BinarySet` range-for medium sparse (~26 elements) | ~22 ns |
| `BinarySet` range-for medium dense (~230 elements) | ~154 ns |
| `std::set` range-for medium sparse | ~42 ns |
| `std::set` range-for medium dense | ~517 ns |
| `vector<bool>` scan-all medium dense | ~108 ns |
| `BinarySet` range-for large sparse (~103 elements) | ~67 ns |
| `BinarySet` range-for large dense (~922 elements) | ~548 ns |
| `std::set` range-for large sparse | ~198 ns |
| `std::set` range-for large dense | ~2.10 µs |
| `vector<bool>` scan-all large sparse | ~319 ns |
| `vector<bool>` scan-all large dense | ~421 ns |

For sparse sets, `BinarySet` iteration is **2–3× faster** than `std::set` (no pointer chasing). For dense sets, `BinarySet` and `vector<bool>` are comparable; `std::set` is **3–4× slower** due to cache-unfriendly tree traversal.

## When to Use BinarySet

| Scenario | Recommendation |
|----------|---------------|
| Frequent set algebra (intersection, union, etc.) | **BinarySet** — word-at-a-time is 2–900× faster |
| Many copies or cheap clone needed | **BinarySet** — flat memcpy vs tree traversal (~160× faster) |
| Subset / superset checks | **BinarySet** — 22× faster than `std::includes` |
| Integer universe ≤ a few thousand | **BinarySet** |
| Arbitrary key type (strings, structs) | `std::set` / `std::unordered_set` |
| Dynamic capacity (elements outside initial range) | `std::set` / `std::unordered_set` |
| Fast membership read only, no algebra, no size tracking | `std::vector<bool>` or `std::bitset` |
| Element-wise loops on the bit representation | Avoid `vector<bool>` — bit proxy makes loops ~29× slower than `BinarySet` |

`BinarySet` memory use is `capacity / 8` bytes regardless of fill — 1024 elements costs 128 bytes. This is always less than `std::set` (48+ bytes per node × element count) for any non-trivial fill density.
