#include <chrono>
#include <thread>

#include "../testing/test_main.hpp"
#include "../timer/timer.hxx"

using namespace utilz;

namespace {
void busy_wait_ms(int milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Timer
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Timer")

TEST_CASE("starts stopped and reports running state correctly") {
    Timer timer;
    expect(timer.is_running()).to_be_false();
    timer.start();
    expect(timer.is_running()).to_be_true();
    timer.stop();
    expect(timer.is_running()).to_be_false();
}

TEST_CASE("elapsed time grows with wall time") {
    Timer timer;
    timer.start();
    busy_wait_ms(5);
    timer.stop();
    expect(timer.elapsed_ms()).to_be_greater_or_equal(4.0);
    expect(timer.elapsed_ns()).to_be_greater_than(timer.elapsed_us());
}

TEST_CASE("last_lap_ns records the most recent start/stop interval") {
    Timer timer;
    timer.start();
    busy_wait_ms(2);
    timer.stop();
    expect(timer.last_lap_ns()).to_be_greater_than(0.0);
}

TEST_CASE("reset clears accumulated time") {
    Timer timer;
    timer.start();
    busy_wait_ms(2);
    timer.stop();
    timer.reset();
    expect(timer.is_running()).to_be_false();
    expect(timer.elapsed_ns()).to_equal(0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// WelfordAccumulator
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("WelfordAccumulator")

TEST_CASE("computes mean, min, max over recorded samples") {
    timer_detail::WelfordAccumulator acc;
    for (double val : {1.0, 2.0, 3.0, 4.0, 5.0}) {
        acc.record(val);
    }
    expect(acc.count).to_equal(std::size_t{5});
    expect(acc.mean).to_approx_equal(3.0);
    expect(acc.min).to_equal(1.0);
    expect(acc.max).to_equal(5.0);
}

TEST_CASE("variance and stddev match the closed-form result") {
    timer_detail::WelfordAccumulator acc;
    for (double val : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) {
        acc.record(val);
    }
    expect(acc.variance()).to_approx_equal(4.0);
    expect(acc.stddev()).to_approx_equal(2.0);
}

TEST_CASE("merge combines two accumulators losslessly") {
    timer_detail::WelfordAccumulator acc_a;
    timer_detail::WelfordAccumulator acc_b;
    timer_detail::WelfordAccumulator acc_all;
    for (double val : {1.0, 2.0, 3.0}) {
        acc_a.record(val);
        acc_all.record(val);
    }
    for (double val : {10.0, 20.0}) {
        acc_b.record(val);
        acc_all.record(val);
    }
    acc_a.merge(acc_b);
    expect(acc_a.count).to_equal(acc_all.count);
    expect(acc_a.mean).to_approx_equal(acc_all.mean);
    expect(acc_a.variance()).to_approx_equal(acc_all.variance());
    expect(acc_a.min).to_equal(1.0);
    expect(acc_a.max).to_equal(20.0);
}

TEST_CASE("reset returns the accumulator to its initial state") {
    timer_detail::WelfordAccumulator acc;
    acc.record(42.0);
    acc.reset();
    expect(acc.count).to_equal(std::size_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// TimerRegistry — compile-time named timers
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("TimerRegistry")

TEST_CASE("start/stop by name records one sample") {
    TimerRegistry reg;
    reg.start<"reg_basic">();
    busy_wait_ms(1);
    reg.stop<"reg_basic">();
    const auto stats = reg.stats<"reg_basic">();
    expect(stats.count).to_equal(std::size_t{1});
    expect(stats.get_total()).to_be_greater_than(0.0);
}

TEST_CASE("stop via Slot* handle skips the lookup and records") {
    TimerRegistry reg;
    auto* slot = reg.start<"reg_handle">();
    busy_wait_ms(1);
    TimerRegistry::stop(slot);
    expect(reg.stats<"reg_handle">().count).to_equal(std::size_t{1});
}

TEST_CASE("is_running reflects timer state") {
    TimerRegistry reg;
    expect(reg.is_running<"reg_running">()).to_be_false();
    auto* slot = reg.start<"reg_running">();
    expect(reg.is_running<"reg_running">()).to_be_true();
    TimerRegistry::stop(slot);
    expect(reg.is_running<"reg_running">()).to_be_false();
}

TEST_CASE("stop on a non-running slot is a no-op") {
    TimerRegistry reg;
    auto* slot = reg.start<"reg_noop">();
    TimerRegistry::stop(slot);
    TimerRegistry::stop(slot);  // second stop must not record a stale lap
    expect(reg.stats<"reg_noop">().count).to_equal(std::size_t{1});
}

TEST_CASE("reset clears stats but keeps the name registered") {
    TimerRegistry reg;
    reg.start<"reg_reset">();
    reg.stop<"reg_reset">();
    reg.reset<"reg_reset">();
    expect(reg.stats<"reg_reset">().count).to_equal(std::size_t{0});
    reg.start<"reg_reset">();
    reg.stop<"reg_reset">();
    expect(reg.stats<"reg_reset">().count).to_equal(std::size_t{1});
}

TEST_CASE("multiple samples accumulate into stats") {
    TimerRegistry reg;
    for (int i = 0; i < 3; ++i) {
        reg.start<"reg_multi">();
        reg.stop<"reg_multi">();
    }
    const auto stats = reg.stats<"reg_multi">();
    expect(stats.count).to_equal(std::size_t{3});
    expect(stats.get_max()).to_be_greater_or_equal(stats.get_min());
}

// ─────────────────────────────────────────────────────────────────────────────
// Scoped timers
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("scoped timers")

TEST_CASE("make_scoped_timer records exactly one sample per scope") {
    TimerRegistry reg;
    {
        auto scoped = make_scoped_timer<"scoped_one">(reg);
        busy_wait_ms(1);
    }
    expect(reg.stats<"scoped_one">().count).to_equal(std::size_t{1});
    {
        auto scoped = make_scoped_timer<"scoped_one">(reg);
    }
    expect(reg.stats<"scoped_one">().count).to_equal(std::size_t{2});
}

TEST_CASE("standalone ScopedTimer measures its scope") {
    // Prints one line to stdout on destruction; we only assert it doesn't blow up.
    expect_no_throw({
        ScopedTimer scoped("standalone_scope");
        busy_wait_ms(1);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Reports
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("reports")

TEST_CASE("get_totals_report sums completed laps per name") {
    TimerRegistry reg;
    reg.start<"rep_a">();
    busy_wait_ms(1);
    reg.stop<"rep_a">();
    reg.start<"rep_b">();
    reg.stop<"rep_b">();

    const auto rows = reg.get_totals_report<std::chrono::nanoseconds>();
    expect(rows.size()).to_equal(std::size_t{2});
    expect(rows[0].first).to_equal(std::string("rep_a"));
    expect(rows[0].second).to_be_greater_than(0.0);
    expect(rows[1].first).to_equal(std::string("rep_b"));
}

TEST_CASE("in-flight time is excluded from the totals snapshot") {
    TimerRegistry reg;
    auto* slot = reg.start<"rep_inflight">();
    const auto rows = reg.get_totals_report<std::chrono::nanoseconds>();
    expect(rows.size()).to_equal(std::size_t{1});
    expect(rows[0].second).to_equal(0.0);
    TimerRegistry::stop(slot);
}

TEST_CASE("get_stats_report aggregates per-name Welford stats") {
    TimerRegistry reg;
    for (int i = 0; i < 4; ++i) {
        reg.start<"rep_stats">();
        reg.stop<"rep_stats">();
    }
    const auto rows = reg.get_stats_report<std::chrono::nanoseconds>();
    expect(rows.size()).to_equal(std::size_t{1});
    expect(rows[0].name).to_equal(std::string("rep_stats"));
    expect(rows[0].call_count).to_equal(std::size_t{4});
    expect(rows[0].thread_count).to_equal(std::size_t{1});
    expect(rows[0].max).to_be_greater_or_equal(rows[0].min);
    expect(rows[0].total).to_be_greater_or_equal(rows[0].max);
}

TEST_CASE("get_thread_report lists one row per thread and name") {
    TimerRegistry reg;
    reg.start<"rep_thread">();
    reg.stop<"rep_thread">();
    std::thread worker([&reg] {
        reg.start<"rep_thread">();
        reg.stop<"rep_thread">();
    });
    worker.join();

    const auto rows = reg.get_thread_report<std::chrono::nanoseconds>();
    expect(rows.size()).to_equal(std::size_t{2});
    for (const auto& row : rows) {
        expect(row.name).to_equal(std::string("rep_thread"));
        expect(row.call_count).to_equal(std::size_t{1});
    }
}

TEST_CASE("exited threads are merged via the graveyard") {
    TimerRegistry reg;
    std::thread worker([&reg] {
        reg.start<"rep_grave">();
        busy_wait_ms(1);
        reg.stop<"rep_grave">();
    });
    worker.join();

    const auto totals = reg.get_totals_report<std::chrono::nanoseconds>();
    expect(totals.size()).to_equal(std::size_t{1});
    expect(totals[0].second).to_be_greater_than(0.0);

    const auto stats = reg.get_stats_report<std::chrono::nanoseconds>();
    expect(stats.size()).to_equal(std::size_t{1});
    expect(stats[0].call_count).to_equal(std::size_t{1});
}

TEST_CASE("report string renderers include names and units") {
    TimerRegistry reg;
    reg.start<"rep_str">();
    busy_wait_ms(1);
    reg.stop<"rep_str">();

    expect(reg.totals_report_to_str<std::chrono::microseconds>()).to_contain("rep_str").to_contain("us");
    expect(reg.stats_report_to_str()).to_contain("rep_str").to_contain("Calls");
    expect(reg.thread_report_to_str()).to_contain("rep_str").to_contain("Calls");
}

TEST_CASE("report renderers are empty for an unused registry") {
    TimerRegistry reg;
    expect(reg.totals_report_to_str().empty()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// timer_detail unit helpers
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("unit helpers")

TEST_CASE("ns_to converts between duration units") {
    expect(timer_detail::ns_to<std::chrono::microseconds>(1500.0)).to_approx_equal(1.5);
    expect(timer_detail::ns_to<std::chrono::milliseconds>(2.5e6)).to_approx_equal(2.5);
    expect(timer_detail::ns_to<std::chrono::seconds>(3.0e9)).to_approx_equal(3.0);
}

TEST_CASE("unit_name reports the right suffix") {
    expect(std::string(timer_detail::unit_name<std::chrono::nanoseconds>())).to_equal(std::string("ns"));
    expect(std::string(timer_detail::unit_name<std::chrono::milliseconds>())).to_equal(std::string("ms"));
}

TEST_CASE("pick_unit scales to the magnitude of its input") {
    expect(std::string(timer_detail::pick_unit(50.0).suffix)).to_equal(std::string("ns"));
    expect(std::string(timer_detail::pick_unit(5.0e4).suffix)).to_equal(std::string("us"));
    expect(std::string(timer_detail::pick_unit(5.0e7).suffix)).to_equal(std::string("ms"));
    expect(std::string(timer_detail::pick_unit(5.0e9).suffix)).to_equal(std::string("s"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Global registry
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("global registry")

TEST_CASE("TIMER_REG macro reaches the global registry from any scope") {
    TIMER_REG.start<"global_macro">();
    TIMER_REG.stop<"global_macro">();
    expect(TIMER_REG.stats<"global_macro">().count).to_be_greater_or_equal(std::size_t{1});
    expect(&TIMER_REG).to_equal(&global_timers());
}
