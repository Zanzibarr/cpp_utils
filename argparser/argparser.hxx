#pragma once

/**
 * @file argparser.hxx
 * @brief Simple CLI argument parser class with optional TOML config support
 * @version 1.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 *
 * TOML config support
 * -------------------
 * Pass --config <path/to/file.toml> on the CLI to load parameters from a TOML file
 *
 * Precedence (lowest → highest):
 *   1. Defaults registered with .default_val()
 *   2. Values from the TOML config file
 *   3. Values from the CLI
 *
 * TOML format expected
 * --------------------
 * Only a flat key = value section (or an optional [args] table) is supported.
 * Inline comments after a value are allowed.  Example:
 *
 *   [args]             # optional table header – parsed if present, ignored otherwise
 *   verbose = true
 *   threads = 8
 *   output  = "/tmp/out"
 *   mode    = 'w'      # single-quoted char
 *
 * Supported value literals
 *   int    : any decimal integer, optionally signed
 *   bool   : true / false
 *   char   : single character wrapped in single quotes, e.g. 'x'
 *   string : double-quoted string, e.g. "hello world"
 *   path   : double-quoted string for an arg whose type is fs::path
 *
 * CLI always wins: a CLI-supplied value overwrites whatever the TOML file set.
 */

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <variant>
#include <vector>

namespace cli {

namespace fs = std::filesystem;

// Supported value types
using Value = std::variant<int, bool, char, std::string, fs::path>;

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ── Argument descriptor ────────────────────────────────────────────────────

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

    // ── fluent builders ────────────────────────────────────────────────
    auto shorthand(char short_name) -> Arg& {
        if (short_name == '0') {
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
        for (auto v : list) {
            choices.emplace_back(normalize_and_store(v));
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
        } else if constexpr (std::is_same_v<T, fs::path>) {
            if (type != typeid(fs::path)) {
                throw ParseError("type mismatch: expected path");
            }
            return Value{value};
        } else if constexpr (std::is_convertible_v<T, std::string>) {
            if (type != typeid(std::string)) {
                throw ParseError("type mismatch: expected string");
            }
            return Value{std::string{value}};
        } else if constexpr (std::is_integral_v<T>) {
            if (type == typeid(char)) {
                return Value{static_cast<char>(value)};
            }
            if (type == typeid(int)) {
                return Value{static_cast<int>(value)};
            }
            throw ParseError("type mismatch: expected char or int");
        } else {
            throw ParseError("unsupported type");
        }
    }
};

// ── Parser ─────────────────────────────────────────────────────────────────

class ArgParser {
   public:
    explicit ArgParser(std::string programName, std::string description = "")
        : programName_(std::move(programName)), description_(std::move(description)) {}

    // Register a new argument and return a reference for chaining
    template <typename T>
    auto add(std::string name) -> Arg& {
        if (find_arg(name) != nullptr) {
            throw ParseError(std::format("duplicate argument registration: --{}", name));
        }

        args_.push_back(Arg{.name = std::move(name), .type = typeid(T)});

        return args_.back();
    }

    // ── parse ──────────────────────────────────────────────────────────
    //
    // Precedence: defaults < TOML config < CLI flags.
    // --config <file> is consumed before the second CLI pass so it is never
    // forwarded to the normal argument matching logic.

    void parse(int argc, char* argv[]) {
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
    }

    // ── accessors ──────────────────────────────────────────────────────

    [[nodiscard]] auto has(const std::string& name) const -> bool { return parsed_.contains(name); }

    template <typename T>
    auto get(const std::string& name) const -> T {
        // 1. Find parsed value
        auto value_it = parsed_.find(name);
        if (value_it == parsed_.end()) {
            throw ParseError(std::format("argument not found: {}", name));
        }

        // 2. Find argument descriptor
        const Arg* arg = nullptr;
        for (const auto& a : args_) {
            if (a.name == name) {
                arg = &a;
                break;
            }
        }

        if (!arg) {
            throw ParseError(std::format("internal error: argument '{}' not registered", name));
        }

        // 3. Type check
        if (arg->type != typeid(T)) {
            throw ParseError(std::format("type mismatch for --{} (expected {}, requested by get() {})", name, type_to_string(arg->type),
                                         type_to_string(typeid(T))));
        }

        // 4. Safe extraction
        return std::get<T>(value_it->second);
    }

    // ── help ───────────────────────────────────────────────────────────

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

    // ── existing helpers (unchanged) ───────────────────────────────────

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
            return Value{parse_bool(raw)};  // Use your existing parse_bool
        } else if constexpr (std::is_same_v<T, char>) {
            if (raw.size() != 1) {
                throw ParseError("Expected single character");
            }
            return Value{raw[0]};
        } else if constexpr (std::is_same_v<T, std::string>) {
            return Value{raw};
        } else if constexpr (std::is_same_v<T, fs::path>) {
            return Value{fs::path{raw}};
        }
        throw ParseError("Unsupported type");
    }

    static auto parse_value(const Arg& arg, const std::string& raw) -> Value {
        // 1. Pre-process: Strip TOML-style quotes if they exist
        std::string clean = raw;
        if (clean.size() >= 2) {
            if ((clean.front() == '"' && clean.back() == '"') || (clean.front() == '\'' && clean.back() == '\'')) {
                clean = clean.substr(1, clean.size() - 2);
            }
        }

        // 2. Convert using the template dispatcher
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
            if (arg.type == typeid(fs::path)) {
                return convert_to_value<fs::path>(clean);
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
                } else if constexpr (std::is_same_v<T, fs::path>) {
                    return val.string();
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
        if (type == typeid(fs::path)) {
            return "path";
        }
        return "unknown";
    }

    std::string programName_;
    std::string description_;
    std::vector<Arg> args_;
    std::map<std::string, Value> parsed_;
};

}  // namespace cli

template <typename Parser>
inline auto argparser_parse(Parser& parser, int argc, char* argv[]) -> bool {
    try {
        parser.parse(argc, argv);
        return true;
    } catch (const cli::ParseError& e) {
        std::cerr << "Error: " << e.what() << "\n";
        parser.print_help();
        return false;
    }
}
