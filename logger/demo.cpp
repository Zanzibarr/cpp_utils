/**
 * @file demo.cpp
 * @brief Demonstrates logger.hxx in a realistic multi-threaded scenario.
 *
 * Simulates a small HTTP server with:
 *  - A main thread that starts up, adjusts log level at runtime, then shuts down.
 *  - A pool of worker threads that each handle a batch of "requests".
 *  - A background metrics thread that periodically reports stats.
 *  - A deliberately triggered fatal error path.
 *
 * Compile (C++17):
 *   g++ -std=c++17 -pthread -o demo demo.cpp
 *
 * Run:
 *   ./demo               # sync mode, stdout
 *   ./demo file          # sync mode, writes to server.log
 *   ./demo async         # async (non-blocking) mode
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "logger.hxx"

// ── Shared state ──────────────────────────────────────────────────────────────

std::atomic<int> requests_handled{0};
std::atomic<int> requests_failed{0};

// ── Fake workload helpers ─────────────────────────────────────────────────────

/// Simulates variable-latency I/O (1–50 ms).
static void fake_io(int min_ms = 1, int max_ms = 50) {
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(min_ms, max_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
}

/// Returns a fake HTTP status code, weighted towards 200.
static int fake_status() {
    thread_local std::mt19937 rng{std::random_device{}()};
    std::discrete_distribution<int> dist({70, 15, 10, 5});  // 200 301 404 500
    static const int codes[] = {200, 301, 404, 500};
    return codes[dist(rng)];
}

// ── Worker thread ─────────────────────────────────────────────────────────────

void worker(int worker_id, int num_requests) {
    LOG_DEBUG << "Worker-" << worker_id << " starting, will handle " << num_requests << " requests";

    for (int i = 0; i < num_requests; ++i) {
        const std::string req_id = "W" + std::to_string(worker_id) + "-R" + std::to_string(i);

        LOG_DEBUG << "[" << req_id << "] received GET /api/data";
        fake_io(5, 30);

        int status = fake_status();
        ++requests_handled;

        if (status == 200) {
            LOG_INFO << "[" << req_id << "] → 200 OK";
        } else if (status == 301) {
            LOG_INFO << "[" << req_id << "] → 301 Moved Permanently";
        } else if (status == 404) {
            LOG_WARN << "[" << req_id << "] → 404 Not Found";
            ++requests_failed;
        } else {
            LOG_WARN << "[" << req_id << "] → 500 Internal Server Error";
            ++requests_failed;
        }
    }

    LOG_SUCCESS << "Worker-" << worker_id << " finished (" << num_requests << " requests processed)";
}

// ── Metrics thread ────────────────────────────────────────────────────────────

void metrics_reporter(std::atomic<bool> &running) {
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        int handled = requests_handled.load();
        int failed = requests_failed.load();
        LOG_INFO << "[metrics] handled=" << handled << "  failed=" << failed << "  ok=" << (handled - failed);
    }
}

// ── Startup / shutdown ────────────────────────────────────────────────────────

void simulate_startup() {
    LOG_INFO << "Loading configuration...";
    fake_io(10, 20);

    LOG_INFO << "Connecting to database...";
    fake_io(20, 40);

    LOG_SUCCESS << "Server ready on port 8080";
}

void simulate_config_reload() {
    LOG_WARN << "SIGHUP received — reloading config";
    fake_io(5, 15);

    // Example: raise the minimum level at runtime (suppresses DEBUG in production)
    default_logger().set_min_level(Logger::level::INFO);
    LOG_INFO << "Log level raised to INFO (DEBUG suppressed from here)";
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    // ── 1. Initialize ─────────────────────────────────────────────────────────

    bool use_file = argc > 1 && std::strcmp(argv[1], "file") == 0;
    bool use_async = argc > 1 && std::strcmp(argv[1], "async") == 0;

    if (use_file) {
        default_logger().initialize(true, "server.log", false, true, false);
        // (colors disabled for file output — the file already contains no escape codes)
    } else if (use_async) {
        default_logger().initialize(false, "", true, true, true);
    } else {
        log_init_async();
    }

    // ── 2. Startup ────────────────────────────────────────────────────────────

    simulate_startup();

    // ── 3. Spawn metrics reporter ─────────────────────────────────────────────

    std::atomic<bool> metrics_running{true};
    std::thread metrics_thread(metrics_reporter, std::ref(metrics_running));

    // ── 4. Spawn worker pool ──────────────────────────────────────────────────

    constexpr int NUM_WORKERS = 4;
    constexpr int REQS_PER_WORKER = 6;

    LOG_INFO << "Spawning " << NUM_WORKERS << " workers";

    std::vector<std::thread> workers;
    workers.reserve(NUM_WORKERS);
    for (int i = 0; i < NUM_WORKERS; ++i) workers.emplace_back(worker, i, REQS_PER_WORKER);

    // ── 5. Mid-run: simulate a config reload that changes the log level ───────

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    simulate_config_reload();

    // ── 6. Wait for workers ───────────────────────────────────────────────────

    for (auto &w : workers) w.join();

    // ── 7. Stop metrics ───────────────────────────────────────────────────────

    metrics_running.store(false);
    metrics_thread.join();

    // ── 8. Final summary ──────────────────────────────────────────────────────

    int total = requests_handled.load();
    int failed = requests_failed.load();
    int success = total - failed;

    default_logger().set_min_level(Logger::level::BASIC);  // restore for summary

    LOG_SUCCESS << "All workers done.  total=" << total << "  ok=" << success << "  failed=" << failed;

    LOG << "Exiting";

    default_logger().flush();
    return 0;
}