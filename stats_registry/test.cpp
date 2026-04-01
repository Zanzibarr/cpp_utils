/**
 * test_stats_registry.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Full test suite for TimerRegistry and StatsRegistry (CTString API).
 *
 * Compile (C++20):
 *   g++ -std=c++20 -O2 -pthread test_stats_registry.cpp -o test_stats && ./test_stats
 */

#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "../testing/test_main.hpp"
#include "stats_registry.hxx"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

inline auto find_gauge(const std::vector<StatsRegistry::GaugeRow>& rows, const std::string& name) -> StatsRegistry::GaugeRow {
    for (const auto& r : rows)
        if (r.name == name) return r;
    throw std::runtime_error("Gauge '" + name + "' not found");
}

inline auto find_counter(const std::vector<StatsRegistry::CounterRow>& rows, const std::string& name) -> StatsRegistry::CounterRow {
    for (const auto& r : rows)
        if (r.name == name) return r;
    throw std::runtime_error("Counter '" + name + "' not found");
}

inline auto find_histogram(const std::vector<StatsRegistry::HistogramRow>& rows, const std::string& name) -> StatsRegistry::HistogramRow {
    for (const auto& r : rows)
        if (r.name == name) return r;
    throw std::runtime_error("Histogram '" + name + "' not found");
}

inline auto find_timer(const std::vector<TimerRegistry::StatsRow>& rows, const std::string& name) -> TimerRegistry::StatsRow {
    for (const auto& r : rows)
        if (r.name == name) return r;
    throw std::runtime_error("Timer '" + name + "' not found");
}

inline auto find_series(const std::vector<StatsRegistry::SeriesRow>& rows, const std::string& name) -> StatsRegistry::SeriesRow {
    for (const auto& row : rows)
        if (row.name == name) return row;
    throw std::runtime_error("Series '" + name + "' not found");
}

// Suppresses std::cout output for the duration of the scope.
struct SuppressStdout {
    SuppressStdout() : old_buf_(std::cout.rdbuf(null_stream_.rdbuf())) {}
    ~SuppressStdout() { std::cout.rdbuf(old_buf_); }
    SuppressStdout(const SuppressStdout&) = delete;
    SuppressStdout& operator=(const SuppressStdout&) = delete;

   private:
    std::ostringstream null_stream_;
    std::streambuf* old_buf_;
};

}  // namespace

// ═════════════════════════════════════════════════════════════════════════════
// TIMER — basic Timer class
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Timer – basic")

TEST_CASE("timer starts not running") {
    Timer t;
    expect(t.is_running()).to_be_false();
}

TEST_CASE("timer starts running when constructed with start_immediately=true") {
    Timer t(true);
    expect(t.is_running()).to_be_true();
    t.stop();
}

TEST_CASE("start sets is_running to true") {
    Timer t;
    t.start();
    expect(t.is_running()).to_be_true();
    t.stop();
}

TEST_CASE("stop sets is_running to false") {
    Timer t;
    t.start();
    t.stop();
    expect(t.is_running()).to_be_false();
}

TEST_CASE("elapsed is non-negative after start and stop") {
    Timer t;
    t.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    t.stop();
    expect(t.elapsed_ms() >= 0.0).to_be_true();
}

TEST_CASE("elapsed grows while timer is running") {
    Timer t(true);
    double e1 = t.elapsed_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    double e2 = t.elapsed_ms();
    t.stop();
    expect(e2 > e1).to_be_true();
}

TEST_CASE("elapsed accumulates across multiple start/stop cycles") {
    Timer t;
    t.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t.stop();
    t.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t.stop();
    // should be roughly 20ms total
    expect(t.elapsed_ms() >= 15.0).to_be_true();
}

TEST_CASE("reset clears elapsed time") {
    Timer t;
    t.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t.stop();
    t.reset();
    expect(t.elapsed_ms()).to_approx_equal(0.0);
    expect(t.is_running()).to_be_false();
}

TEST_CASE("calling start while already running is a no-op") {
    Timer t;
    t.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    t.start();  // should not reset the start point
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    t.stop();
    expect(t.elapsed_ms() >= 8.0).to_be_true();
}

TEST_CASE("calling stop while not running is a no-op") {
    Timer t;
    t.stop();  // not running — should not crash or set elapsed
    expect(t.elapsed_ms()).to_approx_equal(0.0);
}

TEST_CASE("last_lap_ns is zero before first stop") {
    Timer t;
    expect(t.last_lap_ns()).to_approx_equal(0.0);
}

TEST_CASE("last_lap_ns reflects only the most recent start/stop interval") {
    Timer t;
    t.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t.stop();
    double lap1 = t.last_lap_ns();

    t.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    t.stop();
    double lap2 = t.last_lap_ns();

    expect(lap2 > lap1).to_be_true();
}

TEST_CASE("elapsed_ns elapsed_us elapsed_s return consistent values") {
    Timer t;
    t.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    t.stop();
    double ms = t.elapsed_ms();
    double us = t.elapsed_us();
    double ns = t.elapsed_ns();
    double s = t.elapsed_s();
    // All should represent the same duration
    expect(std::abs(us - ms * 1000.0) < 1.0).to_be_true();
    expect(std::abs(ns - ms * 1e6) < 1e6).to_be_true();
    expect(std::abs(s - ms / 1000.0) < 0.001).to_be_true();
}

TEST_CASE("elapsed_ms is zero immediately after construction") {
    Timer t;
    expect(t.elapsed_ms()).to_approx_equal(0.0);
}

TEST_CASE("elapsed includes in-flight time while running") {
    Timer t(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    double e = t.elapsed_ms();
    t.stop();
    expect(e > 0.0).to_be_true();
}

TEST_CASE("elapsed after stop does not grow") {
    Timer t(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t.stop();
    double e1 = t.elapsed_ms();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    double e2 = t.elapsed_ms();
    expect(e1).to_approx_equal(e2, 0.1);
}

TEST_CASE("reset while running stops the timer") {
    Timer t(true);
    t.reset();
    expect(t.is_running()).to_be_false();
    expect(t.elapsed_ms()).to_approx_equal(0.0);
}

TEST_CASE("last_lap_ns is set after stop") {
    Timer t(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    t.stop();
    expect(t.last_lap_ns() > 0.0).to_be_true();
}

TEST_CASE("second stop after first stop does not update last_lap") {
    Timer t(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    t.stop();
    double lap1 = t.last_lap_ns();
    t.stop();  // no-op
    expect(t.last_lap_ns()).to_approx_equal(lap1);
}

TEST_CASE("elapsed template with nanoseconds returns large values") {
    Timer t(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    t.stop();
    expect(t.elapsed<std::chrono::nanoseconds>() > 1000.0).to_be_true();
}

TEST_CASE("elapsed template with seconds returns small values for short timings") {
    Timer t(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t.stop();
    expect(t.elapsed<std::chrono::seconds>() < 1.0).to_be_true();
    expect(t.elapsed<std::chrono::seconds>() > 0.0).to_be_true();
}

// ═════════════════════════════════════════════════════════════════════════════
// TIMER STATS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("TimerStats")

TEST_CASE("record increments count") {
    TimerStats s;
    s.record(1000.0);
    s.record(2000.0);
    expect(s.count).to_equal(static_cast<std::size_t>(2));
}

TEST_CASE("total accumulates correctly") {
    TimerStats s;
    s.record(1000.0);
    s.record(3000.0);
    expect(s.total).to_approx_equal(4000.0);
}

TEST_CASE("min and max are tracked correctly") {
    TimerStats s;
    s.record(500.0);
    s.record(1500.0);
    s.record(1000.0);
    expect(s.min).to_approx_equal(500.0);
    expect(s.max).to_approx_equal(1500.0);
}

TEST_CASE("mean is correct for known values") {
    TimerStats s;
    s.record(1000.0);
    s.record(2000.0);
    s.record(3000.0);
    expect(s.mean).to_approx_equal(2000.0);
}

TEST_CASE("stddev is zero for single sample") {
    TimerStats s;
    s.record(1000.0);
    expect(s.stddev()).to_approx_equal(0.0);
}

TEST_CASE("stddev is zero for identical samples") {
    TimerStats s;
    for (int i = 0; i < 5; ++i) s.record(1000.0);
    expect(s.stddev()).to_approx_equal(0.0);
}

TEST_CASE("population stddev is correct for known dataset") {
    // {2,4,4,4,5,5,7,9} — population stddev = 2.0  (values * 1e6 ns)
    TimerStats s;
    for (double v : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) s.record(v * 1e6);
    expect(s.stddev() / 1e6).to_approx_equal(2.0, 1e-9);
}

TEST_CASE("reset clears all fields") {
    TimerStats s;
    s.record(1000.0);
    s.reset();
    expect(s.count).to_equal(static_cast<std::size_t>(0));
    expect(s.total).to_approx_equal(0.0);
}

TEST_CASE("merge with empty other is a no-op") {
    TimerStats s;
    s.record(1000.0);
    TimerStats empty;
    s.merge(empty);
    expect(s.count).to_equal(static_cast<std::size_t>(1));
    expect(s.mean).to_approx_equal(1000.0);
}

TEST_CASE("merge into empty takes other's values") {
    TimerStats s;
    TimerStats other;
    other.record(1000.0);
    s.merge(other);
    expect(s.count).to_equal(static_cast<std::size_t>(1));
    expect(s.mean).to_approx_equal(1000.0);
}

TEST_CASE("merge correctly combines two non-empty sets") {
    TimerStats a;
    a.record(1000.0);
    a.record(2000.0);
    TimerStats b;
    b.record(3000.0);
    b.record(4000.0);
    a.merge(b);
    expect(a.count).to_equal(static_cast<std::size_t>(4));
    expect(a.mean).to_approx_equal(2500.0);
    expect(a.min).to_approx_equal(1000.0);
    expect(a.max).to_approx_equal(4000.0);
}

TEST_CASE("get_total get_mean get_min get_max honour Duration conversion") {
    TimerStats s;
    s.record(1'000'000.0);  // 1 ms in ns
    expect(s.get_total<std::chrono::milliseconds>()).to_approx_equal(1.0, 1e-9);
    expect(s.get_mean<std::chrono::milliseconds>()).to_approx_equal(1.0, 1e-9);
    expect(s.get_min<std::chrono::milliseconds>()).to_approx_equal(1.0, 1e-9);
    expect(s.get_max<std::chrono::milliseconds>()).to_approx_equal(1.0, 1e-9);
}

TEST_CASE("sample variance uses Bessel's correction") {
    TimerStats s;
    // Population variance of {1,2,3} = 2/3, sample variance = 1.0
    s.record(1.0);
    s.record(2.0);
    s.record(3.0);
    expect(s.sample_variance()).to_approx_equal(1.0, 1e-9);
    expect(s.variance()).to_approx_equal(2.0 / 3.0, 1e-9);
}

TEST_CASE("sample_stddev is larger than population stddev for small samples") {
    TimerStats s;
    s.record(1.0);
    s.record(2.0);
    s.record(3.0);
    expect(s.sample_stddev() > s.stddev()).to_be_true();
}

TEST_CASE("variance returns 0 for a single sample") {
    TimerStats s;
    s.record(42.0);
    expect(s.variance()).to_approx_equal(0.0);
    expect(s.sample_variance()).to_approx_equal(0.0);
}

TEST_CASE("merge preserves min across both sets") {
    TimerStats a;
    a.record(500.0);
    a.record(1500.0);
    TimerStats b;
    b.record(100.0);
    b.record(2000.0);
    a.merge(b);
    expect(a.min).to_approx_equal(100.0);
}

TEST_CASE("merge preserves max across both sets") {
    TimerStats a;
    a.record(500.0);
    TimerStats b;
    b.record(9999.0);
    a.merge(b);
    expect(a.max).to_approx_equal(9999.0);
}

TEST_CASE("merge preserves total") {
    TimerStats a;
    a.record(1000.0);
    a.record(2000.0);
    TimerStats b;
    b.record(4000.0);
    a.merge(b);
    expect(a.total).to_approx_equal(7000.0);
}

TEST_CASE("merge of two sets gives correct combined stddev") {
    // Set A: {2,4}, Set B: {6,8}
    // Combined: {2,4,6,8}, mean=5, population variance=5, stddev=sqrt(5)
    TimerStats a;
    a.record(2.0);
    a.record(4.0);
    TimerStats b;
    b.record(6.0);
    b.record(8.0);
    a.merge(b);
    expect(a.stddev()).to_approx_equal(std::sqrt(5.0), 1e-9);
}

TEST_CASE("get_stddev returns value in correct Duration unit") {
    TimerStats s;
    // Two values 2ms apart in ns: 0ns and 2,000,000ns → stddev in ns = 1,000,000
    s.record(0.0);
    s.record(2'000'000.0);
    double stddev_ms = s.get_stddev<std::chrono::milliseconds>();
    double stddev_us = s.get_stddev<std::chrono::microseconds>();
    expect(std::abs(stddev_us - stddev_ms * 1000.0) < 1e-3).to_be_true();
}

TEST_CASE("get_sample_stddev returns value in correct Duration unit") {
    TimerStats s;
    s.record(0.0);
    s.record(2'000'000.0);
    double ssd_ms = s.get_sample_stddev<std::chrono::milliseconds>();
    expect(ssd_ms > 0.0).to_be_true();
}

TEST_CASE("WelfordAccumulator record single value sets mean equal to value") {
    timer_detail::WelfordAccumulator acc;
    acc.record(42.0);
    expect(acc.mean).to_approx_equal(42.0);
    expect(acc.min).to_approx_equal(42.0);
    expect(acc.max).to_approx_equal(42.0);
    expect(acc.total).to_approx_equal(42.0);
    expect(acc.count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("WelfordAccumulator reset produces clean state") {
    timer_detail::WelfordAccumulator acc;
    acc.record(100.0);
    acc.reset();
    timer_detail::WelfordAccumulator fresh;
    expect(acc.count).to_equal(fresh.count);
    expect(acc.total).to_approx_equal(fresh.total);
    expect(acc.mean).to_approx_equal(fresh.mean);
    expect(acc.M2).to_approx_equal(fresh.M2);
}

// ═════════════════════════════════════════════════════════════════════════════
// TIMER REGISTRY — Global Singleton
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("TimerRegistry – Global Singleton")

TEST_CASE("global_timers returns the same instance on every call") {
    auto& a = global_timers();
    auto& b = global_timers();
    expect(&a).to_equal(&b);
}

TEST_CASE("TIMER_REG macro refers to the same instance as global_timers()") { expect(&TIMER_REG).to_equal(&global_timers()); }

TEST_CASE("global_timers instance is distinct from a local TimerRegistry") {
    TimerRegistry local;
    expect(&local).not_to_equal(&global_timers());
}

// ═════════════════════════════════════════════════════════════════════════════
// TIMER REGISTRY — Instance Isolation
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("TimerRegistry – Instance Isolation")

TEST_CASE("stats in reg1 do not appear in reg2") {
    TimerRegistry reg1;
    TimerRegistry reg2;
    reg1.start<"iso_t">();
    reg1.stop<"iso_t">();
    expect(reg2.get_stats_report().empty()).to_be_true();
}

TEST_CASE("call counts in two registries are independent") {
    TimerRegistry reg1;
    TimerRegistry reg2;
    for (int i = 0; i < 3; ++i) {
        reg1.start<"iso_t2">();
        reg1.stop<"iso_t2">();
    }
    for (int i = 0; i < 7; ++i) {
        reg2.start<"iso_t2">();
        reg2.stop<"iso_t2">();
    }
    expect(reg1.stats<"iso_t2">().count).to_equal(static_cast<std::size_t>(3));
    expect(reg2.stats<"iso_t2">().count).to_equal(static_cast<std::size_t>(7));
}

TEST_CASE("reset in reg1 does not affect reg2") {
    TimerRegistry reg1;
    TimerRegistry reg2;
    for (int i = 0; i < 3; ++i) {
        reg1.start<"iso_rst">();
        reg1.stop<"iso_rst">();
    }
    for (int i = 0; i < 3; ++i) {
        reg2.start<"iso_rst">();
        reg2.stop<"iso_rst">();
    }
    reg1.reset<"iso_rst">();
    reg1.start<"iso_rst">();
    reg1.stop<"iso_rst">();
    expect(reg1.stats<"iso_rst">().count).to_equal(static_cast<std::size_t>(1));
    expect(reg2.stats<"iso_rst">().count).to_equal(static_cast<std::size_t>(3));
}

// ═════════════════════════════════════════════════════════════════════════════
// TIMER REGISTRY — compile-time API
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("TimerRegistry – CTString API")

TEST_CASE("start and stop accumulate one call") {
    TimerRegistry reg;
    reg.start<"t">();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    reg.stop<"t">();
    expect(reg.stats<"t">().count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("elapsed returns positive value while running") {
    TimerRegistry reg;
    reg.start<"t2">();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    double e = reg.elapsed<"t2", std::chrono::milliseconds>();
    reg.stop<"t2">();
    expect(e > 0.0).to_be_true();
}

TEST_CASE("is_running reflects timer state") {
    TimerRegistry reg;
    expect(reg.is_running<"t3">()).to_be_false();
    reg.start<"t3">();
    expect(reg.is_running<"t3">()).to_be_true();
    reg.stop<"t3">();
    expect(reg.is_running<"t3">()).to_be_false();
}

TEST_CASE("multiple start/stop cycles accumulate stats") {
    TimerRegistry reg;
    for (int i = 0; i < 5; ++i) {
        reg.start<"multi">();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        reg.stop<"multi">();
    }
    expect(reg.stats<"multi">().count).to_equal(static_cast<std::size_t>(5));
}

TEST_CASE("handle-based stop works correctly") {
    TimerRegistry reg;
    auto* slot = reg.start<"handle">();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    TimerRegistry::stop(slot);
    expect(reg.stats<"handle">().count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("handle-based stop and named stop accumulate to the same slot") {
    TimerRegistry reg;
    auto* slot = reg.start<"h2">();
    TimerRegistry::stop(slot);
    reg.start<"h2">();
    reg.stop<"h2">();
    expect(reg.stats<"h2">().count).to_equal(static_cast<std::size_t>(2));
}

TEST_CASE("reset clears per-thread stats for the named timer") {
    TimerRegistry reg;
    for (int i = 0; i < 3; ++i) {
        reg.start<"r">();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        reg.stop<"r">();
    }
    reg.reset<"r">();
    reg.start<"r">();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    reg.stop<"r">();
    expect(reg.stats<"r">().count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("get_stats_report contains entry for started timer") {
    TimerRegistry reg;
    reg.start<"rep">();
    reg.stop<"rep">();
    auto rows = reg.get_stats_report();
    auto row = find_timer(rows, "rep");
    expect(row.call_count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("get_stats_report is empty when no timers have been used") {
    TimerRegistry reg;
    expect(reg.get_stats_report().empty()).to_be_true();
}

TEST_CASE("get_stats_report merged across threads has correct total call count") {
    TimerRegistry reg;
    constexpr int N = 4;
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&] {
            reg.start<"mt">();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            reg.stop<"mt">();
        });
    }
    for (auto& t : threads) t.join();
    auto row = find_timer(reg.get_stats_report(), "mt");
    expect(row.call_count).to_equal(static_cast<std::size_t>(N));
    expect(row.thread_count).to_equal(static_cast<std::size_t>(N));
}

TEST_CASE("get_thread_report has one row per thread") {
    TimerRegistry reg;
    constexpr int N = 3;
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&] {
            reg.start<"pt">();
            reg.stop<"pt">();
        });
    }
    for (auto& t : threads) t.join();
    auto rows = reg.get_thread_report();
    std::size_t count = 0;
    for (const auto& r : rows)
        if (r.name == "pt") ++count;
    expect(count).to_equal(static_cast<std::size_t>(N));
}

TEST_CASE("start returns non-null slot pointer") {
    TimerRegistry reg;
    auto* slot = reg.start<"slot_notnull">();
    expect(slot != nullptr).to_be_true();
    TimerRegistry::stop(slot);
}

TEST_CASE("successive start calls on same name return same slot address") {
    TimerRegistry reg;
    auto* s1 = reg.start<"same_slot">();
    TimerRegistry::stop(s1);
    auto* s2 = reg.start<"same_slot">();
    TimerRegistry::stop(s2);
    expect(s1).to_equal(s2);
}

TEST_CASE("stopping an already-stopped slot is a no-op") {
    TimerRegistry reg;
    auto* slot = reg.start<"stop_noop">();
    TimerRegistry::stop(slot);
    std::size_t count_before = reg.stats<"stop_noop">().count;
    TimerRegistry::stop(slot);  // second stop — should not record
    expect(reg.stats<"stop_noop">().count).to_equal(count_before);
}

TEST_CASE("stopping via name an already-stopped timer is a no-op") {
    TimerRegistry reg;
    reg.start<"stop_name_noop">();
    reg.stop<"stop_name_noop">();
    std::size_t count_before = reg.stats<"stop_name_noop">().count;
    reg.stop<"stop_name_noop">();
    expect(reg.stats<"stop_name_noop">().count).to_equal(count_before);
}

TEST_CASE("min and max are tracked correctly in stats") {
    TimerRegistry reg;
    // Use busy-spin to ensure measurable elapsed time differences
    for (int i = 0; i < 3; ++i) {
        reg.start<"minmax">();
        std::this_thread::sleep_for(std::chrono::milliseconds(5 + i * 5));
        reg.stop<"minmax">();
    }
    auto s = reg.stats<"minmax">();
    expect(s.max >= s.min).to_be_true();
    expect(s.min > 0.0).to_be_true();
}

TEST_CASE("mean is between min and max") {
    TimerRegistry reg;
    for (int i = 0; i < 5; ++i) {
        reg.start<"meanclamp">();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        reg.stop<"meanclamp">();
    }
    auto s = reg.stats<"meanclamp">();
    expect(s.mean >= s.min).to_be_true();
    expect(s.mean <= s.max).to_be_true();
}

TEST_CASE("stats count matches call count after many iterations") {
    TimerRegistry reg;
    constexpr int N = 20;
    for (int i = 0; i < N; ++i) {
        auto* slot = reg.start<"many_calls">();
        TimerRegistry::stop(slot);
    }
    expect(reg.stats<"many_calls">().count).to_equal(static_cast<std::size_t>(N));
}

TEST_CASE("get_totals_report returns entry for used timer") {
    TimerRegistry reg;
    reg.start<"rep_simple">();
    reg.stop<"rep_simple">();
    auto rows = reg.get_totals_report();
    bool found = false;
    for (const auto& [name, val] : rows)
        if (name == "rep_simple") found = true;
    expect(found).to_be_true();
}

TEST_CASE("get_totals_report elapsed value is positive after real work") {
    TimerRegistry reg;
    reg.start<"rep_pos">();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    reg.stop<"rep_pos">();
    auto rows = reg.get_totals_report();
    double val = 0.0;
    for (const auto& [name, v] : rows)
        if (name == "rep_pos") val = v;
    expect(val > 0.0).to_be_true();
}

TEST_CASE("get_totals_report is empty when no timers have been started") {
    TimerRegistry reg;
    expect(reg.get_totals_report().empty()).to_be_true();
}

TEST_CASE("get_totals_report does not include in-flight time from running timer") {
    TimerRegistry reg;
    // Record one completed lap
    reg.start<"inflight_excl">();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    reg.stop<"inflight_excl">();
    auto rows_stopped = reg.get_totals_report();
    double stopped_val = 0.0;
    for (const auto& [name, v] : rows_stopped)
        if (name == "inflight_excl") stopped_val = v;

    // Start again but don't stop — in-flight time should not appear in snapshot
    reg.start<"inflight_excl">();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto rows_running = reg.get_totals_report();
    double running_val = 0.0;
    for (const auto& [name, v] : rows_running)
        if (name == "inflight_excl") running_val = v;
    reg.stop<"inflight_excl">();

    expect(running_val).to_approx_equal(stopped_val, 1.0);  // within 1ms tolerance
}

TEST_CASE("reset then re-use shows only new calls in stats") {
    TimerRegistry reg;
    for (int i = 0; i < 5; ++i) {
        reg.start<"reset_reuse">();
        reg.stop<"reset_reuse">();
    }
    reg.reset<"reset_reuse">();
    reg.start<"reset_reuse">();
    reg.stop<"reset_reuse">();
    reg.start<"reset_reuse">();
    reg.stop<"reset_reuse">();
    expect(reg.stats<"reset_reuse">().count).to_equal(static_cast<std::size_t>(2));
}

TEST_CASE("get_stats_report total is sum of individual laps") {
    TimerRegistry reg;
    reg.start<"total_sum">();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    reg.stop<"total_sum">();
    reg.start<"total_sum">();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    reg.stop<"total_sum">();
    auto rows = reg.get_stats_report();
    auto row = find_timer(rows, "total_sum");
    expect(row.total >= 15.0).to_be_true();  // at least 20ms total, allow slack
}

TEST_CASE("two distinct timer names do not interfere") {
    TimerRegistry reg;
    reg.start<"ta">();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    reg.stop<"ta">();
    reg.start<"tb">();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    reg.stop<"tb">();
    auto s_a = reg.stats<"ta">();
    auto s_b = reg.stats<"tb">();
    expect(s_b.mean > s_a.mean).to_be_true();
}

TEST_CASE("timer from exited thread appears in get_stats_report") {
    TimerRegistry reg;
    {
        std::thread t([&] {
            reg.start<"exited_thread_timer">();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            reg.stop<"exited_thread_timer">();
        });
        t.join();
    }
    auto row = find_timer(reg.get_stats_report(), "exited_thread_timer");
    expect(row.call_count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("timer from exited thread appears in get_thread_report") {
    TimerRegistry reg;
    {
        std::thread t([&] {
            reg.start<"exited_pt">();
            reg.stop<"exited_pt">();
        });
        t.join();
    }
    auto rows = reg.get_thread_report();
    bool found = false;
    for (const auto& r : rows)
        if (r.name == "exited_pt") found = true;
    expect(found).to_be_true();
}

TEST_CASE("elapsed is zero after reset while not running") {
    TimerRegistry reg;
    reg.start<"el_rst">();
    reg.stop<"el_rst">();
    reg.reset<"el_rst">();
    expect(reg.elapsed<"el_rst">()).to_approx_equal(0.0);
}

TEST_CASE("reset clears graveyard entry for timer run on an exited thread") {
    // Exercises the remove_if lambda inside reset<Name>() that removes
    // thread_graveyard_ entries once a worker thread has exited.
    TimerRegistry reg;
    std::thread([&] {
        reg.start<"el_rst">();
        reg.stop<"el_rst">();
    }).join();
    // The joined thread's stats moved into thread_graveyard_; reset cleans them.
    reg.reset<"el_rst">();
    expect(reg.stats<"el_rst">().count).to_equal(static_cast<std::size_t>(0));
}

TEST_CASE("get_stats_report merged stddev is non-negative") {
    TimerRegistry reg;
    for (int i = 0; i < 5; ++i) {
        reg.start<"stddev_nn">();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        reg.stop<"stddev_nn">();
    }
    auto row = find_timer(reg.get_stats_report(), "stddev_nn");
    expect(row.stddev >= 0.0).to_be_true();
    expect(row.sample_stddev >= 0.0).to_be_true();
}

TEST_CASE("stats report preserves insertion order of names") {
    TimerRegistry reg;
    reg.start<"first_name">();
    reg.stop<"first_name">();
    reg.start<"second_name">();
    reg.stop<"second_name">();
    reg.start<"third_name">();
    reg.stop<"third_name">();
    auto rows = reg.get_stats_report();
    std::vector<std::string> names;
    for (const auto& r : rows) names.push_back(r.name);
    // Find the three in order
    auto it1 = std::find(names.begin(), names.end(), "first_name");
    auto it2 = std::find(names.begin(), names.end(), "second_name");
    auto it3 = std::find(names.begin(), names.end(), "third_name");
    expect(it1 != names.end()).to_be_true();
    expect(it2 != names.end()).to_be_true();
    expect(it3 != names.end()).to_be_true();
    expect(it1 < it2).to_be_true();
    expect(it2 < it3).to_be_true();
}

TEST_CASE("many threads all use the same timer name concurrently") {
    TimerRegistry reg;
    constexpr int N = 10;
    constexpr int REPS = 50;
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < REPS; ++j) {
                auto* s = reg.start<"concurrent_same">();
                TimerRegistry::stop(s);
            }
        });
    }
    for (auto& t : threads) t.join();
    auto row = find_timer(reg.get_stats_report(), "concurrent_same");
    expect(row.call_count).to_equal(static_cast<std::size_t>(N * REPS));
}

TEST_CASE("registry destructor does not crash with live threads") {
    expect_no_throw({
        TimerRegistry reg;
        std::thread t([&] {
            reg.start<"dtor_live">();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            reg.stop<"dtor_live">();
        });
        t.join();
        // reg destructs here
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// make_scoped_timer
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("make_scoped_timer")

TEST_CASE("scoped timer records one call on scope exit") {
    TimerRegistry reg;
    {
        auto t = make_scoped_timer<"sc">(reg);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(reg.stats<"sc">().count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("scoped timer stops on exception unwind") {
    TimerRegistry reg;
    try {
        auto t = make_scoped_timer<"sc_ex">(reg);
        throw std::runtime_error("test");
    } catch (...) {
    }
    expect(reg.stats<"sc_ex">().count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("multiple scoped timers accumulate correctly") {
    TimerRegistry reg;
    for (int i = 0; i < 4; ++i) {
        auto t = make_scoped_timer<"sc_multi">(reg);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(reg.stats<"sc_multi">().count).to_equal(static_cast<std::size_t>(4));
}

TEST_CASE("scoped timer elapsed is positive") {
    TimerRegistry reg;
    {
        auto t = make_scoped_timer<"sc_e">(reg);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(reg.stats<"sc_e">().get_total<std::chrono::milliseconds>() > 0.0).to_be_true();
}

TEST_CASE("scoped timer for different names are tracked separately") {
    TimerRegistry reg;
    {
        auto t1 = make_scoped_timer<"sc_diff_a">(reg);
        auto t2 = make_scoped_timer<"sc_diff_b">(reg);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(reg.stats<"sc_diff_a">().count).to_equal(static_cast<std::size_t>(1));
    expect(reg.stats<"sc_diff_b">().count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("scoped timer total elapsed is positive after sleep") {
    TimerRegistry reg;
    for (int i = 0; i < 3; ++i) {
        auto t = make_scoped_timer<"sc_total_pos">(reg);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto row = find_timer(reg.get_stats_report(), "sc_total_pos");
    expect(row.total > 0.0).to_be_true();
}

TEST_CASE("scoped timer works correctly across multiple threads") {
    TimerRegistry reg;
    constexpr int N = 6;
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&] {
            auto t = make_scoped_timer<"sc_mt">(reg);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        });
    }
    for (auto& t : threads) t.join();
    auto row = find_timer(reg.get_stats_report(), "sc_mt");
    expect(row.call_count).to_equal(static_cast<std::size_t>(N));
}

// ─────────────────────────────────────────────────────────────────────────────
// ScopedTimer (standalone, no registry)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("ScopedTimer – standalone")

TEST_CASE("ScopedTimer constructs and destructs without error") {
    SuppressStdout suppress;
    expect_no_throw({
        ScopedTimer<std::chrono::milliseconds> t("standalone");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    });
}

TEST_CASE("ScopedTimer with microsecond duration constructs and destructs without error") {
    SuppressStdout suppress;
    expect_no_throw({ ScopedTimer<std::chrono::microseconds> t("standalone_us"); });
}

// ═════════════════════════════════════════════════════════════════════════════
// COUNTERS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("StatsRegistry – Counters")

TEST_CASE("counter starts at zero on first access") {
    StatsRegistry reg;
    expect(reg.counter_get<"cnt_zero">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("counter_inc increments by default delta 1") {
    StatsRegistry reg;
    reg.counter_inc<"cnt_inc1">();
    expect(reg.counter_get<"cnt_inc1">()).to_equal(static_cast<int64_t>(1));
}

TEST_CASE("counter_inc increments by explicit delta") {
    StatsRegistry reg;
    reg.counter_inc<"cnt_inc2">(10);
    reg.counter_inc<"cnt_inc2">(5);
    expect(reg.counter_get<"cnt_inc2">()).to_equal(static_cast<int64_t>(15));
}

TEST_CASE("counter_dec decrements by default delta 1") {
    StatsRegistry reg;
    reg.counter_inc<"cnt_dec1">(5);
    reg.counter_dec<"cnt_dec1">();
    expect(reg.counter_get<"cnt_dec1">()).to_equal(static_cast<int64_t>(4));
}

TEST_CASE("counter_dec decrements by explicit delta") {
    StatsRegistry reg;
    reg.counter_inc<"cnt_dec2">(10);
    reg.counter_dec<"cnt_dec2">(3);
    expect(reg.counter_get<"cnt_dec2">()).to_equal(static_cast<int64_t>(7));
}

TEST_CASE("counter can go negative") {
    StatsRegistry reg;
    reg.counter_dec<"cnt_neg">(5);
    expect(reg.counter_get<"cnt_neg">()).to_equal(static_cast<int64_t>(-5));
}

TEST_CASE("counter_set assigns exact value") {
    StatsRegistry reg;
    reg.counter_inc<"cnt_set">(999);
    reg.counter_set<"cnt_set">(42);
    expect(reg.counter_get<"cnt_set">()).to_equal(static_cast<int64_t>(42));
}

TEST_CASE("counter_set to zero works") {
    StatsRegistry reg;
    reg.counter_inc<"cnt_set0">(99);
    reg.counter_set<"cnt_set0">(0);
    expect(reg.counter_get<"cnt_set0">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("counter_reset sets value back to zero") {
    StatsRegistry reg;
    reg.counter_inc<"cnt_rst">(100);
    reg.counter_reset<"cnt_rst">();
    expect(reg.counter_get<"cnt_rst">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("counter survives repeated inc/dec/reset cycle") {
    StatsRegistry reg;
    for (int i = 0; i < 100; ++i) reg.counter_inc<"cnt_cycle">();
    reg.counter_reset<"cnt_cycle">();
    for (int i = 0; i < 50; ++i) reg.counter_dec<"cnt_cycle">();
    expect(reg.counter_get<"cnt_cycle">()).to_equal(static_cast<int64_t>(-50));
}

TEST_CASE("counter_ref returns stable pointer to the same atomic") {
    StatsRegistry reg;
    auto* ptr = reg.counter_ref<"cnt_ref">();
    reg.counter_inc<"cnt_ref">(10);
    expect(ptr->load(std::memory_order_relaxed)).to_equal(static_cast<int64_t>(10));
}

TEST_CASE("counter_ref direct fetch_add is reflected in counter_get") {
    StatsRegistry reg;
    auto* ptr = reg.counter_ref<"cnt_ref2">();
    ptr->fetch_add(7, std::memory_order_relaxed);
    expect(reg.counter_get<"cnt_ref2">()).to_equal(static_cast<int64_t>(7));
}

TEST_CASE("get_counter_report contains all registered counters") {
    StatsRegistry reg;
    reg.counter_inc<"cr_a">(1);
    reg.counter_inc<"cr_b">(2);
    reg.counter_inc<"cr_c">(3);
    auto rows = reg.get_counter_report();
    expect(find_counter(rows, "cr_a").value).to_equal(static_cast<int64_t>(1));
    expect(find_counter(rows, "cr_b").value).to_equal(static_cast<int64_t>(2));
    expect(find_counter(rows, "cr_c").value).to_equal(static_cast<int64_t>(3));
}

TEST_CASE("get_counter_report reflects counter_reset") {
    StatsRegistry reg;
    reg.counter_inc<"cr_rst">(50);
    reg.counter_reset<"cr_rst">();
    auto rows = reg.get_counter_report();
    expect(find_counter(rows, "cr_rst").value).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("counter_inc is thread-safe across multiple threads") {
    StatsRegistry reg;
    constexpr int N_THREADS = 8;
    constexpr int N_INC = 10'000;
    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i)
        threads.emplace_back([&] {
            for (int j = 0; j < N_INC; ++j) reg.counter_inc<"cnt_thr">();
        });
    for (auto& t : threads) t.join();
    expect(reg.counter_get<"cnt_thr">()).to_equal(static_cast<int64_t>(N_THREADS * N_INC));
}

TEST_CASE("counter_ref is thread-safe across multiple threads") {
    StatsRegistry reg;
    auto* ptr = reg.counter_ref<"cnt_ref_thr">();
    constexpr int N_THREADS = 8;
    constexpr int N_INC = 10'000;
    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i)
        threads.emplace_back([ptr] {
            for (int j = 0; j < N_INC; ++j) ptr->fetch_add(1, std::memory_order_relaxed);
        });
    for (auto& t : threads) t.join();
    expect(reg.counter_get<"cnt_ref_thr">()).to_equal(static_cast<int64_t>(N_THREADS * N_INC));
}

TEST_CASE("counter_inc called from multiple threads in a tight burst remains correct") {
    StatsRegistry reg;
    constexpr int N_THREADS = 16;
    constexpr int N_INC = 1000;
    std::barrier ready(N_THREADS);
    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i) {
        threads.emplace_back([&] {
            ready.arrive_and_wait();
            for (int j = 0; j < N_INC; ++j) reg.counter_inc<"cnt_burst">();
        });
    }
    for (auto& t : threads) t.join();
    expect(reg.counter_get<"cnt_burst">()).to_equal(static_cast<int64_t>(N_THREADS * N_INC));
}

TEST_CASE("counter_set followed immediately by counter_inc reflects both") {
    StatsRegistry reg;
    reg.counter_set<"cnt_set_inc">(100);
    reg.counter_inc<"cnt_set_inc">(5);
    expect(reg.counter_get<"cnt_set_inc">()).to_equal(static_cast<int64_t>(105));
}

TEST_CASE("counter_dec below zero and back up is correct") {
    StatsRegistry reg;
    reg.counter_dec<"cnt_round_trip">(50);
    reg.counter_inc<"cnt_round_trip">(50);
    expect(reg.counter_get<"cnt_round_trip">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("get_counter_report is empty when no counters are registered") {
    StatsRegistry reg;
    expect(reg.get_counter_report().empty()).to_be_true();
}

TEST_CASE("counter_get for name never modified returns zero and registers it") {
    StatsRegistry reg;
    int64_t val = reg.counter_get<"ghost_counter">();
    expect(val).to_equal(static_cast<int64_t>(0));
    expect(reg.get_counter_report().empty()).to_be_false();
}

TEST_CASE("counter large increment and decrement does not overflow int64") {
    StatsRegistry reg;
    constexpr int64_t BIG = std::numeric_limits<int32_t>::max();
    reg.counter_inc<"cnt_big">(BIG);
    reg.counter_inc<"cnt_big">(BIG);
    expect(reg.counter_get<"cnt_big">()).to_equal(static_cast<int64_t>(BIG) * 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// make_scoped_counter
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("make_scoped_counter")

TEST_CASE("scoped counter increments on construction") {
    StatsRegistry reg;
    {
        auto sc = make_scoped_counter<"sc_cnt">(reg);
        expect(reg.counter_get<"sc_cnt">()).to_equal(static_cast<int64_t>(1));
    }
}

TEST_CASE("scoped counter decrements on destruction") {
    StatsRegistry reg;
    {
        auto sc = make_scoped_counter<"sc_dtor">(reg);
    }
    expect(reg.counter_get<"sc_dtor">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("nested scoped counters track concurrency depth") {
    StatsRegistry reg;
    {
        auto outer = make_scoped_counter<"sc_nest">(reg);
        expect(reg.counter_get<"sc_nest">()).to_equal(static_cast<int64_t>(1));
        {
            auto inner = make_scoped_counter<"sc_nest">(reg);
            expect(reg.counter_get<"sc_nest">()).to_equal(static_cast<int64_t>(2));
        }
        expect(reg.counter_get<"sc_nest">()).to_equal(static_cast<int64_t>(1));
    }
    expect(reg.counter_get<"sc_nest">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("scoped counter works with pre-existing counter value") {
    StatsRegistry reg;
    reg.counter_set<"sc_pre">(10);
    {
        auto sc = make_scoped_counter<"sc_pre">(reg);
        expect(reg.counter_get<"sc_pre">()).to_equal(static_cast<int64_t>(11));
    }
    expect(reg.counter_get<"sc_pre">()).to_equal(static_cast<int64_t>(10));
}

TEST_CASE("scoped counter decrements on exception unwind") {
    StatsRegistry reg;
    try {
        auto sc = make_scoped_counter<"sc_ex">(reg);
        throw std::runtime_error("test");
    } catch (...) {
    }
    expect(reg.counter_get<"sc_ex">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("multiple scoped counters across threads track max concurrency") {
    StatsRegistry reg;
    constexpr int N = 8;
    std::atomic<int> peak{0};
    std::atomic<int> current{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&] {
            auto sc = make_scoped_counter<"sc_peak">(reg);
            int cur = ++current;
            int old_peak = peak.load();
            while (cur > old_peak && !peak.compare_exchange_weak(old_peak, cur)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            --current;
        });
    }
    for (auto& t : threads) t.join();
    // After all threads exit, counter should be 0
    expect(reg.counter_get<"sc_peak">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("scoped counter is exception-safe through multiple exceptions") {
    StatsRegistry reg;
    for (int i = 0; i < 5; ++i) {
        try {
            auto sc = make_scoped_counter<"sc_multi_ex">(reg);
            throw std::runtime_error("loop");
        } catch (...) {
        }
    }
    expect(reg.counter_get<"sc_multi_ex">()).to_equal(static_cast<int64_t>(0));
}

// ═════════════════════════════════════════════════════════════════════════════
// GAUGES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("StatsRegistry – Gauges")

TEST_CASE("gauge count equals number of recordings") {
    StatsRegistry reg;
    reg.gauge_record<"g_cnt">(1.0);
    reg.gauge_record<"g_cnt">(2.0);
    reg.gauge_record<"g_cnt">(3.0);
    auto row = find_gauge(reg.get_gauge_report(), "g_cnt");
    expect(row.count).to_equal(static_cast<std::size_t>(3));
}

TEST_CASE("gauge total equals sum of recorded values") {
    StatsRegistry reg;
    reg.gauge_record<"g_tot">(1.0);
    reg.gauge_record<"g_tot">(2.0);
    reg.gauge_record<"g_tot">(3.0);
    auto row = find_gauge(reg.get_gauge_report(), "g_tot");
    expect(row.total).to_approx_equal(6.0);
}

TEST_CASE("gauge mean is correctly computed") {
    StatsRegistry reg;
    reg.gauge_record<"g_mean">(10.0);
    reg.gauge_record<"g_mean">(20.0);
    reg.gauge_record<"g_mean">(30.0);
    auto row = find_gauge(reg.get_gauge_report(), "g_mean");
    expect(row.mean).to_approx_equal(20.0);
}

TEST_CASE("gauge min and max are tracked correctly") {
    StatsRegistry reg;
    reg.gauge_record<"g_minmax">(5.0);
    reg.gauge_record<"g_minmax">(-3.0);
    reg.gauge_record<"g_minmax">(10.0);
    auto row = find_gauge(reg.get_gauge_report(), "g_minmax");
    expect(row.min).to_approx_equal(-3.0);
    expect(row.max).to_approx_equal(10.0);
}

TEST_CASE("gauge stddev is zero for single sample") {
    StatsRegistry reg;
    reg.gauge_record<"g_std1">(42.0);
    auto row = find_gauge(reg.get_gauge_report(), "g_std1");
    expect(row.stddev).to_approx_equal(0.0);
}

TEST_CASE("gauge stddev is zero for identical samples") {
    StatsRegistry reg;
    for (int i = 0; i < 5; ++i) reg.gauge_record<"g_stdi">(7.0);
    auto row = find_gauge(reg.get_gauge_report(), "g_stdi");
    expect(row.stddev).to_approx_equal(0.0);
}

TEST_CASE("gauge population stddev is correct for known dataset") {
    // {2,4,4,4,5,5,7,9} — population stddev = 2.0
    StatsRegistry reg;
    for (double v : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) reg.gauge_record<"g_stdknown">(v);
    auto row = find_gauge(reg.get_gauge_report(), "g_stdknown");
    expect(row.stddev).to_approx_equal(2.0, 1e-9);
}

TEST_CASE("gauge handles negative values") {
    StatsRegistry reg;
    reg.gauge_record<"g_neg">(-100.0);
    reg.gauge_record<"g_neg">(-200.0);
    auto row = find_gauge(reg.get_gauge_report(), "g_neg");
    expect(row.mean).to_approx_equal(-150.0);
    expect(row.min).to_approx_equal(-200.0);
    expect(row.max).to_approx_equal(-100.0);
}

TEST_CASE("gauge handles very large and very small values without crashing") {
    StatsRegistry reg;
    reg.gauge_record<"g_extreme">(1e15);
    reg.gauge_record<"g_extreme">(1e-15);
    auto row = find_gauge(reg.get_gauge_report(), "g_extreme");
    expect(row.count).to_equal(static_cast<std::size_t>(2));
}

TEST_CASE("get_gauge_report is empty when no gauges recorded") {
    StatsRegistry reg;
    expect(reg.get_gauge_report().empty()).to_be_true();
}

TEST_CASE("multiple distinct gauges are independent") {
    StatsRegistry reg;
    reg.gauge_record<"g_ia">(1.0);
    reg.gauge_record<"g_ib">(100.0);
    expect(find_gauge(reg.get_gauge_report(), "g_ia").mean).to_approx_equal(1.0);
    expect(find_gauge(reg.get_gauge_report(), "g_ib").mean).to_approx_equal(100.0);
}

TEST_CASE("gauge_reset clears all accumulated data — gauge absent from report") {
    StatsRegistry reg;
    reg.gauge_record<"g_rst">(100.0);
    reg.gauge_reset<"g_rst">();
    for (const auto& r : reg.get_gauge_report()) expect(r.name == "g_rst").to_be_false();
}

TEST_CASE("gauge records after reset accumulate fresh statistics") {
    StatsRegistry reg;
    reg.gauge_record<"g_rstfr">(50.0);
    reg.gauge_reset<"g_rstfr">();
    reg.gauge_record<"g_rstfr">(10.0);
    reg.gauge_record<"g_rstfr">(20.0);
    auto row = find_gauge(reg.get_gauge_report(), "g_rstfr");
    expect(row.count).to_equal(static_cast<std::size_t>(2));
    expect(row.mean).to_approx_equal(15.0);
}

TEST_CASE("gauge_record is thread-safe") {
    StatsRegistry reg;
    constexpr int N_THREADS = 8;
    constexpr int N_RECS = 1000;
    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i)
        threads.emplace_back([&] {
            for (int j = 0; j < N_RECS; ++j) reg.gauge_record<"g_thr">(1.0);
        });
    for (auto& t : threads) t.join();
    auto row = find_gauge(reg.get_gauge_report(), "g_thr");
    expect(row.count).to_equal(static_cast<std::size_t>(N_THREADS * N_RECS));
}

TEST_CASE("gauge single sample has mean equal to the sample") {
    StatsRegistry reg;
    reg.gauge_record<"g_single">(77.5);
    auto row = find_gauge(reg.get_gauge_report(), "g_single");
    expect(row.mean).to_approx_equal(77.5);
    expect(row.min).to_approx_equal(77.5);
    expect(row.max).to_approx_equal(77.5);
}

TEST_CASE("gauge total is correct after many records") {
    StatsRegistry reg;
    double expected = 0.0;
    for (int i = 1; i <= 100; ++i) {
        reg.gauge_record<"g_total100">(static_cast<double>(i));
        expected += static_cast<double>(i);
    }
    auto row = find_gauge(reg.get_gauge_report(), "g_total100");
    expect(row.total).to_approx_equal(expected, 1e-6);
}

TEST_CASE("gauge sample_stddev is larger than population stddev") {
    StatsRegistry reg;
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0}) reg.gauge_record<"g_ssd">(v);
    auto row = find_gauge(reg.get_gauge_report(), "g_ssd");
    expect(row.sample_stddev > row.stddev).to_be_true();
}

TEST_CASE("gauge records zero correctly") {
    StatsRegistry reg;
    reg.gauge_record<"g_zero">(0.0);
    reg.gauge_record<"g_zero">(0.0);
    auto row = find_gauge(reg.get_gauge_report(), "g_zero");
    expect(row.mean).to_approx_equal(0.0);
    expect(row.stddev).to_approx_equal(0.0);
}

TEST_CASE("gauge is thread-safe mean is correct under concurrency") {
    StatsRegistry reg;
    // All threads record the same value — mean must equal that value
    constexpr int N_THREADS = 4;
    constexpr int N_RECS = 500;
    constexpr double VAL = 10.0;
    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i)
        threads.emplace_back([&] {
            for (int j = 0; j < N_RECS; ++j) reg.gauge_record<"g_thr_mean">(VAL);
        });
    for (auto& t : threads) t.join();
    auto row = find_gauge(reg.get_gauge_report(), "g_thr_mean");
    expect(row.count).to_equal(static_cast<std::size_t>(N_THREADS * N_RECS));
    expect(row.mean).to_approx_equal(VAL, 1e-9);
    expect(row.stddev).to_approx_equal(0.0, 1e-9);
}

TEST_CASE("gauge report not empty after recording") {
    StatsRegistry reg;
    reg.gauge_record<"g_nonempty">(1.0);
    expect(reg.get_gauge_report().empty()).to_be_false();
}

// ═════════════════════════════════════════════════════════════════════════════
// HISTOGRAMS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("StatsRegistry – Histograms")

TEST_CASE("histogram_create succeeds for valid parameters") {
    StatsRegistry reg;
    expect_no_throw(reg.histogram_create<"h_ok">(0.0, 10.0, 5));
}

TEST_CASE("histogram_create with same bounds is a no-op") {
    StatsRegistry reg;
    reg.histogram_create<"h_dup">(0.0, 10.0);
    expect_no_throw(reg.histogram_create<"h_dup">(0.0, 10.0));
}

TEST_CASE("histogram_create throws for duplicate name with different bounds") {
    StatsRegistry reg;
    reg.histogram_create<"h_dup2">(0.0, 10.0);
    expect_throws(std::logic_error, reg.histogram_create<"h_dup2">(0.0, 20.0));
}

TEST_CASE("histogram_create throws when low >= high") {
    StatsRegistry reg;
    expect_throws(std::invalid_argument, reg.histogram_create<"h_lohi">(10.0, 0.0));
}

TEST_CASE("histogram_create throws when low == high") {
    StatsRegistry reg;
    expect_throws(std::invalid_argument, reg.histogram_create<"h_lohi2">(5.0, 5.0));
}

TEST_CASE("histogram_create throws for zero buckets") {
    StatsRegistry reg;
    expect_throws(std::invalid_argument, reg.histogram_create<"h_zb">(0.0, 1.0, 0));
}

TEST_CASE("histogram total count equals number of records") {
    StatsRegistry reg;
    reg.histogram_create<"h_tot">(0.0, 10.0, 5);
    for (int i = 0; i < 7; ++i) reg.histogram_record<"h_tot">(static_cast<double>(i));
    auto row = find_histogram(reg.get_histogram_report(), "h_tot");
    expect(row.total).to_equal(static_cast<std::size_t>(7));
}

TEST_CASE("histogram underflow counts values below low") {
    StatsRegistry reg;
    reg.histogram_create<"h_uf">(5.0, 15.0, 5);
    reg.histogram_record<"h_uf">(3.0);
    reg.histogram_record<"h_uf">(-1.0);
    auto row = find_histogram(reg.get_histogram_report(), "h_uf");
    expect(row.underflow).to_equal(static_cast<std::size_t>(2));
}

TEST_CASE("histogram overflow counts values at or above high") {
    StatsRegistry reg;
    reg.histogram_create<"h_of">(0.0, 10.0, 5);
    reg.histogram_record<"h_of">(10.0);  // == high → overflow
    reg.histogram_record<"h_of">(100.0);
    auto row = find_histogram(reg.get_histogram_report(), "h_of");
    expect(row.overflow).to_equal(static_cast<std::size_t>(2));
}

TEST_CASE("histogram value exactly at low goes into first bucket") {
    StatsRegistry reg;
    reg.histogram_create<"h_lo">(0.0, 10.0, 5);
    reg.histogram_record<"h_lo">(0.0);
    auto row = find_histogram(reg.get_histogram_report(), "h_lo");
    expect(row.underflow).to_equal(static_cast<std::size_t>(0));
    expect(row.buckets[0].count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("histogram bucket counts sum to in-range total") {
    StatsRegistry reg;
    reg.histogram_create<"h_bsum">(0.0, 10.0, 5);
    for (double v : {0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5}) reg.histogram_record<"h_bsum">(v);
    auto row = find_histogram(reg.get_histogram_report(), "h_bsum");
    std::size_t bucket_sum = 0;
    for (const auto& b : row.buckets) bucket_sum += b.count;
    expect(bucket_sum).to_equal(row.total - row.underflow - row.overflow);
}

TEST_CASE("histogram bucket percentages sum to 100 when all values are in-range") {
    StatsRegistry reg;
    reg.histogram_create<"h_pct">(0.0, 10.0, 5);
    for (double v : {1.0, 3.0, 5.0, 7.0, 9.0}) reg.histogram_record<"h_pct">(v);
    auto row = find_histogram(reg.get_histogram_report(), "h_pct");
    double pct_sum = 0.0;
    for (const auto& b : row.buckets) pct_sum += b.pct;
    expect(pct_sum).to_approx_equal(100.0, 1e-3);
}

TEST_CASE("histogram correctly assigns values to individual buckets") {
    // 10 buckets over [0,10): value i+0.5 should land in bucket i
    StatsRegistry reg;
    reg.histogram_create<"h_buckets">(0.0, 10.0, 10);
    for (int i = 0; i < 10; ++i) reg.histogram_record<"h_buckets">(static_cast<double>(i) + 0.5);
    auto row = find_histogram(reg.get_histogram_report(), "h_buckets");
    for (std::size_t i = 0; i < 10; ++i) expect(row.buckets[i].count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("histogram with single bucket still works") {
    StatsRegistry reg;
    reg.histogram_create<"h_1b">(0.0, 10.0, 1);
    reg.histogram_record<"h_1b">(5.0);
    auto row = find_histogram(reg.get_histogram_report(), "h_1b");
    expect(row.buckets.size()).to_equal(static_cast<std::size_t>(1));
    expect(row.buckets[0].count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("histogram_reset zeroes all counts") {
    StatsRegistry reg;
    reg.histogram_create<"h_rst">(0.0, 10.0, 5);
    reg.histogram_record<"h_rst">(5.0);
    reg.histogram_reset<"h_rst">();
    auto row = find_histogram(reg.get_histogram_report(), "h_rst");
    expect(row.total).to_equal(static_cast<std::size_t>(0));
    expect(row.underflow).to_equal(static_cast<std::size_t>(0));
    expect(row.overflow).to_equal(static_cast<std::size_t>(0));
    for (const auto& b : row.buckets) expect(b.count).to_equal(static_cast<std::size_t>(0));
}

TEST_CASE("histogram records after reset accumulate fresh data") {
    StatsRegistry reg;
    reg.histogram_create<"h_rstfr">(0.0, 10.0, 2);
    reg.histogram_record<"h_rstfr">(3.0);
    reg.histogram_reset<"h_rstfr">();
    reg.histogram_record<"h_rstfr">(7.0);
    auto row = find_histogram(reg.get_histogram_report(), "h_rstfr");
    expect(row.total).to_equal(static_cast<std::size_t>(1));
    expect(row.buckets[1].count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("multiple distinct histograms are independent") {
    StatsRegistry reg;
    reg.histogram_create<"h_ia">(0.0, 10.0);
    reg.histogram_create<"h_ib">(0.0, 10.0);
    reg.histogram_record<"h_ia">(1.0);
    reg.histogram_record<"h_ia">(2.0);
    reg.histogram_record<"h_ib">(5.0);
    expect(find_histogram(reg.get_histogram_report(), "h_ia").total).to_equal(static_cast<std::size_t>(2));
    expect(find_histogram(reg.get_histogram_report(), "h_ib").total).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("get_histogram_report is empty when no histograms exist") {
    StatsRegistry reg;
    expect(reg.get_histogram_report().empty()).to_be_true();
}

TEST_CASE("histogram_record is thread-safe") {
    StatsRegistry reg;
    reg.histogram_create<"h_thr">(0.0, 1000.0, 10);
    constexpr int N_THREADS = 6;
    constexpr int N_RECS = 500;
    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i)
        threads.emplace_back([&] {
            for (int j = 0; j < N_RECS; ++j) reg.histogram_record<"h_thr">(static_cast<double>(j % 1000));
        });
    for (auto& t : threads) t.join();
    auto row = find_histogram(reg.get_histogram_report(), "h_thr");
    expect(row.total).to_equal(static_cast<std::size_t>(N_THREADS * N_RECS));
}

TEST_CASE("histogram bucket low and high boundaries are contiguous") {
    StatsRegistry reg;
    reg.histogram_create<"h_contiguous">(0.0, 10.0, 5);
    reg.histogram_record<"h_contiguous">(5.0);
    auto row = find_histogram(reg.get_histogram_report(), "h_contiguous");
    expect(row.buckets.size()).to_equal(static_cast<std::size_t>(5));
    for (std::size_t i = 1; i < row.buckets.size(); ++i) expect(std::abs(row.buckets[i].low - row.buckets[i - 1].high) < 1e-9).to_be_true();
}

TEST_CASE("histogram first bucket low equals histogram low") {
    StatsRegistry reg;
    reg.histogram_create<"h_first_low">(2.5, 7.5, 5);
    reg.histogram_record<"h_first_low">(3.0);
    auto row = find_histogram(reg.get_histogram_report(), "h_first_low");
    expect(std::abs(row.buckets.front().low - 2.5) < 1e-9).to_be_true();
}

TEST_CASE("histogram last bucket high equals histogram high") {
    StatsRegistry reg;
    reg.histogram_create<"h_last_high">(0.0, 5.0, 5);
    reg.histogram_record<"h_last_high">(4.5);
    auto row = find_histogram(reg.get_histogram_report(), "h_last_high");
    expect(std::abs(row.buckets.back().high - 5.0) < 1e-9).to_be_true();
}

TEST_CASE("histogram underflow plus overflow plus bucket sum equals total") {
    StatsRegistry reg;
    reg.histogram_create<"h_full_sum">(5.0, 10.0, 5);
    for (double v : {1.0, 5.0, 6.0, 7.0, 10.0, 11.0, 15.0}) reg.histogram_record<"h_full_sum">(v);
    auto row = find_histogram(reg.get_histogram_report(), "h_full_sum");
    std::size_t bucket_sum = 0;
    for (const auto& b : row.buckets) bucket_sum += b.count;
    expect(bucket_sum + row.underflow + row.overflow).to_equal(row.total);
}

TEST_CASE("histogram all values in underflow") {
    StatsRegistry reg;
    reg.histogram_create<"h_all_uf">(100.0, 200.0, 5);
    for (int i = 0; i < 10; ++i) reg.histogram_record<"h_all_uf">(static_cast<double>(i));
    auto row = find_histogram(reg.get_histogram_report(), "h_all_uf");
    expect(row.underflow).to_equal(static_cast<std::size_t>(10));
    expect(row.overflow).to_equal(static_cast<std::size_t>(0));
    std::size_t bucket_sum = 0;
    for (const auto& b : row.buckets) bucket_sum += b.count;
    expect(bucket_sum).to_equal(static_cast<std::size_t>(0));
}

TEST_CASE("histogram all values in overflow") {
    StatsRegistry reg;
    reg.histogram_create<"h_all_of">(0.0, 1.0, 5);
    for (int i = 2; i < 12; ++i) reg.histogram_record<"h_all_of">(static_cast<double>(i));
    auto row = find_histogram(reg.get_histogram_report(), "h_all_of");
    expect(row.overflow).to_equal(static_cast<std::size_t>(10));
    expect(row.underflow).to_equal(static_cast<std::size_t>(0));
}

TEST_CASE("histogram pct is zero for empty buckets") {
    StatsRegistry reg;
    reg.histogram_create<"h_zeropct">(0.0, 10.0, 5);
    reg.histogram_record<"h_zeropct">(0.5);  // only bucket 0 filled
    auto row = find_histogram(reg.get_histogram_report(), "h_zeropct");
    for (std::size_t i = 1; i < row.buckets.size(); ++i) expect(row.buckets[i].pct).to_approx_equal(0.0);
}

TEST_CASE("histogram with many buckets and many records is consistent") {
    StatsRegistry reg;
    constexpr std::size_t N_BUCKETS = 16;
    reg.histogram_create<"h_many">(0.0, 160.0, N_BUCKETS);
    for (int i = 0; i < 160; ++i) reg.histogram_record<"h_many">(static_cast<double>(i));
    auto row = find_histogram(reg.get_histogram_report(), "h_many");
    expect(row.total).to_equal(static_cast<std::size_t>(160));
    expect(row.underflow).to_equal(static_cast<std::size_t>(0));
    expect(row.overflow).to_equal(static_cast<std::size_t>(0));
    for (const auto& b : row.buckets) expect(b.count).to_equal(static_cast<std::size_t>(10));
}

TEST_CASE("histogram_record before histogram_create succeeds via lazy-init") {
    StatsRegistry reg;
    expect_no_throw(reg.histogram_record<"uncreated_hist2">(0.5));
    auto rows = reg.get_histogram_report();
    bool found = false;
    for (const auto& row : rows)
        if (row.name == "uncreated_hist2") found = true;
    expect(found).to_be_true();
}

TEST_CASE("histogram name appears in report even with only underflow and overflow") {
    StatsRegistry reg;
    reg.histogram_create<"h_edge_only">(5.0, 6.0, 2);
    reg.histogram_record<"h_edge_only">(0.0);   // underflow
    reg.histogram_record<"h_edge_only">(10.0);  // overflow
    auto rows = reg.get_histogram_report();
    bool found = false;
    for (const auto& r : rows)
        if (r.name == "h_edge_only") found = true;
    expect(found).to_be_true();
}

// ═════════════════════════════════════════════════════════════════════════════
// GLOBAL SINGLETON
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("StatsRegistry – global_stats singleton")

TEST_CASE("global_stats returns the same instance on every call") {
    auto& a = global_stats();
    auto& b = global_stats();
    expect(&a).to_equal(&b);
}

TEST_CASE("STATS_REG macro refers to the same instance as global_stats()") { expect(&STATS_REG).to_equal(&global_stats()); }

TEST_CASE("global_stats instance is distinct from a local StatsRegistry") {
    StatsRegistry local;
    expect(&local).not_to_equal(&global_stats());
}

// ═════════════════════════════════════════════════════════════════════════════
// INSTANCE ISOLATION
// Two separate StatsRegistry instances share CtStatID slot indices (global
// template), but each has its own independent storage arrays.
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("StatsRegistry – Instance Isolation")

TEST_CASE("counter in reg1 does not affect reg2") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.counter_inc<"iso_cnt">(42);
    // reg2 has separate storage — its slot is still 0
    expect(reg2.counter_get<"iso_cnt">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("counter mutations in two instances are independent") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.counter_inc<"iso_cnt2">(10);
    reg2.counter_inc<"iso_cnt2">(20);
    expect(reg1.counter_get<"iso_cnt2">()).to_equal(static_cast<int64_t>(10));
    expect(reg2.counter_get<"iso_cnt2">()).to_equal(static_cast<int64_t>(20));
}

TEST_CASE("gauge data in reg1 does not appear in reg2") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.gauge_record<"iso_g">(99.0);
    expect(reg2.get_gauge_report().empty()).to_be_true();
}

TEST_CASE("gauge mutations in two instances are independent") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.gauge_record<"iso_g2">(1.0);
    reg2.gauge_record<"iso_g2">(2.0);
    expect(find_gauge(reg1.get_gauge_report(), "iso_g2").mean).to_approx_equal(1.0);
    expect(find_gauge(reg2.get_gauge_report(), "iso_g2").mean).to_approx_equal(2.0);
}

TEST_CASE("histogram_create in reg1 does not appear in reg2") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.histogram_create<"iso_h">(0.0, 10.0);
    expect(reg2.get_histogram_report().empty()).to_be_true();
}

TEST_CASE("histogram_create with same name in two instances does not conflict") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.histogram_create<"iso_h2">(0.0, 10.0);
    expect_no_throw(reg2.histogram_create<"iso_h2">(0.0, 10.0));
}

TEST_CASE("histogram records in two instances are independent") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.histogram_create<"iso_h3">(0.0, 10.0);
    reg2.histogram_create<"iso_h3">(0.0, 10.0);
    reg1.histogram_record<"iso_h3">(5.0);
    reg1.histogram_record<"iso_h3">(5.0);
    reg2.histogram_record<"iso_h3">(5.0);
    expect(find_histogram(reg1.get_histogram_report(), "iso_h3").total).to_equal(static_cast<std::size_t>(2));
    expect(find_histogram(reg2.get_histogram_report(), "iso_h3").total).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("counter reset in reg1 does not affect reg2") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.counter_inc<"iso_rst">(5);
    reg2.counter_inc<"iso_rst">(5);
    reg1.counter_reset<"iso_rst">();
    expect(reg1.counter_get<"iso_rst">()).to_equal(static_cast<int64_t>(0));
    expect(reg2.counter_get<"iso_rst">()).to_equal(static_cast<int64_t>(5));
}

TEST_CASE("gauge reset in reg1 does not affect reg2") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.gauge_record<"iso_grst">(10.0);
    reg2.gauge_record<"iso_grst">(10.0);
    reg1.gauge_reset<"iso_grst">();
    bool r1_found = false;
    for (const auto& r : reg1.get_gauge_report())
        if (r.name == "iso_grst") r1_found = true;
    expect(r1_found).to_be_false();
    expect(find_gauge(reg2.get_gauge_report(), "iso_grst").count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("scoped counter operates on its own registry only") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    {
        auto sc = make_scoped_counter<"iso_sc">(reg1);
        expect(reg1.counter_get<"iso_sc">()).to_equal(static_cast<int64_t>(1));
        expect(reg2.counter_get<"iso_sc">()).to_equal(static_cast<int64_t>(0));
    }
    expect(reg1.counter_get<"iso_sc">()).to_equal(static_cast<int64_t>(0));
    expect(reg2.counter_get<"iso_sc">()).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("timer data does not appear in stats counter report") {
    StatsRegistry reg;
    reg.start<"only_timer">();
    reg.stop<"only_timer">();
    expect(reg.get_counter_report().empty()).to_be_true();
}

TEST_CASE("timer data does not appear in gauge report") {
    StatsRegistry reg;
    reg.start<"only_timer2">();
    reg.stop<"only_timer2">();
    expect(reg.get_gauge_report().empty()).to_be_true();
}

TEST_CASE("timer data does not appear in histogram report") {
    StatsRegistry reg;
    reg.start<"only_timer3">();
    reg.stop<"only_timer3">();
    expect(reg.get_histogram_report().empty()).to_be_true();
}

// ═════════════════════════════════════════════════════════════════════════════
// STATSREGISTRY – ADVANCED & EDGE CASES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("StatsRegistry – Advanced & Edge Cases")

TEST_CASE("using same name for different primitive types throws") {
    StatsRegistry reg;
    reg.counter_inc<"conflict_name">();
    expect_throws(std::logic_error, reg.gauge_record<"conflict_name">(1.0));
    expect_throws(std::logic_error, reg.histogram_create<"conflict_name">(0, 1));
}

TEST_CASE("using same name for different primitive types throws (gauge first)") {
    StatsRegistry reg;
    reg.gauge_record<"conflict_name2">(1.0);
    expect_throws(std::logic_error, reg.counter_inc<"conflict_name2">());
    expect_throws(std::logic_error, reg.histogram_create<"conflict_name2">(0, 1));
}

TEST_CASE("using same name for different primitive types throws (histogram first)") {
    StatsRegistry reg;
    reg.histogram_create<"conflict_name3">(0, 1);
    expect_throws(std::logic_error, reg.counter_inc<"conflict_name3">());
    expect_throws(std::logic_error, reg.gauge_record<"conflict_name3">(1.0));
}

TEST_CASE("histogram_record without prior histogram_create succeeds via lazy-init") {
    StatsRegistry reg;
    expect_no_throw(reg.histogram_record<"uncreated_hist">(0.5));
}

TEST_CASE("counter_get auto-registers the counter for reports") {
    StatsRegistry reg;
    (void)reg.counter_get<"unreported_counter">();
    auto rows = reg.get_counter_report();
    bool found = false;
    for (const auto& r : rows) {
        if (r.name == "unreported_counter") {
            found = true;
        }
    }
    expect(found).to_be_true();
}

TEST_CASE("counter set to zero appears in reports") {
    StatsRegistry reg;
    reg.counter_set<"reported_zero_counter">(0);
    auto row = find_counter(reg.get_counter_report(), "reported_zero_counter");
    expect(row.value).to_equal(static_cast<int64_t>(0));
}

TEST_CASE("concurrent histogram_create with same bounds all succeed") {
    StatsRegistry reg;
    std::atomic<int> success_count = 0;
    constexpr int N_THREADS = 4;
    std::barrier sync_point(N_THREADS);

    auto task = [&] {
        sync_point.arrive_and_wait();
        try {
            reg.histogram_create<"concurrent_hist">(0.0, 10.0, 10);
            success_count++;
        } catch (...) {
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i) {
        threads.emplace_back(task);
    }
    for (auto& thr : threads) {
        thr.join();
    }

    expect(success_count.load()).to_equal(N_THREADS);
}

TEST_CASE("histogram correctly buckets value just below upper bound") {
    StatsRegistry reg;
    reg.histogram_create<"h_boundary">(0.0, 10.0, 10);  // Buckets are [0,1), [1,2), ... [9,10)

    // Value very close to the overall upper bound
    double near_high = std::nextafter(10.0, 0.0);
    reg.histogram_record<"h_boundary">(near_high);

    // Value very close to an internal bucket boundary
    double near_bucket_boundary = std::nextafter(9.0, 0.0);  // Should go in bucket [8,9)
    reg.histogram_record<"h_boundary">(near_bucket_boundary);

    auto row = find_histogram(reg.get_histogram_report(), "h_boundary");
    expect(row.buckets[9].count).to_equal(static_cast<std::size_t>(1));
    expect(row.buckets[8].count).to_equal(static_cast<std::size_t>(1));
    expect(row.overflow).to_equal(static_cast<std::size_t>(0));
}

TEST_CASE("print_stats_report does not crash with single timer") {
    StatsRegistry reg;
    reg.start<"print_single">();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    reg.stop<"print_single">();
    SuppressStdout suppress;
    expect_no_throw(reg.print_stats_report());
}

TEST_CASE("print_counter_report does not crash with registered counter") {
    StatsRegistry reg;
    reg.counter_inc<"print_cnt">();
    SuppressStdout suppress;
    expect_no_throw(reg.print_counter_report());
}

TEST_CASE("print_gauge_report does not crash with recorded gauge") {
    StatsRegistry reg;
    reg.gauge_record<"print_gauge">(1.0);
    SuppressStdout suppress;
    expect_no_throw(reg.print_gauge_report());
}

TEST_CASE("print_histogram_report does not crash with valid histogram") {
    StatsRegistry reg;
    reg.histogram_create<"print_hist">(0.0, 10.0, 5);
    reg.histogram_record<"print_hist">(5.0);
    SuppressStdout suppress;
    expect_no_throw(reg.print_histogram_report());
}

TEST_CASE("print_all_reports does not crash when all primitives are populated") {
    StatsRegistry reg;
    reg.start<"all_t">();
    reg.stop<"all_t">();
    reg.counter_inc<"all_c">();
    reg.gauge_record<"all_g">(1.0);
    reg.histogram_create<"all_h">(0.0, 10.0);
    reg.histogram_record<"all_h">(5.0);
    SuppressStdout suppress;
    expect_no_throw(reg.print_all_reports());
}

TEST_CASE("print_totals_report (simple) does not crash with used timer") {
    TimerRegistry reg;
    reg.start<"print_rep">();
    reg.stop<"print_rep">();
    SuppressStdout suppress;
    expect_no_throw(reg.print_totals_report());
}

TEST_CASE("print_thread_report does not crash") {
    TimerRegistry reg;
    reg.start<"print_pt">();
    reg.stop<"print_pt">();
    SuppressStdout suppress;
    expect_no_throw(reg.print_thread_report());
}

TEST_CASE("all three primitive types can coexist in the same registry with different names") {
    StatsRegistry reg;
    expect_no_throw({
        reg.counter_inc<"coexist_c">(1);
        reg.gauge_record<"coexist_g">(1.0);
        reg.histogram_create<"coexist_h">(0.0, 1.0);
        reg.histogram_record<"coexist_h">(0.5);
        reg.start<"coexist_t">();
        reg.stop<"coexist_t">();
    });
    expect(reg.counter_get<"coexist_c">()).to_equal(static_cast<int64_t>(1));
    expect(find_gauge(reg.get_gauge_report(), "coexist_g").count).to_equal(static_cast<std::size_t>(1));
    expect(find_histogram(reg.get_histogram_report(), "coexist_h").total).to_equal(static_cast<std::size_t>(1));
    expect(find_timer(reg.get_stats_report(), "coexist_t").call_count).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("timer and stats registries share no state through inheritance") {
    StatsRegistry reg;
    reg.start<"only_timer_sr">();
    reg.stop<"only_timer_sr">();
    expect(reg.get_counter_report().empty()).to_be_true();
    expect(reg.get_gauge_report().empty()).to_be_true();
    expect(reg.get_histogram_report().empty()).to_be_true();
}

TEST_CASE("concurrent gauge and counter records do not deadlock") {
    StatsRegistry reg;
    constexpr int N = 4;
    constexpr int REPS = 500;
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < REPS; ++j) {
                reg.gauge_record<"concurrent_gc">(1.0);
                reg.counter_inc<"concurrent_gc_cnt">();
            }
        });
    }
    for (auto& t : threads) t.join();
    expect(reg.counter_get<"concurrent_gc_cnt">()).to_equal(static_cast<int64_t>(N * REPS));
    expect(find_gauge(reg.get_gauge_report(), "concurrent_gc").count).to_equal(static_cast<std::size_t>(N * REPS));
}

TEST_CASE("get_stats_report returns correct thread_count for mixed live and exited threads") {
    TimerRegistry reg;
    // Start 2 threads and let them exit, then record from the current thread
    std::vector<std::thread> threads;
    for (int i = 0; i < 2; ++i) {
        threads.emplace_back([&] {
            reg.start<"mixed_tc">();
            reg.stop<"mixed_tc">();
        });
    }
    for (auto& t : threads) t.join();
    reg.start<"mixed_tc">();
    reg.stop<"mixed_tc">();
    auto row = find_timer(reg.get_stats_report(), "mixed_tc");
    expect(row.call_count).to_equal(static_cast<std::size_t>(3));
    expect(row.thread_count).to_equal(static_cast<std::size_t>(3));
}

// ═════════════════════════════════════════════════════════════════════════════
// SERIES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("StatsRegistry – Series")

TEST_CASE("series_push single-thread preserves insertion order") {
    StatsRegistry reg;
    reg.series_push<"sr_order">(1.0);
    reg.series_push<"sr_order">(2.0);
    reg.series_push<"sr_order">(3.0);
    const auto pts = reg.series_get<"sr_order">();
    expect(pts.size()).to_equal(static_cast<std::size_t>(3));
    expect(pts[0].value).to_equal(1.0);
    expect(pts[1].value).to_equal(2.0);
    expect(pts[2].value).to_equal(3.0);
}

TEST_CASE("series_push timestamps are monotonically non-decreasing") {
    StatsRegistry reg;
    for (int i = 0; i < 10; ++i) {
        reg.series_push<"sr_mono">(static_cast<double>(i));
    }
    const auto pts = reg.series_get<"sr_mono">();
    for (std::size_t i = 1; i < pts.size(); ++i) {
        expect(pts[i].timestamp_ns >= pts[i - 1].timestamp_ns).to_be_true();
    }
}

TEST_CASE("series_reset clears all stored points") {
    StatsRegistry reg;
    reg.series_push<"sr_reset">(1.0);
    reg.series_push<"sr_reset">(2.0);
    reg.series_reset<"sr_reset">();
    const auto pts = reg.series_get<"sr_reset">();
    expect(pts.empty()).to_be_true();
}

TEST_CASE("series_push after series_reset starts a fresh sequence") {
    StatsRegistry reg;
    reg.series_push<"sr_reset2">(99.0);
    reg.series_reset<"sr_reset2">();
    reg.series_push<"sr_reset2">(1.0);
    reg.series_push<"sr_reset2">(2.0);
    const auto pts = reg.series_get<"sr_reset2">();
    expect(pts.size()).to_equal(static_cast<std::size_t>(2));
    expect(pts[0].value).to_equal(1.0);
    expect(pts[1].value).to_equal(2.0);
}

TEST_CASE("get_series_report is empty when no series have been pushed") {
    StatsRegistry reg;
    expect(reg.get_series_report().empty()).to_be_true();
}

TEST_CASE("get_series_report omits series with no pushes after registration") {
    // series_reset without any prior push should not register the series
    StatsRegistry reg;
    reg.series_reset<"sr_empty_after_reset">();
    expect(reg.get_series_report().empty()).to_be_true();
}

TEST_CASE("get_series_report returns all registered series") {
    StatsRegistry reg;
    reg.series_push<"sr_multi_a">(1.0);
    reg.series_push<"sr_multi_b">(2.0);
    const auto rows = reg.get_series_report();
    expect(rows.size()).to_equal(static_cast<std::size_t>(2));
    const auto row_a = find_series(rows, "sr_multi_a");
    const auto row_b = find_series(rows, "sr_multi_b");
    expect(row_a.points.size()).to_equal(static_cast<std::size_t>(1));
    expect(row_b.points.size()).to_equal(static_cast<std::size_t>(1));
}

TEST_CASE("series points are sorted by timestamp in get_series_report") {
    StatsRegistry reg;
    reg.series_push<"sr_sorted">(10.0);
    reg.series_push<"sr_sorted">(20.0);
    reg.series_push<"sr_sorted">(30.0);
    const auto rows = reg.get_series_report();
    const auto row = find_series(rows, "sr_sorted");
    for (std::size_t i = 1; i < row.points.size(); ++i) {
        expect(row.points[i].timestamp_ns >= row.points[i - 1].timestamp_ns).to_be_true();
    }
}

TEST_CASE("concurrent series_push from multiple threads has correct total count") {
    StatsRegistry reg;
    constexpr int N_THREADS = 4;
    constexpr int PUSHES_PER_THREAD = 250;
    std::vector<std::thread> threads;
    for (int i = 0; i < N_THREADS; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < PUSHES_PER_THREAD; ++j) {
                reg.series_push<"sr_concurrent">(static_cast<double>(j));
            }
        });
    }
    for (auto& thr : threads) thr.join();
    const auto pts = reg.series_get<"sr_concurrent">();
    expect(pts.size()).to_equal(static_cast<std::size_t>(N_THREADS * PUSHES_PER_THREAD));
}

TEST_CASE("series data from exited thread is preserved in report") {
    StatsRegistry reg;
    {
        std::thread worker([&] {
            reg.series_push<"sr_exited">(42.0);
        });
        worker.join();
    }
    const auto pts = reg.series_get<"sr_exited">();
    expect(pts.size()).to_equal(static_cast<std::size_t>(1));
    expect(pts[0].value).to_equal(42.0);
}

TEST_CASE("series_reset clears data from exited threads") {
    StatsRegistry reg;
    {
        std::thread worker([&] {
            reg.series_push<"sr_exited_reset">(10.0);
        });
        worker.join();
    }
    reg.series_reset<"sr_exited_reset">();
    reg.series_push<"sr_exited_reset">(99.0);
    const auto pts = reg.series_get<"sr_exited_reset">();
    expect(pts.size()).to_equal(static_cast<std::size_t>(1));
    expect(pts[0].value).to_equal(99.0);
}

TEST_CASE("series name registered as different primitive type throws") {
    StatsRegistry reg;
    reg.series_push<"sr_kind_conflict">(1.0);
    expect_throws(std::logic_error, reg.counter_inc<"sr_kind_conflict">());
}

TEST_CASE("series registry isolation — two instances are independent") {
    StatsRegistry reg1;
    StatsRegistry reg2;
    reg1.series_push<"sr_iso">(1.0);
    expect(reg1.get_series_report().size()).to_equal(static_cast<std::size_t>(1));
    expect(reg2.get_series_report().empty()).to_be_true();
}

TEST_CASE("series_report_to_str does not crash with populated series") {
    StatsRegistry reg;
    reg.series_push<"sr_str">(1.0);
    reg.series_push<"sr_str">(2.0);
    SuppressStdout suppress;
    expect_no_throw(reg.print_series_report());
}

TEST_CASE("series_report_to_str is empty when no series registered") {
    StatsRegistry reg;
    expect(reg.series_report_to_str().empty()).to_be_true();
}

TEST_CASE("series coexists with counter gauge histogram in the same registry") {
    StatsRegistry reg;
    reg.series_push<"sr_coexist">(3.14);
    reg.counter_inc<"sr_coexist_c">();
    reg.gauge_record<"sr_coexist_g">(1.0);
    reg.histogram_create<"sr_coexist_h">(0.0, 1.0);
    reg.histogram_record<"sr_coexist_h">(0.5);
    expect(find_series(reg.get_series_report(), "sr_coexist").points.size()).to_equal(static_cast<std::size_t>(1));
    expect(reg.counter_get<"sr_coexist_c">()).to_equal(static_cast<int64_t>(1));
}

TEST_CASE("print_all_reports includes series section without crash") {
    StatsRegistry reg;
    reg.series_push<"sr_all_reports">(1.0);
    SuppressStdout suppress;
    expect_no_throw(reg.print_all_reports());
}