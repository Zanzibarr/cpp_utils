#include "../scope_guard/scope_guard.hxx"
#include "../testing/test_main.hpp"

using namespace utilz;

// ─────────────────────────────────────────────────────────────────────────────
// EXIT strategy
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("EXIT strategy")

TEST_CASE("EXIT guard runs on normal scope exit") {
    int counter = 0;
    {
        auto g = on_scope_exit([&] { ++counter; });
    }
    expect(counter).to_equal(1);
}

TEST_CASE("EXIT guard runs even when an exception is thrown") {
    int counter = 0;
    try {
        auto g = on_scope_exit([&] { ++counter; });
        throw std::runtime_error("test");
    } catch (...) {
    }
    expect(counter).to_equal(1);
}

TEST_CASE("EXIT guard does not run after dismiss()") {
    int counter = 0;
    {
        auto g = on_scope_exit([&] { ++counter; });
        g.dismiss();
    }
    expect(counter).to_equal(0);
}

TEST_CASE("EXIT guard dismiss during exception still prevents execution") {
    int counter = 0;
    try {
        auto g = on_scope_exit([&] { ++counter; });
        g.dismiss();
        throw std::runtime_error("test");
    } catch (...) {
    }
    expect(counter).to_equal(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// SUCCESS strategy
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("SUCCESS strategy")

TEST_CASE("SUCCESS guard runs on normal scope exit") {
    int counter = 0;
    {
        auto g = on_scope_success([&] { ++counter; });
    }
    expect(counter).to_equal(1);
}

TEST_CASE("SUCCESS guard does NOT run when exception is thrown") {
    int counter = 0;
    try {
        auto g = on_scope_success([&] { ++counter; });
        throw std::runtime_error("test");
    } catch (...) {
    }
    expect(counter).to_equal(0);
}

TEST_CASE("SUCCESS guard does not run after dismiss()") {
    int counter = 0;
    {
        auto g = on_scope_success([&] { ++counter; });
        g.dismiss();
    }
    expect(counter).to_equal(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// FAIL strategy
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("FAIL strategy")

TEST_CASE("FAIL guard does NOT run on normal scope exit") {
    int counter = 0;
    {
        auto g = on_scope_fail([&] { ++counter; });
    }
    expect(counter).to_equal(0);
}

TEST_CASE("FAIL guard runs when exception is thrown") {
    int counter = 0;
    try {
        auto g = on_scope_fail([&] { ++counter; });
        throw std::runtime_error("test");
    } catch (...) {
    }
    expect(counter).to_equal(1);
}

TEST_CASE("FAIL guard does not run after dismiss(), even with exception") {
    int counter = 0;
    try {
        auto g = on_scope_fail([&] { ++counter; });
        g.dismiss();
        throw std::runtime_error("test");
    } catch (...) {
    }
    expect(counter).to_equal(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Direct ScopeGuard construction
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("ScopeGuard direct construction")

TEST_CASE("ScopeGuard default strategy is EXIT") {
    int counter = 0;
    {
        ScopeGuard g([&] { ++counter; });
    }
    expect(counter).to_equal(1);
}

TEST_CASE("ScopeGuard with explicit EXIT strategy runs on normal exit") {
    int counter = 0;
    {
        ScopeGuard g([&] { ++counter; }, ScopeStrategy::EXIT);
    }
    expect(counter).to_equal(1);
}

TEST_CASE("ScopeGuard with explicit SUCCESS strategy") {
    int counter = 0;
    {
        ScopeGuard g([&] { ++counter; }, ScopeStrategy::SUCCESS);
    }
    expect(counter).to_equal(1);
}

TEST_CASE("ScopeGuard with explicit FAIL strategy, no exception") {
    int counter = 0;
    {
        ScopeGuard g([&] { ++counter; }, ScopeStrategy::FAIL);
    }
    expect(counter).to_equal(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callable behaviour
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("callable behaviour")

TEST_CASE("guard callable can modify captured state") {
    std::string result;
    {
        auto g = on_scope_exit([&] { result = "done"; });
    }
    expect(result).to_equal(std::string("done"));
}

TEST_CASE("multiple guards run in reverse order (LIFO)") {
    std::string order;
    {
        auto g1 = on_scope_exit([&] { order += "1"; });
        auto g2 = on_scope_exit([&] { order += "2"; });
    }
    expect(order).to_equal(std::string("21"));
}

TEST_CASE("dismissed guard inside nested scope does not affect outer guard") {
    int counter = 0;
    {
        auto outer = on_scope_exit([&] { ++counter; });
        {
            auto inner = on_scope_exit([&] { ++counter; });
            inner.dismiss();
        }
    }
    expect(counter).to_equal(1);
}
