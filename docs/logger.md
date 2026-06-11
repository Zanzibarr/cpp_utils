# logger

> All symbols below live in `namespace utilz`; examples assume `using namespace utilz;`.

**File:** [`logger/logger.hxx`](../logger/logger.hxx)
**Dependencies:** [ansi_colors](ansi_colors.md)
**Benchmarks:** [logger/BENCHMARKS.md](../logger/BENCHMARKS.md)

Thread-safe logger with runtime level filtering, synchronous and asynchronous output modes, ANSI color output, optional file logging, and both stream and string APIs.

## Log Levels

```
DEBUG(0) < INFO(1) < WARNING(2) < RAW(3) < SUCCESS(4) < ERROR(5) < FATAL(6)
```

Only messages at or above the configured minimum level are emitted. The default minimum is `INFO`.

## Construction

All options are set via `LoggerConfig`, which uses a fluent builder style. All fields have sensible defaults.

```cpp
// Defaults: sync, stdout, colors, thread-IDs, min_level=RAW
Logger lg;

// Async mode (background writer thread)
Logger lg(LoggerConfig{}.with_async());

// Log to file as well as stdout
Logger lg(LoggerConfig{}.with_file("app.log"));

// Full control
Logger lg(LoggerConfig{}
    .with_file("app.log")
    .with_async()
    .with_colors(false)
    .with_thread(false)
    .with_min_level(LoggerLevel::INFO));
```

An externally-captured `steady_clock::time_point` can be passed as a second argument so that elapsed timestamps are measured from before the Logger was constructed.

## Logging API

### Stream API — compose messages with `<<`

```cpp
lg.log()     << "plain message";
lg.debug()   << "x = " << x << ", y = " << y;
lg.info()    << "server started on port " << port;
lg.success() << "job done in " << elapsed << " ms";
lg.warning() << "retrying after " << delay << " ms";
lg.error()   << "failed to open " << path;
lg.fatal()   << "unrecoverable error — exits after flush";
```

Select a level dynamically via subscript:

```cpp
lg[LoggerLevel::INFO] << "dynamic level";
```

Stream directly at `RAW` level via `operator<<` on the logger itself:

```cpp
lg << "raw message";
```

### String API — pass a pre-formatted `string_view`

```cpp
lg.log("plain message");
lg.debug("checkpoint reached");
lg.info("server started");
lg.success("all tests passed");
lg.warning("low disk space");
lg.error("connection refused");
lg.fatal("unrecoverable error");   // flushes and calls _Exit
```

Both APIs have equivalent per-call cost. The stream path accumulates into a
thread-local buffer (no allocation per call) and moves it into the log record;
the string path copies from the `string_view`. See [Performance](#performance).

## Runtime Controls

```cpp
lg.set_min_level(LoggerLevel::WARNING);  // lock-free atomic update
lg.set_stdout(false);
lg.set_colors(false);
lg.set_thread(false);
lg.set_memory(true);                     // prefix each line with RSS in KB

lg.open_file("run.log");                 // add/switch file output
lg.close_file();

lg.flush();                              // in async mode, blocks until queue is drained
```

`set_config(LoggerConfig)` updates all options except `async` (constructor-only) in one call.

## Sync vs Async Mode

| | Sync | Async |
|--|------|-------|
| Write timing | On calling thread | Background thread |
| Mutex | Yes, per write | Only on queue push |
| Call cost | ~95–105 ns | Lower (queuing only) |
| Ordering | Strict | FIFO via queue |
| Use when | Simplicity, debugging | Low-latency hot paths |

In async mode, `flush()` blocks until the queue is drained.

## Filtering Cost

When a message is below the minimum level it is rejected before any allocation or formatting:

| API | Filtered cost |
|-----|--------------|
| String (`lg.info(msg)`) | ~3 ns |
| Stream (`lg.info() << msg`) | ~1–2 ns |

The stream variant is marginally cheaper when filtered because `log_stream` destructs immediately without touching the buffer.

## Output Format

```
[HH:MM:SS.mmm] [NNNNNNN KB] [T:xxxx] [LEVEL  ] message
               └─ memory ──┘ └─ tid ─┘
               (optional)    (optional)
```

Colors are applied per level on the console; file output is always plain text.

## Performance

See [BENCHMARKS.md](../logger/BENCHMARKS.md) for full results.

| Mode | Per-call cost |
|------|--------------|
| Sync / string | ~100 ns |
| Sync / stream | ~95 ns |
| Filtered / string | ~3 ns |
| Filtered / stream | ~1–2 ns |
