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

#include "yb/util/jemalloc_util.h"

#include <limits>
#include <string>

#include "yb/util/logging.h"

#if YB_JEMALLOC_ENABLED
#include <jemalloc/jemalloc.h>
#endif

namespace yb {

#if YB_JEMALLOC_ENABLED

namespace {

// Write callback for malloc_stats_print that appends into a std::string.
void JEMallocStatsWriteCb(void* opaque, const char* buf) {
  auto* out = static_cast<std::string*>(opaque);
  *out += buf;
}

void AdvanceJEMallocEpoch() {
  uint64_t epoch = 1;
  size_t epoch_sz = sizeof(epoch);
  // Ignore failures; stats may be slightly stale.
  (void)mallctl("epoch", &epoch, &epoch_sz, &epoch, epoch_sz);
}

}  // namespace

#endif  // YB_JEMALLOC_ENABLED

uint64_t GetJEMallocStat(const char* name) {
#if YB_JEMALLOC_ENABLED
  AdvanceJEMallocEpoch();
  size_t value = 0;
  size_t sz = sizeof(value);
  int err = mallctl(name, &value, &sz, nullptr, 0);
  if (err != 0) {
    YB_LOG_EVERY_N_SECS(DFATAL, 10)
        << "Failed to read jemalloc stat " << name << ": errno=" << err;
    return 0;
  }
  if (value > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    YB_LOG_EVERY_N_SECS(DFATAL, 1)
        << "Value of jemalloc stat " << name << " too large for an int64_t: " << value;
    return static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
  }
  return static_cast<uint64_t>(value);
#else
  (void)name;
  return 0;
#endif
}

int64_t GetJEMallocAllocatedBytes() {
  return static_cast<int64_t>(GetJEMallocStat("stats.allocated"));
}

int64_t GetJEMallocActiveBytes() {
  return static_cast<int64_t>(GetJEMallocStat("stats.active"));
}

int64_t GetJEMallocMappedBytes() {
  return static_cast<int64_t>(GetJEMallocStat("stats.mapped"));
}

int64_t GetJEMallocResidentBytes() {
  return static_cast<int64_t>(GetJEMallocStat("stats.resident"));
}

int64_t GetJEMallocRetainedBytes() {
  return static_cast<int64_t>(GetJEMallocStat("stats.retained"));
}

int64_t GetJEMallocMetadataBytes() {
  return static_cast<int64_t>(GetJEMallocStat("stats.metadata"));
}

int64_t GetJEMallocActualHeapSizeBytes() {
#if YB_JEMALLOC_ENABLED
  // Resident approximates physical memory held by jemalloc for the process, analogous to
  // TCMalloc's heap size excluding unmapped pages for root memtracker accounting.
  return GetJEMallocResidentBytes();
#else
  return 0;
#endif
}

void JEMallocReleaseMemoryToSystem() {
#if YB_JEMALLOC_ENABLED
  // Purge all arenas so unused dirty pages can be returned to the OS.
  // MALLCTL_ARENAS_ALL selects every arena (jemalloc 5+).
  std::string purge_cmd = "arena." + std::to_string(MALLCTL_ARENAS_ALL) + ".purge";
  int err = mallctl(purge_cmd.c_str(), nullptr, nullptr, nullptr, 0);
  if (err != 0) {
    YB_LOG_EVERY_N_SECS(WARNING, 30)
        << "jemalloc arena purge via " << purge_cmd << " failed (errno=" << err
        << "); memory may not be returned to OS";
  }
#endif
}

void ConfigureJEMalloc(int64_t mem_limit) {
#if YB_JEMALLOC_ENABLED
  const char* version = nullptr;
  size_t sz = sizeof(version);
  if (mallctl("version", &version, &sz, nullptr, 0) == 0 && version != nullptr) {
    LOG(INFO) << "jemalloc enabled, version=" << version
              << ", root memory limit hint=" << mem_limit;
  } else {
    LOG(INFO) << "jemalloc enabled (version unknown), root memory limit hint=" << mem_limit;
  }
#else
  (void)mem_limit;
#endif
}

std::string GetJEMallocStatsString() {
#if YB_JEMALLOC_ENABLED
  std::string out;
  // Moderate detail suitable for logs / debugging (general + arenas).
  malloc_stats_print(JEMallocStatsWriteCb, &out, "ga");
  return out;
#else
  return {};
#endif
}

}  // namespace yb
