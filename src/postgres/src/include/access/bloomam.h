/*--------------------------------------------------------------------------
 *
 * bloomam.h
 *	  Public header for the built-in bloom signature index access method.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *	  src/include/access/bloomam.h
 *--------------------------------------------------------------------------
 */
#pragma once

#include "access/amapi.h"
#include "access/itup.h"
#include "nodes/pathnodes.h"

/* Support procedure numbers */
#define BLOOMAM_HASH_PROC		1
#define BLOOMAM_NPROC			1

/* Scan strategies */
#define BLOOMAM_EQUAL_STRATEGY	1
#define BLOOMAM_NSTRATEGIES		1

typedef uint16 BloomSigWord;
#define BLOOMAM_SIGNWORDBITS	((int) (BITS_PER_BYTE * sizeof(BloomSigWord)))

#define BLOOMAM_DEFAULT_LENGTH	(5 * BLOOMAM_SIGNWORDBITS)	/* bits */
#define BLOOMAM_MAX_LENGTH		(256 * BLOOMAM_SIGNWORDBITS)
#define BLOOMAM_DEFAULT_BITS	2
#define BLOOMAM_MAX_BITS		(BLOOMAM_MAX_LENGTH - 1)

#define BLOOMAM_PAGE_ID			0xFB10
#define BLOOMAM_META			(1 << 0)
#define BLOOMAM_DELETED			(1 << 1)
#define BLOOMAM_METAPAGE_BLKNO	0
#define BLOOMAM_HEAD_BLKNO		1
#define BLOOMAM_MAGIC			0x0BF10B10U

typedef struct BloomAmPageOpaqueData
{
	OffsetNumber maxoff;
	uint16		flags;
	uint16		unused;
	uint16		bloom_page_id;
} BloomAmPageOpaqueData;
typedef BloomAmPageOpaqueData *BloomAmPageOpaque;

typedef struct BloomAmOptions
{
	int32		vl_len_;
	int			bloomLength;	/* signature length in words */
	int			bitSize[INDEX_MAX_KEYS];
} BloomAmOptions;

typedef struct BloomAmMetaPageData
{
	uint32		magic;
	uint16		nStart;
	uint16		nEnd;
	BloomAmOptions opts;
	BlockNumber notFullPage[1];	/* variable; sized at init */
} BloomAmMetaPageData;

typedef struct BloomAmState
{
	FmgrInfo	hashFn[INDEX_MAX_KEYS];
	Oid			collations[INDEX_MAX_KEYS];
	BloomAmOptions opts;
	int32		nColumns;
	Size		sizeOfTuple;
} BloomAmState;

typedef struct BloomAmTupleData
{
	ItemPointerData heapPtr;
	BloomSigWord sign[FLEXIBLE_ARRAY_MEMBER];
} BloomAmTupleData;
typedef BloomAmTupleData *BloomAmTuple;

#define BLOOMAM_TUPLEHDRSZ MAXALIGN(offsetof(BloomAmTupleData, sign))

typedef struct BloomAmScanOpaqueData
{
	BloomSigWord *sign;
	BloomAmState state;
} BloomAmScanOpaqueData;
typedef BloomAmScanOpaqueData *BloomAmScanOpaque;

#define BloomAmPageGetOpaque(page) \
	((BloomAmPageOpaque) PageGetSpecialPointer(page))
#define BloomAmPageGetMaxOffset(page) (BloomAmPageGetOpaque(page)->maxoff)
#define BloomAmPageIsMeta(page) \
	((BloomAmPageGetOpaque(page)->flags & BLOOMAM_META) != 0)
#define BloomAmPageIsDeleted(page) \
	((BloomAmPageGetOpaque(page)->flags & BLOOMAM_DELETED) != 0)
#define BloomAmPageGetMeta(page) ((BloomAmMetaPageData *) PageGetContents(page))
#define BloomAmPageGetTuple(state, page, offset) \
	((BloomAmTuple) (PageGetContents(page) + \
		(state)->sizeOfTuple * ((offset) - 1)))
#define BloomAmPageGetFreeSpace(state, page) \
	(BLCKSZ - MAXALIGN(SizeOfPageHeaderData) \
		- BloomAmPageGetMaxOffset(page) * (state)->sizeOfTuple \
		- MAXALIGN(sizeof(BloomAmPageOpaqueData)))

extern Datum blhandler(PG_FUNCTION_ARGS);

extern void BloomAmInitState(BloomAmState *state, Relation index);
extern void BloomAmInitPage(Page page, uint16 flags);
extern void BloomAmFillMetapage(Relation index, Page metaPage);
extern void BloomAmInitMetapage(Relation index);
extern Buffer BloomAmNewBuffer(Relation index);
extern void BloomAmSignValue(BloomAmState *state, BloomSigWord *sign,
							 Datum value, int attno);
extern BloomAmTuple BloomAmFormTuple(BloomAmState *state, ItemPointer iptr,
									 Datum *values, bool *isnull);
extern bool BloomAmPageAddItem(BloomAmState *state, Page page,
							   BloomAmTuple tuple);
extern bool bloomamvalidate(Oid opclassoid);
extern bytea *bloomamoptions(Datum reloptions, bool validate);

extern IndexBuildResult *bloomambuild(Relation heap, Relation index,
									  struct IndexInfo *indexInfo);
extern void bloomambuildempty(Relation index);
extern bool bloomaminsert(Relation index, Datum *values, bool *isnull,
						  ItemPointer ht_ctid, Relation heapRel,
						  IndexUniqueCheck checkUnique,
						  bool indexUnchanged,
						  struct IndexInfo *indexInfo);
extern IndexScanDesc bloomambeginscan(Relation r, int nkeys, int norderbys);
extern void bloomamrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
						  ScanKey orderbys, int norderbys);
extern int64 bloomamgetbitmap(IndexScanDesc scan, TIDBitmap *tbm);
extern void bloomamendscan(IndexScanDesc scan);
extern IndexBulkDeleteResult *bloomambulkdelete(IndexVacuumInfo *info,
												IndexBulkDeleteResult *stats,
												IndexBulkDeleteCallback callback,
												void *callback_state);
extern IndexBulkDeleteResult *bloomamvacuumcleanup(IndexVacuumInfo *info,
												   IndexBulkDeleteResult *stats);
extern void bloomamcostestimate(PlannerInfo *root, IndexPath *path,
								double loop_count, Cost *indexStartupCost,
								Cost *indexTotalCost,
								Selectivity *indexSelectivity,
								double *indexCorrelation,
								double *indexPages);
