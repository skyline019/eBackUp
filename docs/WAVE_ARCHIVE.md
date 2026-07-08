# Wave 深化归档（Wave 1 → Wave T）

本文档为 **recoveryProjects / ebbackup** 全部 Wave 深化的结构化归档，覆盖性能 sprint（Wave 1–5）与能力 sprint（Wave A–T）。逐 commit 明细仍以根目录 [`CHANGELOG.md`](../CHANGELOG.md) 为准；工程路线图见 [`technical/CAPABILITY_ROADMAP.md`](technical/CAPABILITY_ROADMAP.md)；缺口闭合见 [`product/BACKUP_CAPABILITY_GAPS.md`](product/BACKUP_CAPABILITY_GAPS.md)。

**当前快照（2026-07-09）**：C API **ABI v37** · gtest **393** · Workbench IT **13** · ctest 含 `ebsync_tests` + bench · L5 ~170 MB/s · L7 ~650 MB/s · CI sync+GUI

---

## 命名约定

| 系列 | 范围 | 含义 | 主要文档 |
|------|------|------|----------|
| **Wave 1** | 根基 v0.1–0.4 | bench 制度化、manifest v4、snapshot、durability | ecosystem ch.5–6 |
| **Wave 2–5** | 性能 v0.5–0.9.4 | DigestPool、EbPack、Pipeline v4、Streaming、Sprint 4 | ecosystem ch.7–8 |
| **Wave A–U** | 能力 Phase 0–13 | 可证明备份、Win 元数据、多 Job、DR、平台、垂直、智能排除、备份窗口、**分层压缩** | 本文 §能力 Wave |

两套编号**并行**：Wave 2–5 解决 **P1 吞吐**；Wave A–U 解决 **P4 可交付 + 产品缺口**。

---

## 第一部分：性能 Wave（Wave 1–5）

### Wave 1 — 内核奠基与 bench 立法（M5 → v0.4.x）

| 项 | 内容 |
|----|------|
| 压力 | P3 正确性 + P4 可运维；P1 尚无绝对指标 |
| 交付 | HCRBO 四阶段 Pipeline；AES-GCM；RAR；daemon；Filter + Merkle（ABI v7–v8） |
| v0.1.0 | L1–L3 bench 硬门禁；reuse ≥90%；`ci_floor.json` |
| v0.3.0 | manifest v4 二进制；持久化 HXID 索引；zstd（ABI v10） |
| v0.4.0 | Snapshot 时间旅行；GFS 保留（ABI v11） |
| 剪枝 | 无 cloud 同步；无 premature 性能优化 |
| LaTeX | `ecosystem-evolution` ch.5–6；`kernel-manual` ch.1 |

### Wave 2 — 技术预研 opt-in（v0.5.0）

| Track | 交付 |
|-------|------|
| A | DigestPool、SIMD CDC、CFI Bloom、dedup-before-encode |
| B | EbPack 8MB pack + HXID v2 |
| C | Coalesced meta（减 superblock fsync） |
| D | L5 floor 84 MB/s（~70% 实测） |
| 风险 | `uint8_t` feature 截断（v0.6.0 修） |

### Wave 3 — 硬件与并行（v0.7.0）

- 16-shard EbPack；SHA-NI runtime dispatch
- Pipeline v3 + `FastCdcStreamFeed`
- L6 2GB 门禁；ABI v12

### Wave 4 — Pipeline v4 拓扑（v0.8.0）

- 全局 chunk/encoded 队列 + `FileAggregator`
- `PipelinePhaseStats`；L7 32×32MB 门禁 ~554 MB/s
- 负向信号：单文件 L5 仍 ~146 MB/s → 为 Wave 5 分化铺路

### Wave 5 + Sprint 4（v0.9.0–0.9.4）

| 版本 | 主题 | 关键数据 |
|------|------|----------|
| v0.9.0 | ChunkStore 16-shard 并发修复 | P3 heap corruption 归零；Stage 3.1 L5=105 |
| v0.9.2 | StreamingChunkCpuPipeline | L5 ~154；chunk 占比 98%→83% |
| v0.9.3 | digest_base 批处理 | L5 ~161；digest 38%→16% |
| v0.9.4 | CDC Phase B seg1 bulk | L5 ~171；FastCdcSlice opt-in |
| Sprint 4 | Phase A/B 双路径 | A 默认关（-39% MB/s） |

---

## 第二部分：能力 Wave（Wave A–T）

### Wave A — 缺口基线与归档治理（Phase 0）

- `BACKUP_CAPABILITY_GAPS.md` 缺口表
- `CAPABILITY_ROADMAP.md` 分阶段路线图
- `docs/README.md` 索引

### Wave B — Manifest v5 Windows 元数据（Phase 3 / ABI v16）

- `EBMANIFEST5`：`security_descriptor_b64`、`inode_id`、`reparse_tag`、`reparse_target`、`stream_name`
- `win_meta.cc` 扫描/恢复 enrichment
- GUI：ACL 策略 + 联接点恢复策略

### Wave C — 尽力一致备份 Completion（Phase 4 / ABI v17）

- `scan_entry.cc`：`ScanResult`、深度上限、issue 收集
- 备份报告 `reports/<txn>.json`
- Hooks：`pre_backup_cmd` / `post_backup_cmd`
- C API：`eb_backup_get_backup_report_json`

### Wave D — 尽力一致深化（Phase 4 done）

- Issue 路径 skip chunk；Pipeline per-path skip
- 报告分桶：`reparse_junction` / `hook_failed`
- `BackupReportPanel.vue`；`eb_backup_set_backup_hooks`

### Wave E — Windows 元数据保真深化（Phase 3 done）

| GAP | 交付 |
|-----|------|
| WIN-01 | 硬链 dedup + `CreateHardLinkW` 恢复 |
| WIN-02 | Junction `reparse_target` + `RecreateReparsePoint` |
| WIN-03 | ACL best_effort + 验收 `issues[]` |
| WIN-04 | ADS alternate stream E2E |

### Wave F — 多 Job 内核（Phase 5 / ABI v18）

- `jobs.json` CRUD；`RunJob`；`exclude_globs`
- `snapshot_meta.jsonl`：`job_id`、`retention_tag`、`immutable_until`
- Prune WORM 门控

### Wave G — GUI Job 管理（Phase 5）

- `BackupView` 作业 CRUD/运行
- 快照/报告展示 `job_id` 与 WORM
- Prune audit key

### Wave I — Hybrid CDC（Sprint 5 / ABI v20 默认）

- 默认启用 Hybrid CDC；`EBBACKUP_CDC_HYBRID=0` opt-out
- bench `hybrid_stream_ratio` ≥ 0.95
- `hybrid_cuts_ns` / `hybrid_replay_ns` phase stats

### Wave K — Symlink remap + EBB v2（Phase 6 / ABI v21）

- `--symlink-remap-from/to`；remap JSON
- EBB v2 delta bundle：`export/import/apply_delta_json`

### Wave L — 规模与韧性（Phase 7 / ABI v22）

- Manifest browse sidecar `catalog/manifest_browse/<txn>.mbi`
- 持久化 Job 队列 `catalog/job_queue.jsonl`
- 扫描深度 + symlink 循环检测

### Wave M — 灾备闭环（Phase 6 / ABI v23）

- 三路合并就地恢复：base/target/live；`both_changed`
- RestoreView bulk conflict workstation
- `ebrecover` 最小 runtime

### Wave N — 平台合规 MVP（Phase 8 / ABI v24）

- 增量链可达性 + `eb verify-chain`
- RPO 摘要 + GUI 卡片
- Job 队列 GUI + daemon `queue_drain`
- 本地备份滞后告警

### Wave O — 运维可解释（Phase 8 / ABI v25）

- 孤儿解释 JSON + Maintenance 解释图
- GUI 破坏性 ops → RAR；`eb audit-ops list`

### Wave O+ — 常驻服务与多 Profile（Phase 8 / ABI v26）

- Windows SCM `eb service` + systemd unit/timer
- Daemon 可中断 stop
- Workbench 多 Profile（主题/最近仓库/告警分 profile）

### Wave P — 垂直插件（Phase 9 / ABI v27）

- `IBackupPlugin`：`sqlite_checkpoint` / `registry_hive` / `vhdx_scan`
- `jobs.json` `plugins[]`；CLI `eb plugin list`

### Wave Q — 插件打磨（仍 ABI v27）

- 报告 `plugins[]` + `plugin_skipped`/`plugin_failed`
- schedule `plugins=`；VHDX 条件 E2E gtest

### Wave R — 智能排除（Phase 10 / ABI v28）

- `SuggestExcludeFilters` 浅扫描 catalog
- `exclude_paths[]`；CLI `eb suggest-excludes`；`--include-ide`
- GUI 逐条采纳 + 作业预填

### Wave T — 备份窗口（Phase 11 / ABI v29）

- `window_start/end` + `deadline_grace_seconds` + `durability_adaptive`
- 窗外 Conflict；queue skip；deadline Strict→Balanced；超时截断
- GUI 作业对话框 + 报告字段

### Wave U — 分层压缩与可观测性（Phase 13 / ABI v30）

- **CompressTier**：fast / balanced / max；CLI `--compress-tier`、`--compress-level`
- **Zstd LDM** + **仓库字典**（`meta/zstd_dict.bin`）
- **`EbRepoStats`**：`compress_ratio`、`live_uncompressed_bytes`、`has_zstd_dict` 等
- **Decode fuzz**：`decode_corruption_test.cc`
- **CI**：`sync_cpp` + `gui` 纳入 GitHub Actions

---

## ABI 演进总表（v14–v30）

| ABI | Wave | 关键新增 |
|-----|------|----------|
| v14 | L | path index build/query, manifest page |
| v15 | L | snapshot diff, restore acceptance |
| v16 | B/E | manifest v5 Win meta |
| v17 | C/D | backup report JSON, hooks |
| v18 | F | multi-job config API |
| v19–v21 | K/M | in-place preview/apply, delta bundle, symlink remap |
| v22 | L | manifest browse sidecar, job queue |
| v23 | M | three-way merge, dry-run |
| v24 | N | reachability, RPO, queue drain, alerts |
| v25 | O | orphan explain, ops audit |
| v26 | O+ | Windows Service, multi-profile |
| v27 | P/Q | vertical plugins, report plugins[] |
| v28 | R | smart exclude suggestions |
| v29 | T | backup window, durability adaptive |
| v30 | U | repo-stats compression metrics; CompressTier + zstd dict |

---

## 测试与 bench 门禁（Wave U 后）

| 套件 | 规模 | 说明 |
|------|------|------|
| `ebbackup_tests` | 383 gtest | 含 Wave U 压缩/fuzz、pipeline deadlock、restore streaming |
| `ebsync_tests` | 6 | sync_cpp ferry/local push |
| `eb_run_tests_capi` | 15 | C API ABI 兼容 |
| `ebbackup_bench_check` | L1–L7 | Stage 3.1 blocking floors |
| `workbench_integration.rs` | 11 | Tauri shim roundtrip（Windows CI） |

---

## LaTeX 交叉索引

| PDF | 路径 | Wave 覆盖 |
|-----|------|-----------|
| 工程技术手册 | `docs/engineering/kernel-manual/build/main.pdf` | ch.1 总览；ch.16 CLI；ch.18 C API；ch.21 能力 Wave |
| 生态实录 | `docs/engineering/ecosystem-evolution/build/main.pdf` | ch.5–8 性能 Wave；ch.13 能力 Wave |
| 答辩 | `docs/engineering/defense-presentation/build/slides.pdf` | 性能经历 + 能力 Wave 弧 |

---

## 相关文档

- [`VERSION_HISTORY.md`](VERSION_HISTORY.md)
- [`technical/ABI_AND_FEATURES.md`](technical/ABI_AND_FEATURES.md)
- [`technical/COMPRESSION.md`](technical/COMPRESSION.md)
- [`reference/PERF_BASELINE.md`](reference/PERF_BASELINE.md)
- [`engineering/kernel-manual/README.md`](engineering/kernel-manual/README.md)
- [`engineering/ecosystem-evolution/README.md`](engineering/ecosystem-evolution/README.md)
