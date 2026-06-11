#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../logger/logger.hxx"
#include "../testing/test_main.hpp"

using namespace utilz;

namespace {

// Reads the whole file into a string.
auto slurp(const std::string& path) -> std::string {
    std::ifstream in(path);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// Unique-ish log path per test to avoid cross-test interference.
auto log_path(const char* name) -> std::string { return std::string("_logger_test_") + name + ".log"; }

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// File sink
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("file sink")

TEST_CASE("messages are written to the log file") {
    const auto path = log_path("basic");
    {
        Logger lg{LoggerConfig{}.with_stdout(false).with_file(path)};
        lg.info("hello file sink");
        lg.error("something failed");
    }  // destructor flushes and closes
    const auto content = slurp(path);
    expect(content).to_contain("hello file sink");
    expect(content).to_contain("something failed");
    std::remove(path.c_str());
}

TEST_CASE("stream API formats values into a single record") {
    const auto path = log_path("stream");
    {
        Logger lg{LoggerConfig{}.with_stdout(false).with_file(path)};
        lg.info() << "answer=" << 42 << " pi=" << 3.5;
    }
    const auto content = slurp(path);
    expect(content).to_contain("answer=42 pi=3.5");
    std::remove(path.c_str());
}

TEST_CASE("file output contains no ANSI escape codes") {
    const auto path = log_path("plain");
    {
        Logger lg{LoggerConfig{}.with_stdout(false).with_file(path).with_colors(true)};
        lg.warning("plain text expected");
    }
    const auto content = slurp(path);
    expect(content).to_contain("plain text expected");
    expect(content.find('\033') == std::string::npos).to_be_true();
    std::remove(path.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Level filtering
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("level filtering")

TEST_CASE("messages below min_level are dropped") {
    const auto path = log_path("minlevel");
    {
        Logger lg{LoggerConfig{}.with_stdout(false).with_file(path).with_min_level(LoggerLevel::ERROR)};
        lg.debug("drop debug");
        lg.info("drop info");
        lg.warning("drop warning");
        lg.error("keep error");
    }
    const auto content = slurp(path);
    expect(content).to_contain("keep error");
    expect(content.find("drop debug") == std::string::npos).to_be_true();
    expect(content.find("drop info") == std::string::npos).to_be_true();
    expect(content.find("drop warning") == std::string::npos).to_be_true();
    std::remove(path.c_str());
}

TEST_CASE("set_min_level takes effect immediately") {
    const auto path = log_path("setlevel");
    {
        Logger lg{LoggerConfig{}.with_stdout(false).with_file(path)};
        lg.info("before threshold");
        lg.set_min_level(LoggerLevel::ERROR);
        lg.info("after threshold");
    }
    const auto content = slurp(path);
    expect(content).to_contain("before threshold");
    expect(content.find("after threshold") == std::string::npos).to_be_true();
    std::remove(path.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Async mode
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("async mode")

TEST_CASE("async logger delivers all records on flush") {
    const auto path = log_path("async");
    {
        Logger lg{LoggerConfig{}.with_stdout(false).with_file(path).with_async(true)};
        for (int i = 0; i < 100; ++i) {
            lg.info() << "async record " << i;
        }
        lg.flush();
        const auto content = slurp(path);
        expect(content).to_contain("async record 0");
        expect(content).to_contain("async record 99");
    }
    std::remove(path.c_str());
}

TEST_CASE("async logger drains the queue on destruction") {
    const auto path = log_path("asyncdtor");
    {
        Logger lg{LoggerConfig{}.with_stdout(false).with_file(path).with_async(true)};
        lg.success("last words");
    }  // no explicit flush — destructor must drain
    expect(slurp(path)).to_contain("last words");
    std::remove(path.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// File management
// ─────────────────────────────────────────────────────────────────────────────

TEST_SUITE("file management")

TEST_CASE("open_file / close_file switch the sink at runtime") {
    const auto path_a = log_path("switch_a");
    const auto path_b = log_path("switch_b");
    {
        Logger lg{LoggerConfig{}.with_stdout(false).with_file(path_a)};
        lg.info("goes to A");
        lg.open_file(path_b);
        lg.info("goes to B");
        lg.close_file();
        lg.info("goes nowhere");
    }
    expect(slurp(path_a)).to_contain("goes to A");
    const auto content_b = slurp(path_b);
    expect(content_b).to_contain("goes to B");
    expect(content_b.find("goes nowhere") == std::string::npos).to_be_true();
    std::remove(path_a.c_str());
    std::remove(path_b.c_str());
}
