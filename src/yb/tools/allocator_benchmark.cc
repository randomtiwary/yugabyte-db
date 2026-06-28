// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
// Standalone allocator micro-benchmark for comparing TCMalloc vs jemalloc under a workload that
// approximates 100k "inserts" (allocate row-sized buffers, write payload, retain, then free).
//
// Prefer the repo helper (builds and compares both when libs are installed):
//   ./scripts/benchmark_allocators.sh
//   ./scripts/benchmark_allocators.sh --num-inserts=100000 --threads=4
//
// Manual standalone build examples:
//   g++ -O2 -std=c++17 -DUSE_TCMALLOC=1 allocator_benchmark.cc -o bench_tcmalloc \
//       -ltcmalloc -lpthread -ldl
//   g++ -O2 -std=c++17 -DUSE_JEMALLOC=1 allocator_benchmark.cc -o bench_jemalloc \
//       -ljemalloc -lpthread -ldl
//
// Prints a RESULT line (machine-readable) and exits 0 on success.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Opt-in compile flags from the build script / YB CMake (treat undefined as 0).
#ifndef USE_TCMALLOC
#define USE_TCMALLOC 0
#endif
#ifndef USE_JEMALLOC
#define USE_JEMALLOC 0
#endif
#ifndef YB_TCMALLOC_ENABLED
#define YB_TCMALLOC_ENABLED 0
#endif
#ifndef YB_JEMALLOC_ENABLED
#define YB_JEMALLOC_ENABLED 0
#endif
#ifndef YB_GOOGLE_TCMALLOC
#define YB_GOOGLE_TCMALLOC 0
#endif
// Optional override for the printed allocator label (useful when linking a .so without headers).
// Pass -DALLOCATOR_DISPLAY_NAME=jemalloc (no quotes); we stringize it below.
#ifndef ALLOCATOR_DISPLAY_NAME
#define ALLOCATOR_DISPLAY_NAME unknown
#endif
#define YB_ALLOC_BENCH_STRINGIFY_HELPER(x) #x
#define YB_ALLOC_BENCH_STRINGIFY(x) YB_ALLOC_BENCH_STRINGIFY_HELPER(x)

#if USE_JEMALLOC || YB_JEMALLOC_ENABLED
#if defined(__has_include)
#if __has_include(<jemalloc/jemalloc.h>)
#include <jemalloc/jemalloc.h>
#define HAVE_JEMALLOC_HEADER 1
#endif
#endif
#ifndef HAVE_JEMALLOC_HEADER
// Linked with jemalloc but headers unavailable: still valid for timing, no mallctl stats.
#define HAVE_JEMALLOC_HEADER 0
#endif
#else
#define HAVE_JEMALLOC_HEADER 0
#endif

#if (USE_TCMALLOC || YB_TCMALLOC_ENABLED) && !YB_GOOGLE_TCMALLOC
#if defined(__has_include)
#if __has_include(<gperftools/malloc_extension.h>)
#include <gperftools/malloc_extension.h>
#define HAVE_GPERFTOOLS_TCMALLOC 1
#endif
#endif
#endif

#if (USE_TCMALLOC || YB_TCMALLOC_ENABLED) && YB_GOOGLE_TCMALLOC
#include <tcmalloc/malloc_extension.h>
#define HAVE_GOOGLE_TCMALLOC 1
#endif

namespace {

struct Options {
  int64_t num_inserts = 100000;
  size_t key_size = 16;
  size_t value_size = 256;
  int threads = 1;
  int warmup_inserts = 1000;
  bool retain_all = true;
};

void PrintUsage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " [--num-inserts=N] [--value-size=B] [--key-size=B] [--threads=T] "
         "[--warmup=N] [--no-retain]\n"
      << "  Simulates N insert-like allocations (default 100000).\n"
      << "  Each insert allocates key+value buffers, writes deterministic bytes, and stores them.\n"
      << "  With --no-retain, each insert is freed immediately (allocator churn).\n";
}

bool ParseArgs(int argc, char** argv, Options* opt) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto eq = arg.find('=');
    std::string key = eq == std::string::npos ? arg : arg.substr(0, eq);
    std::string val = eq == std::string::npos ? "" : arg.substr(eq + 1);
    if (key == "-h" || key == "--help") {
      PrintUsage(argv[0]);
      return false;
    }
    if (key == "--num-inserts" && !val.empty()) {
      opt->num_inserts = std::strtoll(val.c_str(), nullptr, 10);
    } else if (key == "--value-size" && !val.empty()) {
      opt->value_size = static_cast<size_t>(std::strtoull(val.c_str(), nullptr, 10));
    } else if (key == "--key-size" && !val.empty()) {
      opt->key_size = static_cast<size_t>(std::strtoull(val.c_str(), nullptr, 10));
    } else if (key == "--threads" && !val.empty()) {
      opt->threads = static_cast<int>(std::strtol(val.c_str(), nullptr, 10));
    } else if (key == "--warmup" && !val.empty()) {
      opt->warmup_inserts = static_cast<int>(std::strtol(val.c_str(), nullptr, 10));
    } else if (key == "--no-retain") {
      opt->retain_all = false;
    } else {
      std::cerr << "Unknown or incomplete argument: " << arg << "\n";
      PrintUsage(argv[0]);
      return false;
    }
  }
  if (opt->num_inserts <= 0 || opt->threads <= 0 || opt->key_size == 0 || opt->value_size == 0) {
    std::cerr << "Invalid numeric options\n";
    return false;
  }
  return true;
}

struct Row {
  char* key = nullptr;
  char* value = nullptr;
  size_t key_size = 0;
  size_t value_size = 0;

  void Free() {
    free(key);
    free(value);
    key = nullptr;
    value = nullptr;
  }
};

bool InsertRow(int64_t seq, const Options& opt, Row* row) {
  row->key_size = opt.key_size;
  row->value_size = opt.value_size;
  row->key = static_cast<char*>(malloc(opt.key_size));
  row->value = static_cast<char*>(malloc(opt.value_size));
  if (!row->key || !row->value) {
    row->Free();
    return false;
  }
  std::memset(row->key, static_cast<int>(seq & 0xff), opt.key_size);
  if (opt.key_size >= sizeof(int64_t)) {
    std::memcpy(row->key, &seq, sizeof(seq));
  }
  std::memset(row->value, static_cast<int>((seq * 31) & 0xff), opt.value_size);
  if (opt.value_size > 8) {
    row->value[opt.value_size / 2] = static_cast<char>(seq);
  }
  return true;
}

std::string DetectAllocatorName() {
  const std::string override_name = YB_ALLOC_BENCH_STRINGIFY(ALLOCATOR_DISPLAY_NAME);
  if (override_name != "unknown") {
    return override_name;
  }
#if HAVE_JEMALLOC_HEADER
  const char* ver = nullptr;
  size_t sz = sizeof(ver);
  if (mallctl("version", &ver, &sz, nullptr, 0) == 0 && ver != nullptr) {
    return std::string("jemalloc ") + ver;
  }
  return "jemalloc";
#elif USE_JEMALLOC || YB_JEMALLOC_ENABLED
  return "jemalloc";
#elif HAVE_GOOGLE_TCMALLOC
  return "google-tcmalloc";
#elif HAVE_GPERFTOOLS_TCMALLOC || USE_TCMALLOC || YB_TCMALLOC_ENABLED
  return "tcmalloc";
#else
  return "system-malloc";
#endif
}

void AppendAllocatorStats(std::ostream& os) {
#if HAVE_JEMALLOC_HEADER
  auto read = [](const char* name) -> size_t {
    size_t v = 0;
    size_t sz = sizeof(v);
    uint64_t epoch = 1;
    size_t esz = sizeof(epoch);
    (void)mallctl("epoch", &epoch, &esz, &epoch, esz);
    if (mallctl(name, &v, &sz, nullptr, 0) != 0) {
      return 0;
    }
    return v;
  };
  os << " jemalloc_allocated=" << read("stats.allocated")
     << " jemalloc_active=" << read("stats.active")
     << " jemalloc_resident=" << read("stats.resident")
     << " jemalloc_mapped=" << read("stats.mapped");
#elif HAVE_GPERFTOOLS_TCMALLOC
  size_t allocated = 0, heap = 0, free_bytes = 0, unmapped = 0;
  MallocExtension::instance()->GetNumericProperty("generic.current_allocated_bytes", &allocated);
  MallocExtension::instance()->GetNumericProperty("generic.heap_size", &heap);
  MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_free_bytes", &free_bytes);
  MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_unmapped_bytes", &unmapped);
  os << " tcmalloc_allocated=" << allocated
     << " tcmalloc_heap=" << heap
     << " tcmalloc_pageheap_free=" << free_bytes
     << " tcmalloc_pageheap_unmapped=" << unmapped;
#elif HAVE_GOOGLE_TCMALLOC
  auto prop = [](const char* name) -> size_t {
    auto v = tcmalloc::MallocExtension::GetNumericProperty(name);
    return v.value_or(0);
  };
  os << " tcmalloc_allocated=" << prop("generic.current_allocated_bytes")
     << " tcmalloc_heap=" << prop("generic.heap_size")
     << " tcmalloc_pageheap_free=" << prop("tcmalloc.pageheap_free_bytes")
     << " tcmalloc_pageheap_unmapped=" << prop("tcmalloc.pageheap_unmapped_bytes");
#else
  (void)os;
#endif
}

struct BenchResult {
  int64_t inserts = 0;
  double seconds = 0;
  uint64_t checksum = 0;
  bool ok = true;
  std::string error;
};

BenchResult RunWorkload(const Options& opt) {
  BenchResult result;
  const int64_t total = opt.num_inserts;
  const int nthreads = opt.threads;
  std::atomic<int64_t> next{0};
  std::atomic<uint64_t> checksum{0};
  std::atomic<bool> failed{false};
  std::string fail_msg;
  std::mutex fail_mu;

  auto worker = [&]() {
    std::vector<Row> retained;
    if (opt.retain_all) {
      retained.reserve(static_cast<size_t>(total / nthreads + 8));
    }
    uint64_t local_sum = 0;
    for (;;) {
      int64_t i = next.fetch_add(1, std::memory_order_relaxed);
      if (i >= total) {
        break;
      }
      Row row;
      if (!InsertRow(i, opt, &row)) {
        failed.store(true);
        std::lock_guard<std::mutex> l(fail_mu);
        fail_msg = "malloc failed (OOM?)";
        break;
      }
      if (row.key_size >= sizeof(int64_t)) {
        int64_t k;
        std::memcpy(&k, row.key, sizeof(k));
        local_sum += static_cast<uint64_t>(k);
      } else {
        local_sum += static_cast<unsigned char>(row.key[0]);
      }
      local_sum += static_cast<unsigned char>(row.value[row.value_size / 2]);
      if (opt.retain_all) {
        retained.push_back(row);
      } else {
        row.Free();
      }
    }
    for (auto& r : retained) {
      r.Free();
    }
    checksum.fetch_add(local_sum, std::memory_order_relaxed);
  };

  for (int w = 0; w < opt.warmup_inserts; ++w) {
    Row row;
    if (InsertRow(w, opt, &row)) {
      row.Free();
    }
  }

  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(nthreads));
  const auto t0 = std::chrono::steady_clock::now();
  for (int t = 0; t < nthreads; ++t) {
    threads.emplace_back(worker);
  }
  for (auto& th : threads) {
    th.join();
  }
  const auto t1 = std::chrono::steady_clock::now();

  result.inserts = total;
  result.seconds = std::chrono::duration<double>(t1 - t0).count();
  result.checksum = checksum.load();
  if (failed.load()) {
    result.ok = false;
    result.error = fail_msg;
  }
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!ParseArgs(argc, argv, &opt)) {
    return 2;
  }

  const std::string allocator = DetectAllocatorName();
  std::cout << "allocator_benchmark starting"
            << " allocator=\"" << allocator << "\""
            << " num_inserts=" << opt.num_inserts
            << " key_size=" << opt.key_size
            << " value_size=" << opt.value_size
            << " threads=" << opt.threads
            << " retain_all=" << (opt.retain_all ? "true" : "false")
            << "\n";

  const BenchResult r = RunWorkload(opt);
  if (!r.ok) {
    std::cerr << "FAIL: " << r.error << "\n";
    return 1;
  }

  const double ips = r.seconds > 0 ? (static_cast<double>(r.inserts) / r.seconds) : 0.0;
  const double ns_per = r.inserts > 0 ? (r.seconds * 1e9 / static_cast<double>(r.inserts)) : 0.0;

  std::ostringstream stats;
  AppendAllocatorStats(stats);

  std::cout << "RESULT"
            << " allocator=\"" << allocator << "\""
            << " inserts=" << r.inserts
            << " seconds=" << r.seconds
            << " inserts_per_sec=" << ips
            << " ns_per_insert=" << ns_per
            << " checksum=" << r.checksum
            << stats.str()
            << "\n";

  std::cout << "OK: completed " << r.inserts << " inserts in " << r.seconds << " s ("
            << ips << " inserts/s)\n";
  return 0;
}
