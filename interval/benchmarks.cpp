/**
 * @file benchmarks.cpp
 * @brief Benchmarks for Interval<T>.
 *
 * Build (C++20):
 *   g++ -std=c++20 -O2 benchmarks.cpp -o benchmarks && ./benchmarks
 *
 * Structure
 * ─────────
 *  1  Construction   — Interval(a,b), make_empty, make_universe
 *  2  Queries        — contains(v), contains(Interval), overlaps, is_empty, length, center
 *  3  Operations     — clamp, merge, intersect, expand, translate
 *  4  Normalization  — normalize and denormalize (float)
 *  5  Operators      — operator==, operator!=
 *  6  Mixed          — realistic sequence of reads and queries
 */

#include <cmath>
#include <limits>

#include "../benchmarking/bench_main.hpp"
#include "interval.hxx"

using benchmark::DoNotOptimize;

// ─────────────────────────────────────────────────────────────────────────────
// Suite 1 — Construction
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("1 — Construction")

BENCH_CASE("Interval<int>(a, b) construction") {
    for (auto _ : state) {
        DoNotOptimize(Interval<int>(0, 100));
    }
}

BENCH_CASE("Interval<double>(a, b) construction") {
    for (auto _ : state) {
        DoNotOptimize(Interval<double>(0.0, 1.0));
    }
}

BENCH_CASE("Interval<int>::make_empty()") {
    for (auto _ : state) {
        DoNotOptimize(Interval<int>::make_empty());
    }
}

BENCH_CASE("Interval<int>::make_universe()") {
    for (auto _ : state) {
        DoNotOptimize(Interval<int>::make_universe());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 2 — Queries
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("2 — Queries")

BENCH_CASE("contains(int value) — value inside") {
    Interval<int> i(0, 100);
    for (auto _ : state) {
        DoNotOptimize(i.contains(42));
    }
}

BENCH_CASE("contains(int value) — value outside") {
    Interval<int> i(0, 100);
    for (auto _ : state) {
        DoNotOptimize(i.contains(200));
    }
}

BENCH_CASE("contains(Interval) — fully inside") {
    Interval<int> outer(0, 100);
    Interval<int> inner(10, 90);
    for (auto _ : state) {
        DoNotOptimize(outer.contains(inner));
    }
}

BENCH_CASE("overlaps() — overlapping intervals") {
    Interval<int> a(0, 50);
    Interval<int> b(25, 75);
    for (auto _ : state) {
        DoNotOptimize(a.overlaps(b));
    }
}

BENCH_CASE("overlaps() — non-overlapping intervals") {
    Interval<int> a(0, 10);
    Interval<int> b(20, 30);
    for (auto _ : state) {
        DoNotOptimize(a.overlaps(b));
    }
}

BENCH_CASE("is_empty()") {
    Interval<int> i(0, 100);
    for (auto _ : state) {
        DoNotOptimize(i.is_empty());
    }
}

BENCH_CASE("length()") {
    Interval<int> i(0, 100);
    for (auto _ : state) {
        DoNotOptimize(i.length());
    }
}

BENCH_CASE("center()") {
    Interval<int> i(0, 100);
    for (auto _ : state) {
        DoNotOptimize(i.center());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 3 — Operations
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("3 — Operations")

BENCH_CASE("clamp() — value inside range") {
    Interval<int> i(0, 100);
    for (auto _ : state) {
        DoNotOptimize(i.clamp(42));
    }
}

BENCH_CASE("clamp() — value below range") {
    Interval<int> i(0, 100);
    for (auto _ : state) {
        DoNotOptimize(i.clamp(-5));
    }
}

BENCH_CASE("clamp() — value above range") {
    Interval<int> i(0, 100);
    for (auto _ : state) {
        DoNotOptimize(i.clamp(200));
    }
}

BENCH_CASE("merge() — overlapping intervals") {
    Interval<int> a(0, 50);
    Interval<int> b(25, 75);
    for (auto _ : state) {
        DoNotOptimize(a.merge(b));
    }
}

BENCH_CASE("merge() — non-overlapping intervals") {
    Interval<int> a(0, 10);
    Interval<int> b(20, 30);
    for (auto _ : state) {
        DoNotOptimize(a.merge(b));
    }
}

BENCH_CASE("intersect() — overlapping intervals") {
    Interval<int> a(0, 50);
    Interval<int> b(25, 75);
    for (auto _ : state) {
        DoNotOptimize(a.intersect(b));
    }
}

BENCH_CASE("intersect() — non-overlapping intervals (returns nullopt)") {
    Interval<int> a(0, 10);
    Interval<int> b(20, 30);
    for (auto _ : state) {
        DoNotOptimize(a.intersect(b));
    }
}

BENCH_CASE("expand() by fixed amount") {
    Interval<int> i(10, 90);
    for (auto _ : state) {
        DoNotOptimize(i.expand(5));
    }
}

BENCH_CASE("translate() by positive offset") {
    Interval<int> i(0, 100);
    for (auto _ : state) {
        DoNotOptimize(i.translate(10));
    }
}

BENCH_CASE("translate() by negative offset") {
    Interval<int> i(100, 200);
    for (auto _ : state) {
        DoNotOptimize(i.translate(-50));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 4 — Normalization (float only)
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("4 — Normalization (float)")

BENCH_CASE("normalize() float value") {
    Interval<float> i(0.f, 1.f);
    for (auto _ : state) {
        DoNotOptimize(i.normalize(0.5f));
    }
}

BENCH_CASE("denormalize() float value") {
    Interval<float> i(0.f, 100.f);
    for (auto _ : state) {
        DoNotOptimize(i.denormalize(0.75f));
    }
}

BENCH_CASE("normalize then denormalize round-trip") {
    Interval<double> i(2.0, 8.0);
    double val = 5.0;
    for (auto _ : state) {
        DoNotOptimize(i.denormalize(i.normalize(val)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 5 — Operators
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("5 — Operators")

BENCH_CASE("operator== (equal intervals)") {
    Interval<int> a(0, 100);
    Interval<int> b(0, 100);
    for (auto _ : state) {
        DoNotOptimize(a == b);
    }
}

BENCH_CASE("operator!= (different intervals)") {
    Interval<int> a(0, 100);
    Interval<int> b(0, 99);
    for (auto _ : state) {
        DoNotOptimize(a != b);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 6 — Mixed (realistic usage)
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("6 — Mixed / realistic")

BENCH_CASE("construct, query, and clamp in one iteration") {
    for (auto _ : state) {
        Interval<int> i(0, 100);
        bool inside = i.contains(42);
        int clamped = i.clamp(120);
        DoNotOptimize(inside);
        DoNotOptimize(clamped);
    }
}

BENCH_CASE("merge three intervals and query result") {
    Interval<int> a(0, 30);
    Interval<int> b(20, 60);
    Interval<int> c(50, 100);
    for (auto _ : state) {
        auto merged = a.merge(b).merge(c);
        DoNotOptimize(merged.length());
    }
}

BENCH_CASE("intersect chain — float interval") {
    Interval<double> base(-10.0, 10.0);
    Interval<double> mask(-5.0, 7.0);
    for (auto _ : state) {
        auto res = base.intersect(mask);
        DoNotOptimize(res.has_value());
    }
}
