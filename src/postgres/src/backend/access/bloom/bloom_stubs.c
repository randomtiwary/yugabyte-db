#include "postgres.h"
#include "access/bloomam.h"

bool bloomamvalidate(Oid opclassoid) { return true; }
IndexBulkDeleteResult *bloomambulkdelete(IndexVacuumInfo *info,
	IndexBulkDeleteResult *stats, IndexBulkDeleteCallback callback, void *callback_state)
{ return stats; }
IndexBulkDeleteResult *bloomamvacuumcleanup(IndexVacuumInfo *info,
	IndexBulkDeleteResult *stats) { return stats; }
void bloomamcostestimate(PlannerInfo *root, IndexPath *path, double loop_count,
	Cost *indexStartupCost, Cost *indexTotalCost, Selectivity *indexSelectivity,
	double *indexCorrelation, double *indexPages)
{
	*indexStartupCost = 0; *indexTotalCost = 1; *indexSelectivity = 0.01;
	*indexCorrelation = 0; *indexPages = 1;
}
