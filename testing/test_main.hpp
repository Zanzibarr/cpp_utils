#pragma once

#include "test_framework.hxx"

auto main() -> int { return ::testing::test_registry::instance().run_all(); }