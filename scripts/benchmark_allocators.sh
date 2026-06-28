#!/usr/bin/env bash
# Benchmark real YugabyteDB YSQL INSERTs under TCMalloc vs jemalloc builds.
#
# This drives INSERT traffic against a live cluster (via ysqlsh), not an in-process
# malloc micro-benchmark. To compare allocators you need two server builds (or run
# twice against the same cluster after rebuilding/restarting with each allocator).
#
# ---------------------------------------------------------------------------
# Quick: benchmark the cluster you already have running
# ---------------------------------------------------------------------------
#   ./scripts/benchmark_allocators.sh --host 127.0.0.1 --port 5433 --label mybuild
#
# ---------------------------------------------------------------------------
# Compare two installations (starts yugabyted for each, runs 100k inserts, stops)
# ---------------------------------------------------------------------------
#   # Build A (default tcmalloc):
#   ./yb_build.sh release
#   # Build B (jemalloc) into a separate build root, e.g.:
#   YB_BUILD_ROOT=/tmp/yb-build-jemalloc ./yb_build.sh release --use-jemalloc
#
#   ./scripts/benchmark_allocators.sh --compare \
#       --tcmalloc-home /path/to/yugabyte-db   \  # tree whose build/latest has tcmalloc bins
#       --jemalloc-home /path/to/yugabyte-db   \  # or set YB_BUILD_ROOT via --*-bindir
#       --tcmalloc-bindir /path/to/build-tcmalloc/latest \
#       --jemalloc-bindir /path/to/build-jemalloc/latest
#
# Flags after -- are forwarded to ysql_insert_allocator_bench.py
#   ./scripts/benchmark_allocators.sh --host 127.0.0.1 -- --mode single_row --num-inserts 10000
#
# Exit 0 if all runs succeed and row counts verify.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
PY_BENCH="${SCRIPT_DIR}/ysql_insert_allocator_bench.py"
PYTHON=${PYTHON:-python3}

mode=single   # single | compare
host=127.0.0.1
port=5433
user=yugabyte
database=yugabyte
label=running_cluster
num_inserts=100000
payload_bytes=256
insert_mode=generate_series
ysqlsh="${REPO_ROOT}/bin/ysqlsh"
tserver_web=http://127.0.0.1:9000
ready_timeout=180

tcmalloc_home=""
jemalloc_home=""
tcmalloc_bindir=""
jemalloc_bindir=""
work_root="${TMPDIR:-/tmp}/yb_alloc_bench_$$"
keep_work=0

forward_args=()

usage() {
  sed -n '2,30p' "$0" | sed 's/^# \?//'
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage ;;
    --compare) mode=compare; shift ;;
    --host) host=$2; shift 2 ;;
    --port) port=$2; shift 2 ;;
    --user) user=$2; shift 2 ;;
    --database) database=$2; shift 2 ;;
    --label) label=$2; shift 2 ;;
    --num-inserts) num_inserts=$2; shift 2 ;;
    --payload-bytes) payload_bytes=$2; shift 2 ;;
    --mode) insert_mode=$2; shift 2 ;;  # generate_series|batched|single_row
    --ysqlsh) ysqlsh=$2; shift 2 ;;
    --tserver-web) tserver_web=$2; shift 2 ;;
    --ready-timeout) ready_timeout=$2; shift 2 ;;
    --tcmalloc-home) tcmalloc_home=$2; shift 2 ;;
    --jemalloc-home) jemalloc_home=$2; shift 2 ;;
    --tcmalloc-bindir) tcmalloc_bindir=$2; shift 2 ;;
    --jemalloc-bindir) jemalloc_bindir=$2; shift 2 ;;
    --work-root) work_root=$2; shift 2 ;;
    --keep-work) keep_work=1; shift ;;
    --) shift; forward_args+=("$@"); break ;;
    *)
      # Treat unknown as forward to python for convenience.
      forward_args+=("$1")
      shift
      ;;
  esac
done

if [[ ! -f "$PY_BENCH" ]]; then
  echo "ERROR: missing $PY_BENCH" >&2
  exit 2
fi

resolve_ysqlsh() {
  local candidate=$1
  if [[ -x "$candidate" ]]; then
    echo "$candidate"
    return
  fi
  if [[ -x "${REPO_ROOT}/bin/ysqlsh" ]]; then
    echo "${REPO_ROOT}/bin/ysqlsh"
    return
  fi
  if command -v ysqlsh >/dev/null 2>&1; then
    command -v ysqlsh
    return
  fi
  echo "$candidate"
}

run_python_bench() {
  local lbl=$1
  local h=$2
  local p=$3
  local ysh=$4
  local web=$5
  "$PYTHON" "$PY_BENCH" \
    --ysqlsh "$ysh" \
    --host "$h" \
    --port "$p" \
    --user "$user" \
    --database "$database" \
    --num-inserts "$num_inserts" \
    --payload-bytes "$payload_bytes" \
    --mode "$insert_mode" \
    --label "$lbl" \
    --tserver-web "$web" \
    --ready-timeout-sec "$ready_timeout" \
    "${forward_args[@]+"${forward_args[@]}"}"
}

find_yugabyted() {
  local home=$1
  local bindir=$2
  local c
  for c in \
      "${bindir}/bin/yugabyted" \
      "${home}/bin/yugabyted" \
      "${bindir}/yugabyted" \
      "${REPO_ROOT}/bin/yugabyted"; do
    if [[ -n "$c" && -x "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

# Start a disposable single-node cluster with binaries from bindir/home, run bench, stop.
run_with_yugabyted() {
  local lbl=$1
  local home=$2
  local bindir=$3
  local base_port=$4

  local ybd
  if ! ybd=$(find_yugabyted "$home" "$bindir"); then
    echo "ERROR: yugabyted not found for label=$lbl (home=$home bindir=$bindir)" >&2
    return 1
  fi

  local data_dir="${work_root}/${lbl}/data"
  local log_dir="${work_root}/${lbl}/logs"
  mkdir -p "$data_dir" "$log_dir"

  # Prefer pointing yugabyted at an explicit install / build output when provided.
  local -a start_cmd=( "$ybd" start
    --base_dir "$data_dir"
    --advertise_address 127.0.0.1
  )
  # yugabyted discovers binaries relative to its location; if bindir is a full
  # YB build "latest" tree, use that yugabyted by preferring bindir/bin.
  if [[ -n "$bindir" && -x "${bindir}/bin/yugabyted" ]]; then
    start_cmd=( "${bindir}/bin/yugabyted" start --base_dir "$data_dir" --advertise_address 127.0.0.1 )
    ybd="${bindir}/bin/yugabyted"
  fi

  # Custom UI/SQL ports so two sequential runs never clash with a leftover process.
  # yugabyted uses --ysql_port in recent versions; fall back to defaults if unsupported.
  if "$ybd" start --help 2>&1 | grep -q -- '--ysql_port'; then
    start_cmd+=( --ysql_port "$base_port" )
  fi
  if "$ybd" start --help 2>&1 | grep -q -- '--webserver_port'; then
    # master web; tserver web often base+1 or fixed 9000 — we pass --tserver-web below.
    :
  fi

  echo "=== starting cluster label=$lbl via $ybd ==="
  set +e
  "${start_cmd[@]}" >"${log_dir}/yugabyted_start.log" 2>&1
  local rc=$?
  set -e
  if [[ $rc -ne 0 ]]; then
    echo "ERROR: yugabyted start failed (rc=$rc). Log: ${log_dir}/yugabyted_start.log" >&2
    tail -40 "${log_dir}/yugabyted_start.log" >&2 || true
    return 1
  fi

  # Resolve ysqlsh next to the same install when possible.
  local ysh
  ysh=$(resolve_ysqlsh "${bindir:-$home}/bin/ysqlsh")
  if [[ ! -x "$ysh" ]]; then
    ysh=$(resolve_ysqlsh "$ysqlsh")
  fi

  # Default yugabyted ports: YSQL 5433, tserver web 9000 (override if custom).
  local ysql_port=$base_port
  local web="http://127.0.0.1:9000"
  if [[ "$base_port" != "5433" ]]; then
    # Best effort: many builds still use 9000 for tserver UI on single node.
    web="http://127.0.0.1:9000"
  fi

  set +e
  run_python_bench "$lbl" 127.0.0.1 "$ysql_port" "$ysh" "$web"
  local bench_rc=$?
  set -e

  echo "=== stopping cluster label=$lbl ==="
  set +e
  "$ybd" stop --base_dir "$data_dir" >"${log_dir}/yugabyted_stop.log" 2>&1
  set -e

  return $bench_rc
}

RESULT_LINES=()
failed=0

echo "=== YugabyteDB YSQL INSERT allocator benchmark ==="
echo "repo:    $REPO_ROOT"
echo "python:  $PYTHON $PY_BENCH"
echo "mode:    $mode"
echo "inserts: $num_inserts  payload_bytes=$payload_bytes  insert_mode=$insert_mode"
echo

if [[ "$mode" == "single" ]]; then
  ysh=$(resolve_ysqlsh "$ysqlsh")
  echo "target:  ${user}@${host}:${port}/${database}"
  echo "ysqlsh:  $ysh"
  echo
  set +e
  output=$(run_python_bench "$label" "$host" "$port" "$ysh" "$tserver_web")
  rc=$?
  set -e
  printf '%s\n' "$output"
  if [[ $rc -ne 0 ]]; then
    failed=1
  else
    RESULT_LINES+=("$(printf '%s\n' "$output" | grep '^RESULT' | tail -1)")
  fi
else
  # compare
  mkdir -p "$work_root"
  if [[ $keep_work -eq 0 ]]; then
    trap 'rm -rf "$work_root"' EXIT
  else
    echo "work_root=$work_root (kept)"
    trap - EXIT
  fi

  tcmalloc_home=${tcmalloc_home:-$REPO_ROOT}
  jemalloc_home=${jemalloc_home:-$REPO_ROOT}

  echo "tcmalloc: home=$tcmalloc_home bindir=${tcmalloc_bindir:-<default>}"
  echo "jemalloc: home=$jemalloc_home bindir=${jemalloc_bindir:-<default>}"
  echo

  set +e
  out1=$(run_with_yugabyted tcmalloc "$tcmalloc_home" "${tcmalloc_bindir:-}" 5433)
  rc1=$?
  set -e
  printf '%s\n' "$out1"
  if [[ $rc1 -ne 0 ]]; then failed=1; else RESULT_LINES+=("$(printf '%s\n' "$out1" | grep '^RESULT' | tail -1)"); fi

  # Brief pause so ports are released.
  sleep 3

  set +e
  out2=$(run_with_yugabyted jemalloc "$jemalloc_home" "${jemalloc_bindir:-}" 5433)
  rc2=$?
  set -e
  printf '%s\n' "$out2"
  if [[ $rc2 -ne 0 ]]; then failed=1; else RESULT_LINES+=("$(printf '%s\n' "$out2" | grep '^RESULT' | tail -1)"); fi
fi

echo
echo "=== comparison (YSQL INSERT RESULT lines) ==="
printf '%-12s %10s %12s %14s %s\n' "label" "inserts" "seconds" "inserts/sec" "allocator_metrics..."
printf '%-12s %10s %12s %14s %s\n' "------------" "----------" "------------" "--------------" "-------------------"
for line in "${RESULT_LINES[@]:-}"; do
  [[ -z "${line:-}" ]] && continue
  lbl=$(sed -n 's/.*label="\([^"]*\)".*/\1/p' <<<"$line")
  inserts=$(sed -n 's/.* inserts=\([0-9][0-9]*\).*/\1/p' <<<"$line")
  seconds=$(sed -n 's/.* seconds=\([0-9.][0-9.]*\).*/\1/p' <<<"$line")
  ips=$(sed -n 's/.* inserts_per_sec=\([0-9.][0-9.]*\).*/\1/p' <<<"$line")
  rest=$(sed 's/^RESULT label="[^"]*" inserts=[^ ]* seconds=[^ ]* inserts_per_sec=[^ ]* payload_bytes=[^ ]* mode=[^ ]* row_count=[^ ]*//' <<<"$line")
  printf '%-12s %10s %12s %14s %s\n' "$lbl" "$inserts" "$seconds" "$ips" "$rest"
done

if [[ $failed -ne 0 ]]; then
  echo
  echo "FAILED: one or more benchmark runs did not complete successfully." >&2
  echo "Ensure a cluster is running (single mode) or yugabyted + builds exist (compare mode)." >&2
  exit 1
fi

echo
echo "OK: YSQL INSERT benchmark(s) completed; row counts verified."
echo "Tip (single):  $0 --host 127.0.0.1 --port 5433 --label tcmalloc --num-inserts 100000"
echo "Tip (compare): $0 --compare --tcmalloc-bindir /path/build-tc/latest --jemalloc-bindir /path/build-je/latest"
exit 0
