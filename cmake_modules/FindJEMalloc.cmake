# -*- cmake -*-
#
# - Find jemalloc
# This module defines
#  JEMALLOC_INCLUDE_DIR, where to find jemalloc/jemalloc.h
#  JEMALLOC_SHARED_LIB, path to jemalloc's shared library
#  JEMALLOC_STATIC_LIB, path to jemalloc's static library
#
# Portions Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

FIND_PATH(JEMALLOC_INCLUDE_DIR jemalloc/jemalloc.h
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

FIND_LIBRARY(JEMALLOC_SHARED_LIB jemalloc
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
FIND_LIBRARY(JEMALLOC_STATIC_LIB libjemalloc.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

# Fall back to system jemalloc when not present in the third-party install tree.
# This keeps jemalloc opt-in builds workable before jemalloc is packaged in
# yugabyte-db-thirdparty.
if(NOT JEMALLOC_INCLUDE_DIR OR NOT JEMALLOC_STATIC_LIB)
  FIND_PATH(JEMALLOC_INCLUDE_DIR jemalloc/jemalloc.h)
  FIND_LIBRARY(JEMALLOC_SHARED_LIB jemalloc)
  FIND_LIBRARY(JEMALLOC_STATIC_LIB libjemalloc.a NAMES jemalloc)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JEMALLOC REQUIRED_VARS
  JEMALLOC_INCLUDE_DIR JEMALLOC_STATIC_LIB)
