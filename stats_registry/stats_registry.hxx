#pragma once

/**
 * @file stats_registry.hxx
 * @brief Statistics registry to complement the Timer Registry class
 * @version 1.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "../timer/timer.hxx"  // TODO: ← adjust to wherever TimerRegistry lives

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers (private to this file)
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
    double low;   // lower bound of first bucket
    double high;  // upper bound of last bucket
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
// StatsRegistry
// ─────────────────────────────────────────────────────────────────────────────

/**
 * StatsRegistry — extends TimerRegistry with:
 *   - Counters   : increment / decrement / get occurrence counts
 *   - Gauges     : record fractional values, accumulate Welford statistics
 *   - Histograms : bucket sampled values, print distributions with ASCII bars
 *
 * Thread safety
 * ─────────────
 * Counter hot path   : atomic, fully lock-free.
 * Gauge hot path     : per-key mutex (fine-grained, not the global one).
 * Histogram hot path : per-key mutex (same approach).
 * All report / reset / erase paths take the registry-level mutex.
 */
class StatsRegistry : public TimerRegistry {
   public:
    // ── Singleton ─────────────────────────────────────────────────────────

    static auto instance() -> StatsRegistry& {
        static StatsRegistry inst;
        return inst;
    }

    // Default number of histogram buckets
    static constexpr std::size_t DEFAULT_HISTOGRAM_BUCKETS = 10;
    // Default width for histogram bar chart (in characters)
    static constexpr int DEFAULT_HISTOGRAM_BAR_WIDTH = 40;
    // Width for percentage column in histogram output
    static constexpr int HISTOGRAM_PERCENTAGE_WIDTH = 6;

    // ═════════════════════════════════════════════════════════════════════
    // COUNTERS
    // Counts discrete occurrences. Backed by std::atomic<int64_t>, fully
    // lock-free on the hot path.
    // ═════════════════════════════════════════════════════════════════════

    /**
     * Increments counter @p name by @p delta (default 1).
     * The counter is created with value 0 on first access.
     */
    void counter_inc(const std::string& name, int64_t delta = 1) { get_or_create_counter(name).fetch_add(delta, std::memory_order_relaxed); }

    /**
     * Decrements counter @p name by @p delta (default 1).
     */
    void counter_dec(const std::string& name, int64_t delta = 1) { get_or_create_counter(name).fetch_sub(delta, std::memory_order_relaxed); }

    /** Sets counter @p name to an explicit value. */
    void counter_set(const std::string& name, int64_t value) { get_or_create_counter(name).store(value, std::memory_order_relaxed); }

    /** Returns the current value of counter @p name. */
    auto counter_get(const std::string& name) const -> int64_t {
        std::lock_guard lock(stats_mutex_);
        auto iter = counters_.find(name);
        if (iter == counters_.end()) {
            throw std::runtime_error("Counter '" + name + "' does not exist.");
        }
        return iter->second->load(std::memory_order_relaxed);
    }

    /** Resets counter @p name to 0. */
    void counter_reset(const std::string& name) {
        std::lock_guard lock(stats_mutex_);
        check_counter_exists(name);
        counters_[name]->store(0, std::memory_order_relaxed);
    }

    /** Removes counter @p name from the registry. */
    void counter_erase(const std::string& name) {
        std::lock_guard lock(stats_mutex_);
        check_counter_exists(name);
        counters_.erase(name);
        counter_names_.erase(std::remove(counter_names_.begin(), counter_names_.end(), name), counter_names_.end());
    }

    struct CounterRow {
        std::string name;
        int64_t value;
    };

    auto get_counter_report() const -> std::vector<CounterRow> {
        std::lock_guard lock(stats_mutex_);
        std::vector<CounterRow> result;
        result.reserve(counter_names_.size());
        for (const auto& name : counter_names_) {
            result.push_back({name, counters_.at(name)->load(std::memory_order_relaxed)});
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

    /**
     * Records a new sample for gauge @p name.
     * The gauge is created on first access.
     */
    void gauge_record(const std::string& name, double value) {
        auto& entry = get_or_create_gauge(name);
        std::lock_guard lock(entry.mtx);
        entry.stats.record(value);
    }

    /** Resets all samples for gauge @p name. */
    void gauge_reset(const std::string& name) {
        std::lock_guard lock(stats_mutex_);
        check_gauge_exists(name);
        std::lock_guard glock(gauges_[name]->mtx);
        gauges_[name]->stats.reset();
    }

    /** Removes gauge @p name from the registry. */
    void gauge_erase(const std::string& name) {
        std::lock_guard lock(stats_mutex_);
        check_gauge_exists(name);
        gauges_.erase(name);
        gauge_names_.erase(std::remove(gauge_names_.begin(), gauge_names_.end(), name), gauge_names_.end());
    }

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
        result.reserve(gauge_names_.size());
        for (const auto& name : gauge_names_) {
            const auto& entry = *gauges_.at(name);
            std::lock_guard glock(entry.mtx);
            if (entry.stats.count == 0) {
                continue;
            }
            const auto& stats = entry.stats;
            result.push_back({name, stats.count, stats.total, stats.mean, stats.min, stats.max, stats.stddev(), stats.sample_stddev()});
        }
        return result;
    }

    void print_gauge_report() const {
        const auto rows = get_gauge_report();
        if (rows.empty()) {
            return;
        }

        // Pick per-column units using the same helper as TimerRegistry.
        auto pmin = [](double val) -> double { return val > 0.0 ? val : std::numeric_limits<double>::max(); };
        double min_total = std::numeric_limits<double>::max();
        double min_mean = std::numeric_limits<double>::max();
        double min_min = std::numeric_limits<double>::max();
        double min_max = std::numeric_limits<double>::max();
        double min_stddev = std::numeric_limits<double>::max();
        for (const auto& row : rows) {
            min_total = std::min(min_total, pmin(row.total));
            min_mean = std::min(min_mean, pmin(row.mean));
            min_min = std::min(min_min, pmin(row.min));
            min_max = std::min(min_max, pmin(row.max));
            min_stddev = std::min(min_stddev, pmin(row.stddev));
        }
        // Gauges are unit-less; we just right-align with 2 dp, no unit rescaling.
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
    // Each histogram must be created explicitly with create_histogram()
    // before recording into iter.
    // ═════════════════════════════════════════════════════════════════════

    /**
     * Creates a histogram with @p n_buckets equal-width bins over [low, high).
     * Values outside this range are counted separately as underflow / overflow.
     * @throws std::runtime_error if a histogram with this name already exists.
     */
    void histogram_create(const std::string& name, double low, double high, std::size_t n_buckets = DEFAULT_HISTOGRAM_BUCKETS) {
        if (low >= high) {
            throw std::invalid_argument("Histogram low must be < high.");
        }
        if (n_buckets == 0) {
            throw std::invalid_argument("n_buckets must be > 0.");
        }
        std::lock_guard lock(stats_mutex_);
        if (histograms_.contains(name)) {
            throw std::runtime_error("Histogram '" + name + "' already exists.");
        }
        histograms_.emplace(name, std::make_unique<HistEntry>(low, high, n_buckets));
        histogram_names_.push_back(name);
    }

    /**
     * Records value @p v into histogram @p name.
     * @throws std::runtime_error if the histogram has not been created.
     */
    void histogram_record(const std::string& name, double val) {
        auto& entry = get_histogram_entry(name);
        std::lock_guard lock(entry.mtx);
        entry.hist.record(val);
    }

    /** Resets all bucket counts for histogram @p name. */
    void histogram_reset(const std::string& name) {
        std::lock_guard lock(stats_mutex_);
        check_histogram_exists(name);
        std::lock_guard hlock(histograms_[name]->mtx);
        histograms_[name]->hist.reset();
    }

    /** Removes histogram @p name from the registry. */
    void histogram_erase(const std::string& name) {
        std::lock_guard lock(stats_mutex_);
        check_histogram_exists(name);
        histograms_.erase(name);
        histogram_names_.erase(std::remove(histogram_names_.begin(), histogram_names_.end(), name), histogram_names_.end());
    }

    struct HistogramBucket {
        double low;
        double high;
        std::size_t count;
        double pct;  // percentage of in-range samples
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
        for (const auto& name : histogram_names_) {
            const auto& entry = *histograms_.at(name);
            std::lock_guard hlock(entry.mtx);
            const auto& hist = entry.hist;
            std::size_t in_range = hist.total - hist.underflow - hist.overflow;
            double width = (hist.high - hist.low) / static_cast<double>(hist.buckets.size());
            HistogramRow row;
            row.name = name;
            row.total = hist.total;
            row.underflow = hist.underflow;
            row.overflow = hist.overflow;
            for (std::size_t i = 0; i < hist.buckets.size(); ++i) {
                double blo = hist.low + (static_cast<double>(i) * width);
                double bhi = blo + width;
                double pct = in_range > 0 ? 100.0 * static_cast<double>(hist.buckets[i]) / static_cast<double>(in_range) : 0.0;
                row.buckets.push_back({blo, bhi, hist.buckets[i], pct});
            }
            result.push_back(std::move(row));
        }
        return result;
    }

    /**
     * Prints histograms with ASCII bar charts.
     * @param bar_width   Maximum number of '█' characters for 100 %.
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
            // Compute label width from bucket bounds.
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
                std::string bar(static_cast<std::size_t>(filled), '\xe2');
                // Fallback to '#' for non-UTF8 terminals.
                std::string bar_ascii(static_cast<std::size_t>(filled), '#');
                std::cout << std::left << std::setw(label_width) << label.str() << " | " << bar_ascii
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

    /**
     * Prints timers (merged), counters, gauges, and histograms in sequence.
     */
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
    StatsRegistry() = default;
    ~StatsRegistry() = default;
    StatsRegistry(const StatsRegistry&) = delete;
    StatsRegistry& operator=(const StatsRegistry&) = delete;

    // ── Counter internals ─────────────────────────────────────────────────

    std::atomic<int64_t>& get_or_create_counter(const std::string& name) {
        {
            // Fast read path without lock (double-checked with a shared_ptr
            // would be nicer, but a plain mutex is fine here since creation
            // is rare and the store is atomic).
            std::lock_guard lock(stats_mutex_);
            auto iter = counters_.find(name);
            if (iter != counters_.end()) {
                return *iter->second;
            }
            counter_names_.push_back(name);
            return *counters_.emplace(name, std::make_unique<std::atomic<int64_t>>(0)).first->second;
        }
    }

    void check_counter_exists(const std::string& name) const {
        if (!counters_.contains(name)) {
            throw std::runtime_error("Counter '" + name + "' does not exist.");
        }
    }

    // ── Gauge internals ───────────────────────────────────────────────────

    struct GaugeEntry {
        mutable std::mutex mtx;
        stats_detail::GaugeStats stats;
    };

    auto get_or_create_gauge(const std::string& name) -> GaugeEntry& {
        std::lock_guard lock(stats_mutex_);
        auto iter = gauges_.find(name);
        if (iter != gauges_.end()) {
            return *iter->second;
        }
        gauge_names_.push_back(name);
        return *gauges_.emplace(name, std::make_unique<GaugeEntry>()).first->second;
    }

    void check_gauge_exists(const std::string& name) const {
        if (!gauges_.contains(name)) {
            throw std::runtime_error("Gauge '" + name + "' does not exist.");
        }
    }

    // ── Histogram internals ───────────────────────────────────────────────

    struct HistEntry {
        mutable std::mutex mtx;
        stats_detail::Histogram hist;
        explicit HistEntry(double low, double high, std::size_t n) : hist(low, high, n) {}
    };

    auto get_histogram_entry(const std::string& name) -> HistEntry& {
        std::lock_guard lock(stats_mutex_);
        auto iter = histograms_.find(name);
        if (iter == histograms_.end()) {
            throw std::runtime_error("Histogram '" + name +
                                     "' does not exist. "
                                     "Call histogram_create() first.");
        }
        return *iter->second;
    }

    void check_histogram_exists(const std::string& name) const {
        if (!histograms_.contains(name)) {
            throw std::runtime_error("Histogram '" + name + "' does not exist.");
        }
    }

    // ── Shared state ──────────────────────────────────────────────────────

    mutable std::mutex stats_mutex_;  // guards all three collections below

    // Counters
    std::unordered_map<std::string, std::unique_ptr<std::atomic<int64_t>>> counters_;
    std::vector<std::string> counter_names_;

    // Gauges
    std::unordered_map<std::string, std::unique_ptr<GaugeEntry>> gauges_;
    std::vector<std::string> gauge_names_;

    // Histograms
    std::unordered_map<std::string, std::unique_ptr<HistEntry>> histograms_;
    std::vector<std::string> histogram_names_;
};

// ─────────────────────────────────────────────────────────────────────────────
// ScopedCounter — RAII increment/decrement (e.g. active-request gauge)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Increments a StatsRegistry counter on construction, decrements on destruction.
 * Useful for tracking "currently active" resources (connections, threads, etc.).
 */
class ScopedCounter {
   public:
    explicit ScopedCounter(std::string name, StatsRegistry& registry = StatsRegistry::instance()) : name_(std::move(name)), registry_(registry) {
        registry_.counter_inc(name_);
    }
    ~ScopedCounter() noexcept { registry_.counter_dec(name_); }

    ScopedCounter(const ScopedCounter&) = delete;
    ScopedCounter& operator=(const ScopedCounter&) = delete;

   private:
    std::string name_;
    StatsRegistry& registry_;
};