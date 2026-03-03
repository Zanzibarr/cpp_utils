# parameters — Benchmarks

**Source:** [`benchmarks.cpp`](benchmarks.cpp)
**Full doc:** [`docs/parameters.md`](../docs/parameters.md)

Measures the per-call overhead of `ParameterRegistry::get()` versus reading the equivalent values from a plain struct. This answers the practical question: *what do I pay for named, runtime-configurable parameters instead of hardcoded struct fields?*

## Build and Run

```bash
g++ -std=c++20 -O2 benchmarks.cpp -o benchmarks && ./benchmarks
```

## What Is Measured

10 million iterations per benchmark. A `volatile`-style `sum` accumulator is printed at the end to prevent dead-code elimination.

Each `get<Name, T>()` call decomposes into:
1. Array index — `slots_[idx]` where `idx` is a compile-time constant
2. `std::get<T>` on a `std::variant` — one branch on the discriminant
3. Return `const T&` — identical to a struct field reference

No heap allocation, no string comparison, no virtual dispatch.

### Benchmarks

| Suite | What is compared |
|-------|-----------------|
| `double` | `s.learning_rate` vs `reg.get<"learning_rate", double>()` |
| `int64_t` | `s.batch_size` vs `reg.get<"batch_size", int64_t>()` |
| `bool` | `s.shuffle` vs `reg.get<"shuffle", bool>()` |
| `std::string` | `s.optimizer.size()` vs `reg.get<"optimizer", string>().size()` |
| Mixed 3 reads | 3 struct fields vs 3 `get<>` calls per iteration |

## Reference Results

Measured with `-O2` on an Apple M-series CPU (2026-03-02). Results will vary by hardware.

```
── double ──
struct field                            ~1.52 ns
ParameterRegistry::get<double>          ~1.96 ns   (+0.44 ns)

── int64_t ──
struct field                            ~1.04 ns
ParameterRegistry::get<int64_t>         ~1.33 ns   (+0.29 ns)

── bool ──
struct field                            ~0.81 ns
ParameterRegistry::get<bool>            ~1.14 ns   (+0.33 ns)

── std::string (read .size()) ──
struct field                            ~0.73 ns
ParameterRegistry::get<string>          ~1.06 ns   (+0.33 ns)

── mixed: 3 reads per iteration ──
struct (3 fields)                       ~2.07 ns
ParameterRegistry (3 gets)              ~2.55 ns   (+0.48 ns)
```

## Interpretation

The overhead per `get<>` call is **0.3–0.5 ns** — a single array subscript plus a variant discriminant branch. At 1 GHz effective throughput, this is 0.3–0.5 CPU cycles of overhead. The compiler inlines the variant access and elides all indirection.

In a realistic workload where parameters are read a few times per request or frame, this overhead is completely negligible. The break-even point where a plain struct would matter is roughly when you are reading parameters tens of millions of times per second in a tight loop with no other work.

## When to Choose `ParameterRegistry` Over a Struct

**Use ParameterRegistry when:**
- Parameters come from CLI flags, config files, or environment variables (use with [argparser](../docs/argparser.md))
- Parameters need to be shared across translation units without header changes
- You want named access and a built-in `print_report()` for debugging

**Use a plain struct when:**
- Parameters are compile-time constants
- You need the absolute minimum overhead in an ultra-tight loop (e.g. inner loop of a physics simulation reading the same value millions of times)
- No runtime configurability is needed
