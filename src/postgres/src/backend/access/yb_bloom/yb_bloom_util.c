/*--------------------------------------------------------------------------
 *
 * yb_bloom_util.c
 *	  Utility routines for yb_bloom signatures and reloptions.
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
 *			src/backend/access/yb_bloom/yb_bloom_util.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/reloptions.h"
#include "access/yb_bloom.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/typcache.h"

#define GETWORD(x, i) (*((YbBloomSigWord *) (x) + ((i) / YBBLOOM_SIGNWORDBITS)))
#define SETBIT(x, i) GETWORD(x, i) |= ((YbBloomSigWord) 1 << ((i) % YBBLOOM_SIGNWORDBITS))
#define GETBIT(x, i) ((GETWORD(x, i) >> ((i) % YBBLOOM_SIGNWORDBITS)) & 1)

/*
 * Reloption registration state.  ybbloom_init_reloptions() assigns
 * ybbloom_relopt_kind via add_reloption_kind() once, fills
 * ybbloom_relopt_tab with parsers for "length" and "colN", and sets
 * ybbloom_relopts_ready so later CREATE/ALTER INDEX option parsing reuses
 * the same kind and table.  Never reset after initialization.
 */
static relopt_kind ybbloom_relopt_kind = (relopt_kind) 0;
static relopt_parse_elt ybbloom_relopt_tab[INDEX_MAX_KEYS + 1];
static bool ybbloom_relopts_ready = false;

/*
 * Deterministic RNG used while setting signature bits in YbBloomSignValue.
 * ybbloom_srand() seeds it from the column hash; ybbloom_rand() advances it
 * with a libc-style LCG so the same value always sets the same bit positions.
 */
static uint32 ybbloom_rand_state = 1;

static int32
ybbloom_rand(void)
{
	ybbloom_rand_state = ybbloom_rand_state * 1103515245 + 12345;
	return (int32) ((ybbloom_rand_state >> 16) & 0x7fff);
}

static void
ybbloom_srand(uint32 seed)
{
	ybbloom_rand_state = seed ? seed : 1;
}

static void
ybbloom_init_reloptions(void)
{
	int			i;
	char		buf[16];

	if (ybbloom_relopts_ready)
		return;
	ybbloom_relopt_kind = add_reloption_kind();
	add_int_reloption(ybbloom_relopt_kind, "length",
					  "Length of signature in bits",
					  YBBLOOM_DEFAULT_LENGTH, 1, YBBLOOM_MAX_LENGTH,
					  AccessExclusiveLock);
	ybbloom_relopt_tab[0].optname = "length";
	ybbloom_relopt_tab[0].opttype = RELOPT_TYPE_INT;
	ybbloom_relopt_tab[0].offset = offsetof(YbBloomOptions, bloomLength);
	for (i = 0; i < INDEX_MAX_KEYS; i++)
	{
		snprintf(buf, sizeof(buf), "col%d", i + 1);
		add_int_reloption(ybbloom_relopt_kind, buf,
						  "Number of signature bits set per column",
						  YBBLOOM_DEFAULT_BITS, 1, YBBLOOM_MAX_BITS,
						  AccessExclusiveLock);
		ybbloom_relopt_tab[i + 1].optname =
			MemoryContextStrdup(TopMemoryContext, buf);
		ybbloom_relopt_tab[i + 1].opttype = RELOPT_TYPE_INT;
		ybbloom_relopt_tab[i + 1].offset =
			offsetof(YbBloomOptions, bitSize[0]) + sizeof(int) * i;
	}
	ybbloom_relopts_ready = true;
}

bytea *
ybbloomoptions(Datum reloptions, bool validate)
{
	YbBloomOptions *opts;

	ybbloom_init_reloptions();
	opts = (YbBloomOptions *) build_reloptions(reloptions, validate,
											   ybbloom_relopt_kind,
											   sizeof(YbBloomOptions),
											   ybbloom_relopt_tab,
											   lengthof(ybbloom_relopt_tab));
	if (opts)
		opts->bloomLength =
			(opts->bloomLength + YBBLOOM_SIGNWORDBITS - 1) / YBBLOOM_SIGNWORDBITS;
	return (bytea *) opts;
}

void
YbBloomInitState(YbBloomState *state, Relation index)
{
	int			i;
	YbBloomOptions *opts = (YbBloomOptions *) index->rd_options;

	state->nColumns = index->rd_att->natts;
	for (i = 0; i < state->nColumns; i++)
	{
		fmgr_info_copy(&state->hashFn[i],
					   index_getprocinfo(index, i + 1, YBBLOOM_HASH_PROC),
					   CurrentMemoryContext);
		state->collations[i] = index->rd_indcollation[i];
	}
	if (opts)
		memcpy(&state->opts, opts, sizeof(YbBloomOptions));
	else
	{
		memset(&state->opts, 0, sizeof(YbBloomOptions));
		state->opts.bloomLength =
			(YBBLOOM_DEFAULT_LENGTH + YBBLOOM_SIGNWORDBITS - 1) /
			YBBLOOM_SIGNWORDBITS;
		for (i = 0; i < INDEX_MAX_KEYS; i++)
			state->opts.bitSize[i] = YBBLOOM_DEFAULT_BITS;
		SET_VARSIZE(&state->opts, sizeof(YbBloomOptions));
	}
}

void
YbBloomSignValue(YbBloomState *state, YbBloomSigWord *sign, Datum value, int attno)
{
	uint32		hash;
	int			j;
	int			sigBits = state->opts.bloomLength * YBBLOOM_SIGNWORDBITS;

	hash = DatumGetUInt32(FunctionCall1Coll(&state->hashFn[attno],
											state->collations[attno], value));
	ybbloom_srand(hash);
	for (j = 0; j < state->opts.bitSize[attno]; j++)
		SETBIT(sign, ybbloom_rand() % sigBits);
}

bytea *
YbBloomFormSignature(YbBloomState *state, Datum *values, bool *isnull)
{
	int			nwords = state->opts.bloomLength;
	Size		nbytes = nwords * sizeof(YbBloomSigWord);
	bytea	   *result = (bytea *) palloc0(VARHDRSZ + nbytes);
	YbBloomSigWord *sign;
	int			i;

	SET_VARSIZE(result, VARHDRSZ + nbytes);
	sign = (YbBloomSigWord *) VARDATA(result);
	for (i = 0; i < state->nColumns; i++)
		if (!isnull[i])
			YbBloomSignValue(state, sign, values[i], i);
	return result;
}

bool
YbBloomSignatureMatch(bytea *stored, YbBloomSigWord *query, int nwords)
{
	YbBloomSigWord *sign;
	int			i;

	if (VARSIZE_ANY_EXHDR(stored) < nwords * sizeof(YbBloomSigWord))
		return false;
	sign = (YbBloomSigWord *) VARDATA_ANY(stored);
	for (i = 0; i < nwords; i++)
		if ((sign[i] & query[i]) != query[i])
			return false;
	return true;
}
