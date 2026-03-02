/**
 * demo.cpp
 * ───────────────────────────────────────────────────────────────────────────
 * A comprehensive demo for ParameterRegistry.
 * Every public API call uses the compile-time ct_string template syntax.
 * Each section is self-contained and can be read independently.
 *
 * Compile (C++20):
 *   g++ -std=c++20 -O2 demo.cpp -o demo && ./demo
 */

#include <iostream>
#include <stdexcept>
#include <string>

#include "parameters.hxx"

// ─────────────────────────────────────────────────────────────────────────────
// Small helpers
// ─────────────────────────────────────────────────────────────────────────────

static void section(const std::string& title) {
    const int W = 70;
    std::cout << "\n" << std::string(W, '=') << "\n";
    int pad = (W - static_cast<int>(title.size()) - 2) / 2;
    std::cout << std::string(static_cast<std::size_t>(std::max(0, pad)), ' ') << "[ " << title << " ]\n" << std::string(W, '=') << "\n\n";
}

static void subsection(const std::string& title) { std::cout << "\n── " << title << " ──────────────────────────────────\n"; }

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1 — Basic set / get for all four types
// ─────────────────────────────────────────────────────────────────────────────

void demo_basic() {
    section("1 · Basic set<n> / get<n> for all types");

    ParameterRegistry params;

    // ── 1a. double ────────────────────────────────────────────────────────
    subsection("1a. double");

    params.set<"learning_rate">(0.001);
    params.set<"dropout">(0.5);

    std::cout << "learning_rate : " << params.get<"learning_rate", double>() << "\n";
    std::cout << "dropout       : " << params.get<"dropout", double>() << "\n";

    // ── 1b. int64_t ───────────────────────────────────────────────────────
    subsection("1b. int64_t");

    params.set<"batch_size">(32);
    params.set<"epochs">(100);
    params.set<"seed">(42LL);

    std::cout << "batch_size : " << params.get<"batch_size", int64_t>() << "\n";
    std::cout << "epochs     : " << params.get<"epochs", int64_t>() << "\n";
    std::cout << "seed       : " << params.get<"seed", int64_t>() << "\n";

    // ── 1c. bool ──────────────────────────────────────────────────────────
    subsection("1c. bool");

    params.set<"shuffle">(true);
    params.set<"verbose">(false);

    std::cout << "shuffle : " << std::boolalpha << params.get<"shuffle", bool>() << "\n";
    std::cout << "verbose : " << params.get<"verbose", bool>() << "\n";

    // ── 1d. std::string ───────────────────────────────────────────────────
    subsection("1d. std::string");

    params.set<"optimizer">("adam");
    params.set<"checkpoint_dir">("/data/checkpoints");
    params.set<"model_name">(std::string("transformer-base"));

    std::cout << "optimizer       : " << params.get<"optimizer", std::string>() << "\n";
    std::cout << "checkpoint_dir  : " << params.get<"checkpoint_dir", std::string>() << "\n";
    std::cout << "model_name      : " << params.get<"model_name", std::string>() << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2 — Auto-coercion rules
// ─────────────────────────────────────────────────────────────────────────────

void demo_coercion() {
    section("2 · Auto-coercion rules");

    ParameterRegistry params;

    // ── 2a. Integral types → int64_t ──────────────────────────────────────
    subsection("2a. Any integral type (short, int, long, …) → int64_t");

    params.set<"from_short">(static_cast<short>(7));
    params.set<"from_int">(42);
    params.set<"from_long">(1'000'000L);
    params.set<"from_longlong">(9'000'000'000LL);

    std::cout << "from_short    : " << params.get<"from_short",    int64_t>() << "\n";
    std::cout << "from_int      : " << params.get<"from_int",      int64_t>() << "\n";
    std::cout << "from_long     : " << params.get<"from_long",     int64_t>() << "\n";
    std::cout << "from_longlong : " << params.get<"from_longlong", int64_t>() << "\n";

    // ── 2b. Floating-point types → double ─────────────────────────────────
    subsection("2b. Any floating-point type (float, double, long double) → double");

    params.set<"from_float">(3.14f);
    params.set<"from_double">(2.718281828);
    params.set<"from_ldouble">(1.41421356L);

    std::cout << "from_float   : " << params.get<"from_float",   double>() << "\n";
    std::cout << "from_double  : " << params.get<"from_double",  double>() << "\n";
    std::cout << "from_ldouble : " << params.get<"from_ldouble", double>() << "\n";

    // ── 2c. bool is matched before integral ───────────────────────────────
    subsection("2c. bool literal → bool (not int64_t)");

    params.set<"flag_true">(true);
    params.set<"flag_false">(false);

    std::cout << std::boolalpha;
    std::cout << "flag_true  : " << params.get<"flag_true",  bool>() << "\n";
    std::cout << "flag_false : " << params.get<"flag_false", bool>() << "\n";

    // ── 2d. String-like types → std::string ───────────────────────────────
    subsection("2d. const char* / string_view / std::string → std::string");

    params.set<"from_cstr">("hello");
    params.set<"from_sv">(std::string_view("world"));
    params.set<"from_stdstr">(std::string("cpp20"));

    std::cout << "from_cstr   : " << params.get<"from_cstr",   std::string>() << "\n";
    std::cout << "from_sv     : " << params.get<"from_sv",     std::string>() << "\n";
    std::cout << "from_stdstr : " << params.get<"from_stdstr", std::string>() << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 3 — Explicit StoredAs override
// ─────────────────────────────────────────────────────────────────────────────

void demo_stored_as() {
    section("3 · Explicit StoredAs — resolving ambiguous literals");

    ParameterRegistry params;

    // ── 3a. Force int literal to double ───────────────────────────────────
    subsection("3a. set<name, double>(0)  — int literal forced to double");

    params.set<"threshold", double>(0);      // without StoredAs this would be int64_t
    params.set<"scale", double>(1);

    std::cout << "threshold : " << params.get<"threshold", double>() << " (double)\n";
    std::cout << "scale     : " << params.get<"scale",     double>() << " (double)\n";

    // ── 3b. Force double literal to int64_t (truncates) ───────────────────
    subsection("3b. set<name, int64_t>(3.9)  — double truncated to int64_t");

    params.set<"max_steps", int64_t>(1e6);   // 1e6 is a double literal
    params.set<"truncated", int64_t>(3.9);

    std::cout << "max_steps : " << params.get<"max_steps", int64_t>() << "\n";
    std::cout << "truncated : " << params.get<"truncated", int64_t>() << " (was 3.9)\n";

    // ── 3c. Force int to bool ─────────────────────────────────────────────
    subsection("3c. set<name, bool>(1)  — int literal forced to bool");

    params.set<"enabled", bool>(1);
    params.set<"disabled", bool>(0);

    std::cout << std::boolalpha;
    std::cout << "enabled  : " << params.get<"enabled",  bool>() << "\n";
    std::cout << "disabled : " << params.get<"disabled", bool>() << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 4 — has<n> and overwrite
// ─────────────────────────────────────────────────────────────────────────────

void demo_has_and_overwrite() {
    section("4 · has<n> and overwriting parameters");

    ParameterRegistry params;

    // ── 4a. has<n> before and after set ───────────────────────────────────
    subsection("4a. has<n>");

    std::cout << std::boolalpha;
    std::cout << "has 'lr' before set : " << params.has<"lr">() << "\n";
    params.set<"lr">(0.01);
    std::cout << "has 'lr' after set  : " << params.has<"lr">() << "\n";

    // ── 4b. Overwriting ───────────────────────────────────────────────────
    subsection("4b. Calling set<n> again overwrites the value");

    params.set<"lr">(0.01);
    std::cout << "lr initial  : " << params.get<"lr", double>() << "\n";
    params.set<"lr">(0.001);  // reduce on step decay
    std::cout << "lr after decay : " << params.get<"lr", double>() << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 5 — Error handling
// ─────────────────────────────────────────────────────────────────────────────

void demo_errors() {
    section("5 · Error handling");

    ParameterRegistry params;

    // ── 5a. get on an unset parameter ─────────────────────────────────────
    subsection("5a. get<n> on unset parameter → std::out_of_range");

    try {
        [[maybe_unused]] auto v = params.get<"missing", double>();
    } catch (const std::out_of_range& e) {
        std::cout << "Caught out_of_range: " << e.what() << "\n";
    }

    // ── 5b. get with wrong type → std::bad_variant_access ─────────────────
    subsection("5b. get<n, WrongType> → std::bad_variant_access");

    params.set<"count">(42);  // stored as int64_t

    try {
        [[maybe_unused]] auto v = params.get<"count", double>();  // wrong type
    } catch (const std::bad_variant_access& e) {
        std::cout << "Caught bad_variant_access: " << e.what() << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 6 — Reporting
// ─────────────────────────────────────────────────────────────────────────────

void demo_report() {
    section("6 · Reporting");

    ParameterRegistry params;
    params.set<"learning_rate">(0.001);
    params.set<"batch_size">(32);
    params.set<"epochs">(100);
    params.set<"shuffle">(true);
    params.set<"optimizer">("adam");
    params.set<"dropout">(0.3);
    params.set<"checkpoint_dir">("/data/ckpt");
    params.set<"verbose">(false);

    // ── 6a. get_report() — programmatic ───────────────────────────────────
    subsection("6a. get_report() — programmatic access");

    for (const auto& row : params.get_report()) {
        std::cout << row.name << " [" << row.type << "] = " << row.value_str << "\n";
    }

    // ── 6b. print_report() — formatted table ──────────────────────────────
    subsection("6b. print_report() — formatted table");
    params.print_report();
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 7 — Global PARAMS singleton
// ─────────────────────────────────────────────────────────────────────────────

void demo_global() {
    section("7 · Global PARAMS singleton");

    PARAMS.set<"app.name">("cpp_utils_demo");
    PARAMS.set<"app.version">(1);
    PARAMS.set<"app.debug">(false);

    std::cout << "app.name    : " << PARAMS.get<"app.name",    std::string>() << "\n";
    std::cout << "app.version : " << PARAMS.get<"app.version", int64_t>()     << "\n";
    std::cout << "app.debug   : " << std::boolalpha << PARAMS.get<"app.debug", bool>() << "\n";

    PARAMS.print_report();
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main() {
    std::cout << std::string(70, '-') << "\n"
              << "  ParameterRegistry Demo (ct_string API)\n"
              << std::string(70, '-') << "\n";

    demo_basic();
    demo_coercion();
    demo_stored_as();
    demo_has_and_overwrite();
    demo_errors();
    demo_report();
    demo_global();

    std::cout << "\n"
              << std::string(70, '-') << "\n"
              << "  Demo complete.\n"
              << std::string(70, '-') << "\n";
    return 0;
}
