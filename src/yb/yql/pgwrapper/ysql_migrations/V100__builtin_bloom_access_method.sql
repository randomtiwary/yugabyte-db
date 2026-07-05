-- Register built-in bloom signature index access method for existing clusters.
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

INSERT INTO pg_catalog.pg_proc (
    oid, proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
    prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
    pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
    proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
) VALUES
    (8501, 'blhandler', 11, 10, 12, 1, 0, 0, '-', 'f', false, false, true, false,
     'v', 's', 1, 0, 325, '2281', NULL, NULL, NULL, NULL, NULL, 'blhandler', NULL, NULL, NULL)
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_am (oid, amname, amhandler, amtype) VALUES
  (8500, 'bloom', 'blhandler', 'i')
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_opfamily (oid, opfmethod, opfname, opfnamespace, opfowner) VALUES
  (8502, 8500, 'integer_ops', 11, 10),
  (8503, 8500, 'text_ops', 11, 10)
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_opclass (
  oid, opcmethod, opcname, opcnamespace, opcowner, opcfamily, opcintype, opcdefault, opckeytype
) VALUES
  (8504, 8500, 'int4_ops', 11, 10, 8502, 23, true, 0),
  (8505, 8500, 'text_ops', 11, 10, 8503, 25, true, 0)
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_amop (
  oid, amopfamily, amoplefttype, amoprighttype, amopstrategy, amoppurpose, amopopr, amopmethod, amopsortfamily
)
SELECT 8506, 8502, 23, 23, 1, 's', o.oid, 8500, 0 FROM pg_catalog.pg_operator o
WHERE o.oprname = '=' AND o.oprleft = 23 AND o.oprright = 23
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_amop (
  oid, amopfamily, amoplefttype, amoprighttype, amopstrategy, amoppurpose, amopopr, amopmethod, amopsortfamily
)
SELECT 8507, 8503, 25, 25, 1, 's', o.oid, 8500, 0 FROM pg_catalog.pg_operator o
WHERE o.oprname = '=' AND o.oprleft = 25 AND o.oprright = 25
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_amproc (
  oid, amprocfamily, amproclefttype, amprocrighttype, amprocnum, amproc
)
SELECT 8508, 8502, 23, 23, 1, p.oid FROM pg_catalog.pg_proc p
WHERE p.proname = 'hashint4' AND p.pronamespace = 11 AND p.proargtypes = '23'
ON CONFLICT DO NOTHING;

INSERT INTO pg_catalog.pg_amproc (
  oid, amprocfamily, amproclefttype, amprocrighttype, amprocnum, amproc
)
SELECT 8509, 8503, 25, 25, 1, p.oid FROM pg_catalog.pg_proc p
WHERE p.proname = 'hashtext' AND p.pronamespace = 11 AND p.proargtypes = '25'
ON CONFLICT DO NOTHING;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_depend WHERE refclassid = 1255 AND refobjid = 8501
  ) THEN
    INSERT INTO pg_catalog.pg_depend (
      classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
    ) VALUES
      (0, 0, 0, 1255, 8501, 0, 'p'),
      (0, 0, 0, 2601, 8500, 0, 'p'),
      (0, 0, 0, 2612, 8502, 0, 'p'),
      (0, 0, 0, 2612, 8503, 0, 'p'),
      (0, 0, 0, 2616, 8504, 0, 'p'),
      (0, 0, 0, 2616, 8505, 0, 'p');
  END IF;
END $$;
