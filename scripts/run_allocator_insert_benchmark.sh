#!/usr/bin/env bash
# One-shot TCMalloc vs jemalloc YSQL INSERT benchmark.
#
# Default flow (from repo root):
#   ./scripts/run_allocator_insert_benchmark.sh
#
# 1) Builds release with TCMalloc (default) into build-alloc-bench/tcmalloc
# 2) Builds release with jemalloc into build-alloc-bench/jemalloc (--use-jemalloc)
# 3) Starts each build with yugabyted, runs 100k YSQL inserts, stops the cluster
# 4) Prints a side-by-side comparison
#
# Flags:
#   --skip-build     Reuse existing build dirs (fail if missing)
#   --num-inserts N  Default 100000
#   --build-type T   Passed to yb_build.sh (default: release)
#   --yb-build-extra ARGS  Extra args for every yb_build.sh invocation (quoted string)
#   --work-dir DIR   Default: <repo>/build-alloc-bench
#   --keep-data      Do not delete yugabyted data dirs under work-dir
#   --dry-run        Print actions only
#
# Requires: python3, and a successful toolchain setup for yb_build.sh.
# Full builds can take a long time; use --skip-build on later runs.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
PY_BENCH="${SCRIPT_DIR}/ysql_insert_allocator_bench.py"
YB_BUILD="${REPO_ROOT}/yb_build.sh"

skip_build=0
num_inserts=100000
payload_bytes=256
insert_mode=generate_series
build_type=release
yb_build_extra=""
work_dir="${REPO_ROOT}/build-alloc-bench"
keep_data=0
dry_run=0
ready_timeout=300
ysql_port=5433
tserver_web=http://127.0.0.1:9000

usage() {
  cat <<'USAGE'
Usage: ./scripts/run_allocator_insert_benchmark.sh [options]

Builds YugabyteDB twice (TCMalloc + jemalloc), runs a YSQL INSERT benchmark on
each, and prints a comparison. Run from anywhere; uses the repo that contains
this script.

  --skip-build              Do not build; require existing work-dir trees
  --num-inserts N           Rows to insert (default 100000)
  --payload-bytes N         TEXT payload size (default 256)
  --mode MODE               generate_series | batched | single_row
  --build-type TYPE         yb_build.sh build type (default release)
  --yb-build-extra "ARGS"   Extra args appended to every yb_build.sh call
  --work-dir DIR            Build + data root (default: <repo>/build-alloc-bench)
  --keep-data               Keep yugabyted data dirs after the run
  --ready-timeout SEC       Wait for YSQL (default 300)
  --dry-run                 Print steps only
  -h, --help                This help

Examples:
  ./scripts/run_allocator_insert_benchmark.sh
  ./scripts/run_allocator_insert_benchmark.sh --skip-build
  ./scripts/run_allocator_insert_benchmark.sh --num-inserts 50000 --build-type release
USAGE
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage ;;
    --skip-build) skip_build=1; shift ;;
    --num-inserts) num_inserts=$2; shift 2 ;;
    --payload-bytes) payload_bytes=$2; shift 2 ;;
    --mode) insert_mode=$2; shift 2 ;;
    --build-type) build_type=$2; shift 2 ;;
    --yb-build-extra) yb_build_extra=$2; shift 2 ;;
    --work-dir) work_dir=$2; shift 2 ;;
    --keep-data) keep_data=1; shift ;;
    --ready-timeout) ready_timeout=$2; shift 2 ;;
    --dry-run) dry_run=1; shift ;;
    *)
      echo "Unknown option: $1 (try --help)" >&2
      exit 2
      ;;
  esac
done

log() { printf '+ %s\n' "$*"; }
run() {
  if [[ $dry_run -eq 1 ]]; then
    log "DRY-RUN: $*"
    return 0
  fi
  log "$*"
  "$@"
}

if [[ ! -x "$YB_BUILD" && ! -f "$YB_BUILD" ]]; then
  echo "ERROR: yb_build.sh not found at $YB_BUILD" >&2
  exit 2
fi
if [[ ! -f "$PY_BENCH" ]]; then
  echo "ERROR: missing $PY_BENCH" >&2
  exit 2
fi

mkdir -p "$work_dir"
TC_ROOT="${work_dir}/tcmalloc"
JE_ROOT="${work_dir}/jemalloc"
TC_DATA="${work_dir}/data-tcmalloc"
JE_DATA="${work_dir}/data-jemalloc"
RESULTS_DIR="${work_dir}/results"
mkdir -p "$RESULTS_DIR"

# Locate yugabyted / ysqlsh under a YB_BUILD_ROOT-style tree (…/latest or the root itself).
resolve_bin() {
  local root=$1
  local name=$2
  local c
  for c in \
    "${root}/latest/bin/${name}" \
    "${root}/bin/${name}" \
    "${root}/latest/postgres/bin/${name}" \
    "${root}/postgres/bin/${name}"; do
    if [[ -x "$c" || -f "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  return 1
}

build_one() {
  local label=$1
  local root=$2
  shift 2
  # remaining: extra yb_build flags (e.g. --use-jemalloc)
  local -a extra=("$@")
  # shellcheck disable=SC2206
  local -a user_extra=( ${yb_build_extra} )

  echo
  echo "========== BUILD ${label} =========="
  echo "YB_BUILD_ROOT=${root}"
  mkdir -p "$root"

  if [[ $skip_build -eq 1 ]]; then
    if ! resolve_bin "$root" yugabyted >/dev/null; then
      echo "ERROR: --skip-build set but no yugabyted under $root" >&2
      echo "       Run once without --skip-build, or point --work-dir at finished builds." >&2
      exit 1
    fi
    echo "Skipping build (using existing tree)."
    return 0
  fi

  # Clean data from a previous interrupted run for this label only (not the build).
  (
    cd "$REPO_ROOT"
    export YB_BUILD_ROOT="$root"
    # Avoid clobbering the developer's normal build/latest symlink if possible.
    export YB_DISABLE_LATEST_SYMLINK="${YB_DISABLE_LATEST_SYMLINK:-1}"
    # shellcheck disable=SC2086
    run bash "$YB_BUILD" "$build_type" "${extra[@]}" ${user_extra[@]+"${user_extra[@]}"}
  )
}

stop_cluster() {
  local root=$1
  local data=$2
  local ybd
  if ybd=$(resolve_bin "$root" yugabyted); then
    set +e
    "$ybd" stop --base_dir "$data" >/dev/null 2>&1
    set -e
  fi
  # Best-effort: kill leftovers only if they use our data dir (avoid killing user clusters).
  if [[ -d "$data" ]]; then
    set +e
    pkill -f "base_dir=${data}" 2>/dev/null
    pkill -f "--fs_data_dirs=${data}" 2>/dev/null
    set -e
  fi
  sleep 2
}

start_cluster() {
  local label=$1
  local root=$2
  local data=$3

  local ybd
  if ! ybd=$(resolve_bin "$root" yugabyted); then
    echo "ERROR: yugabyted not found under $root after build" >&2
    find "$root" -name yugabyted 2>/dev/null | head -5 >&2 || true
    exit 1
  fi

  rm -rf "$data"
  mkdir -p "$data"

  echo
  echo "========== START cluster (${label}) =========="
  echo "yugabyted=$ybd"
  echo "base_dir=$data"

  stop_cluster "$root" "$data"

  local -a cmd=( "$ybd" start --base_dir "$data" --advertise_address 127.0.0.1 )
  if "$ybd" start --help 2>&1 | grep -q -- '--ysql_port'; then
    cmd+=( --ysql_port "$ysql_port" )
  fi

  if [[ $dry_run -eq 1 ]]; then
    log "DRY-RUN: ${cmd[*]}"
    return 0
  fi

  if ! "${cmd[@]}"; then
    echo "ERROR: yugabyted start failed for $label" >&2
    exit 1
  fi
}

run_bench() {
  local label=$1
  local root=$2
  local out_file=$3

  local ysqlsh
  if ! ysqlsh=$(resolve_bin "$root" ysqlsh); then
    # Repo wrapper often works if build/latest points at this root — prefer explicit.
    ysqlsh="${REPO_ROOT}/bin/ysqlsh"
  fi

  echo
  echo "========== BENCH (${label}) =========="
  echo "ysqlsh=$ysqlsh"

  if [[ $dry_run -eq 1 ]]; then
    log "DRY-RUN: python3 $PY_BENCH --label $label ..."
    echo "RESULT label=\"${label}\" inserts=${num_inserts} seconds=0 inserts_per_sec=0 (dry-run)" | tee "$out_file"
    return 0
  fi

  set +e
  python3 "$PY_BENCH" \
    --ysqlsh "$ysqlsh" \
    --host 127.0.0.1 \
    --port "$ysql_port" \
    --user yugabyte \
    --database yugabyte \
    --num-inserts "$num_inserts" \
    --payload-bytes "$payload_bytes" \
    --mode "$insert_mode" \
    --label "$label" \
    --tserver-web "$tserver_web" \
    --ready-timeout-sec "$ready_timeout" \
    | tee "$out_file"
  local rc=${PIPESTATUS[0]}
  set -e
  return "$rc"
}

extract_result_line() {
  local f=$1
  grep '^RESULT' "$f" | tail -1 || true
}

echo "=== YugabyteDB allocator INSERT benchmark (automated) ==="
echo "repo:         $REPO_ROOT"
echo "work_dir:     $work_dir"
echo "build_type:   $build_type"
echo "num_inserts:  $num_inserts"
echo "skip_build:   $skip_build"
echo "dry_run:      $dry_run"
echo

# --- builds ---
build_one tcmalloc "$TC_ROOT"
build_one jemalloc "$JE_ROOT" --use-jemalloc

failed=0
TC_OUT="${RESULTS_DIR}/tcmalloc.txt"
JE_OUT="${RESULTS_DIR}/jemalloc.txt"
rm -f "$TC_OUT" "$JE_OUT"

# --- tcmalloc run ---
start_cluster tcmalloc "$TC_ROOT" "$TC_DATA"
if ! run_bench tcmalloc "$TC_ROOT" "$TC_OUT"; then
  echo "ERROR: tcmalloc benchmark failed" >&2
  failed=1
fi
stop_cluster "$TC_ROOT" "$TC_DATA"

sleep 3

# --- jemalloc run ---
start_cluster jemalloc "$JE_ROOT" "$JE_DATA"
if ! run_bench jemalloc "$JE_ROOT" "$JE_OUT"; then
  echo "ERROR: jemalloc benchmark failed" >&2
  failed=1
fi
stop_cluster "$JE_ROOT" "$JE_DATA"

if [[ $keep_data -eq 0 && $dry_run -eq 0 ]]; then
  rm -rf "$TC_DATA" "$JE_DATA"
fi

# --- compare ---
echo
echo "========== COMPARISON =========="
tc_line=$(extract_result_line "$TC_OUT")
je_line=$(extract_result_line "$JE_OUT")

printf '%-10s %s\n' "tcmalloc" "${tc_line:-<missing>}"
printf '%-10s %s\n' "jemalloc" "${je_line:-<missing>}"
echo

parse_field() {
  local line=$1
  local key=$2
  sed -n "s/.* ${key}=\([^ ]*\).*/\1/p" <<<"$line" | head -1
}

if [[ -n "$tc_line" && -n "$je_line" ]]; then
  tc_s=$(parse_field "$tc_line" seconds)
  je_s=$(parse_field "$je_line" seconds)
  tc_ips=$(parse_field "$tc_line" inserts_per_sec)
  je_ips=$(parse_field "$je_line" inserts_per_sec)
  printf '%-10s %12s %14s\n' "allocator" "seconds" "inserts/sec"
  printf '%-10s %12s %14s\n' "----------" "------------" "--------------"
  printf '%-10s %12s %14s\n' "tcmalloc" "${tc_s:-?}" "${tc_ips:-?}"
  printf '%-10s %12s %14s\n' "jemalloc" "${je_s:-?}" "${je_ips:-?}"
  echo
  echo "Full logs: $TC_OUT  and  $JE_OUT"
fi

if [[ $failed -ne 0 ]]; then
  echo "FAILED." >&2
  exit 1
fi

echo "OK: both builds (unless --skip-build) and both INSERT benchmarks finished."
echo "Re-run without rebuilding:  $0 --skip-build --work-dir $work_dir"
exit 0
