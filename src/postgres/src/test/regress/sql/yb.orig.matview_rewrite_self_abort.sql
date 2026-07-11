-- Reproduction: "current transaction is expired or aborted" / "Unknown transaction"
-- after CREATE MATERIALIZED VIEW + REFRESH of that same view in one open transaction.
--
-- Observed failure mode does NOT require ROLLBACK or ROLLBACK TO SAVEPOINT. The crash
-- sequence issued SAVEPOINT statements, then CREATE MV + REFRESH, then later SELECTs
-- failed while the transaction was still open (no rollback first).
--
-- Why this is uncommon:
-- 1. Unusual pattern: CREATE a matview and REFRESH that same matview in one open
--    transaction. REFRESH rewrites DocDB (create new table + mark old for drop).
-- 2. Racey timing around DDL verification / DocDB cleanup of the rewritten tables
--    while the client transaction is still running. Related sequences often pass
--    because the race does not always fire.
--
-- Stress:
--   ./yb_build.sh release --java-test 'org.yb.pgsql.TestMatviewRewriteSelfAbort' -n 50 --tp 1

-- ---------------------------------------------------------------------------
-- Primary repro (crash-log shape): CREATE + REFRESH, then more statements.
-- No ROLLBACK / ROLLBACK TO SAVEPOINT before the critical reads.
-- ---------------------------------------------------------------------------
CREATE TABLE mv_rewrite_base (
    emp_id INT PRIMARY KEY,
    name TEXT,
    salary NUMERIC
);
INSERT INTO mv_rewrite_base
SELECT i, 'name_' || i, i * 1.5 FROM generate_series(1, 50) i;

BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SAVEPOINT sp_0;
DELETE FROM mv_rewrite_base WHERE emp_id = (
    SELECT emp_id FROM mv_rewrite_base WHERE emp_id IS NOT NULL LIMIT 1);
SAVEPOINT sp_1;
CREATE MATERIALIZED VIEW mv_rewrite_mv AS SELECT * FROM mv_rewrite_base;
CREATE TEMP TABLE mv_rewrite_temp AS SELECT * FROM mv_rewrite_base;
REFRESH MATERIALIZED VIEW mv_rewrite_mv;
-- Failure mode: one of these (or similar later statements) returns
-- "Unknown transaction" / "current transaction is expired or aborted".
SELECT COUNT(*) FROM mv_rewrite_base;
SELECT COUNT(*) FROM mv_rewrite_mv;
SELECT matviewname FROM pg_matviews WHERE matviewname = 'mv_rewrite_mv';
SELECT COUNT(*) FROM mv_rewrite_temp;
-- Extra reads to widen the race window slightly (catalog + user tables).
SELECT COUNT(*) FROM mv_rewrite_base;
SELECT COUNT(*) FROM mv_rewrite_mv;
COMMIT;

DROP MATERIALIZED VIEW IF EXISTS mv_rewrite_mv;
DROP TABLE IF EXISTS mv_rewrite_base;

-- ---------------------------------------------------------------------------
-- Same rewrite path without SAVEPOINT statements (check whether SAVEPOINT
-- itself is load-bearing vs incidental to the randomness test).
-- ---------------------------------------------------------------------------
CREATE TABLE mv_rewrite_base_nosavepoint (
    emp_id INT PRIMARY KEY,
    name TEXT,
    salary NUMERIC
);
INSERT INTO mv_rewrite_base_nosavepoint
SELECT i, 'name_' || i, i * 1.5 FROM generate_series(1, 50) i;

BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
DELETE FROM mv_rewrite_base_nosavepoint WHERE emp_id = (
    SELECT emp_id FROM mv_rewrite_base_nosavepoint WHERE emp_id IS NOT NULL LIMIT 1);
CREATE MATERIALIZED VIEW mv_rewrite_mv_nosavepoint AS
  SELECT * FROM mv_rewrite_base_nosavepoint;
CREATE TEMP TABLE mv_rewrite_temp_nosavepoint AS
  SELECT * FROM mv_rewrite_base_nosavepoint;
REFRESH MATERIALIZED VIEW mv_rewrite_mv_nosavepoint;
SELECT COUNT(*) FROM mv_rewrite_base_nosavepoint;
SELECT COUNT(*) FROM mv_rewrite_mv_nosavepoint;
SELECT COUNT(*) FROM mv_rewrite_temp_nosavepoint;
SELECT COUNT(*) FROM mv_rewrite_base_nosavepoint;
COMMIT;

DROP MATERIALIZED VIEW IF EXISTS mv_rewrite_mv_nosavepoint;
DROP TABLE IF EXISTS mv_rewrite_base_nosavepoint;
