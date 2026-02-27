#pragma once

// Include this header in exactly ONE .cpp file (your test runner entry point).
// It defines main() and hands control to the test registry.
//
// Example:
//   // runner.cpp
//   #include "testing/test_main.hpp"
//   #include "interval_test.cpp"
//   #include "timer_test.cpp"
//   // ... other test files

#include "test_framework.hpp"

auto main() -> int { return ::testing::test_registry::instance().run_all(); }