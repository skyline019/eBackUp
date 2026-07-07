# ebbackup 技术发展史归档（v0.1 → v0.9.4）

本文档汇总 **recoveryProjects / ebbackup** 备份引擎从 M5 内核里程碑到 v0.9.4 的功能演进、架构变迁、ABI 迭代、测试与性能门禁发展史。逐条 commit 级明细以 [`CHANGELOG.md`](../CHANGELOG.md) 与 [`CHANGELOG_ARCHIVE.md`](CHANGELOG_ARCHIVE.md) 为权威来源；本文为结构化归档索引。

---

## 一、产品定位（恒定约束）

| 约束 | 说明 |
|------|------|
| Content ID | `SHA256(content)`，不因 digest 栈、pipeline 路径或 CDC 实现而改变语义 |
| 提交点耐久性 | manifest commit 前 `Flush`/fsync；中断 txn → `kAborted` / 前一 snapshot |
| FSM 阶段 | `Idle → Scanning → Chunking → Storing → CommittingMeta → Auditing → Complete/Aborted` |
| 双槽 superblock | `superblock.bin` 双 4KB slot，CRC 选举 |
| Immutable CI | `reuse_pct_min≥90`、`pipeline_ratio*_min≥0.90` 不可降低 |

---

## 二、版本时间线总览

```
M5–M6 (内核)     HCRBO / Pipeline / 加密 / 审计链 / daemon
    ↓
v0.1.0           L1–L3 性能门禁 + CFI rolling batch skip + M9 收尾
    ↓
v0.2.0           双 Digest 栈（Legacy / Standard）
    ↓
v0.3.0           ContentClass / zstd / 持久化索引 / compact / repo-stats
v0.3.1           Init 路径统一 / schedule 单 repo / corrupt tail 修复
    ↓
v0.4.0           Snapshot 时间旅行 + GFS 保留策略
v0.4.1–v0.4.2    Schedule/CI 打磨 + 分层 E2E 测试矩阵
v0.4.3–v0.4.4    Real-world / media fixture
v0.4.5–v0.4.6    Bench SI MB/s + L3 绝对门禁 + 提交点耐久性 + mmap pipeline
    ↓
v0.5.0 (Wave 2)  DigestPool / SIMD CDC / CFI Bloom / EbPack / Coalesced meta
    ↓
v0.6.0           默认 EbPack 产品化 + 运维闭环 + Pipeline v2 双 Store worker
    ↓
v0.7.0 (Wave 3)  EbPack 16-shard + SHA-NI + Pipeline v3 + 流式 CDC
v0.8.0 (Wave 4)  Pipeline v4 chunk 队列 + store 去锁 + fsync 去重 + L7 multi-file
    ↓
v0.9.0 (Wave 5)  ChunkStore 并发修复 + Stage 3 分层门禁 + L3a/L3b 拆分
v0.9.1           Stage 3.1 / 3.2 双轨 floor（并入 v0.9.0 CHANGELOG）
v0.9.2           StreamingChunkCpuPipeline（>32MB 单文件流式 CDC）
v0.9.3           digest_base 批处理 + DigestPool span 分片
v0.9.4 (Sprint4) CDC Phase B seg1 bulk + Phase A FastCdcSlice opt-in
    ↓
v0.9.5           Desktop Workbench GUI（Tauri + Vue + ebbackup_workbench.dll）
```

**当前版本**：CHANGELOG **v0.9.5** · C API `EB_BACKUP_ABI_VERSION = 12` · ctest **233** gtest（含空仓库 repo-stats）+ C API + bench L1–L7 + Workbench Rust 集成测试 · CMake `project VERSION 0.6.0`（与 CHANGELOG 版本号不同步，以 CHANGELOG 为准）

---

## 三、里程碑阶段（M5–M9，v0.1 之前/同期）

### M5–M6 — 核心备份内核

- **HCRBO** 增量分块：CFI 锚点 + rolling checksum 快速拒绝
- **四阶段 Pipeline**：Reader → Chunker → Compressor → Store
- **AES-256-GCM** 内容加密 + PBKDF2 密钥派生
- **RAR 审计链** + Merkle manifest 根
- **Daemon**：`schedule` / `watch` 定时与文件监视备份
- **CLI** `eb`：init / backup / verify / restore / recover

### M7 — ABI v7

- **选择性恢复**：`BackupFilterOptions`（include/exclude path/glob）
- `eb_backup_load_filter_file()` 同时作用于 backup 与 restore
- **CFI rolling checksum** 快速拒绝门
- 统一 filter loader（CLI / watch / schedule）

### M8 — ABI v8

- `EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY`
- 选择性恢复后 **内容 Merkle 校验**（磁盘 hash vs manifest）
- Glob 语义：`*.tmp` 匹配 basename；`src/*.cpp` 匹配相对路径
- **CFI offset index** 加速增量分块
- NIST AES-256-GCM 测试向量（portable 路径）

### M9（并入 v0.1.0）

- 流式 `VerifyRestoredFileChunks`（按 chunk 读，不全文件缓冲）
- 全量恢复可选内容校验：`--verify-content` / `RestoreOptions.verify_restored_content`
- CLI `--skip-content-verify`
- `EbHcrboStats.cfi_rolling_skip_hits` 统计暴露

---

## 四、逐版本技术档案

### v0.1.0 — 性能基线与 CFI 增强

**主题**：建立可回归的性能门禁；完善增量分块跳过统计。

| 领域 | 交付 |
|------|------|
| Bench | `ebbackup_bench_check` ctest 硬门禁 L1–L3 |
| 文档 | `PERF_BASELINE.md`、`ci_floor.json` |
| 算法 | CFI rolling batch skip（rolling 预检失败扩展 stale 窗口，边界不变） |
| 恢复 | 流式内容校验、CLI verify-content 开关 |
| 测试 | CLI 集成测试、bench 输出 `cfi_rolling_skip_hits` |

---

### v0.2.0 — 双 Digest 栈

**主题**：Legacy 冻结栈与 NIST/RFC 标准栈并存，按 repo 路由。

| 领域 | 交付 |
|------|------|
| 算法 | `DigestAlgo::kLegacy` / `kStandard`；`digest_legacy.cc` + `digest_standard.cc` |
| Superblock | `kBackupFeatureDigestStandard` (0x08) |
| C API | ABI **v9**；`eb_backup_init_repo_ex()` + `EB_BACKUP_FLAG_LEGACY_DIGEST` |
| CLI | `eb init --legacy-digest` |
| 默认 | CLI/C API init → Standard；C++ `InitRepo()` → Legacy（兼容） |
| 测试 | `digest_legacy_test`、`digest_standard_test`、`digest_dual_test` |

---

### v0.3.0 — 现代存储与自适应压缩

**主题**：从 append-only chunk 文件进化为可运维的现代仓库形态。

| 领域 | 交付 |
|------|------|
| 压缩 | zstd（`FetchZSTD.cmake`）；ContentClass 熵/路径分类；`--compress auto\|lz4\|zstd\|off` |
| 分块 | ChunkProfile 按文件大小调 FastCDC/HCRBO 参数 |
| 耐久性 | `DurabilityMode` strict / balanced；`--durability` |
| 索引 | 持久化 `data/chunk.idx`（EBCHIDX1）；scan fallback |
| 运维 | `eb compact`、`eb repo-stats`；legacy chunk 重写 compact |
| Manifest | v4 二进制（32B chunk hash） |
| C API | ABI **v10**；`EB_BACKUP_INIT_LEGACY`、`eb_backup_compact()`、`eb_backup_repo_stats()` |
| 修复 | LoadIndex 多 chunk scan 截断 bug；Pipeline `PutPrecompressed` 避免二次压缩 |

**默认 init（CLI/C API）**：persistent index + manifest v4 + compress-auto

---

### v0.3.1 — Init 与 Schedule 一致性

**主题**：测试/生产路径对齐；降低持久化索引写放大。

| 领域 | 交付 |
|------|------|
| 测试 | `InitV03Repo()` helper；compactor/bench 统一 v0.3 repo |
| Schedule | 单 repo `<repo_base>/current`；增量链；`compress`/`cpu_budget`/`durability` 配置 |
| 性能 | defer `chunk.idx` 写入至 backup 完成 |
| 健壮性 | corrupt chunk tail 在 `Open()` scan 时截断 |
| CLI | `eb compact --wait-idle SEC` |
| CI | ASan job；balanced durability recovery 测试 |

---

### v0.4.0 — Snapshot 时间旅行

**主题**：多版本 manifest 归档 + GFS 分层保留。

| 领域 | 交付 |
|------|------|
| 存储 | `snapshots/<txn>.manifest` + `snapshots/index`（EBSNAPIDX1） |
| 保留 | GFS tiers `1h:24,1d:7,7d:4,30d:6` + `retain_min=3` |
| CLI | `list-snapshots`、`prune-snapshots`、`restore --at`、`verify --at`、`gc-orphans --latest-only` |
| GC/Compact | 引用 hash 取自**所有保留 snapshot** 的并集 |
| Schedule | `retention_policy`、`auto_prune`、`auto_gc_after_prune` |
| Feature | `kBackupFeatureSnapshots` (0x80) |
| C API | ABI **v11**；`eb_backup_list_snapshots`、`eb_backup_prune_snapshots` |

---

### v0.4.1 — v0.4.6 — 测试矩阵与 Bench 标准化

| 版本 | 要点 |
|------|------|
| v0.4.1 | Schedule/CI 打磨；C API 独立 test target |
| v0.4.2 | ~188 测试：E2E + chaos + powerfail 分层矩阵 |
| v0.4.3 | Real-world fixture（多类型 + 5 层嵌套 + Unicode） |
| v0.4.4 | Media fixture（JPEG/WebP/MP4/ZIP 等 ~2MB） |
| v0.4.5 | 吞吐统一 **SI MB/s**；L3 绝对门禁初值 49 |
| v0.4.6 | 提交点耐久性 fsync；mmap pipeline；L3 floor **53** MB/s |

---

### v0.5.0 — Wave 2 高压性能（技术预研 → 可选特性）

**主题**：算法/存储/元数据三层优化；特性通过 `InitV05Repo` 显式开启。

| Track | 交付 |
|-------|------|
| **A 算法** | `DigestPool` 并行 SHA256；`fast_cdc_simd.cc` AVX2；`cfi_bloom` 负向预滤；dedup-before-encode |
| **B 存储** | **EbPack** (`0x100`)：8MB pack blob → `data/packs/`；chunk index **v2**（HXID + pack_name） |
| **C 元数据** | **Coalesced meta** (`0x200`)：备份中 superblock 仅内存更新；abort/idle 才落盘 |
| **D 门禁** | L5 256MB pipeline bench；floor `backup_pipeline_256MBps_min: 84` |

---

### v0.6.0 — 默认 EbPack 产品 + Pipeline v2

**主题**：v0.5 技术栈上升为产品默认；硬门禁驱动优化。

| Track | 交付 |
|-------|------|
| **默认 init** | CLI/CAPI/daemon → `ebpack + coalesced_meta` |
| **Pipeline v2** | 双 Store worker；`queue_depth=32` |
| **门禁** | L3 **101** MB/s、L5 **97** MB/s |
| **ABI** | **v12**；breaking：新 init 默认 EbPack |

---

### v0.7.0 — Wave 3 极致性能

| Track | 交付 |
|-------|------|
| **EbPack shard** | 16 分片；`pack-{txn}-s{shard}.ebpack` |
| **SHA-NI** | `digest_sha_ni.cc` runtime CPUID dispatch |
| **Pipeline v3** | `FileScheduler` 图着色；N×(Reader/Chunker/Compressor) |
| **流式 CDC** | `FastCdcStreamFeed` 与 `FastCdcSlice::Chunk` bit-identical cut points |
| **门禁** | L6 **2GB** pipeline bench |

---

### v0.8.0 — Wave 4 E2E 吞吐

| Track | 交付 |
|-------|------|
| **Pipeline v4** | 全局 chunk/encoded 队列；`FileAggregator` |
| **Store** | `shard_index_mu_[16]`；EbPack append-only fd |
| **Profile** | `PipelinePhaseStats`；`EBBACKUP_PIPELINE_PROFILE=1` |
| **门禁** | L7 32×32MB multi-file；floors L3 **70**、L5/L6 **100**、L7 **120** MB/s |

**Release Windows 实测**：L3 ~118、L5 ~146、L6 ~153、L7 ~554 MB/s

---

### v0.9.0 — Wave 5 正确性 + Stage 3 分层

**主题**：修复多 worker store 并发；建立 Stage 3.1/3.2 双轨门禁。

| Track | 交付 |
|-------|------|
| **ChunkStore 并发** | `shard_index_[16]` 独立 map；`ForEachRecord` snapshot 回调；tombstone 锁序 |
| **Pipeline 竞态** | `finalized` atomic；`RecordStoredChunkHash` under `finalize_mu` |
| **L3a 快路径** | ≤32MB 单文件 `worker_count==0` → `ChunkPendingFiles` + `StorePendingChunks` |
| **Stage 3.1** | L5 **105**、L6 **112**、L7 **550** MB/s；L3 绝对 disabled |
| **Stage 3.2** |  aspirational L5 **280**、L6 **300**（nightly only） |
| **L3a/L3b 拆分** | L3a = 32MB adaptive ratio；L3b = 256MB pipeline ratio |

**实测**：L5 ~142–149 MB/s；L3b ratio ~0.96–0.98

---

### v0.9.2 — StreamingChunkCpuPipeline

**主题**：大文件单线程 feed + 有限 encode/store worker，保留 feed 级 overlap。

| 领域 | 交付 |
|------|------|
| **拓扑** | 单文件 >32MB、full backup、`worker_count==0` → `RunSingleFileStreamingChunkPipeline` |
| **Carry ring** | `StreamSegmentView` 零拷贝虚拟段；bounded carry tail |
| **Sub-timing** | `stream_cdc_ns` / `stream_digest_ns` / `stream_carry_ns` |
| **测试** | `streaming_chunk_pipeline_test.cc`；256MB feed parity |

**实测**：L5 ~154 MB/s；L3b ratio ~1.04

---

### v0.9.3 — digest_base 批处理

**主题**：carry feed 上 file-absolute DigestPool 批处理，消除 per-chunk 串行 fallback。

| 领域 | 交付 |
|------|------|
| **digest_base** | `FastCdcStreamState::digest_base`；`DigestPool::HashRegions` on mmap |
| **Span sharding** | `span_count >= threads_` 时并行 span hash |
| **Deferred** | CDC carry-boundary contiguous scan（gear state handoff  parity 失败回退） |

**实测**：L5 ~159–163 MB/s；stream digest share **~16%**（v0.9.2 ~38%）

---

### v0.9.4 — Sprint 4 CDC 双路径

**主题**：Phase B 默认 stream path seg1 bulk；Phase A whole-file FastCdcSlice opt-in。

| 领域 | 交付 |
|------|------|
| **Phase B** | `ChunkCarryPrefixVirtual`：`pos >= view.len0` 后切换 seg1-only contiguous CDC |
| **Phase A** | `UseCdcFastPath` + `ChunkFileStreamingFastSlice`；`EBBACKUP_CDC_FAST_SLICE=1` |
| **Opt-out** | `EBBACKUP_FORCE_STREAM_CDC=1` 强制 stream-feed CDC |
| **API** | `FastCdcSlice::ChunkCuts()` |
| **测试** | `CdcFastPathMatchesStreamFeedManifest` |

**实测**：L5 **~170–172 MB/s**；L6 **~186 MB/s；L3b ratio ~1.01–1.05**

**设计决策**：Phase A 默认关闭——whole-file CDC 在现有拓扑下失去 8×32MB feed/encode overlap（~105 vs ~172 MB/s）。

---

### v0.9.5 — Desktop Workbench GUI

**主题**：可选桌面 GUI 入口；JSON shim DLL + Tauri 宿主；文档正式归档。

| 领域 | 交付 |
|------|------|
| **GUI** | `gui/` Tauri 2 + Vue 3；六 Activity；壁纸 + 透明度调节器 |
| **Native** | `ebbackup_workbench` SHARED + `engine_cpp/workbench/` JSON shim |
| **测试** | Rust 集成测试 roundtrip；`RepoStatsTest.EmptyInitializedRepoWithoutManifest` |
| **Release** | NSIS 0.1.0 x64；`npm run build:desktop` |
| **文档** | [`docs/product/WORKBENCH_GUI.md`](product/WORKBENCH_GUI.md) |

---

## 五、仓库布局演进

| 路径 | 引入版本 | 说明 |
|------|----------|------|
| `manifest` | M5 | v2/v3 文本 → v4 二进制（v0.3） |
| `data/chunks` | M5 | Legacy append-only chunk 文件 |
| `data/chunk.idx` | v0.3 | 持久化索引 EBCHIDX1 → v2 EbPack 条目（v0.5） |
| `data/packs/*.ebpack` | v0.5 → **v0.6 默认** | 8MB pack blob；v0.7 16-shard |
| `snapshots/` | v0.4 | 时间旅行 manifest 归档 |
| `superblock.bin` | M5 | 双槽 FSM + `backup_features`（v0.6 扩 uint32_t） |
| `audit/rar.chain` | M5 | 哈希链审计 |
| `crypto.salt` | M6 | 加密仓库盐 |

---

## 六、C API / ABI 演进表

| ABI | 版本 | 关键新增 |
|-----|------|----------|
| v7 | M7 | 选择性恢复 filter；CFI rolling gate |
| v8 | M8 | `EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY`；内容 Merkle 校验 |
| v9 | v0.2 | `eb_backup_init_repo_ex`；`EB_BACKUP_FLAG_LEGACY_DIGEST` |
| v10 | v0.3 | compress-auto/zstd/balanced；`eb_backup_compact`；`eb_backup_repo_stats` |
| v11 | v0.4 | snapshot list/prune/restore_at/verify_at |
| v12 | v0.6 | init 默认 EbPack；`EB_BACKUP_FLAG_NO_PIPELINE` |

v0.7–v0.9.4 **ABI 保持 v12**，无 breaking API 变更。

---

## 七、Feature Flags 演进

| Flag | 值 | 版本 | 含义 |
|------|-----|------|------|
| `kBackupFeatureMeta` | 0x01 | M5 | 扩展文件元数据 |
| `kBackupFeatureSpecialFiles` | 0x02 | M5 | 符号链接/FIFO 等 |
| `kBackupFeatureEncrypted` | 0x04 | M6 | 内容加密 |
| `kBackupFeatureDigestStandard` | 0x08 | v0.2 | NIST/RFC digest 栈 |
| `kBackupFeaturePersistentIndex` | 0x10 | v0.3 | `data/chunk.idx` |
| `kBackupFeatureManifestBinary` | 0x20 | v0.3 | Manifest v4 |
| `kBackupFeatureBalancedDurability` | 0x40 | v0.3 | balanced 模式 |
| `kBackupFeatureSnapshots` | 0x80 | v0.4 | 时间旅行 snapshot |
| `kBackupFeatureEbPack` | 0x100 | v0.5 → **v0.6 默认** | Pack 存储 |
| `kBackupFeatureCoalescedMeta` | 0x200 | v0.5 → **v0.6 默认** | 合并 superblock 写入 |

---

## 八、性能门禁（Bench L1–L7）发展史

| Level | 引入 | 工作负载 | v0.9.4 Stage 3.1 floor | v0.9.4 实测 |
|-------|------|----------|------------------------|-------------|
| **L1** | v0.1 | 64MB FastCDC + HCRBO | fastcdc 121, hcrbo 79 MB/s | — |
| **L2** | v0.1 | 1-byte edit @ 5MB | reuse ≥ 90% | — |
| **L3a** | v0.9 | 32MB adaptive ratio | ratio ≥ 0.90 | ~1.17–1.44 |
| **L3b** | v0.9 | 256MB streaming vs seq | ratio ≥ 0.90 | ~1.01–1.05 |
| **L5** | v0.5 | 256MB pipeline | **105** MB/s | **~170–172** |
| **L6** | v0.7 | 2GB pipeline | **112** MB/s | **~186** |
| **L7** | v0.8 | 32×32MB multi-file | **550** MB/s | ~646–658 |
| **L4** | v0.3 | EbPack compact + ContentClass | ampl ≤1.05, auto/lz4 ≤1.10 | — |

详见 [`reference/PERF_BASELINE.md`](reference/PERF_BASELINE.md)。

---

## 九、测试体系演进

| 阶段 | 规模 | 亮点 |
|------|------|------|
| M5–M6 | ~数十 | 核心 engine/store/pipeline 单测 |
| v0.4.2 | ~188 | 分层 E2E + chaos + powerfail |
| v0.5 | ~206 | DigestPool、EbPack、CoalescedMeta |
| v0.6 | 207 | EbPackCompactTest |
| v0.8 | 220+ | PipelineV4 parity + L7 |
| **v0.9.4** | **232** | streaming_chunk_pipeline + CDC fast path parity |
| **v0.9.5** | **233** gtest + Workbench Rust IT | 空仓库 repo-stats；`gui/` 桌面 Workbench |

详见 [`technical/TEST_AND_CI.md`](technical/TEST_AND_CI.md)。

---

## 十、架构演进（备份路径）

### v0.6 默认路径

```
Source → [Pipeline 4-stage ×2 Store workers]
           Reader(mmap) → Chunker → Compressor → Store → EbPack
```

### v0.8 Pipeline v4（多文件 / 显式 workers）

```
FileScheduler → N×(Reader → Chunker → Compressor) → global chunk queue
                                                      → M Store workers
                                                      → FileAggregator
```

### v0.9.4 单文件拓扑（`worker_count==0`）

```
≤32MB  → RunSingleFileInlinePipeline (L3a sequential fast path)
>32MB  → RunSingleFileStreamingChunkPipeline (default)
           main: 32MB FastCdcStreamFeed + Phase B seg1 bulk
           workers: 2 encode + 1 store
         optional: ChunkFileStreamingFastSlice (EBBACKUP_CDC_FAST_SLICE=1)
multi-file / incremental / workers>0 → Pipeline v4
```

详见 [`technical/ARCHITECTURE.md`](technical/ARCHITECTURE.md)、[`technical/CHUNK_AND_CDC.md`](technical/CHUNK_AND_CDC.md)。

---

## 十一、Init 默认行为对照表

| 入口 | v0.3 | v0.4 | v0.6+ |
|------|------|------|-------|
| `eb init` | persistent index + manifest v4 | + snapshots | + **EbPack + coalesced meta** |
| `eb_backup_init_repo_ex(0)` | 同左 | 同左 | 同左 |
| `InitDefaultRepo()` (测试) | v0.3 | v0.4 | **V05 / v0.6** |
| `InitV03Repo()` / `--legacy-init` | legacy chunks | 无 snapshot | 无 EbPack |

---

## 十二、相关文档索引

| 文档 | 路径 |
|------|------|
| 文档归档入口 | [`README.md`](README.md) |
| 版本变更明细 | [`CHANGELOG.md`](../CHANGELOG.md) |
| 引擎 / CLI 手册 | [`engine_cpp/README.md`](../engine_cpp/README.md) |
| 性能基线 | [`reference/PERF_BASELINE.md`](reference/PERF_BASELINE.md) |
| 桌面 Workbench GUI | [`product/WORKBENCH_GUI.md`](product/WORKBENCH_GUI.md) |
| CI floor | [`engine_cpp/bench/baselines/ci_floor.json`](../engine_cpp/bench/baselines/ci_floor.json) |

---

*归档日期：2026-07-07 · 对应产品版本 v0.9.4*
