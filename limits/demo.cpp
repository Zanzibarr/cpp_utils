// demo.cpp — limits.hxx feature demo
//
// Scenario: a program that downloads and processes a list of files.
//   - A global time limit caps the entire run (e.g. a CI job timeout)
//   - Each file gets its own local time limit (e.g. per-task deadline)
//   - A memory limit protects against runaway allocations
//
// The interesting cases this shows:
//   1. A file finishes processing within its local limit   → OK
//   2. A file exceeds its local limit                      → local timeout
//   3. The global limit fires mid-processing               → everything stops
//
// Build: g++ -std=c++20 -pthread -O2 demo.cpp -o demo

#include <chrono>
#include <format>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "limits.hxx"

// ─── simulation helpers ──────────────────────────────────────────────────────

static auto rng = std::mt19937{std::random_device{}()};

// Returns elapsed seconds since program start as a formatted string.
std::string elapsed() {
    static auto t0 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return std::format("{:5.2f}s", secs);
}

// Simulates processing work that takes between min_ms and max_ms milliseconds.
// Checks the local limiter periodically so it can abort early if needed.
bool process_chunk(timelim::LocalTimeLimiter& limiter, int min_ms, int max_ms) {
    auto chunk_time = std::uniform_int_distribution{min_ms, max_ms}(rng);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(chunk_time);

    while (std::chrono::steady_clock::now() < deadline) {
        if (limiter.expired()) return false;  // aborted — local or global fired
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

// ─── the "application" ───────────────────────────────────────────────────────

struct File {
    std::string name;
    int processing_ms;  // how long this file actually takes to process
};

// Processes one file under a local time limit.
// Returns true if the file was fully processed before any timeout.
bool process_file(const File& file, std::chrono::seconds local_limit) {
    timelim::LocalTimeLimiter local;

    local.set(local_limit, [&] {
        // Fired from the timer thread when the local limit expires.
        // Keep this minimal — just flag it; the main thread reads expired().
    });

    std::cout << std::format("  [{}]  processing {}  (limit: {}s) ...\n", elapsed(), file.name, local_limit.count());

    // Simulate processing in 3 chunks.
    for (int chunk = 1; chunk <= 3; ++chunk) {
        if (!process_chunk(local, file.processing_ms / 3, file.processing_ms / 2)) {
            if (CHECK_STOP())
                std::cout << std::format("  [{}]  {} — STOPPED (global limit reached)\n", elapsed(), file.name);
            else
                std::cout << std::format("  [{}]  {} — TIMED OUT (exceeded {}s local limit)\n", elapsed(), file.name, local_limit.count());
            return false;
        }
    }

    local.cancel();
    std::cout << std::format("  [{}]  {} — done\n", elapsed(), file.name);
    return true;
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    // Files to process, with simulated processing times (ms).
    // Some are fast, some will exceed their local limit, one will hit the global.
    const std::vector<File> files = {
        {"report_2024_q1.csv", 800},  // fast — finishes fine
        {"report_2024_q2.csv", 900},  // fast — finishes fine
        {"video_raw_4k.mp4", 4500},   // slow — will hit local limit
        {"report_2024_q3.csv", 700},  // fast — finishes fine
        {"database_dump.sql", 5000},  // slow — will hit global limit
        {"report_2024_q4.csv", 600},  // never reached
    };

    constexpr auto GLOBAL_LIMIT = std::chrono::seconds{8};
    constexpr auto LOCAL_LIMIT = std::chrono::seconds{2};
    constexpr int MEMORY_LIMIT_MB = 512;

    // ── setup ────────────────────────────────────────────────────────────────

    std::cout << std::format(
        "limits.hxx demo\n"
        "───────────────────────────────────────────\n"
        "  files to process : {}\n"
        "  global time limit: {}s  (entire run)\n"
        "  local time limit : {}s  (per file)\n"
        "  memory limit     : {} MB\n"
        "───────────────────────────────────────────\n\n",
        files.size(), GLOBAL_LIMIT.count(), LOCAL_LIMIT.count(), MEMORY_LIMIT_MB);

    if (memlim::set_memory_limit(MEMORY_LIMIT_MB)) std::cout << std::format("  [{}]  memory capped at {} MB\n\n", elapsed(), MEMORY_LIMIT_MB);

    // The global limit: if the entire run exceeds this, stop everything.
    timelim::set_time_limit(static_cast<unsigned>(GLOBAL_LIMIT.count()), [&] {
        std::cout << std::format(
            "\n  [{}]  *** global time limit reached — "
            "aborting remaining work ***\n\n",
            elapsed());
    });

    // ── process files ────────────────────────────────────────────────────────

    int ok = 0, timed_out = 0, aborted = 0;

    for (const auto& file : files) {
        if (CHECK_STOP()) {
            std::cout << std::format("  [{}]  skipping {} (global limit already reached)\n", elapsed(), file.name);
            ++aborted;
            continue;
        }

        bool success = process_file(file, LOCAL_LIMIT);

        if (success)
            ++ok;
        else if (CHECK_STOP())
            ++aborted;
        else
            ++timed_out;
    }

    timelim::cancel_time_limit();

    // ── summary ──────────────────────────────────────────────────────────────

    std::cout << std::format(
        "\n───────────────────────────────────────────\n"
        "  completed  : {}/{}\n"
        "  timed out  : {}  (exceeded {}s local limit)\n"
        "  aborted    : {}  (global limit reached)\n"
        "  total time : {}\n"
        "───────────────────────────────────────────\n",
        ok, files.size(), timed_out, LOCAL_LIMIT.count(), aborted, elapsed());
}