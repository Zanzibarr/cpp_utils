/**
 * @file test.cpp
 * @brief Unit tests for ParameterRegistry.
 *
 * Build (C++20):
 *   g++ -std=c++20 -O2 test.cpp -o test && ./test
 *
 * NOTE: `expect(reg.get<"name", T>())` cannot be used directly because the
 * preprocessor splits on the comma inside `<>`. Results are captured in local
 * variables first.
 */

#include "../testing/test_main.hpp"
#include "parameters.hxx"

// ─────────────────────────────────────────────────────────────────────────────
// set / get — basic types
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("set and get — basic types")

TEST_CASE("set and get double") {
    ParameterRegistry reg;
    reg.set<"t_double">(3.14);
    double v = reg.get<"t_double", double>();
    expect(v).to_approx_equal(3.14);
}

TEST_CASE("set and get int") {
    ParameterRegistry reg;
    reg.set<"t_int64">(42);
    int v = reg.get<"t_int64", int>();
    expect(v).to_equal(int{42});
}

TEST_CASE("set and get bool true") {
    ParameterRegistry reg;
    reg.set<"t_bool_t">(true);
    bool v = reg.get<"t_bool_t", bool>();
    expect(v).to_be_true();
}

TEST_CASE("set and get bool false") {
    ParameterRegistry reg;
    reg.set<"t_bool_f">(false);
    bool v = reg.get<"t_bool_f", bool>();
    expect(v).to_be_false();
}

TEST_CASE("set and get string from string literal") {
    ParameterRegistry reg;
    reg.set<"t_str">("hello");
    std::string v = reg.get<"t_str", std::string>();
    expect(v).to_equal(std::string{"hello"});
}

TEST_CASE("set and get string from std::string") {
    ParameterRegistry reg;
    std::string val = "world";
    reg.set<"t_str2">(val);
    std::string v = reg.get<"t_str2", std::string>();
    expect(v).to_equal(std::string{"world"});
}

// ─────────────────────────────────────────────────────────────────────────────
// Type coercions
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("type coercions")

TEST_CASE("int literal is coerced to int") {
    ParameterRegistry reg;
    reg.set<"c_int">(100);
    int v = reg.get<"c_int", int>();
    expect(v).to_equal(int{100});
}

TEST_CASE("float literal is coerced to double") {
    ParameterRegistry reg;
    reg.set<"c_float">(1.5f);
    double v = reg.get<"c_float", double>();
    expect(v).to_approx_equal(1.5);
}

TEST_CASE("negative int is coerced to int") {
    ParameterRegistry reg;
    reg.set<"c_neg">(-7);
    int v = reg.get<"c_neg", int>();
    expect(v).to_equal(int{-7});
}

TEST_CASE("bool is stored as bool, not int") {
    ParameterRegistry reg;
    reg.set<"c_boolt">(true);
    bool v = reg.get<"c_boolt", bool>();
    expect(v).to_be_true();
}

TEST_CASE("StoredAs explicit override: int literal stored as double") {
    ParameterRegistry reg;
    reg.set<"c_sto", double>(0);
    double v = reg.get<"c_sto", double>();
    expect(v).to_approx_equal(0.0);
}

TEST_CASE("StoredAs explicit override: large int stored as int") {
    ParameterRegistry reg;
    reg.set<"c_sto2", int>(999);
    int v = reg.get<"c_sto2", int>();
    expect(v).to_equal(int{999});
}

// ─────────────────────────────────────────────────────────────────────────────
// has()
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("has()")

TEST_CASE("has() returns false for unset parameter") {
    ParameterRegistry reg;
    expect(reg.has<"h_unset">()).to_be_false();
}

TEST_CASE("has() returns true after set()") {
    ParameterRegistry reg;
    reg.set<"h_set">(42);
    expect(reg.has<"h_set">()).to_be_true();
}

TEST_CASE("has() returns false for a different unset parameter") {
    ParameterRegistry reg;
    reg.set<"h_a">(1);
    expect(reg.has<"h_b">()).to_be_false();
}

// ─────────────────────────────────────────────────────────────────────────────
// get() error conditions
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("get() error conditions")

TEST_CASE("get() throws out_of_range for unset parameter") {
    ParameterRegistry reg;
    expect_throws(std::out_of_range, reg.get<"e_unset", double>());
}

TEST_CASE("get() throws runtime_error on type mismatch (int stored, double requested)") {
    ParameterRegistry reg;
    reg.set<"e_mismatch">(42);  // stored as int
    expect_throws(std::runtime_error, reg.get<"e_mismatch", double>());
}

TEST_CASE("get() throws runtime_error when bool stored and int requested") {
    ParameterRegistry reg;
    reg.set<"e_boolint">(true);
    expect_throws(std::runtime_error, reg.get<"e_boolint", int>());
}

// ─────────────────────────────────────────────────────────────────────────────
// Overwrite behaviour
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("overwrite behaviour")

TEST_CASE("set() again overwrites the previous value") {
    ParameterRegistry reg;
    reg.set<"ow_val">(1);
    reg.set<"ow_val">(99);
    int v = reg.get<"ow_val", int>();
    expect(v).to_equal(int{99});
}

TEST_CASE("set() again for string updates the value") {
    ParameterRegistry reg;
    reg.set<"ow_str">("first");
    reg.set<"ow_str">("second");
    std::string v = reg.get<"ow_str", std::string>();
    expect(v).to_equal(std::string{"second"});
}

TEST_CASE("has() remains true after overwrite") {
    ParameterRegistry reg;
    reg.set<"ow_has">(0);
    reg.set<"ow_has">(1);
    expect(reg.has<"ow_has">()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple parameters
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("multiple parameters")

TEST_CASE("all four supported types can be stored simultaneously") {
    ParameterRegistry reg;
    reg.set<"m_lr">(0.001);
    reg.set<"m_bs">(32);
    reg.set<"m_shuf">(true);
    reg.set<"m_opt">("adam");

    double lr = reg.get<"m_lr", double>();
    int bs = reg.get<"m_bs", int>();
    bool shuf = reg.get<"m_shuf", bool>();
    std::string opt = reg.get<"m_opt", std::string>();

    expect(lr).to_approx_equal(0.001);
    expect(bs).to_equal(int{32});
    expect(shuf).to_be_true();
    expect(opt).to_equal(std::string{"adam"});
}

TEST_CASE("parameters are independent — setting one does not affect another") {
    ParameterRegistry reg;
    reg.set<"ind_a">(10);
    reg.set<"ind_b">(20);
    int a = reg.get<"ind_a", int>();
    int b = reg.get<"ind_b", int>();
    expect(a).to_equal(int{10});
    expect(b).to_equal(int{20});
}

// ─────────────────────────────────────────────────────────────────────────────
// get_report / print_report
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("get_report")

TEST_CASE("get_report is empty when no parameters are set") {
    ParameterRegistry reg;
    expect(reg.get_report().empty()).to_be_true();
}

TEST_CASE("get_report has one row after one set()") {
    ParameterRegistry reg;
    reg.set<"r_one">(42);
    auto rows = reg.get_report();
    expect(rows.size()).to_equal(std::size_t{1});
}

TEST_CASE("get_report row has correct name") {
    ParameterRegistry reg;
    reg.set<"r_name">(1.0);
    auto rows = reg.get_report();
    expect(rows[0].name).to_equal(std::string{"r_name"});
}

TEST_CASE("get_report shows correct type for int") {
    ParameterRegistry reg;
    reg.set<"r_itype">(1);
    expect(reg.get_report()[0].type).to_equal(std::string{"int"});
}

TEST_CASE("get_report shows correct type for double") {
    ParameterRegistry reg;
    reg.set<"r_dtype">(1.0);
    expect(reg.get_report()[0].type).to_equal(std::string{"double"});
}

TEST_CASE("get_report shows correct type for bool") {
    ParameterRegistry reg;
    reg.set<"r_btype">(true);
    expect(reg.get_report()[0].type).to_equal(std::string{"bool"});
}

TEST_CASE("get_report shows correct type for string") {
    ParameterRegistry reg;
    reg.set<"r_stype">("x");
    expect(reg.get_report()[0].type).to_equal(std::string{"string"});
}

TEST_CASE("get_report preserves insertion order") {
    ParameterRegistry reg;
    reg.set<"r_ord_a">(1);
    reg.set<"r_ord_b">(2);
    reg.set<"r_ord_c">(3);
    auto rows = reg.get_report();
    expect(rows.size()).to_equal(std::size_t{3});
    expect(rows[0].name).to_equal(std::string{"r_ord_a"});
    expect(rows[1].name).to_equal(std::string{"r_ord_b"});
    expect(rows[2].name).to_equal(std::string{"r_ord_c"});
}

TEST_CASE("print_report does not throw") {
    ParameterRegistry reg;
    reg.set<"r_print">(42);
    expect_no_throw(reg.print_report());
}

TEST_CASE("cast to string does not throw") {
    ParameterRegistry reg;
    reg.set<"r_print">(42);
    expect_no_throw(static_cast<std::string>(reg));
}

// ─────────────────────────────────────────────────────────────────────────────
// global_params
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("global_params")

TEST_CASE("global_params returns the same singleton on repeated calls") {
    auto& a = global_params();
    auto& b = global_params();
    expect(&a == &b).to_be_true();
}

TEST_CASE("global_params supports set and get") {
    global_params().set<"gp_val">(int{7});
    int v = global_params().get<"gp_val", int>();
    expect(v).to_equal(int{7});
}
