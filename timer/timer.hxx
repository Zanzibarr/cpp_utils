#pragma once

/**
 * @file timer.hxx
 * @brief Timer class and related utilities (ScopedTimer and TimerRegistry)
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
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace timer_detail {

using clock = std::chrono::steady_clock;
using time_point = clock::time_point;

// Constraint shared by all public Duration template parameters.
template <typename D>
concept ValidDuration = std::is_same_v<D, std::chrono::nanoseconds> || std::is_same_v<D, std::chrono::microseconds> ||
                        std::is_same_v<D, std::chrono::milliseconds> || std::is_same_v<D, std::chrono::seconds>;

// Convert a raw nanosecond double to the requested Duration.
template <ValidDuration D>
auto ns_to(double nanos) -> double {
    using Target = std::chrono::duration<double, typename D::period>;
    return std::chrono::duration_cast<Target>(std::chrono::duration<double, std::nano>(nanos)).count();
}

// Convert a steady_clock duration to nanoseconds as double.
inline auto to_ns(clock::duration dur) -> double { return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count()); }

template <ValidDuration D>
constexpr auto unit_name() -> const char* {
    if constexpr (std::is_same_v<D, std::chrono::nanoseconds>) {
        return "ns";
    }
    if constexpr (std::is_same_v<D, std::chrono::microseconds>) {
        return "us";
    }
    if constexpr (std::is_same_v<D, std::chrono::milliseconds>) {
        return "ms";
    }
    return "s";
}

// ── Print helpers shared by both print_stats_report variants ─────────────────

struct UnitSpec {
    double divisor;
    const char* suffix;
};

inline auto pick_unit(double min_ns) -> UnitSpec {
    constexpr double NANOS_PER_SECOND = 1e9;
    constexpr double NANOS_PER_MILLI = 1e6;
    constexpr double NANOS_PER_MICRO = 1e3;

    if (min_ns >= NANOS_PER_SECOND) {
        return {.divisor = NANOS_PER_SECOND, .suffix = "s"};
    }
    if (min_ns >= NANOS_PER_MILLI) {
        return {.divisor = NANOS_PER_MILLI, .suffix = "ms"};
    }
    if (min_ns >= NANOS_PER_MICRO) {
        return {.divisor = NANOS_PER_MICRO, .suffix = "us"};
    }
    return {.divisor = 1.0, .suffix = "ns"};
}

template <typename Row>
auto col_min(const std::vector<Row>& rows, std::function<double(const Row&)> func) -> double {
    double max = std::numeric_limits<double>::max();
    for (const auto& row : rows) {
        double val = func(row);
        if (val > 0.0) {
            max = std::min(max, val);
        }
    }
    return (max == std::numeric_limits<double>::max()) ? 1.0 : max;
}

}  // namespace timer_detail

// ─────────────────────────────────────────────────────────────────────────────
// Timer
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A simple, single-threaded wall-clock timer.
 * All internal state is stored in nanoseconds as doubles.
 *
 * Thread safety: not thread-safe. Each instance must be owned by one thread.
 * Use TimerRegistry for multi-threaded access.
 */
class Timer {
   public:
    explicit Timer(bool start_immediately = false) {
        if (start_immediately) {
            start();
        }
    }

    void start() {
        if (running_) {
            return;
        }
        running_ = true;
        start_tp_ = timer_detail::clock::now();
    }

    void stop() {
        if (!running_) {
            return;
        }
        running_ = false;
        last_lap_ = timer_detail::to_ns(timer_detail::clock::now() - start_tp_);
        elapsed_ += last_lap_;
    }

    void reset() {
        running_ = false;
        elapsed_ = 0.0;
        last_lap_ = 0.0;
        start_tp_ = {};
    }

    [[nodiscard]] auto is_running() const -> bool { return running_; }

    /** Elapsed time in the requested unit. Counts live time if still running. */
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    [[nodiscard]] auto elapsed() const -> double {
        double total = elapsed_;
        if (running_) {
            total += timer_detail::to_ns(timer_detail::clock::now() - start_tp_);
        }
        return timer_detail::ns_to<D>(total);
    }
    [[nodiscard]] auto elapsed_ns() const -> double { return elapsed<std::chrono::nanoseconds>(); }
    [[nodiscard]] auto elapsed_us() const -> double { return elapsed<std::chrono::microseconds>(); }
    [[nodiscard]] auto elapsed_ms() const -> double { return elapsed<std::chrono::milliseconds>(); }
    [[nodiscard]] auto elapsed_s() const -> double { return elapsed<std::chrono::seconds>(); }

    /** Duration of the most recent start->stop pair in nanoseconds. Zero if never stopped. */
    [[nodiscard]] auto last_lap_ns() const -> double { return last_lap_; }

   private:
    bool running_ = false;
    double elapsed_ = 0.0;
    double last_lap_ = 0.0;
    timer_detail::time_point start_tp_;
};

// ─────────────────────────────────────────────────────────────────────────────
// TimerStats
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Accumulates statistics over multiple start/stop cycles using Welford's
 * online algorithm. All internal state is in nanoseconds.
 *
 * Thread safety: not thread-safe on its own. Access is serialised externally.
 */
struct TimerStats {
    std::size_t count = 0;
    double total = 0.0;
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
    double mean = 0.0;  // Welford running mean (ns)
    double M2 = 0.0;    // Welford running sum of squared deviations (ns²)

    void record(double lap_ns) {
        ++count;
        total += lap_ns;
        min = std::min(min, lap_ns);
        max = std::max(max, lap_ns);
        double delta = lap_ns - mean;
        mean += delta / static_cast<double>(count);
        M2 += delta * (lap_ns - mean);
    }

    void reset() { *this = TimerStats{}; }

    [[nodiscard]] auto variance() const -> double { return count < 2 ? 0.0 : M2 / static_cast<double>(count); }
    [[nodiscard]] auto sample_variance() const -> double { return count < 2 ? 0.0 : M2 / static_cast<double>(count - 1); }
    [[nodiscard]] auto stddev() const -> double { return std::sqrt(variance()); }
    [[nodiscard]] auto sample_stddev() const -> double { return std::sqrt(sample_variance()); }

    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    [[nodiscard]] auto get_total() const -> double {
        return timer_detail::ns_to<D>(total);
    }
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    [[nodiscard]] auto get_mean() const -> double {
        return count == 0 ? 0.0 : timer_detail::ns_to<D>(mean);
    }
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    [[nodiscard]] auto get_min() const -> double {
        return count == 0 ? 0.0 : timer_detail::ns_to<D>(min);
    }
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    [[nodiscard]] auto get_max() const -> double {
        return count == 0 ? 0.0 : timer_detail::ns_to<D>(max);
    }
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    [[nodiscard]] auto get_stddev() const -> double {
        return timer_detail::ns_to<D>(stddev());
    }
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    [[nodiscard]] auto get_sample_stddev() const -> double {
        return timer_detail::ns_to<D>(sample_stddev());
    }

    /**
     * Parallel Welford merge — correctly combines means and variances from two
     * independent sets without needing access to the original samples.
     * Reference: https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Parallel_algorithm
     */
    void merge(const TimerStats& other) {
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

// ─────────────────────────────────────────────────────────────────────────────
// TimerRegistry
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A registry for managing multiple named timers across threads.
 *
 * Performance design
 * ──────────────────
 * The hot path (start / stop / elapsed / is_running) is entirely lock-free:
 * each thread owns thread_local storage and never touches shared state.
 * The registry mutex is only acquired when:
 *   - A thread calls start() on a name for the very first time (registers itself).
 *   - reset() or erase() is called.
 *   - A report is requested.
 *   - A thread exits (snapshots its stats into the graveyard).
 *
 * Thread lifetime
 * ───────────────
 * When a thread exits, its thread_local destructor merges its final stats into
 * a per-name graveyard so no data is lost even if threads exit before reporting.
 */
class TimerRegistry {
   public:
    using thread_id = std::thread::id;

    TimerRegistry() = default;
    ~TimerRegistry() = default;
    TimerRegistry(const TimerRegistry&) = delete;
    auto operator=(const TimerRegistry&) -> TimerRegistry& = delete;

    // ── Hot path — lock-free ──────────────────────────────────────────────

    /** Starts the calling thread's timer for the given name. O(1), lock-free after first call. */
    void start(const std::string& name) { get_or_create_slot(name).timer.start(); }

    /**
     * Stops the calling thread's timer and records the lap. O(1), lock-free.
     * @throws std::runtime_error if stop() is called before start() on this thread.
     */
    static void stop(const std::string& name) {
        auto* slot = find_slot(name);
        if ((slot == nullptr) || !slot->timer.is_running()) {
            throw std::runtime_error("stop() called before start() for timer '" + name + "' on this thread.");
        }
        slot->timer.stop();
        slot->stats.record(slot->timer.last_lap_ns());
    }

    /** Returns true if the calling thread's timer is running. O(1), lock-free. */
    auto is_running(const std::string& name) const -> bool {
        const auto* slot = find_slot(name);
        if (slot == nullptr) {
            throw_slot_missing_(name);
        }
        return slot->timer.is_running();
    }

    /** Elapsed time for the calling thread's timer. O(1), lock-free. */
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    auto elapsed(const std::string& name) const -> double {
        const auto* slot = find_slot(name);
        if (!slot) {
            throw_slot_missing_(name);
        }
        return slot->timer.elapsed<D>();
    }
    auto elapsed_ns(const std::string& n) const -> double { return elapsed<std::chrono::nanoseconds>(n); }
    auto elapsed_us(const std::string& n) const -> double { return elapsed<std::chrono::microseconds>(n); }
    auto elapsed_ms(const std::string& n) const -> double { return elapsed<std::chrono::milliseconds>(n); }
    auto elapsed_s(const std::string& n) const -> double { return elapsed<std::chrono::seconds>(n); }

    /** Returns a copy of the calling thread's stats for the given name. Lock-free. */
    auto stats(const std::string& name) const -> TimerStats {
        const auto* slot = find_slot(name);
        if (slot == nullptr) {
            throw_slot_missing_(name, true);
        }
        return slot->stats;
    }

    // ── Slow path — acquires mutex ────────────────────────────────────────

    /**
     * Returns true if any live thread's timer for the given name is running.
     * @throws std::runtime_error if the name has never been started.
     */
    auto any_running(const std::string& name) const -> bool {
        std::lock_guard lock(mutex_);
        check_name_exists_locked(name);
        return std::ranges::any_of(live_threads_, [&](const auto& pair) {
            auto iter = pair.second->slots.find(name);
            return iter != pair.second->slots.end() && iter->second.timer.is_running();
        });
    }

    /**
     * Resets all threads' timers and stats for the given name (global, symmetric with erase).
     * The name remains registered; any thread can call start() again immediately.
     * @throws std::runtime_error if the name has never been started.
     */
    void reset(const std::string& name) {
        std::lock_guard lock(mutex_);
        check_name_exists_locked(name);
        for (auto& [tid, local] : live_threads_) {
            auto iter = local->slots.find(name);
            if (iter != local->slots.end()) {
                iter->second.timer.reset();
                iter->second.stats.reset();
            }
        }
        if (graveyard_.contains(name)) {
            graveyard_[name].reset();
        }
        auto& thr_grv = thread_graveyard_;
        thr_grv.erase(std::remove_if(thr_grv.begin(), thr_grv.end(), [&](const ThreadStatsRow& row) { return row.name == name; }), thr_grv.end());
    }

    /**
     * Erases all threads' timers, stats, and the name itself from the registry.
     * @throws std::runtime_error if the name has never been started.
     */
    void erase(const std::string& name) {
        // Remove the calling thread's own slot first (no mutex needed for own storage).
        auto& tloc = thread_local_storage();
        tloc.slots.erase(name);
        // Remove all other threads' slots, the graveyard entry, and the name itself.
        std::lock_guard lock(mutex_);
        check_name_exists_locked(name);
        for (auto& [tid, local] : live_threads_) {
            local->slots.erase(name);
        }
        graveyard_.erase(name);
        auto& thr_grv = thread_graveyard_;
        thr_grv.erase(std::remove_if(thr_grv.begin(), thr_grv.end(), [&](const ThreadStatsRow& row) { return row.name == name; }), thr_grv.end());
        known_names_.erase(name);
    }

    // ── Report types ──────────────────────────────────────────────────────

    /** One row in a merged (per-name) report. All time fields in the requested Duration. */
    struct StatsRow {
        std::string name;
        std::size_t thread_count;
        std::size_t call_count;
        double total;
        double mean;
        double min;
        double max;
        double stddev;
        double sample_stddev;
    };

    /** One row in a per-thread report. All time fields in the requested Duration. */
    struct ThreadStatsRow {
        std::string name;
        thread_id tid;
        std::size_t call_count;
        double total;
        double mean;
        double min;
        double max;
        double stddev;
        double sample_stddev;
    };

    // ── Report accessors ──────────────────────────────────────────────────

    /**
     * Returns a merged report — one row per name, stats aggregated across all
     * threads (including exited ones) via parallel Welford.
     */
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    auto get_stats_report() const -> std::vector<StatsRow> {
        std::vector<StatsRow> result;
        std::lock_guard lock(mutex_);
        for (const auto& name : known_names_) {
            TimerStats merged;
            std::size_t thread_count = 0;
            // Count individual exited threads from the per-thread graveyard.
            for (const auto& row : thread_graveyard_) {
                if (row.name != name) {
                    continue;
                }
                TimerStats stats;
                stats.count = row.call_count;
                stats.total = row.total;
                stats.mean = row.mean;
                stats.min = row.min;
                stats.max = row.max;
                // Reconstruct M2 from stddev: M2 = stddev^2 * count
                stats.M2 = (row.stddev * row.stddev) * static_cast<double>(row.call_count);
                merged.merge(stats);
                ++thread_count;
            }
            // Add live threads.
            for (const auto& [tid, local] : live_threads_) {
                auto iter = local->slots.find(name);
                if (iter != local->slots.end() && iter->second.stats.count > 0) {
                    merged.merge(iter->second.stats);
                    ++thread_count;
                }
            }
            if (merged.count == 0) {
                continue;
            }
            result.push_back({
                name,
                thread_count,
                merged.count,
                merged.get_total<D>(),
                merged.get_mean<D>(),
                merged.get_min<D>(),
                merged.get_max<D>(),
                merged.get_stddev<D>(),
                merged.get_sample_stddev<D>(),
            });
        }
        return result;
    }

    /** Returns a per-thread report — one row per (name, thread_id) pair for live threads. */
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    auto get_stats_report_per_thread() const -> std::vector<ThreadStatsRow> {
        std::vector<ThreadStatsRow> result;
        std::lock_guard lock(mutex_);
        for (const auto& row : thread_graveyard_) {
            result.push_back({
                row.name,
                row.tid,
                row.call_count,
                timer_detail::ns_to<D>(row.total),
                timer_detail::ns_to<D>(row.mean),
                timer_detail::ns_to<D>(row.min),
                timer_detail::ns_to<D>(row.max),
                timer_detail::ns_to<D>(row.stddev),
                timer_detail::ns_to<D>(row.sample_stddev),
            });
        }
        for (const auto& [tid, local] : live_threads_) {
            for (const auto& [name, slot] : local->slots) {
                if (slot.stats.count == 0) {
                    continue;
                }
                const auto& stats = slot.stats;
                result.push_back({
                    name,
                    tid,
                    stats.count,
                    stats.get_total<D>(),
                    stats.get_mean<D>(),
                    stats.get_min<D>(),
                    stats.get_max<D>(),
                    stats.get_stddev<D>(),
                    stats.get_sample_stddev<D>(),
                });
            }
        }
        return result;
    }

    /** Returns a simple elapsed-time report — one value per name, summed across live threads. */
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    auto get_report() const -> std::vector<std::pair<std::string, double>> {
        std::vector<std::pair<std::string, double>> result;
        std::lock_guard lock(mutex_);
        for (const auto& name : known_names_) {
            double total = 0.0;
            for (const auto& [tid, local] : live_threads_) {
                auto iter = local->slots.find(name);
                if (iter != local->slots.end()) {
                    total += iter->second.timer.elapsed<D>();
                }
            }
            result.emplace_back(name, total);
        }
        return result;
    }

    /** Prints the simple elapsed-time report. */
    template <timer_detail::ValidDuration D = std::chrono::milliseconds>
    void print_report() const {
        for (const auto& [name, value] : get_report<D>()) {
            std::cout << name << ": " << value << " " << timer_detail::unit_name<D>() << "\n";
        }
    }

    /**
     * Prints the merged statistics report.
     * Numbers are right-aligned, 2 decimal places. Each column independently
     * selects the most readable unit based on the smallest value in that column.
     */
    void print_stats_report() const {
        const auto rows = get_stats_report<std::chrono::nanoseconds>();
        if (rows.empty()) {
            return;
        }
        print_stats_table_(rows);
    }

    /**
     * Prints the per-thread statistics report.
     * Same formatting rules as print_stats_report().
     */
    void print_stats_report_per_thread() const {
        const auto rows = get_stats_report_per_thread<std::chrono::nanoseconds>();
        if (rows.empty()) {
            return;
        }
        print_per_thread_table_(rows);
    }

   private:
    // ── Per-thread slot ───────────────────────────────────────────────────

    struct Slot {
        Timer timer;
        TimerStats stats;
    };

    struct ThreadLocal {
        thread_id tid;
        std::unordered_map<std::string, Slot> slots;
        TimerRegistry* registry = nullptr;

        ~ThreadLocal() {
            if (registry == nullptr) {
                return;
            }
            std::lock_guard lock(registry->mutex_);
            for (const auto& [name, slot] : slots) {
                if (slot.stats.count == 0) {
                    continue;
                }
                registry->graveyard_[name].merge(slot.stats);
                const auto& stats = slot.stats;
                registry->thread_graveyard_.push_back({
                    name,
                    tid,
                    stats.count,
                    stats.total,
                    stats.mean,
                    stats.min,
                    stats.max,
                    stats.stddev(),
                    stats.sample_stddev(),
                });
            }
            registry->live_threads_.erase(tid);
        }
    };

    // ── Hot-path slot access ──────────────────────────────────────────────

    auto get_or_create_slot(const std::string& name) -> Slot& {
        auto& tloc = thread_local_storage();
        auto iter = tloc.slots.find(name);
        if (iter != tloc.slots.end()) {
            return iter->second;
        }
        {
            std::lock_guard lock(mutex_);
            known_names_.insert(name);
            if (tloc.registry == nullptr) {
                tloc.tid = std::this_thread::get_id();
                tloc.registry = this;
                live_threads_[tloc.tid] = &tloc;
            }
        }
        return tloc.slots[name];
    }

    static auto find_slot(const std::string& name) -> Slot* {
        auto& tloc = thread_local_storage();
        auto iter = tloc.slots.find(name);
        return iter != tloc.slots.end() ? &iter->second : nullptr;
    }

    static auto thread_local_storage() -> ThreadLocal& {
        thread_local ThreadLocal tloc;
        return tloc;
    }

    // ── Helpers ───────────────────────────────────────────────────────────

    void check_name_exists_locked(const std::string& name) const {
        if (!known_names_.contains(name)) {
            throw std::runtime_error("Timer '" + name + "' does not exist in the registry.");
        }
    }

    // Called on the hot path when find_slot returns nullptr. Distinguishes between
    // "name never existed / was erased" (does not exist) and "name exists but this
    // thread never called start()" (no entry for the calling thread).
    // Takes the mutex only to read known_names_ — this only runs in the error path.
    [[noreturn]] void throw_slot_missing_(const std::string& name, bool is_stats = false) const {
        std::lock_guard lock(mutex_);
        if (!known_names_.contains(name)) {
            throw std::runtime_error("Timer '" + name + "' does not exist in the registry.");
        }
        if (is_stats) {
            throw std::runtime_error("Timer '" + name + "' has no stats for the calling thread.");
        }
        throw std::runtime_error("Timer '" + name + "' has no entry for the calling thread.");
    }

    // ── Print helpers ─────────────────────────────────────────────────────

    static void print_stats_table_(const std::vector<StatsRow>& rows) {
        using namespace timer_detail;
        auto cmin = [&](auto func) -> auto { return col_min<StatsRow>(rows, func); };
        auto [dt, ut] = pick_unit(cmin([](const auto& row) { return row.total; }));
        auto [dm, um] = pick_unit(cmin([](const auto& row) { return row.mean; }));
        auto [di, ui] = pick_unit(cmin([](const auto& row) { return row.min; }));
        auto [dx, ux] = pick_unit(cmin([](const auto& row) { return row.max; }));
        auto [ds, us] = pick_unit(cmin([](const auto& row) { return row.stddev > 0 ? row.stddev : std::numeric_limits<double>::max(); }));

        constexpr int NAME_WIDTH = 24;
        constexpr int THREAD_WIDTH = 9;
        constexpr int VALUE_WIDTH = 14;
        constexpr int THREAD_COLUMNS = 2;
        constexpr int VALUE_COLUMNS = 5;
        auto hdr = [](const char* key, const char* unit) -> std::string { return std::string(key) + "(" + unit + ")"; };
        std::cout << std::left << std::setw(NAME_WIDTH) << "Timer" << std::right << std::setw(THREAD_WIDTH) << "Threads" << std::setw(THREAD_WIDTH)
                  << "Calls" << std::setw(VALUE_WIDTH) << hdr("Total", ut) << std::setw(VALUE_WIDTH) << hdr("Mean", um) << std::setw(VALUE_WIDTH)
                  << hdr("Min", ui) << std::setw(VALUE_WIDTH) << hdr("Max", ux) << std::setw(VALUE_WIDTH) << hdr("Stddev", us) << "\n"
                  << std::string(NAME_WIDTH + (THREAD_WIDTH * THREAD_COLUMNS) + (VALUE_WIDTH * VALUE_COLUMNS), '-') << "\n";
        for (const auto& row : rows) {
            std::cout << std::left << std::setw(NAME_WIDTH) << row.name << std::right << std::setw(THREAD_WIDTH) << row.thread_count
                      << std::setw(THREAD_WIDTH) << row.call_count << std::fixed << std::setprecision(2) << std::setw(VALUE_WIDTH) << row.total / dt
                      << std::setw(VALUE_WIDTH) << row.mean / dm << std::setw(VALUE_WIDTH) << row.min / di << std::setw(VALUE_WIDTH) << row.max / dx
                      << std::setw(VALUE_WIDTH) << row.stddev / ds << "\n";
        }
    }

    static void print_per_thread_table_(const std::vector<ThreadStatsRow>& rows) {
        using namespace timer_detail;
        auto cmin = [&](auto func) -> auto { return col_min<ThreadStatsRow>(rows, func); };
        auto [dt, ut] = pick_unit(cmin([](const auto& row) { return row.total; }));
        auto [dm, um] = pick_unit(cmin([](const auto& row) { return row.mean; }));
        auto [di, ui] = pick_unit(cmin([](const auto& row) { return row.min; }));
        auto [dx, ux] = pick_unit(cmin([](const auto& row) { return row.max; }));
        auto [ds, us] = pick_unit(cmin([](const auto& row) { return row.stddev > 0 ? row.stddev : std::numeric_limits<double>::max(); }));

        constexpr int NAME_WIDTH = 24;
        constexpr int IDW = 24;
        constexpr int CALL_WIDTH = 8;
        constexpr int VALUE_WIDTH = 14;
        constexpr int VALUE_COLUMNS = 5;
        auto hdr = [](const char* key, const char* unit) -> std::string { return std::string(key) + "(" + unit + ")"; };
        std::cout << std::left << std::setw(NAME_WIDTH) << "Timer" << std::setw(IDW) << "Thread ID" << std::right << std::setw(CALL_WIDTH) << "Calls"
                  << std::setw(VALUE_WIDTH) << hdr("Total", ut) << std::setw(VALUE_WIDTH) << hdr("Mean", um) << std::setw(VALUE_WIDTH)
                  << hdr("Min", ui) << std::setw(VALUE_WIDTH) << hdr("Max", ux) << std::setw(VALUE_WIDTH) << hdr("Stddev", us) << "\n"
                  << std::string(NAME_WIDTH + IDW + CALL_WIDTH + (VALUE_WIDTH * VALUE_COLUMNS), '-') << "\n";
        for (const auto& row : rows) {
            std::ostringstream ostr;
            ostr << row.tid;
            std::cout << std::left << std::setw(NAME_WIDTH) << row.name << std::setw(IDW) << ostr.str() << std::right << std::setw(CALL_WIDTH)
                      << row.call_count << std::fixed << std::setprecision(2) << std::setw(VALUE_WIDTH) << row.total / dt << std::setw(VALUE_WIDTH)
                      << row.mean / dm << std::setw(VALUE_WIDTH) << row.min / di << std::setw(VALUE_WIDTH) << row.max / dx << std::setw(VALUE_WIDTH)
                      << row.stddev / ds << "\n";
        }
    }

    // ── State — all guarded by mutex_ ─────────────────────────────────────

    mutable std::mutex mutex_;
    std::unordered_map<thread_id, ThreadLocal*> live_threads_;
    std::unordered_map<std::string, TimerStats> graveyard_;
    std::vector<ThreadStatsRow> thread_graveyard_;
    std::unordered_set<std::string> known_names_;
};

// ─────────────────────────────────────────────────────────────────────────────
// ScopedTimer
// ─────────────────────────────────────────────────────────────────────────────

/**
 * RAII timer: starts on construction, stops on destruction.
 * When backed by a registry the hot path is lock-free (thread_local storage).
 * When standalone iter prints name + elapsed to stdout on destruction.
 */
template <timer_detail::ValidDuration D = std::chrono::milliseconds>
class ScopedTimer {
   public:
    explicit ScopedTimer(std::string name) : name_(std::move(name)), registry_(nullptr) { timer_.start(); }

    explicit ScopedTimer(std::string name, TimerRegistry& registry) : name_(std::move(name)), registry_(&registry) { registry_->start(name_); }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    ~ScopedTimer() noexcept {
        if (registry_ != nullptr) {
            try {
                registry_->stop(name_);
            } catch (...) {
            }
        } else {
            timer_.stop();
            std::cout << name_ << ": " << timer_.elapsed<D>() << " " << timer_detail::unit_name<D>() << "\n";
        }
    }

   private:
    std::string name_;
    TimerRegistry* registry_;
    Timer timer_;
};

// For global use (as if it was singleton)
inline auto global_timers() -> TimerRegistry& {
    static TimerRegistry inst;
    return inst;
}

#define TIMERS global_timers()