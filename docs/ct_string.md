# ct_string

**File:** [`utilities/ct_string.hxx`](../utilities/ct_string.hxx)
**Dependencies:** none

Compile-time string literal type usable as a non-type template parameter (NTTP), plus an FNV-1a hash function. This is the foundation for the O(1) name-lookup mechanism used by [parameters](parameters.md), [timer](timer.md), and [stats_registry](stats_registry.md).

## Key Types

### `CTString<N>`

A fixed-size wrapper around a string literal that can be used as a template argument.

```cpp
template <std::size_t N>
struct CTString {
    char data[N]{};
    consteval CTString(const char (&s)[N]);   // implicit from string literal
    consteval bool operator==(const CTString&) const;
    constexpr std::string_view view() const;
};

// CTAD: type deduced from the literal
CTString s = "hello";  // CTString<6>
```

### `hash_name(std::string_view)`

64-bit FNV-1a hash, usable at compile time or runtime.

```cpp
constexpr uint64_t hash_name(std::string_view sv);
```

## How the O(1) Lookup Works

Registries (ParameterRegistry, TimerRegistry, StatsRegistry) use `CTString` as template parameters:

```cpp
registry.set<"learning_rate">(0.001);
// The compiler resolves "learning_rate" → a unique compile-time constant index.
// No string comparison at runtime — just an array subscript.
```

Each unique name seen in the source produces a unique compile-time slot ID via template specialisation. First registration (at runtime init) writes the slot; subsequent calls are a direct array index.

## Usage

```cpp
#include "ct_string.hxx"

// As a template argument
template <CTString Name>
void print_name() {
    std::cout << Name.view() << "\n";
}
print_name<"hello">();

// Runtime hash (e.g. for string → index lookup)
uint64_t h = hash_name("some_key");
```

## Performance

- Zero runtime cost for name resolution once compiled — template instantiation happens at compile time.
- `hash_name()` is a simple loop over bytes; on modern hardware this is a handful of nanoseconds for short strings, and can be fully evaluated at compile time with `consteval`.
