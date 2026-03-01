#pragma once

#include "benchmark.hxx"

int main() { return benchmark::bench_registry::instance().run_all(); }