-- Verify built-in bloom indexes are gated behind yb_enable_bloom_index
-- (default off).

SHOW yb_enable_bloom_index;

CREATE TABLE bloom_gate_tst (i int4);
INSERT INTO bloom_gate_tst SELECT generate_series(1, 100);

-- CREATE INDEX ... USING bloom fails while the flag is off.
CREATE INDEX bloom_gate_idx ON bloom_gate_tst USING bloom (i);

-- Enable, create the index, then exercise planner/scan gating.
SET yb_enable_bloom_index = on;
CREATE INDEX bloom_gate_idx ON bloom_gate_tst USING bloom (i);

SELECT am.amname FROM pg_class c JOIN pg_am am ON am.oid = c.relam
WHERE c.relname = 'bloom_gate_idx';

-- With the flag off again, the planner prefers a sequential scan.
SET yb_enable_bloom_index = off;
SET enable_seqscan = on;
SET enable_bitmapscan = off;
SET enable_indexscan = on;
EXPLAIN (COSTS OFF) SELECT * FROM bloom_gate_tst WHERE i = 1;
SELECT count(*) FROM bloom_gate_tst WHERE i = 1;

-- Forcing an index scan errors in ybbloombeginscan while disabled.
SET enable_seqscan = off;
SELECT * FROM bloom_gate_tst WHERE i = 1;

-- Re-enabling restores index scans.
SET yb_enable_bloom_index = on;
EXPLAIN (COSTS OFF) SELECT * FROM bloom_gate_tst WHERE i = 1;
SELECT count(*) FROM bloom_gate_tst WHERE i = 1;

-- CREATE INDEX fails again after RESET (default off).
RESET yb_enable_bloom_index;
SHOW yb_enable_bloom_index;
CREATE INDEX bloom_gate_idx2 ON bloom_gate_tst USING bloom (i);

DROP TABLE bloom_gate_tst;
RESET enable_seqscan;
RESET enable_bitmapscan;
RESET enable_indexscan;
