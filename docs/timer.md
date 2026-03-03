# timer

**File:** [`timer/timer.hxx`](../timer/timer.hxx)
**Dependencies:** [ct_string](ct_string.md)

High-resolution wall-clock timer with per-name Welford statistics accumulation. Supports standalone use, a thread-safe named registry, and RAII scoped helpers.

## Key Types

### `Timer`

Simple single-threaded wall-clock timer.

```cpp
class Timer {
public:
    explicit Timer(bool start_now = false);
    void start();
    void stop();
    void reset();
    bool is_running() const;
    double elapsed_ms() const;    // wall time since start (even if running)
    double elapsed_us() const;
    double elapsed_ns() const;
    double last_lap_ns() const;   // duration of the most recent start→stop
};
```

### `TimerStats`

Welford online statistics for a stream of duration samples.

```cpp
struct TimerStats {
    std::size_t count;
    double total_ns;
    double mean_ns;
    double stddev_ns;
    double min_ns;
    double max_ns;

    void record(double ns);     // update with one sample
    void merge(const TimerStats& other);  // combine two accumulators
    void reset();
};
```

### `TimerRegistry`

Thread-safe registry of named timers with per-thread slot arrays (lock-free on the hot path).

```cpp
class TimerRegistry {
public:
    // Start a named timer; returns the thread-local Slot* for cheap stop
    template <CTString Name>
    Slot* start();

    // Stop by name (array lookup) or by slot pointer (direct deref — cheaper)
    template <CTString Name>
    void stop();
    static void stop(Slot* slot);

    // Query (no locking)
    template <CTString Name>
    bool is_running() const;

    template <CTString Name, typename Unit = std::chrono::milliseconds>
    double elapsed() const;

    template <CTString Name>
    TimerStats stats() const;

    // Reports (acquire lock, merge all threads)
    std::string get_report() const;
    std::string get_stats_report() const;
    std::string get_stats_report_per_thread() const;
    void print_report() const;
    void print_stats_report() const;

    // Reset (acquires lock)
    template <CTString Name>
    void reset();
    void reset_all();
};

TimerRegistry& global_timers();
#define TIMERS global_timers()
```

**Limit:** up to 128 named timers per registry (`MAX_CT_TIMERS`).

### `ScopedTimer<Unit>`

RAII timer that prints elapsed time on destruction (standalone, no registry).

```cpp
ScopedTimer<std::chrono::milliseconds> t("my_function");
// On destruction: prints "my_function: 42 ms"
```

### `make_scoped_timer<Name>(registry)`

Preferred RAII helper — ties into the registry for statistics accumulation.

```cpp
auto t = make_scoped_timer<"render">(TIMERS);
// On destruction: stops the "render" timer and records the sample
```

## Usage

```cpp
#include "timer.hxx"

// Standalone timer
Timer t(true);   // start immediately
do_work();
t.stop();
std::cout << t.elapsed_ms() << " ms\n";

// Registry — named timing with stats
TIMERS.start<"db_query">();
query_database();
TIMERS.stop<"db_query">();

// RAII (recommended for registry)
{
    auto scoped = make_scoped_timer<"render">(TIMERS);
    render_frame();
}  // automatically stopped and recorded

// Print statistics across all recorded samples
TIMERS.print_stats_report();
```

## Thread Safety

- **Hot path** (`start`/`stop`): lock-free after first use per name per thread. Each thread has its own slot array; no shared state is touched.
- **First use** per name per thread: acquires the registry mutex once to register the slot.
- **Reports** (`get_stats_report`, `print_*`): acquire the mutex, merge all thread-local data.
- **Reset**: acquires the mutex, clears all thread-local and merged data.

## Performance

See [stats_registry/BENCHMARKS.md](../stats_registry/BENCHMARKS.md) (timer suites 1–4) for measurements. Hot-path `start<n>` + `stop<n>` ≈ 45–50 ns; `make_scoped_timer` adds no measurable overhead over direct `start`/`stop`.
