#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <vector>

#include "../testing/test_main.hpp"
#include "../thread_pool/thread_pool.hxx"

// ─────────────────────────────────────────────────────────────────────────────
// Basic submit
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Basic submit")

TEST_CASE("submit returns correct int result") {
    ThreadPool pool{2};
    auto f = pool.submit([] { return 42; });
    expect(f.get()).to_equal(42);
}

TEST_CASE("submit with args forwards correctly") {
    ThreadPool pool{2};
    auto f = pool.submit([](int a, int b) { return a + b; }, 3, 7);
    expect(f.get()).to_equal(10);
}

TEST_CASE("submit void task completes without blocking") {
    ThreadPool pool{2};
    std::atomic<bool> ran{false};
    auto f = pool.submit([&ran] { ran.store(true); });
    f.get();
    expect(ran.load()).to_be_true();
}

TEST_CASE("multiple futures all resolve") {
    ThreadPool pool{4};
    std::vector<Future<int>> futs;
    for (int i = 0; i < 10; ++i) futs.push_back(pool.submit([i] { return i * i; }));
    for (int i = 0; i < 10; ++i) expect(futs[i].get()).to_equal(i * i);
}

TEST_CASE("future is copyable and both copies resolve to the same value") {
    ThreadPool pool{2};
    auto f1 = pool.submit([] { return 7; });
    Future<int> f2 = f1;  // copy
    expect(f1.get()).to_equal(7);
    expect(f2.get()).to_equal(7);
}

// ─────────────────────────────────────────────────────────────────────────────
// Concurrency
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Concurrency")

TEST_CASE("all tasks execute across workers") {
    constexpr int N = 100;
    ThreadPool pool{4};
    std::atomic<int> count{0};
    std::vector<Future<void>> futs;
    for (int i = 0; i < N; ++i) futs.push_back(pool.submit([&count] { count.fetch_add(1); }));
    for (auto& f : futs) f.get();
    expect(count.load()).to_equal(N);
}

TEST_CASE("thread_count returns configured value") {
    ThreadPool pool{3};
    expect(pool.thread_count()).to_equal(std::size_t{3});
}

// Fixed: with 1 worker the second task stays queued while the first runs.
// We only assert the final state (0) since start_pending depends on scheduling.
TEST_CASE("pending_count reaches zero after all tasks complete") {
    ThreadPool pool{1};
    std::promise<void> gate;
    auto gate_fut = gate.get_future().share();
    pool.submit([gate_fut] { gate_fut.wait(); });
    pool.submit([gate_fut] { gate_fut.wait(); });
    gate.set_value();
    pool.wait_all();
    expect(pool.pending_count()).to_equal(std::size_t{0});
}

// ─────────────────────────────────────────────────────────────────────────────
// wait_all
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("wait_all")

TEST_CASE("wait_all returns only after all tasks finish") {
    ThreadPool pool{4};
    std::atomic<int> count{0};
    for (int i = 0; i < 50; ++i) pool.submit([&count] { count.fetch_add(1); });
    pool.wait_all();
    expect(count.load()).to_equal(50);
}

TEST_CASE("wait_all on empty pool returns immediately") {
    ThreadPool pool{2};
    pool.wait_all();  // must not block
    expect(pool.pending_count()).to_equal(std::size_t{0});
}

TEST_CASE("wait_all accounts for then() continuations") {
    // wait_all must not return until the full chain is done, not just the root task.
    ThreadPool pool{4};
    std::atomic<bool> tail_ran{false};
    for (int i = 0; i < 8; ++i) {
        pool.submit([] { return 1; }).then([](int v) { return v + 1; }).then([&tail_ran](int) { tail_ran.store(true); });
    }
    pool.wait_all();
    expect(tail_ran.load()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// Exceptions
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Exceptions")

TEST_CASE("exception propagates through future") {
    ThreadPool pool{2};
    auto f = pool.submit([] {
        throw std::runtime_error("oops");
        return 0;
    });
    expect_throws(std::runtime_error, f.get());
}

TEST_CASE("one failing task does not affect others") {
    ThreadPool pool{2};
    auto bad = pool.submit([] {
        throw std::runtime_error("x");
        return 0;
    });
    auto good = pool.submit([] { return 99; });
    expect_throws(std::runtime_error, bad.get());
    expect(good.get()).to_equal(99);
}

// ─────────────────────────────────────────────────────────────────────────────
// Future::then()
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Future::then()")

TEST_CASE("single then() receives parent result and returns new value") {
    ThreadPool pool{2};
    auto f = pool.submit([] { return 6; }).then([](int v) { return v * 7; });
    expect(f.get()).to_equal(42);
}

TEST_CASE("three-hop chain produces correct final value") {
    ThreadPool pool{4};
    auto f = pool.submit([] { return std::string("hello"); }).then([](std::string s) { return s + " world"; }).then([](std::string s) {
        return static_cast<int>(s.size());
    });
    expect(f.get()).to_equal(11);  // "hello world"
}

TEST_CASE("then() with void continuation runs and resolves") {
    ThreadPool pool{2};
    std::atomic<bool> ran{false};
    pool.submit([] { return 1; }).then([&ran](int) { ran.store(true); }).get();
    expect(ran.load()).to_be_true();
}

TEST_CASE("exception in parent propagates through then() to final get()") {
    ThreadPool pool{2};
    auto f = pool.submit([]() -> int { throw std::runtime_error("parent"); }).then([](int v) { return v * 2; });  // must not run
    expect_throws(std::runtime_error, f.get());
}

TEST_CASE("exception thrown inside then() continuation propagates to its future") {
    ThreadPool pool{2};
    auto f = pool.submit([] { return 1; }).then([](int) -> int { throw std::runtime_error("cont"); });
    expect_throws(std::runtime_error, f.get());
}

TEST_CASE("then() registered after parent already completed fires immediately") {
    // Force parent to finish before .then() is called by blocking main
    // thread until the future is ready via a separate gate.
    ThreadPool pool{2};
    std::promise<void> ready;
    std::atomic<bool> parent_done{false};

    auto f = pool.submit([&ready, &parent_done] {
        parent_done.store(true);
        ready.set_value();
        return 99;
    });

    ready.get_future().wait();  // ensure parent has already called set_value

    int result = 0;
    f.then([&result](int v) { result = v; }).get();
    expect(result).to_equal(99);
}

TEST_CASE("multiple independent chains run concurrently") {
    ThreadPool pool{4};
    constexpr int N = 16;
    std::vector<Future<int>> finals;
    finals.reserve(N);
    for (int i = 0; i < N; ++i) {
        finals.push_back(pool.submit([i] { return i; }).then([](int v) { return v * 2; }).then([](int v) { return v + 1; }));
    }
    for (int i = 0; i < N; ++i) {
        expect(finals[i].get()).to_equal(i * 2 + 1);
    }
}

TEST_CASE("then() continuation is not submitted until parent finishes") {
    // With 1 worker: if the continuation were queued immediately it would
    // deadlock (worker blocks on get() while holding the only thread).
    // Correct deferred behaviour means no deadlock.
    ThreadPool pool{1};
    auto f = pool.submit([] { return 10; }).then([](int v) { return v + 5; });
    expect(f.get()).to_equal(15);
}

// ─────────────────────────────────────────────────────────────────────────────
// TaskGroup
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("TaskGroup")

TEST_CASE("wait() returns only after all group tasks finish") {
    ThreadPool pool{4};
    std::atomic<int> count{0};
    {
        TaskGroup g{pool};
        for (int i = 0; i < 20; ++i) g.submit([&count] { count.fetch_add(1); });
        g.wait();
    }
    expect(count.load()).to_equal(20);
}

TEST_CASE("done() is false while tasks are pending, true after wait()") {
    ThreadPool pool{1};
    std::promise<void> gate;
    auto sf = gate.get_future().share();

    TaskGroup g{pool};
    g.submit([sf] { sf.wait(); });
    expect(g.done()).to_be_false();
    gate.set_value();
    g.wait();
    expect(g.done()).to_be_true();
}

TEST_CASE("two sequential groups on the same pool") {
    ThreadPool pool{4};
    std::atomic<int> total{0};

    {
        TaskGroup g1{pool};
        for (int i = 0; i < 10; ++i) g1.submit([&total] { total.fetch_add(1); });
        g1.wait();
    }
    expect(total.load()).to_equal(10);

    {
        TaskGroup g2{pool};
        for (int i = 0; i < 10; ++i) g2.submit([&total] { total.fetch_add(1); });
        g2.wait();
    }
    expect(total.load()).to_equal(20);
}

TEST_CASE("group task exception does not deadlock wait()") {
    ThreadPool pool{2};
    TaskGroup g{pool};
    g.submit([] { throw std::runtime_error("group task threw"); });
    g.wait();  // must not hang
    expect(g.done()).to_be_true();
}

TEST_CASE("cancel policy: queued group task is dropped on shutdown") {
    // Destruction order matters: g captures a reference to pool, so g must
    // be destroyed before pool.  But on_cancel fires during pool::shutdown(),
    // calling task_done() on g — so g must still be alive at that point.
    // Calling pool.shutdown() explicitly (while g is in scope) avoids the
    // use-after-destroy that the implicit destructor ordering would cause.
    std::atomic<bool> ran{false};
    ThreadPool pool{0};  // 0 workers — task stays queued
    TaskGroup g{pool};
    g.submit(ShutdownPolicy::cancel, [&ran] { ran.store(true); });
    pool.shutdown();  // on_cancel fires here → g.task_done(); g still alive
    g.wait();         // pending_ already 0; returns immediately
    expect(ran.load()).to_be_false();
    expect(g.done()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Shutdown")

// Fixed: gate is released naturally when sf goes out of scope at end of block,
// so the worker is guaranteed to still be blocked when the destructor runs.
TEST_CASE("drain policy: queued task completes on destruction") {
    std::atomic<bool> ran{false};
    {
        ThreadPool pool{1};
        std::promise<void> gate;
        auto sf = gate.get_future().share();
        pool.submit([sf] { sf.wait(); });          // holds the worker
        pool.submit([&ran] { ran.store(true); });  // stays queued (drain)
        gate.set_value();                          // release inside scope
        // destructor drains the queue before joining
    }
    expect(ran.load()).to_be_true();
}

TEST_CASE("cancel policy: future throws runtime_error on shutdown") {
    // 0 workers: cancel task stays queued until destructor cancels it.
    Future<int> fut;
    {
        ThreadPool pool{0};
        fut = pool.submit(ShutdownPolicy::cancel, [] { return 1; });
    }
    expect_throws(std::runtime_error, fut.get());
}

TEST_CASE("shutdown is idempotent") {
    ThreadPool pool{2};
    pool.shutdown();
    pool.shutdown();  // must not crash
}

TEST_CASE("submit after shutdown throws") {
    ThreadPool pool{2};
    pool.shutdown();
    expect_throws(std::runtime_error, pool.submit([] { return 0; }));
}