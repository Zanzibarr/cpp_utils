#pragma once

/**
 * @file thread_pool.hxx
 * @brief Thread pool with per-thread work-stealing queues, future-based result retrieval and barrier synchronisation.
 * @version 2.0.0
 *
 * @details
 * `ThreadPool` manages a fixed set of worker threads, each with its own task
 * queue.  Idle workers steal tasks from peers for load balancing.
 * Two submission modes are provided:
 *   - **submit()** — returns a `Future<R>` for per-task synchronisation and
 *     result / exception retrieval.
 *   - **submit(ShutdownPolicy::cancel, ...)** — task is dropped on shutdown;
 *     the future receives a `runtime_error`.
 *
 * A `ShutdownPolicy` tag controls what happens to *queued* tasks on destruction:
 *   - **drain** (default) — queued tasks finish before threads are joined.
 *   - **cancel** — queued tasks are dropped; futures receive a runtime_error.
 *
 * @code
 *   ThreadPool pool{4};
 *
 *   // per-task future
 *   auto f = pool.submit([] { return compute(); });
 *   int r = f.get();
 *
 *   // fire-and-forget batch with barrier
 *   for (auto& item : data) pool.submit([&] { process(item); });
 *   pool.wait_all();
 * @endcode
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * Repository: https://github.com/Zanzibarr/cpp_utils
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <tuple>
#include <vector>

namespace utilz {
// ── Priority ──────────────────────────────────────────────────────────────────

/**
 * @brief Scheduling priority for submitted tasks.
 *
 * Higher-priority tasks execute before lower-priority ones within the same queue.
 * Work stealing always takes the back of the victim's deque (lowest-priority task).
 *
 * @code
 *   pool.submit(Priority::high, f);                          // high, drain
 *   pool.submit(Priority::high, ShutdownPolicy::cancel, f);  // high, cancel
 * @endcode
 */
enum class Priority { low = 0, normal = 1, high = 2 };

// ── ShutdownPolicy ────────────────────────────────────────────────────────────

/**
 * @brief Controls the fate of queued (not yet started) tasks when the pool shuts down.
 *
 * @code
 *   pool.submit(task);                           // drain (default)
 *   pool.submit(ShutdownPolicy::cancel, task);   // cancel on shutdown
 * @endcode
 */
enum class ShutdownPolicy { drain, cancel };

// ── thread_pool_detail ────────────────────────────────────────────────────────

namespace thread_pool_detail {

template <typename F, typename... Args>
concept invocable_with_token = std::invocable<std::decay_t<F>, std::stop_token, std::decay_t<Args>...>;

// Primary template: callable takes Args only.
// Constrained specialisation wins when the callable also accepts a leading stop_token, giving the correct return type without any runtime check.
template <typename F, typename... Args>
struct result_type_impl : std::invoke_result<std::decay_t<F>, std::decay_t<Args>...> {};

template <typename F, typename... Args>
    requires invocable_with_token<F, Args...>
struct result_type_impl<F, Args...> {
    using type = std::invoke_result_t<std::decay_t<F>, std::stop_token, std::decay_t<Args>...>;
};

template <typename F, typename... Args>
using result_type = typename result_type_impl<F, Args...>::type;

struct Task {
    std::function<void()> body;
    std::function<void()> on_cancel;
    ShutdownPolicy policy;
    Priority priority;
};

struct WorkQueue {
    std::deque<Task> tasks;
    std::mutex mtx;
    // condition_variable_any (not condition_variable) — required for the stop_token overload of wait() used in worker_fn.
    std::condition_variable_any cv;
};

template <typename T>
class SharedState {
   public:
    // void is not storable in optional<>; monostate is a zero-size stand-in.
    using Box = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

    void set_value(Box val = {}) {
        {
            std::lock_guard lk(mtx_);
            value_.emplace(std::move(val));
        }
        cv_.notify_all();
    }

    void set_exception(std::exception_ptr ep) {
        {
            std::lock_guard lk(mtx_);
            ex_ = std::move(ep);
        }
        cv_.notify_all();
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

   private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::optional<Box> value_;
    std::exception_ptr ex_;
};

}  // namespace thread_pool_detail

// ── Forward declarations ──────────────────────────────────────────────────────

class ThreadPool;
template <typename T>
class Future;

// ── Future<T> ─────────────────────────────────────────────────────────────────

/**
 * @brief Copyable future returned by `ThreadPool::submit()`.
 *
 * Wraps a `shared_ptr<SharedState<T>>` so it can be captured by value in
 * lambdas or stored in containers. Multiple copies share the same result.
 *
 * @code
 *   auto f = pool.submit([] { return 42; });
 *   int r = f.get();
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

    /** @brief Request cooperative cancellation.  Safe no-op if the task ignores stop_token. */
    void cancel() { stop_src_.request_stop(); }

   private:
    explicit Future(std::shared_ptr<thread_pool_detail::SharedState<T>> state, std::stop_source stop_src)
        : state_(std::move(state)), stop_src_(std::move(stop_src)) {}

    friend class ThreadPool;

    std::shared_ptr<thread_pool_detail::SharedState<T>> state_{};
    // nostopstate: a default-constructed Future carries no token, so cancel() is a safe no-op.
    std::stop_source stop_src_{std::nostopstate};
};

// ── ThreadPool ────────────────────────────────────────────────────────────────

/**
 * @brief Fixed-size thread pool with per-thread work-stealing queues.
 *
 * Each worker owns a queue; idle workers steal from peers (back of victim's
 * deque) to balance load.  Non-copyable, non-movable.  Destructor calls `shutdown()`.
 */
class ThreadPool {
   public:
    // ── Construction ─────────────────────────────────────────────────────────

    /** @brief Create a pool with @p n worker threads (default: hardware concurrency). */
    explicit ThreadPool(std::size_t n = std::thread::hardware_concurrency()) {
        // Always at least one queue so the stealing loop (which modulos by queues_.size()) never divides by zero, even when the pool has zero worker
        // threads.
        std::size_t n_queues = std::max(n, std::size_t{1});
        queues_.reserve(n_queues);
        for (std::size_t i = 0; i < n_queues; ++i) {
            queues_.emplace_back(std::make_unique<thread_pool_detail::WorkQueue>());
        }

        threads_.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            threads_.emplace_back([this, i](std::stop_token stoken) { worker_fn(i, std::move(stoken)); });
        }
    }

    ~ThreadPool() { shutdown(); }

    ThreadPool(const ThreadPool&) = delete;
    auto operator=(const ThreadPool&) -> ThreadPool& = delete;

    // ── Submit ───────────────────────────────────────────────────────────────

    /**
     * @brief Submit a callable; returns a `Future<R>` for the result.  Default: normal priority, drain.
     *        Callable may optionally accept `std::stop_token` as its first parameter for cooperative cancellation.
     * @throws std::runtime_error if the pool has been shut down.
     */
    template <typename F, typename... Args>
        requires(std::invocable<std::decay_t<F>, std::decay_t<Args>...> || thread_pool_detail::invocable_with_token<F, Args...>)
    auto submit(F&& func, Args&&... args) -> Future<thread_pool_detail::result_type<F, Args...>> {
        using R = thread_pool_detail::result_type<F, Args...>;
        auto [state, stop_src] = submit_impl(Priority::normal, ShutdownPolicy::drain, std::forward<F>(func), std::forward<Args>(args)...);
        return Future<R>{std::move(state), std::move(stop_src)};
    }

    /**
     * @brief Submit with an explicit shutdown policy (normal priority).
     * @throws std::runtime_error if the pool has been shut down.
     */
    template <typename F, typename... Args>
        requires(std::invocable<std::decay_t<F>, std::decay_t<Args>...> || thread_pool_detail::invocable_with_token<F, Args...>)
    auto submit(ShutdownPolicy policy, F&& func, Args&&... args) -> Future<thread_pool_detail::result_type<F, Args...>> {
        using R = thread_pool_detail::result_type<F, Args...>;
        auto [state, stop_src] = submit_impl(Priority::normal, policy, std::forward<F>(func), std::forward<Args>(args)...);
        return Future<R>{std::move(state), std::move(stop_src)};
    }

    /**
     * @brief Submit with an explicit priority (drain policy).
     * @throws std::runtime_error if the pool has been shut down.
     */
    template <typename F, typename... Args>
        requires(std::invocable<std::decay_t<F>, std::decay_t<Args>...> || thread_pool_detail::invocable_with_token<F, Args...>)
    auto submit(Priority priority, F&& func, Args&&... args) -> Future<thread_pool_detail::result_type<F, Args...>> {
        using R = thread_pool_detail::result_type<F, Args...>;
        auto [state, stop_src] = submit_impl(priority, ShutdownPolicy::drain, std::forward<F>(func), std::forward<Args>(args)...);
        return Future<R>{std::move(state), std::move(stop_src)};
    }

    /**
     * @brief Submit with explicit priority and shutdown policy.
     * @throws std::runtime_error if the pool has been shut down.
     */
    template <typename F, typename... Args>
        requires(std::invocable<std::decay_t<F>, std::decay_t<Args>...> || thread_pool_detail::invocable_with_token<F, Args...>)
    auto submit(Priority priority, ShutdownPolicy policy, F&& func, Args&&... args) -> Future<thread_pool_detail::result_type<F, Args...>> {
        using R = thread_pool_detail::result_type<F, Args...>;
        auto [state, stop_src] = submit_impl(priority, policy, std::forward<F>(func), std::forward<Args>(args)...);
        return Future<R>{std::move(state), std::move(stop_src)};
    }

    // ── Bulk submit ──────────────────────────────────────────────────────────

    /** @brief Submit one task per element in [first, last); returns a vector of futures. */
    template <typename Iter, typename F>
    auto submit_each(Priority priority, ShutdownPolicy policy, Iter first, Iter last, F func)
        -> std::vector<Future<thread_pool_detail::result_type<F, std::iter_reference_t<Iter>>>> {
        using R = thread_pool_detail::result_type<F, std::iter_reference_t<Iter>>;
        std::vector<Future<R>> futs;
        for (auto it = first; it != last; ++it) {
            auto [state, stop_src] = submit_impl(priority, policy, func, *it);
            futs.emplace_back(Future<R>{std::move(state), std::move(stop_src)});
        }
        return futs;
    }

    template <typename Iter, typename F>
    auto submit_each(Priority priority, Iter first, Iter last, F&& func)
        -> std::vector<Future<thread_pool_detail::result_type<F, std::iter_reference_t<Iter>>>> {
        return submit_each(priority, ShutdownPolicy::drain, first, last, std::forward<F>(func));
    }

    template <typename Iter, typename F>
    auto submit_each(ShutdownPolicy policy, Iter first, Iter last, F&& func)
        -> std::vector<Future<thread_pool_detail::result_type<F, std::iter_reference_t<Iter>>>> {
        return submit_each(Priority::normal, policy, first, last, std::forward<F>(func));
    }

    template <typename Iter, typename F>
    auto submit_each(Iter first, Iter last, F&& func) -> std::vector<Future<thread_pool_detail::result_type<F, std::iter_reference_t<Iter>>>> {
        return submit_each(Priority::normal, ShutdownPolicy::drain, first, last, std::forward<F>(func));
    }

    /** @brief Submit n tasks indexed 0..n-1; callable receives `std::size_t i`. Returns a vector of futures. */
    template <typename F>
    auto submit_n(Priority priority, ShutdownPolicy policy, std::size_t n, F func)
        -> std::vector<Future<thread_pool_detail::result_type<F, std::size_t>>> {
        using R = thread_pool_detail::result_type<F, std::size_t>;
        std::vector<Future<R>> futs;
        futs.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            auto [state, stop_src] = submit_impl(priority, policy, func, i);
            futs.emplace_back(Future<R>{std::move(state), std::move(stop_src)});
        }
        return futs;
    }

    template <typename F>
    auto submit_n(Priority priority, std::size_t n, F&& func) -> std::vector<Future<thread_pool_detail::result_type<F, std::size_t>>> {
        return submit_n(priority, ShutdownPolicy::drain, n, std::forward<F>(func));
    }

    template <typename F>
    auto submit_n(ShutdownPolicy policy, std::size_t n, F&& func) -> std::vector<Future<thread_pool_detail::result_type<F, std::size_t>>> {
        return submit_n(Priority::normal, policy, n, std::forward<F>(func));
    }

    template <typename F>
    auto submit_n(std::size_t n, F&& func) -> std::vector<Future<thread_pool_detail::result_type<F, std::size_t>>> {
        return submit_n(Priority::normal, ShutdownPolicy::drain, n, std::forward<F>(func));
    }

    // ── Synchronisation ──────────────────────────────────────────────────────

    /** @brief Block until every submitted task (queued + running) has finished. */
    void wait_all() {
        std::unique_lock lock(wait_mutex_);
        wait_cv_.wait(lock, [this] { return total_pending_.load(std::memory_order_acquire) == 0; });
    }

    /** @brief Block at most @p timeout; returns true if all tasks finished, false if timed out. */
    template <typename Rep, typename Period>
    auto wait_for(std::chrono::duration<Rep, Period> timeout) -> bool {
        std::unique_lock lock(wait_mutex_);
        return wait_cv_.wait_for(lock, timeout, [this] { return total_pending_.load(std::memory_order_acquire) == 0; });
    }

    /** @brief Block until @p deadline; returns true if all tasks finished, false if timed out. */
    template <typename Clock, typename Duration>
    auto wait_until(std::chrono::time_point<Clock, Duration> deadline) -> bool {
        std::unique_lock lock(wait_mutex_);
        return wait_cv_.wait_until(lock, deadline, [this] { return total_pending_.load(std::memory_order_acquire) == 0; });
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
            std::lock_guard global_lk(global_mtx_);
            if (stopped_) {
                return;
            }
            stopped_ = true;

            for (auto& queue : queues_) {
                std::lock_guard q_lk(queue->mtx);
                for (auto it = queue->tasks.begin(); it != queue->tasks.end();) {
                    if (it->policy == ShutdownPolicy::cancel) {
                        if (it->on_cancel) {
                            it->on_cancel();
                        }
                        total_pending_.fetch_sub(1, std::memory_order_relaxed);
                        it = queue->tasks.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }

        {
            std::lock_guard wl(wait_mutex_);
            wait_cv_.notify_all();
        }

        for (auto& t : threads_) {
            t.request_stop();
        }
        for (auto& queue : queues_) {
            queue->cv.notify_all();
        }
        threads_.clear();  // jthread destructor joins
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    /** @brief Number of worker threads. */
    auto thread_count() const noexcept -> std::size_t { return threads_.size(); }

    /** @brief Tasks currently queued or executing. */
    auto pending_count() const noexcept -> std::size_t { return total_pending_.load(std::memory_order_relaxed); }

   private:
    template <typename T>
    friend class Future;

    // ── Internal ─────────────────────────────────────────────────────────────

    template <typename F, typename... Args>
    auto submit_impl(Priority priority, ShutdownPolicy policy, F&& func, Args&&... args)
        -> std::pair<std::shared_ptr<thread_pool_detail::SharedState<thread_pool_detail::result_type<F, Args...>>>, std::stop_source> {
        using R = thread_pool_detail::result_type<F, Args...>;
        auto state = std::make_shared<thread_pool_detail::SharedState<R>>();
        auto stop_src = std::stop_source{};
        push_task(
            {// Args are packed into a tuple because std::function<void()> erases the type;
             // std::apply unpacks them back into the call. mutable is required to move out of captures.
             [callable = std::forward<F>(func), tup = std::make_tuple(std::forward<Args>(args)...), state, token = stop_src.get_token()]() mutable {
                 try {
                     if constexpr (std::is_void_v<R>) {
                         if constexpr (thread_pool_detail::invocable_with_token<F, Args...>) {
                             std::apply([&callable, &token](
                                            auto&&... uargs) { std::invoke(std::move(callable), token, std::forward<decltype(uargs)>(uargs)...); },
                                        std::move(tup));
                         } else {
                             std::apply(std::move(callable), std::move(tup));
                         }
                         state->set_value();
                     } else {
                         if constexpr (thread_pool_detail::invocable_with_token<F, Args...>) {
                             state->set_value(std::apply(
                                 [&callable, &token](auto&&... uargs) {
                                     return std::invoke(std::move(callable), token, std::forward<decltype(uargs)>(uargs)...);
                                 },
                                 std::move(tup)));
                         } else {
                             state->set_value(std::apply(std::move(callable), std::move(tup)));
                         }
                     }
                 } catch (...) {
                     state->set_exception(std::current_exception());
                 }
             },
             [state] { state->set_exception(std::make_exception_ptr(std::runtime_error("task cancelled"))); }, policy, priority});
        return {state, std::move(stop_src)};
    }

    void push_task(thread_pool_detail::Task task) {
        std::lock_guard global_lk(global_mtx_);
        if (stopped_) {
            throw std::runtime_error("ThreadPool is stopped");
        }
        total_pending_.fetch_add(1, std::memory_order_relaxed);
        std::size_t idx = next_push_.fetch_add(1, std::memory_order_relaxed) % queues_.size();
        {
            std::lock_guard q_lk(queues_[idx]->mtx);
            // Insert before the first task with lower priority, keeping the deque
            // sorted descending. Workers pop_front, so the highest-priority task runs next.
            auto pos = std::find_if(queues_[idx]->tasks.begin(), queues_[idx]->tasks.end(),
                                    [&task](const thread_pool_detail::Task& existing) { return existing.priority < task.priority; });
            queues_[idx]->tasks.insert(pos, std::move(task));
        }
        queues_[idx]->cv.notify_one();
    }

    void task_done() noexcept {
        // fetch_sub returns the old value, so == 1 means this was the last task.
        // acq_rel: the release half makes the task's side-effects visible to whoever
        // wakes from wait_all/wait_for.
        if (total_pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard lock(wait_mutex_);
            wait_cv_.notify_all();
        }
    }

    void worker_fn(std::size_t my_idx, std::stop_token stoken) {
        while (true) {
            thread_pool_detail::Task task;
            bool got_task = false;

            // Try own queue first
            {
                std::lock_guard own_lk(queues_[my_idx]->mtx);
                if (!queues_[my_idx]->tasks.empty()) {
                    task = std::move(queues_[my_idx]->tasks.front());
                    queues_[my_idx]->tasks.pop_front();
                    got_task = true;
                }
            }

            // Steal from peers (back of victim = their lowest-priority work)
            if (!got_task) {
                for (std::size_t i = 1; i < queues_.size(); ++i) {
                    std::size_t victim = (my_idx + i) % queues_.size();
                    // try_to_lock: non-blocking — if the victim is busy, skip to the next one
                    // rather than stalling. Avoids priority inversion and convoy effects.
                    std::unique_lock steal_lk(queues_[victim]->mtx, std::try_to_lock);
                    if (steal_lk && !queues_[victim]->tasks.empty()) {
                        // Back = lowest-priority task: the victim loses its least-valuable work first.
                        task = std::move(queues_[victim]->tasks.back());
                        queues_[victim]->tasks.pop_back();
                        got_task = true;
                        break;
                    }
                }
            }

            if (got_task) {
                task.body();
                task_done();
                continue;
            }

            // Nothing to do — wait on own CV (wakes on new task or stop request)
            std::unique_lock wait_lk(queues_[my_idx]->mtx);
            // C++20 stop_token overload: returns when either the predicate is true
            // OR the stop has been requested, eliminating the need for a manual poll loop.
            queues_[my_idx]->cv.wait(wait_lk, stoken, [this, my_idx] { return !queues_[my_idx]->tasks.empty(); });
            if (queues_[my_idx]->tasks.empty()) {
                break;  // stop requested with no remaining work
            }
        }
    }

    // ── Data ─────────────────────────────────────────────────────────────────

    std::vector<std::unique_ptr<thread_pool_detail::WorkQueue>> queues_;
    std::mutex global_mtx_;
    std::atomic<std::size_t> next_push_{0};
    std::vector<std::jthread> threads_;
    std::atomic<std::size_t> total_pending_{0};
    bool stopped_{false};
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
};

}  // namespace utilz
