#pragma once

#include "test_framework.hxx"

auto main() -> int { return ::utilz::testing::test_registry::instance().run_all(); }