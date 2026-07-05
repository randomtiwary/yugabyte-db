/*--------------------------------------------------------------------------
 *
 * bloom_insert.c
 *	  Build and insert for the built-in bloom index AM.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/access/bloom/bloom_insert.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/bloomam.h"
#include "access/generic_xlog.h"
#include "access/tableam.h"
#include "catalog/index.h"
#include "miscadmin.h"
#include "storage/bufmgr.h"
#include "storage/indexfsm.h"
#include "storage/smgr.h"
#include "utils/memutils.h"

typedef struct
{
	BloomAmState state;
	int64		indtuples;
	MemoryContext tmpCtx;
	Buffer		buffer;
} BloomAmBuildState;

static void
bloomam_flush(BloomAmBuildState *buildstate, Relation index)
{
	GenericXLogState *xlog;

	if (!BufferIsValid(buildstate->buffer))
		return;
	xlog = GenericXLogStart(index);
	GenericXLogRegisterBuffer(xlog, buildstate->buffer,
							  GENERIC_XLOG_FULL_IMAGE);
	GenericXLogFinish(xlog);
	UnlockReleaseBuffer(buildstate->buffer);
	buildstate->buffer = InvalidBuffer;
}

static void
bloomam_build_callback(Relation index, ItemPointer tid, Datum *values,
					   bool *isnull, bool tupleIsAlive, void *state)
{
	BloomAmBuildState *buildstate = (BloomAmBuildState *) state;
	MemoryContext oldCtx;
	BloomAmTuple tuple;
	Page		page;

	oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);
	tuple = BloomAmFormTuple(&buildstate->state, tid, values, isnull);

	if (!BufferIsValid(buildstate->buffer) ||
		BloomAmPageGetFreeSpace(&buildstate->state,
								BufferGetPage(buildstate->buffer)) <
		buildstate->state.sizeOfTuple)
	{
		if (BufferIsValid(buildstate->buffer))
		{
			MemoryContextSwitchTo(oldCtx);
			bloomam_flush(buildstate, index);
			oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);
		}
		buildstate->buffer = BloomAmNewBuffer(index);
		page = BufferGetPage(buildstate->buffer);
		BloomAmInitPage(page, 0);
	}

	page = BufferGetPage(buildstate->buffer);
	if (!BloomAmPageAddItem(&buildstate->state, page, tuple))
		elog(ERROR, "failed to add item to bloom index page");
	buildstate->indtuples++;
	MemoryContextSwitchTo(oldCtx);
	MemoryContextReset(buildstate->tmpCtx);
}

IndexBuildResult *
bloomambuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{
	IndexBuildResult *result;
	BloomAmBuildState buildstate;
	double		reltuples;

	BloomAmInitMetapage(index);
	memset(&buildstate, 0, sizeof(buildstate));
	BloomAmInitState(&buildstate.state, index);
	buildstate.buffer = InvalidBuffer;
	buildstate.tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
											  "Bloom build temporary context",
											  ALLOCSET_DEFAULT_SIZES);
	reltuples = table_index_build_scan(heap, index, indexInfo, true, true,
									   bloomam_build_callback,
									   (void *) &buildstate, NULL);
	bloomam_flush(&buildstate, index);
	MemoryContextDelete(buildstate.tmpCtx);

	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
	result->heap_tuples = reltuples;
	result->index_tuples = buildstate.indtuples;
	return result;
}

void
bloomambuildempty(Relation index)
{
	Page		metapage;

	metapage = (Page) palloc(BLCKSZ);
	BloomAmFillMetapage(index, metapage);
	PageSetChecksumInplace(metapage, BLOOMAM_METAPAGE_BLKNO);
	smgrwrite(RelationGetSmgr(index), MAIN_FORKNUM, BLOOMAM_METAPAGE_BLKNO,
			  (char *) metapage, true);
	log_newpage(&RelationGetSmgr(index)->smgr_rnode.node, MAIN_FORKNUM,
				BLOOMAM_METAPAGE_BLKNO, metapage, true);
	pfree(metapage);
}

static Buffer
bloomam_find_insert_page(Relation index, BloomAmState *state)
{
	Buffer		metaBuffer;
	Page		metaPage;
	BloomAmMetaPageData *meta;
	Buffer		buffer;
	Page		page;
	GenericXLogState *xlog;
	BlockNumber blk;

	metaBuffer = ReadBuffer(index, BLOOMAM_METAPAGE_BLKNO);
	LockBuffer(metaBuffer, BUFFER_LOCK_EXCLUSIVE);
	metaPage = BufferGetPage(metaBuffer);
	meta = BloomAmPageGetMeta(metaPage);

	while (meta->nStart < meta->nEnd)
	{
		blk = meta->notFullPage[meta->nStart];
		buffer = ReadBuffer(index, blk);
		LockBuffer(buffer, BUFFER_LOCK_EXCLUSIVE);
		page = BufferGetPage(buffer);
		if (!BloomAmPageIsDeleted(page) &&
			BloomAmPageGetFreeSpace(state, page) >= state->sizeOfTuple)
		{
			UnlockReleaseBuffer(metaBuffer);
			return buffer;
		}
		UnlockReleaseBuffer(buffer);
		meta->nStart++;
	}

	buffer = BloomAmNewBuffer(index);
	xlog = GenericXLogStart(index);
	page = GenericXLogRegisterBuffer(xlog, buffer, GENERIC_XLOG_FULL_IMAGE);
	BloomAmInitPage(page, 0);
	GenericXLogFinish(xlog);

	blk = BufferGetBlockNumber(buffer);
	xlog = GenericXLogStart(index);
	metaPage = GenericXLogRegisterBuffer(xlog, metaBuffer, 0);
	meta = BloomAmPageGetMeta(metaPage);
	if (meta->nEnd < 16)
	{
		meta->notFullPage[meta->nEnd] = blk;
		meta->nEnd++;
	}
	GenericXLogFinish(xlog);
	UnlockReleaseBuffer(metaBuffer);
	return buffer;
}

bool
bloomaminsert(Relation index, Datum *values, bool *isnull,
			  ItemPointer ht_ctid, Relation heapRel,
			  IndexUniqueCheck checkUnique, bool indexUnchanged,
			  struct IndexInfo *indexInfo)
{
	BloomAmState state;
	BloomAmTuple tuple;
	Buffer		buffer;
	Page		page;
	GenericXLogState *xlog;
	MemoryContext oldCtx;
	MemoryContext tmpCtx;

	tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
								   "Bloom insert temporary context",
								   ALLOCSET_DEFAULT_SIZES);
	oldCtx = MemoryContextSwitchTo(tmpCtx);
	BloomAmInitState(&state, index);
	tuple = BloomAmFormTuple(&state, ht_ctid, values, isnull);
	buffer = bloomam_find_insert_page(index, &state);
	xlog = GenericXLogStart(index);
	page = GenericXLogRegisterBuffer(xlog, buffer, 0);
	if (!BloomAmPageAddItem(&state, page, tuple))
		elog(ERROR, "failed to add item to bloom index page");
	GenericXLogFinish(xlog);
	UnlockReleaseBuffer(buffer);
	MemoryContextSwitchTo(oldCtx);
	MemoryContextDelete(tmpCtx);
	return false;
}
