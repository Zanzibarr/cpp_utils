/**
 * @file bench_logger_simple.cpp
 * @brief Simple timing comparison: loop with vs without logging.
 *
 * Compile (C++23):
 *   g++ -std=c++23 -O2 -pthread bench_logger_simple.cpp -o bench_logger_simple
 *
 * Output is suppressed via rdbuf swap so terminal I/O doesn't dominate.
 */

#include <chrono>
#include <iostream>
#include <streambuf>
#include <string>

#include "logger.hxx"

// ── /dev/null sink ────────────────────────────────────────────────────────────

struct NullBuf : std::streambuf {
    auto overflow(int c) -> int override { return c; }
    auto xsputn(const char*, std::streamsize n) -> std::streamsize override { return n; }
};

// ── Timing helper ─────────────────────────────────────────────────────────────

using clk = std::chrono::steady_clock;

static auto now_ms() -> double { return std::chrono::duration<double, std::milli>(clk::now().time_since_epoch()).count(); }

// ── Config ────────────────────────────────────────────────────────────────────

static constexpr int ITERATIONS = 1'000'000;
static const std::string MSG = "request processed: id=12345 status=200 latency=42ms";

// ─────────────────────────────────────────────────────────────────────────────

auto main() -> int {
    // Redirect logger output to /dev/null so we measure CPU not I/O.
    // We do this AFTER cout is set up so our own print calls still work.
    NullBuf null_buf;

    // ── Logger setup ──────────────────────────────────────────────────────────
    log_init();  // sync, stdout, colors on, thread-id on

    // ── 1. Baseline: loop with no logging ─────────────────────────────────────
    volatile int sink = 0;  // prevent the loop being optimised away

    double t0 = now_ms();
    for (int i = 0; i < ITERATIONS; ++i) {
        sink += i;
    }
    double baseline_ms = now_ms() - t0;

    // ── 2. Sync logger, string API ────────────────────────────────────────────
    {
        std::streambuf* old_cout = std::cout.rdbuf(&null_buf);
        std::streambuf* old_cerr = std::cerr.rdbuf(&null_buf);

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            LOG_INFO_S(MSG);
        }
        double sync_string_ms = now_ms() - t0;

        std::cout.rdbuf(old_cout);
        std::cerr.rdbuf(old_cerr);

        std::cout << "[sync  / string ] total: " << sync_string_ms << " ms"
                  << "   overhead vs baseline: " << (sync_string_ms - baseline_ms) << " ms"
                  << "   per-call: " << (sync_string_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── 3. Sync logger, stream API ────────────────────────────────────────────
    {
        std::streambuf* old_cout = std::cout.rdbuf(&null_buf);
        std::streambuf* old_cerr = std::cerr.rdbuf(&null_buf);

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            LOG_INFO << MSG;
        }
        double sync_stream_ms = now_ms() - t0;

        std::cout.rdbuf(old_cout);
        std::cerr.rdbuf(old_cerr);

        std::cout << "[sync  / stream ] total: " << sync_stream_ms << " ms"
                  << "   overhead vs baseline: " << (sync_stream_ms - baseline_ms) << " ms"
                  << "   per-call: " << (sync_stream_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── 4. Filtered logger (min=ERROR, INFO is silenced) ─────────────────────
    {
        default_logger().set_min_level(Logger::level::ERROR);

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            LOG_INFO_S(MSG);
        }
        double filtered_string_ms = now_ms() - t0;

        default_logger().set_min_level(Logger::level::BASIC);  // restore

        std::cout << "[filter/ string ] total: " << filtered_string_ms << " ms"
                  << "   overhead vs baseline: " << (filtered_string_ms - baseline_ms) << " ms"
                  << "   per-call: " << (filtered_string_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── 5. Filtered stream API ────────────────────────────────────────────────
    {
        default_logger().set_min_level(Logger::level::ERROR);

        t0 = now_ms();
        for (int i = 0; i < ITERATIONS; ++i) {
            sink += i;
            LOG_INFO << MSG;
        }
        double filtered_stream_ms = now_ms() - t0;

        default_logger().set_min_level(Logger::level::BASIC);

        std::cout << "[filter/ stream ] total: " << filtered_stream_ms << " ms"
                  << "   overhead vs baseline: " << (filtered_stream_ms - baseline_ms) << " ms"
                  << "   per-call: " << (filtered_stream_ms / ITERATIONS * 1e6) << " ns\n";
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    std::cout << "\n[baseline       ] total: " << baseline_ms << " ms"
              << "   (" << ITERATIONS << " iterations)\n";
    std::cout << "(sink=" << sink << ")\n";  // prevent sink being optimised away

    return 0;
}