/* Temporary stubs removed in later commits as real files land. */
#include "postgres.h"
#include "access/bloomam.h"

void BloomAmInitState(BloomAmState *state, Relation index)
{ elog(ERROR, "bloom index is not fully initialized"); }
void BloomAmSignValue(BloomAmState *state, BloomSigWord *sign, Datum value, int attno) {}
BloomAmTuple BloomAmFormTuple(BloomAmState *state, ItemPointer iptr, Datum *values, bool *isnull)
{ return NULL; }
bool BloomAmPageAddItem(BloomAmState *state, Page page, BloomAmTuple tuple)
{ return false; }
bool bloomamvalidate(Oid opclassoid) { return true; }
bytea *bloomamoptions(Datum reloptions, bool validate) { return NULL; }
IndexBuildResult *bloomambuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{ elog(ERROR, "bloomambuild stub"); return NULL; }
void bloomambuildempty(Relation index) { BloomAmInitMetapage(index); }
bool bloomaminsert(Relation index, Datum *values, bool *isnull, ItemPointer ht_ctid,
	Relation heapRel, IndexUniqueCheck checkUnique, bool indexUnchanged,
	struct IndexInfo *indexInfo) { elog(ERROR, "bloomaminsert stub"); return false; }
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
	*indexStartupCost = 0;
	*indexTotalCost = 1;
	*indexSelectivity = 0.01;
	*indexCorrelation = 0;
	*indexPages = 1;
}
