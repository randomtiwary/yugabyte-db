# TCMalloc vs jemalloc INSERT benchmark

One command builds **both** allocator variants, runs **100k YSQL INSERTs** on each, and prints a comparison.

## Run (from the repo root)

```bash
./scripts/run_allocator_insert_benchmark.sh
```

That will:

1. Build `release` with **TCMalloc** (default) under `build-alloc-bench/tcmalloc`
2. Build `release` with **jemalloc** (`--use-jemalloc`) under `build-alloc-bench/jemalloc`
3. Start each build with `yugabyted`, insert 100000 rows, verify the row count, stop the cluster
4. Print a side-by-side table (`seconds`, `inserts_per_sec`)

**First run can take a long time** (two full builds). Leave it running until it prints `OK` and the comparison table.

## Run again without rebuilding

```bash
./scripts/run_allocator_insert_benchmark.sh --skip-build
```

Uses the trees already under `build-alloc-bench/`.

## Common options

```bash
# Fewer rows (faster smoke)
./scripts/run_allocator_insert_benchmark.sh --num-inserts 10000

# Heavier rows
./scripts/run_allocator_insert_benchmark.sh --payload-bytes 1024

# More statement overhead (slower)
./scripts/run_allocator_insert_benchmark.sh --mode single_row --num-inserts 10000

# Custom work directory
./scripts/run_allocator_insert_benchmark.sh --work-dir /tmp/my-alloc-bench

# Pass extra flags to every yb_build.sh invocation
./scripts/run_allocator_insert_benchmark.sh --yb-build-extra "--no-tests"

# Show what would run
./scripts/run_allocator_insert_benchmark.sh --dry-run
```

## Output

Look for:

```text
========== COMPARISON ==========
tcmalloc   RESULT label="tcmalloc" inserts=100000 seconds=... inserts_per_sec=...
jemalloc   RESULT label="jemalloc" inserts=100000 seconds=... inserts_per_sec=...

allocator      seconds    inserts/sec
---------- ------------ --------------
tcmalloc         ...           ...
jemalloc         ...           ...
```

Detailed logs: `build-alloc-bench/results/tcmalloc.txt` and `jemalloc.txt`.

Success means exit code **0** and `row_count` matching `--num-inserts` on each `RESULT` line.

## What you need

- Ability to run `./yb_build.sh` successfully in this checkout (deps/toolchain already set up)
- `python3`
- Free local ports for a single-node `yugabyted` (default YSQL **5433**, tserver web **9000**)

Do **not** leave another cluster on those ports while the script runs.

## Lower-level tools (optional)

You usually do **not** need these if you use `run_allocator_insert_benchmark.sh`:

| File | Role |
|------|------|
| `scripts/run_allocator_insert_benchmark.sh` | **Use this** — build + bench + compare |
| `scripts/ysql_insert_allocator_bench.py` | Single run against an already-running cluster |
| `scripts/benchmark_allocators.sh` | Thin wrapper around the Python tool / manual compare |

Example single-cluster run (advanced):

```bash
python3 ./scripts/ysql_insert_allocator_bench.py \
  --host 127.0.0.1 --port 5433 --label mybuild --num-inserts 100000
```
