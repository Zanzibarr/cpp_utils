/**
 * benchmarks.cpp
 * ───────────────────────────────────────────────────────────────────────────
 * Measures the per-call overhead of ParameterRegistry::get() versus
 * accessing the same values stored in a plain struct.
 *
 * What is measured
 * ────────────────
 * Each benchmark reads one or more values in a tight loop and accumulates
 * the result into `sum`, which is printed at the end to prevent the compiler
 * from eliminating the reads as dead code.
 *
 * ParameterRegistry::get() cost breakdown (per call):
 *   1. Array index — slots_[idx] where idx is a compile-time constant
 *   2. std::get<T> on the variant — one branch on the discriminant
 *   3. Return const T& — same as returning a struct field reference
 *
 * No heap allocation, no string comparison, no virtual dispatch.
 *
 * Compile (C++20):
 *   g++ -std=c++20 -O2 benchmarks.cpp -o benchmarks && ./benchmarks
 */

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "parameters.hxx"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

using clk = std::chrono::steady_clock;

static auto elapsed_ns(clk::time_point t0) -> double { return std::chrono::duration<double, std::nano>(clk::now() - t0).count(); }

static constexpr long ITERS = 10'000'000;

static void print_header() {
    std::cout << "\n"
              << std::left << std::setw(42) << "Benchmark" << std::right << std::setw(11) << "Total" << std::setw(11) << "Per call"
              << "\n"
              << std::string(64, '-') << "\n";
}

static void print_row(const char* label, double total_ns) {
    std::cout << std::left << std::setw(42) << label << std::right << std::fixed << std::setprecision(1) << std::setw(8) << (total_ns / 1e6) << " ms"
              << std::setw(8) << std::setprecision(2) << (total_ns / static_cast<double>(ITERS)) << " ns\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// The equivalent plain struct — used as the zero-overhead baseline
// ─────────────────────────────────────────────────────────────────────────────

struct HardcodedParams {
    double learning_rate;
    int64_t batch_size;
    bool shuffle;
    std::string optimizer;
};

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << std::string(64, '-') << "\n"
              << "  ParameterRegistry — get<n> vs struct field access\n"
              << "  Iterations per benchmark : " << ITERS << "\n"
              << std::string(64, '-') << "\n";

    // ── Setup ─────────────────────────────────────────────────────────────

    ParameterRegistry reg;
    reg.set<"learning_rate">(0.001);
    reg.set<"batch_size">(32);
    reg.set<"shuffle">(true);
    reg.set<"optimizer">("adam");

    HardcodedParams s{0.001, 32, true, "adam"};

    // `sum` is accumulated and printed to prevent dead-code elimination.
    double sum = 0.0;

    // ── Warm-up (stabilise CPU frequency) ────────────────────────────────

    for (long i = 0; i < ITERS / 10; ++i) {
        sum += s.learning_rate;
        sum += reg.get<"learning_rate", double>();
    }
    sum = 0.0;

    // ─────────────────────────────────────────────────────────────────────
    // BM1 — double
    // ─────────────────────────────────────────────────────────────────────

    print_header();
    std::cout << "── double ──\n";
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) sum += s.learning_rate;
        print_row("struct field", elapsed_ns(t0));
    }
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) sum += reg.get<"learning_rate", double>();
        print_row("ParameterRegistry::get<double>", elapsed_ns(t0));
    }

    // ─────────────────────────────────────────────────────────────────────
    // BM2 — int64_t
    // ─────────────────────────────────────────────────────────────────────

    std::cout << "\n── int64_t ──\n";
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) sum += static_cast<double>(s.batch_size);
        print_row("struct field", elapsed_ns(t0));
    }
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) sum += static_cast<double>(reg.get<"batch_size", int64_t>());
        print_row("ParameterRegistry::get<int64_t>", elapsed_ns(t0));
    }

    // ─────────────────────────────────────────────────────────────────────
    // BM3 — bool
    // ─────────────────────────────────────────────────────────────────────

    std::cout << "\n── bool ──\n";
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) sum += s.shuffle ? 1.0 : 0.0;
        print_row("struct field", elapsed_ns(t0));
    }
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) sum += reg.get<"shuffle", bool>() ? 1.0 : 0.0;
        print_row("ParameterRegistry::get<bool>", elapsed_ns(t0));
    }

    // ─────────────────────────────────────────────────────────────────────
    // BM4 — std::string (.size() forces actual read of the string object)
    // ─────────────────────────────────────────────────────────────────────

    std::cout << "\n── std::string (read .size()) ──\n";
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) sum += static_cast<double>(s.optimizer.size());
        print_row("struct field", elapsed_ns(t0));
    }
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) sum += static_cast<double>(reg.get<"optimizer", std::string>().size());
        print_row("ParameterRegistry::get<string>", elapsed_ns(t0));
    }

    // ─────────────────────────────────────────────────────────────────────
    // BM5 — Realistic: 3 mixed reads per iteration
    // ─────────────────────────────────────────────────────────────────────

    std::cout << "\n── mixed: 3 reads per iteration ──\n";
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) {
            sum += s.learning_rate;
            sum += static_cast<double>(s.batch_size);
            sum += s.shuffle ? 1.0 : 0.0;
        }
        print_row("struct (3 fields)", elapsed_ns(t0));
    }
    {
        auto t0 = clk::now();
        for (long i = 0; i < ITERS; ++i) {
            sum += reg.get<"learning_rate", double>();
            sum += static_cast<double>(reg.get<"batch_size", int64_t>());
            sum += reg.get<"shuffle", bool>() ? 1.0 : 0.0;
        }
        print_row("ParameterRegistry (3 gets)", elapsed_ns(t0));
    }

    std::cout << "\n(sum=" << sum << ")\n";  // prevent dead-code elimination
    return 0;
}
