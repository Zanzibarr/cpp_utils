/**
 * @file test.cpp
 * @brief Unit tests for TimeLimiter, MemoryLimiter, global_limits helpers,
 *        and memlim helpers.
 *
 * Build (C++20 + pthreads):
 *   g++ -std=c++20 -O2 -pthread test.cpp -o test && ./test
 *
 * NOTE: Tests that verify actual timer expiry wait for 1 second plus the
 * 50 ms poll interval. They are correct but intentionally slow.
 */

#include <thread>

#include "../testing/test_main.hpp"
#include "limits.hxx"

using namespace utilz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Cancels the global time limiter and resets time_flag so tests don't
// interfere with each other.  (cancel_time_limit() itself does NOT reset the
// flag — that is intentional behaviour of the new API.)
static void reset_global_time() {
    cancel_time_limit();
    global_limits::time_flag.store(false, std::memory_order_relaxed);
}

// Same for the global memory limiter.
static void reset_global_memory() {
    cancel_memory_limit();
    global_limits::memory_flag.store(false, std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────────────────
// TimeLimiter — initial state
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("TimeLimiter — initial state")

TEST_CASE("default-constructed limiter is not expired") {
    TimeLimiter lim;
    expect(lim.expired()).to_be_false();
}

TEST_CASE("cancel() on a never-started limiter does not throw") {
    TimeLimiter lim;
    expect_no_throw(lim.cancel());
}

// ─────────────────────────────────────────────────────────────────────────────
// TimeLimiter — set and cancel
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("TimeLimiter — set and cancel")

TEST_CASE("after set() the limiter is not immediately expired") {
    TimeLimiter lim;
    lim.set(std::chrono::seconds{10});
    expect(lim.expired()).to_be_false();
    lim.cancel();
}

TEST_CASE("after cancel() before firing the limiter is not expired") {
    TimeLimiter lim;
    lim.set(std::chrono::seconds{10});
    lim.cancel();
    expect(lim.expired()).to_be_false();
}

TEST_CASE("cancel() is idempotent") {
    TimeLimiter lim;
    lim.set(std::chrono::seconds{10});
    expect_no_throw(lim.cancel());
    expect_no_throw(lim.cancel());
}

TEST_CASE("set() replaces a previously running timer") {
    TimeLimiter lim;
    lim.set(std::chrono::seconds{10});
    lim.set(std::chrono::seconds{10});  // implicitly cancels the first
    expect(lim.expired()).to_be_false();
    lim.cancel();
}

TEST_CASE("destructor joins background thread without hanging") {
    // If this test completes, the destructor ran correctly.
    {
        TimeLimiter lim;
        lim.set(std::chrono::seconds{10});
    }  // destructor calls cancel()
}

// ─────────────────────────────────────────────────────────────────────────────
// TimeLimiter — expiry (timing-sensitive, takes ~1 s)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("TimeLimiter — expiry")

TEST_CASE("limiter fires and expired() becomes true after the duration") {
    TimeLimiter lim;
    lim.set(std::chrono::seconds{1});

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!lim.expired() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    expect(lim.expired()).to_be_true();
}

TEST_CASE("on_expire callback is invoked when the timer fires") {
    std::atomic<bool> called{false};
    TimeLimiter lim;
    lim.set(std::chrono::seconds{1}, [&called] { called.store(true, std::memory_order_release); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!called.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    expect(called.load()).to_be_true();
}

TEST_CASE("callback is NOT invoked when the timer is cancelled before firing") {
    std::atomic<bool> called{false};
    TimeLimiter lim;
    lim.set(std::chrono::seconds{10}, [&called] { called.store(true); });
    lim.cancel();
    expect(called.load()).to_be_false();
}

TEST_CASE("expired() remains true after cancel() if the timer already fired") {
    TimeLimiter lim;
    lim.set(std::chrono::seconds{1});

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!lim.expired() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    lim.cancel();  // should NOT reset expired_
    expect(lim.expired()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// Global time limit — initial state and set/cancel
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Global time limit — set and cancel")

TEST_CASE("time_reached() is false at startup") {
    reset_global_time();
    expect(global_limits::time_reached()).to_be_false();
}

TEST_CASE("set_time_limit does not immediately set the global flag") {
    reset_global_time();
    set_time_limit(10);
    expect(global_limits::time_reached()).to_be_false();
    cancel_time_limit();
}

TEST_CASE("cancel_time_limit does NOT reset time_flag once it has fired") {
    // Arm with a very short duration, let it fire, then cancel.
    reset_global_time();
    set_time_limit(1);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!global_limits::time_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    bool fired = global_limits::time_reached();
    cancel_time_limit();  // should NOT reset the flag
    expect(fired).to_be_true();
    expect(global_limits::time_reached()).to_be_true();
    reset_global_time();  // clean up for subsequent tests
}

TEST_CASE("cancel_time_limit is idempotent") {
    reset_global_time();
    set_time_limit(10);
    expect_no_throw(cancel_time_limit());
    expect_no_throw(cancel_time_limit());
}

// ─────────────────────────────────────────────────────────────────────────────
// Global time limit — expiry (timing-sensitive, takes ~1 s)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Global time limit — expiry")

TEST_CASE("global time flag becomes true after the duration") {
    reset_global_time();
    set_time_limit(1);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!global_limits::time_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    bool fired = global_limits::time_reached();
    reset_global_time();
    expect(fired).to_be_true();
}

TEST_CASE("global time callback is invoked on expiry") {
    reset_global_time();
    std::atomic<bool> called{false};
    set_time_limit(1, [&called] { called.store(true, std::memory_order_release); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!called.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    reset_global_time();
    expect(called.load()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// MemoryLimiter — initial state
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("MemoryLimiter — initial state")

TEST_CASE("default-constructed MemoryLimiter is not exceeded") {
    MemoryLimiter mlim;
    expect(mlim.exceeded()).to_be_false();
}

TEST_CASE("cancel() on a never-started MemoryLimiter does not throw") {
    MemoryLimiter mlim;
    expect_no_throw(mlim.cancel());
}

// ─────────────────────────────────────────────────────────────────────────────
// MemoryLimiter — set and cancel
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("MemoryLimiter — set and cancel")

TEST_CASE("after set() with a high limit the MemoryLimiter is not immediately exceeded") {
    MemoryLimiter mlim;
    mlim.set(65536);  // 64 GB — won't fire
    expect(mlim.exceeded()).to_be_false();
    mlim.cancel();
}

TEST_CASE("after cancel() before firing exceeded() is false") {
    MemoryLimiter mlim;
    mlim.set(65536);
    mlim.cancel();
    expect(mlim.exceeded()).to_be_false();
}

TEST_CASE("cancel() is idempotent") {
    MemoryLimiter mlim;
    mlim.set(65536);
    expect_no_throw(mlim.cancel());
    expect_no_throw(mlim.cancel());
}

TEST_CASE("set() replaces a previously running monitor") {
    MemoryLimiter mlim;
    mlim.set(65536);
    mlim.set(65536);  // implicitly cancels the first
    expect(mlim.exceeded()).to_be_false();
    mlim.cancel();
}

TEST_CASE("MemoryLimiter destructor joins background thread without hanging") {
    {
        MemoryLimiter mlim;
        mlim.set(65536);
    }  // destructor calls cancel()
}

TEST_CASE("MemoryLimiter fires when limit is set below current RSS") {
    const auto current = memlim::current_memory_bytes();
    if (current == 0) {
        // Can't sample RSS on this system — skip.
        return;
    }

    // Set threshold 1 byte below the current RSS to guarantee immediate breach.
    const std::size_t threshold_mb = (current / memlim::BYTES_PER_MB);

    std::atomic<bool> called{false};
    MemoryLimiter mlim;
    mlim.set(threshold_mb, [&called] { called.store(true, std::memory_order_release); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!mlim.exceeded() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{150});
    }
    expect(mlim.exceeded()).to_be_true();
    expect(called.load()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// Global memory limit
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Global memory limit — set and cancel")

TEST_CASE("memory_reached() is false at startup") {
    reset_global_memory();
    expect(global_limits::memory_reached()).to_be_false();
}

TEST_CASE("set_memory_limit with a very high threshold does not immediately set the flag") {
    reset_global_memory();
    set_memory_limit(65536);  // 64 GB — won't fire
    expect(global_limits::memory_reached()).to_be_false();
    cancel_memory_limit();
}

TEST_CASE("cancel_memory_limit is idempotent") {
    reset_global_memory();
    set_memory_limit(65536);
    expect_no_throw(cancel_memory_limit());
    expect_no_throw(cancel_memory_limit());
}

TEST_CASE("global memory flag fires when threshold is below current RSS") {
    reset_global_memory();
    const auto current = memlim::current_memory_bytes();
    if (current == 0) {
        return;  // Can't sample RSS on this system — skip.
    }

    const std::size_t threshold_mb = current / memlim::BYTES_PER_MB;

    std::atomic<bool> called{false};
    set_memory_limit(threshold_mb, [&called] { called.store(true, std::memory_order_release); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!global_limits::memory_reached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{150});
    }
    bool fired = global_limits::memory_reached();
    reset_global_memory();
    expect(fired).to_be_true();
    expect(called.load()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// memlim helpers
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("memlim helpers")

TEST_CASE("current_memory_bytes returns a non-zero value") {
    // Any real process uses some memory; 0 means a sampling failure.
    const auto bytes = memlim::current_memory_bytes();
    expect(bytes > 0).to_be_true();
}

TEST_CASE("current_memory_usage returns a positive value") {
    const auto usage = memlim::current_memory_usage();
    expect(usage > 0).to_be_true();
}
