SET enable_seqscan = off;
SET enable_bitmapscan = off;
SET enable_indexscan = on;

-- Feature is off by default.
CREATE TABLE bloom_flag_tst (i int4);
CREATE INDEX bloom_flag_idx ON bloom_flag_tst USING bloom (i); -- error
SET yb_enable_bloom_index = on;

CREATE TABLE bloom_tst (i int4, t text, j int4);
INSERT INTO bloom_tst SELECT i % 10, substr(md5(i::text), 1, 1), i % 7
FROM generate_series(1, 2000) i;
CREATE INDEX bloomidx ON bloom_tst USING bloom (i, t) WITH (col1 = 3);

SELECT am.amname FROM pg_class c JOIN pg_am am ON am.oid = c.relam
WHERE c.relname = 'bloomidx';

EXPLAIN (COSTS OFF) SELECT count(*) FROM bloom_tst WHERE i = 7 AND t = '5';
SELECT count(*) FROM bloom_tst WHERE i = 7;
SELECT count(*) FROM bloom_tst WHERE t = '5';
SELECT count(*) FROM bloom_tst WHERE i = 7 AND t = '5';

CREATE TABLE bloom_mc (c1 int4, c2 text, c3 int4);
INSERT INTO bloom_mc VALUES (1,'a',10),(1,'b',20),(2,'a',10),(2,'b',20),(3,'c',30);
CREATE INDEX bloom_mc_idx ON bloom_mc USING bloom (c1, c2, c3);
SELECT * FROM bloom_mc WHERE c1 = 1 AND c2 = 'a' AND c3 = 10;
SELECT count(*) FROM bloom_mc WHERE c2 = 'a';

DROP INDEX bloomidx;
CREATE INDEX bloomidx ON bloom_tst USING bloom (i, t) WITH (col1 = 3);
SELECT count(*) FROM bloom_tst WHERE i = 7 AND t = '5';

SELECT opcname, amvalidate(opc.oid) FROM pg_opclass opc
JOIN pg_am am ON am.oid = opc.opcmethod WHERE amname = 'bloom' ORDER BY 1;

DROP TABLE bloom_tst, bloom_mc, bloom_flag_tst;
RESET yb_enable_bloom_index;
RESET enable_seqscan; RESET enable_bitmapscan; RESET enable_indexscan;
