/**
 * @file test.cpp
 * @brief Unit tests for timelim::LocalTimeLimiter, timelim::GlobalTimeLimiter,
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

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Resets the global terminate condition and cancels the global limiter so that
// tests do not interfere with each other.
static void reset_global() {
    timelim::cancel_time_limit();
}

// ─────────────────────────────────────────────────────────────────────────────
// LocalTimeLimiter — initial state
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("LocalTimeLimiter — initial state")

TEST_CASE("default-constructed limiter is not expired") {
    timelim::LocalTimeLimiter lim;
    expect(lim.expired()).to_be_false();
}

TEST_CASE("cancel() on a never-started limiter does not throw") {
    timelim::LocalTimeLimiter lim;
    expect_no_throw(lim.cancel());
}

// ─────────────────────────────────────────────────────────────────────────────
// LocalTimeLimiter — set and cancel
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("LocalTimeLimiter — set and cancel")

TEST_CASE("after set() the limiter is not immediately expired") {
    timelim::LocalTimeLimiter lim;
    lim.set(std::chrono::seconds{10});
    expect(lim.expired()).to_be_false();
    lim.cancel();
}

TEST_CASE("after cancel() the limiter is not expired") {
    timelim::LocalTimeLimiter lim;
    lim.set(std::chrono::seconds{10});
    lim.cancel();
    expect(lim.expired()).to_be_false();
}

TEST_CASE("cancel() is idempotent") {
    timelim::LocalTimeLimiter lim;
    lim.set(std::chrono::seconds{10});
    expect_no_throw(lim.cancel());
    expect_no_throw(lim.cancel());
}

TEST_CASE("set() replaces a previously running timer") {
    timelim::LocalTimeLimiter lim;
    lim.set(std::chrono::seconds{10});
    lim.set(std::chrono::seconds{10});  // implicitly cancels the first
    expect(lim.expired()).to_be_false();
    lim.cancel();
}

TEST_CASE("destructor joins background thread without hanging") {
    // If this test completes, the destructor ran correctly.
    {
        timelim::LocalTimeLimiter lim;
        lim.set(std::chrono::seconds{10});
    }  // destructor calls cancel()
}

// ─────────────────────────────────────────────────────────────────────────────
// LocalTimeLimiter — expiry (timing-sensitive, takes ~1 s)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("LocalTimeLimiter — expiry")

TEST_CASE("limiter fires and expired() becomes true after the duration") {
    timelim::LocalTimeLimiter lim;
    lim.set(std::chrono::seconds{1});

    // Wait until expired or timeout after 2 seconds
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!lim.expired() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    expect(lim.expired()).to_be_true();
}

TEST_CASE("on_expire callback is invoked when the timer fires") {
    std::atomic<bool> called{false};
    timelim::LocalTimeLimiter lim;
    lim.set(std::chrono::seconds{1}, [&called] { called.store(true, std::memory_order_release); });

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!called.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    expect(called.load()).to_be_true();
}

TEST_CASE("callback is NOT invoked when the timer is cancelled before firing") {
    std::atomic<bool> called{false};
    timelim::LocalTimeLimiter lim;
    lim.set(std::chrono::seconds{10}, [&called] { called.store(true); });
    lim.cancel();
    expect(called.load()).to_be_false();
}

// ─────────────────────────────────────────────────────────────────────────────
// GlobalTimeLimiter — initial state
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("GlobalTimeLimiter — initial state")

TEST_CASE("LIMITS_CHECK_STOP() is 0 at startup (after reset)") {
    reset_global();
    expect(LIMITS_CHECK_STOP()).to_equal(0);
}

TEST_CASE("GlobalTimeLimiter::expired() is false at startup") {
    reset_global();
    expect(timelim::GlobalTimeLimiter::expired()).to_be_false();
}

// ─────────────────────────────────────────────────────────────────────────────
// GlobalTimeLimiter — set and cancel
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("GlobalTimeLimiter — set and cancel")

TEST_CASE("set_time_limit does not immediately set the global flag") {
    reset_global();
    timelim::set_time_limit(10);
    expect(LIMITS_CHECK_STOP()).to_equal(0);
    timelim::cancel_time_limit();
}

TEST_CASE("cancel_time_limit resets GLOBAL_TERMINATE_CONDITION to 0") {
    reset_global();
    timelim::set_time_limit(10);
    timelim::cancel_time_limit();
    expect(LIMITS_CHECK_STOP()).to_equal(0);
}

TEST_CASE("cancel_time_limit is idempotent") {
    reset_global();
    timelim::set_time_limit(10);
    expect_no_throw(timelim::cancel_time_limit());
    expect_no_throw(timelim::cancel_time_limit());
}

// ─────────────────────────────────────────────────────────────────────────────
// GlobalTimeLimiter — expiry (timing-sensitive, takes ~1 s)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("GlobalTimeLimiter — expiry")

TEST_CASE("global flag becomes non-zero after the duration") {
    reset_global();
    timelim::set_time_limit(1);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (LIMITS_CHECK_STOP() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    bool fired = LIMITS_CHECK_STOP() != 0;
    reset_global();
    expect(fired).to_be_true();
}

TEST_CASE("GlobalTimeLimiter::expired() returns true after the duration") {
    reset_global();
    timelim::set_time_limit(1);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!timelim::GlobalTimeLimiter::expired() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    bool fired = timelim::GlobalTimeLimiter::expired();
    reset_global();
    expect(fired).to_be_true();
}

TEST_CASE("LocalTimeLimiter::expired() reflects the global flag") {
    reset_global();
    timelim::LocalTimeLimiter local;
    // Local timer has a long deadline; it expires because the global fires.
    local.set(std::chrono::seconds{60});
    timelim::set_time_limit(1);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!local.expired() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
    }
    bool fired = local.expired();
    local.cancel();
    reset_global();
    expect(fired).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// memlim
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// GlobalTimeLimiter destructor
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("GlobalTimeLimiter destructor")

TEST_CASE("GlobalTimeLimiter destructor cancels a running timer") {
    {
        timelim::GlobalTimeLimiter limiter;
        limiter.set(std::chrono::seconds{60});
        // destructor calls cancel() when limiter goes out of scope
    }
    // verify no crash/hang
}

// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("memlim helpers")

TEST_CASE("set_memory_limit returns a bool (does not throw)") {
    // On macOS this returns false; on Linux it may return true.
    // We only verify it does not throw.
    expect_no_throw(memlim::set_memory_limit(512));
}

TEST_CASE("current_memory_usage does not throw") {
    expect_no_throw(memlim::current_memory_usage());
}

#ifdef __APPLE__
TEST_CASE("set_memory_limit returns false on macOS") {
    // Suppress the warning printed to stderr
    std::streambuf* old = std::cerr.rdbuf();
    std::ostringstream sink;
    std::cerr.rdbuf(sink.rdbuf());
    bool result = memlim::set_memory_limit(512);
    std::cerr.rdbuf(old);
    expect(result).to_be_false();
}

TEST_CASE("current_memory_usage returns -1 on macOS") {
    expect(memlim::current_memory_usage()).to_equal(std::ptrdiff_t{-1});
}
#endif
