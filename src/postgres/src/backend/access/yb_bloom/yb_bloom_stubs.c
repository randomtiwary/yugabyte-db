#include "postgres.h"
#include "access/yb_bloom.h"
bool ybbloomvalidate(Oid o) { return true; }
void ybbloomcostestimate(struct PlannerInfo *r, struct IndexPath *p, double l, Cost *a, Cost *b, Selectivity *c, double *d, double *e) { *a=*b=0; *c=0.01; *d=*e=0; }
void ybbloombindschema(YbcPgStatement h, struct IndexInfo *i, TupleDesc t, int16 *c, Oid *o, Datum r) {}
IndexBuildResult *ybbloombuild(Relation h, Relation i, struct IndexInfo *n) { elog(ERROR,"stub"); return NULL; }
void ybbloombuildempty(Relation i) {}
IndexBuildResult *ybbloombackfill(Relation h, Relation i, struct IndexInfo *n, struct YbBackfillInfo *b, struct YbPgExecOutParam *f) { return ybbloombuild(h,i,n); }
bool ybbloomybinsert(Relation i, Datum *v, bool *n, Datum y, Relation h, IndexUniqueCheck c, struct IndexInfo *f, bool s) { return false; }
void ybbloomybdelete(Relation i, Datum *v, bool *n, Datum y, Relation h, struct IndexInfo *f) {}
IndexScanDesc ybbloombeginscan(Relation r, int k, int o) { elog(ERROR,"stub"); return NULL; }
void ybbloomrescan(IndexScanDesc s, ScanKey k, int n, ScanKey o, int m) {}
bool ybbloomgettuple(IndexScanDesc s, ScanDirection d) { return false; }
void ybbloomendscan(IndexScanDesc s) {}
IndexBulkDeleteResult *ybbloombulkdelete(IndexVacuumInfo *i, IndexBulkDeleteResult *s, IndexBulkDeleteCallback c, void *x) { return s; }
IndexBulkDeleteResult *ybbloomvacuumcleanup(IndexVacuumInfo *i, IndexBulkDeleteResult *s) { return s; }
bool ybbloommightrecheck(Scan *s, Relation h, Relation i, bool x, ScanKey k, int n) { return true; }
