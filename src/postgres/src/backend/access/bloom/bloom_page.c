/*--------------------------------------------------------------------------
 *
 * bloom_page.c
 *	  Page initialization helpers for the built-in bloom index AM.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/access/bloom/bloom_page.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/bloomam.h"
#include "access/generic_xlog.h"
#include "storage/bufmgr.h"
#include "storage/indexfsm.h"
#include "storage/lmgr.h"
#include "storage/smgr.h"

void
BloomAmInitPage(Page page, uint16 flags)
{
	BloomAmPageOpaque opaque;

	PageInit(page, BLCKSZ, sizeof(BloomAmPageOpaqueData));
	opaque = BloomAmPageGetOpaque(page);
	opaque->maxoff = 0;
	opaque->flags = flags;
	opaque->unused = 0;
	opaque->bloom_page_id = BLOOMAM_PAGE_ID;
}

Buffer
BloomAmNewBuffer(Relation index)
{
	Buffer		buffer;

	buffer = ReadBuffer(index, P_NEW);
	LockBuffer(buffer, BUFFER_LOCK_EXCLUSIVE);
	return buffer;
}

void
BloomAmFillMetapage(Relation index, Page metaPage)
{
	BloomAmOptions *opts;
	BloomAmMetaPageData *meta;
	int			nFree;
	Size		metaBytes;

	opts = (BloomAmOptions *) index->rd_options;
	if (!opts)
	{
		int			i;

		opts = (BloomAmOptions *) palloc0(sizeof(BloomAmOptions));
		opts->bloomLength =
			(BLOOMAM_DEFAULT_LENGTH + BLOOMAM_SIGNWORDBITS - 1) /
			BLOOMAM_SIGNWORDBITS;
		for (i = 0; i < INDEX_MAX_KEYS; i++)
			opts->bitSize[i] = BLOOMAM_DEFAULT_BITS;
		SET_VARSIZE(opts, sizeof(BloomAmOptions));
	}

	BloomAmInitPage(metaPage, BLOOMAM_META);
	meta = BloomAmPageGetMeta(metaPage);
	memset(meta, 0, sizeof(BloomAmMetaPageData));
	meta->magic = BLOOMAM_MAGIC;
	meta->opts = *opts;
	meta->nStart = 0;
	meta->nEnd = 0;

	metaBytes = offsetof(BloomAmMetaPageData, notFullPage);
	nFree = (BLCKSZ - MAXALIGN(SizeOfPageHeaderData) -
			 MAXALIGN(sizeof(BloomAmPageOpaqueData)) - metaBytes) /
		sizeof(BlockNumber);
	if (nFree < 1)
		elog(ERROR, "bloom metapage has no room for free-page cache");

	((PageHeader) metaPage)->pd_lower =
		MAXALIGN(SizeOfPageHeaderData) + metaBytes +
		nFree * sizeof(BlockNumber);
}

void
BloomAmInitMetapage(Relation index)
{
	Buffer		metaBuffer;
	Page		metaPage;
	GenericXLogState *state;

	metaBuffer = BloomAmNewBuffer(index);
	Assert(BufferGetBlockNumber(metaBuffer) == BLOOMAM_METAPAGE_BLKNO);

	state = GenericXLogStart(index);
	metaPage = GenericXLogRegisterBuffer(state, metaBuffer,
										 GENERIC_XLOG_FULL_IMAGE);
	BloomAmFillMetapage(index, metaPage);
	GenericXLogFinish(state);
	UnlockReleaseBuffer(metaBuffer);
}
