# Performance baseline (L1–L3)

ebbackup tracks micro and end-to-end throughput on fixed synthetic workloads.
CI runs `ebbackup_bench_check` as a **hard gate** via ctest.

## Levels

| Level | Workload | Metrics | Gate file |
|-------|----------|---------|-----------|
| L1 | 64MB synthetic, FastCDC + HCRBO incremental | `fastcdc_mbps`, `hcrbo_incr_mbps`, `reuse_pct` | [`bench/baselines/ci_floor.json`](../bench/baselines/ci_floor.json) |
| L2 | Same + 1-byte edit @ 5MB | `reuse_pct` ≥ 90% | same |
| L3 | 32MB full backup sequential vs pipeline | `pipeline_ratio` ≥ 0.90 | same |

## Run locally

From repo root after building Release:

```bash
cmake --build build --config Release --target ebbackup_bench_check
./build/engine_cpp/Release/ebbackup_bench_check.exe   # Windows
# or
./build/engine_cpp/ebbackup_bench_check                 # Linux single-config
```

Override floors:

```bash
export EB_BENCH_FLOOR_PATH=/path/to/ci_floor.json
```

## Updating floors

1. Run `ebbackup_bench_check` on a clean Release build on the target runner class.
2. Set each `*_min` in `ci_floor.json` to roughly **70%** of observed values (reuse/ratio keep at 90 / 0.90).
3. Commit the JSON and verify CI passes.

## Algorithm change policy

Any change to `eb_hcrbo.cc`, `fast_cdc.cc`, or `backup_pipeline.cc` must:

- Keep all gtest parity tests green
- Pass `ebbackup_bench_check` (ctest)

Do not change chunk Content ID / SHA256 semantics without a major release plan.
