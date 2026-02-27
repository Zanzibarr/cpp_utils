#pragma once

/**
 * @file scope_guard.hxx
 * @brief ScopeGuard class for executing a function on scope exit, with optional strategies for exception handling.
 * @version 1.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 */

#include <exception>
#include <utility>

enum class ScopeStrategy {
    Exit,    // Always run
    Fail,    // Run only if an exception is thrown
    Success  // Run only if no exception is thrown
};

template <typename F>
class ScopeGuard {
   public:
    ScopeGuard(F&& func, ScopeStrategy strategy = ScopeStrategy::Exit)
        : strategy(strategy), func(std::move(func)), initial_exceptions(std::uncaught_exceptions()), active(true) {}

    ~ScopeGuard() {
        if (!active) {
            return;
        }

        int current_exceptions = std::uncaught_exceptions();
        bool failed = current_exceptions > initial_exceptions;

        if (strategy == ScopeStrategy::Exit || (strategy == ScopeStrategy::Fail && failed) || (strategy == ScopeStrategy::Success && !failed)) {
            func();
        }
    }

    // Boilerplate: Disable copy, enable move
    ScopeGuard(const ScopeGuard&) = delete;
    auto operator=(const ScopeGuard&) -> ScopeGuard& = delete;
    ScopeGuard(ScopeGuard&& other) noexcept
        : strategy(other.strategy), func(std::move(other.func)), initial_exceptions(other.initial_exceptions), active(other.active) {
        other.active = false;
    }

    void dismiss() noexcept { active = false; }

   private:
    ScopeStrategy strategy;
    F func;
    int initial_exceptions;
    bool active{};
};

template <typename F>
auto on_scope_exit(F&& func) {
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(func), ScopeStrategy::Exit);
}

template <typename F>
auto on_scope_success(F&& func) {
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(func), ScopeStrategy::Success);
}

template <typename F>
auto on_scope_fail(F&& func) {
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(func), ScopeStrategy::Fail);
}