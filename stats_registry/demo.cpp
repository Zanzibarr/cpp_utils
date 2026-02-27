/**
 * demo.cpp
 * ───────────────────────
 * A comprehensive, well-commented demo for StatsRegistry (and its parent,
 * TimerRegistry). Each section is self-contained and can be read independently.
 *
 * Compile (C++20):
 *   g++ -std=c++20 -O2 -pthread demo.cpp -o demo && ./demo
 */

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "stats_registry.hxx"

// ─────────────────────────────────────────────────────────────────────────────
// Small helpers used throughout the demo
// ─────────────────────────────────────────────────────────────────────────────

// Prints a big section banner so the terminal output is easy to navigate.
static void section(const std::string& title) {
    const int W = 70;
    std::string bar(W, '=');
    std::cout << "\n" << bar << "\n";
    // Centre the title
    int pad = (W - static_cast<int>(title.size()) - 2) / 2;
    std::cout << std::string(static_cast<std::size_t>(std::max(0, pad)), ' ') << "[ " << title << " ]\n";
    std::cout << bar << "\n\n";
}

static void subsection(const std::string& title) { std::cout << "\n── " << title << " ──────────────────────────────────\n"; }

// Sleeps for a random duration between lo_ms and hi_ms milliseconds.
static void random_sleep(int lo_ms, int hi_ms) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(lo_ms, hi_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 — Basic Timer usage (inherited from TimerRegistry)
// ─────────────────────────────────────────────────────────────────────────────

void demo_basic_timers() {
    section("1 · Basic Timers (inherited from TimerRegistry)");

    // StatsRegistry IS a TimerRegistry, so every timer feature works as-is.
    auto& reg = STATS;

    // ── 1a. Simple start / stop ───────────────────────────────────────────
    subsection("1a. start / stop / elapsed");

    reg.start("load_config");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    reg.stop("load_config");

    std::cout << "load_config elapsed: " << reg.elapsed_ms("load_config") << " ms\n";

    // ── 1b. Multiple laps (start → stop repeated) ─────────────────────────
    subsection("1b. Repeated start/stop — accumulating stats");

    // Simulate calling an "image resize" routine 5 times.
    for (int i = 0; i < 5; ++i) {
        reg.start("image_resize");
        random_sleep(10, 40);
        reg.stop("image_resize");
    }

    auto s = reg.stats("image_resize");
    std::cout << "image_resize — calls: " << s.count << "  total: " << s.get_total<std::chrono::milliseconds>() << " ms"
              << "  mean: " << s.get_mean<std::chrono::milliseconds>() << " ms"
              << "  min: " << s.get_min<std::chrono::milliseconds>() << " ms"
              << "  max: " << s.get_max<std::chrono::milliseconds>() << " ms"
              << "\n";

    // ── 1c. ScopedTimer — RAII convenience ───────────────────────────────
    subsection("1c. ScopedTimer (RAII — stops automatically on scope exit)");

    for (int i = 0; i < 3; ++i) {
        // Timer starts here, stops when `t` goes out of scope.
        ScopedTimer<std::chrono::milliseconds> t("json_parse", reg);
        random_sleep(5, 20);
    }  // ← stop() called here

    std::cout << "json_parse stats recorded via ScopedTimer.\n";

    // ── 1d. Standalone ScopedTimer (no registry — prints to stdout) ───────
    subsection("1d. Standalone ScopedTimer (prints on destruction)");
    {
        ScopedTimer<std::chrono::milliseconds> t("standalone_op");
        random_sleep(15, 15);
    }  // prints "standalone_op: 15 ms" here

    // ── 1e. Final merged report ───────────────────────────────────────────
    subsection("1e. Merged timer stats report");
    reg.print_stats_report();
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2 — Counters
// ─────────────────────────────────────────────────────────────────────────────

void demo_counters() {
    section("2 · Counters");

    auto& reg = STATS;

    // ── 2a. Basic increment / decrement ──────────────────────────────────
    subsection("2a. Increment and decrement");

    // Count HTTP requests by status code.
    for (int i = 0; i < 142; ++i) reg.counter_inc("http.200");
    for (int i = 0; i < 17; ++i) reg.counter_inc("http.404");
    for (int i = 0; i < 3; ++i) reg.counter_inc("http.500");

    std::cout << "http.200 : " << reg.counter_get("http.200") << "\n";
    std::cout << "http.404 : " << reg.counter_get("http.404") << "\n";
    std::cout << "http.500 : " << reg.counter_get("http.500") << "\n";

    // ── 2b. Custom delta ──────────────────────────────────────────────────
    subsection("2b. Custom delta (batch accounting)");

    // Suppose we process 512 bytes in one go.
    reg.counter_inc("bytes_read", 512);
    reg.counter_inc("bytes_read", 1024);
    reg.counter_inc("bytes_read", 256);
    std::cout << "bytes_read total: " << reg.counter_get("bytes_read") << " bytes\n";

    // ── 2c. Decrement (e.g. available slots) ─────────────────────────────
    subsection("2c. Decrement — tracking available pool slots");

    reg.counter_set("pool_slots", 8);
    reg.counter_dec("pool_slots");  // slot acquired
    reg.counter_dec("pool_slots");  // slot acquired
    std::cout << "pool_slots available: " << reg.counter_get("pool_slots") << " / 8\n";
    reg.counter_inc("pool_slots");  // slot released
    std::cout << "pool_slots available: " << reg.counter_get("pool_slots") << " / 8 (after release)\n";

    // ── 2d. ScopedCounter — automatic inc/dec ────────────────────────────
    subsection("2d. ScopedCounter (RAII inc on enter, dec on exit)");

    std::cout << "active_connections before: " << [&] {
        try {
            return reg.counter_get("active_connections");
        } catch (...) {
            return int64_t(0);
        }
    }() << "\n";
    {
        ScopedCounter c1("active_connections", STATS);
        ScopedCounter c2("active_connections", STATS);
        std::cout << "active_connections (2 open): " << reg.counter_get("active_connections") << "\n";
    }  // both decremented here
    std::cout << "active_connections (all closed): " << reg.counter_get("active_connections") << "\n";

    // ── 2e. Thread-safe increment from multiple threads ───────────────────
    subsection("2e. Concurrent increments from 8 threads (1000 each → expect 8000)");

    // Reset from any previous runs.
    try {
        reg.counter_reset("concurrent_hits");
    } catch (...) {
    }

    constexpr int THREADS = 8, ITERS = 1000;
    std::vector<std::thread> workers;
    for (int t = 0; t < THREADS; ++t)
        workers.emplace_back([&] {
            for (int i = 0; i < ITERS; ++i) reg.counter_inc("concurrent_hits");
        });
    for (auto& w : workers) w.join();

    std::cout << "concurrent_hits: " << reg.counter_get("concurrent_hits") << "  (expected " << THREADS * ITERS << ")\n";

    // ── 2f. Counter report ────────────────────────────────────────────────
    subsection("2f. Counter report");
    reg.print_counter_report();
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 3 — Gauges
// ─────────────────────────────────────────────────────────────────────────────

void demo_gauges() {
    section("3 · Gauges (fractional statistics)");

    auto& reg = STATS;

    std::mt19937 rng{42};

    // ── 3a. Basic recording ───────────────────────────────────────────────
    subsection("3a. Recording CPU utilisation samples (0–100 %)");

    std::normal_distribution<double> cpu_dist(65.0, 12.0);  // mean 65%, std 12%
    for (int i = 0; i < 200; ++i) reg.gauge_record("cpu_pct", std::clamp(cpu_dist(rng), 0.0, 100.0));

    // ── 3b. Multiple gauges with different semantics ───────────────────────
    subsection("3b. Memory usage, request latency, and cache hit-rate");

    std::normal_distribution<double> mem_dist(72.0, 8.0);    // % memory used
    std::lognormal_distribution<double> lat_dist(3.5, 0.8);  // response latency (ms), log-normal
    std::bernoulli_distribution cache_dist(0.87);            // cache hit (1) or miss (0)

    for (int i = 0; i < 500; ++i) {
        reg.gauge_record("memory_pct", std::clamp(mem_dist(rng), 0.0, 100.0));
        reg.gauge_record("latency_ms", lat_dist(rng));
        reg.gauge_record("cache_hit", cache_dist(rng) ? 1.0 : 0.0);  // mean ≈ hit-rate
    }

    // ── 3c. Reading stats back programmatically ───────────────────────────
    subsection("3c. Reading gauge stats programmatically");

    auto rows = reg.get_gauge_report();
    for (const auto& r : rows) {
        std::cout << std::left << std::setw(16) << r.name << "  samples=" << std::setw(5) << r.count << "  mean=" << std::fixed
                  << std::setprecision(3) << std::setw(9) << r.mean << "  stddev=" << std::setw(9) << r.stddev << "  min=" << std::setw(9) << r.min
                  << "  max=" << std::setw(9) << r.max << "\n";
    }

    // For cache_hit the mean IS the hit-rate.
    auto it = std::find_if(rows.begin(), rows.end(), [](const auto& r) { return r.name == "cache_hit"; });
    if (it != rows.end()) std::cout << "\nDerived cache hit-rate: " << std::fixed << std::setprecision(1) << it->mean * 100.0 << " %\n";

    // ── 3d. Gauge report (formatted table) ────────────────────────────────
    subsection("3d. Gauge report (formatted table)");
    reg.print_gauge_report();
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 4 — Histograms
// ─────────────────────────────────────────────────────────────────────────────

void demo_histograms() {
    section("4 · Histograms (bucketed distributions)");

    auto& reg = STATS;
    std::mt19937 rng{99};

    // ── 4a. Uniform distribution ──────────────────────────────────────────
    subsection("4a. Uniform distribution over [0, 100) — 10 equal buckets");

    reg.histogram_create("uniform_score", 0.0, 100.0, 10);
    std::uniform_real_distribution<double> uni(0.0, 100.0);
    for (int i = 0; i < 1000; ++i) reg.histogram_record("uniform_score", uni(rng));

    // ── 4b. Normal distribution centred at 50 ─────────────────────────────
    subsection("4b. Normal distribution (μ=50, σ=10) — 20 buckets, range [0,100)");

    reg.histogram_create("normal_score", 0.0, 100.0, 20);
    std::normal_distribution<double> norm(50.0, 10.0);
    for (int i = 0; i < 2000; ++i) reg.histogram_record("normal_score", norm(rng));
    // Some values will land outside [0,100) → overflow/underflow counted.

    // ── 4c. Response-time histogram ───────────────────────────────────────
    subsection("4c. Log-normal response times (ms) — bucket [0, 500), 10 buckets");

    reg.histogram_create("response_time_ms", 0.0, 500.0, 10);
    std::lognormal_distribution<double> lognorm(4.0, 0.9);
    for (int i = 0; i < 2000; ++i) reg.histogram_record("response_time_ms", lognorm(rng));

    // ── 4d. Programmatic access ───────────────────────────────────────────
    subsection("4d. Reading histogram data programmatically");

    auto hist_rows = reg.get_histogram_report();
    for (const auto& row : hist_rows) {
        std::cout << "Histogram: " << row.name << "  total=" << row.total << "  underflow=" << row.underflow << "  overflow=" << row.overflow << "\n";
        // Find the peak bucket.
        auto peak = std::max_element(row.buckets.begin(), row.buckets.end(), [](const auto& a, const auto& b) { return a.count < b.count; });
        if (peak != row.buckets.end())
            std::cout << "  Peak bucket: [" << std::fixed << std::setprecision(1) << peak->low << ", " << peak->high << ")  count=" << peak->count
                      << "  (" << std::setprecision(1) << peak->pct << "%)\n";
    }

    // ── 4e. ASCII bar chart report ────────────────────────────────────────
    subsection("4e. ASCII bar-chart report");
    reg.print_histogram_report(50);
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 5 — Multi-threaded scenario
// ─────────────────────────────────────────────────────────────────────────────

void demo_multithreaded() {
    section("5 · Multi-threaded scenario — simulated web server");

    /*
     * We simulate a tiny HTTP server with 6 worker threads.
     * Each thread handles "requests" in a loop and:
     *   - Times   : request handling ("req.handle") + DB query ("req.db")
     *   - Counters: total requests, errors, active requests (scoped)
     *   - Gauges  : response payload size
     *   - Histogram: response time distribution
     */

    auto& reg = STATS;

    // Create the histogram once before the threads start.
    reg.histogram_create("server.response_ms", 0.0, 300.0, 12);

    constexpr int N_THREADS = 6;
    constexpr int N_REQUESTS = 50;  // requests per thread

    std::mt19937 seed_rng{77};
    std::vector<std::thread> workers;

    for (int t = 0; t < N_THREADS; ++t) {
        unsigned seed = seed_rng();
        workers.emplace_back([&reg, seed]() {
            std::mt19937 rng{seed};
            std::normal_distribution<double> latency_dist(80.0, 30.0);       // ms
            std::normal_distribution<double> db_dist(20.0, 8.0);             // ms
            std::uniform_real_distribution<double> payload_dist(0.5, 50.0);  // KB
            std::bernoulli_distribution error_dist(0.05);                    // 5% error rate

            for (int i = 0; i < N_REQUESTS; ++i) {
                // Track how many requests are in-flight right now.
                ScopedCounter in_flight("server.active_requests", STATS);

                // Time the whole request.
                reg.start("req.handle");

                // Simulate DB query (sub-timer).
                reg.start("req.db");
                double db_ms = std::max(1.0, db_dist(rng));
                std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long>(db_ms * 1000)));
                reg.stop("req.db");

                // Simulate CPU work.
                double work_ms = std::max(1.0, latency_dist(rng) - db_ms);
                std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long>(work_ms * 1000)));

                reg.stop("req.handle");

                // Record total response time.
                double total_ms = db_ms + work_ms;
                reg.histogram_record("server.response_ms", total_ms);
                reg.gauge_record("server.payload_kb", payload_dist(rng));

                reg.counter_inc("server.requests_total");
                if (error_dist(rng)) reg.counter_inc("server.errors");
            }
        });
    }

    // Show the active-request count while threads run.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    try {
        std::cout << "server.active_requests (mid-run): " << reg.counter_get("server.active_requests") << "\n";
    } catch (...) {
    }

    for (auto& w : workers) w.join();

    std::cout << "\n--- All threads finished ---\n\n";

    // ── Reports ───────────────────────────────────────────────────────────
    subsection("Timer report — per name, merged across all threads");
    reg.print_stats_report();

    subsection("Timer report — per thread breakdown");
    reg.print_stats_report_per_thread();

    subsection("Counter report");
    reg.print_counter_report();

    subsection("Gauge report");
    reg.print_gauge_report();

    subsection("Histogram report");
    reg.print_histogram_report(45);
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 6 — Reset and erase
// ─────────────────────────────────────────────────────────────────────────────

void demo_reset_erase() {
    section("6 · Reset & Erase");

    auto& reg = STATS;

    subsection("6a. Resetting a counter back to zero");
    reg.counter_inc("scratch_counter", 99);
    std::cout << "Before reset: " << reg.counter_get("scratch_counter") << "\n";
    reg.counter_reset("scratch_counter");
    std::cout << "After reset:  " << reg.counter_get("scratch_counter") << "\n";

    subsection("6b. Erasing a gauge");
    reg.gauge_record("temp_gauge", 3.14);
    reg.gauge_erase("temp_gauge");
    // Attempting to use it now would throw; new records create a fresh gauge.
    reg.gauge_record("temp_gauge", 2.71);
    std::cout << "temp_gauge re-created with 1 sample.\n";

    subsection("6c. Resetting a timer");
    reg.start("ephemeral_timer");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    reg.stop("ephemeral_timer");
    std::cout << "Before reset — ephemeral_timer calls: " << reg.stats("ephemeral_timer").count << "\n";
    reg.reset("ephemeral_timer");
    // After reset the name still exists, but stats are cleared.
    reg.start("ephemeral_timer");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    reg.stop("ephemeral_timer");
    std::cout << "After  reset — ephemeral_timer calls: " << reg.stats("ephemeral_timer").count << "  (should be 1)\n";

    subsection("6d. Erasing a histogram");
    reg.histogram_create("old_hist", 0.0, 10.0, 5);
    reg.histogram_record("old_hist", 4.5);
    reg.histogram_erase("old_hist");
    std::cout << "old_hist erased. Re-creating with different range...\n";
    reg.histogram_create("old_hist", 0.0, 100.0, 10);
    reg.histogram_record("old_hist", 55.0);
    std::cout << "old_hist re-created OK.\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 7 — Combined print_all_reports()
// ─────────────────────────────────────────────────────────────────────────────

void demo_all_reports() {
    section("7 · print_all_reports() — everything in one call");
    STATS.print_all_reports();
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << std::string(70, '-') << "\n";
    std::cout << "  StatsRegistry Demo\n";
    std::cout << std::string(70, '-') << "\n";

    demo_basic_timers();
    demo_counters();
    demo_gauges();
    demo_histograms();
    demo_multithreaded();
    demo_reset_erase();
    demo_all_reports();

    std::cout << "\n" << std::string(70, '-') << "\n";
    std::cout << "  Demo complete.\n";
    std::cout << std::string(70, '-') << "\n";
    return 0;
}