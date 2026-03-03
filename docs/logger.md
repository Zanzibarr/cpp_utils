# logger

**File:** [`logger/logger.hxx`](../logger/logger.hxx)
**Dependencies:** [ansi_colors](ansi_colors.md)
**Benchmarks:** [logger/BENCHMARKS.md](../logger/BENCHMARKS.md)

Thread-safe logger with runtime level filtering, synchronous and asynchronous output modes, ANSI color output, optional file logging, and both stream and string APIs.

## Log Levels

`Logger::level::BASIC < DEBUG < INFO < SUCCESS < WARNING < ERROR`

Only messages at or above the configured minimum level are emitted.

## Initialization

```cpp
// Sync, stdout only, colors auto-detected, thread-IDs shown
log_init();

// Async mode (background writer thread)
log_init_async();

// Log to file as well
log_init_file("app.log");

// Full control
default_logger().initialize(
    /*write_to_file*/ true,
    /*file_path*/     "app.log",
    /*use_colors*/    true,
    /*show_thread*/   false,
    /*async_mode*/    true,
    /*min_level*/     Logger::level::INFO
);
```

## Logging — Macros

### Stream API (use `<<` to compose messages)

```cpp
LOG        << "plain message";
LOG_DEBUG  << "x = " << x << ", y = " << y;
LOG_INFO   << "server started on port " << port;
LOG_SUCCESS << "job done in " << elapsed << " ms";
LOG_WARN   << "retrying after " << delay << " ms";
LOG_ERROR  << "failed to open " << path;
```

### String API (no stream construction — slightly faster)

```cpp
LOG_S(msg)
LOG_DEBUG_S(msg)
LOG_INFO_S(msg)
LOG_SUCCESS_S(msg)
LOG_WARN_S(msg)
LOG_ERROR_S(msg)
```

### Utility macros

```cpp
LOG_HERE            // stamps current source file + line
LOG_TODO(msg)       // LOG_WARN + marks unimplemented code
LOG_TODO_WARN(msg)  // same at WARNING level
```

## Direct API

```cpp
Logger& logger = default_logger();

logger.info("server started");
logger.error("disk full");
logger.log(Logger::level::SUCCESS, "OK");

// Runtime controls
logger.set_min_level(Logger::level::WARNING);
logger.set_colors(false);
logger.set_thread(true);

// Flush async queue (no-op in sync mode)
logger.flush();
```

## Sync vs Async Mode

| | Sync | Async |
|--|------|-------|
| Write timing | On calling thread | Background thread |
| Mutex | Yes, per write | Only on queue push |
| Call cost | ~188–326 ns | Lower (queuing only) |
| Ordering | Strict | FIFO via queue |
| Use when | Simplicity, debugging | Low-latency hot paths |

In async mode, `flush()` blocks until the queue is drained.

## Filtering Cost

When a message is below the minimum level, it is rejected before any allocation or formatting:

| API | Filtered cost |
|-----|--------------|
| String (`LOG_INFO_S`) | ~25 ns |
| Stream (`LOG_INFO`) | ~3–4 ns |

The stream variant is cheaper when filtered because `log_stream` destructs immediately without building a string.

## Output Format

```
[HH:MM:SS.mmm] [LEVEL] [thread-id] message
```

Colors are applied per level when stdout is a TTY and colors are enabled.

## Performance

See [BENCHMARKS.md](../logger/BENCHMARKS.md) for full results.

| Mode | Per-call cost |
|------|--------------|
| Sync / string | ~188 ns |
| Sync / stream | ~326 ns |
| Filtered / string | ~25 ns |
| Filtered / stream | ~3–4 ns |
