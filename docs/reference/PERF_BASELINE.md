# Performance baseline (L1–L7)

ebbackup tracks micro and end-to-end throughput on fixed synthetic workloads.
CI runs `ebbackup_bench_check` as a **hard gate** via ctest.

## Measurement units

All throughput gates and bench output use **MB/s (SI)** = bytes per second ÷ **10⁶**.

| Unit | Definition | Used for |
|------|------------|----------|
| **MB/s** | `bytes / 1_000_000 / seconds` | CI floors, CLI `eb bench`, market comparability |
| **MiB/s** | `bytes / 1_048_576 / seconds` | Optional secondary line in bench logs only |

Helper: [`include/ebbackup/bench/throughput.h`](../../engine_cpp/include/ebbackup/bench/throughput.h).

Legacy floor keys `fastcdc_mbps_min` / `hcrbo_incr_mbps_min` (MiB/s) are still parsed with a deprecation warning and converted automatically.

## Levels

| Level | Workload | Metrics | Gate file |
|-------|----------|---------|-----------|
| L1 | 64MB synthetic, FastCDC + HCRBO incremental | `fastcdc_MBps`, `hcrbo_incr_MBps`, `reuse_pct` | [`bench/baselines/ci_floor.json`](../../engine_cpp/bench/baselines/ci_floor.json) |
| L2 | Same + 1-byte edit @ 5MB | `reuse_pct` ≥ 90% | same |
| L3a | 32MB adaptive path (`use_pipeline=true`, engine may use sequential fast path) | `pipeline_ratio` ≥ 0.90 | same |
| L3b | 256MB StreamingChunkCpuPipeline vs sequential | `pipeline_ratio_256MB` ≥ 0.90 | same |
| L5 | 256MB full backup pipeline (absolute throughput) | `backup_pipeline_256MBps` + `profile_pct` + `stream_sub` | same |
| L6 | 2GB full backup pipeline | `backup_pipeline_2GBps` absolute floor | same |
| L7 | 32×32MB multi-file pipeline | `backup_pipeline_multi_MBps` absolute floor | same |
| L4 | EbPack orphan inject + compact; 64MB incompressible ContentClass | `ampl_ratio_after` ≤ 1.05, `content_auto_vs_lz4` ≤ 1.10 | same |

### L3a details (adaptive path)

- 32MB single-file synthetic; compares `disable_pipeline` vs `use_pipeline=true`.
- With default `worker_count==0`, engine delegates to `ChunkPendingFiles` + `StorePendingChunks` (sequential fast path) — **not** threaded `RunBackupPipeline`.
- **Ratio gate only** (`pipeline_ratio_min` ≥ 0.90, immutable). `backup_pipeline_MBps_min` is **0** in Stage 3.1 (non-blocking).

### Topology (single-file full backup, `worker_count==0`)

| Path | Condition | Implementation |
|------|-----------|----------------|
| **L3a sequential fast path** | ≤32MB | `RunSingleFileInlinePipeline` / engine `ChunkPendingFiles` |
| **StreamingChunkCpuPipeline** | >32MB, one file | `RunSingleFileStreamingChunkPipeline`: 32MB `FastCdcStreamFeed` + 2 encode + 1 store worker; optional whole-file `FastCdcSlice` when `EBBACKUP_CDC_FAST_SLICE=1` |
| **L7 Pipeline v4** | multi-file or explicit workers | `RunBackupPipeline` threaded stages |

Incremental backup and `worker_count>0` / `EBBACKUP_PIPELINE_WORKERS` still use Pipeline v4. Set `EBBACKUP_FORCE_STREAM_CDC=1` to disable CDC fast slice and force stream feeds.

### L3b details (StreamingChunkCpuPipeline)

- 256MB single-file synthetic; compares `disable_pipeline` vs `use_pipeline=true`.
- Default topology routes to **StreamingChunkCpuPipeline** (not full v4): feed-level CDC/digest overlap with bounded encode/store workers.
- **Ratio gate** (`pipeline_ratio_256MB_min` ≥ 0.90, immutable).

### L5 details

- 256MB single-file synthetic workload; v0.6 default repo (`InitV05Repo` / `InitDefaultRepo`: EbPack + coalesced superblock meta).
- Times full pipeline `RunBackup` with commit-point durability unchanged.
- **Absolute gate**: `backup_pipeline_256MBps_min` (Stage 3.1: ~70% of observed Release throughput).
- **Profile line**: every L5 run prints `bench_check L5 profile_pct: chunk=…% encode=…% store=…%`, `stream_sub: cdc=…% digest=…% carry=…%`, and `sha_ni: available|unavailable`. Set `EBBACKUP_PIPELINE_PROFILE=1` for millisecond breakdown.

### L5 bottleneck (v0.9.4 profile, 256MB StreamingChunkCpuPipeline @ ~171 MB/s)

| Phase | Share of pipe |
|-------|---------------|
| chunk (CDC+digest) | **~82%** |
| encode | ~16% |
| store | ~1% |
| read | ~0.5% |

**Stream sub-timing** (within chunk): cdc **~82%**, digest **~16%**, carry **~1.7%**.

**Sprint 4 changes:** Phase B seg1 bulk scan after carry virtual prefix; Phase A whole-file `FastCdcSlice::ChunkCuts` router (opt-in `EBBACKUP_CDC_FAST_SLICE=1` — default stream feeds retain encode overlap).

**Optimization priority:** encode overlap tuning for CDC fast slice > Stage 3.2 digest saturation.

### L5 bottleneck (v0.9.3 profile, 256MB StreamingChunkCpuPipeline @ ~161 MB/s)

| Phase | Share of pipe |
|-------|---------------|
| chunk (CDC+digest) | **~83%** |
| encode | ~16% |
| store | ~1% |
| read | ~1% |

**Stream sub-timing** (within chunk, after Phase 3 digest batching): cdc **~82%**, digest **~16%**, carry **~1.6%** (was digest ~38% in v0.9.2).

**Phase 3 changes:** `digest_base` file-absolute `DigestPool` batching on carry feeds; intra-job span sharding in `DigestPool`. CDC carry-split deferred (gear state continuity).

**Optimization priority:** CDC scan on carry feeds (future) > encode overlap > Stage 3.2 digest saturation.

### L6 details

- 2GB single-file synthetic workload; v0.7 default repo (EbPack sharded + Pipeline v3).
- Times full pipeline `RunBackup` with commit-point durability unchanged.
- **Absolute gate**: `backup_pipeline_2GBps_min` (same floor class as L5; amortizes scan/manifest fixed cost).

### L7 details

- 32×32MB files (1GB total); v0.8 Pipeline v4 chunk-level parallelism + multi-file scheduler.
- Times full pipeline `RunBackup` with commit-point durability unchanged.
- **Absolute gate**: `backup_pipeline_multi_MBps_min`.

### Pipeline profiling (Wave 4)

L5 bench prints phase percentages on every run; set `EBBACKUP_PIPELINE_PROFILE=1` for full millisecond breakdown at backup end:

```
pipeline_profile: scan=… read=… chunk=… encode=… store=… flush=… meta=… pipe=… total=…
```

L5 bench also prints a condensed millisecond breakdown when `EBBACKUP_PIPELINE_PROFILE=1` is set.

### L4 details

- **Storage**: v0.6 EbPack backup 512KB → inject 32KB orphan via EbPack session → `ampl_ratio` > 1 → `eb compact` (pack rewrite) → `ampl_ratio_after` ≤ 1.05, verify OK.
- **ContentClass CPU**: 64MB incompressible payload (`.jpg` path hint) encoded with `compress=auto` must finish within **1.10×** the time of `compress=lz4` (auto skips LZ4/zstd work).

Ops alert: run `eb repo-stats` when `ampl_ratio` > **1.3**; compact when > **1.5**.

## Market comparability

| Level | Comparable to | Notes |
|-------|---------------|-------|
| L1 | lz4/zstd chunk micro-benchmarks, restic chunker CPU tests | In-memory only; no disk or network |
| L3a | Local full backup, adaptive engine path | Single 32MB file; ratio gate only |
| L3b | Same vendors, large single file | 256MB; StreamingChunkCpuPipeline vs sequential |
| L5 | Same vendors at larger single-file sizes | 256MB reduces fixed-cost noise vs 32MB L3 |
| L6 | Long sequential backup / single huge file | 2GB further amortizes per-backup overhead |
| L7 | Multi-file desktop / dataset backup | 32 files stress FileScheduler + global chunk queue |

When comparing published numbers, confirm the vendor uses **decimal MB/s (10⁶ B/s)**. Many storage tools report MiB/s under the label “MB/s”; ebbackup now labels SI MB/s explicitly.

## Immutable gates (Wave 5)

These floors in [`ci_floor.json`](../../engine_cpp/bench/baselines/ci_floor.json) are **never lowered** in a performance sprint:

| Key | Value | Rationale |
|-----|-------|-----------|
| `reuse_pct_min` | **90** | HCRBO incremental reuse after 1-byte edit @ 5MB (L1/L2) |
| `pipeline_ratio_min` | **0.90** | L3a adaptive path must not regress vs sequential |
| `pipeline_ratio_256MB_min` | **0.90** | L3b real pipeline must not regress vs sequential |
| `hybrid_stream_ratio_min` | **0.95** | L5 Hybrid CDC must stay within 5% of stream-feed throughput |

## Stage 3.1 vs 3.2

| | Stage 3.1 (CI blocking) | Stage 3.2 (aspirational) |
|--|-------------------------|---------------------------|
| File | [`ci_floor.json`](../../engine_cpp/bench/baselines/ci_floor.json) | [`ci_floor_stage32.json`](../../engine_cpp/bench/baselines/ci_floor_stage32.json) |
| L3a ratio | 0.90 | 0.90 |
| L3b ratio | 0.90 | 0.90 |
| L3 absolute | **0** (disabled) | 150 MB/s |
| L5 | **105** MB/s (~70% × 149) | **280** MB/s |
| L6 | **112** MB/s (~70% × 160) | **300** MB/s |
| L7 | **550** MB/s | **550** MB/s |

Stage 3.2 is for manual/nightly measurement only — not wired to ctest. Use `ci_floor_measure.json` or `ci_floor_profile.json` for probe runs.

Throughput floors in Stage 3.1 may rise when measured improvement is demonstrated; immutable ratio/reuse floors must not be lowered.

## v0.9.4 measured reference (Release Windows, Stage 3.1)

| Level | Measured | Stage 3.1 floor | Stage 3.2 aspirational | Status (3.1) |
|-------|----------|-----------------|------------------------|--------------|
| L3a ratio | 1.17–1.44 | 0.90 | 0.90 | **PASS** |
| L3b ratio | ~1.01–1.05 | 0.90 | 0.90 | **PASS** |
| L5 abs | **~170–172** | 105 | 280 | **PASS** |
| L6 abs | **~186** | 112 | 300 | **PASS** |
| L7 abs | ~646–658 | 550 | 550 | **PASS** |

L5 profile: stream path default; `stream_sub` cdc ~82%, digest ~16%; Phase B seg1 bulk reduces virtual CDC on carry feeds.

## v0.9.3 measured reference (Release Windows, Stage 3.1)

| Level | Measured | Stage 3.1 floor | Stage 3.2 aspirational | Status (3.1) |
|-------|----------|-----------------|------------------------|--------------|
| L3a ratio | 1.03–1.33 | 0.90 | 0.90 | **PASS** |
| L3b ratio | ~1.01–1.13 | 0.90 | 0.90 | **PASS** |
| L5 abs | **~159–163** | 105 | 280 | **PASS** |
| L6 abs | **~172–179** | 112 | 300 | **PASS** |
| L7 abs | ~596–627 | 550 | 550 | **PASS** |

L5 profile: digest sub-timing **~16%** (down from ~38% v0.9.2); `sha_ni=available` on reference x64 runner.

## v0.9.2 measured reference (Release Windows, Stage 3.1)

| Level | Measured | Stage 3.1 floor | Stage 3.2 aspirational | Status (3.1) |
|-------|----------|-----------------|------------------------|--------------|
| L3a ratio | 1.03–1.33 | 0.90 | 0.90 | **PASS** |
| L3b ratio | ~1.04 | 0.90 | 0.90 | **PASS** |
| L5 abs | ~154 | 105 | 280 | **PASS** |
| L6 abs | ~166 | 112 | 300 | **PASS** |
| L7 abs | ~601 | 550 | 550 | **PASS** |

L5 profile: chunk ~83%, encode ~16%; `stream_sub` carry ~1% after carry ring buffer.

## v0.9 measured reference (Release Windows, Stage 3.1)

| Level | Measured | Stage 3.1 floor | Stage 3.2 aspirational | Status (3.1) |
|-------|----------|-----------------|------------------------|--------------|
| L3a ratio | 1.03–1.26 | 0.90 | 0.90 | **PASS** |
| L3b ratio | ~0.96–0.98 | 0.90 | 0.90 | **PASS** |
| L5 abs | ~142–149 | 105 | 280 | **PASS** |
| L6 abs | ~160 | 112 | 300 | **PASS** |
| L7 abs | ~598 | 550 | 550 | **PASS** |

L5 profile (256MB): chunk ~98% of pipeline time — see [L5 bottleneck](#l5-bottleneck-v09-profile-256mb--147-mbs) above.

## v0.8 measured reference (Release Windows)

| Level | Throughput (MB/s) | CI floor (MB/s) |
|-------|-------------------|-----------------|
| L3 | ~118 | 70 |
| L5 | ~146 | 100 |
| L6 | ~153 | 100 |
| L7 | ~554 | 120 |

Floors are ~70% of observed on the reference runner; multi-file L7 benefits most from Pipeline v4 global chunk queues.

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
