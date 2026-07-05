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
