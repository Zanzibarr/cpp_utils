# scope_guard

> All symbols below live in `namespace utilz`; examples assume `using namespace utilz;`.

**File:** [`scope_guard/scope_guard.hxx`](../scope_guard/scope_guard.hxx)
**Dependencies:** none
**Benchmarks:** [scope_guard/BENCHMARKS.md](../scope_guard/BENCHMARKS.md)

RAII scope-guard that executes a callable on scope exit. Supports three strategies: always run, run only on exception, or run only on normal exit. Uses `std::uncaught_exceptions()` for correct behaviour with nested exceptions.

## `ScopeStrategy`

```cpp
enum class ScopeStrategy {
    EXIT,     // always execute (default)
    FAIL,     // execute only if scope exits via exception
    SUCCESS,  // execute only if scope exits normally
};
```

## `ScopeGuard<F>`

```cpp
template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F func, ScopeStrategy strategy = ScopeStrategy::EXIT);
    void dismiss();   // cancel — callable will not run on destruction
    // copy and move are both deleted
};
```

If the callable itself throws, the program terminates with a message (prevents double-throw / exception cascade).

## Factory Functions

```cpp
auto g = on_scope_exit(f);     // ScopeGuard with EXIT
auto g = on_scope_success(f);  // ScopeGuard with SUCCESS
auto g = on_scope_fail(f);     // ScopeGuard with FAIL
```

The factory functions use CTAD, so `F` is deduced automatically from the lambda.

## Usage

```cpp
#include "scope_guard.hxx"

// Always run — classic cleanup guard
FILE* f = fopen("data.bin", "rb");
auto close_f = on_scope_exit([&] { fclose(f); });

// Run only on failure — rollback uncommitted state
db.begin_transaction();
auto rollback = on_scope_fail([&] { db.rollback(); });
db.write(data);
db.commit();
rollback.dismiss();   // commit succeeded — cancel the rollback

// Run only on success — post-commit logging
auto log_done = on_scope_success([&] {
    LOG_SUCCESS << "transaction committed";
});

// Dismiss to skip execution
auto guard = on_scope_exit(cleanup);
if (transfer_ownership) {
    guard.dismiss();
}
```

## Strategies at a Glance

| Strategy | Runs when... |
|----------|-------------|
| `EXIT` | Always — scope left normally or via exception |
| `SUCCESS` | Scope left normally (no active exception) |
| `FAIL` | Scope left via exception |

## Exception Detection

The guard captures `std::uncaught_exceptions()` at construction time. On destruction it compares to the current count:

- `FAIL`: runs if count increased (an exception is propagating)
- `SUCCESS`: runs if count did not increase
- `EXIT`: always runs

This correctly handles nested exceptions and exceptions thrown inside other destructors.

## Performance

`ScopeGuard` stores the callable inline with no heap allocation (for typical lambdas). Construction reads `std::uncaught_exceptions()` once; destruction reads it again and calls the callable. Total overhead is a handful of nanoseconds. `dismiss()` makes destruction a no-op.
