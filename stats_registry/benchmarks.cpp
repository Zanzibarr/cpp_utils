/**
 * @file bench_stats_registry.cpp
 * @brief Benchmarks for TimerRegistry and StatsRegistry — CTString API.
 *
 * Build (C++20):
 *   g++ -std=c++20 -O2 -pthread bench_stats_registry.cpp -o bench_stats && ./bench_stats
 *
 * Goal: quantify the steady-state overhead of each instrumentation primitive
 * so you can make informed decisions about where to use which API.
 *
 * Structure
 * ─────────
 *  0  Raw baselines          — minimum cost of the underlying primitives
 *  1  Timer (Timer class)    — start/stop/elapsed on the standalone Timer
 *  2  TimerRegistry          — CTString start/stop, handle-based, scoped
 *  3  make_scoped_timer      — RAII overhead on top of TimerRegistry
 *  4  ScopedTimer            — standalone (no registry)
 *  5  Counters               — inc/dec/set/get, cached ref, scoped
 *  6  Gauges                 — record, reset
 *  7  Histograms             — record (in-range, overflow, underflow), reset
 *  8  Multi-threaded         — contention profiles for every primitive
 *  9  Report generation      — cost of get_*_report() calls
 * 10  Combined hot path      — realistic "annotated function body" scenario
 */

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <streambuf>
#include <thread>
#include <vector>

#include "../benchmarking/bench_main.hpp"
#include "stats_registry.hxx"

using namespace utilz;

using benchmark::DoNotOptimize;
using clk = std::chrono::steady_clock;

// Capacity pre-reserved for bench.series — must exceed the 1M-iteration push
// benchmark so the hot path never reallocates.
static constexpr std::size_t SERIES_BENCH_CAPACITY = 1'100'000;

// ─────────────────────────────────────────────────────────────────────────────
// Shared pre-warmed registry
//
// Every CTString name used in a benchmark must be touched here first so that
// the first-call registration overhead (mutex + name table write) never
// contaminates the measured hot path.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

StatsRegistry& reg() {
    static StatsRegistry instance;
    static bool ready = [] {
        // ── Timers ────────────────────────────────────────────────────────
        instance.start<"bench.timer">();
        instance.stop<"bench.timer">();
        {
            auto* s = instance.start<"bench.timer.handle">();
            TimerRegistry::stop(s);
        }
        instance.start<"bench.timer.mt">();
        instance.stop<"bench.timer.mt">();

        // ── Counters ──────────────────────────────────────────────────────
        instance.counter_inc<"bench.counter">(0);
        instance.counter_inc<"bench.counter.dec">(0);
        instance.counter_inc<"bench.counter.set">(0);
        instance.counter_inc<"bench.counter.get">(0);
        instance.counter_inc<"bench.counter.ref">(0);
        instance.counter_inc<"bench.counter.mt">(0);
        instance.counter_inc<"bench.counter.sc">(0);  // scoped counter

        // ── Gauges ────────────────────────────────────────────────────────
        instance.gauge_record<"bench.gauge">(0.0);
        instance.gauge_record<"bench.gauge.mt.shared">(0.0);
        instance.gauge_record<"bench.gauge.mt.t0">(0.0);
        instance.gauge_record<"bench.gauge.mt.t1">(0.0);
        instance.gauge_record<"bench.gauge.mt.t2">(0.0);
        instance.gauge_record<"bench.gauge.mt.t3">(0.0);

        // ── Histograms ────────────────────────────────────────────────────
        instance.histogram_create<"bench.hist">(0.0, 1000.0, 10);
        instance.histogram_create<"bench.hist.mt">(0.0, 1000.0, 10);

        // ── Series ────────────────────────────────────────────────────────
        // Pre-reserve enough capacity to cover the 1M-iteration push benchmark
        // (and the read benchmark) without any reallocation on the hot path.
        instance.series_create<"bench.series">(SERIES_BENCH_CAPACITY);
        instance.series_push<"bench.series">(0.0);
        instance.series_push<"bench.series.mt">(0.0);

        // ── Combined hot path ─────────────────────────────────────────────
        instance.counter_inc<"bench.combined.requests">(0);
        instance.counter_inc<"bench.combined.errors">(0);
        instance.gauge_record<"bench.combined.payload">(0.0);
        instance.histogram_create<"bench.combined.latency">(0.0, 500.0, 10);
        instance.start<"bench.combined.total">();
        instance.stop<"bench.combined.total">();
        instance.start<"bench.combined.db">();
        instance.stop<"bench.combined.db">();

        return true;
    }();
    (void)ready;
    return instance;
}

// ── Cached handles (set during warm-up, reused in benchmarks) ────────────────
std::atomic<int64_t>* g_counter_ref = nullptr;
TimerRegistry::Slot* g_slot_ref = nullptr;

// ── Manual baseline types ────────────────────────────────────────────────────

// Minimal Welford accumulator — no locks, no map, no name table.
struct ManualWelford {
    std::size_t count = 0;
    double total = 0.0;
    double mean = 0.0;
    double M2 = 0.0;
    double vmin = std::numeric_limits<double>::max();
    double vmax = std::numeric_limits<double>::lowest();

    void record(double v) noexcept {
        ++count;
        total += v;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        double d = v - mean;
        mean += d / static_cast<double>(count);
        M2 += d * (v - mean);
    }
};

ManualWelford g_manual_welford;
std::atomic<int64_t> g_manual_counter{0};

// ── /dev/null streambuf — swallows ScopedTimer output ────────────────────────
struct NullBuf : std::streambuf {
    int overflow(int c) override { return c; }
};
NullBuf g_null_buf;

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 0 — Raw baselines
// The minimum cost of the underlying system primitives.  Every other suite
// should be read relative to the numbers here.
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("0 · Baselines — raw primitives")

BENCH_CASE_N("steady_clock::now() — single call", 200'000) {
    for (auto _ : state) {
        auto t = clk::now();
        DoNotOptimize(t);
    }
}

BENCH_CASE_N("two clock calls + nanosecond subtract", 200'000) {
    for (auto _ : state) {
        auto t0 = clk::now();
        auto t1 = clk::now();
        double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        DoNotOptimize(ns);
    }
}

BENCH_CASE_N("two clock calls + Welford record (manual baseline)", 200'000) {
    for (auto _ : state) {
        auto t0 = clk::now();
        auto t1 = clk::now();
        double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        g_manual_welford.record(ns);
        DoNotOptimize(g_manual_welford.count);
    }
}

BENCH_CASE_N("std::atomic fetch_add relaxed (manual counter baseline)", 200'000) {
    for (auto _ : state) {
        g_manual_counter.fetch_add(1, std::memory_order_relaxed);
        DoNotOptimize(g_manual_counter.load(std::memory_order_relaxed));
    }
}

BENCH_CASE_N("uncontended std::mutex lock + unlock", 200'000) {
    static std::mutex mtx;
    for (auto _ : state) {
        std::lock_guard lock(mtx);
        DoNotOptimize(&lock);
    }
}

BENCH_CASE_N("std::mutex + Welford record (manual gauge baseline)", 200'000) {
    static std::mutex mtx;
    static ManualWelford acc;
    for (auto _ : state) {
        std::lock_guard lock(mtx);
        acc.record(42.0);
        DoNotOptimize(acc.count);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 1 — Timer (standalone class, no registry)
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("1 · Timer — standalone class")

BENCH_CASE_N("Timer::start + stop", 200'000) {
    Timer t;
    for (auto _ : state) {
        t.start();
        t.stop();
        DoNotOptimize(t.last_lap_ns());
    }
}

BENCH_CASE_N("Timer::start + stop + elapsed_ms", 200'000) {
    Timer t;
    for (auto _ : state) {
        t.start();
        t.stop();
        double e = t.elapsed_ms();
        DoNotOptimize(e);
    }
}

BENCH_CASE_N("Timer::elapsed_ms while running (live query)", 200'000) {
    Timer t(true);
    for (auto _ : state) {
        double e = t.elapsed_ms();
        DoNotOptimize(e);
    }
    t.stop();
}

BENCH_CASE_N("Timer::reset", 200'000) {
    Timer t;
    t.start();
    t.stop();
    for (auto _ : state) {
        t.reset();
        DoNotOptimize(t.is_running());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 2 — TimerRegistry — CTString API, single thread
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("2 · TimerRegistry — CTString, single-threaded")

// Hot path option 1: named start + named stop (two array lookups via CtSlotID)
BENCH_CASE_N("start<n> + stop<n> — array lookup on both sides", 200'000) {
    for (auto _ : state) {
        reg().start<"bench.timer">();
        reg().stop<"bench.timer">();
        DoNotOptimize(0);
    }
}

// Hot path option 2: named start returns Slot*, stop(Slot*) is a raw deref
BENCH_CASE_N("start<n> + stop(Slot*) — handle-based stop (no lookup on stop)", 200'000) {
    for (auto _ : state) {
        auto* slot = reg().start<"bench.timer.handle">();
        TimerRegistry::stop(slot);
        DoNotOptimize(slot);
    }
}

// Hot path option 3: both sides bypass CtSlotID entirely — direct Timer access
// This is the theoretical minimum for registry-backed timing.
BENCH_CASE_N("timer.start() + stop() via cached Slot* (absolute minimum)", 200'000) {
    // Obtain a stable Slot pointer once; reuse across all iterations.
    if (g_slot_ref == nullptr) g_slot_ref = reg().start<"bench.timer.handle">();

    for (auto _ : state) {
        g_slot_ref->timer.start();
        g_slot_ref->timer.stop();
        g_slot_ref->stats.record(g_slot_ref->timer.last_lap_ns());
        DoNotOptimize(g_slot_ref->stats.count);
    }
}

// Query the accumulated stats without stopping (array lookup + copy)
BENCH_CASE_N("stats<n> — copy accumulated TimerStats", 200'000) {
    for (auto _ : state) {
        auto s = reg().stats<"bench.timer">();
        DoNotOptimize(s.count);
    }
}

// elapsed<n> while timer is stopped (array lookup + ns_to conversion)
BENCH_CASE_N("elapsed<n> — stopped timer", 200'000) {
    for (auto _ : state) {
        double e = reg().elapsed<"bench.timer", std::chrono::microseconds>();
        DoNotOptimize(e);
    }
}

// is_running<n> — cheapest named query
BENCH_CASE_N("is_running<n> — single array lookup + bool load", 200'000) {
    for (auto _ : state) {
        bool r = reg().is_running<"bench.timer">();
        DoNotOptimize(r);
    }
}

// reset<n> — acquires the registry mutex, flushes all thread data
BENCH_CASE_N("reset<n> — slow path (global mutex, all threads flushed)", 10'000) {
    for (auto _ : state) {
        reg().reset<"bench.timer">();
        DoNotOptimize(0);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 3 — make_scoped_timer (RAII)
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("3 · make_scoped_timer — RAII")

// Construction = start<n>(); destruction = stop(Slot*)
// Net overhead vs raw Timer::start+stop = one CtSlotID array lookup on entry.
BENCH_CASE_N("make_scoped_timer<n> — construct + destruct", 200'000) {
    for (auto _ : state) {
        auto t = make_scoped_timer<"bench.timer">(reg());
        DoNotOptimize(t);
    }
}

// Measure the impact of a non-trivial body inside the scoped region
BENCH_CASE_N("make_scoped_timer<n> — with volatile body (realistic overhead)", 200'000) {
    volatile int sink = 0;
    for (auto _ : state) {
        auto t = make_scoped_timer<"bench.timer">(reg());
        sink += 1;
        DoNotOptimize(sink);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 4 — ScopedTimer standalone (no registry)
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("4 · ScopedTimer — standalone (no registry)")

// Destructor prints to stdout — suppress output to avoid I/O skew.
BENCH_CASE_N("ScopedTimer<ms> — construct + destruct (output suppressed)", 100'000) {
    auto* old = std::cout.rdbuf(&g_null_buf);
    for (auto _ : state) {
        ScopedTimer<std::chrono::milliseconds> t("bench_standalone");
        DoNotOptimize(t);
    }
    std::cout.rdbuf(old);
}

BENCH_CASE_N("ScopedTimer<ns> — construct + destruct (output suppressed)", 100'000) {
    auto* old = std::cout.rdbuf(&g_null_buf);
    for (auto _ : state) {
        ScopedTimer<std::chrono::nanoseconds> t("bench_standalone_ns");
        DoNotOptimize(t);
    }
    std::cout.rdbuf(old);
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 5 — Counters, single-threaded
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("5 · Counters — single-threaded")

// counter_inc<n>: one CtStatID array lookup + atomic fetch_add relaxed
BENCH_CASE_N("counter_inc<n> — array lookup + fetch_add", 200'000) {
    for (auto _ : state) {
        reg().counter_inc<"bench.counter">();
        DoNotOptimize(0);
    }
}

// counter_inc<n> with explicit delta — same path, different argument
BENCH_CASE_N("counter_inc<n>(delta) — with explicit delta", 200'000) {
    for (auto _ : state) {
        reg().counter_inc<"bench.counter">(7);
        DoNotOptimize(0);
    }
}

// counter_dec<n>: identical cost to inc — here for symmetry
BENCH_CASE_N("counter_dec<n> — array lookup + fetch_sub", 200'000) {
    for (auto _ : state) {
        reg().counter_dec<"bench.counter.dec">();
        DoNotOptimize(0);
    }
}

// counter_set<n>: atomic store — slightly cheaper than fetch_add (no RMW)
BENCH_CASE_N("counter_set<n> — array lookup + atomic store", 200'000) {
    for (auto _ : state) {
        reg().counter_set<"bench.counter.set">(42);
        DoNotOptimize(0);
    }
}

// counter_get<n>: array lookup + atomic load — read-only path
BENCH_CASE_N("counter_get<n> — array lookup + atomic load", 200'000) {
    for (auto _ : state) {
        auto v = reg().counter_get<"bench.counter.get">();
        DoNotOptimize(v);
    }
}

// counter_ref<n>: lookup once, then raw fetch_add — absolute hot-path minimum
// Measures: one CtStatID lookup + pointer return (warm-up only).
// Inside the loop: just the raw atomic fetch_add, no lookup at all.
BENCH_CASE_N("counter_ref<n> raw fetch_add — no lookup in loop (cached pointer)", 200'000) {
    if (g_counter_ref == nullptr) g_counter_ref = reg().counter_ref<"bench.counter.ref">();

    for (auto _ : state) {
        g_counter_ref->fetch_add(1, std::memory_order_relaxed);
        DoNotOptimize(g_counter_ref->load(std::memory_order_relaxed));
    }
}

// counter_reset<n>: atomic store to 0 — same cost as counter_set
BENCH_CASE_N("counter_reset<n> — atomic store to 0", 200'000) {
    for (auto _ : state) {
        reg().counter_reset<"bench.counter">();
        DoNotOptimize(0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// make_scoped_counter
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("5b · make_scoped_counter — RAII")

// Construction = counter_ref<n> lookup + fetch_add 1
// Destruction  = raw fetch_sub 1 via cached pointer
BENCH_CASE_N("make_scoped_counter<n> — construct + destruct", 200'000) {
    for (auto _ : state) {
        auto sc = make_scoped_counter<"bench.counter.sc">(reg());
        DoNotOptimize(sc);
    }
}

// Nested scoped counters — two inc on entry, two dec on exit
BENCH_CASE_N("nested make_scoped_counter<n> x2 — two inc + two dec", 200'000) {
    for (auto _ : state) {
        auto outer = make_scoped_counter<"bench.counter.sc">(reg());
        auto inner = make_scoped_counter<"bench.counter.sc">(reg());
        DoNotOptimize(inner);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 6 — Gauges, single-threaded
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("6 · Gauges — single-threaded")

// gauge_record<n>: CtStatID lookup + per-entry mutex lock + Welford update
BENCH_CASE_N("gauge_record<n> — lookup + mutex + Welford", 200'000) {
    double val = 0.0;
    for (auto _ : state) {
        reg().gauge_record<"bench.gauge">(val);
        val = std::fmod(val + 1.1, 1000.0);
        DoNotOptimize(val);
    }
}

// gauge_record<n> with a constant value — removes branch on min/max
BENCH_CASE_N("gauge_record<n> — constant value (best case Welford)", 200'000) {
    for (auto _ : state) {
        reg().gauge_record<"bench.gauge">(42.0);
        DoNotOptimize(0);
    }
}

// gauge_reset<n>: per-entry mutex + zero stats — slow path
BENCH_CASE_N("gauge_reset<n> — per-entry mutex + zero stats", 10'000) {
    for (auto _ : state) {
        reg().gauge_reset<"bench.gauge">();
        DoNotOptimize(0);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 7 — Histograms, single-threaded
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("7 · Histograms — single-threaded")

// In-range: lookup + mutex + index computation + bucket increment
BENCH_CASE_N("histogram_record<n> — in-range value", 200'000) {
    double val = 0.0;
    for (auto _ : state) {
        reg().histogram_record<"bench.hist">(val);
        val = std::fmod(val + 7.3, 999.0);
        DoNotOptimize(val);
    }
}

// Underflow: lookup + mutex + early increment (no index computation)
BENCH_CASE_N("histogram_record<n> — underflow (val < low)", 200'000) {
    for (auto _ : state) {
        reg().histogram_record<"bench.hist">(-1.0);
        DoNotOptimize(0);
    }
}

// Overflow: lookup + mutex + early increment (no index computation)
BENCH_CASE_N("histogram_record<n> — overflow (val >= high)", 200'000) {
    for (auto _ : state) {
        reg().histogram_record<"bench.hist">(1e9);
        DoNotOptimize(0);
    }
}

// Boundary value exactly at low (== first bucket, not underflow)
BENCH_CASE_N("histogram_record<n> — boundary value at low (first bucket)", 200'000) {
    for (auto _ : state) {
        reg().histogram_record<"bench.hist">(0.0);
        DoNotOptimize(0);
    }
}

// histogram_reset<n>: per-entry mutex + fill buckets to 0
BENCH_CASE_N("histogram_reset<n> — per-entry mutex + zero all buckets", 10'000) {
    for (auto _ : state) {
        reg().histogram_reset<"bench.hist">();
        DoNotOptimize(0);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 8 — Multi-threaded contention
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("8a · Counters — multi-threaded contention")

// 4 threads, same CTString key — atomic contention only (no lock)
BENCH_CASE_NW("counter_inc<n> — 4 threads, same key, 500 iters each", 500, 3) {
    for (auto _ : state) {
        constexpr int N = 4, ITERS = 500;
        std::vector<std::thread> workers;
        workers.reserve(N);
        for (int t = 0; t < N; ++t)
            workers.emplace_back([] {
                for (int i = 0; i < ITERS; ++i) reg().counter_inc<"bench.counter.mt">();
            });
        for (auto& w : workers) w.join();
        DoNotOptimize(0);
    }
}

// Same key, but via cached counter_ref* — no CtStatID lookup inside threads
BENCH_CASE_NW("counter_ref* fetch_add — 4 threads, cached pointer", 500, 3) {
    auto* ptr = reg().counter_ref<"bench.counter.mt">();
    for (auto _ : state) {
        constexpr int N = 4, ITERS = 500;
        std::vector<std::thread> workers;
        workers.reserve(N);
        for (int t = 0; t < N; ++t)
            workers.emplace_back([ptr] {
                for (int i = 0; i < ITERS; ++i) ptr->fetch_add(1, std::memory_order_relaxed);
            });
        for (auto& w : workers) w.join();
        DoNotOptimize(0);
    }
}

// make_scoped_counter RAII overhead under 4-thread concurrency
BENCH_CASE_NW("make_scoped_counter<n> — 4 threads, inc+dec per iter", 500, 3) {
    for (auto _ : state) {
        constexpr int N = 4, ITERS = 500;
        std::vector<std::thread> workers;
        workers.reserve(N);
        for (int t = 0; t < N; ++t)
            workers.emplace_back([] {
                for (int i = 0; i < ITERS; ++i) {
                    auto sc = make_scoped_counter<"bench.counter.sc">(reg());
                    DoNotOptimize(sc);
                }
            });
        for (auto& w : workers) w.join();
        DoNotOptimize(0);
    }
}

BENCH_SUITE("8b · Gauges — multi-threaded contention")

// Shared key: per-entry mutex serialises all 4 threads
BENCH_CASE_NW("gauge_record<n> — 4 threads, same key (serialised by per-entry mutex)", 500, 3) {
    for (auto _ : state) {
        constexpr int N = 4, ITERS = 500;
        std::vector<std::thread> workers;
        workers.reserve(N);
        for (int t = 0; t < N; ++t)
            workers.emplace_back([t] {
                for (int i = 0; i < ITERS; ++i) reg().gauge_record<"bench.gauge.mt.shared">(static_cast<double>(t * ITERS + i));
            });
        for (auto& w : workers) w.join();
        DoNotOptimize(0);
    }
}

// Distinct keys: each thread hits a different per-entry mutex — true parallelism
BENCH_CASE_NW("gauge_record<n> — 4 threads, distinct keys (parallel, no contention)", 500, 3) {
    for (auto _ : state) {
        constexpr int ITERS = 500;
        std::vector<std::thread> workers;
        workers.reserve(4);
        workers.emplace_back([] {
            for (int i = 0; i < ITERS; ++i) reg().gauge_record<"bench.gauge.mt.t0">(static_cast<double>(i));
        });
        workers.emplace_back([] {
            for (int i = 0; i < ITERS; ++i) reg().gauge_record<"bench.gauge.mt.t1">(static_cast<double>(i));
        });
        workers.emplace_back([] {
            for (int i = 0; i < ITERS; ++i) reg().gauge_record<"bench.gauge.mt.t2">(static_cast<double>(i));
        });
        workers.emplace_back([] {
            for (int i = 0; i < ITERS; ++i) reg().gauge_record<"bench.gauge.mt.t3">(static_cast<double>(i));
        });
        for (auto& w : workers) w.join();
        DoNotOptimize(0);
    }
}

BENCH_SUITE("8c · Histograms — multi-threaded contention")

// Shared key: per-entry mutex serialises all 4 threads
BENCH_CASE_NW("histogram_record<n> — 4 threads, same key (serialised)", 500, 3) {
    for (auto _ : state) {
        constexpr int N = 4, ITERS = 500;
        std::vector<std::thread> workers;
        workers.reserve(N);
        for (int t = 0; t < N; ++t)
            workers.emplace_back([] {
                for (int i = 0; i < ITERS; ++i) reg().histogram_record<"bench.hist.mt">(static_cast<double>(i % 1000));
            });
        for (auto& w : workers) w.join();
        DoNotOptimize(0);
    }
}

BENCH_SUITE("8d · Timers — multi-threaded")

// Each thread has its own thread_local Slot — no contention on the hot path.
// The only shared state is the registry mutex on the very first call per thread.
BENCH_CASE_NW("start<n>+stop<n> — 4 threads, same key (thread-local, no hot-path contention)", 500, 3) {
    for (auto _ : state) {
        constexpr int N = 4, ITERS = 500;
        std::vector<std::thread> workers;
        workers.reserve(N);
        for (int t = 0; t < N; ++t)
            workers.emplace_back([] {
                for (int i = 0; i < ITERS; ++i) {
                    reg().start<"bench.timer.mt">();
                    reg().stop<"bench.timer.mt">();
                }
            });
        for (auto& w : workers) w.join();
        DoNotOptimize(0);
    }
}

// make_scoped_timer in a tight multi-threaded loop
BENCH_CASE_NW("make_scoped_timer<n> — 4 threads (thread-local Slot, cached pointer stop)", 500, 3) {
    for (auto _ : state) {
        constexpr int N = 4, ITERS = 500;
        std::vector<std::thread> workers;
        workers.reserve(N);
        for (int t = 0; t < N; ++t)
            workers.emplace_back([] {
                for (int i = 0; i < ITERS; ++i) {
                    auto sc = make_scoped_timer<"bench.timer.mt">(reg());
                    DoNotOptimize(sc);
                }
            });
        for (auto& w : workers) w.join();
        DoNotOptimize(0);
    }
}

// Cold path: first call per new thread triggers mutex + name table write
BENCH_CASE_NW("start<n>+stop<n> — cold path (new thread per iteration)", 200, 2) {
    for (auto _ : state) {
        std::thread t([] {
            reg().start<"bench.timer.mt">();
            reg().stop<"bench.timer.mt">();
        });
        t.join();
        DoNotOptimize(0);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 9 — Report generation (slow paths)
//
// These are never on the hot path, but knowing their cost tells you how
// often you can safely call them in a monitoring loop.
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("9 · Report generation — slow paths")

BENCH_CASE_N("get_counter_report() — iterate all registered counters", 5'000) {
    for (auto _ : state) {
        auto r = reg().get_counter_report();
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("get_gauge_report() — iterate all registered gauges", 5'000) {
    for (auto _ : state) {
        auto r = reg().get_gauge_report();
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("get_histogram_report() — iterate all registered histograms", 5'000) {
    for (auto _ : state) {
        auto r = reg().get_histogram_report();
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("get_stats_report() — merge all timer threads (Welford combine)", 5'000) {
    for (auto _ : state) {
        auto r = reg().get_stats_report();
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("get_thread_report() — iterate graveyard + live threads", 5'000) {
    for (auto _ : state) {
        auto r = reg().get_thread_report();
        DoNotOptimize(r.size());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 11 — Series
//
// Measures the cost of series_push (lock-free hot path after first call) and
// series_get (merge + sort across all threads on read).
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("11 · Series — push throughput and read latency")

// Single-threaded push: measures the lock-free append path.
BENCH_CASE_N("single-thread series_push — hot path after first call", 1'000'000) {
    for (auto _ : state) {
        reg().series_push<"bench.series">(1.0);
    }
}

// Multi-threaded push: 4 threads all pushing to the same named series.
BENCH_CASE_NW("4 threads series_push — same name concurrent", 200, 3) {
    for (auto _ : state) {
        constexpr int N_THREADS = 4;
        constexpr int PUSHES = 200;
        std::vector<std::thread> workers;
        workers.reserve(N_THREADS);
        for (int t = 0; t < N_THREADS; ++t) {
            workers.emplace_back([] {
                for (int i = 0; i < PUSHES; ++i) {
                    reg().series_push<"bench.series.mt">(static_cast<double>(i));
                }
            });
        }
        for (auto& w : workers) w.join();
        DoNotOptimize(0);
    }
}

// Read (merge+sort) latency for a series already populated with 1 000 points.
BENCH_CASE_N("series_get — merge+sort 1 000 points", 1'000) {
    // Pre-populate on first iteration only (idempotent across runs).
    {
        static bool done = false;
        if (!done) {
            for (int i = 0; i < 1'000; ++i) {
                reg().series_push<"bench.series.read.1k">(static_cast<double>(i));
            }
            done = true;
        }
    }
    for (auto _ : state) {
        auto pts = reg().series_get<"bench.series.read.1k">();
        DoNotOptimize(pts.size());
    }
}