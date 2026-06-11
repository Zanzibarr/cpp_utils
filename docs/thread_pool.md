# thread_pool

> All symbols below live in `namespace utilz`; examples assume `using namespace utilz;`.

**File:** [`thread_pool/thread_pool.hxx`](../thread_pool/thread_pool.hxx)
**Dependencies:** — (standard library only; requires C++20)

Fixed-size thread pool with per-thread work-stealing queues, priority scheduling, cooperative cancellation via `std::stop_token`, and barrier-style synchronisation.

## Key Types

### `Priority`

Scheduling priority for submitted tasks. Higher-priority tasks run before lower-priority ones within the same queue. Work-stealing always takes the back of the victim's deque (their lowest-priority work).

```cpp
enum class Priority { low = 0, normal = 1, high = 2 };
```

### `ShutdownPolicy`

Controls the fate of queued (not yet started) tasks when the pool shuts down.

```cpp
enum class ShutdownPolicy { drain, cancel };
```

- **`drain`** (default) — queued tasks run to completion before threads are joined.
- **`cancel`** — queued tasks are dropped; their futures receive a `std::runtime_error`.

### `Future<T>`

Copyable future returned by all `submit` variants. Multiple copies share the same result via an internal `shared_ptr`.

```cpp
template <typename T>
class Future {
public:
    T    get() const;  // block until done; rethrows task exceptions
    void cancel();     // request cooperative cancellation via stop_token
};
```

### `ThreadPool`

Non-copyable, non-movable pool. Destructor calls `shutdown()`.

```cpp
class ThreadPool {
public:
    explicit ThreadPool(std::size_t n = std::thread::hardware_concurrency());

    // ── Single submit ─────────────────────────────────────────────────────
    // Defaults: Priority::normal, ShutdownPolicy::drain
    // Callable may accept std::stop_token as its first parameter.

    template <typename F, typename... Args>
    Future<R> submit(F&& func, Args&&... args);

    template <typename F, typename... Args>
    Future<R> submit(ShutdownPolicy policy, F&& func, Args&&... args);

    template <typename F, typename... Args>
    Future<R> submit(Priority priority, F&& func, Args&&... args);

    template <typename F, typename... Args>
    Future<R> submit(Priority priority, ShutdownPolicy policy, F&& func, Args&&... args);

    // ── Bulk submit ───────────────────────────────────────────────────────
    // submit_each — one task per element in [first, last)
    template <typename Iter, typename F>
    std::vector<Future<R>> submit_each(Iter first, Iter last, F func);
    // overloads: (Priority, ...), (ShutdownPolicy, ...), (Priority, ShutdownPolicy, ...)

    // submit_n — n tasks; callable receives std::size_t index 0..n-1
    template <typename F>
    std::vector<Future<R>> submit_n(std::size_t n, F func);
    // overloads: (Priority, ...), (ShutdownPolicy, ...), (Priority, ShutdownPolicy, ...)

    // ── Synchronisation ───────────────────────────────────────────────────
    void wait_all();                                         // block until all tasks finish
    bool wait_for(std::chrono::duration timeout);            // true = finished, false = timed out
    bool wait_until(std::chrono::time_point deadline);       // true = finished, false = timed out

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void shutdown();   // drain/cancel queued tasks, join threads; idempotent

    // ── Accessors ─────────────────────────────────────────────────────────
    std::size_t thread_count()   const noexcept;
    std::size_t pending_count()  const noexcept;  // queued + running
};
```

`submit` throws `std::runtime_error` if called after `shutdown()`.

## Usage

```cpp
#include "thread_pool.hxx"

ThreadPool pool{4};

// Basic submit — get result via Future
auto f = pool.submit([] { return 42; });
int result = f.get();

// With arguments
auto g = pool.submit([](int a, int b) { return a + b; }, 3, 7);

// Priority — high-priority tasks jump ahead of queued low-priority ones
pool.submit(Priority::high, [] { /* urgent work */ });

// Cooperative cancellation — callable takes stop_token as first arg
auto fut = pool.submit([](std::stop_token tok) {
    while (!tok.stop_requested()) { /* work */ }
});
fut.cancel();   // signals the token
fut.get();      // wait for the task to acknowledge and exit

// Fire-and-forget batch with barrier
for (int i = 0; i < 100; ++i) {
    pool.submit([i] { process(i); });
}
pool.wait_all();

// Bulk submit — one future per element
std::vector<int> data{1, 2, 3, 4};
auto futs = pool.submit_each(data.begin(), data.end(), [](int x) { return x * x; });

// Index-based bulk submit — n tasks receiving 0..n-1
auto results = pool.submit_n(8, [](std::size_t i) { return heavy(i); });

// Timed wait
if (!pool.wait_for(std::chrono::seconds(5))) {
    // timed out — pool still running
}

// Cancel-on-shutdown: task is dropped when pool destructs (future gets runtime_error)
pool.submit(ShutdownPolicy::cancel, [] { /* optional work */ });
```

## Work Stealing

Each worker owns a `deque`. Workers pop from the **front** of their own queue (highest-priority task first). When idle, a worker tries to steal from the **back** of a peer's queue (lowest-priority task of the busiest queue). Stealing uses `std::try_to_lock` to skip busy peers and avoid convoy effects.

## Thread Safety

All public methods are thread-safe. `submit` and the bulk variants acquire a global lock to push tasks; the per-queue lock is held only while inserting. `wait_all` / `wait_for` / `wait_until` are safe to call concurrently from multiple threads.

## Build

```bash
g++ -std=c++20 -O2 -pthread thread_pool.hxx your_file.cpp -o output
```

Requires C++20: `std::jthread`, `std::stop_token`, `std::stop_source`, `std::condition_variable_any`.
