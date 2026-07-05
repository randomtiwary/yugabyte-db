-- Register built-in bloom AM (yb_bloom DocDB implementation) for existing clusters.
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl)
VALUES (8501, 'ybbloomhandler', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
     'v', 's', 1, 0, 325, '2281', NULL, NULL, NULL, NULL, NULL, 'ybbloomhandler', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_am (oid, amname, amhandler, amtype)
VALUES (8500, 'bloom', 'ybbloomhandler', 'i') ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_opfamily (oid, opfmethod, opfname, opfnamespace, opfowner) VALUES
  (8502, 8500, 'integer_ops', 11, 10),
  (8503, 8500, 'text_ops', 11, 10) ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_opclass (
  oid, opcmethod, opcname, opcnamespace, opcowner, opcfamily, opcintype, opcdefault, opckeytype)
VALUES
  (8504, 8500, 'int4_ops', 11, 10, 8502, 23, true, 17),
  (8505, 8500, 'text_ops', 11, 10, 8503, 25, true, 17) ON CONFLICT DO NOTHING;
