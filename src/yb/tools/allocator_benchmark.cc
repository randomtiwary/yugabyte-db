// Copyright (c) YugabyteDB, Inc.
//
// This file previously hosted an in-process malloc micro-benchmark. Allocator
// comparisons for YugabyteDB should measure real YSQL INSERT traffic against a
// live cluster built with TCMalloc (default) vs jemalloc (--use-jemalloc).
//
// Use:
//   ./scripts/benchmark_allocators.sh --host 127.0.0.1 --port 5433 --label <build>
//   ./scripts/benchmark_allocators.sh --compare \
//       --tcmalloc-bindir <build-with-tcmalloc>/latest \
//       --jemalloc-bindir <build-with-jemalloc>/latest
//
// Implementation: scripts/ysql_insert_allocator_bench.py (invoked by the shell wrapper).
//
// This translation unit is intentionally a tiny stub so existing CMake wiring keeps
// working; it prints guidance and exits non-zero if executed.

#include <iostream>

int main() {
  std::cerr
      << "allocator_benchmark: in-process malloc micro-benchmark removed.\n"
      << "Run real YSQL INSERT allocator benchmarks via:\n"
      << "  ./scripts/benchmark_allocators.sh --help\n"
      << "  ./scripts/ysql_insert_allocator_bench.py --help\n";
  return 2;
}
