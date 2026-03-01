/**
 * @file bench_binary_set.cpp
 * @brief Extensive benchmarks for BinarySet and BSSearcher.
 *
 * Every operation is compared against the most natural STL alternative.
 * The goal is to answer: "what do I pay / gain by using BinarySet instead
 * of a standard container?"
 *
 * Build (C++20):
 *   g++ -std=c++20 -O2 -march=native -o bench_bs bench_binary_set.cpp
 *
 * Capacity choices
 * ────────────────
 *   SMALL  =  64   — fits in exactly one 64-bit chunk; best case for BinarySet
 *   MEDIUM = 256   — four chunks; typical real-world set
 *   LARGE  = 1024  — sixteen chunks; stresses word-at-a-time operations
 *
 * Density choices
 * ───────────────
 *   SPARSE — ~10% fill  (few elements relative to capacity)
 *   DENSE  — ~90% fill  (most elements present)
 *
 * Suites
 * ──────
 *   0   Baselines               — raw STL cost for orientation
 *   1   Construction            — empty, full, copy
 *   2   Single-element ops      — add / remove / contains
 *   3   Bulk conversion         — sparse() / begin()..end()
 *   4   Set algebra             — |, &, -, ^, ! (non-mutating)
 *   5   In-place algebra        — |=, &=, -=, ^=
 *   6   Subset / superset tests — subset_of, superset_of, intersects
 *   7   Iteration               — range-for over all elements
 *   8   BSSearcher              — add, remove, find_subsets vs brute-force
 */

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <random>
#include <set>
#include <unordered_set>
#include <vector>

#include "../benchmarking/bench_main.hpp"
#include "binary_set.hxx"

using benchmark::DoNotOptimize;

// ─────────────────────────────────────────────────────────────────────────────
// Capacity constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr unsigned int CAP_SMALL = 64;
static constexpr unsigned int CAP_MEDIUM = 256;
static constexpr unsigned int CAP_LARGE = 1024;

// ─────────────────────────────────────────────────────────────────────────────
// Reproducible random helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Returns a seeded Mersenne Twister — same seed every time for reproducibility.
inline auto make_rng() -> std::mt19937 { return std::mt19937{12345}; }

// Fills a BinarySet to approximately `density` fraction of its capacity.
inline void fill_random(BinarySet& bs, double density, std::mt19937& rng) {
    std::bernoulli_distribution coin(density);
    for (unsigned int i = 0; i < bs.capacity(); ++i) {
        if (coin(rng)) bs.add(i);
    }
}

// Returns a BinarySet pre-filled at the given density.
inline auto make_bs(unsigned int cap, double density, std::mt19937& rng) -> BinarySet {
    BinarySet bs(cap);
    fill_random(bs, density, rng);
    return bs;
}

// Returns an std::set<unsigned int> with the same elements as `bs`.
inline auto to_std_set(const BinarySet& bs) -> std::set<unsigned int> {
    std::set<unsigned int> s;
    for (unsigned int e : bs) s.insert(e);
    return s;
}

// Returns an std::unordered_set<unsigned int> with the same elements.
inline auto to_unordered_set(const BinarySet& bs) -> std::unordered_set<unsigned int> {
    std::unordered_set<unsigned int> s;
    for (unsigned int e : bs) s.insert(e);
    return s;
}

// Returns a std::vector<bool> of length cap representing the same membership.
inline auto to_vector_bool(const BinarySet& bs) -> std::vector<bool> {
    std::vector<bool> v(bs.capacity(), false);
    for (unsigned int e : bs) v[e] = true;
    return v;
}

// Pre-built fixture sets (initialised once, reused across all benchmarks).
struct Fixtures {
    // Small
    BinarySet bs_small_sparse;
    BinarySet bs_small_dense;
    BinarySet bs_small_sparse2;  // second set for binary ops
    BinarySet bs_small_dense2;
    std::set<unsigned int> ss_small_sparse, ss_small_dense;
    std::unordered_set<unsigned int> us_small_sparse, us_small_dense;
    std::vector<bool> vb_small_sparse, vb_small_dense;

    // Medium
    BinarySet bs_med_sparse;
    BinarySet bs_med_dense;
    BinarySet bs_med_sparse2;
    BinarySet bs_med_dense2;
    std::set<unsigned int> ss_med_sparse, ss_med_dense;
    std::unordered_set<unsigned int> us_med_sparse, us_med_dense;
    std::vector<bool> vb_med_sparse, vb_med_dense;

    // Large
    BinarySet bs_large_sparse;
    BinarySet bs_large_dense;
    BinarySet bs_large_sparse2;
    BinarySet bs_large_dense2;
    std::set<unsigned int> ss_large_sparse, ss_large_dense;
    std::unordered_set<unsigned int> us_large_sparse, us_large_dense;
    std::vector<bool> vb_large_sparse, vb_large_dense;

    Fixtures()
        : bs_small_sparse(CAP_SMALL),
          bs_small_dense(CAP_SMALL),
          bs_small_sparse2(CAP_SMALL),
          bs_small_dense2(CAP_SMALL),
          bs_med_sparse(CAP_MEDIUM),
          bs_med_dense(CAP_MEDIUM),
          bs_med_sparse2(CAP_MEDIUM),
          bs_med_dense2(CAP_MEDIUM),
          bs_large_sparse(CAP_LARGE),
          bs_large_dense(CAP_LARGE),
          bs_large_sparse2(CAP_LARGE),
          bs_large_dense2(CAP_LARGE) {
        auto rng = make_rng();
        fill_random(bs_small_sparse, 0.10, rng);
        fill_random(bs_small_dense, 0.90, rng);
        fill_random(bs_small_sparse2, 0.10, rng);
        fill_random(bs_small_dense2, 0.90, rng);
        fill_random(bs_med_sparse, 0.10, rng);
        fill_random(bs_med_dense, 0.90, rng);
        fill_random(bs_med_sparse2, 0.10, rng);
        fill_random(bs_med_dense2, 0.90, rng);
        fill_random(bs_large_sparse, 0.10, rng);
        fill_random(bs_large_dense, 0.90, rng);
        fill_random(bs_large_sparse2, 0.10, rng);
        fill_random(bs_large_dense2, 0.90, rng);

        ss_small_sparse = to_std_set(bs_small_sparse);
        ss_small_dense = to_std_set(bs_small_dense);
        us_small_sparse = to_unordered_set(bs_small_sparse);
        us_small_dense = to_unordered_set(bs_small_dense);
        vb_small_sparse = to_vector_bool(bs_small_sparse);
        vb_small_dense = to_vector_bool(bs_small_dense);

        ss_med_sparse = to_std_set(bs_med_sparse);
        ss_med_dense = to_std_set(bs_med_dense);
        us_med_sparse = to_unordered_set(bs_med_sparse);
        us_med_dense = to_unordered_set(bs_med_dense);
        vb_med_sparse = to_vector_bool(bs_med_sparse);
        vb_med_dense = to_vector_bool(bs_med_dense);

        ss_large_sparse = to_std_set(bs_large_sparse);
        ss_large_dense = to_std_set(bs_large_dense);
        us_large_sparse = to_unordered_set(bs_large_sparse);
        us_large_dense = to_unordered_set(bs_large_dense);
        vb_large_sparse = to_vector_bool(bs_large_sparse);
        vb_large_dense = to_vector_bool(bs_large_dense);
    }
};

inline auto fx() -> const Fixtures& {
    static Fixtures f;
    return f;
}

// ── std::set binary ops (intersection, union, difference, symmetric diff) ────

inline auto std_set_intersect(const std::set<unsigned int>& a, const std::set<unsigned int>& b) -> std::set<unsigned int> {
    std::set<unsigned int> result;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::inserter(result, result.end()));
    return result;
}

inline auto std_set_union(const std::set<unsigned int>& a, const std::set<unsigned int>& b) -> std::set<unsigned int> {
    std::set<unsigned int> result;
    std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::inserter(result, result.end()));
    return result;
}

inline auto std_set_diff(const std::set<unsigned int>& a, const std::set<unsigned int>& b) -> std::set<unsigned int> {
    std::set<unsigned int> result;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::inserter(result, result.end()));
    return result;
}

inline auto std_set_sym_diff(const std::set<unsigned int>& a, const std::set<unsigned int>& b) -> std::set<unsigned int> {
    std::set<unsigned int> result;
    std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::inserter(result, result.end()));
    return result;
}

// ── vector<bool> binary ops ───────────────────────────────────────────────────

inline auto vb_and(const std::vector<bool>& a, const std::vector<bool>& b) -> std::vector<bool> {
    std::vector<bool> r(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) r[i] = a[i] & b[i];
    return r;
}

inline auto vb_or(const std::vector<bool>& a, const std::vector<bool>& b) -> std::vector<bool> {
    std::vector<bool> r(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) r[i] = a[i] | b[i];
    return r;
}

inline auto vb_diff(const std::vector<bool>& a, const std::vector<bool>& b) -> std::vector<bool> {
    std::vector<bool> r(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) r[i] = a[i] & !b[i];
    return r;
}

inline auto vb_xor(const std::vector<bool>& a, const std::vector<bool>& b) -> std::vector<bool> {
    std::vector<bool> r(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) r[i] = a[i] ^ b[i];
    return r;
}

// ── BSSearcher brute-force baseline ──────────────────────────────────────────

struct BruteForceSearcher {
    std::vector<std::pair<unsigned int, BinarySet>> entries;

    void add(unsigned int id, const BinarySet& bs) { entries.emplace_back(id, bs); }

    auto find_subsets(const BinarySet& query) const -> std::vector<unsigned int> {
        std::vector<unsigned int> result;
        for (const auto& [id, bs] : entries) {
            if (bs.subset_of(query)) result.push_back(id);
        }
        return result;
    }
};

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 0 — Baselines
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("0 · Baselines — raw STL for orientation")

BENCH_CASE_N("std::set<uint> insert single element (small, sparse)", 200'000) {
    for (auto _ : state) {
        std::set<unsigned int> s = fx().ss_small_sparse;
        s.insert(33);
        DoNotOptimize(s.size());
    }
}

BENCH_CASE_N("std::unordered_set<uint> insert single element (small, sparse)", 200'000) {
    for (auto _ : state) {
        std::unordered_set<unsigned int> s = fx().us_small_sparse;
        s.insert(33);
        DoNotOptimize(s.size());
    }
}

BENCH_CASE_N("std::vector<bool> set single bit (small)", 200'000) {
    for (auto _ : state) {
        std::vector<bool> v = fx().vb_small_sparse;
        v[33] = true;
        DoNotOptimize(v.size());
    }
}

BENCH_CASE_N("BinarySet::add single element (small, sparse)", 200'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_small_sparse;
        bs.add(33);
        DoNotOptimize(bs.size());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 1 — Construction
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("1a · Construction — small (cap=64)")

BENCH_CASE_N("BinarySet(64) empty", 500'000) {
    for (auto _ : state) {
        BinarySet bs(CAP_SMALL);
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet(64, fill_all=true)", 500'000) {
    for (auto _ : state) {
        BinarySet bs(CAP_SMALL, true);
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet(64) copy", 500'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_small_sparse;
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("std::set<uint> default construct", 500'000) {
    for (auto _ : state) {
        std::set<unsigned int> s;
        DoNotOptimize(s.size());
    }
}

BENCH_CASE_N("std::unordered_set<uint> default construct", 500'000) {
    for (auto _ : state) {
        std::unordered_set<unsigned int> s;
        DoNotOptimize(s.size());
    }
}

BENCH_CASE_N("std::vector<bool>(64, false)", 500'000) {
    for (auto _ : state) {
        std::vector<bool> v(CAP_SMALL, false);
        DoNotOptimize(v.size());
    }
}

BENCH_CASE_N("std::vector<bool>(64, true)", 500'000) {
    for (auto _ : state) {
        std::vector<bool> v(CAP_SMALL, true);
        DoNotOptimize(v.size());
    }
}

BENCH_SUITE("1b · Construction — large (cap=1024)")

BENCH_CASE_N("BinarySet(1024) empty", 200'000) {
    for (auto _ : state) {
        BinarySet bs(CAP_LARGE);
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet(1024, fill_all=true)", 200'000) {
    for (auto _ : state) {
        BinarySet bs(CAP_LARGE, true);
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet(1024) copy — sparse", 200'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_large_sparse;
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet(1024) copy — dense", 200'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_large_dense;
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("std::set<uint> copy — sparse (1024 cap ~102 elements)", 200'000) {
    for (auto _ : state) {
        std::set<unsigned int> s = fx().ss_large_sparse;
        DoNotOptimize(s.size());
    }
}

BENCH_CASE_N("std::vector<bool>(1024, false)", 200'000) {
    for (auto _ : state) {
        std::vector<bool> v(CAP_LARGE, false);
        DoNotOptimize(v.size());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 2 — Single-element operations
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("2a · Single-element ops — small (cap=64)")

BENCH_CASE_N("BinarySet::add — element present (no-op)", 500'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_small_dense;
        bool r = bs.add(0);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::add — element absent (actual insert)", 500'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_small_sparse;
        bool r = bs.add(63);  // may or may not be present; always copies first
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::remove — element present", 500'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_small_dense;
        bool r = bs.remove(0);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::contains — hit (dense set)", 500'000) {
    const auto& bs = fx().bs_small_dense;
    for (auto _ : state) {
        bool r = bs.contains(0);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::contains — miss (sparse set, check high element)", 500'000) {
    const auto& bs = fx().bs_small_sparse;
    for (auto _ : state) {
        bool r = bs.contains(63);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("std::set<uint>::count — hit", 500'000) {
    const auto& s = fx().ss_small_dense;
    for (auto _ : state) {
        auto r = s.count(0);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("std::unordered_set<uint>::count — hit", 500'000) {
    const auto& s = fx().us_small_dense;
    for (auto _ : state) {
        auto r = s.count(0);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("std::vector<bool> operator[] read", 500'000) {
    const auto& v = fx().vb_small_dense;
    for (auto _ : state) {
        bool r = v[0];
        DoNotOptimize(r);
    }
}

BENCH_SUITE("2b · Single-element ops — large (cap=1024)")

BENCH_CASE_N("BinarySet::contains — hit (dense, large)", 500'000) {
    const auto& bs = fx().bs_large_dense;
    for (auto _ : state) {
        bool r = bs.contains(512);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("std::set<uint>::count — hit (large)", 500'000) {
    const auto& s = fx().ss_large_dense;
    for (auto _ : state) {
        auto r = s.count(512);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("std::unordered_set<uint>::count — hit (large)", 500'000) {
    const auto& s = fx().us_large_dense;
    for (auto _ : state) {
        auto r = s.count(512);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("std::vector<bool> operator[] read (large)", 500'000) {
    const auto& v = fx().vb_large_dense;
    for (auto _ : state) {
        bool r = v[512];
        DoNotOptimize(r);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 3 — Bulk conversion / materialisation
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("3a · Bulk conversion — medium (cap=256)")

BENCH_CASE_N("BinarySet::sparse() — sparse set (~26 elements)", 100'000) {
    for (auto _ : state) {
        auto v = fx().bs_med_sparse.sparse();
        DoNotOptimize(v.size());
    }
}

BENCH_CASE_N("BinarySet::sparse() — dense set (~230 elements)", 100'000) {
    for (auto _ : state) {
        auto v = fx().bs_med_dense.sparse();
        DoNotOptimize(v.size());
    }
}

BENCH_CASE_N("std::set copy into vector (equivalent to sparse)", 100'000) {
    for (auto _ : state) {
        std::vector<unsigned int> v(fx().ss_med_sparse.begin(), fx().ss_med_sparse.end());
        DoNotOptimize(v.size());
    }
}

BENCH_CASE_N("std::unordered_set copy into vector", 100'000) {
    for (auto _ : state) {
        std::vector<unsigned int> v(fx().us_med_sparse.begin(), fx().us_med_sparse.end());
        DoNotOptimize(v.size());
    }
}

BENCH_SUITE("3b · Bulk conversion — large (cap=1024)")

BENCH_CASE_N("BinarySet::sparse() — sparse (large, ~103 elements)", 50'000) {
    for (auto _ : state) {
        auto v = fx().bs_large_sparse.sparse();
        DoNotOptimize(v.size());
    }
}

BENCH_CASE_N("BinarySet::sparse() — dense (large, ~922 elements)", 50'000) {
    for (auto _ : state) {
        auto v = fx().bs_large_dense.sparse();
        DoNotOptimize(v.size());
    }
}

BENCH_CASE_N("std::set copy into vector — sparse (large)", 50'000) {
    for (auto _ : state) {
        std::vector<unsigned int> v(fx().ss_large_sparse.begin(), fx().ss_large_sparse.end());
        DoNotOptimize(v.size());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 4 — Set algebra, non-mutating
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("4a · Set algebra (non-mutating) — small (cap=64)")

BENCH_CASE_N("BinarySet operator& (intersection) — sparse", 500'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_small_sparse & fx().bs_small_sparse2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("std::set_intersection — sparse", 500'000) {
    for (auto _ : state) {
        auto r = std_set_intersect(fx().ss_small_sparse, fx().ss_small_sparse);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("vector<bool> element-wise AND — sparse", 500'000) {
    for (auto _ : state) {
        auto r = vb_and(fx().vb_small_sparse, fx().vb_small_sparse);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator| (union) — sparse", 500'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_small_sparse | fx().bs_small_sparse2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("std::set_union — sparse", 500'000) {
    for (auto _ : state) {
        auto r = std_set_union(fx().ss_small_sparse, fx().ss_small_sparse);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator- (difference) — sparse", 500'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_small_sparse - fx().bs_small_sparse2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("std::set_difference — sparse", 500'000) {
    for (auto _ : state) {
        auto r = std_set_diff(fx().ss_small_sparse, fx().ss_small_sparse);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator^ (symmetric diff) — sparse", 500'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_small_sparse ^ fx().bs_small_sparse2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("std::set_symmetric_difference — sparse", 500'000) {
    for (auto _ : state) {
        auto r = std_set_sym_diff(fx().ss_small_sparse, fx().ss_small_sparse);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator! (complement) — sparse", 500'000) {
    for (auto _ : state) {
        BinarySet r = !fx().bs_small_sparse;
        DoNotOptimize(r.size());
    }
}

BENCH_SUITE("4b · Set algebra (non-mutating) — medium (cap=256)")

BENCH_CASE_N("BinarySet operator& — sparse", 200'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_med_sparse & fx().bs_med_sparse2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("std::set_intersection — sparse", 200'000) {
    for (auto _ : state) {
        auto r = std_set_intersect(fx().ss_med_sparse, fx().ss_med_sparse);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("vector<bool> element-wise AND — sparse", 200'000) {
    for (auto _ : state) {
        auto r = vb_and(fx().vb_med_sparse, fx().vb_med_sparse);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator& — dense", 200'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_med_dense & fx().bs_med_dense2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("std::set_intersection — dense", 200'000) {
    for (auto _ : state) {
        auto r = std_set_intersect(fx().ss_med_dense, fx().ss_med_dense);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator| — dense", 200'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_med_dense | fx().bs_med_dense2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("std::set_union — dense", 200'000) {
    for (auto _ : state) {
        auto r = std_set_union(fx().ss_med_dense, fx().ss_med_dense);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator- — dense", 200'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_med_dense - fx().bs_med_dense2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator! — dense", 200'000) {
    for (auto _ : state) {
        BinarySet r = !fx().bs_med_dense;
        DoNotOptimize(r.size());
    }
}

BENCH_SUITE("4c · Set algebra (non-mutating) — large (cap=1024)")

BENCH_CASE_N("BinarySet operator& — sparse", 100'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_large_sparse & fx().bs_large_sparse2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("std::set_intersection — sparse", 100'000) {
    for (auto _ : state) {
        auto r = std_set_intersect(fx().ss_large_sparse, fx().ss_large_sparse);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("vector<bool> element-wise AND — sparse (large)", 100'000) {
    for (auto _ : state) {
        auto r = vb_and(fx().vb_large_sparse, fx().vb_large_sparse);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator& — dense", 100'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_large_dense & fx().bs_large_dense2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("std::set_intersection — dense", 100'000) {
    for (auto _ : state) {
        auto r = std_set_intersect(fx().ss_large_dense, fx().ss_large_dense);
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator| — dense", 100'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_large_dense | fx().bs_large_dense2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator^ — dense", 100'000) {
    for (auto _ : state) {
        BinarySet r = fx().bs_large_dense ^ fx().bs_large_dense2;
        DoNotOptimize(r.size());
    }
}

BENCH_CASE_N("BinarySet operator! — dense", 100'000) {
    for (auto _ : state) {
        BinarySet r = !fx().bs_large_dense;
        DoNotOptimize(r.size());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 5 — In-place algebra
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("5 · In-place algebra — medium (cap=256)")

BENCH_CASE_N("BinarySet operator&= — sparse", 200'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_med_sparse;
        bs &= fx().bs_med_sparse2;
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet operator|= — sparse", 200'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_med_sparse;
        bs |= fx().bs_med_sparse2;
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet operator-= — sparse", 200'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_med_sparse;
        bs -= fx().bs_med_sparse2;
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet operator^= — sparse", 200'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_med_sparse;
        bs ^= fx().bs_med_sparse2;
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet operator&= — dense", 200'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_med_dense;
        bs &= fx().bs_med_dense2;
        DoNotOptimize(bs.size());
    }
}

BENCH_CASE_N("BinarySet operator|= — dense", 200'000) {
    for (auto _ : state) {
        BinarySet bs = fx().bs_med_dense;
        bs |= fx().bs_med_dense2;
        DoNotOptimize(bs.size());
    }
}

// vector<bool> in-place (no operator overloads — manual loop)
BENCH_CASE_N("vector<bool> element-wise &= loop — sparse", 200'000) {
    for (auto _ : state) {
        std::vector<bool> v = fx().vb_med_sparse;
        const auto& w = fx().vb_med_sparse;
        for (std::size_t i = 0; i < v.size(); ++i) v[i] = v[i] & w[i];
        DoNotOptimize(v.size());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 6 — Subset / superset / intersects
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("6a · Subset / superset / intersects — medium (cap=256)")

BENCH_CASE_N("BinarySet::subset_of — sparse ⊆ dense (true)", 500'000) {
    for (auto _ : state) {
        bool r = fx().bs_med_sparse.subset_of(fx().bs_med_dense);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::subset_of — sparse ⊆ sparse2 (likely false)", 500'000) {
    for (auto _ : state) {
        bool r = fx().bs_med_sparse.subset_of(fx().bs_med_sparse2);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::superset_of — dense ⊇ sparse (true)", 500'000) {
    for (auto _ : state) {
        bool r = fx().bs_med_dense.superset_of(fx().bs_med_sparse);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::intersects — sparse & sparse2", 500'000) {
    for (auto _ : state) {
        bool r = fx().bs_med_sparse.intersects(fx().bs_med_sparse2);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::intersects — sparse & dense (always true)", 500'000) {
    for (auto _ : state) {
        bool r = fx().bs_med_sparse.intersects(fx().bs_med_dense);
        DoNotOptimize(r);
    }
}

// std::set equivalent: std::includes for subset test
BENCH_CASE_N("std::includes — sparse ⊆ dense (std::set equivalent)", 500'000) {
    for (auto _ : state) {
        bool r = std::includes(fx().ss_med_dense.begin(), fx().ss_med_dense.end(), fx().ss_med_sparse.begin(), fx().ss_med_sparse.end());
        DoNotOptimize(r);
    }
}

// vector<bool> subset: element-wise implication check
BENCH_CASE_N("vector<bool> subset check (manual loop) — sparse ⊆ dense", 500'000) {
    for (auto _ : state) {
        const auto& a = fx().vb_med_sparse;
        const auto& b = fx().vb_med_dense;
        bool r = true;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] && !b[i]) {
                r = false;
                break;
            }
        }
        DoNotOptimize(r);
    }
}

BENCH_SUITE("6b · Subset / superset / intersects — large (cap=1024)")

BENCH_CASE_N("BinarySet::subset_of — sparse ⊆ dense (large, true)", 200'000) {
    for (auto _ : state) {
        bool r = fx().bs_large_sparse.subset_of(fx().bs_large_dense);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("std::includes — sparse ⊆ dense (large, std::set)", 200'000) {
    for (auto _ : state) {
        bool r = std::includes(fx().ss_large_dense.begin(), fx().ss_large_dense.end(), fx().ss_large_sparse.begin(), fx().ss_large_sparse.end());
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::intersects — sparse & dense (large)", 200'000) {
    for (auto _ : state) {
        bool r = fx().bs_large_sparse.intersects(fx().bs_large_dense);
        DoNotOptimize(r);
    }
}

BENCH_CASE_N("BinarySet::intersects — sparse & sparse2 (large, early exit likely)", 200'000) {
    for (auto _ : state) {
        bool r = fx().bs_large_sparse.intersects(fx().bs_large_sparse2);
        DoNotOptimize(r);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SUITE 7 — Iteration
// ═════════════════════════════════════════════════════════════════════════════

BENCH_SUITE("7a · Iteration — small (cap=64)")

BENCH_CASE_N("BinarySet range-for — sparse (~6 elements)", 500'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().bs_small_sparse) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("BinarySet range-for — dense (~57 elements)", 500'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().bs_small_dense) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("std::set range-for — sparse", 500'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().ss_small_sparse) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("std::unordered_set range-for — sparse", 500'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().us_small_sparse) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("vector<bool> scan-all loop — sparse", 500'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        const auto& v = fx().vb_small_sparse;
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i]) sum += static_cast<unsigned int>(i);
        DoNotOptimize(sum);
    }
}

BENCH_SUITE("7b · Iteration — medium (cap=256)")

BENCH_CASE_N("BinarySet range-for — sparse (~26 elements)", 200'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().bs_med_sparse) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("BinarySet range-for — dense (~230 elements)", 200'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().bs_med_dense) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("std::set range-for — sparse (medium)", 200'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().ss_med_sparse) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("std::set range-for — dense (medium)", 200'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().ss_med_dense) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("vector<bool> scan-all loop — dense (medium)", 200'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        const auto& v = fx().vb_med_dense;
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i]) sum += static_cast<unsigned int>(i);
        DoNotOptimize(sum);
    }
}

BENCH_SUITE("7c · Iteration — large (cap=1024)")

BENCH_CASE_N("BinarySet range-for — sparse (~103 elements)", 100'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().bs_large_sparse) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("BinarySet range-for — dense (~922 elements)", 100'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().bs_large_dense) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("std::set range-for — sparse (large)", 100'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().ss_large_sparse) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("std::set range-for — dense (large)", 100'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        for (unsigned int e : fx().ss_large_dense) sum += e;
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("vector<bool> scan-all loop — sparse (large)", 100'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        const auto& v = fx().vb_large_sparse;
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i]) sum += static_cast<unsigned int>(i);
        DoNotOptimize(sum);
    }
}

BENCH_CASE_N("vector<bool> scan-all loop — dense (large)", 100'000) {
    for (auto _ : state) {
        unsigned int sum = 0;
        const auto& v = fx().vb_large_dense;
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i]) sum += static_cast<unsigned int>(i);
        DoNotOptimize(sum);
    }
}
