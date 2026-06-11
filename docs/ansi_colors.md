# ansi_colors

> All symbols below live in `namespace utilz`; examples assume `using namespace utilz;`.

**File:** [`utilities/ansi_colors.hxx`](../utilities/ansi_colors.hxx)
**Dependencies:** none

Shared ANSI escape-code constants and TTY-aware color wrappers. Used internally by [logger](logger.md), [benchmarking](benchmarking.md), and [testing](testing.md).

## Key API

### TTY check

```cpp
bool ansi::enabled()   // true when stdout is a real TTY (result is cached)
```

### Raw codes

```cpp
ansi::codes::reset
ansi::codes::red
ansi::codes::green
ansi::codes::yellow
ansi::codes::cyan
ansi::codes::blue
ansi::codes::magenta
ansi::codes::white
ansi::codes::bold
ansi::codes::dim
// ...and bright variants
```

All codes are `constexpr const char*` ANSI escape sequences.

### TTY-aware wrappers

```cpp
std::string ansi::green(std::string_view s)
std::string ansi::red(std::string_view s)
std::string ansi::yellow(std::string_view s)
std::string ansi::cyan(std::string_view s)
std::string ansi::bold(std::string_view s)
std::string ansi::dim(std::string_view s)
```

Each wrapper returns the string wrapped in the appropriate escape codes when `enabled()` is true, or returns the string unchanged otherwise.

## Usage

```cpp
#include "ansi_colors.hxx"

// Check TTY
if (ansi::enabled()) {
    std::cout << ansi::green("OK") << "\n";
}

// Always safe — no-op when not a TTY
std::cout << ansi::red("Error: ") << message << "\n";

// Use raw codes directly
std::cout << ansi::codes::bold << "Important" << ansi::codes::reset << "\n";
```

## Performance

`enabled()` calls `isatty()` once and caches the result — subsequent calls are a simple boolean load. The color wrapper functions perform one branch and one string allocation when colors are on, and return the input string unchanged when off.

Use raw `ansi::codes::*` constants in tight loops to avoid any allocation.
