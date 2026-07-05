/*--------------------------------------------------------------------------
 *
 * bloom_cost.c
 *	  Cost estimation for the built-in bloom index AM.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/access/bloom/bloom_cost.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/bloomam.h"
#include "nodes/pathnodes.h"
#include "utils/selfuncs.h"

void
bloomamcostestimate(PlannerInfo *root, IndexPath *path, double loop_count,
					Cost *indexStartupCost, Cost *indexTotalCost,
					Selectivity *indexSelectivity, double *indexCorrelation,
					double *indexPages)
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
