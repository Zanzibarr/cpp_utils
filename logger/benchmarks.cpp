/**
 * @file bench_logger_simple.cpp
 * @brief Benchmark producer-side latency and total throughput against a real file sink.
 *
 * Compile (C++20):
 *   g++ -std=c++20 -O2 -pthread bench_logger_simple.cpp -o bench_logger_simple
 *
 * Two metrics per variant:
 *   - enqueue_ms : time for the producer loop only (what the hot path pays)
 *   - total_ms   : enqueue + flush (all bytes on disk)
 */

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

#include "logger.hxx"

using enum LoggerLevel;
using clk = std::chrono::steady_clock;

static auto now_ms() -> double { return std::chrono::duration<double, std::milli>(clk::now().time_since_epoch()).count(); }

// ── Config ────────────────────────────────────────────────────────────────────

static constexpr int ITERATIONS = 100'000;  // fewer — we're hitting real disk
static const std::string MSG = "request processed: id=12345 status=200 latency=42ms";

static constexpr const char* SYNC_FILE = "/tmp/bench_sync.log";
static constexpr const char* ASYNC_FILE = "/tmp/bench_async.log";

// ── helpers ───────────────────────────────────────────────────────────────────

static void remove_file(const char* path) { std::remove(path); }

// ─────────────────────────────────────────────────────────────────────────────

auto main() -> int {
    volatile int sink = 0;

    // ── Baseline ──────────────────────────────────────────────────────────────
    double t0 = now_ms();
    for (int i = 0; i < ITERATIONS; ++i) sink += i;
    double baseline_ms = now_ms() - t0;

    // ── 1. Sync / string ──────────────────────────────────────────────────────
    {
        remove_file(SYNC_FILE);
        Logger log(LoggerConfig{}.with_stdout(false).with_file(SYNC_FILE).with_min_level(DEBUG));

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            log.info(MSG);
        }
        double enqueue_ms = now_ms() - t0;
        log.flush();
        double total_ms = now_ms() - t0;

        std::cout << "[sync  / string ] enqueue: " << enqueue_ms << " ms"
                  << "   total: " << total_ms << " ms"
                  << "   overhead vs baseline: " << (enqueue_ms - baseline_ms) << " ms"
                  << "   per-call: " << (enqueue_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── 2. Sync / stream ──────────────────────────────────────────────────────
    {
        remove_file(SYNC_FILE);
        Logger log(LoggerConfig{}.with_stdout(false).with_file(SYNC_FILE).with_min_level(DEBUG));

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            log[INFO] << MSG;
        }
        double enqueue_ms = now_ms() - t0;
        log.flush();
        double total_ms = now_ms() - t0;

        std::cout << "[sync  / stream ] enqueue: " << enqueue_ms << " ms"
                  << "   total: " << total_ms << " ms"
                  << "   overhead vs baseline: " << (enqueue_ms - baseline_ms) << " ms"
                  << "   per-call: " << (enqueue_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── 3. Async / string ─────────────────────────────────────────────────────
    {
        remove_file(ASYNC_FILE);
        Logger log(LoggerConfig{}.with_stdout(false).with_file(ASYNC_FILE).with_async().with_min_level(DEBUG));

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            log.info(MSG);
        }
        // Stop the clock here — this is the producer hot-path cost.
        double enqueue_ms = now_ms() - t0;
        log.flush();  // drain worker; includes actual disk writes
        double total_ms = now_ms() - t0;

        std::cout << "[async / string ] enqueue: " << enqueue_ms << " ms"
                  << "   total: " << total_ms << " ms"
                  << "   overhead vs baseline: " << (enqueue_ms - baseline_ms) << " ms"
                  << "   per-call: " << (enqueue_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── 4. Async / stream ─────────────────────────────────────────────────────
    {
        remove_file(ASYNC_FILE);
        Logger log(LoggerConfig{}.with_stdout(false).with_file(ASYNC_FILE).with_async().with_min_level(DEBUG));

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            log[INFO] << MSG;
        }
        double enqueue_ms = now_ms() - t0;
        log.flush();
        double total_ms = now_ms() - t0;

        std::cout << "[async / stream ] enqueue: " << enqueue_ms << " ms"
                  << "   total: " << total_ms << " ms"
                  << "   overhead vs baseline: " << (enqueue_ms - baseline_ms) << " ms"
                  << "   per-call: " << (enqueue_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── 5. Filter / string (sanity check — no file I/O) ───────────────────────
    {
        Logger log(LoggerConfig{}.with_stdout(false).with_min_level(ERROR));

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            log.info(MSG);
        }
        double enqueue_ms = now_ms() - t0;
        double total_ms = enqueue_ms;  // nothing to flush

        std::cout << "[filter/ string ] enqueue: " << enqueue_ms << " ms"
                  << "   total: " << total_ms << " ms"
                  << "   overhead vs baseline: " << (enqueue_ms - baseline_ms) << " ms"
                  << "   per-call: " << (enqueue_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── 6. Filter / stream ────────────────────────────────────────────────────
    {
        Logger log(LoggerConfig{}.with_stdout(false).with_min_level(ERROR));

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            log[INFO] << MSG;
        }
        double enqueue_ms = now_ms() - t0;
        double total_ms = enqueue_ms;

        std::cout << "[filter/ stream ] enqueue: " << enqueue_ms << " ms"
                  << "   total: " << total_ms << " ms"
                  << "   overhead vs baseline: " << (enqueue_ms - baseline_ms) << " ms"
                  << "   per-call: " << (enqueue_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "\n[baseline       ] total: " << baseline_ms << " ms"
              << "   (" << ITERATIONS << " iterations)\n";
    std::cout << "(sink=" << sink << ")\n";

    remove_file(SYNC_FILE);
    remove_file(ASYNC_FILE);

    return 0;
}