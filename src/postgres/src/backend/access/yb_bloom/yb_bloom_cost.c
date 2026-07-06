/*--------------------------------------------------------------------------
 *
 * yb_bloom_cost.c
 *	  Cost estimation for the yb_bloom index access method.
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
 *			src/backend/access/yb_bloom/yb_bloom_cost.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/yb_bloom.h"
#include "utils/selfuncs.h"

void
ybbloomcostestimate(struct PlannerInfo *root, struct IndexPath *path,
					double loop_count, Cost *indexStartupCost,
					Cost *indexTotalCost, Selectivity *indexSelectivity,
					double *indexCorrelation, double *indexPages)
{
	IndexOptInfo *index = path->indexinfo;
	GenericCosts costs;

	MemSet(&costs, 0, sizeof(costs));
	costs.numIndexTuples = index->tuples;
	genericcostestimate(root, path, loop_count, &costs);
	*indexStartupCost = costs.indexStartupCost;
	*indexTotalCost = costs.indexTotalCost;
	*indexSelectivity = costs.indexSelectivity;
	*indexCorrelation = costs.indexCorrelation;
	*indexPages = costs.numIndexPages;
}
