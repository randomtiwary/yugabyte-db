/*--------------------------------------------------------------------------
 *
 * bloom_scan.c
 *	  Bitmap scan support for the built-in bloom index AM.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/access/bloom/bloom_scan.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/bloomam.h"
#include "access/relscan.h"
#include "miscadmin.h"
#include "storage/bufmgr.h"
#include "utils/memutils.h"

#define GETWORD(x, i) (*((BloomSigWord *) (x) + ((i) / BLOOMAM_SIGNWORDBITS)))
#define GETBIT(x, i) \
	((GETWORD(x, i) >> ((i) % BLOOMAM_SIGNWORDBITS)) & 1)

static bool
bloomam_sign_match(BloomSigWord *pageSign, BloomSigWord *querySign, int nwords)
{
	int			i;

	for (i = 0; i < nwords; i++)
	{
		if ((pageSign[i] & querySign[i]) != querySign[i])
			return false;
	}
	return true;
}

IndexScanDesc
bloomambeginscan(Relation r, int nkeys, int norderbys)
{
	IndexScanDesc scan;
	BloomAmScanOpaque so;

	scan = RelationGetIndexScan(r, nkeys, norderbys);
	so = (BloomAmScanOpaque) palloc0(sizeof(BloomAmScanOpaqueData));
	BloomAmInitState(&so->state, r);
	so->sign = palloc0(sizeof(BloomSigWord) * so->state.opts.bloomLength);
	scan->opaque = so;
	return scan;
}

void
bloomamrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
			  ScanKey orderbys, int norderbys)
{
	BloomAmScanOpaque so = (BloomAmScanOpaque) scan->opaque;
	int			i;

	memset(so->sign, 0, sizeof(BloomSigWord) * so->state.opts.bloomLength);
	if (scankey && scan->numberOfKeys > 0)
		memmove(scan->keyData, scankey, scan->numberOfKeys * sizeof(ScanKeyData));

	for (i = 0; i < scan->numberOfKeys; i++)
	{
		ScanKey		key = &scan->keyData[i];
		int			attno = key->sk_attno - 1;

		if (key->sk_flags & SK_ISNULL)
			continue;
		if (key->sk_strategy != BLOOMAM_EQUAL_STRATEGY)
			elog(ERROR, "unsupported bloom strategy %d", key->sk_strategy);
		BloomAmSignValue(&so->state, so->sign, key->sk_argument, attno);
	}
}

int64
bloomamgetbitmap(IndexScanDesc scan, TIDBitmap *tbm)
{
	BloomAmScanOpaque so = (BloomAmScanOpaque) scan->opaque;
	int64		ntids = 0;
	BlockNumber nblocks;
	BlockNumber blkno;
	Buffer		buffer;
	Page		page;
	OffsetNumber maxoff;
	OffsetNumber off;
	BloomAmTuple tuple;
	int			nwords = so->state.opts.bloomLength;

	nblocks = RelationGetNumberOfBlocks(scan->indexRelation);
	for (blkno = BLOOMAM_HEAD_BLKNO; blkno < nblocks; blkno++)
	{
		CHECK_FOR_INTERRUPTS();
		buffer = ReadBuffer(scan->indexRelation, blkno);
		LockBuffer(buffer, BUFFER_LOCK_SHARE);
		page = BufferGetPage(buffer);
		if (PageIsNew(page) || BloomAmPageIsMeta(page) || BloomAmPageIsDeleted(page))
		{
			UnlockReleaseBuffer(buffer);
			continue;
		}
		maxoff = BloomAmPageGetMaxOffset(page);
		for (off = FirstOffsetNumber; off <= maxoff; off++)
		{
			tuple = BloomAmPageGetTuple(&so->state, page, off);
			if (ItemPointerIsValid(&tuple->heapPtr) &&
				bloomam_sign_match(tuple->sign, so->sign, nwords))
			{
				tbm_add_tuples(tbm, &tuple->heapPtr, 1, false);
				ntids++;
			}
		}
		UnlockReleaseBuffer(buffer);
	}
	return ntids;
}

void
bloomamendscan(IndexScanDesc scan)
{
	BloomAmScanOpaque so = (BloomAmScanOpaque) scan->opaque;

	if (so->sign)
		pfree(so->sign);
	pfree(so);
	scan->opaque = NULL;
}
