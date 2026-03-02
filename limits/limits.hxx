#pragma once

/**
 * @file limits.hxx
 * @brief Cooperative time limits and process memory limits.
 * @version 1.0.0
 *
 * @details
 * **Time limiting** — two classes in namespace `timelim`:
 *   - `LocalTimeLimiter` — scoped, per-instance expiry state.  Safe to
 *     instantiate multiple times concurrently; each has its own background
 *     thread that polls at 50 ms intervals.  `expired()` returns true when
 *     either this limiter or the global limit has fired.
 *   - `GlobalTimeLimiter` — process-wide singleton that writes the inline
 *     `GLOBAL_TERMINATE_CONDITION` atomic on expiry.  The macro
 *     `LIMITS_CHECK_STOP()` reads this flag cheaply (relaxed load) from
 *     anywhere in the codebase.
 *
 * Convenience free functions `set_time_limit(seconds)` and
 * `cancel_time_limit()` operate on the `timelim::global_limiter` instance.
 *
 * **Memory limiting** — `memlim::set_memory_limit(mb)` calls `setrlimit`
 * with `RLIMIT_AS`.  On macOS the kernel silently ignores this syscall;
 * the function prints a warning and returns `false`.
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

// Written by the global TimeLimiter only. Read via LIMITS_CHECK_STOP().
inline std::atomic<int> GLOBAL_TERMINATE_CONDITION{0};
#define LIMITS_CHECK_STOP() GLOBAL_TERMINATE_CONDITION.load(std::memory_order_relaxed)

namespace timelim {

/**
 * Scoped time limiter with independent per-instance expiry state.
 *
 * Starts a background polling thread on `set()` and joins it on `cancel()`
 * or destruction.  Multiple instances may run concurrently without interference.
 */
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

    /**
     * Starts the timer.  Cancels any previously running timer first.
     *
     * @param duration   How long until expiry.
     * @param on_expire  Optional callback invoked from the polling thread on expiry.
     */
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

    /** Stops the timer; blocks until the polling thread exits. */
    void cancel() {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
    }

    /**
     * Returns `true` if this limiter expired **or** the global limit fired.
     * Checking only this is sufficient — no need to also call `LIMITS_CHECK_STOP()`.
     */
    [[nodiscard]] auto expired() const noexcept -> bool { return expired_.load(std::memory_order_acquire) || LIMITS_CHECK_STOP(); }

   private:
    std::atomic<bool> expired_{false};
    std::jthread thread_;
    Callback on_expire_;
};

/**
 * Process-wide time limiter that writes `GLOBAL_TERMINATE_CONDITION` on expiry.
 *
 * Intended as a singleton (`timelim::global_limiter`).  When the global limit
 * fires, `LIMITS_CHECK_STOP()` becomes non-zero everywhere in the program
 * without any handle passing.
 */
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

    /**
     * Starts the global timer.  Resets `GLOBAL_TERMINATE_CONDITION` and
     * cancels any previously running timer first.
     *
     * @param duration   How long until the process-wide limit fires.
     * @param on_expire  Optional callback invoked from the polling thread on expiry.
     */
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

    /** Stops the timer and resets `GLOBAL_TERMINATE_CONDITION` to 0. */
    void cancel() {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join();
        }
        GLOBAL_TERMINATE_CONDITION.store(0, std::memory_order_release);
    }

    /** Returns `true` if the global limit has fired. */
    static auto expired() noexcept -> bool { return GLOBAL_TERMINATE_CONDITION.load(std::memory_order_acquire) != 0; }

   private:
    std::jthread thread_;
    Callback on_expire_;
};

// Process-wide instance
inline GlobalTimeLimiter global_limiter;

/**
 * Starts the process-wide time limit.
 * @param seconds    Limit duration in seconds.
 * @param on_expire  Optional callback invoked on the polling thread when time runs out.
 */
inline void set_time_limit(unsigned int seconds, GlobalTimeLimiter::Callback on_expire = nullptr) {
    global_limiter.set(std::chrono::seconds{seconds}, std::move(on_expire));
}

/** Cancels the process-wide time limit and resets `GLOBAL_TERMINATE_CONDITION`. */
inline void cancel_time_limit() { global_limiter.cancel(); }

}  // namespace timelim

namespace memlim {

constexpr std::size_t BYTES_PER_MB = 1024ULL * 1024ULL;

/**
 * Sets the virtual address space limit for the process via `setrlimit(RLIMIT_AS)`.
 *
 * The requested limit is silently clamped to the OS hard cap if necessary.
 * On macOS the syscall is a no-op (the kernel rejects it); a warning is
 * printed and `false` is returned.
 *
 * @param limit_mb  Desired limit in mebibytes.
 * @return `true` on success, `false` if the syscall failed or the platform
 *         does not support memory limits.
 */
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

/**
 * Returns the current virtual address space soft limit in bytes, or `-1` if
 * it cannot be determined (always `-1` on macOS).
 */
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