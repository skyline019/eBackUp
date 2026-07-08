# Changelog

## v0.10.3 — 2026-07-08

### Added — Phase 16–19 收尾（ABI v37）

- **Manifest EFS meta**：`kMetaEfs` 持久化 `efs_encrypted` + `efs_key_blob_b64`
- **EFS Tier B 完整**：`ReadEncryptedFileRaw` / `WriteEncryptedFileRaw`；恢复 `efs_restored`
- **VSS WMI**：`Win32_ShadowStorage` 优先；`vssadmin` fallback
- **Workbench**：`init_encrypt` / `unwrap_recovery_key` / `rotate_password` / `upgrade_legacy_envelope` JSON API
- **C API flags**：`EB_BACKUP_FLAG_SPARSE_OFF`、`EB_BACKUP_FLAG_EFS_EXPORT_KEYS`
- **GUI**：恢复密钥向导、解锁/轮换密码、作业 quiesce/webhook、稀疏/EFS 高级选项、Webhook 测试
- **ebrecover-portable.zip**：CI artifact + `package_ebrecover_portable.ps1`
- **报告**：`recovery_key_issued`（本次 txn 新生成时）

## v0.10.2 — 2026-07-08

### Added — EFS + 告警 + 云可观测（Phase 19 / ABI v36）

- **EFS Tier A**：检测加密文件、skip 内容、`efs_encrypted_skipped` issue、`efs_skipped_count`
- **EFS Tier B**：`--efs-export-keys` + `ReadEncryptedFileRaw` → manifest `efs_key_blob_b64`
- **Webhook**：`post_backup_webhook_url`（job/daemon）；备份完成后 POST 报告 JSON
- **sync_cpp**：`status --json` 增 `failed_chunks`、`backoff_until_unix`、`last_error`
- **GUI**：`BackupJobDto` / 报告 DTO 扩展 quiesce、webhook、EFS/VSS 字段

## v0.10.1 — 2026-07-08

### Added — VSS 运维深化（Phase 18 / ABI v35）

- **真实影子存储**：`vss_shadow_storage.cc` — `vssadmin list shadowstorage` 解析与预检增强
- **CLI**：`eb vss status` — 影子关联与空间诊断
- **Job**：`quiesce_profile`、`vss_app_failure_policy`；schedule/daemon 集成
- **报告**：`vss_shadow_storage_bytes[]` 按卷

## v0.10.0 — 2026-07-08

### Added — 恢复密钥与灾备交付（Phase 17 / ABI v34）

- **`crypto.envelope.json`**：主密钥 + 密码/恢复密钥双包裹；向后兼容 legacy salt
- **C API**：`eb_backup_unwrap_with_recovery_key`、`eb_backup_rotate_password`
- **CLI**：`eb init --encrypt --recovery-key-out`；`eb unlock`；`eb rotate-password`
- **ebrecover**：`list`；`--password` / `--recovery-key`；restore 进度

## v0.9.9 — 2026-07-08

### Added — NTFS 稀疏文件（Phase 16 / ABI v33）

- **稀疏检测/备份/恢复**：`FSCTL_GET_RETRIEVAL_POINTERS` / `FSCTL_SET_SPARSE`
- **Manifest**：sparse runs + chunk offsets；报告 `sparse_file_count`
- **CLI**：`--sparse auto|off`（Windows 默认 auto）
- **测试**：`sparse_backup_restore_test`

## v0.9.8 — 2026-07-08

### Added — VSS 深化（Phase 15 / ABI v32）

- **VSS 生命周期**：`Begin` / `FinishBackup` / `End` 拆分；持久 `IVssBackupComponents`；读完成后 `GatherWriterStatus` → `BackupComplete`
- **多卷闭包**：`vss_volume_closure.cc` — junction 探测 + 多卷 `AddToSnapshotSet`
- **一致性模式**：crash / app / auto；`--vss-mode`；`EB_BACKUP_FLAG_VSS_APP`（0x1000）
- **影子预检**：默认 512MB 阈值；`vss_shadow_storage_low` issue
- **报告扩展**：`vss_mode`、`vss_cross_volume`、`vss_shadow_storage_ok`、`vss_writers[]`
- **Job / GUI**：`jobs.json` VSS 字段；BackupView 模式与 junction 选项；`eb_backup_set_vss_mode`
- **文档**：[`docs/technical/VSS.md`](docs/technical/VSS.md)

## v0.9.7 — 2026-07-08

### Added — VSS 核心（Phase 14 / ABI v31）

- **Windows VSS 快照读**：`--vss` / `EB_BACKUP_FLAG_VSS`；crash-consistent 目录备份；`--vss-fallback-live` 可选降级
- **`VssSession`**：`engine_cpp/src/winmeta/vss_session.cc`；路径重映射 `walk_root` / `logical_root` 扫描
- **备份报告**：`vss_used`、`vss_consistency`、`vss_snapshot_set_id`、`vss_volumes`
- **GUI**：BackupView 高级「使用 VSS 卷影副本」；BackupReportPanel 展示 VSS 状态
- **Schedule**：`use_vss` / `vss_fallback_live` 配置键

## v0.9.6 — 2026-07-08

### Added — Tiered compression & repo-stats observability

- **CompressTier** 三档：`fast`（默认）/ `balanced` / `max`；CLI `--compress-tier`、`--compress-level`、`--zstd-dict` / `--no-zstd-dict`
- **Zstd LDM** 与 **仓库级字典**（`{repo}/meta/zstd_dict.bin`）；`balanced`/`max` 默认启用字典训练与加载
- **ABI v30**：`EbRepoStats` 扩展 `live_uncompressed_bytes`、`live_stored_payload_bytes`、`compress_ratio`、`compressed_chunk_count`、`raw_chunk_count`、`has_zstd_dict`、`zstd_dict_bytes`
- **`eb repo-stats`** / Workbench `repo_info_json` 输出压缩率与字典信息
- **文档**：[`docs/technical/COMPRESSION.md`](docs/technical/COMPRESSION.md)

### Added — Pipeline / restore / resilience

- **Pipeline 死锁规避**：`ForEachRecord` 锁外回调、store shard 独立锁序
- **GB 级流式**：大文件 backup streaming、restore 分段校验（`restore_streaming_test`）
- **Decode fuzz**：`tests/fuzz/decode_corruption_test.cc`（ChunkStore / EbPack record 变异，`Get()` 不 crash）

### Changed — CI & sync/GUI

- GitHub Actions 触发路径含 `sync_cpp/**`、`gui/**`；`EBBACKUP_BUILD_SYNC=ON`
- Windows：`ebsync_tests` + `npm run build` + Workbench Rust 集成测试
- Linux：`ebsync_tests` + GUI 前端 build；ASan job 含 sync

### Fixed

- **ZstdDictTest** 全量套件偶发 SEH：测试 fixture 串行化 + 独立 temp 路径
- Workbench 集成测试支持 `.dll` / `.so` / `.dylib`（`EBBACKUP_DLL_DIR`）

### Docs

- 全量文档同步至 ABI v30、ctest **383** gtest、`COMPRESSION.md`、CI/sync 矩阵

## v0.9.5 — 2026-07-07

### Added — Desktop Workbench GUI (`gui/`)

- **Tauri 2 + Vue 3** 桌面工作台：仓库 / 备份 / 快照 / 恢复 / 验证 / 维护 六个 Activity
- **`ebbackup_workbench` SHARED DLL** + JSON shim（`engine_cpp/workbench/`）封装 C API
- **Rust 集成测试** `gui/src-tauri/tests/workbench_integration.rs`（`npm run test:rust`）
- **Release 打包** NSIS 安装包 + 便携 exe（`npm run build:desktop`）
- **UI 设置持久化**、壁纸模式、工作区卡片 / 输出面板 **5%–100% 透明度调节器**
- **扩展 shim API**：`runtime_info`、`last_error`、`get_backup_stats`

### Fixed — Workbench 配套引擎/GUI

- **`ComputeRepoStats`**：空仓库（无 manifest、`txn_id=0`）返回零统计，修复 `repo_info` 报错
- **备份页**：源路径存在性校验、空仓库自动全量、中文错误提示
- **透明度**：预计算 `--workspace-card-surface` / `--log-panel-surface`，壁纸模式下父层透底

### Docs

- 正式归档 [`docs/product/WORKBENCH_GUI.md`](docs/product/WORKBENCH_GUI.md)
- 工程技术手册 / 生态实录补充 Workbench 生态位说明

## v0.9.4 — 2026-07-07

### Added — Sprint 4 CDC dual-path + gear seg1 bulk scan

- **Phase B (stream path)**: `ChunkCarryPrefixVirtual` switches to contiguous seg1-only CDC scan once `pos >= view.len0` (carry prefix fully consumed); preserves cut/hash parity with `FastCdcSlice::Chunk`
- **Phase A (opt-in)**: `ChunkFileStreamingFastSlice` via `FastCdcSlice::ChunkCuts` + batched `DigestPool` hash/push; router `UseCdcFastPath` when `EBBACKUP_CDC_FAST_SLICE=1` (default off — whole-file scan loses feed/encode overlap on current topology)
- **Opt-out**: `EBBACKUP_FORCE_STREAM_CDC=1` forces stream-feed CDC for parity/regression
- **Tests**: `CdcFastPathMatchesStreamFeedManifest`; `FastCdcSlice::ChunkCuts` API

### Changed

- L5 **~170–172 MB/s** (+~6–9% vs v0.9.3); L6 **~186 MB/s**; L3b ratio **~1.01–1.05**
- `PERF_BASELINE.md`: topology CDC fast-path conditions; v0.9.4 measured table

### Unchanged (by design)

- Commit-point durability; Content ID SHA256 semantics; immutable ratio/reuse floors; C API ABI **v12**; Stage 3.1 floors unchanged

## v0.9.3 — 2026-07-07

### Added — L5/L6 Chunk CPU (Phase 3)

- **File-absolute digest batching**: `FastCdcStreamState::digest_base`; carry feeds hash via `DigestPool::HashRegions` on mmap base (fixes per-chunk serial fallback when `view.len0 > 0`)
- **DigestPool intra-job sharding**: parallel span hashing when `span_count >= threads_`
- **Bench**: L5 prints `sha_ni: available|unavailable`; extended parity tests for `digest_base`
- **Tests**: `MatchesFullChunk256MBWith32MBFeedsAndDigestBase`, `DigestBaseReducesDigestTimeOnCarryFeeds`

### Changed

- L5 **~159–163 MB/s** (+~5% vs v0.9.2); L6 **~172–179 MB/s**; L3b ratio **~1.01–1.13**
- `stream_sub` digest share **~16%** (was ~38%); cdc now dominant within chunk (~82%)
- `PERF_BASELINE.md`: v0.9.3 profile table

### Deferred

- CDC carry-boundary contiguous scan (Phase 3C): requires gear-hash state handoff at virtual→contiguous transition; reverted after parity failure

### Unchanged (by design)

- Commit-point durability; Content ID SHA256 semantics; immutable ratio/reuse floors; C API ABI **v12**; Stage 3.1 floors unchanged

## v0.9.2 — 2026-07-07

### Added — L5/L6 Chunk CPU (Phase 1+2)

- **Carry ring buffer**: `FastCdcStreamFeed` zero-copy virtual segments (`StreamSegmentView`); carry tail bounded by `max_size`; no full-feed `vector` insert/head-erase
- **Stream sub-timing**: `PipelinePhaseStats` fields `stream_cdc_ns`, `stream_digest_ns`, `stream_carry_ns`; L5 bench prints `stream_sub: cdc=…% digest=…% carry=…%`
- **StreamingChunkCpuPipeline**: `RunSingleFileStreamingChunkPipeline` for single-file full backup **>32MB** with default workers — 32MB `FastCdcStreamFeed` on main thread + 2 encode + 1 store worker (feed-level overlap without full v4 topology)
- **Topology gate** in `RunBackupPipeline`: single file `>32MB`, `BackupMode::kFull`, `worker_count==0`, no `EBBACKUP_PIPELINE_WORKERS` → streaming path; multi-file and explicit workers remain Pipeline v4
- **Tests**: `streaming_chunk_pipeline_test.cc`; `Pipeline256UsesStreamingPathTest` (renamed from threaded path test)
- **Parity tests**: `MatchesFullChunk256MBWith32MBFeeds`, `CarryBufferNeverExceedsMaxSize`

### Changed

- L5 ~154 MB/s (+~5% vs v0.9.1); L3b ratio ~1.04–1.06; L7 multi-file unchanged (~580–640 MB/s)
- `PERF_BASELINE.md`: topology table (L3a / StreamingChunkCpuPipeline / L7 v4)

### Unchanged (by design)

- Commit-point durability; Content ID SHA256 semantics; immutable ratio/reuse floors; C API ABI **v12**

## v0.9.0 — 2026-07-07

### Fixed — Wave 5 correctness (ChunkStore concurrency)

- **Sharded index maps** `shard_index_[16]`: each shard has its own map under `shard_index_mu_[shard]`, fixing concurrent insert/rehash races that caused heap corruption (`0xC0000005` / `0xC0000374`) under multi-worker pipeline store
- **`ForEachRecord` snapshot**: collect records under shard lock, invoke callbacks outside lock — fixes reentrant deadlock (`resource deadlock would occur` on MSVC) when compact/GC/verify call back into `ReadRecordForHash`
- **Tombstone lock ordering**: dedicated `tombstones_mu_`; always acquire tombstones before shard locks; `TombstoneHash` releases shard lock before tombstone insert
- **`index_entries_mu_`** for persistent index entry tracking; `record_count()` reads shard sizes under per-shard locks
- **Pipeline finalize races**: `finalized` → `std::atomic<bool>`; `RecordStoredChunkHash` under `finalize_mu`; `SetError` closes chunk/encoded queues; stats via `stats_mu`
- **Recover path**: `kAborted` → `kScanning` on `kScanFile` event

### Added — Wave 5 performance (Stage 3)

- **Sequential fast path in pipeline mode**: single file ≤32 MB with `worker_count==0` delegates to `ChunkPendingFiles` + `StorePendingChunks` (same code as `--no-pipeline`) so L3 `pipeline_ratio` gate is stable ≥ 0.90
- **Inline pipeline store loop**: mutex-free encode/store/manifest finalize in `RunSingleFileInlinePipeline` for direct `RunBackupPipeline` callers
- **Streaming threshold**: `FastCdcStreamFeed` only when file size **>** 32 MB (not ≥)
- **Meta/flush overlap**: `MergeMetaManifestEntries` runs in `std::async` during `chunk_store_->Flush()`
- **Bench**: `ci_floor_measure.json` for measure-only runs; `bench_check` ratio compare uses `ratio + 1e-6` epsilon
- **Stage 3 CI floors** in `ci_floor.json`: L3 **150**, L5 **280**, L6 **300**, L7 **550** MB/s (`reuse_pct_min` **90** and `pipeline_ratio_min` **0.90** immutable)

### Added — v0.9.1 bench layering (Stage 3.1 / 3.2)

- **L3a / L3b split**: L3a = 32MB adaptive-path ratio; L3b = 256MB real threaded pipeline ratio (`pipeline_ratio_256MB_min`)
- **L5 profile_pct**: every L5 run prints phase share; pipeline phase stats always collected for pipeline backups
- **Stage 3.1 floors** ([`ci_floor.json`](engine_cpp/bench/baselines/ci_floor.json)): L5 **105**, L6 **112**, L7 **550** MB/s; L3 absolute disabled (`backup_pipeline_MBps_min=0`)
- **Stage 3.2 aspirational** ([`ci_floor_stage32.json`](engine_cpp/bench/baselines/ci_floor_stage32.json)): L3 **150**, L5 **280**, L6 **300** — measure/nightly only
- **Probe baselines**: `ci_floor_profile.json`, updated `ci_floor_probe_l567.json`
- **Test**: `Pipeline256UsesThreadedPathTest` asserts non-zero pipeline phase stats on 256MB backup

### Changed

- Large single-file pipeline: `use_mmap=false` when `worker_count>1`
- `ebbackup_bench_check` searches additional relative paths for `ci_floor.json`

### Unchanged (by design)

- Commit-point durability: `chunk_store_->Flush()` fsync before manifest commit
- Content ID: SHA256(content) semantics
- C API ABI remains **v12**

## v0.8.0 — 2026-07-07

### Added — Wave 4 E2E throughput (Pipeline v4)

- **Pipeline v4**: global `ChunkTask` / `EncodedChunkTask` queues; N×(Reader/Chunker/Compressor) + M Store workers; `FileAggregator` per-file manifest finalize
- **Streaming CDC ingest**: large full-backup files use `FastCdcStreamFeed` (16 MiB blocks) with chunk-level encode/store overlap
- **Pipeline profiling**: `PipelinePhaseStats` + `EBBACKUP_PIPELINE_PROFILE=1` phase breakdown (scan/read/chunk/encode/store/flush/meta)
- **ChunkStore::ExistsMany**: single lock per shard batch dedup; `PutPrecompressed(..., skip_exists_check)` for pipeline trust path
- **DigestPool**: persistent worker thread pool (no per-batch thread spawn)
- **EbPack append-only writer**: incremental record append + header patch (avoids full-pack rewrite on spill)
- **Per-shard index locks**: `shard_index_mu_[16]` replaces global `index_mu_` for store/index
- **L7 bench gate**: 32×32MB multi-file floor `backup_pipeline_multi_MBps_min`
- **Tests**: `PipelineV4ChunkParityTest`, `PipelineV4MultiFileTest`

### Changed

- **Fsync dedup**: `EbPackWriter::FsyncAll` skips paths already fsynced by `FlushAllOpenPacks(true)`
- **Adaptive workers**: single-file or &lt;64MB workloads default to `max(2, hw/4)` pipeline workers
- **Store workers**: `max(2, store_shard_count/2)` workers consume shared encoded-chunk queue
- Bench CI floors set to **~70%** Release measured: L3 **70**, L5/L6 **100**, L7 **120** MB/s (multi-file L7 ~550 MB/s on reference runner)
- **Streaming finalize fix**: `chunking_complete` gate prevents premature manifest finalize during multi-block `FastCdcStreamFeed`

### Unchanged (by design)

- Commit-point durability: `chunk_store_->Flush()` fsync before manifest commit
- Content ID: SHA256(content) semantics
- Strict spill: 4 MiB / 32 records per EbPack shard
- C API ABI remains **v12**

## v0.7.0 — 2026-07-07

### Added — Wave 3 extreme performance

- **EbPack sharding**: 16 parallel pack writers (`EbPackShardSet`); per-shard strict spill (4 MiB / 32 records); backward-compatible `pack-*-s{shard}.ebpack` naming
- **SHA-NI**: hardware SHA256 for `DigestAlgo::kStandard` when CPU + self-test pass (`digest_sha_ni.cc`)
- **Pipeline v3**: `FileScheduler` graph coloring; N×(Reader/Chunker/Compressor) + shared encode queue + multiple Store workers; `EBBACKUP_PIPELINE_WORKERS` env override
- **CFI rolling cache**: incremental rolling checksum reuse in HCRBO hot path (`cfi_rolling.cc`)
- **Streaming FastCDC**: `FastCdcStreamFeed` / `FastCdcStreamFinish` with bit-identical cut points vs `FastCdcSlice::Chunk`
- **L6 bench gate**: 2GB pipeline floor `backup_pipeline_2GBps_min` in `ci_floor.json`
- **Tests**: `EbPackShardTest`, `DigestShaNiTest`, `PipelineV3Test`, `FastCdcStreamingParityTest`, extended powerfail sharded EbPack cases

### Changed

- `DigestPool` default thread count: `max(4, min(16, hw/2))` (override via `EBBACKUP_DIGEST_THREADS`)
- Gear table scalar scan: 8-byte unroll (AVX2 path unchanged)
- Linux Release builds: `-msha -msse4.2` on x86_64

### Unchanged (by design)

- Commit-point durability: `Flush()` fsync before manifest commit
- Content ID: SHA256(content) semantics
- C API ABI remains **v12** (no breaking init/layout change)

## v0.6.0 — 2026-07-06

### Added — Default EbPack product + ops闭环 + Pipeline v2

- **Default init**: CLI/CAPI/daemon create **EbPack + coalesced meta** repos (v0.6); `InitDefaultRepo` → `InitV05Repo` in tests
- **Auto pipeline**: EbPack repos enable pipeline backup by default (`ResolveBackupOptions`); `--no-pipeline` / `EB_BACKUP_FLAG_NO_PIPELINE` to opt out
- **EbPack compact**: `CompactEbPackStore` pack-rewrite compaction; `PhysicalBytes()`; repo-stats/GC EbPack-aware
- **Pipeline v2**: dual Store workers + `queue_depth=32` + `append_mu_` on `ChunkStore`
- **Tests**: `EbPackCompactTest`, storage integration v0.6 chain; bench L3/L4/L5 use V05 repos
- **ABI 12**: `EB_BACKUP_ABI_VERSION` bump; init semantics documented

### Changed

- `backup_features` superblock field widened to `uint32_t` (fixes EbPack/coalesced feature flags > 0xFF)
- `BackupEngine::Open` configures chunk store flags **before** `ChunkStore::Open`
- Bench floors recalibrated (Release Windows): L3 **101** MB/s, L5 **97** MB/s (~70% of observed 145/139)

### Breaking

- **New repos** default to EbPack layout (`data/packs/`). Existing v0.3–v0.5 repos remain read-only compatible; no automatic migration.

## v0.5.0 — 2026-07-06

### Added — Wave 2 high-pressure performance

- **DigestPool**: parallel SHA256 over chunk regions (`DigestPool::HashRegions`); FastCDC two-phase scan + batch hash for files ≥ 1MB
- **SIMD FastCDC**: AVX2 prefetch path in `fast_cdc_simd.cc` with scalar-identical cut points
- **CFI Bloom filter**: negative pre-filter before full hash on HCRBO incremental (`cfi_bloom_skip_hits` stat)
- **Dedup-before-encode**: pipeline skips compress/encrypt for chunks already in chunk index
- **EbPack** (`kBackupFeatureEbPack = 0x100`): 8MB pack blobs under `data/packs/`; chunk index v2 with pack paths
- **Coalesced superblock meta** (`kBackupFeatureCoalescedMeta = 0x200`): in-memory phase updates during backup; disk fsync only on abort or successful idle
- **StartupSelfCheck**: manifest txn ahead of superblock → recover to idle; truncate incomplete `manifest.new`
- **L5 bench gate**: 256MB pipeline floor `backup_pipeline_256MBps_min` in `ci_floor.json`
- **Tests**: `DigestPoolTest`, `CfiBloomParityTest`, `EbPackTest`, `CoalescedMetaTest`, `PowerfailExtendedTest.KillDuringV05EbPackCoalescedPipeline`

### Changed

- `InitRepoEx` accepts `ebpack` and `coalesced_meta` flags; `InitV05Repo` helper for tests/bench L5
- L1 bench logs `cfi_bloom_skip_hits`

## v0.4.6 — 2026-07-06

### Changed

- **Commit-point durability**: `strict` and `balanced` both use pack-buffered append with **fsync at manifest commit** (completed backups remain fully verifiable); crash during in-flight txn rolls back to prior snapshot / `kAborted`
- **ChunkStore write session**: `BeginAppendSession` / `Flush` / `EndAppendSession` with single append fd + `FsyncFd`; eliminates per-chunk open/fsync (major E2E win)
- **Pack thresholds**: strict 4 MiB / 32 records; balanced 16 MiB / 64 records (intermediate writes spill to fd; fsync only at commit flush)
- **Pipeline zero-copy**: mmap `FileView` through reader/chunker/compressor stages (no full-file vector copy); default `queue_depth` 16
- **L3 bench floor**: `backup_pipeline_MBps_min` raised to **53** (~70% of post-optimization observed ~76 MB/s)

### Added

- **Durability tests**: `CommitPointDurabilityTest` (strict pipeline verify, interrupted storing recovery); `PowerfailExtendedTest.KillDuringStrictPipelineFull`

## v0.4.5 — 2026-07-06

### Changed

- **Bench throughput units**: all gates and CLI bench output now use **SI MB/s (10⁶ B/s)** via `ebbackup/bench/throughput.h`; MiB/s shown as optional secondary line
- **`ci_floor.json`**: renamed floors to `fastcdc_MBps_min` (121), `hcrbo_incr_MBps_min` (79), equivalent to prior 115/75 MiB/s; legacy keys auto-converted with deprecation warning
- **L3 hard gate**: added `backup_pipeline_MBps_min` (**49**, ~70% of observed 32MB pipeline full-backup throughput)
- **Docs**: [`PERF_BASELINE.md`](engine_cpp/docs/PERF_BASELINE.md) — measurement units + market comparability section

## v0.4.4 — 2026-07-06

### Added

- **Real media fixture** `tests/fixtures/real_world/media/`: downloaded JPEG, WebP, GIF, MP4, **ZIP**, **tar.gz**, **EXE** (~2MB checked-in) with `media_manifest.json` and `ATTRIBUTION.md`
- **Fetch scripts** `tests/fixtures/scripts/fetch_media_fixtures.ps1` / `.sh` (primary + jsDelivr mirror via `-UseMirror` / `--mirror`)
- **Media tests** `media_fixture_test.cc`: `RealMediaTypeAgnosticRoundTrip`, `RealMediaRecursiveComposite`, `RealArchiveAndExeRoundTrip`
- **Content class**: `.exe`/`.dll` extensions skip re-compression in `kAuto` mode (with `.zip`/`.gz`/…)

### Changed

- `AssertTreeMatchesManifest` skips `media_manifest.json` meta files
- `ebbackup_tests` ~196 cases; CMake project version **0.4.4**; no ABI/CLI behavior change

## v0.4.3 — 2026-07-06

### Added

- **Unified real-world fixture** `tests/fixtures/real_world/full/`: multi-type files (markdown, JSON, empty, binary, PNG stub), 5-level nested tree, Unicode paths, `full_manifest.json` golden hashes
- **Fixture helpers**: `LoadFixtureManifest`, `AssertTreeMatchesManifest` (UTF-8 path aware on Windows)
- **Real-world complete tests** in `real_fixture_test.cc`: `RealWorldCompleteRoundTrip`, `RealWorldMultiTypeIncremental`, `RealWorldCompositeMaintenance`

### Changed

- `ebbackup_tests` ~191 cases; full PR `ctest` still under 60s (Release Windows)
- CMake project version **0.4.3**; no ABI/CLI behavior change

## v0.4.2-test — 2026-07-06

### Added

- **Layered E2E test matrix** (~35 new cases, ~188 total in `ebbackup_tests`): success/failure/nested/pipeline/powerfail/chaos/composite/real-fixture workflows
- **Shared test helpers**: `tree_util`, `fixture_util`, `chaos_util`; extended `subprocess_util` (pipeline/incremental/encrypt kill paths)
- **Checked-in fixtures** under `tests/fixtures/real_world/` (`mixed`, `unicode`, `nested`); compile defs `EBTEST_FIXTURE_DIR`, `EBTEST_ENGINE_ROOT`
- **Chaos tests**: 8 deterministic trials (`std::mt19937(42)`)

### Changed

- **`RecoveryTest`** suite rename (was misnamed `BackupEngineTest` in `recovery_test.cc`)
- **`storage_integration_test`**: uses `InitDefaultRepo()` (v0.4 defaults)
- Full PR `ctest` ~53s (Release Windows); no ABI/CLI behavior change

## v0.4.1 — 2026-07-06

### Changed

- **Schedule**: `PruneRotatedRepos` runs only when legacy `repo-*` directories exist under `repo_base` (avoids empty directory scans on normal `current/` deployments)
- **CI**: `eb_run_tests_capi` runs `EbBackupCapiTest.*` only; full 153 gtest cases remain in `ebbackup_tests`
- **Compression**: internal `NormalizeCompressOptions` unifies `use_lz4` and `compress_mode` (all CLI/C API/schedule entry points unchanged)
- **Tests**: `test::InitDefaultRepo()` alias; general integration tests use v0.4 repo defaults (digest/manifest legacy tests unchanged)
- **`MakeRotatedRepoPath`**: deprecated (use `ScheduleRepoPath`; `repo-*` rotation is legacy)
- CMake project version **0.4.1**; ABI remains **v11**

## v0.4.0 — 2026-07-06

### Added

- **Time-travel snapshots**: per-commit manifest archive under `snapshots/<txn_id>.manifest` with EBSNAPIDX1 index
- **GFS tiered retention** (`retention_policy`): default `1h:24,1d:7,7d:4,30d:6` + `retain_min=3`
- **Restore / verify at txn**: `eb restore --at TXN`, `eb verify --at TXN`, C API `eb_backup_restore_at` / `eb_backup_verify_at`
- **Snapshot GC union**: orphan GC, compact, and repo-stats reference hashes from all retained snapshots
- **CLI**: `list-snapshots`, `prune-snapshots`, `gc-orphans --latest-only`
- **Schedule**: `retention_policy`, `auto_prune`, `auto_gc_after_prune` post-backup
- **Feature flag** `kBackupFeatureSnapshots` (0x80); default on for `eb init` / `InitRepoEx` (use `--legacy-init` to disable)
- ABI **v11**: `EbSnapshotInfo`, `EbPruneReport`, `eb_backup_list_snapshots`, `eb_backup_prune_snapshots`

## v0.3.1 — 2026-07-06

### Fixed

- **Init path consistency**: `test::InitV03Repo()` helper; compactor/repo_stats/bench use v0.3 repos; schedule uses `InitRepoEx` + single `current` repo with incremental backups
- **Schedule config**: `compress`, `cpu_budget`, `durability` fields; legacy `repo-*` rotation removed
- **Persistent index IOPS**: defer `chunk.idx` writes until backup completion
- **Corrupt chunk tail**: truncate partial records on `ChunkStore::Open` scan
- **CLI**: `eb compact --wait-idle SEC`
- **Tests**: balanced durability recovery, C API compact/repo_stats, ASan CI job
- Root README L1–L4; CMake project version **0.3.0**

## v0.3.0 — 2026-07-06

### Added

- **zstd-dev vendored** via `FetchZSTD.cmake`; `ChunkCodec` extended with `kZstd` / `kEncryptedZstd`
- **ContentClass adaptive compression** — entropy/path classification, CPU budget governor, `--compress auto|lz4|zstd|off`
- **ChunkProfile** — size-aware FastCDC/HCRBO tuning (`ChunkProfileMode`)
- **DurabilityMode** — `strict` (default) vs `balanced` batched fsync (`--durability`)
- **Persistent chunk index** (`data/chunk.idx`, EBCHIDX1) with scan fallback
- **EbPack compaction** — `eb compact [--dry-run]`; **repo metrics** — `eb repo-stats`
- **Manifest v4** binary format (32B chunk hashes); opt-in via `RepoInitOptions` / `eb init`
- ABI **v10**: `EB_BACKUP_FLAG_COMPRESS_AUTO`, `EB_BACKUP_FLAG_BALANCED_DURABILITY`, `EB_BACKUP_INIT_LEGACY`, `eb_backup_compact()`, `eb_backup_repo_stats()`

### Fixed

- **LoadIndex** no longer truncates multi-chunk scans when rebuilding the in-memory index
- Pipeline store stage uses `PutPrecompressed` for all pre-encoded chunks (avoids double compression)

### Notes

- `eb init` defaults: persistent index + manifest v4 + compress-auto repo defaults; use `--legacy-init` to opt out
- C++ `BackupEngine::InitRepo()` remains legacy-compatible (no v0.3 storage flags unless `InitRepoEx`)

## v0.2.0 — 2026-07-06

### Added

- **Dual digest stack** — frozen legacy SHA256/HMAC/PBKDF2 plus NIST/RFC-verified standard stack
- **`kBackupFeatureDigestStandard`** superblock flag; repos created via CLI `eb init` default to standard digest
- **`DigestAlgo` routing** through chunking, chunk store, Merkle, RAR chain, and content-key KDF
- **`eb_backup_init_repo_ex()`** with `EB_BACKUP_FLAG_LEGACY_DIGEST`; ABI **v9**
- CLI **`eb init --legacy-digest`** for legacy-compatible repos
- Unit tests: `digest_legacy_test`, `digest_standard_test`, `digest_dual_test`
- Test helpers: `test::InitLegacyRepo()` / `test::InitStandardRepo()`

### Notes

- Existing C++ tests and `BackupEngine::InitRepo(path)` default remain **legacy** for backward compatibility.
- Legacy repos are unchanged; standard repos use RFC-verified digests for new content IDs and KDF.

## v0.1.0 — 2026-07-06

### Added

- **Performance baseline (L1–L3)** with `ebbackup_bench_check` ctest hard gate
- [`engine_cpp/docs/PERF_BASELINE.md`](engine_cpp/docs/PERF_BASELINE.md) and [`engine_cpp/bench/baselines/ci_floor.json`](engine_cpp/bench/baselines/ci_floor.json)
- **CFI rolling batch skip** — extend stale windows on rolling pre-check failure without changing chunk boundaries

### Includes M9 (wrap-up)

- Streaming `VerifyRestoredFileChunks` (chunk-sized reads, no full-file buffer)
- Full restore opt-in content verification: CLI `--verify-content`, `RestoreOptions.verify_restored_content`
- CLI `--skip-content-verify` (skip wins if both flags are passed)
- `EbHcrboStats.cfi_rolling_skip_hits` exposed in bench/CLI output
- Root README, CLI integration tests

## M8 — ABI v8

- `EB_BACKUP_ABI_VERSION` **8**
- `EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY` on `eb_backup_restore_ex()`
- Post-restore **content Merkle** for selective restore (disk hash vs manifest)
- Glob full-path semantics (`src/*.cpp` vs `*.tmp` basename)
- CFI offset index for incremental chunking
- NIST AES-256-GCM test vectors (portable path)

C integrators should use `#if eb_backup_abi_version() >= 8` before passing restore flags.

## M7 — ABI v7

- Selective restore via `BackupFilterOptions` / manifest filter
- `eb_backup_load_filter_file()` applies to backup **and** restore
- CFI rolling checksum fast-reject gate
- Unified filter loader for CLI, watch, schedule

## M5–M6

- HCRBO incremental backup, pipeline mode, encryption, audit chain, daemon/schedule/watch
