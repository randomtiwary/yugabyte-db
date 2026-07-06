/*--------------------------------------------------------------------------
 *
 * yb_bloom_validate.c
 *	  Operator class validation for the yb_bloom access method.
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
 *			src/backend/access/yb_bloom/yb_bloom_validate.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/amvalidate.h"
#include "access/htup_details.h"
#include "access/yb_bloom.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_amproc.h"
#include "catalog/pg_opclass.h"
#include "utils/syscache.h"

bool
ybbloomvalidate(Oid opclassoid)
{
	bool		result = true;
	HeapTuple	classtup = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclassoid));
	Form_pg_opclass classform;
	Oid			opfamilyoid;
	Oid			opcintype;
	CatCList   *proclist;
	int			i;
	bool		has_hash = false;

	if (!HeapTupleIsValid(classtup))
		elog(ERROR, "cache lookup failed for operator class %u", opclassoid);

	classform = (Form_pg_opclass) GETSTRUCT(classtup);
	opfamilyoid = classform->opcfamily;
	opcintype = classform->opcintype;
	ReleaseSysCache(classtup);
	proclist = SearchSysCacheList1(AMPROCNUM, ObjectIdGetDatum(opfamilyoid));
	for (i = 0; i < proclist->n_members; i++)
	{
		Form_pg_amproc procform =
			(Form_pg_amproc) GETSTRUCT(&proclist->members[i]->tuple);
		if (procform->amproclefttype == opcintype &&
			procform->amprocnum == YBBLOOM_HASH_PROC)
			has_hash = true;
	}
	ReleaseCatCacheList(proclist);

	if (!has_hash)
	{
		ereport(INFO, (errmsg("yb_bloom operator class %u lacks hash support",
							  opclassoid)));
		result = false;
	}
	return result;
}
