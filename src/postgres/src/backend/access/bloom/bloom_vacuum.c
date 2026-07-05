/*--------------------------------------------------------------------------
 *
 * bloom_vacuum.c
 *	  Vacuum support for the built-in bloom index AM.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/access/bloom/bloom_vacuum.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/bloomam.h"
#include "access/generic_xlog.h"
#include "commands/vacuum.h"
#include "storage/bufmgr.h"
#include "storage/indexfsm.h"
#include "storage/lmgr.h"

IndexBulkDeleteResult *
bloomambulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				  IndexBulkDeleteCallback callback, void *callback_state)
{
	Relation	index = info->index;
	BloomAmState state;
	BlockNumber nblocks;
	BlockNumber blkno;

	if (stats == NULL)
		stats = (IndexBulkDeleteResult *) palloc0(sizeof(IndexBulkDeleteResult));

	BloomAmInitState(&state, index);
	nblocks = RelationGetNumberOfBlocks(index);
	for (blkno = BLOOMAM_HEAD_BLKNO; blkno < nblocks; blkno++)
	{
		Buffer		buffer;
		Page		page;
		OffsetNumber maxoff;
		OffsetNumber off;
		GenericXLogState *xlog;
		bool		modified = false;

		vacuum_delay_point();
		buffer = ReadBufferExtended(index, MAIN_FORKNUM, blkno, RBM_NORMAL,
									info->strategy);
		LockBuffer(buffer, BUFFER_LOCK_EXCLUSIVE);
		page = BufferGetPage(buffer);
		if (PageIsNew(page) || BloomAmPageIsMeta(page) || BloomAmPageIsDeleted(page))
		{
			UnlockReleaseBuffer(buffer);
			continue;
		}
		maxoff = BloomAmPageGetMaxOffset(page);
		xlog = GenericXLogStart(index);
		page = GenericXLogRegisterBuffer(xlog, buffer, 0);
		for (off = FirstOffsetNumber; off <= maxoff; off++)
		{
			BloomAmTuple tuple = BloomAmPageGetTuple(&state, page, off);

			if (!ItemPointerIsValid(&tuple->heapPtr))
				continue;
			if (callback(&tuple->heapPtr, callback_state))
			{
				ItemPointerSetInvalid(&tuple->heapPtr);
				stats->tuples_removed += 1;
				modified = true;
			}
			else
				stats->num_index_tuples += 1;
		}
		if (modified)
			GenericXLogFinish(xlog);
		else
			GenericXLogAbort(xlog);
		UnlockReleaseBuffer(buffer);
	}
	return stats;
}

IndexBulkDeleteResult *
bloomamvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	if (info->analyze_only)
		return stats;
	if (stats == NULL)
		stats = (IndexBulkDeleteResult *) palloc0(sizeof(IndexBulkDeleteResult));
	stats->num_pages = RelationGetNumberOfBlocks(info->index);
	return stats;
}
