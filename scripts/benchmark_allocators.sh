#!/usr/bin/env bash
# Benchmark TCMalloc vs jemalloc (and system malloc) with a 100k insert-like workload.
#
# Usage (from repo root — no full YugabyteDB build required):
#   ./scripts/benchmark_allocators.sh
#   ./scripts/benchmark_allocators.sh --num-inserts=100000 --threads=4 --value-size=512
#
# Optional packages for full comparison + allocator stats:
#   Debian/Ubuntu: sudo apt-get install -y g++ libgoogle-perftools-dev libjemalloc-dev
#   RHEL/Fedora:   sudo dnf install -y gcc-c++ gperftools-devel jemalloc-devel
#
# Works with only runtime .so libraries too (links by absolute path; stats may be limited).
# Exit 0 if all built variants succeed.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
SRC="${REPO_ROOT}/src/yb/tools/allocator_benchmark.cc"
OUT_DIR="${TMPDIR:-/tmp}/yb_allocator_benchmark_$$"
CXX=${CXX:-g++}
CXXFLAGS=${CXXFLAGS:--O2 -std=c++17 -Wall -Wextra}
BENCH_ARGS=("$@")

if [[ ! -f "$SRC" ]]; then
  echo "ERROR: missing source $SRC" >&2
  exit 2
fi

mkdir -p "$OUT_DIR"
cleanup() { rm -rf "$OUT_DIR"; }
trap cleanup EXIT

# Resolve a shared library path for -lNAME or return empty.
find_lib() {
  local name=$1
  local candidates=(
    "/usr/lib/x86_64-linux-gnu/lib${name}.so"
    "/usr/lib/x86_64-linux-gnu/lib${name}.so.2"
    "/usr/lib/x86_64-linux-gnu/lib${name}.so.4"
    "/usr/lib64/lib${name}.so"
    "/usr/lib/lib${name}.so"
    "/usr/local/lib/lib${name}.so"
  )
  local c
  for c in "${candidates[@]}"; do
    if [[ -e "$c" ]]; then
      echo "$c"
      return 0
    fi
  done
  # ldconfig cache
  if command -v ldconfig >/dev/null 2>&1; then
    local hit
    hit=$(ldconfig -p 2>/dev/null | awk -v n="lib${name}.so" '$1 ~ n { print $NF; exit }')
    if [[ -n "${hit:-}" && -e "$hit" ]]; then
      echo "$hit"
      return 0
    fi
  fi
  return 1
}

# Try -lNAME link probe.
link_dash_l_ok() {
  local name=$1
  echo 'int main(){return 0;}' | "$CXX" -x c++ - -l"$name" -o "${OUT_DIR}/probe_${name}" 2>/dev/null
}

build_one() {
  local tag=$1
  shift
  local out="${OUT_DIR}/allocator_benchmark_${tag}"
  local log="${OUT_DIR}/build_${tag}.log"
  # shellcheck disable=SC2086
  if "$CXX" $CXXFLAGS "$@" "$SRC" -o "$out" -lpthread -ldl -lm >"$log" 2>&1; then
    echo "$out"
    return 0
  fi
  echo "  build failed for ${tag}:" >&2
  tail -8 "$log" >&2 || true
  return 1
}

echo "=== YugabyteDB allocator benchmark ==="
echo "source: $SRC"
echo "cxx:    $CXX $CXXFLAGS"
if [[ ${#BENCH_ARGS[@]} -eq 0 ]]; then
  echo "args:   (defaults: 100k inserts, retain-until-end)"
else
  echo "args:   ${BENCH_ARGS[*]}"
fi
echo

RUN_TAGS=()
RUN_BINS=()
RESULT_LINES=()

# --- tcmalloc ---
tcmalloc_built=0
if link_dash_l_ok tcmalloc; then
  if bin=$(build_one tcmalloc -DUSE_TCMALLOC=1 -DALLOCATOR_DISPLAY_NAME=tcmalloc -ltcmalloc); then
    RUN_TAGS+=(tcmalloc); RUN_BINS+=("$bin"); tcmalloc_built=1
  fi
elif link_dash_l_ok tcmalloc_minimal; then
  if bin=$(build_one tcmalloc -DUSE_TCMALLOC=1 -DALLOCATOR_DISPLAY_NAME=tcmalloc_minimal -ltcmalloc_minimal); then
    RUN_TAGS+=(tcmalloc); RUN_BINS+=("$bin"); tcmalloc_built=1
  fi
fi
if [[ $tcmalloc_built -eq 0 ]]; then
  if libpath=$(find_lib tcmalloc); then
    if bin=$(build_one tcmalloc -DUSE_TCMALLOC=1 -DALLOCATOR_DISPLAY_NAME=tcmalloc "$libpath"); then
      RUN_TAGS+=(tcmalloc); RUN_BINS+=("$bin"); tcmalloc_built=1
    fi
  elif libpath=$(find_lib tcmalloc_minimal); then
    if bin=$(build_one tcmalloc -DUSE_TCMALLOC=1 -DALLOCATOR_DISPLAY_NAME=tcmalloc_minimal "$libpath"); then
      RUN_TAGS+=(tcmalloc); RUN_BINS+=("$bin"); tcmalloc_built=1
    fi
  fi
fi
if [[ $tcmalloc_built -eq 0 ]]; then
  echo "SKIP tcmalloc: no libtcmalloc / libtcmalloc_minimal found"
fi

# --- jemalloc ---
jemalloc_built=0
if link_dash_l_ok jemalloc; then
  if bin=$(build_one jemalloc -DUSE_JEMALLOC=1 -DALLOCATOR_DISPLAY_NAME=jemalloc -ljemalloc); then
    RUN_TAGS+=(jemalloc); RUN_BINS+=("$bin"); jemalloc_built=1
  fi
fi
if [[ $jemalloc_built -eq 0 ]]; then
  if libpath=$(find_lib jemalloc); then
    if bin=$(build_one jemalloc -DUSE_JEMALLOC=1 -DALLOCATOR_DISPLAY_NAME=jemalloc "$libpath"); then
      RUN_TAGS+=(jemalloc); RUN_BINS+=("$bin"); jemalloc_built=1
    fi
  fi
fi
if [[ $jemalloc_built -eq 0 ]]; then
  echo "SKIP jemalloc: no libjemalloc found"
fi

# --- system malloc baseline ---
if bin=$(build_one system_malloc -DALLOCATOR_DISPLAY_NAME=system_malloc); then
  RUN_TAGS+=(system_malloc)
  RUN_BINS+=("$bin")
fi

if [[ ${#RUN_BINS[@]} -eq 0 ]]; then
  echo "ERROR: could not build any benchmark binary" >&2
  exit 1
fi

echo
echo "=== runs ==="
failed=0
for i in "${!RUN_BINS[@]}"; do
  tag=${RUN_TAGS[$i]}
  bin=${RUN_BINS[$i]}
  echo
  echo "--- ${tag} ---"
  set +e
  output=$("$bin" "${BENCH_ARGS[@]}")
  rc=$?
  set -e
  printf '%s\n' "$output"
  if [[ $rc -ne 0 ]]; then
    echo "FAILED (exit $rc)" >&2
    failed=1
  else
    result_line=$(printf '%s\n' "$output" | grep '^RESULT' | tail -1 || true)
    RESULT_LINES+=("$result_line")
  fi
done

echo
echo "=== comparison ==="
printf '%-22s %10s %12s %14s\n' "allocator" "inserts" "seconds" "inserts/sec"
printf '%-22s %10s %12s %14s\n' "----------------------" "----------" "------------" "--------------"
for line in "${RESULT_LINES[@]:-}"; do
  [[ -z "${line:-}" ]] && continue
  alloc=$(sed -n 's/.*allocator="\([^"]*\)".*/\1/p' <<<"$line")
  inserts=$(sed -n 's/.* inserts=\([0-9][0-9]*\).*/\1/p' <<<"$line")
  seconds=$(sed -n 's/.* seconds=\([0-9.][0-9.]*\).*/\1/p' <<<"$line")
  ips=$(sed -n 's/.* inserts_per_sec=\([0-9.][0-9.]*\).*/\1/p' <<<"$line")
  printf '%-22s %10s %12s %14s\n' "${alloc:0:22}" "$inserts" "$seconds" "$ips"
done

if [[ $failed -ne 0 ]]; then
  echo
  echo "One or more runs failed." >&2
  exit 1
fi

echo
echo "OK: all built benchmarks completed successfully."
if [[ $tcmalloc_built -eq 0 || $jemalloc_built -eq 0 ]]; then
  echo "Note: install -dev packages for both allocators for a complete TCMalloc vs jemalloc comparison:"
  echo "  sudo apt-get install -y libgoogle-perftools-dev libjemalloc-dev   # Debian/Ubuntu"
fi
echo "Tip: $0 --num-inserts=100000 --threads=4 --value-size=512"
exit 0
