/**
 * @file demo.cpp
 * @brief ThreadPool feature showcase: priority lanes, cooperative cancellation,
 *        bulk submit (submit_n / submit_each), timed wait, and a parallel
 *        image-processing pipeline.
 *
 * Compile (C++20):
 *   g++ -std=c++20 -O2 -pthread demo.cpp -o demo && ./demo
 */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <iomanip>
#include <string>
#include <vector>

#include "../logger/logger.hxx"
#include "../timer/timer.hxx"
#include "thread_pool.hxx"

using namespace utilz;

using enum LoggerLevel;

// ─────────────────────────────────────────────────────────────────────────────
// CPU simulation — noinline prevents the compiler from hoisting or eliminating
// ─────────────────────────────────────────────────────────────────────────────

__attribute__((noinline)) static auto crunch(int n) -> double {
    double acc = 0.0;
    for (int i = 1; i <= n; ++i) {
        acc += std::sqrt(static_cast<double>(i));
    }
    return acc;
}

static volatile double v_sink = 0.0;

// ─────────────────────────────────────────────────────────────────────────────
// Data types
// ─────────────────────────────────────────────────────────────────────────────

struct RawImage {
    int idx;
    double raw;
};

struct NormImage {
    int idx;
    double value;
    double mean;
};

struct ToneMapped {
    int idx;
    double value;
};

struct Histogram {
    static constexpr int BINS = 10;
    std::array<std::atomic<int>, BINS> counts{};
    double gamma = 1.0;

    void record(double norm_val) noexcept {
        int bin = std::clamp(static_cast<int>(norm_val * BINS), 0, BINS - 1);
        counts.at(static_cast<std::size_t>(bin)).fetch_add(1, std::memory_order_relaxed);
    }

    void compute_gamma() noexcept {
        int peak_bin = 0;
        int peak_count = 0;
        for (int bin = 0; bin < BINS; ++bin) {
            int cnt = counts.at(static_cast<std::size_t>(bin)).load(std::memory_order_relaxed);
            if (cnt > peak_count) {
                peak_count = cnt;
                peak_bin = bin;
            }
        }
        double center = std::clamp((peak_bin + 0.5) / BINS, 0.05, 0.95);
        gamma = std::log(0.5) / std::log(center);
    }

    void print(int n_images) const {
        constexpr int BAR_W = 24;
        int max_cnt = 0;
        for (int bin = 0; bin < BINS; ++bin) {
            max_cnt = std::max(max_cnt, counts.at(static_cast<std::size_t>(bin)).load());
        }
        std::cout << "\n    Exposure histogram (" << n_images << " images, " << BINS << " bins):\n";
        for (int bin = 0; bin < BINS; ++bin) {
            int cnt = counts.at(static_cast<std::size_t>(bin)).load();
            int bar = (max_cnt > 0) ? cnt * BAR_W / max_cnt : 0;
            double lo = static_cast<double>(bin) / BINS;
            double hi = static_cast<double>(bin + 1) / BINS;
            std::cout << "    [" << std::fixed << std::setprecision(1) << lo << ", " << hi << ")  " << std::string(static_cast<std::size_t>(bar), '#')
                      << std::string(static_cast<std::size_t>(BAR_W - bar), ' ') << "  " << cnt << "\n";
        }
        std::cout << "\n    Peak exposure > 0.5 → images are OVER-exposed.\n"
                  << "    Gamma = " << std::setprecision(3) << gamma << " (< 1 brightens; > 1 darkens — here darkens to correct)\n";
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Work functions
// ─────────────────────────────────────────────────────────────────────────────

namespace work {
constexpr int LOAD = 50'000'000;
constexpr int NORM = 15'000'000;
constexpr int TONE = 25'000'000;
constexpr int SAVE = 35'000'000;
constexpr int LOAD_STRIDE = 17'000;
constexpr int NORM_STRIDE = 13'000;
constexpr int TONE_STRIDE = 11'000;
constexpr int SAVE_STRIDE = 7'000;
}  // namespace work

__attribute__((noinline)) static auto stage_load(int idx) -> RawImage { return {.idx = idx, .raw = crunch(work::LOAD + (idx * work::LOAD_STRIDE))}; }

__attribute__((noinline)) static auto stage_normalize(RawImage img) -> NormImage {
    double base = crunch(work::NORM + (img.idx * work::NORM_STRIDE));
    double value = 0.65 + (0.28 * std::sin(img.idx * 0.5)) + ((img.raw + base) * 1e-13);
    value = std::clamp(value, 0.05, 0.95);
    double mean = (img.raw + base) / static_cast<double>(work::LOAD + work::NORM + img.idx + 1);
    return {.idx = img.idx, .value = value, .mean = mean};
}

__attribute__((noinline)) static auto stage_tone_map(NormImage img, double gamma) -> ToneMapped {
    double base = crunch(work::TONE + (img.idx * work::TONE_STRIDE));
    double mapped = std::pow(img.value + (base * 1e-13), gamma);
    return {.idx = img.idx, .value = std::clamp(mapped, 0.0, 1.0)};
}

__attribute__((noinline)) static void stage_save(ToneMapped img) { v_sink += img.value + crunch(work::SAVE + (img.idx * work::SAVE_STRIDE)); }

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Demo: Priority lanes
// ─────────────────────────────────────────────────────────────────────────────

static void demo_priority() {
    Logger log{{.min_level = INFO}};
    static constexpr int LINE_W = 72;
    log[INFO] << std::string(LINE_W, '-');
    log[INFO] << "Demo: Priority lanes";
    log[INFO] << std::string(LINE_W, '-');

    ThreadPool pool{1};

    // Gate task saturates the single worker so all subsequent tasks queue up.
    std::promise<void> gate;
    auto shared_gate = gate.get_future().share();
    pool.submit([shared_gate] { shared_gate.wait(); });

    // Five low-priority tasks submitted first.
    std::atomic<int> counter{0};
    std::vector<int> low_items{0, 1, 2, 3, 4};
    pool.submit_each(Priority::low, low_items.begin(), low_items.end(), [&counter](int) { ++counter; });

    // One high-priority task submitted after — should jump to the front.
    std::atomic<int> high_pos{0};
    pool.submit(Priority::high, [&counter, &high_pos]() { high_pos = ++counter; });

    gate.set_value();
    pool.wait_all();

    log[INFO] << std::format("  high-priority task ran at position {}/6  (expected: 1)", high_pos.load());
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo: Cooperative cancellation
// ─────────────────────────────────────────────────────────────────────────────

static void demo_cancellation() {
    Logger log{{.min_level = INFO}};
    static constexpr int LINE_W = 72;
    log[INFO] << std::string(LINE_W, '-');
    log[INFO] << "Demo: Cooperative cancellation";
    log[INFO] << std::string(LINE_W, '-');

    ThreadPool pool{1};
    std::promise<void> started;
    std::atomic<long long> iterations{0};

    auto fut = pool.submit([&](std::stop_token stoken) {
        started.set_value();
        while (!stoken.stop_requested()) {
            ++iterations;
            std::this_thread::yield();
        }
    });

    started.get_future().wait();  // ensure task is running before cancelling
    fut.cancel();
    fut.get();

    log[INFO] << std::format("  task ran {:L} iteration(s) before honouring cancellation", iterations.load());
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline
// ─────────────────────────────────────────────────────────────────────────────

static void run_pipeline(int n_images, int n_threads) {
    Logger log{{.min_level = DEBUG}};

    static constexpr int LINE_W = 72;

    log[INFO] << std::string(LINE_W, '=');
    log[INFO] << std::format("Parallel Image-Processing Pipeline  —  {} images · {} worker threads", n_images, n_threads);
    log[INFO] << std::string(LINE_W, '=');

    {
        auto _tmr_parallel = make_scoped_timer<"parallel">(TIMER_REG);

        ThreadPool pool{static_cast<std::size_t>(n_threads)};
        Histogram hist;
        std::vector<NormImage> norm_imgs(static_cast<std::size_t>(n_images));

        // ── Stage 1: load + normalise (bulk submit via submit_n) ─────────────
        {
            auto _tmr_dispatch = make_scoped_timer<"dispatch">(TIMER_REG);
            pool.submit_n(static_cast<std::size_t>(n_images), [&](std::size_t idx) {
                auto img = stage_normalize(stage_load(static_cast<int>(idx)));
                norm_imgs[img.idx] = img;
                hist.record(img.value);
            });
        }

        log.flush();
        static constexpr int STAGE1_TIMEOUT_S = 120;
        if (!pool.wait_for(std::chrono::seconds(STAGE1_TIMEOUT_S))) {
            log[WARNING] << "  stage-1 timed out — pipeline aborted";
            return;
        }
        hist.compute_gamma();
        log[INFO] << std::format("gamma = {:3}", hist.gamma);

        // ── Stage 2: tone-map + save (bulk submit via submit_n) ──────────────
        {
            auto _tmr_dispatch = make_scoped_timer<"dispatch">(TIMER_REG);
            pool.submit_n(static_cast<std::size_t>(n_images), [&](std::size_t idx) { stage_save(stage_tone_map(norm_imgs[idx], hist.gamma)); });
        }
        pool.wait_all();
    }
    log.flush();

    // ── Speedup comparison ────────────────────────────────────────────────────

    log[INFO] << std::string(LINE_W, '=');
    log[INFO] << std::format("Serial Image-Processing Pipeline  —  {} images", n_images);
    log[INFO] << std::string(LINE_W, '=');

    {
        auto _tmr_serial = make_scoped_timer<"serial">(TIMER_REG);
        Histogram hist;
        std::vector<NormImage> norm_imgs(static_cast<std::size_t>(n_images));
        for (int idx = 0; idx < n_images; ++idx) {
            auto norm = stage_normalize(stage_load(idx));
            hist.record(norm.value);
            norm_imgs[static_cast<std::size_t>(idx)] = norm;
        }
        hist.compute_gamma();
        log[INFO] << std::format("gamma = {:3}", hist.gamma);
        for (const auto& img : norm_imgs) {
            stage_save(stage_tone_map(img, hist.gamma));
        }
    }
    log.flush();

    TIMER_REG.print_stats_report();
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

auto main() -> int {
    demo_priority();
    demo_cancellation();
    run_pipeline(256, 8);
    return 0;
}
