/**
 * @file test.cpp
 * @brief Unit tests for ArgParser.
 *
 * Build (C++20):
 *   g++ -std=c++20 -O2 test.cpp -o test && ./test
 *
 * NOTE: `expect(parser.get<"name", T>())` cannot be used directly because the
 * preprocessor splits on the comma inside `<>`. Results are captured in local
 * variables first.
 */

#include <fstream>

#include "../testing/test_main.hpp"
#include "argparser.hxx"

// ─────────────────────────────────────────────────────────────────────────────
// Helper — builds argc / argv from an initializer list of string literals.
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Basic parsing
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("basic parsing")

TEST_CASE("parse int argument from CLI") {
    cli::ArgParser parser("prog");
    parser.add<"ap_epochs", int>().description("Num epochs");

    Args args{"prog", "--ap_epochs", "10"};
    parser.parse(args.argc(), args.argv());
    int val = parser.get<"ap_epochs", int>();
    expect(val).to_equal(10);
}

TEST_CASE("parse double argument from CLI") {
    cli::ArgParser parser("prog");
    parser.add<"ap_lr", double>().description("Learning rate");

    Args args{"prog", "--ap_lr", "0.01"};
    parser.parse(args.argc(), args.argv());
    double val = parser.get<"ap_lr", double>();
    expect(val).to_approx_equal(0.01);
}

TEST_CASE("parse bool argument without explicit value (flag style)") {
    cli::ArgParser parser("prog");
    parser.add<"ap_verbose", bool>().description("Verbose output");

    Args args{"prog", "--ap_verbose"};
    parser.parse(args.argc(), args.argv());
    bool val = parser.get<"ap_verbose", bool>();
    expect(val).to_be_true();
}

TEST_CASE("parse bool argument with explicit value true") {
    cli::ArgParser parser("prog");
    parser.add<"ap_flag", bool>();

    Args args{"prog", "--ap_flag", "true"};
    parser.parse(args.argc(), args.argv());
    bool val = parser.get<"ap_flag", bool>();
    expect(val).to_be_true();
}

TEST_CASE("parse bool argument with explicit value false") {
    cli::ArgParser parser("prog");
    parser.add<"ap_bflag", bool>();

    Args args{"prog", "--ap_bflag", "false"};
    parser.parse(args.argc(), args.argv());
    bool val = parser.get<"ap_bflag", bool>();
    expect(val).to_be_false();
}

TEST_CASE("parse string argument from CLI") {
    cli::ArgParser parser("prog");
    parser.add<"ap_model", std::string>().description("Model name");

    Args args{"prog", "--ap_model", "resnet50"};
    parser.parse(args.argc(), args.argv());
    std::string val = parser.get<"ap_model", std::string>();
    expect(val).to_equal(std::string{"resnet50"});
}

TEST_CASE("parse multiple arguments of different types") {
    cli::ArgParser parser("prog");
    parser.add<"ap_mix_n", int>();
    parser.add<"ap_mix_lr", double>();
    parser.add<"ap_mix_s", std::string>();
    parser.add<"ap_mix_v", bool>();

    Args args{"prog", "--ap_mix_n", "5", "--ap_mix_lr", "0.001", "--ap_mix_s", "sgd", "--ap_mix_v", "true"};
    parser.parse(args.argc(), args.argv());

    int mix_n = parser.get<"ap_mix_n", int>();
    double mix_lr = parser.get<"ap_mix_lr", double>();
    std::string mix_s = parser.get<"ap_mix_s", std::string>();
    bool mix_v = parser.get<"ap_mix_v", bool>();

    expect(mix_n).to_equal(5);
    expect(mix_lr).to_approx_equal(0.001);
    expect(mix_s).to_equal(std::string{"sgd"});
    expect(mix_v).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// Short flags
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("short flags")

TEST_CASE("short flag resolves to the correct argument") {
    cli::ArgParser parser("prog");
    parser.add<"ap_sh_epochs", int>().shorthand('e');

    Args args{"prog", "-e", "20"};
    parser.parse(args.argc(), args.argv());
    int val = parser.get<"ap_sh_epochs", int>();
    expect(val).to_equal(20);
}

TEST_CASE("unknown short flag throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_sh_n", int>();

    Args args{"prog", "-z", "1"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Default values
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("default values")

TEST_CASE("default int is used when flag is absent") {
    cli::ArgParser parser("prog");
    parser.add<"ap_def_n", int>().default_val(42);

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());
    int val = parser.get<"ap_def_n", int>();
    expect(val).to_equal(42);
}

TEST_CASE("default double is used when flag is absent") {
    cli::ArgParser parser("prog");
    parser.add<"ap_def_lr", double>().default_val(0.01);

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());
    double val = parser.get<"ap_def_lr", double>();
    expect(val).to_approx_equal(0.01);
}

TEST_CASE("default bool is used when flag is absent") {
    cli::ArgParser parser("prog");
    parser.add<"ap_def_b", bool>().default_val(true);

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());
    bool val = parser.get<"ap_def_b", bool>();
    expect(val).to_be_true();
}

TEST_CASE("default string is used when flag is absent") {
    cli::ArgParser parser("prog");
    parser.add<"ap_def_s", std::string>().default_val(std::string{"adam"});

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());
    std::string val = parser.get<"ap_def_s", std::string>();
    expect(val).to_equal(std::string{"adam"});
}

TEST_CASE("CLI value overrides default") {
    cli::ArgParser parser("prog");
    parser.add<"ap_ov_n", int>().default_val(1);

    Args args{"prog", "--ap_ov_n", "99"};
    parser.parse(args.argc(), args.argv());
    int val = parser.get<"ap_ov_n", int>();
    expect(val).to_equal(99);
}

// ─────────────────────────────────────────────────────────────────────────────
// Required arguments
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("required arguments")

TEST_CASE("missing required argument throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_req_n", int>().require();

    Args args{"prog"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("providing a required argument does not throw") {
    cli::ArgParser parser("prog");
    parser.add<"ap_req_ok", int>().require();

    Args args{"prog", "--ap_req_ok", "5"};
    expect_no_throw(parser.parse(args.argc(), args.argv()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Error conditions
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("error conditions")

TEST_CASE("unknown long flag throws ParseError") {
    cli::ArgParser parser("prog");

    Args args{"prog", "--ap_unknown", "1"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("duplicate CLI argument throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_dup", int>();

    Args args{"prog", "--ap_dup", "1", "--ap_dup", "2"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("duplicate argument registration throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_dreg", int>();
    expect_throws(cli::ParseError, parser.add<"ap_dreg", int>());
}

TEST_CASE("flag without value throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_noval", int>();

    Args args{"prog", "--ap_noval"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("invalid int value throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_badint", int>();

    Args args{"prog", "--ap_badint", "notanumber"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("invalid double value throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_baddbl", double>();

    Args args{"prog", "--ap_baddbl", "notanumber"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation — min / max / choices
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("validation — min / max / choices")

TEST_CASE("value below min throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_min", int>().min(5);

    Args args{"prog", "--ap_min", "3"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("value at min is accepted") {
    cli::ArgParser parser("prog");
    parser.add<"ap_minat", int>().min(5);

    Args args{"prog", "--ap_minat", "5"};
    expect_no_throw(parser.parse(args.argc(), args.argv()));
}

TEST_CASE("value above max throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_max", int>().max(10);

    Args args{"prog", "--ap_max", "11"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("value at max is accepted") {
    cli::ArgParser parser("prog");
    parser.add<"ap_maxat", int>().max(10);

    Args args{"prog", "--ap_maxat", "10"};
    expect_no_throw(parser.parse(args.argc(), args.argv()));
}

TEST_CASE("value not in choices throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_ch", std::string>().allow<std::string>({"adam", "sgd"});

    Args args{"prog", "--ap_ch", "rmsprop"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("value in choices is accepted") {
    cli::ArgParser parser("prog");
    parser.add<"ap_chok", std::string>().allow<std::string>({"adam", "sgd"});

    Args args{"prog", "--ap_chok", "sgd"};
    expect_no_throw(parser.parse(args.argc(), args.argv()));
}

// ─────────────────────────────────────────────────────────────────────────────
// has() and get()
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("has() and get()")

TEST_CASE("has() returns false when argument was not provided and has no default") {
    cli::ArgParser parser("prog");
    parser.add<"ap_has_no", int>();

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());
    expect(parser.has<"ap_has_no">()).to_be_false();
}

TEST_CASE("has() returns true after parsing a provided argument") {
    cli::ArgParser parser("prog");
    parser.add<"ap_has_yes", int>();

    Args args{"prog", "--ap_has_yes", "1"};
    parser.parse(args.argc(), args.argv());
    expect(parser.has<"ap_has_yes">()).to_be_true();
}

TEST_CASE("has() returns true when a default value was used") {
    cli::ArgParser parser("prog");
    parser.add<"ap_has_def", int>().default_val(0);

    Args args{"prog"};
    parser.parse(args.argc(), args.argv());
    expect(parser.has<"ap_has_def">()).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// set() — programmatic value injection
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("set() — programmatic injection")

TEST_CASE("set() succeeds for a registered argument") {
    cli::ArgParser parser("prog");
    parser.add<"ap_set_n", int>().default_val(0);
    expect_no_throw(parser.set<"ap_set_n">(77));
}

TEST_CASE("set() throws ParseError for unknown argument name") {
    cli::ArgParser parser("prog");
    expect_throws(cli::ParseError, parser.set<"ap_set_unk">(1));
}

// ─────────────────────────────────────────────────────────────────────────────
// argparser_parse() wrapper
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("argparser_parse() wrapper")

TEST_CASE("argparser_parse returns true on success") {
    cli::ArgParser parser("prog");
    parser.add<"ap_wrap_n", int>().default_val(0);

    Args args{"prog"};
    bool result = argparser_parse(parser, args.argc(), args.argv());
    expect(result).to_be_true();
}

TEST_CASE("argparser_parse returns false on ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_wrap_req", int>().require();

    // suppress error output to stderr during the test
    std::streambuf* old_cerr = std::cerr.rdbuf();
    std::ostringstream sink;
    std::cerr.rdbuf(sink.rdbuf());

    Args args{"prog"};  // missing required arg
    bool result = argparser_parse(parser, args.argc(), args.argv());

    std::cerr.rdbuf(old_cerr);
    expect(result).to_be_false();
}

TEST_CASE("argparser_parse with lambda callback invokes callback on success") {
    cli::ArgParser parser("prog");
    parser.add<"ap_cb_n", int>().default_val(5);

    bool called = false;
    Args args{"prog"};
    argparser_parse(parser, args.argc(), args.argv(), [&called](cli::ArgParser&) { called = true; });
    expect(called).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// TOML config loading
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("TOML config loading")

TEST_CASE("values loaded from a TOML file are used when no CLI override") {
    const std::string path = "/tmp/argparser_test.toml";
    {
        std::ofstream f(path);
        f << "ap_toml_n = 99\n";
        f << "ap_toml_s = \"hello\"\n";
    }

    cli::ArgParser parser("prog");
    parser.add<"ap_toml_n", int>().default_val(0);
    parser.add<"ap_toml_s", std::string>().default_val(std::string{""});

    Args args{"prog", "--config", path.c_str()};
    parser.parse(args.argc(), args.argv());

    int toml_n = parser.get<"ap_toml_n", int>();
    std::string toml_s = parser.get<"ap_toml_s", std::string>();

    expect(toml_n).to_equal(99);
    expect(toml_s).to_equal(std::string{"hello"});
}

TEST_CASE("CLI value overrides TOML config") {
    const std::string path = "/tmp/argparser_test2.toml";
    {
        std::ofstream f(path);
        f << "ap_toml_ov = 1\n";
    }

    cli::ArgParser parser("prog");
    parser.add<"ap_toml_ov", int>().default_val(0);

    Args args{"prog", "--config", path.c_str(), "--ap_toml_ov", "42"};
    parser.parse(args.argc(), args.argv());

    int ov = parser.get<"ap_toml_ov", int>();
    expect(ov).to_equal(42);
}

TEST_CASE("nonexistent config file throws ParseError") {
    cli::ArgParser parser("prog");
    parser.add<"ap_toml_ne", int>().default_val(0);

    Args args{"prog", "--config", "/tmp/this_file_does_not_exist_xyz.toml"};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("unknown key in TOML file throws ParseError") {
    const std::string path = "/tmp/argparser_test3.toml";
    {
        std::ofstream f(path);
        f << "ap_toml_badkey = 1\n";
    }

    cli::ArgParser parser("prog");  // ap_toml_badkey is not registered

    Args args{"prog", "--config", path.c_str()};
    expect_throws(cli::ParseError, parser.parse(args.argc(), args.argv()));
}

TEST_CASE("bool value loaded from TOML file") {
    const std::string path = "/tmp/argparser_test_bool.toml";
    {
        std::ofstream f(path);
        f << "ap_toml_bool = true\n";
    }
    cli::ArgParser parser("prog");
    parser.add<"ap_toml_bool", bool>().default_val(false);
    Args args{"prog", "--config", path.c_str()};
    parser.parse(args.argc(), args.argv());
    bool val = parser.get<"ap_toml_bool", bool>();
    expect(val).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// parse() overload with std::function callback
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("parse() with std::function callback")

TEST_CASE("parse(argc, argv, std::function) invokes callback on success") {
    cli::ArgParser parser("prog");
    parser.add<"ap_scb_n", int>().default_val(0);

    bool called = false;
    std::function<void(cli::ArgParser&)> cb = [&called](cli::ArgParser&) { called = true; };
    Args args{"prog"};
    parser.parse(args.argc(), args.argv(), std::move(cb));
    expect(called).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// argparser_parse() std::function overload
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("argparser_parse() std::function overload")

TEST_CASE("argparser_parse with std::function callback invokes it on success") {
    cli::ArgParser parser("prog");
    parser.add<"ap_sfn_n", int>().default_val(0);

    bool called = false;
    std::function<void(cli::ArgParser&)> cb = [&called](cli::ArgParser&) { called = true; };
    Args args{"prog"};
    bool result = argparser_parse(parser, args.argc(), args.argv(), cb);
    expect(result).to_be_true();
    expect(called).to_be_true();
}

// ─────────────────────────────────────────────────────────────────────────────
// parameters() accessor
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("parameters() accessor")

TEST_CASE("parameters() returns the underlying ParameterRegistry") {
    cli::ArgParser parser("prog");
    parser.add<"ap_preg_n", int>().default_val(5);
    Args args{"prog"};
    parser.parse(args.argc(), args.argv());
    const auto& reg = parser.parameters();
    int val = reg.get<"ap_preg_n", int>();
    expect(val).to_equal(int{5});
}

// ─────────────────────────────────────────────────────────────────────────────
// value_to_string (exercised via print_help)
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("print_help exercises value_to_string")

TEST_CASE("print_help does not throw with int default and range") {
    cli::ArgParser parser("prog");
    parser.add<"ap_ph_n", int>().default_val(10).min(0).max(100);
    std::streambuf* old = std::cout.rdbuf();
    std::ostringstream sink;
    std::cout.rdbuf(sink.rdbuf());
    expect_no_throw(parser.print_help());
    std::cout.rdbuf(old);
}

TEST_CASE("print_help does not throw with string choices") {
    cli::ArgParser parser("prog");
    parser.add<"ap_ph_s", std::string>().default_val(std::string{"adam"}).allow<std::string>({"adam", "sgd"});
    std::streambuf* old = std::cout.rdbuf();
    std::ostringstream sink;
    std::cout.rdbuf(sink.rdbuf());
    expect_no_throw(parser.print_help());
    std::cout.rdbuf(old);
}