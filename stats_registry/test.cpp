/**
 * test_stats_registry.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Full test suite for TimerRegistry and StatsRegistry (ct_string API).
 *
 * Compile (C++20):
 *   g++ -std=c++20 -O2 -pthread test_stats_registry.cpp -o test_stats && ./test_stats
 */

#include <atomic>
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

// ═════════════════════════════════════════════════════════════════════════════
// TIMER REGISTRY — compile-time API
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("TimerRegistry – ct_string API")

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

TEST_CASE("get_stats_report_per_thread has one row per thread") {
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
    auto rows = reg.get_stats_report_per_thread();
    std::size_t count = 0;
    for (const auto& r : rows)
        if (r.name == "pt") ++count;
    expect(count).to_equal(static_cast<std::size_t>(N));
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

// ─────────────────────────────────────────────────────────────────────────────
// ScopedTimer (standalone, no registry)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("ScopedTimer – standalone")

TEST_CASE("ScopedTimer constructs and destructs without error") {
    expect_no_throw({
        ScopedTimer<std::chrono::milliseconds> t("standalone");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    });
}

TEST_CASE("ScopedTimer with microsecond duration constructs and destructs without error") {
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

// ═════════════════════════════════════════════════════════════════════════════
// HISTOGRAMS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("StatsRegistry – Histograms")

TEST_CASE("histogram_create succeeds for valid parameters") {
    StatsRegistry reg;
    expect_no_throw(reg.histogram_create<"h_ok">(0.0, 10.0, 5));
}

TEST_CASE("histogram_create throws for duplicate name") {
    StatsRegistry reg;
    reg.histogram_create<"h_dup">(0.0, 10.0);
    expect_throws(std::runtime_error, reg.histogram_create<"h_dup">(0.0, 10.0));
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

// ═════════════════════════════════════════════════════════════════════════════
// GLOBAL SINGLETON
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("StatsRegistry – global_stats singleton")

TEST_CASE("global_stats returns the same instance on every call") {
    auto& a = global_stats();
    auto& b = global_stats();
    expect(&a).to_equal(&b);
}

TEST_CASE("STATS macro refers to the same instance as global_stats()") { expect(&STATS).to_equal(&global_stats()); }

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