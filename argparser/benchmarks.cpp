/**
 * @file benchmarks.cpp
 * @brief Benchmarks for ArgParser — registration, parsing, and value retrieval.
 *
 * Build (C++20):
 *   g++ -std=c++20 -O2 benchmarks.cpp -o benchmarks && ./benchmarks
 *
 * Structure
 * ─────────
 *  1  Argument registration  — cost of add<Name, T>()
 *  2  Parsing                — parse() for 1, 4, and 8 arguments
 *  3  Value retrieval        — get<Name, T>() after parse
 *  4  Validation overhead    — min/max and choices checks
 */

#include <string>
#include <vector>

#include "../benchmarking/bench_main.hpp"
#include "argparser.hxx"

using namespace utilz;

using benchmark::DoNotOptimize;

// ─────────────────────────────────────────────────────────────────────────────
// Shared argv builders
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct Args {
    std::vector<std::string> strs;
    std::vector<char*> ptrs;

    Args(std::initializer_list<const char*> args) {
        for (auto s : args) {
            strs.emplace_back(s);
        }
        for (auto& s : strs) {
            ptrs.push_back(s.data());
        }
    }

    int argc() const { return static_cast<int>(ptrs.size()); }
    char** argv() { return ptrs.data(); }
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Suite 1 — Argument registration
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("1 — Argument registration")

BENCH_CASE_N("ArgParser construction (empty)", 50000) {
    for (auto _ : state) {
        cli::ArgParser parser("bench");
        DoNotOptimize(parser);
    }
}

BENCH_CASE_N("add<Name, int>() — register one argument", 50000) {
    for (auto _ : state) {
        cli::ArgParser parser("bench");
        DoNotOptimize(parser.add<"bm_reg_n", int>());
    }
}

BENCH_CASE_N("add four arguments of mixed types", 20000) {
    for (auto _ : state) {
        cli::ArgParser parser("bench");
        parser.add<"bm_reg4_n", int>();
        parser.add<"bm_reg4_lr", double>();
        parser.add<"bm_reg4_s", std::string>();
        parser.add<"bm_reg4_v", bool>();
        DoNotOptimize(parser);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 2 — Parsing
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("2 — Parsing")

BENCH_CASE("parse one int argument") {
    cli::ArgParser parser("bench");
    parser.add<"bm_p1_n", int>().default_val(0);

    Args args{"prog", "--bm_p1_n", "42"};

    for (auto _ : state) {
        parser.parse(args.argc(), args.argv());
        DoNotOptimize(parser);
    }
}

BENCH_CASE("parse four mixed-type arguments") {
    cli::ArgParser parser("bench");
    parser.add<"bm_p4_n", int>().default_val(0);
    parser.add<"bm_p4_lr", double>().default_val(0.0);
    parser.add<"bm_p4_s", std::string>().default_val(std::string{""});
    parser.add<"bm_p4_v", bool>().default_val(false);

    Args args{"prog", "--bm_p4_n", "10", "--bm_p4_lr", "0.01", "--bm_p4_s", "adam", "--bm_p4_v", "true"};

    for (auto _ : state) {
        parser.parse(args.argc(), args.argv());
        DoNotOptimize(parser);
    }
}

BENCH_CASE("parse with defaults only (no CLI args)") {
    cli::ArgParser parser("bench");
    parser.add<"bm_def_n", int>().default_val(1);
    parser.add<"bm_def_lr", double>().default_val(0.001);
    parser.add<"bm_def_s", std::string>().default_val(std::string{"sgd"});
    parser.add<"bm_def_v", bool>().default_val(false);

    Args args{"prog"};

    for (auto _ : state) {
        parser.parse(args.argc(), args.argv());
        DoNotOptimize(parser);
    }
}

BENCH_CASE("parse eight int arguments") {
    cli::ArgParser parser("bench");
    parser.add<"bm_p8_a", int>().default_val(0);
    parser.add<"bm_p8_b", int>().default_val(0);
    parser.add<"bm_p8_c", int>().default_val(0);
    parser.add<"bm_p8_d", int>().default_val(0);
    parser.add<"bm_p8_e", int>().default_val(0);
    parser.add<"bm_p8_f", int>().default_val(0);
    parser.add<"bm_p8_g", int>().default_val(0);
    parser.add<"bm_p8_h", int>().default_val(0);

    Args args{"prog",      "--bm_p8_a", "1",         "--bm_p8_b", "2",         "--bm_p8_c", "3",         "--bm_p8_d", "4",
              "--bm_p8_e", "5",         "--bm_p8_f", "6",         "--bm_p8_g", "7",         "--bm_p8_h", "8"};

    for (auto _ : state) {
        parser.parse(args.argc(), args.argv());
        DoNotOptimize(parser);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 3 — Value retrieval after parse
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("3 — Value retrieval")

BENCH_CASE("get<int> after parse") {
    cli::ArgParser parser("bench");
    parser.add<"bm_g_n", int>().default_val(7);

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());

    for (auto _ : state) {
        DoNotOptimize(parser.get<"bm_g_n", int>());
    }
}

BENCH_CASE("get<double> after parse") {
    cli::ArgParser parser("bench");
    parser.add<"bm_g_lr", double>().default_val(0.001);

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());

    for (auto _ : state) {
        DoNotOptimize(parser.get<"bm_g_lr", double>());
    }
}

BENCH_CASE("get<bool> after parse") {
    cli::ArgParser parser("bench");
    parser.add<"bm_g_v", bool>().default_val(true);

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());

    for (auto _ : state) {
        DoNotOptimize(parser.get<"bm_g_v", bool>());
    }
}

BENCH_CASE("get<string> after parse") {
    cli::ArgParser parser("bench");
    parser.add<"bm_g_s", std::string>().default_val(std::string{"adam"});

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());

    for (auto _ : state) {
        DoNotOptimize(parser.get<"bm_g_s", std::string>());
    }
}

BENCH_CASE("four mixed get() calls per iteration") {
    cli::ArgParser parser("bench");
    parser.add<"bm_gm_n", int>().default_val(1);
    parser.add<"bm_gm_lr", double>().default_val(0.01);
    parser.add<"bm_gm_s", std::string>().default_val(std::string{"sgd"});
    parser.add<"bm_gm_v", bool>().default_val(false);

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());

    for (auto _ : state) {
        DoNotOptimize(parser.get<"bm_gm_n", int>());
        DoNotOptimize(parser.get<"bm_gm_lr", double>());
        DoNotOptimize(parser.get<"bm_gm_s", std::string>());
        DoNotOptimize(parser.get<"bm_gm_v", bool>());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Suite 4 — Validation overhead
// ─────────────────────────────────────────────────────────────────────────────

BENCH_SUITE("4 — Validation overhead")

BENCH_CASE("parse with min/max range validation") {
    cli::ArgParser parser("bench");
    parser.add<"bm_val_n", int>().min(0).max(100).default_val(50);

    Args args{"prog", "--bm_val_n", "42"};

    for (auto _ : state) {
        parser.parse(args.argc(), args.argv());
        DoNotOptimize(parser);
    }
}

BENCH_CASE("parse with choices validation (3 options)") {
    cli::ArgParser parser("bench");
    parser.add<"bm_val_s", std::string>().allow<std::string>({"adam", "sgd", "rmsprop"});

    Args args{"prog", "--bm_val_s", "sgd"};

    for (auto _ : state) {
        parser.parse(args.argc(), args.argv());
        DoNotOptimize(parser);
    }
}
