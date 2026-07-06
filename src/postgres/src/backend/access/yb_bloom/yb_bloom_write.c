/*--------------------------------------------------------------------------
 *
 * yb_bloom_write.c
 *	  DocDB insert, delete, and build support for yb_bloom.
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
 *			src/backend/access/yb_bloom/yb_bloom_write.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/genam.h"
#include "miscadmin.h"
#include "access/sysattr.h"
#include "access/yb_bloom.h"
#include "access/tableam.h"
#include "access/yb_scan.h"
#include "commands/progress.h"
#include "catalog/index.h"
#include "catalog/pg_type_d.h"
#include "catalog/yb_type.h"
#include "commands/yb_cmds.h"
#include "executor/ybModifyTable.h"
#include "pg_yb_utils.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/memutils.h"
#include "utils/rel.h"

static void
YbCheckBloomIndexEnabled(void)
{
	if (!yb_enable_bloom_index)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("bloom indexes are not enabled"),
				 errhint("Set yb_enable_bloom_index to on.")));
}

typedef struct
{
	YbBloomState state;		/* hash support, options, and column count */
	double		indtuples;	/* index rows written so far in this build */

	/*
	 * Per-tuple scratch context.  Created in ybbloombuild() /
	 * ybbloombackfill() before the heap scan, entered for each
	 * ybbloomBuildCallback invocation so signature allocations live here,
	 * reset at the end of that tuple, and deleted when the scan finishes.
	 */
	MemoryContext tmpCtx;

	/*
	 * Online backfill write timestamp passed to YBCExecuteInsertIndex.
	 * Non-NULL only during ybbloombackfill(); NULL for offline builds.
	 */
	uint64_t   *backfilltime;
} YbBloomBuildState;

static void
ybbloom_bind_write(YbcPgStatement stmt, void *indexstate, Relation index,
				   Datum *values, bool *isnull, int n_bound_atts,
				   Datum ybbasectid, bool ybctid_as_value)
{
	YbBloomState *state = (YbBloomState *) indexstate;
	bytea	   *signature;
	Datum		sigdatum;
	bool		signull = false;

	if (ybbasectid == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("missing base table ybctid in yb_bloom index write")));

	signature = YbBloomFormSignature(state, values, isnull);
	sigdatum = PointerGetDatum(signature);

	/* Key: base ybctid. Payload: compact bloom signature. */
	YbBindDatumToColumn(stmt, YBIdxBaseTupleIdAttributeNumber, BYTEAOID,
						InvalidOid, ybbasectid, false, NULL);
	YbBindDatumToColumn(stmt, 1, BYTEAOID, InvalidOid, sigdatum, signull, NULL);
}

static void
ybbloom_bind_delete(YbcPgStatement stmt, void *indexstate, Relation index,
					Datum *values, bool *isnull, int n_bound_atts,
					Datum ybbasectid, bool ybctid_as_value)
{
	if (ybbasectid == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("missing base table ybctid in yb_bloom index delete")));
	YbBindDatumToColumn(stmt, YBIdxBaseTupleIdAttributeNumber, BYTEAOID,
						InvalidOid, ybbasectid, false, NULL);
}

void
ybbloombindschema(YbcPgStatement handle, struct IndexInfo *indexInfo,
				  TupleDesc indexTupleDesc, int16 *coloptions,
				  Oid *opclassIds, Datum reloptions)
{
	const YbcPgTypeEntity *bytea_type = YbDataTypeFromOidMod(1, BYTEAOID);

	/*
	 * DocDB table stores a single HASH bytea signature column plus the base
	 * ybctid key suffix added by YB.  User columns drive opclasses only.
	 */
	HandleYBStatus(YBCPgCreateIndexAddColumn(handle,
											 "signature",
											 1,
											 bytea_type,
											 true,	/* is_hash */
											 true,	/* is_key */
											 false,
											 false));
}

static void
ybbloom_write_tuple(YbBloomState *state, Relation index, Datum *values,
					bool *isnull, Datum ybctid, uint64_t *backfilltime,
					bool isinsert)
{
	Datum		storage_values[INDEX_MAX_KEYS];
	bool		storage_isnull[INDEX_MAX_KEYS];
	bytea	   *signature = YbBloomFormSignature(state, values, isnull);

	storage_values[0] = PointerGetDatum(signature);
	storage_isnull[0] = false;

	if (isinsert)
		YBCExecuteInsertIndex(index, storage_values, storage_isnull, ybctid,
							  backfilltime, ybbloom_bind_write, state);
	else
		YBCExecuteDeleteIndex(index, storage_values, storage_isnull, ybctid,
							  ybbloom_bind_delete, state);
}

static void
ybbloomBuildCallback(Relation index, Datum ybctid, Datum *values,
					 bool *isnull, bool tupleIsAlive, void *state)
{
	YbBloomBuildState *buildstate = (YbBloomBuildState *) state;
	MemoryContext oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);

	ybbloom_write_tuple(&buildstate->state, index, values, isnull, ybctid,
						buildstate->backfilltime, true);
	buildstate->indtuples += 1;
	MemoryContextSwitchTo(oldCtx);
	MemoryContextReset(buildstate->tmpCtx);
}

IndexBuildResult *
ybbloombuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{
	YbBloomBuildState buildstate;
	IndexBuildResult *result;
	double		reltuples;

	YbCheckBloomIndexEnabled();
	memset(&buildstate, 0, sizeof(buildstate));
	YbBloomInitState(&buildstate.state, index);
	buildstate.tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
											  "yb_bloom build temporary context",
											  ALLOCSET_DEFAULT_SIZES);
	reltuples = yb_table_index_build_scan(heap, index, indexInfo, true,
								  ybbloomBuildCallback,
									   (void *) &buildstate, NULL);
	MemoryContextDelete(buildstate.tmpCtx);
	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
	result->heap_tuples = reltuples;
	result->index_tuples = buildstate.indtuples;
	return result;
}

/*
 * ambuildempty is mandatory in IndexAmRoutine (index_build asserts it is
 * non-NULL) and is invoked only to populate an unlogged index init fork.
 * YB relations do not use init forks, so this should never run; keep a stub
 * that logs, matching ybginbuildempty / ybcinbuildempty.
 */
void
ybbloombuildempty(Relation index)
{
	YBC_LOG_WARNING("Unexpected building of empty unlogged yb_bloom index");
}

IndexBuildResult *
ybbloombackfill(Relation heap, Relation index, struct IndexInfo *indexInfo,
				struct YbBackfillInfo *bfinfo,
				struct YbPgExecOutParam *bfresult)
{
	YbBloomBuildState buildstate;
	IndexBuildResult *result;
	double		reltuples;

	YbCheckBloomIndexEnabled();
	memset(&buildstate, 0, sizeof(buildstate));
	YbBloomInitState(&buildstate.state, index);
	buildstate.backfilltime = &bfinfo->read_time;
	buildstate.tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
											  "yb_bloom backfill temporary context",
											  ALLOCSET_DEFAULT_SIZES);
	reltuples = IndexBackfillHeapRangeScan(heap, index, indexInfo,
										   ybbloomBuildCallback,
										   (void *) &buildstate,
										   bfinfo, bfresult);
	MemoryContextDelete(buildstate.tmpCtx);
	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
	result->heap_tuples = reltuples;
	result->index_tuples = buildstate.indtuples;
	return result;
}

bool
ybbloomybinsert(Relation index, Datum *values, bool *isnull, Datum ybctid,
				Relation heapRel, IndexUniqueCheck checkUnique,
				struct IndexInfo *indexInfo, bool shared_insert)
{
	YbBloomState state;
	MemoryContext oldCtx;
	MemoryContext tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
												 "yb_bloom insert temporary context",
												 ALLOCSET_DEFAULT_SIZES);

	oldCtx = MemoryContextSwitchTo(tmpCtx);
	YbBloomInitState(&state, index);
	ybbloom_write_tuple(&state, index, values, isnull, ybctid, NULL, true);
	MemoryContextSwitchTo(oldCtx);
	MemoryContextDelete(tmpCtx);
	return false;
}

void
ybbloomybdelete(Relation index, Datum *values, bool *isnull, Datum ybctid,
				Relation heapRel, struct IndexInfo *indexInfo)
{
	YbBloomState state;
	MemoryContext oldCtx;
	MemoryContext tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
												 "yb_bloom delete temporary context",
												 ALLOCSET_DEFAULT_SIZES);

	oldCtx = MemoryContextSwitchTo(tmpCtx);
	YbBloomInitState(&state, index);
	ybbloom_write_tuple(&state, index, values, isnull, ybctid, NULL, false);
	MemoryContextSwitchTo(oldCtx);
	MemoryContextDelete(tmpCtx);
}
