#include <stdexcept>
#include <string>

#include "../testing/test_main.hpp"

using namespace utilz;
using utilz::testing::assertion_error;

// Self-test: the framework verifies its own assertions by checking that
// failing expectations throw assertion_error (which the registry counts as
// a failure when uncaught).

// ─────────────────────────────────────────────────────────────────────────────
// expectation — passing paths
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("expectation passes")

TEST_CASE("to_equal / not_to_equal") {
    expect(2 + 2).to_equal(4);
    expect(std::string("abc")).to_equal(std::string("abc"));
    expect(1).not_to_equal(2);
}

TEST_CASE("boolean expectations") {
    expect(true).to_be_true();
    expect(false).to_be_false();
}

TEST_CASE("ordering expectations chain fluently") {
    expect(5).to_be_greater_than(1).to_be_less_than(10);
    expect(5).to_be_greater_or_equal(5).to_be_less_or_equal(5);
}

TEST_CASE("to_approx_equal honours the tolerance") {
    expect(1.0).to_approx_equal(1.0 + 1e-7);
    expect(100.0).to_approx_equal(100.5, 1.0);
}

TEST_CASE("to_contain matches substrings") { expect(std::string("hello world")).to_contain("world").to_contain("hello"); }

// ─────────────────────────────────────────────────────────────────────────────
// expectation — failing paths must throw assertion_error
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("expectation failures")

TEST_CASE("failing to_equal throws assertion_error") { expect_throws(assertion_error, expect(1).to_equal(2)); }

TEST_CASE("failing not_to_equal throws assertion_error") { expect_throws(assertion_error, expect(7).not_to_equal(7)); }

TEST_CASE("failing boolean expectations throw assertion_error") {
    expect_throws(assertion_error, expect(false).to_be_true());
    expect_throws(assertion_error, expect(true).to_be_false());
}

TEST_CASE("failing ordering expectations throw assertion_error") {
    expect_throws(assertion_error, expect(1).to_be_greater_than(2));
    expect_throws(assertion_error, expect(2).to_be_less_than(1));
    expect_throws(assertion_error, expect(1).to_be_greater_or_equal(2));
    expect_throws(assertion_error, expect(2).to_be_less_or_equal(1));
}

TEST_CASE("failing to_approx_equal throws assertion_error") { expect_throws(assertion_error, expect(1.0).to_approx_equal(2.0)); }

TEST_CASE("failing to_contain throws assertion_error") { expect_throws(assertion_error, expect(std::string("abc")).to_contain("xyz")); }

TEST_CASE("assertion_error carries the failure location") {
    try {
        expect(1).to_equal(2);
    } catch (const assertion_error& err) {
        expect(err.line).to_be_greater_than(0);
        expect(err.file).to_contain("test.cpp");
        expect(std::string(err.what()).empty()).to_be_false();
        return;
    }
    expect(false).to_be_true();  // unreachable — to_equal must have thrown
}

// ─────────────────────────────────────────────────────────────────────────────
// exception helpers
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("exception helpers")

namespace {
void throws_runtime() { throw std::runtime_error("boom"); }
void throws_nothing() {}
}  // namespace

TEST_CASE("expect_throws passes when the right exception is thrown") { expect_throws(std::runtime_error, throws_runtime()); }

TEST_CASE("expect_throws fails when nothing is thrown") { expect_throws(assertion_error, expect_throws(std::runtime_error, throws_nothing())); }

TEST_CASE("expect_throws fails on a different exception type") { expect_throws(assertion_error, expect_throws(std::logic_error, throws_runtime())); }

TEST_CASE("expect_no_throw passes for non-throwing code") { expect_no_throw(throws_nothing()); }

TEST_CASE("expect_no_throw fails when the code throws") { expect_throws(assertion_error, expect_no_throw(throws_runtime())); }
