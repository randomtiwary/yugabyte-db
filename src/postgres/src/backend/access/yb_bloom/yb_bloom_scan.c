/*--------------------------------------------------------------------------
 * yb_bloom_scan.c
 * Copyright (c) YugabyteDB, Inc.
 * IDENTIFICATION
 *	  src/backend/access/yb_bloom/yb_bloom_scan.c
 *--------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/relscan.h"
#include "access/yb_bloom.h"
#include "access/yb_scan.h"
#include "pg_yb_utils.h"
#include "pgstat.h"

IndexScanDesc
ybbloombeginscan(Relation rel, int nkeys, int norderbys)
{
	IndexScanDesc scan;

	Assert(norderbys == 0);
	scan = RelationGetIndexScan(rel, nkeys, norderbys);
	scan->opaque = NULL;
	return scan;
}

void
ybbloomrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
			  ScanKey orderbys, int norderbys)
{
	YbScanDesc	ybScan;

	if (scan->opaque)
	{
		ybc_free_ybscan((YbScanDesc) scan->opaque);
		scan->opaque = NULL;
	}
	if (scankey && nscankeys > 0)
		memmove(scan->keyData, scankey, nscankeys * sizeof(ScanKeyData));

	/* Signatures live in DocDB; do not push raw keys as index predicates. */
	ybScan = YbBeginScan(scan->heapRelation,
						 scan->indexRelation,
						 scan->xs_want_itup,
						 0,
						 NULL,
						 scan->yb_scan_plan,
						 scan->yb_rel_pushdown,
						 scan->yb_idx_pushdown,
						 scan->yb_aggrefs,
						 scan->yb_distinct_prefixlen,
						 scan->yb_exec_params,
						 false,
						 scan->fetch_ybctids_only);
	scan->xs_recheck = true;
	scan->opaque = ybScan;
}

bool
ybbloomgettuple(IndexScanDesc scan, ScanDirection dir)
{
	YbScanDesc	ybscan = (YbScanDesc) scan->opaque;
	bool		has_tuple = false;

	ybscan->exec_params = scan->yb_exec_params;
	if (ybscan->exec_params)
		ybscan->exec_params->work_mem = work_mem;
	if (!ybscan->is_exec_done)
		pgstat_count_index_scan(scan->indexRelation);

	scan->xs_recheck = true;
	if (ybscan->prepare_params.index_only_scan)
	{
		IndexTuple	tuple = ybc_getnext_indextuple(ybscan, dir);

		if (tuple)
		{
			scan->xs_itup = tuple;
			scan->xs_itupdesc = RelationGetDescr(scan->indexRelation);
			has_tuple = true;
		}
	}
	else
	{
		HeapTuple	tuple = ybc_getnext_heaptuple(ybscan, dir);

		if (tuple)
		{
			scan->xs_hitup = tuple;
			scan->xs_hitupdesc = RelationGetDescr(scan->heapRelation);
			has_tuple = true;
		}
	}
	return has_tuple;
}

void
ybbloomendscan(IndexScanDesc scan)
{
	if (scan->opaque)
	{
		ybc_free_ybscan((YbScanDesc) scan->opaque);
		scan->opaque = NULL;
	}
}

bool
ybbloommightrecheck(Scan *scan, Relation heapRelation, Relation indexRelation,
					bool xs_want_itup, ScanKey keys, int nkeys)
{
	return true;
}
