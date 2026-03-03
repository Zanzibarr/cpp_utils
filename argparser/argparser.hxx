#pragma once

/**
 * @file argparser.hxx
 * @brief CLI argument parser with fluent builder API and optional TOML config overlay.
 * @version 2.0.0
 *
 * @details
 * `ArgParser` parses `argv` into a `ParameterRegistry` (from `parameters.hxx`)
 * so that all argument values are accessible via compile-time names after
 * `parse()` completes.
 *
 * Arguments are registered with a fluent builder:
 * @code
 *   parser.add<"lr">().flag("--learning-rate").type<double>().default_val(0.01)
 *                     .help("Learning rate").required(false);
 * @endcode
 *
 * Key features:
 *   - Short (`-x`) and long (`--xx`) flag aliases, both optional.
 *   - Positional arguments (`positional(true)`).
 *   - Auto-generated `--help` / `-h` output with aligned columns.
 *   - TOML config file overlay: `--config path.toml` loads a TOML file
 *     (built-in minimal parser, no external dependencies) and merges its
 *     values before CLI flags, so CLI always wins.
 *   - `auto_params` struct generation via `ARGPARSER_STRUCT` macro for
 *     zero-overhead struct-style access.
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <concepts>
#include <cstdlib>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <variant>
#include <vector>

#include "../parameters/parameters.hxx"  // TODO: Update to the actual path

namespace cli {

// Supported value types
using Value = std::variant<int, double, bool, char, std::string>;

/** Exception thrown when argument parsing fails (unknown flag, type mismatch, etc.). */
struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ── Argument descriptor ────────────────────────────────────────────────────

/**
 * Descriptor for a single CLI argument.  Returned by `ArgParser::add<Name, T>()`
 * and configured via the fluent builder methods before `parse()` is called.
 */
struct Arg {
    std::string name;    // long name, e.g. "verbose"
    char shortName = 0;  // short name, e.g. 'v'  (0 = none)
    std::string help;    // description shown in --help
    bool required = false;

    std::type_index type{typeid(void)};

    std::optional<Value> defaultValue;
    std::optional<Value> minValue;  // inclusive, for int/char
    std::optional<Value> maxValue;  // inclusive, for int/char
    std::vector<Value> choices;     // allowed values (any type)

    std::function<void(Value)> setter;  // populates the ParameterRegistry on parse()

    // ── fluent builders ────────────────────────────────────────────────
    auto shorthand(char short_name) -> Arg& {
        if (short_name == 0) {
            throw ParseError("Cannot set 0 as short name");
        }
        shortName = short_name;
        return *this;
    }

    auto description(std::string_view description) -> Arg& {
        help = description;
        return *this;
    }

    auto require() -> Arg& {
        required = true;
        return *this;
    }

    template <typename T>
    auto default_val(T value) -> Arg& {
        defaultValue = normalize_and_store(value);
        return *this;
    }

    template <typename T>
    auto min(T value) -> Arg& {
        minValue = normalize_and_store(value);
        return *this;
    }

    template <typename T>
    auto max(T value) -> Arg& {
        maxValue = normalize_and_store(value);
        return *this;
    }

    template <typename T>
    auto allow(std::initializer_list<T> list) -> Arg& {
        for (auto val : list) {
            choices.emplace_back(normalize_and_store(val));
        }
        return *this;
    }

    template <typename T>
    auto normalize_and_store(T value) -> Value {
        if constexpr (std::is_same_v<T, bool>) {
            if (type != typeid(bool)) {
                throw ParseError("type mismatch: expected bool");
            }
            return Value{value};
        } else if constexpr (std::is_convertible_v<T, std::string>) {
            if (type != typeid(std::string)) {
                throw ParseError("type mismatch: expected string");
            }
            return Value{std::string{value}};
        } else if constexpr (std::is_floating_point_v<T>) {
            if (type != typeid(double)) {
                throw ParseError("type mismatch: expected double");
            }
            return Value{static_cast<double>(value)};
        } else if constexpr (std::is_integral_v<T>) {
            if (type == typeid(char)) {
                return Value{static_cast<char>(value)};
            }
            if (type == typeid(int)) {
                return Value{static_cast<int>(value)};
            }
            if (type == typeid(double)) {
                return Value{static_cast<double>(value)};  // ← coerce int literals to double
            }
            throw ParseError("type mismatch: expected char, int, or double");
        } else {
            throw ParseError("unsupported type");
        }
    }
};

// ── Parser ─────────────────────────────────────────────────────────────────

/**
 * CLI argument parser.  Register arguments with `add<Name, T>()`, then call
 * `parse(argc, argv)`.  Parsed values are accessible via `get<Name, T>()` or
 * through the underlying `ParameterRegistry` returned by `parameters()`.
 */
class ArgParser {
   public:
    /**
     * @param programName  Shown in the `--help` usage line.
     * @param description  Optional one-line description shown below the usage line.
     */
    explicit ArgParser(std::string programName, std::string description = "")
        : programName_(std::move(programName)), description_(std::move(description)) {}

    /**
     * Registers an argument and returns its descriptor for fluent configuration.
     *
     * @tparam Name  Compile-time argument name (also the key in `ParameterRegistry`).
     * @tparam T     CLI value type: `int`, `double`, `bool`, `char`,
     *               `std::string`.
     *               After `parse()`, values are stored with these coercions:
     *               `int`/`char` → `int64_t`, `double` → `double`,
     *               `bool` → `bool`, `string`/`path` → `std::string`.
     * @throws ParseError if a duplicate `Name` is registered.
     */
    template <CTString Name, typename T>
    auto add() -> Arg& {
        const std::string name_str{Name.view()};
        if (find_arg(name_str) != nullptr) {
            throw ParseError(std::format("duplicate argument registration: --{}", name_str));
        }

        args_.push_back(Arg{
            .name = name_str,
            .type = typeid(T),
            .setter =
                [this](Value val) {
                    if constexpr (std::is_same_v<T, int>) {
                        reg_.set<Name>(static_cast<int64_t>(std::get<int>(val)));
                    } else if constexpr (std::is_same_v<T, double>) {
                        reg_.set<Name>(std::get<double>(val));
                    } else if constexpr (std::is_same_v<T, bool>) {
                        reg_.set<Name>(std::get<bool>(val));
                    } else if constexpr (std::is_same_v<T, char>) {
                        reg_.set<Name>(std::string(1, std::get<char>(val)));
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        reg_.set<Name>(std::get<std::string>(val));
                    }
                },
        });

        return args_.back();
    }

    /**
     * Manually set a parameter value
     *
     * @throws ParseError if the parameter doesn't exist
     */
    template <CTString Name, typename T>
    auto set(T value) -> void {
        const std::string name_str{Name.view()};

        // Find the registered argument
        Arg* arg = find_arg(name_str);
        if (arg == nullptr) {
            throw ParseError(std::format("unknown argument: --{}", name_str));
        }

        // Reuse existing normalization + validation logic
        Value normalized = arg->normalize_and_store(value);
        validate(*arg, normalized);

        // Update both stores
        parsed_[name_str] = normalized;  // ← raw map
        arg->setter(normalized);         // ← ParameterRegistry
    }

    /**
     * Parses `argv` and populates the `ParameterRegistry`.
     *
     * Precedence (lowest → highest): defaults < TOML config < CLI flags.
     * `--config <file>` is consumed before the second CLI pass and never
     * forwarded to the normal argument matching logic.
     *
     * @throws ParseError on unknown flags, missing required args, type errors, etc.
     */

    void parse(int argc, char* argv[]) { parse_impl(argc, argv); }

    /**
     * Parses `argv` and populates the `ParameterRegistry`.
     *
     * Precedence (lowest → highest): defaults < TOML config < CLI flags.
     * `--config <file>` is consumed before the second CLI pass and never
     * forwarded to the normal argument matching logic.
     *
     * Add a callback (lambda, function, functor) to be called after parsing (for additional checks on the parsed parameters)
     *
     * @throws ParseError on unknown flags, missing required args, type errors, etc.
     */

    template <std::invocable<ArgParser&> Callback>
    void parse(int argc, char* argv[], Callback&& callback) {
        parse_impl(argc, argv);
        std::invoke(std::forward<Callback>(callback), *this);
    }

    /**
     * Parses `argv` and populates the `ParameterRegistry`.
     *
     * Precedence (lowest → highest): defaults < TOML config < CLI flags.
     * `--config <file>` is consumed before the second CLI pass and never
     * forwarded to the normal argument matching logic.
     *
     * Add a callback (std::function) to be called after parsing (for additional checks on the parsed parameters)
     *
     * @throws ParseError on unknown flags, missing required args, type errors, etc.
     */

    void parse(int argc, char* argv[], std::function<void(ArgParser&)> callback) {
        parse_impl(argc, argv);
        if (callback) {
            std::invoke(callback, *this);
        }
    }

    // ── accessors ──────────────────────────────────────────────────────

    /** Returns `true` if `Name` was set (from CLI, TOML config, or a default value). */
    template <CTString Name>
    [[nodiscard]] auto has() const -> bool {
        return reg_.has<Name>();
    }

    /**
     * Returns the parsed value for `Name` as type `T`.
     *
     * Reverse coercions applied automatically:
     *   - `int`      ← `int64_t` (narrowing cast)
     *   - `char`     ← first character of the stored `std::string`
     *   - `fs::path` ← stored `std::string`
     *   - others     ← direct `std::get<T>`
     *
     * @throws std::out_of_range       if `Name` was not parsed.
     * @throws std::bad_variant_access if `T` does not match the stored type.
     */
    template <CTString Name, typename T>
    [[nodiscard]] auto get() const -> T {
        if constexpr (std::is_same_v<T, int>) {
            return static_cast<int>(reg_.get<Name, int64_t>());
        } else if constexpr (std::is_same_v<T, char>) {
            const auto& str = reg_.get<Name, std::string>();
            return str.empty() ? '\0' : str[0];
        } else {
            return reg_.get<Name, T>();  // double, bool, std::string, int64_t
        }
    }

    /** Returns the underlying `ParameterRegistry` populated by `parse()`. */
    [[nodiscard]] auto parameters() const -> const ParameterRegistry& { return reg_; }

    // ── help ───────────────────────────────────────────────────────────

    /** Prints formatted usage and option descriptions to stdout. */
    void print_help() const {
        constexpr std::size_t HELP_COLUMN_WIDTH = 22;
        std::cout << "Usage: " << programName_ << " [options]\n";
        if (!description_.empty()) {
            std::cout << description_ << "\n";
        }
        std::cout << "\nOptions:\n";
        std::cout << "  -h, --help          Show this help message\n";
        std::cout << "  -C, --config <file> Load parameters from a TOML config file\n";

        for (const auto& arg : args_) {
            std::string left = "  --" + arg.name;
            if (arg.shortName != 0) {
                left += std::format(", -{}", arg.shortName);
            }
            left += std::format(" <{}>", type_to_string(arg.type));

            if (left.size() < HELP_COLUMN_WIDTH) {
                left.resize(HELP_COLUMN_WIDTH, ' ');
            } else {
                left += ' ';
            }
            std::cout << left << arg.help;

            if (arg.defaultValue) {
                std::cout << std::format(" [default: {}]", value_to_string(*arg.defaultValue));
            }
            if (arg.minValue && arg.maxValue) {
                std::cout << std::format(" [range: {}..{}]", value_to_string(*arg.minValue), value_to_string(*arg.maxValue));
            }
            if (!arg.choices.empty()) {
                std::cout << " [choices: ";
                for (std::size_t i = 0; i < arg.choices.size(); ++i) {
                    if (i != 0U) {
                        std::cout << '|';
                    }
                    std::cout << value_to_string(arg.choices[i]);
                }
                std::cout << ']';
            }
            if (arg.required) {
                std::cout << " (required)";
            }
            std::cout << '\n';
        }
    }

   private:
    // ── CLI parser ─────────────────────────────────────────────────────

    void parse_impl(int argc, char* argv[]) {
        // 1. Seed defaults
        for (auto& arg : args_) {
            if (arg.defaultValue) {
                parsed_[arg.name] = *arg.defaultValue;
            }
        }

        std::vector<std::string> tokens;
        for (int i = 1; i < argc; ++i) {
            tokens.emplace_back(argv[i]);
        }

        // 2. First pass: find --config and load TOML (values go into parsed_
        //    but will be overwritten by any CLI flag in the second pass).
        std::vector<std::string> remaining;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i] == "--config" || tokens[i] == "-C") {
                if (i + 1 >= tokens.size()) {
                    throw ParseError("--config requires a file path");
                }
                load_toml(tokens[++i]);
            } else {
                remaining.push_back(tokens[i]);
            }
        }

        // 3. Second pass: apply CLI flags (override TOML values)
        parse_cli_arguments(remaining);

        // 4. Check required
        for (auto& arg : args_) {
            if (arg.required && !parsed_.contains(arg.name)) {
                throw ParseError(std::format("required argument missing: --{}", arg.name));
            }
        }

        // 5. Populate the compile-time ParameterRegistry from all parsed values.
        for (const auto& arg : args_) {
            auto iter = parsed_.find(arg.name);
            if (iter != parsed_.end()) {
                arg.setter(iter->second);
            }
        }
    }

    // ── CLI argument processor ─────────────────────────────────────────

    void parse_cli_arguments(const std::vector<std::string>& remaining) {
        std::set<std::string> seenCli;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const auto& tok = remaining[i];

            if (tok == "--help" || tok == "-h") {
                print_help();
                std::exit(0);
            }

            std::string key;
            if (tok.starts_with("--")) {
                key = tok.substr(2);
            } else if (tok.starts_with("-") && tok.size() == 2) {
                key = expand_short(tok[1]);
            } else {
                throw ParseError(std::format("unexpected token: {}", tok));
            }

            Arg* arg = find_arg(key);
            if (arg == nullptr) {
                throw ParseError(std::format("unknown argument: --{}", key));
            }

            if (!seenCli.insert(key).second) {
                throw ParseError(std::format("duplicate CLI argument: --{}", key));
            }

            process_cli_value(*arg, key, remaining, i);
            validate(*arg, parsed_[key]);
        }
    }

    void process_cli_value(const Arg& arg, const std::string& key, const std::vector<std::string>& remaining, std::size_t& index) {
        // bool flags may be used without a value
        if (arg.type == typeid(bool)) {
            if (index + 1 < remaining.size() && !remaining[index + 1].starts_with("-")) {
                parsed_[key] = parse_bool(remaining[++index]);
            } else {
                parsed_[key] = true;
            }
        } else {
            if (index + 1 >= remaining.size()) {
                throw ParseError(std::format("--{} requires a value", key));
            }
            parsed_[key] = parse_value(arg, remaining[++index]);
        }
    }

    // ── TOML loader ────────────────────────────────────────────────────
    //
    // Minimal parser: handles flat key = value pairs and an optional
    // [args] (or any single-level) table header.  Lines that cannot be
    // matched to a registered argument are silently skipped.

    void load_toml(const std::string& path) {
        std::ifstream file(path);
        if (!file) {
            throw ParseError(std::format("cannot open config file: {}", path));
        }

        std::set<std::string> seenToml;
        std::string line;
        while (std::getline(file, line)) {
            // Strip inline comment and surrounding whitespace
            auto stripped = strip_comment(trim(line));
            if (stripped.empty() || stripped[0] == '#') {
                continue;
            }
            // Skip table headers like [args]
            if (stripped[0] == '[') {
                continue;
            }

            // Split on first '='
            auto equal = stripped.find('=');
            if (equal == std::string::npos) {
                throw ParseError(std::format("wrongly formatted line in config file: {}", stripped));
            }

            std::string key = trim(stripped.substr(0, equal));
            std::string rawVal = trim(stripped.substr(equal + 1));

            // Only process keys that match a registered argument
            Arg* arg = find_arg(key);
            if (arg == nullptr) {
                throw ParseError(std::format("unknown argument in config file: {}", key));
            }

            if (!seenToml.insert(key).second) {
                throw ParseError(std::format("duplicate key in config file: {}", key));
            }

            Value toml_value = parse_value(*arg, rawVal);
            validate(*arg, toml_value);
            parsed_[key] = toml_value;
        }
    }

    // ── string utilities ───────────────────────────────────────────────

    static auto trim(const std::string& str) -> std::string {
        auto begin = str.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            return {};
        }
        auto end = str.find_last_not_of(" \t\r\n");
        return str.substr(begin, end - begin + 1);
    }

    // Remove everything after the first '#' that is outside a quoted string.
    static auto strip_comment(const std::string& comment) -> std::string {
        bool inDouble = false;
        bool inSingle = false;
        for (std::size_t i = 0; i < comment.size(); ++i) {
            if (comment[i] == '"' && !inSingle) {
                inDouble = !inDouble;
            }
            if (comment[i] == '\'' && !inDouble) {
                inSingle = !inSingle;
            }
            if (comment[i] == '#' && !inDouble && !inSingle) {
                return trim(comment.substr(0, i));
            }
        }
        return comment;
    }

    // ── helpers ────────────────────────────────────────────────────────

    auto find_arg(const std::string& key) -> Arg* {
        for (auto& arg : args_) {
            if (arg.name == key) {
                return &arg;
            }
        }
        return nullptr;
    }

    auto expand_short(char short_name) -> std::string {
        for (auto& arg : args_) {
            if (arg.shortName == short_name) {
                return arg.name;
            }
        }
        throw ParseError(std::format("unknown short option: -{}", short_name));
    }

    static auto parse_bool(const std::string& str) -> bool {
        if (str == "true" || str == "1" || str == "yes") {
            return true;
        }
        if (str == "false" || str == "0" || str == "no") {
            return false;
        }
        throw ParseError(std::format("invalid bool value: {}", str));
    }

    template <typename T>
    static auto convert_to_value(const std::string& raw) -> Value {
        if constexpr (std::is_same_v<T, int>) {
            return Value{std::stoi(raw)};
        } else if constexpr (std::is_same_v<T, bool>) {
            return Value{parse_bool(raw)};
        } else if constexpr (std::is_same_v<T, char>) {
            if (raw.size() != 1) {
                throw ParseError("Expected single character");
            }
            return Value{raw[0]};
        } else if constexpr (std::is_same_v<T, std::string>) {
            return Value{raw};
        } else if constexpr (std::is_same_v<T, double>) {
            return Value{std::stod(raw)};
        }

        throw ParseError("Unsupported type");
    }

    static auto parse_value(const Arg& arg, const std::string& raw) -> Value {
        std::string clean = raw;
        if (clean.size() >= 2) {
            if ((clean.front() == '"' && clean.back() == '"') || (clean.front() == '\'' && clean.back() == '\'')) {
                clean = clean.substr(1, clean.size() - 2);
            }
        }

        try {
            if (arg.type == typeid(int)) {
                return convert_to_value<int>(clean);
            }
            if (arg.type == typeid(bool)) {
                return convert_to_value<bool>(clean);
            }
            if (arg.type == typeid(std::string)) {
                return convert_to_value<std::string>(clean);
            }
            if (arg.type == typeid(char)) {
                return convert_to_value<char>(clean);
            }
            if (arg.type == typeid(double)) {
                return convert_to_value<double>(clean);  // ← was missing
            }
        } catch (const std::exception& e) {
            throw ParseError(std::format("Invalid value for --{}: {}", arg.name, e.what()));
        }
        throw ParseError("Unknown type index");
    }

    static void validate(const Arg& arg, const Value& val) {
        if (!arg.choices.empty()) {
            if (std::ranges::find(arg.choices, val) == arg.choices.end()) {
                throw ParseError(std::format("--{}: value not in allowed choices", arg.name));
            }
        }
        if (std::holds_alternative<int>(val)) {
            int int_value = std::get<int>(val);
            if (arg.minValue && int_value < std::get<int>(*arg.minValue)) {
                throw ParseError(std::format("--{}: value {} below minimum {}", arg.name, int_value, std::get<int>(*arg.minValue)));
            }
            if (arg.maxValue && int_value > std::get<int>(*arg.maxValue)) {
                throw ParseError(std::format("--{}: value {} above maximum {}", arg.name, int_value, std::get<int>(*arg.maxValue)));
            }
        }
        if (std::holds_alternative<double>(val)) {
            double double_value = std::get<double>(val);
            if (arg.minValue && double_value < std::get<double>(*arg.minValue)) {
                throw ParseError(std::format("--{}: value {} below minimum {}", arg.name, double_value, std::get<double>(*arg.minValue)));
            }
            if (arg.maxValue && double_value > std::get<double>(*arg.maxValue)) {
                throw ParseError(std::format("--{}: value {} above maximum {}", arg.name, double_value, std::get<double>(*arg.maxValue)));
            }
        }
        if (std::holds_alternative<char>(val)) {
            char char_value = std::get<char>(val);
            if (arg.minValue && char_value < std::get<char>(*arg.minValue)) {
                throw ParseError(std::format("--{}: char '{}' below minimum", arg.name, char_value));
            }
            if (arg.maxValue && char_value > std::get<char>(*arg.maxValue)) {
                throw ParseError(std::format("--{}: char '{}' above maximum", arg.name, char_value));
            }
        }
    }

    static auto value_to_string(const Value& value) -> std::string {
        return std::visit(
            [](auto&& val) -> std::string {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, bool>) {
                    return val ? "true" : "false";
                } else if constexpr (std::is_same_v<T, char>) {
                    return std::string(1, val);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return val;
                } else if constexpr (std::is_same_v<T, double>) {
                    std::ostringstream oss;
                    oss << val;
                    return oss.str();
                } else {
                    return std::to_string(val);
                }
            },
            value);
    }

    static auto type_to_string(std::type_index type) -> std::string {
        if (type == typeid(int)) {
            return "int";
        }
        if (type == typeid(bool)) {
            return "bool";
        }
        if (type == typeid(char)) {
            return "char";
        }
        if (type == typeid(std::string)) {
            return "string";
        }
        if (type == typeid(double)) {
            return "double";
        }
        return "unknown";
    }

    std::string programName_;
    std::string description_;
    std::vector<Arg> args_;
    std::map<std::string, Value> parsed_;
    ParameterRegistry reg_;
};

}  // namespace cli

/**
 * Convenience wrapper: calls `parser.parse()` and returns `true` on success.
 * On `ParseError`, prints the error and the help message, then returns `false`.
 */
inline auto argparser_parse(cli::ArgParser& parser, int argc, char* argv[]) -> bool {
    try {
        parser.parse(argc, argv);
        return true;
    } catch (const cli::ParseError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        parser.print_help();
        return false;
    }
}

/**
 * Convenience wrapper: calls `parser.parse()` and returns `true` on success.
 * On `ParseError`, prints the error and the help message, then returns `false`.
 *
 * Use a callback function (lambda, function, functor) to be called after the parsing for additional checks on the parsed parameters
 * This is supposed to be a void function, any return value will be discarded
 */
template <std::invocable<cli::ArgParser&> Callback>
inline auto argparser_parse(cli::ArgParser& parser, int argc, char* argv[], Callback&& callback) -> bool {
    try {
        parser.parse(argc, argv, std::forward<Callback>(callback));
        return true;
    } catch (const cli::ParseError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        parser.print_help();
        return false;
    }
}

/**
 * Convenience wrapper: calls `parser.parse()` and returns `true` on success.
 * On `ParseError`, prints the error and the help message, then returns `false`.
 *
 * Use a callback function (std::function) to be called after the parsing for additional checks on the parsed parameters
 */
inline auto argparser_parse(cli::ArgParser& parser, int argc, char* argv[], std::function<void(cli::ArgParser&)> callback) -> bool {
    try {
        parser.parse(argc, argv, std::move(callback));
        return true;
    } catch (const cli::ParseError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        parser.print_help();
        return false;
    }
}