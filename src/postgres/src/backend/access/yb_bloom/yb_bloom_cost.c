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
