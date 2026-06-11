#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <vector>

#include "../testing/test_main.hpp"
#include "../thread_pool/thread_pool.hxx"

using namespace utilz;

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
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Shutdown")

TEST_CASE("drain policy: queued task completes on destruction") {
    std::atomic<bool> ran{false};
    {
        ThreadPool pool{1};
        std::promise<void> gate;
        auto shared_fut = gate.get_future().share();
        pool.submit([shared_fut] { shared_fut.wait(); });  // holds the worker
        pool.submit([&ran] { ran.store(true); });          // stays queued (drain)
        gate.set_value();                                  // release inside scope
        // destructor drains the queue before joining
    }
    expect(ran.load()).to_be_true();
}

TEST_CASE("cancel policy: future throws runtime_error on shutdown") {
    // 0 workers: cancel task stays queued until destructor cancels it.
    Future<int> fut;
    {
        ThreadPool pool{0};
        fut = pool.submit(ShutdownPolicy::cancel, []() { return 1; });
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

// ─────────────────────────────────────────────────────────────────────────────
// Priority
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Priority")

TEST_CASE("high priority runs before normal when queued") {
    ThreadPool pool{1};
    std::promise<void> gate;
    auto shared_gate = gate.get_future().share();
    pool.submit([shared_gate] { shared_gate.wait(); });  // occupy the worker

    std::atomic<int> counter{0};
    std::atomic<int> normal_order{0};
    std::atomic<int> high_order{0};

    pool.submit([&counter, &normal_order] { normal_order = ++counter; });
    pool.submit(Priority::high, [&counter, &high_order] { high_order = ++counter; });

    gate.set_value();
    pool.wait_all();

    expect(high_order.load() < normal_order.load()).to_be_true();
}

TEST_CASE("low priority runs after normal when queued") {
    ThreadPool pool{1};
    std::promise<void> gate;
    auto shared_gate = gate.get_future().share();
    pool.submit([shared_gate] { shared_gate.wait(); });

    std::atomic<int> counter{0};
    std::atomic<int> normal_order{0};
    std::atomic<int> low_order{0};

    pool.submit([&counter, &normal_order] { normal_order = ++counter; });
    pool.submit(Priority::low, [&counter, &low_order] { low_order = ++counter; });

    gate.set_value();
    pool.wait_all();

    expect(low_order.load() > normal_order.load()).to_be_true();
}

TEST_CASE("priority with cancel shutdown policy cancels on shutdown") {
    Future<int> fut;
    {
        ThreadPool pool{0};
        fut = pool.submit(Priority::high, ShutdownPolicy::cancel, [] { return 1; });
    }
    expect_throws(std::runtime_error, fut.get());
}

// ─────────────────────────────────────────────────────────────────────────────
// Cancellation
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Cancellation")

TEST_CASE("cancel sets stop_requested inside task body") {
    ThreadPool pool{1};
    std::promise<void> started;
    std::atomic<bool> saw_cancel{false};

    auto fut = pool.submit([&started, &saw_cancel](std::stop_token stoken) {
        started.set_value();
        while (!stoken.stop_requested()) {
        }
        saw_cancel.store(true);
    });

    started.get_future().wait();
    fut.cancel();
    fut.get();
    expect(saw_cancel.load()).to_be_true();
}

TEST_CASE("task ignoring stop_token runs to completion after cancel") {
    ThreadPool pool{1};
    std::atomic<int> result{0};
    auto fut = pool.submit([&result] { result.store(42); });
    fut.cancel();
    fut.get();
    expect(result.load()).to_equal(42);
}

TEST_CASE("cancel on a future copy cancels the shared task") {
    ThreadPool pool{1};
    std::promise<void> started;
    std::atomic<bool> saw_cancel{false};

    auto fut1 = pool.submit([&started, &saw_cancel](std::stop_token stoken) {
        started.set_value();
        while (!stoken.stop_requested()) {
        }
        saw_cancel.store(true);
    });
    Future<void> fut2 = fut1;

    started.get_future().wait();
    fut2.cancel();
    fut1.get();
    expect(saw_cancel.load()).to_be_true();
}

TEST_CASE("cancel on default-constructed future is a no-op") {
    Future<int> fut;
    fut.cancel();  // must not crash
}

// ─────────────────────────────────────────────────────────────────────────────
// Bulk submit
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Bulk submit")

TEST_CASE("submit_each on a vector resolves all futures with correct values") {
    ThreadPool pool{4};
    std::vector<int> data{1, 2, 3, 4, 5};
    auto futs = pool.submit_each(data.begin(), data.end(), [](int x) { return x * x; });
    for (std::size_t i = 0; i < data.size(); ++i) expect(futs[i].get()).to_equal(data[static_cast<int>(i)] * data[static_cast<int>(i)]);
}

TEST_CASE("submit_n(100, f) yields futures[i].get() == i*i") {
    ThreadPool pool{4};
    auto futs = pool.submit_n(100, [](std::size_t i) { return i * i; });
    for (std::size_t i = 0; i < 100; ++i) expect(futs[i].get()).to_equal(i * i);
}

TEST_CASE("submit_each priority variant enqueues and resolves") {
    ThreadPool pool{4};
    std::vector<int> data{10, 20, 30};
    auto futs = pool.submit_each(Priority::high, data.begin(), data.end(), [](int x) { return x * 2; });
    expect(futs[0].get()).to_equal(20);
    expect(futs[1].get()).to_equal(40);
    expect(futs[2].get()).to_equal(60);
}

TEST_CASE("submit_each empty range returns empty vector") {
    ThreadPool pool{2};
    std::vector<int> empty;
    auto futs = pool.submit_each(empty.begin(), empty.end(), [](int x) { return x; });
    expect(futs.empty()).to_be_true();
}

TEST_CASE("submit_each with stop_token callable works correctly") {
    ThreadPool pool{2};
    std::vector<int> data{10, 20, 30};
    auto futs = pool.submit_each(data.begin(), data.end(), [](std::stop_token, int x) { return x * 2; });
    expect(futs[0].get()).to_equal(20);
    expect(futs[1].get()).to_equal(40);
    expect(futs[2].get()).to_equal(60);
}

// ─────────────────────────────────────────────────────────────────────────────
// Timed wait
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("Timed wait")

TEST_CASE("wait_for returns true when tasks finish in time") {
    ThreadPool pool{2};
    pool.submit([] {});
    bool done = pool.wait_for(std::chrono::seconds(5));
    expect(done).to_be_true();
}

TEST_CASE("wait_for returns false when tasks take too long") {
    ThreadPool pool{1};
    std::promise<void> gate;
    auto shared_gate = gate.get_future().share();
    pool.submit([shared_gate] { shared_gate.wait(); });
    bool done = pool.wait_for(std::chrono::milliseconds(10));
    expect(done).to_be_false();
    gate.set_value();
    pool.wait_all();
}

TEST_CASE("wait_for(0) on empty pool returns true immediately") {
    ThreadPool pool{2};
    bool done = pool.wait_for(std::chrono::seconds(0));
    expect(done).to_be_true();
}

TEST_CASE("wait_until with past deadline returns false on non-empty pool") {
    ThreadPool pool{1};
    std::promise<void> gate;
    auto shared_gate = gate.get_future().share();
    pool.submit([shared_gate] { shared_gate.wait(); });
    auto past = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    bool done = pool.wait_until(past);
    expect(done).to_be_false();
    gate.set_value();
    pool.wait_all();
}
