# limits

**File:** [`limits/limits.hxx`](../limits/limits.hxx)
**Dependencies:** none

Cooperative time and memory limits for long-running processes. Two self-contained limiter classes — `TimeLimiter` and `MemoryLimiter` — each backed by a single background polling thread. A `global_limits` namespace exposes process-wide singleton instances and cheap flag reads.

## `TimeLimiter`

Cooperative time limiter. Arms a background thread that sets `expired()` to `true` after the requested duration.

**Sticky:** once `expired()` returns `true` it never reverts, even after `cancel()`.

```cpp
TimeLimiter lim;

lim.set(std::chrono::seconds{5});               // arm for 5 s, no callback
lim.set(std::chrono::seconds{5}, callback);     // arm with on-expiry callback

while (!lim.expired()) {
    do_work();
}

lim.cancel();   // disarms background thread (expired() state preserved)
```

The polling interval is 50 ms.

## `MemoryLimiter`

Cooperative RSS memory limiter. Arms a background thread that polls the process's physical memory footprint and sets `exceeded()` to `true` when the threshold is breached.

**Sticky:** once `exceeded()` returns `true` it never reverts, even after `cancel()`.

```cpp
MemoryLimiter mlim;

mlim.set(512);                       // arm at 512 MB, no callback
mlim.set(512, [] { cleanup(); });    // arm with on-breach callback

while (!mlim.exceeded()) {
    allocate_more();
}

mlim.cancel();
```

The polling interval is 100 ms. Uses `task_vm_info` (macOS) or `/proc/self/statm` (Linux) to measure RSS.

## Global limits

Convenience free functions operate on process-wide singleton `TimeLimiter` / `MemoryLimiter` instances. On expiry/breach their callbacks write `global_limits::time_flag` / `global_limits::memory_flag`. Poll those flags from any thread with `global_limits::time_reached()` / `global_limits::memory_reached()`.

```cpp
// ── time ──────────────────────────────────────────────────────────────
set_time_limit(30);                           // 30 s, no extra callback
set_time_limit(60, [] { log("time up"); });   // 60 s + extra callback

if (global_limits::time_reached()) break;     // cheap relaxed load, ~1 ns

cancel_time_limit();   // disarms; does NOT reset time_flag if already fired

// ── memory ────────────────────────────────────────────────────────────
set_memory_limit(2048);                            // 2 GB, no extra callback
set_memory_limit(512, [] { log("OOM risk"); });    // 512 MB + extra callback

if (global_limits::memory_reached()) break;        // cheap relaxed load

cancel_memory_limit();   // disarms; does NOT reset memory_flag if already fired
```

> **Note:** `cancel_time_limit()` / `cancel_memory_limit()` stop the background polling thread but do **not** reset the global flags. If a limit has already fired, the flags remain set after cancellation.

## `memlim` helpers

```cpp
// Current process RSS in bytes (0 on sampling failure — treat as "unknown")
std::size_t bytes = memlim::current_memory_bytes();

// Current process RSS in bytes cast to ptrdiff_t (-1 on failure)
std::ptrdiff_t usage = memlim::current_memory_usage();

// Conversion constant
constexpr std::size_t memlim::BYTES_PER_MB = 1024ULL * 1024ULL;
```

Both functions are `noexcept`.

## Example

```cpp
#include "limits.hxx"

// Process-wide time and memory limits
set_time_limit(120, [] {
    std::cerr << "Time limit reached — shutting down\n";
});
set_memory_limit(2048, [] {
    std::cerr << "Memory limit exceeded\n";
});

void search() {
    for (auto& node : frontier) {
        if (global_limits::time_reached())   return;   // ~1 ns check
        if (global_limits::memory_reached()) return;
        expand(node);
    }
}

// Per-phase time limit
TimeLimiter phase_lim;
phase_lim.set(std::chrono::seconds{10});

while (!phase_lim.expired()) {
    if (!refine()) break;
}
```

## Performance

- `global_limits::time_reached()` / `memory_reached()`: ~1 ns (relaxed atomic load)
- `TimeLimiter::expired()` / `MemoryLimiter::exceeded()`: ~1–2 ns (single relaxed load)
- Polling thread wakes every 50 ms (time) / 100 ms (memory) — negligible CPU impact
