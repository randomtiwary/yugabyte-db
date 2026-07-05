#include "postgres.h"
#include "access/yb_bloom.h"
#include "pg_yb_utils.h"

IndexBulkDeleteResult *
ybbloombulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				  IndexBulkDeleteCallback callback, void *callback_state)
{
	YBC_LOG_WARNING("Unexpected bulk delete of yb_bloom index via vacuum");
	return stats;
}

IndexBulkDeleteResult *
ybbloomvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	YBC_LOG_WARNING("Unexpected yb_bloom index cleanup via vacuum");
	return stats;
}
