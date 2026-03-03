# interval

**File:** [`interval/interval.hxx`](../interval/interval.hxx)
**Dependencies:** none

Closed interval `[min, max]` for arithmetic types. Provides containment queries, overlap detection, clamping, merging, intersection, translation, and (for floating-point) normalization — all as simple value types with no heap allocation.

## Template

```cpp
template <typename T>   // T must be arithmetic (int, double, float, ...)
class Interval;
```

## Construction

```cpp
Interval<double> iv(0.0, 1.0);     // [0.0, 1.0] — throws if min > max
Interval<int>    iv2(1, 10);       // [1, 10]

auto empty = Interval<double>::make_empty();     // sentinel: min > max
auto uni   = Interval<double>::make_universe();  // [lowest, max]
```

## Accessors

```cpp
T   lo  = iv.min();
T   hi  = iv.max();
T   len = iv.length();    // max - min
T   ctr = iv.center();    // (min + max) / 2
bool e  = iv.is_empty();  // min > max
```

## Queries

```cpp
bool b = iv.contains(value);        // min <= value <= max
bool b = iv.contains(other);        // other ⊆ iv (interval containment)
bool b = iv.overlaps(other);        // non-empty intersection?
```

## Operations

```cpp
T clamped = iv.clamp(value);        // std::clamp(value, min, max)

Interval<T> merged = iv.merge(other);   // smallest interval containing both

std::optional<Interval<T>> sect = iv.intersect(other);
// nullopt if disjoint, otherwise the overlapping sub-interval

iv.expand(amount);        // grow by amount on each side (with overflow checks)
iv.translate(offset);     // shift by offset (with overflow checks)
```

## Normalization (floating-point only)

```cpp
// Map [min, max] → [0, 1]
double norm = iv.normalize(value);

// Map [0, 1] → [min, max]
double val  = iv.denormalize(norm_value);
```

## Operators

```cpp
bool eq  = iv1 == iv2;
bool neq = iv1 != iv2;
std::cout << iv;   // "[ 0.5, 1.5 ]"
```

## Example

```cpp
#include "interval.hxx"

Interval<double> valid_lr(1e-6, 1.0);
double lr = 0.05;

if (!valid_lr.contains(lr)) {
    lr = valid_lr.clamp(lr);
}

// Normalize for display
double normalized = valid_lr.normalize(lr);   // [0, 1]

// Check if two parameter ranges overlap
Interval<int> range_a(0, 50);
Interval<int> range_b(40, 100);
if (range_a.overlaps(range_b)) {
    auto overlap = range_a.intersect(range_b);  // Some([40, 50])
}
```

## Performance

All operations are arithmetic on two values with no heap allocation. Costs are in the single-digit nanosecond range. `intersect` returns `std::optional` but the compiler typically elides the copy.
