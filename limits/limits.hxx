#pragma once

/**
 * @file limits.hxx
 * @brief Time and Memory limits for general purpose use
 * @version 1.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 */

#include <sys/resource.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>

// Written by the global TimeLimiter only. Read via CHECK_STOP().
inline std::atomic<int> GLOBAL_TERMINATE_CONDITION{0};
#define CHECK_STOP() GLOBAL_TERMINATE_CONDITION.load(std::memory_order_relaxed)

namespace timelim {

// ---------------------------------------------------------------------------
// LocalTimeLimiter — scoped, RAII, independent expiry state per instance.
// Intended for bounding sub-tasks. Multiple instances do not interfere.
// ---------------------------------------------------------------------------
class LocalTimeLimiter {
   public:
    using Clock = std::chrono::steady_clock;
    using Callback = std::function<void()>;
    static constexpr int POLL_INTERVAL_MS = 50;

    LocalTimeLimiter() = default;
    ~LocalTimeLimiter() { cancel(); }

    LocalTimeLimiter(const LocalTimeLimiter&) = delete;
    auto operator=(const LocalTimeLimiter&) -> LocalTimeLimiter& = delete;
    LocalTimeLimiter(LocalTimeLimiter&&) = delete;
    auto operator=(LocalTimeLimiter&&) -> LocalTimeLimiter& = delete;

    void set(std::chrono::seconds duration, Callback on_expire = nullptr) {
        cancel();
        expired_.store(false, std::memory_order_release);
        on_expire_ = std::move(on_expire);

        thread_ = std::jthread([this, deadline = Clock::now() + duration](std::stop_token tok) -> void {
            while (Clock::now() < deadline) {
                if (tok.stop_requested()) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            }
            if (!tok.stop_requested()) {
                expired_.store(true, std::memory_order_release);
                if (on_expire_) {
                    on_expire_();
                }
            }
        });
    }

    void cancel() {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
    }

    // Returns true if this limiter expired OR the global limit was reached.
    // Checking only this is sufficient — no need to also call CHECK_STOP().
    [[nodiscard]] auto expired() const noexcept -> bool { return expired_.load(std::memory_order_acquire) || CHECK_STOP(); }

   private:
    std::atomic<bool> expired_{false};
    std::jthread thread_;
    Callback on_expire_;
};

// ---------------------------------------------------------------------------
// GlobalTimeLimiter — process-wide singleton. Writes GLOBAL_TERMINATE_CONDITION
// so CHECK_STOP() works anywhere in the codebase without passing a handle.
// When the global limit expires, all local limiters become irrelevant anyway.
// ---------------------------------------------------------------------------
class GlobalTimeLimiter {
   public:
    using Clock = std::chrono::steady_clock;
    using Callback = std::function<void()>;
    static constexpr int POLL_INTERVAL_MS = 50;

    GlobalTimeLimiter() = default;
    ~GlobalTimeLimiter() { cancel(); }

    GlobalTimeLimiter(const GlobalTimeLimiter&) = delete;
    GlobalTimeLimiter(GlobalTimeLimiter&&) = delete;
    auto operator=(const GlobalTimeLimiter&) -> GlobalTimeLimiter& = delete;
    auto operator=(GlobalTimeLimiter&&) -> GlobalTimeLimiter& = delete;

    void set(std::chrono::seconds duration, Callback on_expire = nullptr) {
        cancel();
        GLOBAL_TERMINATE_CONDITION.store(0, std::memory_order_release);
        on_expire_ = std::move(on_expire);

        thread_ = std::jthread([this, deadline = Clock::now() + duration](std::stop_token tok) -> void {
            while (Clock::now() < deadline) {
                if (tok.stop_requested()) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            }
            if (!tok.stop_requested()) {
                GLOBAL_TERMINATE_CONDITION.store(1, std::memory_order_release);
                if (on_expire_) {
                    on_expire_();
                }
            }
        });
    }

    void cancel() {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        GLOBAL_TERMINATE_CONDITION.store(0, std::memory_order_release);
    }

    static auto expired() noexcept -> bool { return GLOBAL_TERMINATE_CONDITION.load(std::memory_order_acquire) != 0; }

   private:
    std::jthread thread_;
    Callback on_expire_;
};

// Process-wide instance
inline GlobalTimeLimiter global_limiter;

// Convenience free functions for the global limiter
inline void set_time_limit(unsigned int seconds, GlobalTimeLimiter::Callback on_expire = nullptr) {
    global_limiter.set(std::chrono::seconds{seconds}, std::move(on_expire));
}

inline void cancel_time_limit() { global_limiter.cancel(); }

}  // namespace timelim

namespace memlim {

constexpr std::size_t BYTES_PER_MB = 1024ULL * 1024ULL;

[[nodiscard]]
inline auto set_memory_limit(std::size_t limit_mb) -> bool {
#ifdef __APPLE__
    // macOS kernels (10.12+) unconditionally reject setrlimit for memory
    // resources with EINVAL regardless of value or resource type. There is
    // no user-space workaround without a kernel extension. The call is a
    // no-op on this platform.
    (void)limit_mb;
    std::cerr << "[memlim] memory limits are not enforceable on macOS "
                 "(kernel ignores setrlimit for RLIMIT_AS/DATA/RSS)\n";
    return false;
#else
    const rlim_t requested = static_cast<rlim_t>(limit_mb) * BYTES_PER_MB;

    rlimit cur{};
    if (getrlimit(RLIMIT_AS, &cur) != 0) {
        std::cerr << "[WARNING] getrlimit failed: " << std::strerror(errno) << '\n';
        return false;
    }

    // Never raise above the hard cap — setrlimit returns EPERM if we try.
    const rlim_t effective = (cur.rlim_max == RLIM_INFINITY) ? requested : std::min(requested, cur.rlim_max);

    if (effective < requested) {
        std::cerr << "[memlim] clamped to OS hard cap: " << (effective / BYTES_PER_MB) << " MB"
                  << " (requested " << limit_mb << " MB)\n";
    }

    const rlimit rlim{.rlim_cur = effective, .rlim_max = effective};
    if (setrlimit(RLIMIT_AS, &rlim) != 0) {
        std::cerr << "[WARNING] setrlimit failed (" << (effective / BYTES_PER_MB) << " MB): " << std::strerror(errno) << '\n';
        return false;
    }
    return true;
#endif
}

[[nodiscard]]
inline std::ptrdiff_t current_memory_usage() noexcept {
#ifdef __APPLE__
    return -1;
#else
    rlimit rlim{};
    if (getrlimit(RLIMIT_AS, &rlim) != 0) {
        return -1;
    }
    return static_cast<std::ptrdiff_t>(rlim.rlim_cur);
#endif
}

}  // namespace memlim