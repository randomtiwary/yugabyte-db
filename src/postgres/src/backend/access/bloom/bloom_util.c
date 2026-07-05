/*--------------------------------------------------------------------------
 *
 * bloom_util.c
 *	  Signature helpers and reloptions for the built-in bloom index AM.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/access/bloom/bloom_util.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/bloomam.h"
#include "access/generic_xlog.h"
#include "access/reloptions.h"
#include "storage/bufmgr.h"
#include "utils/memutils.h"

#define GETWORD(x, i) (*((BloomSigWord *) (x) + ((i) / BLOOMAM_SIGNWORDBITS)))
#define SETBIT(x, i) \
	GETWORD(x, i) |= ((BloomSigWord) 1 << ((i) % BLOOMAM_SIGNWORDBITS))
#define GETBIT(x, i) \
	((GETWORD(x, i) >> ((i) % BLOOMAM_SIGNWORDBITS)) & 1)

static relopt_kind bloomam_relopt_kind = (relopt_kind) 0;
static relopt_parse_elt bloomam_relopt_tab[INDEX_MAX_KEYS + 1];
static bool bloomam_relopts_ready = false;

static int32 bloomam_rand(void);
static void bloomam_srand(uint32 seed);
static uint32 bloomam_rand_state = 1;

static void
bloomam_init_reloptions(void)
{
	int			i;
	char		buf[16];

	if (bloomam_relopts_ready)
		return;

	bloomam_relopt_kind = add_reloption_kind();
	add_int_reloption(bloomam_relopt_kind, "length",
					  "Length of signature in bits",
					  BLOOMAM_DEFAULT_LENGTH, 1, BLOOMAM_MAX_LENGTH,
					  AccessExclusiveLock);
	bloomam_relopt_tab[0].optname = "length";
	bloomam_relopt_tab[0].opttype = RELOPT_TYPE_INT;
	bloomam_relopt_tab[0].offset = offsetof(BloomAmOptions, bloomLength);

	for (i = 0; i < INDEX_MAX_KEYS; i++)
	{
		snprintf(buf, sizeof(buf), "col%d", i + 1);
		add_int_reloption(bloomam_relopt_kind, buf,
						  "Number of signature bits set per column",
						  BLOOMAM_DEFAULT_BITS, 1, BLOOMAM_MAX_BITS,
						  AccessExclusiveLock);
		bloomam_relopt_tab[i + 1].optname =
			MemoryContextStrdup(TopMemoryContext, buf);
		bloomam_relopt_tab[i + 1].opttype = RELOPT_TYPE_INT;
		bloomam_relopt_tab[i + 1].offset =
			offsetof(BloomAmOptions, bitSize[0]) + sizeof(int) * i;
	}
	bloomam_relopts_ready = true;
}

bytea *
bloomamoptions(Datum reloptions, bool validate)
{
	BloomAmOptions *opts;

	bloomam_init_reloptions();
	opts = (BloomAmOptions *) build_reloptions(reloptions, validate,
											   bloomam_relopt_kind,
											   sizeof(BloomAmOptions),
											   bloomam_relopt_tab,
											   lengthof(bloomam_relopt_tab));
	if (opts)
		opts->bloomLength =
			(opts->bloomLength + BLOOMAM_SIGNWORDBITS - 1) /
			BLOOMAM_SIGNWORDBITS;
	return (bytea *) opts;
}

void
BloomAmInitState(BloomAmState *state, Relation index)
{
	int			i;
	Buffer		buffer;
	Page		page;
	BloomAmMetaPageData *meta;
	BloomAmOptions *opts;

	state->nColumns = index->rd_att->natts;
	for (i = 0; i < state->nColumns; i++)
	{
		fmgr_info_copy(&state->hashFn[i],
					   index_getprocinfo(index, i + 1, BLOOMAM_HASH_PROC),
					   CurrentMemoryContext);
		state->collations[i] = index->rd_indcollation[i];
	}

	if (!index->rd_amcache)
	{
		opts = MemoryContextAlloc(index->rd_indexcxt, sizeof(BloomAmOptions));
		buffer = ReadBuffer(index, BLOOMAM_METAPAGE_BLKNO);
		LockBuffer(buffer, BUFFER_LOCK_SHARE);
		page = BufferGetPage(buffer);
		if (!BloomAmPageIsMeta(page))
			elog(ERROR, "relation is not a bloom index");
		meta = BloomAmPageGetMeta(page);
		if (meta->magic != BLOOMAM_MAGIC)
			elog(ERROR, "relation is not a bloom index");
		*opts = meta->opts;
		UnlockReleaseBuffer(buffer);
		index->rd_amcache = opts;
	}

	memcpy(&state->opts, index->rd_amcache, sizeof(state->opts));
	state->sizeOfTuple = BLOOMAM_TUPLEHDRSZ +
		sizeof(BloomSigWord) * state->opts.bloomLength;
}

static int32
bloomam_rand(void)
{
	bloomam_rand_state = bloomam_rand_state * 1103515245 + 12345;
	return (int32) ((bloomam_rand_state >> 16) & 0x7fff);
}

static void
bloomam_srand(uint32 seed)
{
	bloomam_rand_state = seed ? seed : 1;
}

void
BloomAmSignValue(BloomAmState *state, BloomSigWord *sign, Datum value, int attno)
{
	uint32		hash;
	int			nBit;
	int			j;
	int			sigBits = state->opts.bloomLength * BLOOMAM_SIGNWORDBITS;

	hash = DatumGetUInt32(FunctionCall1Coll(&state->hashFn[attno],
											state->collations[attno],
											value));
	bloomam_srand(hash ^ (uint32) bloomam_rand());
	for (j = 0; j < state->opts.bitSize[attno]; j++)
	{
		nBit = bloomam_rand() % sigBits;
		SETBIT(sign, nBit);
	}
}

BloomAmTuple
BloomAmFormTuple(BloomAmState *state, ItemPointer iptr,
				 Datum *values, bool *isnull)
{
	BloomAmTuple tuple;
	int			i;

	tuple = palloc0(state->sizeOfTuple);
	tuple->heapPtr = *iptr;
	for (i = 0; i < state->nColumns; i++)
	{
		if (isnull[i])
			continue;
		BloomAmSignValue(state, tuple->sign, values[i], i);
	}
	return tuple;
}

bool
BloomAmPageAddItem(BloomAmState *state, Page page, BloomAmTuple tuple)
{
	OffsetNumber maxoff = BloomAmPageGetMaxOffset(page);
	BloomAmTuple dest;

	if (BloomAmPageGetFreeSpace(state, page) < state->sizeOfTuple)
		return false;
	maxoff++;
	dest = BloomAmPageGetTuple(state, page, maxoff);
	memcpy(dest, tuple, state->sizeOfTuple);
	BloomAmPageGetOpaque(page)->maxoff = maxoff;
	return true;
}
