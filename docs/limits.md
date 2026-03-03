# limits

**File:** [`limits/limits.hxx`](../limits/limits.hxx)
**Dependencies:** none

Cooperative time and memory limits for long-running processes. Provides per-instance and process-wide time limiters (with background polling threads), a cheap global termination check macro, and a POSIX memory limit helper.

## Time Limiting — `timelim` namespace

### `GlobalTimeLimiter`

Process-wide singleton. Sets a global atomic flag when expired; any thread can poll it cheaply.

```cpp
// Set a global limit of N seconds
timelim::set_time_limit(30);                          // 30 s, no callback
timelim::set_time_limit(60, [] { cleanup(); });       // 60 s + callback on expiry

// Cancel before it fires
timelim::cancel_time_limit();

// Check from anywhere — relaxed atomic load, ~1 ns
if (LIMITS_CHECK_STOP()) break;

// Or use the static method
if (timelim::GlobalTimeLimiter::expired()) break;
```

`LIMITS_CHECK_STOP()` is the recommended hot-path check — it reads a global `std::atomic<bool>` with `memory_order_relaxed`.

### `LocalTimeLimiter`

Per-instance limit, scoped to one subsystem. Can run concurrently with other local limiters and the global limiter. `expired()` returns true if this instance OR the global limit has fired.

```cpp
timelim::LocalTimeLimiter lim;

lim.set(std::chrono::seconds(5));               // fire after 5 s
lim.set(std::chrono::seconds(5), callback);     // with callback

while (!lim.expired()) {
    do_work();
}

lim.cancel();   // stop polling thread
```

The polling interval is 50 ms — suitable for cooperative termination, not for sub-100 ms precision.

## Memory Limiting — `memlim` namespace

```cpp
// Set a soft virtual memory limit (RLIMIT_AS)
bool ok = memlim::set_memory_limit(512);   // 512 MB
// Returns false and prints a warning on macOS (kernel ignores RLIMIT_AS)
// Returns true on Linux/Unix

// Read current soft limit in bytes (-1 if unavailable)
int64_t bytes = memlim::current_memory_usage();
```

**Platform note:** `setrlimit(RLIMIT_AS)` is respected on Linux. On macOS the kernel ignores it; `set_memory_limit` returns `false` and prints a warning.

## Example

```cpp
#include "limits.hxx"

// Whole-process wall-clock limit
timelim::set_time_limit(120, [] {
    std::cerr << "Time limit reached — shutting down\n";
});

// Memory cap (Linux only)
memlim::set_memory_limit(2048);   // 2 GB

void search() {
    for (auto& node : frontier) {
        if (LIMITS_CHECK_STOP()) return;   // ~1 ns check
        expand(node);
    }
}

// Per-phase limit
timelim::LocalTimeLimiter phase_lim;
phase_lim.set(std::chrono::seconds(10));

while (!phase_lim.expired()) {
    if (!refine()) break;
}
```

## Performance

- `LIMITS_CHECK_STOP()`: ~1 ns (relaxed atomic load)
- `LocalTimeLimiter::expired()`: ~2–3 ns (two relaxed loads)
- Polling thread wakes every 50 ms — negligible CPU impact
