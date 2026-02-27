#pragma once

/**
 * @file test_framework.hxx
 * @brief Simple testing framework with basic assertions and colored output.
 * @version 1.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// ANSI colors
// ─────────────────────────────────────────────────────────────────────────────
namespace testing::color {

inline auto enabled() -> bool {
#ifdef _WIN32
    return false;
#else
    static bool val = (isatty(fileno(stdout)) != 0);
    return val;
#endif
}

inline auto green(std::string_view str) -> std::string { return enabled() ? "\033[32m" + std::string(str) + "\033[0m" : std::string(str); }
inline auto red(std::string_view str) -> std::string { return enabled() ? "\033[31m" + std::string(str) + "\033[0m" : std::string(str); }
inline auto yellow(std::string_view str) -> std::string { return enabled() ? "\033[33m" + std::string(str) + "\033[0m" : std::string(str); }
inline auto bold(std::string_view str) -> std::string { return enabled() ? "\033[1m" + std::string(str) + "\033[0m" : std::string(str); }
inline auto dim(std::string_view str) -> std::string { return enabled() ? "\033[2m" + std::string(str) + "\033[0m" : std::string(str); }

}  // namespace testing::color

// ─────────────────────────────────────────────────────────────────────────────
// assertion_error
// ─────────────────────────────────────────────────────────────────────────────
namespace testing {

struct assertion_error : std::exception {
    std::string message;
    std::string file;
    int line{};

    assertion_error(std::string msg, std::string file_path, int line_num) : message(std::move(msg)), file(std::move(file_path)), line(line_num) {}

    [[nodiscard]] auto what() const noexcept -> const char* override { return message.c_str(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// expectation<T>
// ─────────────────────────────────────────────────────────────────────────────

namespace {
constexpr double DEFAULT_TOLERANCE = 1e-5;
}

template <typename T>
class expectation {
   public:
    expectation(const T& value, const char* file, int line) : value_(value), file_(file), line_(line) {}

    auto to_equal(const T& expected) -> expectation& {
        if (!(value_ == expected)) {
            std::ostringstream oss;
            oss << "expected: " << to_str(expected) << "\n"
                << "           got:      " << to_str(value_);
            fail(oss.str());
        }
        return *this;
    }

    auto not_to_equal(const T& expected) -> expectation& {
        if (value_ == expected) {
            fail("expected value to differ from: " + to_str(expected));
        }
        return *this;
    }

    auto to_be_true() -> expectation& {
        if (!static_cast<bool>(value_)) {
            fail("expected: true\n           got:      false");
        }
        return *this;
    }

    auto to_be_false() -> expectation& {
        if (static_cast<bool>(value_)) {
            fail("expected: false\n           got:      true");
        }
        return *this;
    }

    auto to_be_greater_than(const T& threshold) -> expectation& {
        if (!(value_ > threshold)) {
            fail(to_str(value_) + " is not greater than " + to_str(threshold));
        }
        return *this;
    }

    auto to_be_less_than(const T& threshold) -> expectation& {
        if (!(value_ < threshold)) {
            fail(to_str(value_) + " is not less than " + to_str(threshold));
        }
        return *this;
    }

    auto to_be_greater_or_equal(const T& threshold) -> expectation& {
        if (!(value_ >= threshold)) {
            fail(to_str(value_) + " is not >= " + to_str(threshold));
        }
        return *this;
    }

    auto to_be_less_or_equal(const T& threshold) -> expectation& {
        if (!(value_ <= threshold)) {
            fail(to_str(value_) + " is not <= " + to_str(threshold));
        }
        return *this;
    }

    auto to_approx_equal(const T& expected, const T& tolerance = static_cast<T>(DEFAULT_TOLERANCE)) -> expectation& {
        static_assert(std::is_floating_point_v<T>, "to_approx_equal() requires a floating point type");
        if (std::abs(value_ - expected) > tolerance) {
            std::ostringstream oss;
            oss << std::fixed << "expected: ~" << expected << " (+-" << tolerance << ")\n"
                << "           got:       " << value_;
            fail(oss.str());
        }
        return *this;
    }

    auto to_contain(std::string_view substr) -> expectation&
        requires std::is_convertible_v<T, std::string_view>
    {
        std::string_view str(value_);
        if (str.find(substr) == std::string_view::npos) {
            fail("\"" + to_str(value_) + "\" does not contain \"" + std::string(substr) + "\"");
        }
        return *this;
    }

   private:
    const T& value_;
    const char* file_;
    int line_;

    [[noreturn]] void fail(const std::string& msg) const { throw assertion_error(msg, file_, line_); }

    template <typename U>
    static auto to_str(const U& val) -> std::string {
        std::ostringstream oss;
        oss << val;
        return oss.str();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Exception helpers
// ─────────────────────────────────────────────────────────────────────────────

template <typename ExceptionType, typename Callable>
void check_throws(Callable&& func, const char* file, int line) {
    bool caught = false;
    try {
        std::forward<Callable>(func)();
    } catch (const ExceptionType&) {
        caught = true;
    } catch (...) {
        throw assertion_error(std::string("expected exception '") + typeid(ExceptionType).name() + "' but a different exception was thrown", file,
                              line);
    }
    if (!caught) {
        throw assertion_error(std::string("expected exception '") + typeid(ExceptionType).name() + "' but no exception was thrown", file, line);
    }
}

template <typename Callable>
void check_no_throw(Callable&& func, const char* file, int line) {
    try {
        std::forward<Callable>(func)();
    } catch (const std::exception& e) {
        throw assertion_error(std::string("expected no exception but got: ") + e.what(), file, line);
    } catch (...) {
        throw assertion_error("expected no exception but an unknown exception was thrown", file, line);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// test_registry
// ─────────────────────────────────────────────────────────────────────────────

struct test_case {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

class test_registry {
   public:
    static auto instance() -> test_registry& {
        static test_registry reg;
        return reg;
    }

    auto register_test(test_case tcase) -> void { tests_.push_back(std::move(tcase)); }

    auto run_all() -> int {
        print_header();

        int passed = 0;
        int failed = 0;
        std::string current_suite;

        for (const auto& tcase : tests_) {
            if (tcase.suite != current_suite) {
                current_suite = tcase.suite;
                std::cout << "\n  " << color::bold(color::yellow("SUITE: " + current_suite)) << "\n";
            }

            try {
                tcase.fn();
                std::cout << "    " << color::green("v") << "  " << tcase.name << "\n";
                ++passed;
            } catch (const assertion_error& e) {
                std::cout << "    " << color::red("x") << "  " << tcase.name << "\n";
                std::cout << color::dim("         " + std::string(e.message)) << "\n";
                std::cout << color::dim("         at: " + short_path(e.file) + ":" + std::to_string(e.line)) << "\n";
                ++failed;
            } catch (const std::exception& e) {
                std::cout << "    " << color::red("x") << "  " << tcase.name << "\n";
                std::cout << color::dim(std::string("         unexpected exception: ") + e.what()) << "\n";
                ++failed;
            } catch (...) {
                std::cout << "    " << color::red("x") << "  " << tcase.name << "\n";
                std::cout << color::dim("         unknown exception thrown") << "\n";
                ++failed;
            }
        }

        print_footer(passed, failed);
        return (failed > 0) ? 1 : 0;
    }

   private:
    std::vector<test_case> tests_;

    static void print_header() {
        std::cout << color::bold("\n+-------------------------------------+\n");
        std::cout << color::bold("|  cpp_utils test runner               |\n");
        std::cout << color::bold("+-------------------------------------+\n");
    }

    static void print_footer(int passed, int failed) {
        constexpr int SEPARATOR_WIDTH = 42;
        std::cout << "\n" << std::string(SEPARATOR_WIDTH, '-') << "\n";
        std::cout << "  Results:  " << color::green(std::to_string(passed) + " passed") << "  |  "
                  << (failed > 0 ? color::red(std::to_string(failed) + " failed") : color::dim("0 failed")) << "  |  "
                  << std::to_string(passed + failed) << " total\n";
        std::cout << std::string(SEPARATOR_WIDTH, '-') << "\n\n";
    }

    static auto short_path(const std::string& path) -> std::string {
        auto pos = path.find_last_of("/\\");
        return (pos == std::string::npos) ? path : path.substr(pos + 1);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// auto_registrar — RAII helper that registers a test at static-init time
// ─────────────────────────────────────────────────────────────────────────────
struct auto_registrar {
    auto_registrar(const char* suite, const char* name, void (*func)()) {
        test_registry::instance().register_test({.suite = suite, .name = name, .fn = func});
    }
};

}  // namespace testing

// ─────────────────────────────────────────────────────────────────────────────
// Macro helpers — paste two tokens together
// ─────────────────────────────────────────────────────────────────────────────
#define _TS_CAT2(a, b) a##b
#define _TS_CAT(a, b) _TS_CAT2(a, b)

// ─────────────────────────────────────────────────────────────────────────────
// TEST_SUITE — sets the current suite name for all TEST_CASEs that follow.
//
// The current suite is stored as a file-scope pointer-to-char* that TEST_SUITE
// reassigns.  The pointer itself is declared once via a sentinel; subsequent
// TEST_SUITE calls just assign to it.  Each call also stores the literal in a
// uniquely-named variable (via __LINE__) so the pointer always points to valid
// storage.  The sentinel declaration uses an anonymous namespace to avoid
// clashes across translation units.
// ─────────────────────────────────────────────────────────────────────────────

// Declare the mutable pointer exactly once per translation unit.
namespace {
inline const char* _ts_current_suite_ = "<unset>";
}

#define TEST_SUITE(name)                                         \
    static const char* _TS_CAT(_ts_suite_str_, __LINE__) = name; \
    static int _TS_CAT(_ts_suite_set_, __LINE__) = (_ts_current_suite_ = _TS_CAT(_ts_suite_str_, __LINE__), 0);

// ─────────────────────────────────────────────────────────────────────────────
// TEST_CASE — uses __LINE__ for unique symbol names.
// Each TEST_CASE must start on its own line (standard practice anyway).
//
// Expansion produces:
//   (1) a function definition:  _ts_fn_<LINE>()  { ... }
//   (2) a static auto_registrar that fires at program startup
// ─────────────────────────────────────────────────────────────────────────────
#define TEST_CASE(test_name)                                                                                                 \
    static void _TS_CAT(_ts_fn_, __LINE__)();                                                                                \
    static ::testing::auto_registrar _TS_CAT(_ts_reg_, __LINE__)(_ts_current_suite_, test_name, _TS_CAT(_ts_fn_, __LINE__)); \
    static void _TS_CAT(_ts_fn_, __LINE__)()

// ─────────────────────────────────────────────────────────────────────────────
// Assertion macros
// ─────────────────────────────────────────────────────────────────────────────

#define expect(val) ::testing::expectation((val), __FILE__, __LINE__)

#define expect_throws(ExType, ...) ::testing::check_throws<ExType>([&] { __VA_ARGS__; }, __FILE__, __LINE__)

#define expect_no_throw(...) ::testing::check_no_throw([&] { __VA_ARGS__; }, __FILE__, __LINE__)