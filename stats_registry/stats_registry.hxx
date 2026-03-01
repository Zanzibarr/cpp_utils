#pragma once

/**
 * @file stats_registry.hxx
 * @brief Statistics registry to complement the TimerRegistry class
 * @version 2.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "../timer/timer.hxx"  // TODO: ← adjust to wherever TimerRegistry lives

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace stats_detail {

// Welford accumulator for gauge values (doubles).
struct GaugeStats {
    std::size_t count = 0;
    double total = 0.0;
    double mean = 0.0;
    double M2 = 0.0;
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();

    void record(double val) {
        ++count;
        total += val;
        min = std::min(min, val);
        max = std::max(max, val);
        double delta = val - mean;
        mean += delta / static_cast<double>(count);
        M2 += delta * (val - mean);
    }

    void reset() { *this = GaugeStats{}; }

    [[nodiscard]] auto variance() const -> double { return count < 2 ? 0.0 : M2 / static_cast<double>(count); }
    [[nodiscard]] auto sample_variance() const -> double { return count < 2 ? 0.0 : M2 / static_cast<double>(count - 1); }
    [[nodiscard]] auto stddev() const -> double { return std::sqrt(variance()); }
    [[nodiscard]] auto sample_stddev() const -> double { return std::sqrt(sample_variance()); }

    // Parallel Welford merge.
    void merge(const GaugeStats& other) {
        if (other.count == 0) {
            return;
        }
        if (count == 0) {
            *this = other;
            return;
        }
        auto this_count = static_cast<double>(count);
        auto other_count = static_cast<double>(other.count);
        double combined = this_count + other_count;
        double delta = other.mean - mean;
        mean = mean + (delta * (other_count / combined));
        M2 += other.M2 + (delta * delta * (this_count * other_count / combined));
        count += other.count;
        total += other.total;
        min = std::min(min, other.min);
        max = std::max(max, other.max);
    }
};

// Fixed-width histogram (equal-width buckets).
struct Histogram {
    double low;
    double high;
    std::vector<std::size_t> buckets;
    std::size_t underflow = 0;
    std::size_t overflow = 0;
    std::size_t total = 0;

    explicit Histogram(double low, double high, std::size_t n_buckets) : low(low), high(high), buckets(n_buckets, 0) {}

    void record(double val) {
        ++total;
        if (val < low) {
            ++underflow;
            return;
        }
        if (val >= high) {
            ++overflow;
            return;
        }
        double width = (high - low) / static_cast<double>(buckets.size());
        auto idx = static_cast<std::size_t>((val - low) / width);
        if (idx >= buckets.size()) {
            idx = buckets.size() - 1;
        }
        ++buckets[idx];
    }

    void reset() {
        std::ranges::fill(buckets, 0);
        underflow = overflow = total = 0;
    }
};

}  // namespace stats_detail

// ─────────────────────────────────────────────────────────────────────────────
// CtStatID — forward declaration; defined after StatsRegistry
// ─────────────────────────────────────────────────────────────────────────────

template <std::size_t Hash>
struct CtStatID {
    static const std::size_t value;
};

// ─────────────────────────────────────────────────────────────────────────────
// StatsRegistry
// ─────────────────────────────────────────────────────────────────────────────

/**
 * StatsRegistry — extends TimerRegistry with:
 *   - Counters   : increment / decrement / get occurrence counts
 *   - Gauges     : record fractional values, accumulate Welford statistics
 *   - Histograms : bucket sampled values, print distributions with ASCII bars
 *
 * All names are compile-time ct_string template parameters.
 * Lookup is O(1) array indexing on the hot path.
 *
 * Thread safety
 * ─────────────
 * Counter hot path   : atomic, fully lock-free.
 * Gauge hot path     : per-entry mutex (fine-grained).
 * Histogram hot path : per-entry mutex (fine-grained).
 * All report / reset paths take stats_mutex_.
 */
class StatsRegistry : public TimerRegistry {
   public:
    StatsRegistry() = default;
    ~StatsRegistry() = default;
    StatsRegistry(const StatsRegistry&) = delete;
    StatsRegistry(StatsRegistry&&) = delete;
    auto operator=(const StatsRegistry&) -> StatsRegistry& = delete;
    auto operator=(StatsRegistry&&) -> StatsRegistry& = delete;

    // Maximum number of distinct compile-time stat names across the whole
    // program. Shared across counters, gauges, and histograms — each name
    // occupies one slot regardless of which primitive uses it.
    // Raise if you hit the abort() in assign_stat_id().
    static constexpr std::size_t MAX_CT_STATS = 128;

    // Default number of histogram buckets
    static constexpr std::size_t DEFAULT_HISTOGRAM_BUCKETS = 10;
    // Default width for histogram bar chart (in characters)
    static constexpr int DEFAULT_HISTOGRAM_BAR_WIDTH = 40;
    // Width for percentage column in histogram output
    static constexpr int HISTOGRAM_PERCENTAGE_WIDTH = 6;

    /**
     * Assigns a unique sequential index to a compile-time stat hash.
     * Called once per unique ct_string at static-init time via CtStatID<H>::value.
     * Thread-safe via a static atomic counter.
     */
    static auto assign_stat_id(std::size_t /*hash*/) -> std::size_t {
        static std::atomic<std::size_t> next{0};
        std::size_t idx = next.fetch_add(1, std::memory_order_relaxed);
        if (idx >= MAX_CT_STATS) {
            std::cerr << "StatsRegistry: MAX_CT_STATS (" << MAX_CT_STATS << ") exceeded. Increase the limit.\n";
            std::abort();
        }
        return idx;
    }

    // ═════════════════════════════════════════════════════════════════════
    // COUNTERS
    // Counts discrete occurrences. Backed by std::atomic<int64_t>, fully
    // lock-free on the hot path.
    // ═════════════════════════════════════════════════════════════════════

    /** Increments counter Name by delta (default 1). */
    template <ct_string Name>
    void counter_inc(int64_t delta = 1) noexcept {
        ct_counters_[ct_stat_id<Name>()].fetch_add(delta, std::memory_order_relaxed);
        ct_ensure_counter_name<Name>();
    }

    /** Decrements counter Name by delta (default 1). */
    template <ct_string Name>
    void counter_dec(int64_t delta = 1) noexcept {
        ct_counters_[ct_stat_id<Name>()].fetch_sub(delta, std::memory_order_relaxed);
        ct_ensure_counter_name<Name>();
    }

    /** Sets counter Name to an explicit value. */
    template <ct_string Name>
    void counter_set(int64_t value) noexcept {
        ct_counters_[ct_stat_id<Name>()].store(value, std::memory_order_relaxed);
    }

    /** Returns the current value of counter Name. */
    template <ct_string Name>
    [[nodiscard]] auto counter_get() const noexcept -> int64_t {
        return ct_counters_[ct_stat_id<Name>()].load(std::memory_order_relaxed);
    }

    /**
     * Returns a stable pointer to the underlying atomic<int64_t> for Name.
     * Cache this in tight loops to bypass even the array lookup:
     *
     *   auto* ctr = reg.counter_ref<"hits">();
     *   for (...) ctr->fetch_add(1, std::memory_order_relaxed);
     *
     * The pointer remains valid for the lifetime of the registry.
     */
    template <ct_string Name>
    [[nodiscard]] auto counter_ref() noexcept -> std::atomic<int64_t>* {
        ct_ensure_counter_name<Name>();
        return &ct_counters_[ct_stat_id<Name>()];
    }

    /** Resets counter Name to 0. */
    template <ct_string Name>
    void counter_reset() noexcept {
        ct_counters_[ct_stat_id<Name>()].store(0, std::memory_order_relaxed);
    }

    // ── Counter reporting ─────────────────────────────────────────────────

    struct CounterRow {
        std::string name;
        int64_t value;
    };

    auto get_counter_report() const -> std::vector<CounterRow> {
        std::lock_guard lock(stats_mutex_);
        std::vector<CounterRow> result;
        for (std::size_t i = 0; i < MAX_CT_STATS; ++i) {
            if (!ct_counter_active_[i]) {
                continue;
            }
            result.push_back({ct_stat_names_[i], ct_counters_[i].load(std::memory_order_relaxed)});
        }
        return result;
    }

    void print_counter_report() const {
        auto rows = get_counter_report();
        if (rows.empty()) {
            return;
        }
        constexpr std::size_t DEFAULT_NAME_WIDTH = 8;
        std::size_t name_width = DEFAULT_NAME_WIDTH;
        for (const auto& row : rows) {
            name_width = std::max(name_width, row.name.size() + 2);
        }
        constexpr int VAL_WIDTH = 14;
        std::cout << std::left << std::setw(static_cast<int>(name_width)) << "Counter" << std::right << std::setw(VAL_WIDTH) << "Value" << "\n"
                  << std::string(name_width + VAL_WIDTH, '-') << "\n";
        for (const auto& row : rows) {
            std::cout << std::left << std::setw(static_cast<int>(name_width)) << row.name << std::right << std::setw(VAL_WIDTH) << row.value << "\n";
        }
    }

    // ═════════════════════════════════════════════════════════════════════
    // GAUGES
    // Accumulates fractional (double) samples; reports Welford statistics
    // identical in style to TimerRegistry's stats report.
    // ═════════════════════════════════════════════════════════════════════

    /** Records a new sample for gauge Name. */
    template <ct_string Name>
    void gauge_record(double value) {
        auto& entry = ct_gauges_[ct_stat_id<Name>()];
        std::lock_guard lock(entry.mtx);
        entry.stats.record(value);
        ct_ensure_gauge_name<Name>();
    }

    /** Resets all samples for gauge Name. */
    template <ct_string Name>
    void gauge_reset() {
        auto& entry = ct_gauges_[ct_stat_id<Name>()];
        std::lock_guard lock(entry.mtx);
        entry.stats.reset();
    }

    // ── Gauge reporting ───────────────────────────────────────────────────

    struct GaugeRow {
        std::string name;
        std::size_t count;
        double total;
        double mean;
        double min;
        double max;
        double stddev;
        double sample_stddev;
    };

    auto get_gauge_report() const -> std::vector<GaugeRow> {
        std::lock_guard lock(stats_mutex_);
        std::vector<GaugeRow> result;
        for (std::size_t i = 0; i < MAX_CT_STATS; ++i) {
            if (!ct_gauge_active_[i]) {
                continue;
            }
            std::lock_guard glock(ct_gauges_[i].mtx);
            if (ct_gauges_[i].stats.count == 0) {
                continue;
            }
            const auto& stats = ct_gauges_[i].stats;
            result.push_back({ct_stat_names_[i], stats.count, stats.total, stats.mean, stats.min, stats.max, stats.stddev(), stats.sample_stddev()});
        }
        return result;
    }

    void print_gauge_report() const {
        const auto rows = get_gauge_report();
        if (rows.empty()) {
            return;
        }
        constexpr std::size_t DEFAULT_GAUGE_NAME_WIDTH = 8;
        std::size_t name_width = DEFAULT_GAUGE_NAME_WIDTH;
        for (const auto& row : rows) {
            name_width = std::max(name_width, row.name.size() + 2);
        }
        constexpr int TAG_WIDTH = 9;
        constexpr int VAL_WIDTH = 14;
        constexpr int NUM_VALUE_COLUMNS = 5;
        std::cout << std::left << std::setw(static_cast<int>(name_width)) << "Gauge" << std::right << std::setw(TAG_WIDTH) << "Samples"
                  << std::setw(VAL_WIDTH) << "Total" << std::setw(VAL_WIDTH) << "Mean" << std::setw(VAL_WIDTH) << "Min" << std::setw(VAL_WIDTH)
                  << "Max" << std::setw(VAL_WIDTH) << "Stddev" << "\n"
                  << std::string(name_width + TAG_WIDTH + (VAL_WIDTH * NUM_VALUE_COLUMNS), '-') << "\n";
        for (const auto& row : rows) {
            std::cout << std::left << std::setw(static_cast<int>(name_width)) << row.name << std::right << std::setw(TAG_WIDTH) << row.count
                      << std::fixed << std::setprecision(4) << std::setw(VAL_WIDTH) << row.total << std::setw(VAL_WIDTH) << row.mean
                      << std::setw(VAL_WIDTH) << row.min << std::setw(VAL_WIDTH) << row.max << std::setw(VAL_WIDTH) << row.stddev << "\n";
        }
    }

    // ═════════════════════════════════════════════════════════════════════
    // HISTOGRAMS
    // Buckets sampled values into equal-width bins over [low, high).
    // histogram_create<n>() must be called once before histogram_record<n>().
    // ═════════════════════════════════════════════════════════════════════

    /**
     * Creates a histogram for Name with n_buckets equal-width bins over [low, high).
     * Values outside this range are counted separately as underflow / overflow.
     * @throws std::runtime_error if called more than once for the same Name.
     */
    template <ct_string Name>
    void histogram_create(double low, double high, std::size_t n_buckets = DEFAULT_HISTOGRAM_BUCKETS) {
        if (low >= high) {
            throw std::invalid_argument("Histogram low must be < high.");
        }
        if (n_buckets == 0) {
            throw std::invalid_argument("n_buckets must be > 0.");
        }
        auto& entry = ct_hist_entries_[ct_stat_id<Name>()];
        std::lock_guard lock(entry.mtx);
        if (entry.created) {
            throw std::runtime_error("Histogram already created for this name.");
        }
        entry.hist = stats_detail::Histogram{low, high, n_buckets};
        entry.created = true;
        ct_ensure_hist_name<Name>();
    }

    /** Records value into histogram Name. */
    template <ct_string Name>
    void histogram_record(double val) {
        auto& entry = ct_hist_entries_[ct_stat_id<Name>()];
        std::lock_guard lock(entry.mtx);
        entry.hist.record(val);
    }

    /** Resets all bucket counts for histogram Name. */
    template <ct_string Name>
    void histogram_reset() {
        auto& entry = ct_hist_entries_[ct_stat_id<Name>()];
        std::lock_guard lock(entry.mtx);
        entry.hist.reset();
    }

    // ── Histogram reporting ───────────────────────────────────────────────

    struct HistogramBucket {
        double low;
        double high;
        std::size_t count;
        double pct;
    };

    struct HistogramRow {
        std::string name;
        std::size_t total{};
        std::size_t underflow{};
        std::size_t overflow{};
        std::vector<HistogramBucket> buckets;
    };

    auto get_histogram_report() const -> std::vector<HistogramRow> {
        std::lock_guard lock(stats_mutex_);
        std::vector<HistogramRow> result;
        for (std::size_t i = 0; i < MAX_CT_STATS; ++i) {
            if (!ct_hist_active_[i]) {
                continue;
            }
            std::lock_guard hlock(ct_hist_entries_[i].mtx);
            const auto& hist = ct_hist_entries_[i].hist;
            std::size_t in_range = hist.total - hist.underflow - hist.overflow;
            double width = (hist.high - hist.low) / static_cast<double>(hist.buckets.size());
            HistogramRow row;
            row.name = ct_stat_names_[i];
            row.total = hist.total;
            row.underflow = hist.underflow;
            row.overflow = hist.overflow;
            for (std::size_t buck_idx = 0; buck_idx < hist.buckets.size(); ++buck_idx) {
                double blo = hist.low + (static_cast<double>(buck_idx) * width);
                double bhi = blo + width;
                double pct = in_range > 0 ? 100.0 * static_cast<double>(hist.buckets[buck_idx]) / static_cast<double>(in_range) : 0.0;
                row.buckets.push_back({blo, bhi, hist.buckets[buck_idx], pct});
            }
            result.push_back(std::move(row));
        }
        return result;
    }

    /**
     * Prints histograms with ASCII bar charts.
     * @param bar_width Maximum number of '#' characters for 100%.
     */
    void print_histogram_report(int bar_width = DEFAULT_HISTOGRAM_BAR_WIDTH) const {
        const auto rows = get_histogram_report();
        if (rows.empty()) {
            return;
        }
        for (const auto& row : rows) {
            std::cout << "── Histogram: " << row.name << "  [total=" << row.total << "  underflow=" << row.underflow << "  overflow=" << row.overflow
                      << "] ──\n";
            std::size_t in_range = row.total - row.underflow - row.overflow;
            int label_width = 0;
            for (const auto& bucket : row.buckets) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2) << "[" << bucket.low << ", " << bucket.high << ")";
                label_width = std::max(label_width, static_cast<int>(oss.str().size()));
            }
            for (const auto& bucket : row.buckets) {
                std::ostringstream label;
                label << std::fixed << std::setprecision(2) << "[" << bucket.low << ", " << bucket.high << ")";
                int filled = in_range > 0 ? static_cast<int>(std::round(bucket.pct / 100.0 * bar_width)) : 0;
                std::cout << std::left << std::setw(label_width) << label.str() << " | " << std::string(static_cast<std::size_t>(filled), '#')
                          << std::string(static_cast<std::size_t>(std::max(0, bar_width - filled)), ' ') << " " << std::right
                          << std::setw(HISTOGRAM_PERCENTAGE_WIDTH) << std::fixed << std::setprecision(1) << bucket.pct << "%"
                          << "  (" << bucket.count << ")\n";
            }
            std::cout << "\n";
        }
    }

    // ═════════════════════════════════════════════════════════════════════
    // CONVENIENCE — print everything at once
    // ═════════════════════════════════════════════════════════════════════

    void print_all_reports() const {
        std::cout << "╔══ Timer Report ══════════════════════════════════╗\n";
        print_stats_report();
        std::cout << "\n╔══ Counter Report ════════════════════════════════╗\n";
        print_counter_report();
        std::cout << "\n╔══ Gauge Report ══════════════════════════════════╗\n";
        print_gauge_report();
        std::cout << "\n╔══ Histogram Report ══════════════════════════════╗\n";
        print_histogram_report();
    }

   private:
    // ── Compile-time ID helper ────────────────────────────────────────────

    template <ct_string Name>
    static auto ct_stat_id() -> std::size_t {
        return CtStatID<hash_name(Name)>::value;
    }

    // Each ensure_*_name writes the string name and marks the slot active on
    // first call. The active flag is checked first without the lock so the
    // common (already-registered) path is a single branch — no mutex overhead.

    template <ct_string Name>
    void ct_ensure_counter_name() {
        const std::size_t idx = ct_stat_id<Name>();
        if (!ct_counter_active_[idx]) {
            std::lock_guard lock(stats_mutex_);
            if (!ct_counter_active_[idx]) {
                if (ct_stat_names_[idx].empty()) {
                    ct_stat_names_[idx] = std::string(Name.view());
                }
                ct_counter_active_[idx] = true;
            }
        }
    }

    template <ct_string Name>
    void ct_ensure_gauge_name() {
        const std::size_t idx = ct_stat_id<Name>();
        if (!ct_gauge_active_[idx]) {
            std::lock_guard lock(stats_mutex_);
            if (!ct_gauge_active_[idx]) {
                if (ct_stat_names_[idx].empty()) {
                    ct_stat_names_[idx] = std::string(Name.view());
                }
                ct_gauge_active_[idx] = true;
            }
        }
    }

    template <ct_string Name>
    void ct_ensure_hist_name() {
        const std::size_t idx = ct_stat_id<Name>();
        if (!ct_hist_active_[idx]) {
            std::lock_guard lock(stats_mutex_);
            if (!ct_hist_active_[idx]) {
                if (ct_stat_names_[idx].empty()) {
                    ct_stat_names_[idx] = std::string(Name.view());
                }
                ct_hist_active_[idx] = true;
            }
        }
    }

    // ── Storage ───────────────────────────────────────────────────────────

    // Lock ordering: if both locks are ever needed in the same code path,
    // always acquire mutex_ (TimerRegistry) before stats_mutex_ (StatsRegistry).
    // Currently no code path takes both simultaneously.
    mutable std::mutex stats_mutex_;

    // ── Counters — one atomic per slot, lock-free on the hot path ─────────
    std::array<std::atomic<int64_t>, MAX_CT_STATS> ct_counters_{};
    std::array<bool, MAX_CT_STATS> ct_counter_active_{};

    // ── Gauges — per-entry mutex + Welford stats ──────────────────────────
    struct CtGaugeEntry {
        mutable std::mutex mtx;
        stats_detail::GaugeStats stats;
    };
    std::array<CtGaugeEntry, MAX_CT_STATS> ct_gauges_;
    std::array<bool, MAX_CT_STATS> ct_gauge_active_{};

    // ── Histograms — per-entry mutex + optional Histogram ─────────────────
    // histogram_create<n>() initialises hist and sets created = true.
    // The placeholder Histogram in the default constructor is never recorded
    // into; histogram_record<n>() requires created == true.
    struct CtHistEntry {
        mutable std::mutex mtx;
        stats_detail::Histogram hist{0.0, 1.0, 1};  // placeholder
        bool created = false;
    };
    std::array<CtHistEntry, MAX_CT_STATS> ct_hist_entries_;
    std::array<bool, MAX_CT_STATS> ct_hist_active_{};

    // Name table — written once per name on first use, read-only thereafter.
    std::array<std::string, MAX_CT_STATS> ct_stat_names_;
};

// ─────────────────────────────────────────────────────────────────────────────
// CtStatID — maps a compile-time hash to a unique sequential stat index.
//
// Instantiated once per unique ct_string across the whole program.
// The static value is assigned at program startup via StatsRegistry::assign_stat_id().
// ─────────────────────────────────────────────────────────────────────────────

template <std::size_t Hash>
const std::size_t CtStatID<Hash>::value = StatsRegistry::assign_stat_id(Hash);

// ─────────────────────────────────────────────────────────────────────────────
// make_scoped_counter — RAII inc/dec for a compile-time named counter
//
// Construction increments, destruction decrements. Both operations are
// lock-free atomic array lookups — no map, no string, no mutex.
//
// Usage:
//   auto c = make_scoped_counter<"active_requests">(reg);
// ─────────────────────────────────────────────────────────────────────────────

template <ct_string Name>
[[nodiscard]] auto make_scoped_counter(StatsRegistry& reg) {
    struct CtScopedCounter {
        std::atomic<int64_t>* ptr_;

        explicit CtScopedCounter(StatsRegistry& reg) : ptr_(reg.template counter_ref<Name>()) { ptr_->fetch_add(1, std::memory_order_relaxed); }

        ~CtScopedCounter() noexcept { ptr_->fetch_sub(1, std::memory_order_relaxed); }

        CtScopedCounter(const CtScopedCounter&) = delete;
        CtScopedCounter(CtScopedCounter&&) = delete;
        auto operator=(const CtScopedCounter&) -> CtScopedCounter& = delete;
        auto operator=(CtScopedCounter&&) -> CtScopedCounter& = delete;
    };
    return CtScopedCounter{reg};
}

// ─────────────────────────────────────────────────────────────────────────────
// Global convenience
// ─────────────────────────────────────────────────────────────────────────────

inline auto global_stats() -> StatsRegistry& {
    static StatsRegistry inst;
    return inst;
}

#define STATS global_stats()