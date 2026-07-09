# Logical Replication with Transactional DDL

## Goal

Support interleaved DML and DDL within a single transaction block in logical replication
(YSQL replication slots / Virtual WAL + walsender). Today logical replication still assumes
DDL runs in autonomous transactions.

Example:

```sql
BEGIN;
INSERT INTO test_table VALUES (1, 'hello', 10);
ALTER TABLE test_table ADD COLUMN d text;
INSERT INTO test_table VALUES (2, 'world', 20, 'foo');
ALTER TABLE test_table DROP COLUMN c;
INSERT INTO test_table VALUES (3, 'foo', 'bar');
COMMIT;
```

Walsender must resolve relation schema for each INSERT under the historical transaction's
mid-transaction catalog state (including same-txn catalog writes).

## Design Summary

See `/home/personal/Downloads/yb_logs_for_share/TXN_ddl_design.pdf` for the full design.

High-level steps:

1. **Virtual WAL**: Stream `pg_class` and `pg_attribute`. Treat DMLs on those catalog tables
   as DDL operations; synthesize DDL records ordered by `record_time` with user DMLs.
   Deprecate relying on `CHANGE_METADATA_OP` for slots that use this path.
2. **Walsender**: Maintain a fake historical transaction session context:
   - `active_transaction_id`: DocDB txn id of the streamed committed txn
   - `yb_read_time`: `commit_time - 1`
   - `in_txn_limit_ht`: `record_time` of the DML currently being decoded
   Plus a `historical_catalog_read` boolean on perform options.
3. **PgClientSession**: When `historical_catalog_read` is set, do not start a new txn or
   heartbeats; forward the txn context on reads.
4. **Tablet read path**: When `historical_catalog_read` is set:
   - Skip status checks for the (already committed / non-active) historical txn
   - Force `use_ht_file_filter = false` so intents are read even when no active txns exist

Guarantees already present:
- Logical replication only streams committed transactions.
- Intent retention holds catalog intents until the stream acknowledges the txn.

## Feature Flag

`ysql_yb_enable_logical_replication_transactional_ddl` (default: `false`)

All behavioral changes must be gated on this flag so the feature can be rolled out safely.

## Key Decisions

1. **Detect DDL via catalog DMLs** rather than CHANGE_METADATA_OP so DDLs share ordering with
   user DMLs inside the same commit_time via record_time/write_id.
2. **Fake historical txn context** on walsender (not a real open txn) so catalog reads see
   same-txn intents with an `in_txn_limit`.
3. **Gated behind a dedicated flag** independent of `ysql_yb_ddl_transaction_block_enabled`
   (which enables transactional DDL on the primary) so replication consumption can be enabled
   separately.

## Open Questions

1. Deduplicating multiple catalog DMLs for a single DDL statement (correctness not affected;
   can collapse consecutive DDLs on the same table in VWAL).

## PR Plan

### PR 1: Add feature flag for logical replication transactional DDL
- **Files/components affected:** `src/yb/common/common_flags.cc`
- **Dependencies:** None
- **Description:** Introduce `ysql_yb_enable_logical_replication_transactional_ddl`
  (default false). No behavior change.

### PR 2: Stream pg_attribute and synthesize DDL records from catalog DMLs
- **Files/components affected:** `src/yb/cdc/cdcsdk_virtual_wal.{cc,h}`,
  `src/yb/cdc/cdc_service.{cc,h}`, `src/yb/master/xrepl_catalog_manager.cc`
- **Dependencies:** PR 1
- **Description:** When the flag is enabled (and catalog streaming is active for the slot),
  poll `pg_attribute`, convert pg_class/pg_attribute DMLs into DDL records for publication
  tables, and ignore CHANGE_METADATA_OP DDL records from GetChanges.

### PR 3: Classify sys catalog tablet records by op for interleaving
- **Files/components affected:** `src/yb/cdc/cdcsdk_unique_record_id.{cc,h}`,
  `src/yb/cdc/cdcsdk_virtual_wal.cc`
- **Dependencies:** PR 2
- **Description:** Stop treating all sys catalog tablet rows as PUBLICATION_REFRESH so
  catalog DMLs merge with user DMLs by record_time/write_id.

### PR 4: Plumb record_time and DocDB transaction_id to walsender records
- **Files/components affected:** `src/yb/yql/pggate/ybc_pg_typedefs.h`,
  `src/yb/yql/pggate/ybc_pggate.cc`
- **Dependencies:** PR 1
- **Description:** Expose `record_time` and DocDB `transaction_id` on `YbcPgRowMessage`
  so the walsender can set historical session context.

### PR 5: historical_catalog_read on perform path and tablet reads
- **Files/components affected:** `src/yb/tserver/pg_client.proto`,
  `src/yb/tserver/pg_client_session.cc`, tablet/docdb read path
- **Dependencies:** PR 1
- **Description:** Add `historical_catalog_read` to `PgPerformOptionsPB` and wire it so
  reads skip txn heartbeats/status checks and force intent reads (`use_ht_file_filter=false`).

### PR 6: Walsender historical transaction session for catalog visibility
- **Files/components affected:** `src/postgres/src/backend/replication/logical/yb_decode.c`,
  `src/yb/yql/pggate/pg_session.{cc,h}`, `ybc_pggate`, `pg_yb_utils`
- **Dependencies:** PR 4, PR 5
- **Description:** On BEGIN set yb_read_time and active_transaction_id; on DDL invalidate
  relcache; on DML set in_txn_limit to record_time; pass historical_catalog_read on catalog
  reads. Gated by the feature flag.

### PR 7: Tests for transactional DDL logical replication
- **Files/components affected:** `src/yb/integration-tests/cdcsdk_consumption_consistent_changes-test.cc`,
  `java/yb-pgsql/.../TestPgReplicationSlot.java`
- **Dependencies:** PR 2, PR 3, PR 6
- **Description:** VWAL ordering test (ADD/DROP COLUMN in txn block) and end-to-end
  replication slot consumption test.
