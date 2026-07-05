/*--------------------------------------------------------------------------
 * yb_bloom.c - handler for built-in yb_bloom AM
 * Copyright (c) YugabyteDB, Inc.
 * IDENTIFICATION
 *	  src/backend/access/yb_bloom/yb_bloom.c
 *--------------------------------------------------------------------------
 */
#include "postgres.h"
#include "access/yb_bloom.h"
#include "commands/vacuum.h"
#include "pg_yb_utils.h"

Datum
ybbloomhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

	amroutine->amstrategies = YBBLOOM_NSTRATEGIES;
	amroutine->amsupport = YBBLOOM_NPROC;
	amroutine->amoptsprocnum = 0;
	amroutine->amcanorder = false;
	amroutine->amcanorderbyop = false;
	amroutine->amcanbackward = false;
	amroutine->amcanunique = false;
	amroutine->amcanmulticol = true;
	amroutine->amoptionalkey = true;
	amroutine->amsearcharray = false;
	amroutine->amsearchnulls = false;
	amroutine->amstorage = true;
	amroutine->amclusterable = false;
	amroutine->ampredlocks = true;
	amroutine->amcanparallel = false;
	amroutine->amcaninclude = false;
	amroutine->amusemaintenanceworkmem = false;
	amroutine->amparallelvacuumoptions = 0;
	amroutine->amkeytype = BYTEAOID;

	amroutine->ambuild = ybbloombuild;
	amroutine->ambuildempty = ybbloombuildempty;
	amroutine->aminsert = NULL; /* YB path uses yb_aminsert */
	amroutine->ambulkdelete = ybbloombulkdelete;
	amroutine->amvacuumcleanup = ybbloomvacuumcleanup;
	amroutine->amcanreturn = NULL;
	amroutine->amcostestimate = ybbloomcostestimate;
	amroutine->amoptions = ybbloomoptions;
	amroutine->amproperty = NULL;
	amroutine->ambuildphasename = NULL;
	amroutine->amvalidate = ybbloomvalidate;
	amroutine->amadjustmembers = NULL;
	amroutine->ambeginscan = ybbloombeginscan;
	amroutine->amrescan = ybbloomrescan;
	amroutine->amgettuple = ybbloomgettuple;
	amroutine->amgetbitmap = NULL;
	amroutine->amendscan = ybbloomendscan;
	amroutine->ammarkpos = NULL;
	amroutine->amrestrpos = NULL;
	amroutine->amestimateparallelscan = NULL;
	amroutine->aminitparallelscan = NULL;
	amroutine->amparallelrescan = NULL;

	/*
	 * Store bloom signatures in DocDB via the YB index table.  Each heap row
	 * produces one index row keyed by ybctid with a bytea signature payload.
	 */
	amroutine->yb_amisforybrelation = true;
	amroutine->yb_amiscopartitioned = false;
	amroutine->yb_aminsert = ybbloomybinsert;
	amroutine->yb_amdelete = ybbloomybdelete;
	amroutine->yb_amupdate = NULL;
	amroutine->yb_ambackfill = ybbloombackfill;
	amroutine->yb_ammightrecheck = ybbloommightrecheck;
	amroutine->yb_amgetbitmap = NULL;
	amroutine->yb_ambindschema = ybbloombindschema;

	PG_RETURN_POINTER(amroutine);
}
