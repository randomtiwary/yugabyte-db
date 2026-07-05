--
-- Yugabyte-owned tests for the built-in bloom signature index access method (not contrib).
-- Bloom uses local storage, so indexes are created on temporary tables.
--

SET enable_seqscan = off;
SET enable_bitmapscan = on;
SET enable_indexscan = on;

--
-- Index creation
--
CREATE TEMP TABLE bloom_tst (
    i int4,
    t text,
    j int4
);

INSERT INTO bloom_tst
SELECT i % 10, substr(md5(i::text), 1, 1), i % 7
FROM generate_series(1, 2000) i;

CREATE INDEX bloomidx ON bloom_tst USING bloom (i, t) WITH (col1 = 3);
ALTER INDEX bloomidx SET (length = 80);

SELECT am.amname, opc.opcname
FROM pg_class c
JOIN pg_am am ON am.oid = c.relam
JOIN pg_index i ON i.indexrelid = c.oid
JOIN pg_opclass opc ON opc.oid = ANY (i.indclass)
WHERE c.relname = 'bloomidx'
ORDER BY opc.opcname;

--
-- Query plans use the bloom index (bitmap scan with recheck)
--
EXPLAIN (COSTS OFF) SELECT count(*) FROM bloom_tst WHERE i = 7;
EXPLAIN (COSTS OFF) SELECT count(*) FROM bloom_tst WHERE t = '5';
EXPLAIN (COSTS OFF) SELECT count(*) FROM bloom_tst WHERE i = 7 AND t = '5';

SELECT count(*) FROM bloom_tst WHERE i = 7;
SELECT count(*) FROM bloom_tst WHERE t = '5';
SELECT count(*) FROM bloom_tst WHERE i = 7 AND t = '5';

--
-- False positives are filtered by heap recheck
-- Use a tiny signature so the index is likelier to return spurious TIDs.
-- Result counts must still match seqscan (recheck removes false positives).
--
CREATE TEMP TABLE bloom_fp (
    id int,
    a int4,
    b text
);

INSERT INTO bloom_fp
SELECT g, g % 50, substr(md5(g::text), 1, 1)
FROM generate_series(1, 1000) g;

CREATE INDEX bloom_fp_idx ON bloom_fp USING bloom (a, b)
    WITH (length = 16, col1 = 1, col2 = 1);

SET enable_seqscan = on;
SELECT count(*) AS seq_count FROM bloom_fp WHERE a = 3 AND b = 'c';

SET enable_seqscan = off;
EXPLAIN (COSTS OFF) SELECT count(*) FROM bloom_fp WHERE a = 3 AND b = 'c';
SELECT count(*) AS idx_count FROM bloom_fp WHERE a = 3 AND b = 'c';

-- Spot-check row identity agrees with a filter that seqscan would apply.
SELECT id FROM bloom_fp WHERE a = 3 AND b = 'c' ORDER BY id;

--
-- Multi-column filtering
--
CREATE TEMP TABLE bloom_mc (
    c1 int4,
    c2 text,
    c3 int4
);

INSERT INTO bloom_mc VALUES
    (1, 'a', 10),
    (1, 'b', 20),
    (2, 'a', 10),
    (2, 'b', 20),
    (3, 'c', 30);

CREATE INDEX bloom_mc_idx ON bloom_mc USING bloom (c1, c2, c3);

EXPLAIN (COSTS OFF)
SELECT * FROM bloom_mc WHERE c1 = 1 AND c2 = 'a' AND c3 = 10;
SELECT * FROM bloom_mc WHERE c1 = 1 AND c2 = 'a' AND c3 = 10;

EXPLAIN (COSTS OFF)
SELECT * FROM bloom_mc WHERE c1 = 2 AND c2 = 'b';
SELECT * FROM bloom_mc WHERE c1 = 2 AND c2 = 'b' ORDER BY c3;

EXPLAIN (COSTS OFF)
SELECT count(*) FROM bloom_mc WHERE c2 = 'a';
SELECT count(*) FROM bloom_mc WHERE c2 = 'a';

--
-- VACUUM / REINDEX must not corrupt the index
--
DELETE FROM bloom_tst WHERE i > 1 OR t = '5';
VACUUM bloom_tst;
INSERT INTO bloom_tst
SELECT i % 10, substr(md5(i::text), 1, 1), i % 7
FROM generate_series(1, 2000) i;

SELECT count(*) FROM bloom_tst WHERE i = 7;
SELECT count(*) FROM bloom_tst WHERE t = '5';
SELECT count(*) FROM bloom_tst WHERE i = 7 AND t = '5';

REINDEX INDEX bloomidx;

EXPLAIN (COSTS OFF) SELECT count(*) FROM bloom_tst WHERE i = 7 AND t = '5';
SELECT count(*) FROM bloom_tst WHERE i = 7;
SELECT count(*) FROM bloom_tst WHERE t = '5';
SELECT count(*) FROM bloom_tst WHERE i = 7 AND t = '5';

VACUUM FULL bloom_tst;

SELECT count(*) FROM bloom_tst WHERE i = 7;
SELECT count(*) FROM bloom_tst WHERE t = '5';
SELECT count(*) FROM bloom_tst WHERE i = 7 AND t = '5';

-- Validate opclasses
SELECT opcname, amvalidate(opc.oid)
FROM pg_opclass opc
JOIN pg_am am ON am.oid = opc.opcmethod
WHERE amname = 'bloom'
ORDER BY 1;

-- Reloptions bounds
\set VERBOSITY terse
CREATE INDEX bloom_bad ON bloom_tst USING bloom (i, t) WITH (length = 0);
CREATE INDEX bloom_bad ON bloom_tst USING bloom (i, t) WITH (col1 = 0);
\set VERBOSITY default

--
-- Not supported on YB storage relations
--
CREATE TABLE bloom_perm (i int4, t text);
CREATE INDEX ON bloom_perm USING bloom (i, t);
DROP TABLE bloom_perm;

DISCARD TEMP;
RESET enable_seqscan;
RESET enable_bitmapscan;
RESET enable_indexscan;
