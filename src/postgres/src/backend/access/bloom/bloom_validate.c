/*--------------------------------------------------------------------------
 *
 * bloom_validate.c
 *	  Opclass validation for the built-in bloom index AM.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * IDENTIFICATION
 *	  src/backend/access/bloom/bloom_validate.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/amvalidate.h"
#include "access/bloomam.h"
#include "access/htup_details.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_amproc.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#include "utils/regproc.h"
#include "utils/syscache.h"

bool
bloomamvalidate(Oid opclassoid)
{
	bool		result = true;
	HeapTuple	classtup;
	Form_pg_opclass classform;
	Oid			opfamilyoid;
	Oid			opcintype;
	CatCList   *proclist;
	CatCList   *oprlist;
	int			i;
	bool		hashproc = false;

	classtup = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclassoid));
	if (!HeapTupleIsValid(classtup))
		elog(ERROR, "cache lookup failed for operator class %u", opclassoid);
	classform = (Form_pg_opclass) GETSTRUCT(classtup);
	opfamilyoid = classform->opcfamily;
	opcintype = classform->opcintype;
	ReleaseSysCache(classtup);

	proclist = SearchSysCacheList1(AMPROCNUM, ObjectIdGetDatum(opfamilyoid));
	for (i = 0; i < proclist->n_members; i++)
	{
		HeapTuple	proctup = &proclist->members[i]->tuple;
		Form_pg_amproc procform = (Form_pg_amproc) GETSTRUCT(proctup);

		if (procform->amproclefttype != opcintype ||
			procform->amprocrighttype != opcintype)
			continue;
		if (procform->amprocnum == BLOOMAM_HASH_PROC)
			hashproc = true;
	}
	ReleaseCatCacheList(proclist);
	if (!hashproc)
	{
		ereport(INFO, (errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					   errmsg("operator class %u lacks hash support function",
							  opclassoid)));
		result = false;
	}

	oprlist = SearchSysCacheList1(AMOPSTRATEGY, ObjectIdGetDatum(opfamilyoid));
	for (i = 0; i < oprlist->n_members; i++)
	{
		HeapTuple	oprtup = &oprlist->members[i]->tuple;
		Form_pg_amop oprform = (Form_pg_amop) GETSTRUCT(oprtup);

		if (oprform->amoplefttype != opcintype ||
			oprform->amoprighttype != opcintype)
			continue;
		if (oprform->amopstrategy != BLOOMAM_EQUAL_STRATEGY)
		{
			ereport(INFO, (errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						   errmsg("bloom opfamily contains unexpected strategy %d",
								  oprform->amopstrategy)));
			result = false;
		}
	}
	ReleaseCatCacheList(oprlist);
	return result;
}
