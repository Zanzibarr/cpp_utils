#pragma once

/**
 * @file benchmark.hxx
 * @brief Micro-benchmark framework with per-iteration timing and statistical output.
 * @version 1.0.0
 *
 * @details
 * Benchmarks are registered with `BENCH_SUITE` / `BENCH_CASE` macros at
 * static-init time and run via `bench_registry::instance().run_all()`.
 *
 * Each benchmark receives a `bench_state&` whose range-for loop records
 * one nanosecond sample per iteration (same idiom as Google Benchmark):
 * @code
 *   BENCH_CASE("my bench") {
 *       for (auto _ : state) { DoNotOptimize(my_function()); }
 *   }
 * @endcode
 *
 * After all iterations complete, `detail::compute_result()` derives mean,
 * median, stddev, min, and max from the raw sample vector.  Results are
 * printed in a colour-coded table with auto-scaling units (ns / µs / ms / s).
 *
 * `DoNotOptimize()` uses inline asm constraints on GCC/Clang to prevent the
 * compiler from eliminating the benchmarked expression.
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * Repository: https://github.com/Zanzibarr/cpp_utils
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

#include "../utilities/ansi_colors.hxx"  // TODO: Update to the actual path

namespace utilz {
// ─────────────────────────────────────────────────────────────────────────────
// ANSI colors — thin namespace aliases over the shared ansi:: helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace benchmark::color {

inline auto enabled() -> bool { return ansi::enabled(); }
inline auto green(std::string_view str) -> std::string { return ansi::green(str); }
inline auto red(std::string_view str) -> std::string { return ansi::red(str); }
inline auto yellow(std::string_view str) -> std::string { return ansi::yellow(str); }
inline auto cyan(std::string_view str) -> std::string { return ansi::cyan(str); }
inline auto bold(std::string_view str) -> std::string { return ansi::bold(str); }
inline auto dim(std::string_view str) -> std::string { return ansi::dim(str); }

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

#if defined(__clang__)
template <typename T>
inline void DoNotOptimize(T const& val) {
    asm volatile("" : : "r,m"(val) : "memory");
}
template <typename T>
inline void DoNotOptimize(T& val) {
    asm volatile("" : "+r,m"(val) : : "memory");
}
#elif defined(__GNUC__)
template <typename T>
inline void DoNotOptimize(T const& val) {
    asm volatile("" : : "rm"(val) : "memory");
}
template <typename T>
inline void DoNotOptimize(T& val) {
    asm volatile("" : "+rm"(val) : : "memory");
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
        return {.state = this, .index = 0};
    }

    auto end() -> iterator { return {.state = this, .index = iters_}; }

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
        double nanos = std::chrono::duration<double, std::nano>(now - start_).count();
        samples_ns_.push_back(nanos);
        start_ = now;  // reset for next iteration
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Statistics helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

inline auto compute_result(std::string suite, std::string name, std::vector<double> samples) -> benchmark_result {
    if (samples.empty()) {
        return {.suite = suite, .name = name, .iterations = 0, .mean_ns = 0, .median_ns = 0, .stddev_ns = 0, .min_ns = 0, .max_ns = 0};
    }

    std::ranges::sort(samples);

    const std::size_t smpl_size = samples.size();
    const double mean = [&] {
        double sum = 0;
        for (double samp : samples) {
            sum += samp;
        }
        return sum / static_cast<double>(smpl_size);
    }();

    const double median = (smpl_size % 2 == 0) ? (samples[(smpl_size / 2) - 1] + samples[smpl_size / 2]) / 2.0 : samples[smpl_size / 2];

    const double variance = [&] {
        double acc = 0;
        for (double samp : samples) {
            acc += (samp - mean) * (samp - mean);
        }
        return acc / static_cast<double>(smpl_size);
    }();

    return benchmark_result{
        .suite = std::move(suite),
        .name = std::move(name),
        .iterations = smpl_size,
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

inline auto fmt_time(double nanoseconds) -> std::string {
    constexpr double NS_PER_US = 1e3;
    constexpr double NS_PER_MS = 1e6;
    constexpr double NS_PER_S = 1e9;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (nanoseconds < NS_PER_US) {
        oss << nanoseconds << " ns";
    } else if (nanoseconds < NS_PER_MS) {
        oss << nanoseconds / NS_PER_US << " µs";
    } else if (nanoseconds < NS_PER_S) {
        oss << nanoseconds / NS_PER_MS << " ms";
    } else {
        oss << nanoseconds / NS_PER_S << "  s";
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
            for (std::size_t warmup_idx = 0; warmup_idx < bcase.warmup; ++warmup_idx) {
                bench_state warmup_state(1);
                bcase.fn(warmup_state);
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

    static void print_result(const benchmark_result& result) {
        constexpr int NAME_COLUMN_WIDTH = 80;
        // Layout:  v  name    mean  median  stddev  [min … max]  N iters
        std::cout << "    " << color::green("v") << "  " << std::left << std::setw(NAME_COLUMN_WIDTH) << result.name
                  << color::cyan(detail::fmt_time(result.mean_ns)) << color::dim("  med " + detail::fmt_time(result.median_ns))
                  << color::dim("  σ " + detail::fmt_time(result.stddev_ns))
                  << color::dim("  [" + detail::fmt_time(result.min_ns) + " … " + detail::fmt_time(result.max_ns) + "]")
                  << color::dim("  ×" + std::to_string(result.iterations)) << "\n";
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

}  // namespace utilz

// ─────────────────────────────────────────────────────────────────────────────
// Macro helpers
// ─────────────────────────────────────────────────────────────────────────────
#define _BM_CAT2(a, b) a##b
#define _BM_CAT(a, b) _BM_CAT2(a, b)

// ─────────────────────────────────────────────────────────────────────────────
// BENCH_SUITE — sets the current suite name for following BENCH_CASEs
// ─────────────────────────────────────────────────────────────────────────────
namespace {
[[maybe_unused]] inline const char* _bm_current_suite_ = "<unset>";
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

#define _BM_DEFINE(test_name, iters, warmup)                                                                                                        \
    static void _BM_CAT(_bm_fn_, __LINE__)(::utilz::benchmark::bench_state & state);                                                                \
    static ::utilz::benchmark::auto_bench_registrar _BM_CAT(_bm_reg_, __LINE__)(_bm_current_suite_, test_name, _BM_CAT(_bm_fn_, __LINE__), (iters), \
                                                                                (warmup));                                                          \
    static void _BM_CAT(_bm_fn_, __LINE__)(::utilz::benchmark::bench_state & state)

#define BENCH_CASE(test_name) _BM_DEFINE(test_name, 1000, 10)
#define BENCH_CASE_N(test_name, iters) _BM_DEFINE(test_name, iters, 10)
#define BENCH_CASE_NW(test_name, iters, w) _BM_DEFINE(test_name, iters, w)