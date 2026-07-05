#include "postgres.h"
#include "access/bloomam.h"

bool bloomamvalidate(Oid opclassoid) { return true; }
IndexScanDesc bloomambeginscan(Relation r, int nkeys, int norderbys)
{ elog(ERROR, "bloomambeginscan stub"); return NULL; }
void bloomamrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
	ScanKey orderbys, int norderbys) {}
int64 bloomamgetbitmap(IndexScanDesc scan, TIDBitmap *tbm) { return 0; }
void bloomamendscan(IndexScanDesc scan) {}
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
