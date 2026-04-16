#pragma once

/**
 * @file thread_pool.hxx
 * @brief Efficient thread pool with future-based, continuation, and group-barrier synchronisation.
 * @version 1.0.0
 *
 * @details
 * `ThreadPool` manages a fixed set of worker threads and distributes tasks
 * across them.  Three submission modes are provided:
 *   - **submit()** — returns a `Future<R>` for per-task synchronisation,
 *     result / exception retrieval, and continuation chaining.
 *   - **Future<T>::then()** — chains a dependent task that runs as soon as
 *     the parent future resolves; returns a new `Future<U>`.
 *   - **TaskGroup** — collects a set of tasks behind a barrier; `wait()`
 *     blocks until every task in the group has completed.
 *
 * A `ShutdownPolicy` tag controls what happens to *queued* tasks on destruction:
 *   - **drain** (default) — queued tasks finish before threads are joined.
 *   - **cancel** — queued tasks are dropped; futures receive a runtime_error.
 *
 * ### Continuation dispatch
 * `Future<T>::then()` registers the continuation on the parent's shared state.
 * The continuation is submitted to the pool only when the parent task calls
 * `set_value()` — no worker thread ever blocks waiting for a predecessor.
 * `total_pending_` is incremented at submit time, so `wait_all()` is always safe.
 *
 * @code
 *   ThreadPool pool{4};
 *
 *   // per-task future
 *   auto f = pool.submit([] { return compute(); });
 *   int r = f.get();
 *
 *   // continuation chain — no main-thread blocking between stages
 *   auto g = pool.submit([] { return load(); })
 *                .then([](RawData d) { return process(d); });
 *   auto result = g.get();
 *
 *   // group barrier
 *   TaskGroup grp{pool};
 *   for (auto& item : data) grp.submit([&] { process(item); });
 *   grp.wait();
 * @endcode
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * Repository: https://github.com/Zanzibarr/cpp_utils
 *
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

// ── ShutdownPolicy ────────────────────────────────────────────────────────────

/**
 * @brief Controls the fate of queued (not yet started) tasks when the pool shuts down.
 *
 * Pass as the first argument to `submit()` to override the default (`drain`).
 *
 * @code
 *   pool.submit(task);                           // drain (default)
 *   pool.submit(ShutdownPolicy::cancel, task);   // cancel on shutdown
 * @endcode
 */
enum class ShutdownPolicy { drain, cancel };

// ── thread_pool_detail ────────────────────────────────────────────────────────

namespace thread_pool_detail {

struct Task {
    std::function<void()> body;
    std::function<void()> on_cancel;
    ShutdownPolicy policy;
};

/**
 * @brief Custom shared state replacing std::promise/shared_future.
 *
 * Supports registering a single continuation that fires inline from
 * set_value()/set_exception() — no worker thread ever blocks on get().
 */
template <typename T>
class SharedState {
   public:
    using Box = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

    void set_value(Box val = {}) {
        std::function<void()> cont;
        {
            std::lock_guard lk(mtx_);
            value_.emplace(std::move(val));
            cont = std::move(cont_);
        }
        cv_.notify_all();
        if (cont) {
            cont();
        }
    }

    void set_exception(std::exception_ptr ep) {
        std::function<void()> cont;
        {
            std::lock_guard lk(mtx_);
            ex_ = std::move(ep);
            cont = std::move(cont_);
        }
        cv_.notify_all();
        if (cont) {
            cont();  // continuation propagates exception via get()
        }
    }

    auto get() -> T {
        std::unique_lock lk(mtx_);
        cv_.wait(lk, [this] { return value_.has_value() || ex_ != nullptr; });
        if (ex_) {
            std::rethrow_exception(ex_);
        }
        if constexpr (!std::is_void_v<T>) {
            return std::move(*value_);
        }
    }

    /** Register a continuation. Returns false if already complete — caller submits immediately. */
    auto try_register(std::function<void()> cont) -> bool {
        std::lock_guard lk(mtx_);
        if (value_.has_value() || ex_ != nullptr) {
            return false;
        }
        cont_ = std::move(cont);
        return true;
    }

   private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::optional<Box> value_;
    std::exception_ptr ex_;
    std::function<void()> cont_;
};

}  // namespace thread_pool_detail

// ── Forward declarations ──────────────────────────────────────────────────────

class ThreadPool;
class TaskGroup;
template <typename T>
class Future;

// ── Future<T> ─────────────────────────────────────────────────────────────────

/**
 * @brief Copyable future returned by `ThreadPool::submit()`.
 *
 * Wraps a `shared_ptr<SharedState<T>>` so it can be captured by value in
 * lambdas stored in `std::function`.
 *
 * Chaining via `.then(f)` registers the continuation on the shared state;
 * it is submitted to the originating pool only when the parent task completes.
 * No worker thread ever blocks waiting for a predecessor.
 *
 * @code
 *   auto f = pool.submit([] { return load(); })
 *                .then([](RawData d) { return process(d); });
 *   auto r = f.get();
 * @endcode
 */
template <typename T>
class Future {
   public:
    Future() = default;
    ~Future() = default;
    Future(const Future&) = default;
    Future(Future&&) = default;
    auto operator=(const Future&) -> Future& = default;
    auto operator=(Future&&) -> Future& = default;

    /** @brief Block until the task completes and return its result. */
    auto get() const -> T { return state_->get(); }

    /**
     * @brief Register a continuation to run as soon as this future resolves.
     *
     * The continuation is submitted to the pool the moment the parent task
     * calls set_value() — no worker thread is parked waiting.
     *
     * @return A new `Future<U>` where `U = std::invoke_result_t<F, T>`.
     */
    template <typename F>
        requires std::invocable<std::decay_t<F>, T>
    auto then(F&& func) -> Future<std::invoke_result_t<std::decay_t<F>, T>>;

   private:
    Future(std::shared_ptr<thread_pool_detail::SharedState<T>> state, ThreadPool& pool) : state_(std::move(state)), pool_(&pool) {}

    friend class ThreadPool;
    template <typename U>
    friend class Future;

    std::shared_ptr<thread_pool_detail::SharedState<T>> state_{};
    ThreadPool* pool_{nullptr};
};

// ── ThreadPool ────────────────────────────────────────────────────────────────

/**
 * @brief Fixed-size thread pool.
 *
 * Non-copyable, non-movable.  Destructor calls `shutdown()`.
 */
class ThreadPool {
   public:
    // ── Construction ─────────────────────────────────────────────────────────

    /** @brief Create a pool with @p n worker threads (default: hardware concurrency). */
    explicit ThreadPool(std::size_t n = std::thread::hardware_concurrency()) {
        threads_.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            threads_.emplace_back([this](std::stop_token st) { worker_fn(std::move(st)); });
        }
    }

    ~ThreadPool() { shutdown(); }

    ThreadPool(const ThreadPool&) = delete;
    auto operator=(const ThreadPool&) -> ThreadPool& = delete;

    // ── Submit ───────────────────────────────────────────────────────────────

    /**
     * @brief Submit a callable; returns a `Future<R>` for the result.  Default policy: drain.
     * @throws std::runtime_error if the pool has been shut down.
     */
    template <typename F, typename... Args>
        requires std::invocable<std::decay_t<F>, std::decay_t<Args>...>
    auto submit(F&& f, Args&&... args) -> Future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        return Future<R>{submit_impl(ShutdownPolicy::drain, std::forward<F>(f), std::forward<Args>(args)...), *this};
    }

    /**
     * @brief Submit with an explicit shutdown policy.
     * @throws std::runtime_error if the pool has been shut down.
     */
    template <typename F, typename... Args>
        requires std::invocable<std::decay_t<F>, std::decay_t<Args>...>
    auto submit(ShutdownPolicy policy, F&& f, Args&&... args) -> Future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        return Future<R>{submit_impl(policy, std::forward<F>(f), std::forward<Args>(args)...), *this};
    }

    // ── Synchronisation ──────────────────────────────────────────────────────

    /** @brief Block until every submitted task (queued + running) has finished. */
    void wait_all() {
        std::unique_lock lock(wait_mutex_);
        wait_cv_.wait(lock, [this] { return total_pending_.load(std::memory_order_acquire) == 0; });
    }

    // ── Lifecycle ────────────────────────────────────────────────────────────

    /**
     * @brief Drain remaining tasks then join all threads.  Idempotent.
     *
     * `cancel`-policy tasks in the queue are dropped (futures receive a
     * runtime_error).  `drain`-policy tasks complete normally.
     */
    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            if (stopped_) {
                return;
            }
            stopped_ = true;

            for (auto it = queue_.begin(); it != queue_.end();) {
                if (it->policy == ShutdownPolicy::cancel) {
                    if (it->on_cancel) {
                        it->on_cancel();
                    }
                    total_pending_.fetch_sub(1, std::memory_order_relaxed);
                    it = queue_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        {
            std::lock_guard wl(wait_mutex_);
            wait_cv_.notify_all();
        }

        for (auto& t : threads_) t.request_stop();
        cv_.notify_all();
        threads_.clear();  // jthread destructor joins
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    /** @brief Number of worker threads. */
    auto thread_count() const noexcept -> std::size_t { return threads_.size(); }

    /** @brief Tasks currently queued or executing. */
    auto pending_count() const noexcept -> std::size_t { return total_pending_.load(std::memory_order_relaxed); }

   private:
    friend class TaskGroup;
    template <typename T>
    friend class Future;

    // ── Internal ─────────────────────────────────────────────────────────────

    template <typename F, typename... Args>
    auto submit_impl(ShutdownPolicy policy, F&& f, Args&&... args)
        -> std::shared_ptr<thread_pool_detail::SharedState<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>> {
        using R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        auto state = std::make_shared<thread_pool_detail::SharedState<R>>();
        push_task({[fn = std::forward<F>(f), tup = std::make_tuple(std::forward<Args>(args)...), state]() mutable {
                       try {
                           if constexpr (std::is_void_v<R>) {
                               std::apply(std::move(fn), std::move(tup));
                               state->set_value();
                           } else {
                               state->set_value(std::apply(std::move(fn), std::move(tup)));
                           }
                       } catch (...) {
                           state->set_exception(std::current_exception());
                       }
                   },
                   [state] { state->set_exception(std::make_exception_ptr(std::runtime_error("task cancelled"))); }, policy});
        return state;
    }

    void push_task(thread_pool_detail::Task task) {
        {
            std::lock_guard lock(mutex_);
            if (stopped_) {
                throw std::runtime_error("ThreadPool is stopped");
            }
            total_pending_.fetch_add(1, std::memory_order_relaxed);
            queue_.push_back(std::move(task));
        }
        cv_.notify_one();
    }

    void task_done() noexcept {
        if (total_pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard lock(wait_mutex_);
            wait_cv_.notify_all();
        }
    }

    void worker_fn(std::stop_token stoken) {
        while (true) {
            thread_pool_detail::Task task;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, stoken, [this] { return !queue_.empty(); });
                if (queue_.empty()) {
                    break;
                }
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            task.body();
            task_done();
        }
    }

    // ── Data ─────────────────────────────────────────────────────────────────

    std::deque<thread_pool_detail::Task> queue_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::vector<std::jthread> threads_;
    std::atomic<std::size_t> total_pending_{0};
    bool stopped_{false};
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
};

// ── Future<T>::then() — defined after ThreadPool ──────────────────────────────

template <typename T>
template <typename F>
    requires std::invocable<std::decay_t<F>, T>
auto Future<T>::then(F&& func) -> Future<std::invoke_result_t<std::decay_t<F>, T>> {
    using U = std::invoke_result_t<std::decay_t<F>, T>;

    auto new_state = std::make_shared<thread_pool_detail::SharedState<U>>();

    // Wrap in shared_ptr so do_submit is copyable for storage in std::function.
    // parent_state->get() is non-blocking here: the parent set its value before
    // firing this continuation.
    auto body = std::make_shared<std::function<void()>>([parent_state = state_, new_state, fn = std::forward<F>(func)]() mutable {
        try {
            if constexpr (std::is_void_v<U>) {
                fn(parent_state->get());
                new_state->set_value();
            } else {
                new_state->set_value(fn(parent_state->get()));
            }
        } catch (...) {
            new_state->set_exception(std::current_exception());
        }
    });

    auto do_submit = [pool = pool_, body]() { pool->push_task({[body]() { (*body)(); }, nullptr, ShutdownPolicy::drain}); };

    if (!state_->try_register(do_submit)) {
        do_submit();
    }
    return Future<U>{std::move(new_state), *pool_};
}

// ── TaskGroup ─────────────────────────────────────────────────────────────────

/**
 * @brief Group of tasks behind a barrier — `wait()` returns only when all finish.
 *
 * The TaskGroup must not outlive the `ThreadPool` it references.
 * Exceptions thrown by group tasks are silently swallowed (use `submit()` with
 * a future if exception propagation is required).
 *
 * Non-copyable, non-movable.
 *
 * @code
 *   TaskGroup g{pool};
 *   for (auto& item : data) g.submit([&] { process(item); });
 *   g.wait();
 * @endcode
 */
class TaskGroup {
   public:
    explicit TaskGroup(ThreadPool& pool) : pool_(pool) {}

    TaskGroup(const TaskGroup&) = delete;
    auto operator=(const TaskGroup&) -> TaskGroup& = delete;

    // ── Submit ───────────────────────────────────────────────────────────────

    /** @brief Submit a task to the group.  Default policy: drain. */
    template <typename F, typename... Args>
        requires std::invocable<std::decay_t<F>, std::decay_t<Args>...>
    void submit(F&& f, Args&&... args) {
        submit_impl(ShutdownPolicy::drain, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /** @brief Submit with an explicit shutdown policy. */
    template <typename F, typename... Args>
        requires std::invocable<std::decay_t<F>, std::decay_t<Args>...>
    void submit(ShutdownPolicy policy, F&& f, Args&&... args) {
        submit_impl(policy, std::forward<F>(f), std::forward<Args>(args)...);
    }

    // ── Synchronisation ──────────────────────────────────────────────────────

    /** @brief Block until all tasks submitted to this group have finished (or been cancelled). */
    void wait() {
        std::unique_lock lock(cv_mutex_);
        cv_.wait(lock, [this] { return pending_.load(std::memory_order_acquire) == 0; });
    }

    /** @brief Non-blocking check — true if all submitted tasks have finished. */
    auto done() const noexcept -> bool { return pending_.load(std::memory_order_acquire) == 0; }

   private:
    template <typename F, typename... Args>
    void submit_impl(ShutdownPolicy policy, F&& f, Args&&... args) {
        pending_.fetch_add(1, std::memory_order_relaxed);

        auto body = [fn = std::forward<F>(f), args_tup = std::make_tuple(std::forward<Args>(args)...), this]() mutable {
            try {
                std::apply(std::move(fn), std::move(args_tup));
            } catch (...) {
            }
            task_done();
        };

        pool_.push_task({std::move(body), [this] { task_done(); }, policy});
    }

    void task_done() noexcept {
        if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard lock(cv_mutex_);
            cv_.notify_all();
        }
    }

    ThreadPool& pool_;
    std::atomic<std::size_t> pending_{0};
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};
