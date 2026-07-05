/*--------------------------------------------------------------------------
 *
 * bloomam.c
 *	  Handler for the built-in bloom signature index access method.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *	  src/backend/access/bloom/bloomam.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/bloomam.h"
#include "commands/vacuum.h"
#include "utils/guc.h"

Datum
blhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

	amroutine->amstrategies = BLOOMAM_NSTRATEGIES;
	amroutine->amsupport = BLOOMAM_NPROC;
	amroutine->amoptsprocnum = 0;
	amroutine->amcanorder = false;
	amroutine->amcanorderbyop = false;
	amroutine->amcanbackward = false;
	amroutine->amcanunique = false;
	amroutine->amcanmulticol = true;
	amroutine->amoptionalkey = true;
	amroutine->amsearcharray = false;
	amroutine->amsearchnulls = false;
	amroutine->amstorage = false;
	amroutine->amclusterable = false;
	amroutine->ampredlocks = false;
	amroutine->amcanparallel = false;
	amroutine->amcaninclude = false;
	amroutine->amusemaintenanceworkmem = false;
	amroutine->amparallelvacuumoptions =
		VACUUM_OPTION_PARALLEL_BULKDEL | VACUUM_OPTION_PARALLEL_CLEANUP;
	amroutine->amkeytype = InvalidOid;

	amroutine->ambuild = bloomambuild;
	amroutine->ambuildempty = bloomambuildempty;
	amroutine->aminsert = bloomaminsert;
	amroutine->ambulkdelete = bloomambulkdelete;
	amroutine->amvacuumcleanup = bloomamvacuumcleanup;
	amroutine->amcanreturn = NULL;
	amroutine->amcostestimate = bloomamcostestimate;
	amroutine->amoptions = bloomamoptions;
	amroutine->amproperty = NULL;
	amroutine->ambuildphasename = NULL;
	amroutine->amvalidate = bloomamvalidate;
	amroutine->amadjustmembers = NULL;
	amroutine->ambeginscan = bloomambeginscan;
	amroutine->amrescan = bloomamrescan;
	amroutine->amgettuple = NULL;
	amroutine->amgetbitmap = bloomamgetbitmap;
	amroutine->amendscan = bloomamendscan;
	amroutine->ammarkpos = NULL;
	amroutine->amrestrpos = NULL;
	amroutine->amestimateparallelscan = NULL;
	amroutine->aminitparallelscan = NULL;
	amroutine->amparallelrescan = NULL;

	/* Local storage only; not for DocDB-backed relations. */
	amroutine->yb_amisforybrelation = false;
	amroutine->yb_amiscopartitioned = false;
	amroutine->yb_aminsert = NULL;
	amroutine->yb_amdelete = NULL;
	amroutine->yb_amupdate = NULL;
	amroutine->yb_ambackfill = NULL;
	amroutine->yb_ammightrecheck = NULL;
	amroutine->yb_amgetbitmap = NULL;
	amroutine->yb_ambindschema = NULL;

	PG_RETURN_POINTER(amroutine);
}
