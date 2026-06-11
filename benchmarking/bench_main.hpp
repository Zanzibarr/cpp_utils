#pragma once

#include "benchmark.hxx"

int main() { return ::utilz::benchmark::bench_registry::instance().run_all(); }