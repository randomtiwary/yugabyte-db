#!/usr/bin/env python3
# Copyright (c) YugabyteDB, Inc.
#
# YSQL INSERT benchmark for comparing server allocators (TCMalloc vs jemalloc).
# Drives a real YugabyteDB cluster via ysqlsh (no extra Python DB drivers required).
#
# Example:
#   ./scripts/ysql_insert_allocator_bench.py \
#       --ysqlsh ./bin/ysqlsh --host 127.0.0.1 --port 5433 \
#       --num-inserts 100000 --label tcmalloc
#
# Exit 0 on success; prints a RESULT line for scripting.

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


def run_ysqlsh(
    ysqlsh: str,
    host: str,
    port: int,
    user: str,
    database: str,
    sql: str,
    extra_env: Optional[Dict[str, str]] = None,
) -> Tuple[int, str, str]:
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
    # Avoid pager / prompts.
    env.setdefault("PGPASSWORD", env.get("PGPASSWORD", "yugabyte"))
    cmd = [
        ysqlsh,
        "-h",
        host,
        "-p",
        str(port),
        "-U",
        user,
        "-d",
        database,
        "-v",
        "ON_ERROR_STOP=1",
        "-q",
        "-t",
        "-A",
        "-c",
        sql,
    ]
    proc = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    return proc.returncode, proc.stdout.strip(), proc.stderr.strip()


def run_ysqlsh_file(
    ysqlsh: str,
    host: str,
    port: int,
    user: str,
    database: str,
    path: Path,
) -> Tuple[int, str, str]:
    env = os.environ.copy()
    env.setdefault("PGPASSWORD", env.get("PGPASSWORD", "yugabyte"))
    cmd = [
        ysqlsh,
        "-h",
        host,
        "-p",
        str(port),
        "-U",
        user,
        "-d",
        database,
        "-v",
        "ON_ERROR_STOP=1",
        "-q",
        "-f",
        str(path),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, env=env, check=False)
    return proc.returncode, proc.stdout.strip(), proc.stderr.strip()


def wait_for_ysql(
    ysqlsh: str,
    host: str,
    port: int,
    user: str,
    database: str,
    timeout_sec: float,
) -> None:
    deadline = time.time() + timeout_sec
    last_err = ""
    while time.time() < deadline:
        rc, _, err = run_ysqlsh(ysqlsh, host, port, user, database, "SELECT 1")
        if rc == 0:
            return
        last_err = err
        time.sleep(1.0)
    raise RuntimeError(f"YSQL not ready on {host}:{port} within {timeout_sec}s: {last_err}")


def fetch_prometheus_text(url: str, timeout: float = 5.0) -> str:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return resp.read().decode("utf-8", errors="replace")
    except (urllib.error.URLError, TimeoutError) as exc:
        return f"# metrics_fetch_failed: {exc}"


def parse_metric_gauges(prom_text: str, names: List[str]) -> Dict[str, float]:
    """Best-effort scrape of simple gauge lines (name{...} value or name value)."""
    out: Dict[str, float] = {}
    want = set(names)
    for line in prom_text.splitlines():
        if not line or line.startswith("#"):
            continue
        # metric{labels} value   OR   metric value
        parts = line.split()
        if len(parts) < 2:
            continue
        metric_part = parts[0]
        base = metric_part.split("{", 1)[0]
        if base not in want:
            continue
        try:
            val = float(parts[-1])
        except ValueError:
            continue
        # Keep max if multiple series (e.g. per-server); fine for single-node.
        out[base] = max(out.get(base, val), val)
    return out


ALLOCATOR_METRIC_NAMES = [
    # tcmalloc (existing YB metrics)
    "generic_current_allocated_bytes",
    "generic_heap_size",
    "tcmalloc_pageheap_free_bytes",
    "tcmalloc_pageheap_unmapped_bytes",
    "tcmalloc_current_total_thread_cache_bytes",
    # jemalloc (added with --use-jemalloc builds)
    "jemalloc_allocated_bytes",
    "jemalloc_active_bytes",
    "jemalloc_mapped_bytes",
    "jemalloc_resident_bytes",
    "jemalloc_retained_bytes",
    "jemalloc_metadata_bytes",
]


def collect_allocator_metrics(tserver_web_hosts: List[str]) -> Dict[str, Any]:
    combined: Dict[str, float] = {}
    raw_ok = 0
    for base in tserver_web_hosts:
        url = base.rstrip("/") + "/prometheus-metrics"
        text = fetch_prometheus_text(url)
        if text.startswith("# metrics_fetch_failed"):
            continue
        raw_ok += 1
        gauges = parse_metric_gauges(text, ALLOCATOR_METRIC_NAMES)
        for k, v in gauges.items():
            combined[k] = combined.get(k, 0.0) + v
    return {"sources_ok": raw_ok, "gauges": combined}


def build_insert_sql(
    table: str,
    num_inserts: int,
    payload_bytes: int,
    mode: str,
    batch_size: int,
) -> str:
    """
    mode:
      - generate_series: single multi-row INSERT (fast path, still server-side allocs)
      - batched: many multi-row INSERTs of batch_size (more statement overhead)
      - single_row: N individual INSERTs (max statement overhead / allocator churn)
    """
    # Payload via repeat() keeps SQL size small while allocating on server.
    if mode == "generate_series":
        return f"""
DROP TABLE IF EXISTS {table};
CREATE TABLE {table} (
  id BIGINT PRIMARY KEY,
  payload TEXT NOT NULL
);
INSERT INTO {table} (id, payload)
SELECT g, repeat('x', {payload_bytes})
FROM generate_series(1, {num_inserts}) AS g;
"""
    if mode == "single_row":
        # Use a DO block with a loop so we don't ship a multi-MB SQL file.
        return f"""
DROP TABLE IF EXISTS {table};
CREATE TABLE {table} (
  id BIGINT PRIMARY KEY,
  payload TEXT NOT NULL
);
DO $$
DECLARE
  i BIGINT;
  p TEXT := repeat('x', {payload_bytes});
BEGIN
  FOR i IN 1..{num_inserts} LOOP
    INSERT INTO {table} (id, payload) VALUES (i, p);
  END LOOP;
END $$;
"""
    # batched via generate_series slices
    stmts = [
        f"DROP TABLE IF EXISTS {table};",
        f"""CREATE TABLE {table} (
  id BIGINT PRIMARY KEY,
  payload TEXT NOT NULL
);""",
    ]
    start = 1
    while start <= num_inserts:
        end = min(start + batch_size - 1, num_inserts)
        stmts.append(
            f"""INSERT INTO {table} (id, payload)
SELECT g, repeat('x', {payload_bytes})
FROM generate_series({start}, {end}) AS g;"""
        )
        start = end + 1
    return "\n".join(stmts)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Benchmark real YSQL INSERTs on a YugabyteDB cluster (allocator comparison)."
    )
    ap.add_argument("--ysqlsh", default=os.environ.get("YB_YSQLSH", "ysqlsh"),
                    help="Path to ysqlsh (default: $YB_YSQLSH or ysqlsh on PATH)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5433)
    ap.add_argument("--user", default="yugabyte")
    ap.add_argument("--database", default="yugabyte")
    ap.add_argument("--num-inserts", type=int, default=100_000)
    ap.add_argument("--payload-bytes", type=int, default=256,
                    help="Size of TEXT payload per row (server-side repeat())")
    ap.add_argument(
        "--mode",
        choices=("generate_series", "batched", "single_row"),
        default="generate_series",
        help="INSERT style (default generate_series = one multi-row INSERT)",
    )
    ap.add_argument("--batch-size", type=int, default=1000,
                    help="Rows per INSERT when --mode=batched")
    ap.add_argument("--table", default="alloc_bench_inserts",
                    help="Temporary table name (dropped/recreated each run)")
    ap.add_argument("--label", default="unknown",
                    help="Allocator / build label embedded in RESULT (e.g. tcmalloc, jemalloc)")
    ap.add_argument("--ready-timeout-sec", type=float, default=120.0)
    ap.add_argument(
        "--tserver-web",
        action="append",
        default=[],
        help="TServer web base URL for allocator metrics, e.g. http://127.0.0.1:9000 "
             "(repeatable). If omitted, tries http://127.0.0.1:9000",
    )
    ap.add_argument("--skip-metrics", action="store_true")
    ap.add_argument("--skip-verify", action="store_true")
    args = ap.parse_args()

    ysqlsh = args.ysqlsh
    if not Path(ysqlsh).exists() and ysqlsh == "ysqlsh":
        # Try repo-relative bin/ysqlsh when run from a checkout.
        repo_guess = Path(__file__).resolve().parent.parent / "bin" / "ysqlsh"
        if repo_guess.exists():
            ysqlsh = str(repo_guess)

    if not Path(ysqlsh).exists():
        # Still allow PATH lookup.
        from shutil import which

        resolved = which(ysqlsh)
        if not resolved:
            print(f"ERROR: ysqlsh not found: {args.ysqlsh}", file=sys.stderr)
            return 2
        ysqlsh = resolved

    print(
        f"ysql_insert_allocator_bench starting label={args.label!r} "
        f"target={args.host}:{args.port} db={args.database} "
        f"num_inserts={args.num_inserts} payload_bytes={args.payload_bytes} "
        f"mode={args.mode} ysqlsh={ysqlsh}",
        flush=True,
    )

    try:
        wait_for_ysql(
            ysqlsh, args.host, args.port, args.user, args.database, args.ready_timeout_sec
        )
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    sql = build_insert_sql(
        args.table, args.num_inserts, args.payload_bytes, args.mode, args.batch_size
    )

    metrics_before: Dict[str, Any] = {}
    web_hosts = args.tserver_web or ["http://127.0.0.1:9000"]
    if not args.skip_metrics:
        metrics_before = collect_allocator_metrics(web_hosts)

    with tempfile.NamedTemporaryFile("w", suffix=".sql", delete=False) as tf:
        tf.write(sql)
        sql_path = Path(tf.name)

    t0 = time.perf_counter()
    rc, out, err = run_ysqlsh_file(
        ysqlsh, args.host, args.port, args.user, args.database, sql_path
    )
    t1 = time.perf_counter()
    try:
        sql_path.unlink(missing_ok=True)
    except OSError:
        pass

    if rc != 0:
        print(f"ERROR: insert workload failed (rc={rc}): {err or out}", file=sys.stderr)
        return 1

    elapsed = t1 - t0
    ips = args.num_inserts / elapsed if elapsed > 0 else 0.0

    row_count = -1
    if not args.skip_verify:
        vrc, vout, verr = run_ysqlsh(
            ysqlsh,
            args.host,
            args.port,
            args.user,
            args.database,
            f"SELECT count(*) FROM {args.table}",
        )
        if vrc != 0:
            print(f"ERROR: verify count failed: {verr}", file=sys.stderr)
            return 1
        try:
            row_count = int(vout.splitlines()[-1].strip())
        except (ValueError, IndexError):
            print(f"ERROR: could not parse count output: {vout!r}", file=sys.stderr)
            return 1
        if row_count != args.num_inserts:
            print(
                f"ERROR: expected {args.num_inserts} rows, found {row_count}",
                file=sys.stderr,
            )
            return 1

    metrics_after: Dict[str, Any] = {}
    if not args.skip_metrics:
        metrics_after = collect_allocator_metrics(web_hosts)

    # Prefer post-run gauges for RESULT; include both in JSON blob for debugging.
    gauges = (metrics_after or {}).get("gauges") or {}
    metric_bits = " ".join(f"{k}={int(v)}" for k, v in sorted(gauges.items()) if v)

    result = {
        "label": args.label,
        "inserts": args.num_inserts,
        "seconds": elapsed,
        "inserts_per_sec": ips,
        "payload_bytes": args.payload_bytes,
        "mode": args.mode,
        "row_count": row_count,
        "host": args.host,
        "port": args.port,
        "metrics_before": metrics_before,
        "metrics_after": metrics_after,
    }

    print(
        "RESULT"
        f' label="{args.label}"'
        f" inserts={args.num_inserts}"
        f" seconds={elapsed:.6f}"
        f" inserts_per_sec={ips:.3f}"
        f" payload_bytes={args.payload_bytes}"
        f" mode={args.mode}"
        f" row_count={row_count}"
        + (f" {metric_bits}" if metric_bits else ""),
        flush=True,
    )
    print(f"DETAIL_JSON {json.dumps(result, sort_keys=True)}", flush=True)
    print(
        f"OK: inserted and verified {args.num_inserts} rows in {elapsed:.3f}s "
        f"({ips:.0f} inserts/s) label={args.label}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
