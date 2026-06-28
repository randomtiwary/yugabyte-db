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
#include "yb/server/jemalloc_metrics.h"

#include "yb/gutil/bind.h"

#include "yb/util/jemalloc_util.h"
#include "yb/util/metrics.h"

#if YB_JEMALLOC_ENABLED
#define JEMALLOC_DISABLED_MSG
#else
#define JEMALLOC_DISABLED_MSG " (Disabled - no jemalloc in this build)"
#endif

// Core jemalloc stats (see http://jemalloc.net/jemalloc.3.html mallctl stats.*).
// Exposed so operators can debug allocator behavior when --use-jemalloc is enabled.

METRIC_DEFINE_gauge_uint64(server, jemalloc_allocated_bytes,
    "JEMalloc Allocated Bytes", yb::MetricUnit::kBytes,
    "Total number of bytes allocated by the application (jemalloc stats.allocated)."
    JEMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, jemalloc_active_bytes,
    "JEMalloc Active Bytes", yb::MetricUnit::kBytes,
    "Total number of bytes in active pages allocated by the application "
    "(jemalloc stats.active)." JEMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, jemalloc_mapped_bytes,
    "JEMalloc Mapped Bytes", yb::MetricUnit::kBytes,
    "Total number of bytes in active extents mapped by the allocator "
    "(jemalloc stats.mapped)." JEMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, jemalloc_resident_bytes,
    "JEMalloc Resident Bytes", yb::MetricUnit::kBytes,
    "Maximum number of bytes in physically resident data pages mapped by the allocator "
    "(jemalloc stats.resident). Useful for correlating with process RSS."
    JEMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, jemalloc_retained_bytes,
    "JEMalloc Retained Bytes", yb::MetricUnit::kBytes,
    "Total number of bytes in virtual memory mappings that were retained rather than "
    "returned to the OS (jemalloc stats.retained)." JEMALLOC_DISABLED_MSG);

METRIC_DEFINE_gauge_uint64(server, jemalloc_metadata_bytes,
    "JEMalloc Metadata Bytes", yb::MetricUnit::kBytes,
    "Total number of bytes dedicated to jemalloc metadata (jemalloc stats.metadata)."
    JEMALLOC_DISABLED_MSG);

#undef JEMALLOC_DISABLED_MSG

namespace yb {
namespace jemalloc {

namespace {

uint64_t ReadAllocated() { return static_cast<uint64_t>(GetJEMallocAllocatedBytes()); }
uint64_t ReadActive() { return static_cast<uint64_t>(GetJEMallocActiveBytes()); }
uint64_t ReadMapped() { return static_cast<uint64_t>(GetJEMallocMappedBytes()); }
uint64_t ReadResident() { return static_cast<uint64_t>(GetJEMallocResidentBytes()); }
uint64_t ReadRetained() { return static_cast<uint64_t>(GetJEMallocRetainedBytes()); }
uint64_t ReadMetadata() { return static_cast<uint64_t>(GetJEMallocMetadataBytes()); }

}  // namespace

void RegisterMetrics(const scoped_refptr<MetricEntity>& entity) {
  entity->NeverRetire(
      METRIC_jemalloc_allocated_bytes.InstantiateFunctionGauge(
          entity, Bind(&ReadAllocated)));
  entity->NeverRetire(
      METRIC_jemalloc_active_bytes.InstantiateFunctionGauge(
          entity, Bind(&ReadActive)));
  entity->NeverRetire(
      METRIC_jemalloc_mapped_bytes.InstantiateFunctionGauge(
          entity, Bind(&ReadMapped)));
  entity->NeverRetire(
      METRIC_jemalloc_resident_bytes.InstantiateFunctionGauge(
          entity, Bind(&ReadResident)));
  entity->NeverRetire(
      METRIC_jemalloc_retained_bytes.InstantiateFunctionGauge(
          entity, Bind(&ReadRetained)));
  entity->NeverRetire(
      METRIC_jemalloc_metadata_bytes.InstantiateFunctionGauge(
          entity, Bind(&ReadMetadata)));
}

}  // namespace jemalloc
}  // namespace yb
