/**
 * demo.cpp
 * ───────────────────────────────────────────────────────────────────────────
 * A comprehensive demo for StatsRegistry (and its parent, TimerRegistry).
 * Every public API call uses the compile-time ct_string template syntax.
 * Each section is self-contained and can be read independently.
 *
 * Compile (C++20):
 *   g++ -std=c++20 -O2 -pthread demo.cpp -o demo && ./demo
 */

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "stats_registry.hxx"

// ─────────────────────────────────────────────────────────────────────────────
// Small helpers
// ─────────────────────────────────────────────────────────────────────────────

static void section(const std::string& title) {
    const int W = 70;
    std::cout << "\n" << std::string(W, '=') << "\n";
    int pad = (W - static_cast<int>(title.size()) - 2) / 2;
    std::cout << std::string(static_cast<std::size_t>(std::max(0, pad)), ' ') << "[ " << title << " ]\n" << std::string(W, '=') << "\n\n";
}

static void subsection(const std::string& title) { std::cout << "\n── " << title << " ──────────────────────────────────\n"; }

static void random_sleep(int lo_ms, int hi_ms) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(lo_ms, hi_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 — Basic Timer usage (from TimerRegistry)
// ─────────────────────────────────────────────────────────────────────────────

void demo_basic_timers() {
    section("1 · Basic Timers (inherited from TimerRegistry)");

    auto& reg = STATS;

    // ── 1a. Manual start / stop ───────────────────────────────────────────
    subsection("1a. start<n> / stop<n> / elapsed<n>");

    reg.start<"load_config">();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    reg.stop<"load_config">();

    std::cout << "load_config elapsed: " << reg.elapsed<"load_config", std::chrono::milliseconds>() << " ms\n";

    // ── 1b. Repeated laps — accumulating stats ────────────────────────────
    subsection("1b. Repeated start/stop — accumulating stats");

    for (int i = 0; i < 5; ++i) {
        reg.start<"image_resize">();
        random_sleep(10, 40);
        reg.stop<"image_resize">();
    }

    auto s = reg.stats<"image_resize">();
    std::cout << "image_resize — calls: " << s.count << "  total: " << s.get_total<std::chrono::milliseconds>() << " ms"
              << "  mean: " << s.get_mean<std::chrono::milliseconds>() << " ms"
              << "  min: " << s.get_min<std::chrono::milliseconds>() << " ms"
              << "  max: " << s.get_max<std::chrono::milliseconds>() << " ms\n";

    // ── 1c. is_running ────────────────────────────────────────────────────
    subsection("1c. is_running<n>");

    reg.start<"background_task">();
    std::cout << "background_task running: " << std::boolalpha << reg.is_running<"background_task">() << "\n";
    reg.stop<"background_task">();
    std::cout << "background_task running: " << reg.is_running<"background_task">() << "\n";

    // ── 1d. Handle-based stop — fastest path ─────────────────────────────
    subsection("1d. Handle-based stop (Slot* returned by start<n>)");

    for (int i = 0; i < 3; ++i) {
        auto* slot = reg.start<"json_parse">();
        random_sleep(5, 15);
        TimerRegistry::stop(slot);  // bypasses even the array lookup
    }
    std::cout << "json_parse calls: " << reg.stats<"json_parse">().count << "\n";

    // ── 1e. make_scoped_timer ─────────────────────────────────────────────
    subsection("1e. make_scoped_timer<n> — RAII, stops on scope exit");

    for (int i = 0; i < 3; ++i) {
        auto t = make_scoped_timer<"render_frame">(reg);
        random_sleep(8, 20);
    }  // stop() called here
    std::cout << "render_frame calls: " << reg.stats<"render_frame">().count << "\n";

    // ── 1f. Standalone ScopedTimer — prints to stdout ─────────────────────
    subsection("1f. Standalone ScopedTimer (no registry — prints on destruction)");
    {
        ScopedTimer<std::chrono::milliseconds> t("standalone_op");
        random_sleep(15, 15);
    }  // prints "standalone_op: 15 ms" here

    // ── 1g. reset<n> ─────────────────────────────────────────────────────
    subsection("1g. reset<n>");

    std::cout << "image_resize calls before reset: " << reg.stats<"image_resize">().count << "\n";
    reg.reset<"image_resize">();
    reg.start<"image_resize">();
    random_sleep(5, 10);
    reg.stop<"image_resize">();
    std::cout << "image_resize calls after reset:  " << reg.stats<"image_resize">().count << "  (should be 1)\n";

    // ── 1h. Merged stats report ───────────────────────────────────────────
    subsection("1h. Merged timer stats report");
    reg.print_stats_report();
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2 — Counters
// ─────────────────────────────────────────────────────────────────────────────

void demo_counters() {
    section("2 · Counters");

    auto& reg = STATS;

    // ── 2a. Basic inc / dec ───────────────────────────────────────────────
    subsection("2a. counter_inc<n> and counter_dec<n>");

    for (int i = 0; i < 142; ++i) reg.counter_inc<"http.200">();
    for (int i = 0; i < 17; ++i) reg.counter_inc<"http.404">();
    for (int i = 0; i < 3; ++i) reg.counter_inc<"http.500">();

    std::cout << "http.200 : " << reg.counter_get<"http.200">() << "\n";
    std::cout << "http.404 : " << reg.counter_get<"http.404">() << "\n";
    std::cout << "http.500 : " << reg.counter_get<"http.500">() << "\n";

    // ── 2b. Custom delta ──────────────────────────────────────────────────
    subsection("2b. Custom delta (batch accounting)");

    reg.counter_inc<"bytes_read">(512);
    reg.counter_inc<"bytes_read">(1024);
    reg.counter_inc<"bytes_read">(256);
    std::cout << "bytes_read total: " << reg.counter_get<"bytes_read">() << " bytes\n";

    // ── 2c. counter_set + decrement ───────────────────────────────────────
    subsection("2c. counter_set<n> — tracking available pool slots");

    reg.counter_set<"pool_slots">(8);
    reg.counter_dec<"pool_slots">();
    reg.counter_dec<"pool_slots">();
    std::cout << "pool_slots available: " << reg.counter_get<"pool_slots">() << " / 8\n";
    reg.counter_inc<"pool_slots">();
    std::cout << "pool_slots available: " << reg.counter_get<"pool_slots">() << " / 8 (after release)\n";

    // ── 2d. counter_ref — cached pointer for tight loops ─────────────────
    subsection("2d. counter_ref<n> — cached pointer, bypasses array lookup");

    auto* ctr = reg.counter_ref<"hot_path_hits">();
    for (int i = 0; i < 10'000; ++i) ctr->fetch_add(1, std::memory_order_relaxed);
    std::cout << "hot_path_hits (via cached pointer): " << reg.counter_get<"hot_path_hits">() << "\n";

    // ── 2e. make_scoped_counter — RAII ───────────────────────────────────
    subsection("2e. make_scoped_counter<n> — RAII inc/dec");

    std::cout << "active_connections before: " << reg.counter_get<"active_connections">() << "\n";
    {
        auto c1 = make_scoped_counter<"active_connections">(reg);
        auto c2 = make_scoped_counter<"active_connections">(reg);
        std::cout << "active_connections (2 open): " << reg.counter_get<"active_connections">() << "\n";
    }  // both decremented here
    std::cout << "active_connections (all closed): " << reg.counter_get<"active_connections">() << "\n";

    // ── 2f. counter_reset ─────────────────────────────────────────────────
    subsection("2f. counter_reset<n>");

    std::cout << "http.200 before reset: " << reg.counter_get<"http.200">() << "\n";
    reg.counter_reset<"http.200">();
    std::cout << "http.200 after reset:  " << reg.counter_get<"http.200">() << "\n";

    // ── 2g. Concurrent increments ─────────────────────────────────────────
    subsection("2g. Concurrent increments from 8 threads (1000 each → expect 8000)");

    reg.counter_reset<"concurrent_hits">();
    constexpr int THREADS = 8, ITERS = 1000;
    std::vector<std::thread> workers;
    for (int t = 0; t < THREADS; ++t)
        workers.emplace_back([&] {
            for (int i = 0; i < ITERS; ++i) reg.counter_inc<"concurrent_hits">();
        });
    for (auto& w : workers) w.join();

    std::cout << "concurrent_hits: " << reg.counter_get<"concurrent_hits">() << "  (expected " << THREADS * ITERS << ")\n";

    // ── 2h. Counter report ────────────────────────────────────────────────
    subsection("2h. Counter report");
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
    subsection("3a. gauge_record<n> — CPU utilisation samples");

    std::normal_distribution<double> cpu_dist(65.0, 12.0);
    for (int i = 0; i < 200; ++i) reg.gauge_record<"cpu_pct">(std::clamp(cpu_dist(rng), 0.0, 100.0));

    // ── 3b. Multiple gauges ───────────────────────────────────────────────
    subsection("3b. Multiple gauges — memory, latency, cache hit-rate");

    std::normal_distribution<double> mem_dist(72.0, 8.0);
    std::lognormal_distribution<double> lat_dist(3.5, 0.8);
    std::bernoulli_distribution cache_dist(0.87);

    for (int i = 0; i < 500; ++i) {
        reg.gauge_record<"memory_pct">(std::clamp(mem_dist(rng), 0.0, 100.0));
        reg.gauge_record<"latency_ms">(lat_dist(rng));
        reg.gauge_record<"cache_hit">(cache_dist(rng) ? 1.0 : 0.0);
    }

    // ── 3c. Reading stats back programmatically ───────────────────────────
    subsection("3c. get_gauge_report()");

    auto rows = reg.get_gauge_report();
    for (const auto& r : rows) {
        std::cout << std::left << std::setw(14) << r.name << "  samples=" << std::setw(5) << r.count << "  mean=" << std::fixed
                  << std::setprecision(3) << std::setw(9) << r.mean << "  stddev=" << std::setw(9) << r.stddev << "  min=" << std::setw(9) << r.min
                  << "  max=" << std::setw(9) << r.max << "\n";
    }

    auto it = std::find_if(rows.begin(), rows.end(), [](const auto& r) { return r.name == "cache_hit"; });
    if (it != rows.end()) std::cout << "\nDerived cache hit-rate: " << std::fixed << std::setprecision(1) << it->mean * 100.0 << " %\n";

    // ── 3d. gauge_reset ───────────────────────────────────────────────────
    subsection("3d. gauge_reset<n>");

    std::cout << "cpu_pct samples before reset: " << reg.get_gauge_report().front().count << "\n";
    reg.gauge_reset<"cpu_pct">();
    reg.gauge_record<"cpu_pct">(50.0);
    auto report = reg.get_gauge_report();
    auto cpu_it = std::find_if(report.begin(), report.end(), [](const auto& r) { return r.name == "cpu_pct"; });
    if (cpu_it != report.end()) std::cout << "cpu_pct samples after reset: " << cpu_it->count << "  (should be 1)\n";

    // ── 3e. Gauge report ──────────────────────────────────────────────────
    subsection("3e. Gauge report (formatted table)");
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

    reg.histogram_create<"uniform_score">(0.0, 100.0, 10);
    std::uniform_real_distribution<double> uni(0.0, 100.0);
    for (int i = 0; i < 1000; ++i) reg.histogram_record<"uniform_score">(uni(rng));

    // ── 4b. Normal distribution ───────────────────────────────────────────
    subsection("4b. Normal distribution (μ=50, σ=10) — 20 buckets [0, 100)");

    reg.histogram_create<"normal_score">(0.0, 100.0, 20);
    std::normal_distribution<double> norm(50.0, 10.0);
    for (int i = 0; i < 2000; ++i) reg.histogram_record<"normal_score">(norm(rng));

    // ── 4c. Response-time histogram ───────────────────────────────────────
    subsection("4c. Log-normal response times (ms) — [0, 500), 10 buckets");

    reg.histogram_create<"response_time_ms">(0.0, 500.0, 10);
    std::lognormal_distribution<double> lognorm(4.0, 0.9);
    for (int i = 0; i < 2000; ++i) reg.histogram_record<"response_time_ms">(lognorm(rng));

    // ── 4d. Programmatic access ───────────────────────────────────────────
    subsection("4d. get_histogram_report() — programmatic access");

    for (const auto& row : reg.get_histogram_report()) {
        std::cout << "Histogram: " << row.name << "  total=" << row.total << "  underflow=" << row.underflow << "  overflow=" << row.overflow << "\n";
        auto peak = std::max_element(row.buckets.begin(), row.buckets.end(), [](const auto& a, const auto& b) { return a.count < b.count; });
        if (peak != row.buckets.end())
            std::cout << "  Peak bucket: [" << std::fixed << std::setprecision(1) << peak->low << ", " << peak->high << ")"
                      << "  count=" << peak->count << "  (" << std::setprecision(1) << peak->pct << "%)\n";
    }

    // ── 4e. histogram_reset ───────────────────────────────────────────────
    subsection("4e. histogram_reset<n>");

    reg.histogram_reset<"uniform_score">();
    reg.histogram_record<"uniform_score">(42.0);
    auto hist_rows = reg.get_histogram_report();
    auto hit = std::find_if(hist_rows.begin(), hist_rows.end(), [](const auto& r) { return r.name == "uniform_score"; });
    if (hit != hist_rows.end()) std::cout << "uniform_score total after reset: " << hit->total << "  (should be 1)\n";

    // ── 4f. ASCII bar chart ───────────────────────────────────────────────
    subsection("4f. ASCII bar-chart report");
    reg.print_histogram_report(50);
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 5 — Multi-threaded scenario
// ─────────────────────────────────────────────────────────────────────────────

void demo_multithreaded() {
    section("5 · Multi-threaded scenario — simulated web server");

    auto& reg = STATS;

    reg.histogram_create<"server.response_ms">(0.0, 300.0, 12);

    constexpr int N_THREADS = 6;
    constexpr int N_REQUESTS = 50;

    std::mt19937 seed_rng{77};
    std::vector<std::thread> workers;

    for (int t = 0; t < N_THREADS; ++t) {
        unsigned seed = seed_rng();
        workers.emplace_back([&reg, seed] {
            std::mt19937 rng{seed};
            std::normal_distribution<double> latency_dist(80.0, 30.0);
            std::normal_distribution<double> db_dist(20.0, 8.0);
            std::uniform_real_distribution<double> payload_dist(0.5, 50.0);
            std::bernoulli_distribution error_dist(0.05);

            for (int i = 0; i < N_REQUESTS; ++i) {
                // Track in-flight requests with RAII counter.
                auto in_flight = make_scoped_counter<"server.active_requests">(reg);

                // Time the whole request with RAII timer.
                auto req_timer = make_scoped_timer<"req.handle">(reg);

                // Sub-timer for the DB query.
                auto* db_slot = reg.start<"req.db">();
                double db_ms = std::max(1.0, db_dist(rng));
                std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long>(db_ms * 1000)));
                TimerRegistry::stop(db_slot);

                // Simulate CPU work.
                double work_ms = std::max(1.0, latency_dist(rng) - db_ms);
                std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long>(work_ms * 1000)));

                reg.histogram_record<"server.response_ms">(db_ms + work_ms);
                reg.gauge_record<"server.payload_kb">(payload_dist(rng));
                reg.counter_inc<"server.requests_total">();
                if (error_dist(rng)) reg.counter_inc<"server.errors">();
            }
        });
    }

    // Snapshot mid-run.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "server.active_requests (mid-run): " << reg.counter_get<"server.active_requests">() << "\n";

    for (auto& w : workers) w.join();
    std::cout << "\n--- All threads finished ---\n\n";

    subsection("Timer report — merged across all threads");
    reg.print_stats_report();

    subsection("Timer report — per-thread breakdown");
    reg.print_stats_report_per_thread();

    subsection("Counter report");
    reg.print_counter_report();

    subsection("Gauge report");
    reg.print_gauge_report();

    subsection("Histogram report");
    reg.print_histogram_report(45);
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 6 — Combined print_all_reports()
// ─────────────────────────────────────────────────────────────────────────────

void demo_all_reports() {
    section("6 · print_all_reports() — everything in one call");
    STATS.print_all_reports();
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << std::string(70, '-') << "\n"
              << "  StatsRegistry Demo (ct_string API)\n"
              << std::string(70, '-') << "\n";

    demo_basic_timers();
    demo_counters();
    demo_gauges();
    demo_histograms();
    demo_multithreaded();
    demo_all_reports();

    std::cout << "\n"
              << std::string(70, '-') << "\n"
              << "  Demo complete.\n"
              << std::string(70, '-') << "\n";
    return 0;
}