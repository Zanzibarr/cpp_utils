#pragma once

/**
 * @file logger.hxx
 * @brief Logger classes and macros
 * @version 1.0.0
 *
 * @author Matteo Zanella <matteozanella2@gmail.com>
 * Copyright 2026 Matteo Zanella
 *
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

// ── Internal clock ────────────────────────────────────────────────────────────

namespace logger_detail {
/// Returns seconds elapsed since the first call (program-relative wall time).
inline auto elapsed_seconds() noexcept -> double {
    using clock = std::chrono::steady_clock;
    using dseconds = std::chrono::duration<double>;
    static const auto start = clock::now();
    return std::chrono::duration_cast<dseconds>(clock::now() - start).count();
}
}  // namespace logger_detail

// ── ANSI color constants ──────────────────────────────────────────────────────

struct Colors {
    static constexpr const char *reset = "\033[0m";
    static constexpr const char *red = "\033[31m";
    static constexpr const char *green = "\033[32m";
    static constexpr const char *yellow = "\033[33m";
    static constexpr const char *blue = "\033[34m";
    static constexpr const char *magenta = "\033[35m";
    static constexpr const char *cyan = "\033[36m";
    static constexpr const char *white = "\033[37m";
    static constexpr const char *bright_red = "\033[91m";
    static constexpr const char *bright_green = "\033[92m";
    static constexpr const char *bright_yellow = "\033[93m";
    static constexpr const char *bright_blue = "\033[94m";
    static constexpr const char *bright_magenta = "\033[95m";
    static constexpr const char *bright_cyan = "\033[96m";
    static constexpr const char *bright_white = "\033[97m";
};

// ── Logger ───────────────────────────────────────────────────────────────────

/**
 * @brief Singleton, thread-safe Logger.
 *
 * Supports:
 *  - Synchronous (default) and asynchronous (background-thread) modes.
 *  - Runtime-configurable minimum log level (filter noisy levels in production).
 *  - Simultaneous stdout/stderr + optional file output.
 *  - ANSI color codes and optional thread-ID stamping.
 *  - Stream-style log_stream objects (RAII flush on destruction).
 */
class Logger {
   public:
    // ── Log level ─────────────────────────────────────────────────────────────

    /**
     * @brief Severity levels, ordered from least to most severe.
     *
     * The numeric ordering is intentional: a minimum_level filter simply
     * checks  `incoming_level >= minimum_level`.
     */
    enum class level : int { BASIC = 0, DEBUG = 1, INFO = 2, SUCCESS = 3, WARNING = 4, ERROR = 5 };

    // ── log_stream ────────────────────────────────────────────────────────────

    /**
     * @brief RAII stream wrapper — accumulates tokens via `operator<<` and
     *        flushes the full message to the Logger on destruction.
     *
     * Typical usage:
     * @code
     *   LOG_INFO << "Value = " << x;   // macro returns a temporary log_stream
     * @endcode
     */
    class log_stream {
       public:
        log_stream(Logger &logger_obj, level lvl, bool exit_on_error = false) : lg_(logger_obj), level_(lvl), exit_on_error_(exit_on_error) {}

        log_stream(log_stream &&logstr) noexcept
            : lg_(logstr.lg_), level_(logstr.level_), buf_(std::move(logstr.buf_)), exit_on_error_(logstr.exit_on_error_) {
            logstr.moved_ = true;
        }

        log_stream(const log_stream &) = delete;
        auto operator=(const log_stream &) -> log_stream & = delete;
        auto operator=(log_stream &&) -> log_stream & = delete;

        template <typename T>
        auto operator<<(const T &val) -> log_stream & {
            buf_ << val;
            return *this;
        }

        ~log_stream() {
            if (moved_) {
                return;
            }
            std::string msg = buf_.str();
            if (msg.empty()) {
                return;
            }
            lg_.emit(msg, level_);
            if (exit_on_error_ && level_ == level::ERROR) {
                _Exit(EXIT_FAILURE);
            }
        }

       private:
        Logger &lg_;
        level level_;
        std::ostringstream buf_;
        bool exit_on_error_ = false;
        bool moved_ = false;
    };

    // ── Singleton access ──────────────────────────────────────────────────────

    static auto get_instance() -> Logger & {
        static Logger instance;
        return instance;
    }

    Logger(const Logger &) = delete;
    auto operator=(const Logger &) -> Logger & = delete;

    // ── Initialization ────────────────────────────────────────────────────────

    /**
     * @brief Initialize the Logger. Must be called once before any logging.
     *
     * @param write_to_file  Write plain-text log lines to a file.
     * @param file_path      Path of the log file (ignored when write_to_file is false).
     * @param use_colors     Emit ANSI escape codes on console output.
     * @param show_thread    Prefix each line with a short thread ID.
     * @param async_mode     Dispatch writes to a background worker thread so
     *                       that the calling thread is never blocked on I/O.
     * @param min_level      Discard messages below this severity.
     *
     * @throws std::runtime_error if called more than once, or if the file
     *         cannot be opened.
     */
    void initialize(bool write_to_file = false, std::string file_path = "", bool use_colors = true, bool show_thread = true, bool async_mode = false,
                    level min_level = level::BASIC) {
        std::lock_guard lock(mutex_);
        if (initialized_) {
            throw std::runtime_error("Logger already initialized!");
        }

        use_colors_ = use_colors;
        show_thread_ = show_thread;
        min_level_.store(min_level, std::memory_order_relaxed);
        async_mode_ = async_mode;

        if (write_to_file && !file_path.empty()) {
            file_.open(file_path, std::ios::app);
            if (!file_.is_open()) {
                throw std::runtime_error("Failed to open log file: " + file_path);
            }
        }

        if (async_mode_) {
            start_worker();
        }

        initialized_ = true;
    }

    // ── Runtime controls ─────────────────────────────────────────────────────

    /// Change whether ANSI colors are emitted (thread-safe).
    void set_colors(bool flag) {
        std::lock_guard lock(mutex_);
        use_colors_ = flag;
    }
    /// Toggle thread-ID stamping at runtime (thread-safe).
    void set_thread(bool flag) {
        std::lock_guard lock(mutex_);
        show_thread_ = flag;
    }
    /// Raise or lower the minimum level filter at runtime (thread-safe, lock-free).
    void set_min_level(level lvl) { min_level_.store(lvl, std::memory_order_relaxed); }

    /// Flush both the log file and console streams immediately.
    void flush() {
        std::lock_guard lock(mutex_);
        std::cout.flush();
        std::cerr.flush();
        if (file_.is_open()) {
            file_.flush();
        }
    }

    // ── String overloads ─────────────────────────────────────────────────────

    void log(const std::string &msg) { emit(msg, level::BASIC); }
    void debug(const std::string &msg) { emit(msg, level::DEBUG); }
    void info(const std::string &msg) { emit(msg, level::INFO); }
    void success(const std::string &msg) { emit(msg, level::SUCCESS); }
    void warning(const std::string &msg) { emit(msg, level::WARNING); }

    [[noreturn]]
    void error(const std::string &msg) {
        emit(msg, level::ERROR);
        _Exit(EXIT_FAILURE);
    }

    // ── Stream-style factory methods ─────────────────────────────────────────

    log_stream log() { return {*this, level::BASIC}; }
    log_stream debug() { return {*this, level::DEBUG}; }
    log_stream info() { return {*this, level::INFO}; }
    log_stream success() { return {*this, level::SUCCESS}; }
    log_stream warning() { return {*this, level::WARNING}; }
    log_stream error() { return {*this, level::ERROR, true}; }

    // ── Destructor ───────────────────────────────────────────────────────────

    ~Logger() {
        if (async_mode_) {
            stop_worker();
        }
        std::lock_guard lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }
    }

    friend class log_stream;

   private:
    // ── Internal record type (async queue entries) ────────────────────────────

    struct record {
        std::string message;
        level lvl;
        double elapsed;         // captured at emit() call time
        std::string thread_id;  // captured at emit() call time
    };

    // ── Construction ─────────────────────────────────────────────────────────

    Logger() = default;

    // ── Level metadata helpers ────────────────────────────────────────────────

    struct level_meta {
        const char *label;  // fixed-width, 7 chars
        const char *color;
        bool use_err;  // route to stderr?
    };

    static auto meta_of(level lvl) noexcept -> level_meta {
        switch (lvl) {
            case level::BASIC:
                return {.label = "       ", .color = Colors::white, .use_err = false};
            case level::DEBUG:
                return {.label = " DEBUG ", .color = Colors::blue, .use_err = false};
            case level::INFO:
                return {.label = "  INFO ", .color = Colors::bright_blue, .use_err = false};
            case level::SUCCESS:
                return {.label = "SUCCESS", .color = Colors::bright_green, .use_err = false};
            case level::WARNING:
                return {.label = "WARNING", .color = Colors::bright_yellow, .use_err = true};
            case level::ERROR:
                return {.label = " ERROR ", .color = Colors::bright_red, .use_err = true};
        }
        return {.label = "       ", .color = Colors::white, .use_err = false};
    }

    // ── Time formatting ───────────────────────────────────────────────────────

    static auto format_time(double elapsed) -> std::string {
        constexpr int MS_PER_SECOND = 1000;
        constexpr int MS_PER_MINUTE = 60000;
        constexpr int MS_PER_HOUR = 3600000;
        constexpr int TIME_BUFFER_SIZE = 32;

        int total_ms = static_cast<int>(elapsed * MS_PER_SECOND);
        int hours = total_ms / MS_PER_HOUR;
        int minutes = (total_ms % MS_PER_HOUR) / MS_PER_MINUTE;
        int seconds = (total_ms % MS_PER_MINUTE) / MS_PER_SECOND;
        int millis = total_ms % MS_PER_SECOND;

        char buf[TIME_BUFFER_SIZE];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", hours, minutes, seconds, millis);
        return buf;
    }

    // ── Thread ID formatting ──────────────────────────────────────────────────

    static auto current_thread_id() -> std::string {
        std::ostringstream strstream;
        strstream << std::this_thread::get_id();
        std::string str = strstream.str();
        // Keep only the last 4 characters for brevity
        if (str.size() > 4) {
            str = str.substr(str.size() - 4);
        }
        return str;
    }

    // ── Core write (called with mutex held) ───────────────────────────────────

    void write_record(const record &rec) {
        const auto [label, color, use_err] = meta_of(rec.lvl);
        std::ostream &ostr = use_err ? std::cerr : std::cout;

        // Plain-text prefix (used for both file and no-color console)
        std::string time_tag = "[" + format_time(rec.elapsed) + "] ";
        std::string thread_tag = show_thread_ ? "[T:" + rec.thread_id + "] " : "";
        std::string level_tag = rec.lvl == level::BASIC ? "" : std::string("[") + label + "] ";

        if (file_.is_open()) {
            // File output — never colored
            file_ << time_tag << thread_tag << level_tag << rec.message << '\n';
            file_.flush();
        } else if (use_colors_) {
            ostr << Colors::cyan << time_tag << Colors::reset << Colors::magenta << thread_tag << Colors::reset << color << level_tag << Colors::reset
                 << rec.message << '\n';
        } else {
            ostr << time_tag << thread_tag << level_tag << rec.message << '\n';
        }
    }

    // ── Emit (public entry point, acquires lock in sync mode) ─────────────────

    void emit(const std::string &message, level lvl) {
        // Fast path: skip below-threshold messages without locking
        if (lvl < min_level_.load(std::memory_order_relaxed)) {
            return;
        }

        record rec{.message = message, .lvl = lvl, .elapsed = logger_detail::elapsed_seconds(), .thread_id = current_thread_id()};

        if (async_mode_) {
            {
                std::lock_guard lock(queue_mutex_);
                queue_.push(std::move(rec));
            }
            queue_cv_.notify_one();
        } else {
            std::lock_guard lock(mutex_);
            if (!initialized_) {
                throw std::runtime_error("Logger not initialized!");
            }
            write_record(rec);
        }
    }

    // ── Async worker ─────────────────────────────────────────────────────────

    void start_worker() {
        worker_running_ = true;
        worker_ = std::thread([this] {
            while (true) {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { return !queue_.empty() || !worker_running_; });

                // Drain everything currently in the queue
                while (!queue_.empty()) {
                    record rec = std::move(queue_.front());
                    queue_.pop();
                    lock.unlock();

                    {
                        std::lock_guard writelock(mutex_);
                        write_record(rec);
                    }

                    lock.lock();
                }

                if (!worker_running_ && queue_.empty()) {
                    break;
                }
            }
        });
    }

    void stop_worker() {
        {
            std::lock_guard lock(queue_mutex_);
            worker_running_ = false;
        }
        queue_cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    // ── Data members ─────────────────────────────────────────────────────────

    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool use_colors_ = true;
    bool show_thread_ = true;
    bool async_mode_ = false;
    std::atomic<level> min_level_{level::BASIC};

    std::ofstream file_;

    // Async support
    std::thread worker_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<record> queue_;
    std::atomic<bool> worker_running_{false};
};

// ── Convenience macros ────────────────────────────────────────────────────────

/// Initialize with default settings (stdout, colors, sync).
inline void log_init() { Logger::get_instance().initialize(); }

/// Initialize with file output.
inline void log_init_file(const std::string &path) { Logger::get_instance().initialize(true, path); }

/// Initialize in async (non-blocking) mode.
inline void log_init_async() { Logger::get_instance().initialize(false, "", true, true, true); }

// Stream-style logging macros
#define LOG Logger::get_instance().log()
#define LOG_DEBUG Logger::get_instance().debug()
#define LOG_INFO Logger::get_instance().info()
#define LOG_SUCCESS Logger::get_instance().success()
#define LOG_WARN Logger::get_instance().warning()
#define LOG_WARNING Logger::get_instance().warning()
#define LOG_ERROR Logger::get_instance().error()

// Direct string logging functions (avoid constructing a log_stream)
template <typename T>
inline void LOG_S(const T &msg) {
    Logger::get_instance().log(std::string(msg));
}
template <typename T>
inline void LOG_DEBUG_S(const T &msg) {
    Logger::get_instance().debug(std::string(msg));
}
template <typename T>
inline void LOG_INFO_S(const T &msg) {
    Logger::get_instance().info(std::string(msg));
}
template <typename T>
inline void LOG_SUCCESS_S(const T &msg) {
    Logger::get_instance().success(std::string(msg));
}
template <typename T>
inline void LOG_WARN_S(const T &msg) {
    Logger::get_instance().warning(std::string(msg));
}
template <typename T>
inline void LOG_ERROR_S(const T &msg) {
    Logger::get_instance().error(std::string(msg));
}

/// Stamp the current source location then continue the stream.
#define LOG_HERE LOG_DEBUG << __FILE__ ":" << __LINE__ << " | "

/// Mark unimplemented code — terminates via ERROR.
#define LOG_TODO LOG_ERROR << __func__ << "() @ " << __FILE__ << ":" << __LINE__ << " — unimplemented"

/// Mark unimplemented code — warns but continues.
#define LOG_TODO_WARN LOG_WARN << __func__ << "() @ " << __FILE__ << ":" << __LINE__ << " — unimplemented"
