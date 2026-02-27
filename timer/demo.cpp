/**
 * demo.cpp — timer.hxx usage examples
 *
 * This file walks through the main features of timer.hxx using a simulated
 * web server as a running example. Requests go through three stages:
 *
 *   parse  →  query  →  render
 *
 * Work is simulated with short sleeps so the focus stays on the timer API.
 */

#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "timer.hxx"

// Simulates a variable-duration operation.
static void work(int min_ms, int max_ms) {
    thread_local std::mt19937 rng{static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id()))};
    std::uniform_int_distribution<int> dist{min_ms, max_ms};
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. Timer — basic start / stop / elapsed
//
// Timer is a plain single-threaded stopwatch. Use it when you want to time
// something inline without involving the registry.
// ─────────────────────────────────────────────────────────────────────────────

void demo_basic_timer() {
    std::cout << "── 1. Basic Timer ───────────────────────────────────────────\n";

    Timer t;

    // Measure server cold-start: load config, bind socket, warm caches.
    t.start();
    work(5, 5);  // load config
    work(3, 3);  // bind socket
    work(8, 8);  // warm caches
    t.stop();

    std::cout << "  Server startup took " << t.elapsed_ms() << " ms\n\n";

    // reset() clears accumulated time — the timer is ready to reuse.
    t.reset();
    t.start();
    work(2, 2);
    t.stop();
    std::cout << "  First health-check took " << t.elapsed_ms() << " ms\n\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. ScopedTimer — automatic stop on scope exit
//
// ScopedTimer starts on construction and stops when it leaves scope.
// The standalone version (no registry) prints the result to stdout
// automatically in its destructor — useful for quick one-off measurements.
// ─────────────────────────────────────────────────────────────────────────────

void handle_request_simple() {
    // Destructor will print: "request: <elapsed> ms"
    ScopedTimer<std::chrono::milliseconds> t("request");

    work(1, 4);  // parse
    work(5, 8);  // query
    work(1, 3);  // render
}  // <-- t.stop() + print happen here

void demo_scoped_timer() {
    std::cout << "── 2. ScopedTimer (standalone) ──────────────────────────────\n";
    std::cout << "  Handling 3 requests:\n";

    handle_request_simple();
    handle_request_simple();
    handle_request_simple();

    std::cout << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. TimerRegistry — named timers with statistics
//
// TimerRegistry tracks multiple named timers and accumulates statistics
// (mean, min, max, stddev) across many start/stop cycles.
// ScopedTimer + registry is the recommended pattern for production code:
// each stage gets its own name and stats accumulate automatically.
// ─────────────────────────────────────────────────────────────────────────────

void handle_request(TimerRegistry& reg) {
    ScopedTimer<std::chrono::milliseconds> total("total", reg);

    {
        ScopedTimer<std::chrono::milliseconds> s("parse", reg);
        work(1, 4);
    }
    {
        ScopedTimer<std::chrono::milliseconds> s("query", reg);
        work(5, 8);
    }
    {
        ScopedTimer<std::chrono::milliseconds> s("render", reg);
        work(1, 3);
    }
}

void demo_registry() {
    std::cout << "── 3. TimerRegistry — single-threaded stats ─────────────────\n";

    auto& reg = TIMERS;

    // Process 20 requests. Each stage's stats accumulate silently.
    for (int i = 0; i < 20; ++i) handle_request(reg);

    // print_stats_report() shows one row per named timer.
    // Column units are chosen automatically: each column independently picks
    // the most readable unit based on the smallest value in that column.
    std::cout << "\n  After 20 requests:\n\n";
    reg.print_stats_report();
    std::cout << "\n";

    // Erase all timers before the next demo section.
    for (const auto& [name, _] : reg.get_report()) reg.erase(name);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Multi-threaded — per-thread stats and merged report
//
// TimerRegistry is designed for multi-threaded use. Each thread accumulates
// its own stats independently with no locking on the hot path. Reports merge
// all threads together (or show per-thread breakdowns) on demand.
// ─────────────────────────────────────────────────────────────────────────────

void demo_multithreaded() {
    std::cout << "── 4. Multi-threaded registry ───────────────────────────────\n";

    auto& reg = TIMERS;
    const int THREADS = 4;
    const int REQUESTS = 32;  // total, divided across threads

    // Each thread processes a share of the requests.
    std::vector<std::thread> workers;
    for (int i = 0; i < THREADS; ++i) {
        workers.emplace_back([&]() {
            for (int j = 0; j < REQUESTS / THREADS; ++j) handle_request(reg);
        });
    }
    for (auto& w : workers) w.join();

    // Merged report: one row per timer name, stats combined across all threads.
    // The 'Threads' column shows how many threads contributed to each row.
    std::cout << "\n  Merged report (" << REQUESTS << " requests across " << THREADS << " threads):\n\n";
    reg.print_stats_report();

    // Per-thread report: one row per (timer name, thread) pair.
    // Useful for spotting load imbalance or a single outlier thread.
    std::cout << "\n  Per-thread report:\n\n";
    reg.print_stats_report_per_thread();

    // ── Programmatic access ───────────────────────────────────────────────
    // Reports are also available as vectors of plain structs if you want to
    // process the numbers yourself rather than just print them.
    auto rows = reg.get_stats_report<std::chrono::milliseconds>();

    double slowest_mean = 0.0;
    std::string slowest_stage;
    for (const auto& r : rows) {
        if (r.name != "total" && r.mean > slowest_mean) {
            slowest_mean = r.mean;
            slowest_stage = r.name;
        }
    }
    std::cout << "\n  Slowest stage: [" << slowest_stage << "]"
              << "  mean = " << std::fixed << std::setprecision(2) << slowest_mean << " ms\n";

    // ── reset() — clear all stats and reuse the same timer names ─────────
    // reset() wipes accumulated stats across all threads but keeps the names
    // registered, so any thread can call start() again immediately.
    for (const auto& [name, _] : reg.get_report()) reg.reset(name);

    std::cout << "\n  Timers reset. Running 5 more requests...\n";
    for (int i = 0; i < 5; ++i) handle_request(reg);

    auto after = reg.get_stats_report<std::chrono::milliseconds>();
    for (const auto& r : after)
        if (r.name == "total")
            std::cout << "  total: " << r.call_count << " calls, mean = " << std::fixed << std::setprecision(2) << r.mean << " ms\n";

    std::cout << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n=== timer.hpp demo ===\n\n";

    demo_basic_timer();
    demo_scoped_timer();
    demo_registry();
    demo_multithreaded();

    return 0;
}