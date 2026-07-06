/*--------------------------------------------------------------------------
 *
 * yb_bloom_vacuum.c
 *	  Vacuum hooks for the yb_bloom index access method.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *			src/backend/access/yb_bloom/yb_bloom_vacuum.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/yb_bloom.h"
#include "pg_yb_utils.h"

IndexBulkDeleteResult *
ybbloombulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				  IndexBulkDeleteCallback callback, void *callback_state)
{
	YBC_LOG_WARNING("Unexpected bulk delete of yb_bloom index via vacuum");
	return stats;
}

IndexBulkDeleteResult *
ybbloomvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	YBC_LOG_WARNING("Unexpected yb_bloom index cleanup via vacuum");
	return stats;
}
