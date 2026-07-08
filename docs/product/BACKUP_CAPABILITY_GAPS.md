# 备份能力缺口归档（非网络）

本文归档 ebbackup 与成熟备份软件的能力差距、创新命题与实施阶段。工程细节见 [CAPABILITY_ROADMAP.md](../technical/CAPABILITY_ROADMAP.md)。

**缺口 ID**：`GAP-{域}-{序号}`

---

## 已有能力（Baseline，非缺口）

- CDC/HCRBO 增量、EbPack、Compact/GC、Snapshot GFS
- 选择性恢复 + 路径重整（ABI v13）
- AES-GCM、Merkle 子集校验、RAR 审计链
- filter / watch / schedule、`.ebb` bundle
- GUI：Browse、Diff、Restore、Maintenance wizard、备份报告页签
- `reports/<txn>.json` 尽力一致侧车 + pre/post hook

---

## A. 一致性层 `CONSIST`

| ID | 缺口 | 现状 | 创新命题 | 用户故事 |
|----|------|------|----------|----------|
| GAP-CONSIST-01 | 卷级/镜像备份 | 目录扫描 | 目录级一致性点 + 可选 VSS | 备份数据库目录时希望时间点一致 |
| GAP-CONSIST-02 | 打开/锁定文件 | 读失败即 job 失败 | **尽力一致 + 未备份清单** | 知道哪些文件因锁定未进备份 |
| GAP-CONSIST-03 | 应用一致性 | 无 hook | Schedule Hook（freeze/thaw） | 备份前自动 quiesce 应用 |
| GAP-CONSIST-04 | 增量链可达性 | **done**：`AnalyzeSnapshotReachability` + `eb verify-chain` + GUI 徽章（ABI v24） |

---

## B. Windows 元数据保真 `WIN`

| ID | 缺口 | 现状 | 创新命题 |
|----|------|------|----------|
| GAP-WIN-01 | ACL/Owner/SID | **done**：manifest v5 SD + inherit/preserve/skip/best_effort 恢复 | SD manifest 扩展 + 四策略恢复 |
| GAP-WIN-02 | ADS | **done**：扫描/恢复 ADS entry + Win32 读写路径 | 每 stream 一条 manifest entry |
| GAP-WIN-03 | Reparse/Junction | **done**：扫描采集 `reparse_target` + skip/recreate 恢复策略 | 联接点不展开 + 恢复策略 |
| GAP-WIN-04 | Hard link | **done**：`inode_id` dedup chunk + `CreateHardLink` 恢复 | inode_id + 结构 dedup |
| GAP-WIN-05 | Sparse NTFS | 全文件读 | 有效区间 map |
| GAP-WIN-06 | EFS 源文件 | 仅仓库 AES | 边界说明或 DPAPI 密钥包 |

---

## C. 作业与策略 `JOB`

| ID | 缺口 | 创新命题 |
|----|------|----------|
| GAP-JOB-01 | 单源单作业 | **已落地**：`jobs.json` + `RunJob` + 共享 chunk store |
| GAP-JOB-02 | 智能默认排除 | **已落地**：浅扫描 + 可解释建议 + job `exclude_paths` + CLI/GUI 一键采纳（ABI v28） |
| GAP-JOB-03 | 文件级保留 | **已落地**：`retention_tag` + `snapshot_meta.jsonl` + Prune 保护 |
| GAP-JOB-04 | 不可变/WORM | **已落地**：`kBackupFeatureImmutable` + `immutable_until` + RAR 门控 |
| GAP-JOB-05 | 备份窗口 | **已落地**：job 时间窗 + deadline grace + durability 自适应 + 报告截断（ABI v29） |

---

## D. 目录与可证明性 `CATALOG`

| ID | 缺口 | 现状（Wave A） | 创新命题 |
|----|------|----------------|----------|
| GAP-CATALOG-01 | 文件版本史 | **已落地**：`path_index` + GUI 路径历史抽屉（分页） | path→历史索引 |
| GAP-CATALOG-02 | 快照 Diff | **已落地**：`diff_snapshots` + DiffView 三表 | entry 级 diff + 块复用率 |
| GAP-CATALOG-03 | 可证明 Diff | **已落地**：`merkle_proof` / `diff_merkle_root` | Diff 附子树 Merkle |
| GAP-CATALOG-04 | 大规模 Browse | **已落地**：sidecar 前缀索引 + 流式分页（ABI v22） | — |
| GAP-CATALOG-05 | 增量透明度 | **已落地**：per-txn report + Job 级 `catalog/jobs/<job_id>.jsonl` |

---

## E. 恢复体验 `RESTORE`

| ID | 缺口 | 创新命题 |
|----|------|----------|
| GAP-RESTORE-01 | 仅新目录恢复 | **已落地**：三路合并 base/target/live + `both_changed` + `--base-at`（ABI v23） |
| GAP-RESTORE-02 | Symlink remap | **已落地**：`--symlink-remap-from/to` + remap JSON `symlink_remap_*` |
| GAP-RESTORE-03 | 恢复验收 | **已落地**：`export_restore_report` + OutputPanel 验收页签 |
| GAP-RESTORE-04 | 冲突 GUI | **已落地**：RestoreView 冲突筛选、bulk 决议、Dry-run、三路状态列 |

---

## F. 密钥与合规 `KEYS`

| ID | 缺口 | 创新命题 |
|----|------|----------|
| GAP-KEYS-01 | 单密码 | 主密钥 + 恢复密钥 envelope |
| GAP-KEYS-02 | GUI 无审计 | **done**：GUI 破坏性 ops append-only → `audit/rar.chain`（ABI v25） |
| GAP-KEYS-03 | 无 RPO 报告 | **done**：`RpoSummaryJson` + RepoHome 卡片 + `eb rpo-summary`（ABI v24） |

---

## G. 运维与平台 `OPS` / `PLATFORM`

| ID | 缺口 | 创新命题 |
|----|------|----------|
| GAP-OPS-01 | 无本地告警 | **done（MVP）**：`useBackupAlerts` + `stale_backup_alert_days` + ElNotification（Wave N） |
| GAP-OPS-02 | GC/Prune 认知 | **done**：Maintenance 孤儿解释图 + `eb orphan-explain`（ABI v25） |
| GAP-PLATFORM-01 | 非常驻服务 | **done**：Windows SCM `eb service` + systemd unit/timer（Wave O+） |
| GAP-PLATFORM-02 | 单 repo session | **done**：Workbench 多 Profile（主题/最近/告警分 profile）（Wave O+） |
| GAP-PLATFORM-03 | 无最小 Runtime | USB 恢复 exe |
| GAP-PLATFORM-04 | bundle 全量 | **已落地**：EBB v2 delta export（`--delta --base-at`） |

---

## H. 引擎韧性 `ENGINE`

| ID | 缺口 | 创新命题 |
|----|------|----------|
| GAP-ENGINE-01 | 扫描遇错全失败 | per-path 错误收集 |
| GAP-ENGINE-02 | 无 job 队列 | **done**：`catalog/job_queue.jsonl` + `eb queue` + C API v22 + GUI 入队/drain（Wave N） |
| GAP-ENGINE-03 | Symlink 循环 | **已落地**：`kMaxScanDepth` + `symlink_loop` 检测 + 集成测试 |

---

## I. 垂直场景 `VERTICAL`

| ID | 缺口 | 创新命题 |
|----|------|----------|
| GAP-VERTICAL-01 | 数据库 | **done**：SQLite checkpoint 插件（Wave P） |
| GAP-VERTICAL-02 | VM | **done**：VHDX 只读扫描插件（Wave P） |
| GAP-VERTICAL-03 | 系统状态 | **done**：Registry hive 导出插件（Wave P） |

---

## 缺口状态跟踪

| ID | Phase | Status | 备注 |
|----|-------|--------|------|
| GAP-CATALOG-01 | 1 | done | path_index sidecar |
| GAP-CATALOG-04 | 1 | done | manifest page API |
| GAP-CATALOG-02 | 2 | done | manifest_diff |
| GAP-CATALOG-03 | 2 | done | Merkle diff report |
| GAP-RESTORE-03 | 2 | done | acceptance report |
| GAP-WIN-01~04 | 3 | done | Wave E：hardlink dedup、reparse_target、ACL best_effort、ADS E2E（`ads_backup_restore_test`） |
| GAP-CONSIST-02 | 4 | done | backup report + issues + pipeline skip |
| GAP-CONSIST-03 | 4 | done | pre/post_backup_cmd + GUI hooks |
| GAP-ENGINE-01 | 4 | done | ScanResult + issue skip chunking |
| GAP-CATALOG-05 | 4 | done | per-txn report + `catalog/jobs/<job_id>.jsonl` sidecar |
| GAP-JOB-01~04 | 5 | done | Wave F 内核 + Wave G GUI：`jobs.json`、RunJob、snapshot meta、WORM prune 门控 |
| GAP-RESTORE-01 | 6 | done | three-way in-place merge（Wave M）；ABI v23 |
| GAP-RESTORE-02 | 6 | done | symlink target remap（Wave K） |
| GAP-RESTORE-04 | 6 | done | RestoreView bulk conflict workstation（Wave M） |
| GAP-PLATFORM-03 | 6 | done | ebrecover CLI |
| GAP-PLATFORM-04 | 6 | done | EBB v2 delta + C API/GUI 导出入口（Wave K） |
| GAP-CATALOG-04 | 7 | done | manifest browse sidecar + prefix page |
| GAP-ENGINE-02 | 7 | done | persistent job queue |
| GAP-ENGINE-03 | 7 | done | scan depth + symlink loop |
| GAP-OPS-01 | 8 | done (MVP) | stale backup local alerts |
| GAP-OPS-02 | 8 | done | orphan explain graph + MaintenanceView |
| GAP-KEYS-02 | 8 | done | GUI ops → RAR audit chain |
| GAP-PLATFORM-01 | 8 | done | Windows Service + systemd + `eb service` CLI |
| GAP-PLATFORM-02 | 8 | done | Workbench multi-profile settings/state |
| GAP-VERTICAL-01~03 | 9 | done | sqlite_checkpoint + registry_hive + vhdx_scan plugins |
| GAP-VERTICAL-04 | 9 | done | Wave Q：报告 plugins[]、GUI 摘要、schedule plugins=、VHDX E2E |

*Status 随实现更新：`planned` → `in_progress` → `done`*
