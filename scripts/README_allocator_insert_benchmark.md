# YSQL INSERT allocator benchmark (TCMalloc vs jemalloc)

Measure **real YugabyteDB INSERT** performance for two builds: default **TCMalloc** vs opt-in **jemalloc** (`--use-jemalloc`).

You need a built tree and a running cluster (or let the compare mode start `yugabyted` for you).

---

## Option A — Fastest (one cluster at a time)

Run the same insert test twice: once on a TCMalloc build, once on a jemalloc build. Compare the two `RESULT` lines.

### 1. Build with TCMalloc (default) and start the DB

```bash
cd /path/to/yugabyte-db

./yb_build.sh release

# Start a local single-node cluster (pick one approach you already use), e.g.:
./bin/yugabyted start --base_dir /tmp/yb-tcmalloc-data
```

Wait until YSQL accepts connections (default port **5433**).

### 2. Run the benchmark (100k inserts)

```bash
./scripts/benchmark_allocators.sh \
  --host 127.0.0.1 \
  --port 5433 \
  --label tcmalloc \
  --num-inserts 100000
```

Save the printed `RESULT` line (seconds / inserts_per_sec / metrics).

### 3. Stop that cluster

```bash
./bin/yugabyted stop --base_dir /tmp/yb-tcmalloc-data
```

### 4. Build with jemalloc and start again

Use a **separate build directory** so you do not overwrite the TCMalloc binaries:

```bash
YB_BUILD_ROOT=/tmp/yb-build-jemalloc ./yb_build.sh release --use-jemalloc

# Start using binaries from that build (yugabyted next to that install):
/tmp/yb-build-jemalloc/latest/bin/yugabyted start --base_dir /tmp/yb-jemalloc-data
# If your layout differs, point at the bindir that contains bin/yugabyted and bin/ysqlsh.
```

If `yugabyted` always uses the repo `bin/` wrappers, ensure `build/latest` (or your install) is the jemalloc build before starting, **or** invoke `yugabyted` / `ysqlsh` from the jemalloc build’s `bin/` directory explicitly.

### 5. Run the same benchmark

```bash
./scripts/benchmark_allocators.sh \
  --host 127.0.0.1 \
  --port 5433 \
  --label jemalloc \
  --num-inserts 100000 \
  --ysqlsh /tmp/yb-build-jemalloc/latest/postgres/bin/ysqlsh
```

(`--ysqlsh` path may be `.../bin/ysqlsh` depending on layout; use whatever `bin/ysqlsh` resolves to for that build.)

### 6. Stop

```bash
/tmp/yb-build-jemalloc/latest/bin/yugabyted stop --base_dir /tmp/yb-jemalloc-data
# or: ./bin/yugabyted stop --base_dir /tmp/yb-jemalloc-data
```

### 7. Compare

Look at the two `RESULT` lines:

```text
RESULT label="tcmalloc" inserts=100000 seconds=... inserts_per_sec=... ...
RESULT label="jemalloc" inserts=100000 seconds=... inserts_per_sec=... ...
```

Lower `seconds` / higher `inserts_per_sec` is faster. Metrics fields (e.g. `generic_current_allocated_bytes` vs `jemalloc_resident_bytes`) come from the tserver Prometheus endpoint when reachable (`http://127.0.0.1:9000` by default).

---

## Option B — Script starts both clusters (`--compare`)

If both builds are already finished and each has a usable `bin/yugabyted`:

```bash
cd /path/to/yugabyte-db

./scripts/benchmark_allocators.sh --compare \
  --tcmalloc-bindir "$PWD/build/latest" \
  --jemalloc-bindir /tmp/yb-build-jemalloc/latest \
  --num-inserts 100000
```

The script starts TCMalloc cluster → runs inserts → stops → starts jemalloc cluster → runs inserts → stops → prints a small comparison table.

Adjust bindir paths to where **your** `release` builds actually landed.

---

## Optional knobs

| Flag | Default | Meaning |
|------|---------|---------|
| `--num-inserts` | `100000` | Number of rows inserted |
| `--payload-bytes` | `256` | Size of each row’s `TEXT` payload |
| `--mode generate_series` | (default) | One multi-row `INSERT ... SELECT generate_series` |
| `--mode batched` | | Many multi-row inserts (`--batch-size`, default 1000) |
| `--mode single_row` | | Loop of single-row inserts (slowest, most statement overhead) |
| `--tserver-web URL` | `http://127.0.0.1:9000` | Scrape allocator metrics |
| `--user` / `--database` | `yugabyte` / `yugabyte` | YSQL login |

Examples:

```bash
# Heavier row payload
./scripts/benchmark_allocators.sh --host 127.0.0.1 --port 5433 --label tcmalloc \
  --num-inserts 100000 --payload-bytes 1024

# More statement overhead
./scripts/benchmark_allocators.sh --host 127.0.0.1 --port 5433 --label tcmalloc \
  --mode single_row --num-inserts 10000
```

Or call the Python driver directly:

```bash
python3 ./scripts/ysql_insert_allocator_bench.py --help
```

---

## What “success” looks like

- Exit code **0**
- Line: `OK: inserted and verified 100000 rows ...`
- `row_count=100000` on the `RESULT` line (table was counted after insert)

If YSQL is down or `ysqlsh` points at a missing build, the script fails with `YSQL not ready` — start the cluster and fix `--ysqlsh` / ports.

---

## Prerequisites (short)

1. Successful `./yb_build.sh release` (and a second build with `--use-jemalloc` for a fair A/B).
2. Cluster listening on the host/port you pass (default `127.0.0.1:5433`).
3. `python3` available; `ysqlsh` from the same build as the servers when possible.
