/**
 * @file benchmarks.cpp
 * @brief Benchmarks for ScopeGuard — construction, execution, and dismiss overhead.
 *
 * Build (C++20):
 *   g++ -std=c++20 -O2 benchmarks.cpp -o benchmarks && ./benchmarks
 *
 * Structure
 * ─────────
 *  1  EXIT strategy    — guard created and destroyed (callable runs) each iter
 *  2  SUCCESS strategy — guard created and destroyed normally (callable runs)
 *  3  FAIL strategy    — guard created and destroyed normally (callable skipped)
 *  4  Dismiss          — guard dismissed before destruction (no callable)
 *  5  Exception path   — guard runs during exception unwinding (FAIL strategy)
 *  6  Callable cost    — lambda capture size impact
 *  7  Multiple guards  — LIFO destruction ordering overhead
 */

#include <stdexcept>

#include "../benchmarking/bench_main.hpp"
#include "scope_guard.hxx"

using benchmark::DoNotOptimize;

// ─────────────────────────────────────────────────────────────────────────────
// Suite 1 — EXIT strategy
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("1 — EXIT strategy")

BENCH_CASE("on_scope_exit — empty lambda, normal exit") {
    int x = 0;
    for (auto _ : state) {
        {
            auto g = on_scope_exit([&x] { ++x; });
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE("ScopeGuard EXIT — direct construction, normal exit") {
    int x = 0;
    for (auto _ : state) {
        {
            ScopeGuard g([&x] { ++x; }, ScopeStrategy::EXIT);
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE("on_scope_exit — no-capture lambda (stateless)") {
    volatile int sink = 0;
    for (auto _ : state) {
        {
            auto g = on_scope_exit([] { /* no capture, no side effect */ });
        }
        DoNotOptimize(sink);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 2 — SUCCESS strategy
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("2 — SUCCESS strategy")

BENCH_CASE("on_scope_success — callable runs on normal exit") {
    int x = 0;
    for (auto _ : state) {
        {
            auto g = on_scope_success([&x] { ++x; });
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE("ScopeGuard SUCCESS — direct construction, normal exit") {
    int x = 0;
    for (auto _ : state) {
        {
            ScopeGuard g([&x] { ++x; }, ScopeStrategy::SUCCESS);
        }
        DoNotOptimize(x);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 3 — FAIL strategy (callable is skipped on normal exit)
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("3 — FAIL strategy")

BENCH_CASE("on_scope_fail — callable skipped on normal exit") {
    int x = 0;
    for (auto _ : state) {
        {
            auto g = on_scope_fail([&x] { ++x; });
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE("ScopeGuard FAIL — direct construction, normal exit") {
    int x = 0;
    for (auto _ : state) {
        {
            ScopeGuard g([&x] { ++x; }, ScopeStrategy::FAIL);
        }
        DoNotOptimize(x);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 4 — Dismiss
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("4 — Dismiss")

BENCH_CASE("on_scope_exit — dismissed before destruction") {
    int x = 0;
    for (auto _ : state) {
        {
            auto g = on_scope_exit([&x] { ++x; });
            g.dismiss();
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE("on_scope_success — dismissed before destruction") {
    int x = 0;
    for (auto _ : state) {
        {
            auto g = on_scope_success([&x] { ++x; });
            g.dismiss();
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE("on_scope_fail — dismissed before destruction") {
    int x = 0;
    for (auto _ : state) {
        {
            auto g = on_scope_fail([&x] { ++x; });
            g.dismiss();
        }
        DoNotOptimize(x);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 5 — Exception path (FAIL guard runs during unwinding)
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("5 — Exception path")

BENCH_CASE_N("on_scope_fail — callable runs during exception unwinding", 500) {
    int x = 0;
    for (auto _ : state) {
        try {
            auto g = on_scope_fail([&x] { ++x; });
            throw std::runtime_error("bench");
        } catch (...) {
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE_N("on_scope_success — callable skipped during exception unwinding", 500) {
    int x = 0;
    for (auto _ : state) {
        try {
            auto g = on_scope_success([&x] { ++x; });
            throw std::runtime_error("bench");
        } catch (...) {
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE_N("on_scope_exit — callable runs during exception unwinding", 500) {
    int x = 0;
    for (auto _ : state) {
        try {
            auto g = on_scope_exit([&x] { ++x; });
            throw std::runtime_error("bench");
        } catch (...) {
        }
        DoNotOptimize(x);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 6 — Callable capture size
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("6 — Callable capture size")

BENCH_CASE("EXIT guard — captures one int by ref") {
    int x = 0;
    for (auto _ : state) {
        {
            auto g = on_scope_exit([&x] { ++x; });
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE("EXIT guard — captures four ints by value") {
    int a = 0, b = 0, c = 0, d = 0;
    for (auto _ : state) {
        {
            auto g = on_scope_exit([a, b, c, d]() mutable {
                DoNotOptimize(a + b + c + d);
            });
        }
        DoNotOptimize(a);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 7 — Multiple guards (LIFO ordering)
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("7 — Multiple guards")

BENCH_CASE("two EXIT guards — LIFO destruction") {
    int x = 0;
    for (auto _ : state) {
        {
            auto g1 = on_scope_exit([&x] { ++x; });
            auto g2 = on_scope_exit([&x] { ++x; });
        }
        DoNotOptimize(x);
    }
}

BENCH_CASE("four EXIT guards — LIFO destruction") {
    int x = 0;
    for (auto _ : state) {
        {
            auto g1 = on_scope_exit([&x] { ++x; });
            auto g2 = on_scope_exit([&x] { ++x; });
            auto g3 = on_scope_exit([&x] { ++x; });
            auto g4 = on_scope_exit([&x] { ++x; });
        }
        DoNotOptimize(x);
    }
}
