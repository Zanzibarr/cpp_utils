# parameters

> All symbols below live in `namespace utilz`; examples assume `using namespace utilz;`.

**File:** [`parameters/parameters.hxx`](../parameters/parameters.hxx)
**Dependencies:** [ct_string](ct_string.md)
**Benchmarks:** [parameters/BENCHMARKS.md](../parameters/BENCHMARKS.md)

O(1) named parameter registry. Store and retrieve typed values by compile-time name with no string comparison at runtime — overhead is nearly identical to a plain struct field access.

## Key Type

### `ParameterRegistry`

```cpp
class ParameterRegistry {
public:
    template <CTString Name, typename T>
    void set(T&& value);

    template <CTString Name, typename T>
    const T& get() const;

    template <CTString Name>
    bool has() const;

    void print_report() const;   // formatted table to stdout
};
```

**Supported types:** `int64_t`, `double`, `bool`, `std::string`

**Automatic coercion:**
- `bool` literals → `bool`
- any integral type → `int64_t`
- any floating-point type → `double`
- `const char*` / `std::string_view` → `std::string`

**Limit:** up to 128 parameters per registry (`MAX_CT_PARAMS`).

## Global Registry

```cpp
ParameterRegistry& global_params();   // singleton
#define PARAMS global_params()

PARAMS.set<"batch_size">(32);
int64_t bs = PARAMS.get<"batch_size", int64_t>();
```

## Usage

```cpp
#include "parameters.hxx"

ParameterRegistry reg;

// Set (typically at startup)
reg.set<"learning_rate">(0.001);   // stored as double
reg.set<"batch_size">(32);         // stored as int64_t
reg.set<"shuffle">(true);          // stored as bool
reg.set<"optimizer">("adam");      // stored as std::string

// Get (hot path — O(1), no string lookup)
double lr  = reg.get<"learning_rate", double>();
int64_t bs = reg.get<"batch_size",    int64_t>();
bool shuf  = reg.get<"shuffle",       bool>();
const std::string& opt = reg.get<"optimizer", std::string>();

// Check presence
if (reg.has<"dropout">()) { ... }

// Print all parameters
reg.print_report();
```

## Performance

See [BENCHMARKS.md](../parameters/BENCHMARKS.md) for full results. Summary:

| Operation | ParameterRegistry | Plain struct | Overhead |
|-----------|------------------|-------------|---------|
| `get<double>` | ~1.96 ns | ~1.52 ns | +0.44 ns |
| `get<int64_t>` | ~1.33 ns | ~1.04 ns | +0.29 ns |
| `get<bool>` | ~1.14 ns | ~0.81 ns | +0.33 ns |
| `get<string>` | ~1.06 ns | ~0.73 ns | +0.33 ns |
| 3 mixed reads | ~2.55 ns | ~2.07 ns | +0.48 ns |

The overhead is ~0.3–0.5 ns per call — a single array index + variant discriminant check. There is no heap allocation, no virtual dispatch, no string comparison.

## When to Use

Use `ParameterRegistry` over a plain struct when you need:
- Runtime-configurable named parameters (e.g. from a config file or CLI)
- Integration with [argparser](argparser.md) which populates the registry automatically
- A shared global store accessible from multiple translation units without a header change
