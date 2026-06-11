#include <cstddef>
#include <string>
#include <vector>

#include "../benchmarking/benchmark.hxx"
#include "../testing/test_main.hpp"

using namespace utilz;

// ─────────────────────────────────────────────────────────────────────────────
// bench_state
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("bench_state")

TEST_CASE("range-for runs exactly the requested number of iterations") {
    benchmark::bench_state state(10);
    std::size_t executed = 0;
    for (auto iteration : state) {
        (void)iteration;
        ++executed;
    }
    expect(executed).to_equal(std::size_t{10});
    expect(state.samples().size()).to_equal(std::size_t{10});
    expect(state.iterations()).to_equal(std::size_t{10});
}

TEST_CASE("every sample is a positive duration") {
    benchmark::bench_state state(5);
    for (auto iteration : state) {
        (void)iteration;
        benchmark::DoNotOptimize(iteration);
    }
    for (double sample : state.samples()) {
        expect(sample).to_be_greater_than(0.0);
    }
}

TEST_CASE("zero iterations produce zero samples") {
    benchmark::bench_state state(0);
    for (auto iteration : state) {
        (void)iteration;
    }
    expect(state.samples().empty()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// compute_result statistics
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("compute_result")

TEST_CASE("mean, min, max and median on a known sample set") {
    const std::vector<double> samples{1.0, 2.0, 3.0, 4.0, 5.0};
    const auto result = benchmark::detail::compute_result("suite", "case", samples);
    expect(result.mean_ns).to_approx_equal(3.0);
    expect(result.min_ns).to_equal(1.0);
    expect(result.max_ns).to_equal(5.0);
    expect(result.median_ns).to_approx_equal(3.0);
    expect(result.iterations).to_equal(std::size_t{5});
    expect(result.suite).to_equal(std::string("suite"));
    expect(result.name).to_equal(std::string("case"));
}

TEST_CASE("stddev is zero for constant samples") {
    const std::vector<double> samples{7.0, 7.0, 7.0, 7.0};
    const auto result = benchmark::detail::compute_result("s", "n", samples);
    expect(result.stddev_ns).to_approx_equal(0.0);
    expect(result.mean_ns).to_approx_equal(7.0);
}

TEST_CASE("median of an even-sized sample set") {
    const std::vector<double> samples{1.0, 2.0, 3.0, 10.0};
    const auto result = benchmark::detail::compute_result("s", "n", samples);
    expect(result.median_ns).to_approx_equal(2.5);
}

// ─────────────────────────────────────────────────────────────────────────────
// fmt_time
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("fmt_time")

TEST_CASE("picks a sensible unit per magnitude") {
    expect(benchmark::detail::fmt_time(12.0)).to_contain("ns");
    expect(benchmark::detail::fmt_time(12'000.0)).to_contain("µs");
    expect(benchmark::detail::fmt_time(12'000'000.0)).to_contain("ms");
    expect(benchmark::detail::fmt_time(12'000'000'000.0)).to_contain("s");
}

// ─────────────────────────────────────────────────────────────────────────────
// registry + registrar
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("bench_registry")

namespace {
void trivial_bench(benchmark::bench_state& state) {
    for (auto iteration : state) {
        benchmark::DoNotOptimize(iteration);
    }
}
}  // namespace

TEST_CASE("registered benchmarks run to completion and return 0") {
    benchmark::auto_bench_registrar registrar("self-test", "trivial", &trivial_bench, 50, 5);
    expect(benchmark::bench_registry::instance().run_all()).to_equal(0);
}
