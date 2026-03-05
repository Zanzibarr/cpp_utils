# argparser — Benchmarks

**Source:** [`benchmarks.cpp`](benchmarks.cpp)
**Full doc:** [`docs/argparser.md`](../docs/argparser.md)

Measures the overhead of argument registration, parsing, value retrieval, and validation in `ArgParser`. This answers the practical question: *what do I pay to parse CLI flags at startup versus reading from a plain struct at runtime?*

## Build and Run

```bash
g++ -std=c++20 -O2 benchmarks.cpp -o benchmarks && ./benchmarks
```

## Suite Overview

| Suite | What is measured |
|-------|-----------------|
| 1 · Argument registration | `ArgParser` construction and `add<Name, T>()` calls |
| 2 · Parsing | `parse()` for 1, 4, and 8 CLI arguments; defaults-only path |
| 3 · Value retrieval | `get<Name, T>()` for each supported type after parse |
| 4 · Validation overhead | `min`/`max` range check and `allow(choices)` check during parse |

## Reference Results

Measured with `-O2` on an Apple M-series CPU (2026-03-05). Results will vary by hardware.

### Suite 1 — Argument registration

| Operation | Mean |
|-----------|------|
| `ArgParser` construction (empty) | ~544 ns |
| `add<Name, int>()` — one argument | ~406 ns |
| `add` four mixed-type arguments | ~498 ns |

Registration is a one-time startup cost. Each `add<>()` call stores metadata in a `vector` and registers the parameter name in the underlying `ParameterRegistry` at compile time.

### Suite 2 — Parsing

| Operation | Mean |
|-----------|------|
| `parse()` — 1 int argument | ~293 ns |
| `parse()` — 4 mixed-type arguments | ~1.07 µs |
| `parse()` — defaults only (no CLI args) | ~179 ns |
| `parse()` — 8 int arguments | ~2.46 µs |

Parsing scales roughly linearly with the number of arguments provided on the command line. The defaults-only path is the fastest because it skips all token matching and string conversion. Each parsed argument requires a string comparison for flag matching and a type conversion (e.g. `std::stoi`, `std::stod`).

### Suite 3 — Value retrieval

All `get<Name, T>()` calls are floor-dominated on Apple Silicon (~18–42 ns measurement floor).

| Operation | Mean |
|-----------|------|
| `get<int>` after parse | ~23 ns |
| `get<double>` after parse | ~27 ns |
| `get<bool>` after parse | ~23 ns |
| `get<string>` after parse | ~23 ns |
| Four mixed `get()` calls per iteration | ~24 ns |

These numbers reflect sub-tick operations: the underlying `ParameterRegistry::get<>()` is a flat array subscript plus a `std::variant` discriminant check. No string lookup or heap access occurs at read time.

### Suite 4 — Validation overhead

| Operation | Mean |
|-----------|------|
| `parse()` with `min`/`max` range check | ~256 ns |
| `parse()` with `allow(choices)` (3 options) | ~244 ns |

Validation adds negligible overhead over plain parsing (~256 ns vs ~293 ns for a single argument). Both `min`/`max` and `allow()` checks are O(1) and O(n choices) respectively, run once per argument at parse time, and have no runtime cost after `parse()` returns.

## Interpretation

`ArgParser` is intended for startup-time use (CLI parsing), not inner-loop use. The costs are:

- **Registration (`add<>`)**: ~400–550 ns each — a one-time cost proportional to the number of arguments defined
- **Parsing**: ~180–300 ns per argument — dominated by string matching and type conversion
- **Retrieval (`get<>`)**: sub-tick, same as `ParameterRegistry` — negligible at any call rate

In a typical program that parses flags once at startup and reads values throughout execution, the total parsing cost is in the low-microsecond range and completely irrelevant to overall performance.
