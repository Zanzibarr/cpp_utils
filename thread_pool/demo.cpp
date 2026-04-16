/**
 * @file demo.cpp
 * @brief Parallel image-processing pipeline — demonstrates ThreadPool, Future::then(), TaskGroup.
 *
 * Five-stage pipeline processes N images through a pool of worker threads:
 *
 *   Stages 1→2  Load → Normalize   Future::then() chain          per-image dependency
 *   Stage  3    Histogram           TaskGroup × N  →  wait()      collective barrier
 *   Stage  4    Tone-map            submit() × N                  parallel, reads gamma
 *   Stage  5    Save                TaskGroup × N  →  wait()      collective barrier
 *
 * Synchronisation patterns:
 *   Future::then()  — chains a dependent task without blocking the main thread;
 *                     dependency is expressed structurally, not via FIFO order.
 *   Future::get()   — called inside worker lambdas (never on the main thread)
 *                     to carry a result from one stage to the next.
 *   TaskGroup       — ensures the entire batch finishes before consuming a
 *                     collective output (histogram gamma, final save confirmation).
 *
 * Compile (C++23):
 *   g++ -std=c++23 -O2 -pthread demo.cpp -o demo && ./demo
 */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <string>
#include <vector>

#include "../logger/logger.hxx"
#include "../timer/timer.hxx"
#include "thread_pool.hxx"

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
constexpr int LOAD_STRIDE = 17;
constexpr int NORM_STRIDE = 13;
constexpr int TONE_STRIDE = 11;
constexpr int SAVE_STRIDE = 7;
}  // namespace work

__attribute__((noinline)) static auto stage_load(int idx) -> RawImage { return {.idx = idx, .raw = crunch(work::LOAD + (idx * work::LOAD_STRIDE))}; }

__attribute__((noinline)) static auto stage_normalize(RawImage img) -> NormImage {
    double base = crunch(work::NORM + (img.idx * work::NORM_STRIDE));
    double value = 0.65 + 0.28 * std::sin(img.idx * 0.5) + (img.raw + base) * 1e-13;
    value = std::clamp(value, 0.05, 0.95);
    double mean = (img.raw + base) / static_cast<double>(work::LOAD + work::NORM + img.idx + 1);
    return {.idx = img.idx, .value = value, .mean = mean};
}

__attribute__((noinline)) static auto stage_tone_map(NormImage img, double gamma) -> ToneMapped {
    double base = crunch(work::TONE + (img.idx * work::TONE_STRIDE));
    double mapped = std::pow(img.value + base * 1e-13, gamma);
    return {.idx = img.idx, .value = std::clamp(mapped, 0.0, 1.0)};
}

__attribute__((noinline)) static void stage_save(ToneMapped img) { v_sink += img.value + crunch(work::SAVE + (img.idx * work::SAVE_STRIDE)); }

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline
// ─────────────────────────────────────────────────────────────────────────────

static void run_pipeline(int n_images, int n_threads) {
    Logger log{{.async = false, .min_level = DEBUG}};

    static constexpr int LINE_W = 72;
    static constexpr int COL_EXP = 8;
    static constexpr int COL_TON = 11;
    static constexpr int COL_DEL = 7;

    log[INFO] << std::string(LINE_W, '=');
    log[INFO] << std::format("Parallel Image-Processing Pipeline  —  {} images · {} worker threads", n_images, n_threads);
    log[INFO] << std::string(LINE_W, '=');

    {
        auto _tmr_parallel = make_scoped_timer<"parallel">(TIMER_REG);

        ThreadPool pool{static_cast<std::size_t>(n_threads)};
        Histogram hist;
        std::vector<NormImage> norm_imgs(n_images);

        {
            auto _tmr_dispatch = make_scoped_timer<"dispatch">(TIMER_REG);
            for (int idx = 0; idx < n_images; ++idx) {
                pool.submit([&, idx]() {
                        auto _tmr_load = make_scoped_timer<"loading">(TIMER_REG);
                        log[DEBUG] << std::format("Loading image {}", idx);
                        return stage_load(idx);
                    })
                    .then([&, idx](RawImage raw) {
                        auto _tmr_norm = make_scoped_timer<"normalizing">(TIMER_REG);
                        log[DEBUG] << std::format("Normalizing image {}", idx);
                        return stage_normalize(raw);
                    })
                    .then([&, idx](NormImage img) {
                        auto _tmr_hist = make_scoped_timer<"histogram-record">(TIMER_REG);
                        log[DEBUG] << std::format("Storing histogram value for image {}", idx);
                        norm_imgs[img.idx] = img;
                        hist.record(img.value);
                    });
            }
        }

        log.flush();
        pool.wait_all();  // Barrier for storing histogram values
        hist.compute_gamma();

        log[INFO] << std::format("gamma = {:3}  (barrier passed — broadcasting to all tone-map tasks)", hist.gamma);
        // hist.print(n_images);

        {
            auto _tmr_dispatch = make_scoped_timer<"dispatch">(TIMER_REG);
            for (int idx = 0; idx < n_images; ++idx) {
                pool.submit([&, idx] {
                        auto _tmr = make_scoped_timer<"tone-map">(TIMER_REG);
                        log[DEBUG] << std::format("Tone map for image {}", idx);
                        return stage_tone_map(norm_imgs[idx], hist.gamma);
                    })
                    .then([&, idx](ToneMapped img) {
                        auto _tmr = make_scoped_timer<"save">(TIMER_REG);
                        log[DEBUG] << std::format("Saving image {}", idx);
                        stage_save(img);
                    });
            }
        }
        pool.wait_all();  // Final sinchronization
    }
    log.flush();

    // ── Speedup comparison ────────────────────────────────────────────────────

    log[INFO] << std::string(LINE_W, '=');
    log[INFO] << std::format("Serial Image-Processing Pipeline  —  {} images", n_images);
    log[INFO] << std::string(LINE_W, '=');

    {
        auto _tmr_parallel = make_scoped_timer<"serial">(TIMER_REG);
        Histogram hist;
        std::vector<NormImage> norm_imgs(n_images);
        for (int idx = 0; idx < n_images; ++idx) {
            auto norm = stage_normalize(stage_load(idx));
            hist.record(norm.value);
            norm_imgs[idx] = norm;
        }
        hist.compute_gamma();
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
    run_pipeline(16, 4);
    return 0;
}
