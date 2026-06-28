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

#pragma once

// Utilities for using jemalloc when YB is built with --use-jemalloc (YB_JEMALLOC_ENABLED).
// When jemalloc is not enabled, the getters return 0 and mutators are no-ops.

#include <cstdint>
#include <string>

namespace yb {

// Returns the jemalloc mallctl size_t statistic with the given name (e.g. "stats.allocated"),
// or 0 if jemalloc is not used / the mallctl call fails (logs DFATAL on failure when enabled).
uint64_t GetJEMallocStat(const char* name);

// Total bytes allocated by the application (stats.allocated).
int64_t GetJEMallocAllocatedBytes();

// Active bytes in application-visible allocations (stats.active).
int64_t GetJEMallocActiveBytes();

// Bytes mapped by jemalloc (stats.mapped).
int64_t GetJEMallocMappedBytes();

// Resident (RSS-like) bytes (stats.resident).
int64_t GetJEMallocResidentBytes();

// Retained (virtual but not necessarily resident) bytes (stats.retained).
int64_t GetJEMallocRetainedBytes();

// Metadata overhead (stats.metadata).
int64_t GetJEMallocMetadataBytes();

// Heap size used for the root memtracker (resident bytes by default).
int64_t GetJEMallocActualHeapSizeBytes();

// Ask jemalloc to release unused dirty pages back to the OS (mallctl "arena.<i>.purge").
void JEMallocReleaseMemoryToSystem();

// Optional configuration based on flags / memory limit (currently logs version info).
void ConfigureJEMalloc(int64_t mem_limit);

// Human-readable jemalloc stats (malloc_stats_print to a string). Empty if not enabled.
std::string GetJEMallocStatsString();

}  // namespace yb
