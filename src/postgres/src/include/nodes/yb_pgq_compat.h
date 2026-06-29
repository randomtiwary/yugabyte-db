/* PG15 compatibility macros for SQL/PGQ port from PG19 */
#ifndef YB_PGQ_COMPAT_H
#define YB_PGQ_COMPAT_H

#include "access/htup.h"
#include "access/htup_details.h"
#include "nodes/pg_list.h"
#include "utils/array.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#define foreach_oid(var, lst) \
	Oid var; \
	ListCell *_yb_pgq_cell_##var; \
	for (_yb_pgq_cell_##var = list_head(lst); \
		 _yb_pgq_cell_##var != NULL; \
		 _yb_pgq_cell_##var = lnext(lst, _yb_pgq_cell_##var)) \
		if (((var) = lfirst_oid(_yb_pgq_cell_##var)) || true)

#define foreach_node(type, var, lst) \
	type *var; \
	ListCell *_yb_pgq_cell_##var; \
	for (_yb_pgq_cell_##var = list_head(lst); \
		 _yb_pgq_cell_##var != NULL; \
		 _yb_pgq_cell_##var = lnext(lst, _yb_pgq_cell_##var)) \
		if (((var) = lfirst_node(type, _yb_pgq_cell_##var)) || true)

#define foreach_ptr(type, var, lst) \
	type *var; \
	ListCell *_yb_pgq_cell_##var; \
	for (_yb_pgq_cell_##var = list_head(lst); \
		 _yb_pgq_cell_##var != NULL; \
		 _yb_pgq_cell_##var = lnext(lst, _yb_pgq_cell_##var)) \
		if (((var) = (type *) lfirst(_yb_pgq_cell_##var)) || true)

static inline Datum
SysCacheGetAttrNotNull(int cacheId, HeapTuple tup, AttrNumber attributeNumber)
{
	bool		isnull;
	Datum		datum;

	datum = SysCacheGetAttr(cacheId, tup, attributeNumber, &isnull);
	if (isnull)
		elog(ERROR, "unexpected null in cache lookup");
	return datum;
}

static inline void
deconstruct_array_builtin(ArrayType *array, Oid elmtype,
						  Datum **elemsp, bool **nullsp, int *nelemsp)
{
	int16		typlen;
	bool		typbyval;
	char		typalign;

	get_typlenbyvalalign(elmtype, &typlen, &typbyval, &typalign);
	deconstruct_array(array, elmtype, typlen, typbyval, typalign,
					  elemsp, nullsp, nelemsp);
}

#endif /* YB_PGQ_COMPAT_H */
