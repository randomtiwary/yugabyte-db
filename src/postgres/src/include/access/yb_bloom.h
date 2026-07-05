/*--------------------------------------------------------------------------
 *
 * yb_bloom.h
 *	  Public header for the built-in yb_bloom signature index access method.
 *
 * Signatures are stored in DocDB for YB relations.
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
 *			src/include/access/yb_bloom.h
 *--------------------------------------------------------------------------
 */

#pragma once

#include "access/amapi.h"
#include "access/itup.h"
#include "nodes/execnodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pathnodes.h"

#define YBBLOOM_HASH_PROC		1
#define YBBLOOM_NPROC			1
#define YBBLOOM_EQUAL_STRATEGY	1
#define YBBLOOM_NSTRATEGIES		1

typedef uint16 YbBloomSigWord;
#define YBBLOOM_SIGNWORDBITS	((int) (BITS_PER_BYTE * sizeof(YbBloomSigWord)))
#define YBBLOOM_DEFAULT_LENGTH	(5 * YBBLOOM_SIGNWORDBITS)
#define YBBLOOM_MAX_LENGTH		(256 * YBBLOOM_SIGNWORDBITS)
#define YBBLOOM_DEFAULT_BITS	2
#define YBBLOOM_MAX_BITS		(YBBLOOM_MAX_LENGTH - 1)

typedef struct YbBloomOptions
{
	int32		vl_len_;
	int			bloomLength;	/* words */
	int			bitSize[INDEX_MAX_KEYS];
} YbBloomOptions;

typedef struct YbBloomState
{
	FmgrInfo	hashFn[INDEX_MAX_KEYS];
	Oid			collations[INDEX_MAX_KEYS];
	YbBloomOptions opts;
	int32		nColumns;
} YbBloomState;

extern Datum ybbloomhandler(PG_FUNCTION_ARGS);
extern void YbBloomInitState(YbBloomState *state, Relation index);
extern void YbBloomSignValue(YbBloomState *state, YbBloomSigWord *sign,
							 Datum value, int attno);
extern bytea *YbBloomFormSignature(YbBloomState *state, Datum *values,
								   bool *isnull);
extern bool YbBloomSignatureMatch(bytea *stored, YbBloomSigWord *query,
								  int nwords);
extern bytea *ybbloomoptions(Datum reloptions, bool validate);
extern bool ybbloomvalidate(Oid opclassoid);
extern void ybbloomcostestimate(struct PlannerInfo *root, struct IndexPath *path,
								double loop_count, Cost *indexStartupCost,
								Cost *indexTotalCost,
								Selectivity *indexSelectivity,
								double *indexCorrelation, double *indexPages);
extern void ybbloombindschema(YbcPgStatement handle, struct IndexInfo *indexInfo,
							  TupleDesc indexTupleDesc, int16 *coloptions,
							  Oid *opclassIds, Datum reloptions);
extern IndexBuildResult *ybbloombuild(Relation heap, Relation index,
									  struct IndexInfo *indexInfo);
extern void ybbloombuildempty(Relation index);
extern IndexBuildResult *ybbloombackfill(Relation heap, Relation index,
										 struct IndexInfo *indexInfo,
										 struct YbBackfillInfo *bfinfo,
										 struct YbPgExecOutParam *bfresult);
extern bool ybbloomybinsert(Relation index, Datum *values, bool *isnull,
							Datum ybctid, Relation heapRel,
							IndexUniqueCheck checkUnique,
							struct IndexInfo *indexInfo, bool shared_insert);
extern void ybbloomybdelete(Relation index, Datum *values, bool *isnull,
							Datum ybctid, Relation heapRel,
							struct IndexInfo *indexInfo);
extern IndexScanDesc ybbloombeginscan(Relation rel, int nkeys, int norderbys);
extern void ybbloomrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
						  ScanKey orderbys, int norderbys);
extern bool ybbloomgettuple(IndexScanDesc scan, ScanDirection dir);
extern void ybbloomendscan(IndexScanDesc scan);
extern IndexBulkDeleteResult *ybbloombulkdelete(IndexVacuumInfo *info,
												IndexBulkDeleteResult *stats,
												IndexBulkDeleteCallback callback,
												void *callback_state);
extern IndexBulkDeleteResult *ybbloomvacuumcleanup(IndexVacuumInfo *info,
												   IndexBulkDeleteResult *stats);
extern bool ybbloommightrecheck(Scan *scan, Relation heapRelation,
								Relation indexRelation, bool xs_want_itup,
								ScanKey keys, int nkeys);
