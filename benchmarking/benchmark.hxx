#pragma once

/**
 * @file benchmark.hxx
 * @brief Simple benchmarking framework with statistical output and colored reporting.
 * @version 1.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// ANSI colors  (mirrors testing::color)
// ─────────────────────────────────────────────────────────────────────────────
namespace benchmark::color {

inline auto enabled() -> bool {
#ifdef _WIN32
    return false;
#else
    static bool val = (isatty(fileno(stdout)) != 0);
    return val;
#endif
}

inline auto green(std::string_view s) -> std::string { return enabled() ? "\033[32m" + std::string(s) + "\033[0m" : std::string(s); }
inline auto red(std::string_view s) -> std::string { return enabled() ? "\033[31m" + std::string(s) + "\033[0m" : std::string(s); }
inline auto yellow(std::string_view s) -> std::string { return enabled() ? "\033[33m" + std::string(s) + "\033[0m" : std::string(s); }
inline auto cyan(std::string_view s) -> std::string { return enabled() ? "\033[36m" + std::string(s) + "\033[0m" : std::string(s); }
inline auto bold(std::string_view s) -> std::string { return enabled() ? "\033[1m" + std::string(s) + "\033[0m" : std::string(s); }
inline auto dim(std::string_view s) -> std::string { return enabled() ? "\033[2m" + std::string(s) + "\033[0m" : std::string(s); }

}  // namespace benchmark::color

// ─────────────────────────────────────────────────────────────────────────────
// benchmark_result — plain data, computed after a run
// ─────────────────────────────────────────────────────────────────────────────
namespace benchmark {

struct benchmark_result {
    std::string suite;
    std::string name;
    std::size_t iterations{};
    double mean_ns{};
    double median_ns{};
    double stddev_ns{};
    double min_ns{};
    double max_ns{};
};

// ─────────────────────────────────────────────────────────────────────────────
// DoNotOptimize — prevents the compiler from optimizing away the benchmarked
// expression.  Uses the same pattern as Google Benchmark / nanobench.
// ─────────────────────────────────────────────────────────────────────────────

#if defined(__GNUC__) || defined(__clang__)
template <typename T>
inline void DoNotOptimize(T const& val) {
    asm volatile("" : : "r,m"(val) : "memory");
}
template <typename T>
inline void DoNotOptimize(T& val) {
    asm volatile("" : "+r,m"(val) : : "memory");
}
#else
// MSVC / unknown: volatile store is the best we can do portably
template <typename T>
inline void DoNotOptimize(T const& val) {
    const volatile T* ptr = &val;
    (void)ptr;
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// bench_state — passed into every benchmark function, mirrors the Google
// Benchmark `State` loop idiom but without range complexity.
//
//   BENCH_CASE("my bench") {
//       for (auto _ : state) {
//           DoNotOptimize(my_function());
//       }
//   }
// ─────────────────────────────────────────────────────────────────────────────

class bench_state {
   public:
    explicit bench_state(std::size_t iters) : iters_(iters) {}

    // ── range-for support ──────────────────────────────────────────────────

    struct iterator {
        bench_state* state;
        std::size_t index;

        auto operator!=(const iterator& other) const -> bool { return index != other.index; }
        auto operator++() -> iterator& {
            state->lap();
            ++index;
            return *this;
        }
        // dereference returns an unused int — the `auto _` binding just needs
        // something to bind to; we match Google Benchmark's convention.
        auto operator*() const -> int { return 0; }
    };

    auto begin() -> iterator {
        start_ = clock::now();
        return {this, 0};
    }

    auto end() -> iterator { return {this, iters_}; }

    // ── results ────────────────────────────────────────────────────────────

    [[nodiscard]] auto samples() const -> const std::vector<double>& { return samples_ns_; }
    [[nodiscard]] auto iterations() const -> std::size_t { return iters_; }

   private:
    using clock = std::chrono::steady_clock;

    std::size_t iters_;
    clock::time_point start_;
    std::vector<double> samples_ns_;

    void lap() {
        auto now = clock::now();
        double ns = std::chrono::duration<double, std::nano>(now - start_).count();
        samples_ns_.push_back(ns);
        start_ = clock::now();  // reset for next iteration
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Statistics helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

inline auto compute_result(std::string suite, std::string name, std::vector<double> samples) -> benchmark_result {
    std::sort(samples.begin(), samples.end());

    const std::size_t n = samples.size();
    const double mean = [&] {
        double sum = 0;
        for (double s : samples) sum += s;
        return sum / static_cast<double>(n);
    }();

    const double median = (n % 2 == 0) ? (samples[n / 2 - 1] + samples[n / 2]) / 2.0 : samples[n / 2];

    const double variance = [&] {
        double acc = 0;
        for (double s : samples) acc += (s - mean) * (s - mean);
        return acc / static_cast<double>(n);
    }();

    return benchmark_result{
        .suite = std::move(suite),
        .name = std::move(name),
        .iterations = n,
        .mean_ns = mean,
        .median_ns = median,
        .stddev_ns = std::sqrt(variance),
        .min_ns = samples.front(),
        .max_ns = samples.back(),
    };
}

// ── Time formatting ──────────────────────────────────────────────────────────
//
// Chooses the most human-readable unit: ns / µs / ms / s.

inline auto fmt_time(double ns) -> std::string {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (ns < 1'000.0) {
        oss << ns << " ns";
    } else if (ns < 1'000'000.0) {
        oss << ns / 1e3 << " µs";
    } else if (ns < 1'000'000'000.0) {
        oss << ns / 1e6 << " ms";
    } else {
        oss << ns / 1e9 << "  s";
    }
    return oss.str();
}

}  // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// bench_case — one registered benchmark
// ─────────────────────────────────────────────────────────────────────────────

struct bench_case {
    std::string suite;
    std::string name;
    std::function<void(bench_state&)> fn;
    std::size_t iterations;
    std::size_t warmup;
};

// ─────────────────────────────────────────────────────────────────────────────
// bench_registry — collects and runs all benchmarks
// ─────────────────────────────────────────────────────────────────────────────

class bench_registry {
   public:
    static auto instance() -> bench_registry& {
        static bench_registry reg;
        return reg;
    }

    auto register_bench(bench_case bcase) -> void { benches_.push_back(std::move(bcase)); }

    /**
     * @brief Run all registered benchmarks and print results.
     * @return 0 (reserved for future failure modes, e.g. regression checks).
     */
    auto run_all() -> int {
        print_header();

        std::vector<benchmark_result> results;
        std::string current_suite;

        for (auto& bcase : benches_) {
            if (bcase.suite != current_suite) {
                current_suite = bcase.suite;
                std::cout << "\n  " << color::bold(color::yellow("SUITE: " + current_suite)) << "\n";
            }

            // ── warmup ──────────────────────────────────────────────────────
            {
                bench_state warmup_state(bcase.warmup);
                for (auto _ : warmup_state) {
                    bcase.fn(warmup_state);
                    break;  // we only need the loop to tick once per warmup iter
                }
                // Simpler: just call fn directly for warmup iterations
            }
            for (std::size_t w = 0; w < bcase.warmup; ++w) {
                bench_state ws(1);
                bcase.fn(ws);
            }

            // ── measured run ─────────────────────────────────────────────────
            bench_state state(bcase.iterations);
            bcase.fn(state);

            auto res = detail::compute_result(bcase.suite, bcase.name, state.samples());
            print_result(res);
            results.push_back(res);
        }

        print_footer(results);
        return 0;
    }

   private:
    std::vector<bench_case> benches_;

    static void print_header() {
        std::cout << color::bold("\n+-------------------------------------+\n");
        std::cout << color::bold("|  cpp_utils benchmark runner          |\n");
        std::cout << color::bold("+-------------------------------------+\n");
    }

    static void print_result(const benchmark_result& r) {
        // Layout:  v  name    mean  median  stddev  [min … max]  N iters
        std::cout << "    " << color::green("v") << "  " << std::left << std::setw(80) << r.name << color::cyan(detail::fmt_time(r.mean_ns))
                  << color::dim("  med " + detail::fmt_time(r.median_ns)) << color::dim("  σ " + detail::fmt_time(r.stddev_ns))
                  << color::dim("  [" + detail::fmt_time(r.min_ns) + " … " + detail::fmt_time(r.max_ns) + "]")
                  << color::dim("  ×" + std::to_string(r.iterations)) << "\n";
    }

    static void print_footer(const std::vector<benchmark_result>& results) {
        constexpr int SEPARATOR_WIDTH = 42;
        std::cout << "\n" << std::string(SEPARATOR_WIDTH, '-') << "\n";
        std::cout << "  " << color::green(std::to_string(results.size()) + " benchmarks completed") << "\n";
        std::cout << std::string(SEPARATOR_WIDTH, '-') << "\n\n";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// auto_bench_registrar — RAII helper, registers at static-init time
// ─────────────────────────────────────────────────────────────────────────────

struct auto_bench_registrar {
    auto_bench_registrar(const char* suite, const char* name, void (*func)(bench_state&), std::size_t iters, std::size_t warmup) {
        bench_registry::instance().register_bench({
            .suite = suite,
            .name = name,
            .fn = func,
            .iterations = iters,
            .warmup = warmup,
        });
    }
};

}  // namespace benchmark

// ─────────────────────────────────────────────────────────────────────────────
// Macro helpers
// ─────────────────────────────────────────────────────────────────────────────
#define _BM_CAT2(a, b) a##b
#define _BM_CAT(a, b) _BM_CAT2(a, b)

// ─────────────────────────────────────────────────────────────────────────────
// BENCH_SUITE — sets the current suite name for following BENCH_CASEs
// ─────────────────────────────────────────────────────────────────────────────
namespace {
inline const char* _bm_current_suite_ = "<unset>";
}

#define BENCH_SUITE(name)                                        \
    static const char* _BM_CAT(_bm_suite_str_, __LINE__) = name; \
    static int _BM_CAT(_bm_suite_set_, __LINE__) = (_bm_current_suite_ = _BM_CAT(_bm_suite_str_, __LINE__), 0);

// ─────────────────────────────────────────────────────────────────────────────
// BENCH_CASE — default iterations / warmup
//
//   BENCH_CASE("my bench") { for (auto _ : state) { ... } }
//
// BENCH_CASE_N — explicit iteration count
//
//   BENCH_CASE_N("my bench", 10000) { for (auto _ : state) { ... } }
//
// BENCH_CASE_NW — explicit iteration count and warmup count
//
//   BENCH_CASE_NW("my bench", 10000, 100) { for (auto _ : state) { ... } }
// ─────────────────────────────────────────────────────────────────────────────

#define _BM_DEFINE(test_name, iters, warmup)                                                                                                 \
    static void _BM_CAT(_bm_fn_, __LINE__)(::benchmark::bench_state & state);                                                                \
    static ::benchmark::auto_bench_registrar _BM_CAT(_bm_reg_, __LINE__)(_bm_current_suite_, test_name, _BM_CAT(_bm_fn_, __LINE__), (iters), \
                                                                         (warmup));                                                          \
    static void _BM_CAT(_bm_fn_, __LINE__)(::benchmark::bench_state & state)

#define BENCH_CASE(test_name) _BM_DEFINE(test_name, 1000, 10)
#define BENCH_CASE_N(test_name, iters) _BM_DEFINE(test_name, iters, 10)
#define BENCH_CASE_NW(test_name, iters, w) _BM_DEFINE(test_name, iters, w)