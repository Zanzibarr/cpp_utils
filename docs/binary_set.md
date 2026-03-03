# binary_set

**File:** [`binary_set/binary_set.hxx`](../binary_set/binary_set.hxx)
**Dependencies:** none
**Benchmarks:** [binary_set/BENCHMARKS.md](../binary_set/BENCHMARKS.md)

Compact bitset-backed set for small non-negative integers. Stores membership in a flat `vector<uint64_t>`, giving O(1) element operations and O(capacity/64) set algebra via word-at-a-time bitwise operations — dramatically faster than `std::set` for most operations, and competitive with `vector<bool>` while providing a proper set API.

## Construction

```cpp
BinarySet bs(capacity);           // empty set, capacity elements
BinarySet bs(capacity, true);     // full set (all elements present)
BinarySet copy = other;           // copy
```

Elements must be non-negative integers in `[0, capacity)`.

## Element Operations — O(1)

```cpp
bool added   = bs.add(elem);      // true if elem was newly added
bool removed = bs.remove(elem);   // true if elem was present
bool present = bs.contains(elem);
bool r       = bs[elem];          // subscript (read-only)

std::size_t n = bs.size();        // number of elements (maintained counter)
bool empty    = bs.empty();

bs.clear();   // remove all
bs.fill();    // add all in [0, capacity)
```

## Set Membership Tests — O(capacity/64)

```cpp
bool sub  = a.subset_of(b);    // a ⊆ b
bool sup  = a.superset_of(b);  // a ⊇ b
bool over = a.intersects(b);   // a ∩ b ≠ ∅ (short-circuits on first overlap)
```

## Set Algebra

All operators return a new `BinarySet` of the same capacity and operate word-at-a-time.

```cpp
BinarySet r = a & b;    // intersection ∩
BinarySet r = a | b;    // union ∪
BinarySet r = a - b;    // difference \ (elements in a but not b)
BinarySet r = a ^ b;    // symmetric difference △
BinarySet r = !a;       // complement ∁ (all elements not in a)
```

In-place variants (avoid allocation):

```cpp
a &= b;   a |= b;   a -= b;   a ^= b;
```

## Iteration

```cpp
for (unsigned int elem : bs) {
    // iterates in ascending order using std::countr_zero
}

// Materialise to vector
std::vector<unsigned int> elements = bs.sparse();
```

## Output

```cpp
std::string vis = std::string(bs);   // "[X-X--X...]" visualization
std::cout << bs;                     // stream operator
```

## Thread Safety

No internal synchronisation. Concurrent reads are safe; mutations require external locking.

## When to Use

| Scenario | Best choice |
|----------|------------|
| Small integer universe (≤ a few thousand), need fast set algebra | `BinarySet` |
| Need ordered iteration, arbitrary key type | `std::set` |
| Need fast point lookup, no set algebra | `std::unordered_set` |
| Membership only, no algebra, no size tracking | `std::vector<bool>` |
| Need concurrent writes | `std::unordered_set` + mutex |

`BinarySet` wins decisively on set algebra (intersection, union, etc.) and is competitive on individual lookups. The main cost is fixed memory proportional to capacity, not to number of elements.

## Performance

See [BENCHMARKS.md](../binary_set/BENCHMARKS.md) for detailed comparisons against `std::set`, `std::unordered_set`, and `vector<bool>`. Reference numbers:

- `add` / `remove` / `contains`: ~14–29 ns (includes copy overhead in benchmark)
- Set algebra (intersection, cap=64 sparse): ~31 ns vs `std::set_intersection` ~280 ns
- Copy (cap=1024 sparse): ~13 ns vs `std::set` copy ~2.93 µs (225× faster)
