# C API 与 Feature Flags

本文归档 C API ABI v7–v31 演进、Feature Flags 与 Init 默认行为。

**头文件**：`engine_cpp/include/ebbackup/eb_backup.h`

---

## ABI 版本

当前：**`EB_BACKUP_ABI_VERSION = 37`**

| ABI | 引入 | 关键 API / 行为 |
|-----|------|-----------------|
| v7 | M7 | `BackupFilterOptions`；`eb_backup_load_filter_file` |
| v8 | M8 | `EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY`；内容 Merkle 校验 |
| v9 | v0.2 | `eb_backup_init_repo_ex`；`EB_BACKUP_FLAG_LEGACY_DIGEST` |
| v10 | v0.3 | `EB_BACKUP_INIT_LEGACY`；`eb_backup_compact`；`eb_backup_repo_stats` |
| v11 | v0.4 | `eb_backup_list_snapshots`；`eb_backup_prune_snapshots`；restore/verify `--at` |
| v12 | v0.6 | 默认 EbPack + coalesced meta；`EB_BACKUP_FLAG_NO_PIPELINE` |
| v13 | v0.7 | `eb_backup_set_filter_json`；`RestorePathRemap`；`eb_backup_preview_restore_at`；`eb_backup_run_maintenance_wizard`；`eb_backup_gc_orphans_ex` |
| v14 | v0.8 | `eb_backup_build_path_index`；`eb_backup_query_path_history_json`；`eb_backup_list_manifest_files_page_json` |
| v15 | v0.8 | `eb_backup_diff_snapshots_json`；`eb_backup_export_restore_report_json` |
| v16 | v0.9 | Manifest v5（`EBMANIFEST5`）Windows 元数据；restore remap JSON `acl_policy` |
| v17 | v0.9 | `eb_backup_get_backup_report_json`；`pre_backup_cmd` / `post_backup_cmd` hooks |
| v18 | v0.10 | `jobs.json` CRUD；`eb_backup_run_job`；`catalog/snapshot_meta.jsonl`；WORM prune 门控 |
| v19 | v0.10 | `eb_backup_preview_in_place_json`；`catalog/jobs/<job_id>.jsonl`；`eb_backup_list_job_reports_json` |
| v20 | v0.10 | `eb_backup_apply_in_place_json`（就地恢复 apply + 冲突策略） |
| v21 | v0.10 | `eb_backup_export/import/apply_delta_json`；`orphan_policy` on apply in-place；symlink remap JSON |
| v22 | v0.11 | manifest browse sidecar（`catalog/manifest_browse/<txn>.mbi`）；`index_source` on manifest page JSON；`eb_backup_enqueue_job_json` / `eb_backup_run_job_queue_json` / `eb_backup_job_queue_status_json` |
| v23 | v0.11 | 就地恢复三路合并：`base_txn_id` / `use_three_way` / `dry_run` on `eb_backup_preview/apply_in_place_json`；preview 增 `base_action` / `live_state` / `both_changed`；apply 增 `overwritten_count` |
| v24 | v0.11 | `eb_backup_snapshot_reachability_json`；`eb_backup_rpo_summary_json`；CLI `eb verify-chain` / `eb rpo-summary`；daemon `queue_drain` schedule 模式 |
| v25 | v0.11 | `eb_backup_orphan_explain_json`；`eb_backup_append_ops_audit_json` / `eb_backup_list_ops_audit_json`；CLI `eb orphan-explain` / `eb audit-ops list`；GUI 维护 ops → RAR |
| v26 | v0.11 | CLI `eb service run|install|uninstall|status`（Windows SCM）；daemon 可中断 stop + `RunScheduleConfig`；systemd unit/timer 模板；Workbench 多 Profile（主题/最近仓库/告警分 profile 持久化） |
| v27 | v0.11 | `IBackupPlugin` 生命周期接入 `RunBackup`；job `plugins[]`；`eb_backup_set_plugins_json`；内置 `sqlite_checkpoint` / `registry_hive` / `vhdx_scan`；CLI `eb plugin list` / `eb backup --plugins` |
| v28 | v0.12 | 智能排除建议：`eb_backup_suggest_exclude_filters_json`；job `exclude_paths[]`；CLI `eb suggest-excludes`；GUI 备份页分析/一键采纳 |
| v29 | v0.12 | 备份窗口：job `window_start/end` + `durability_adaptive`；deadline 驱动 Strict→Balanced；report `durability_downgraded` / `window_truncated` |
| v30 | v0.9.6 | **`EbRepoStats` 压缩统计**：`live_uncompressed_bytes`、`live_stored_payload_bytes`、`compress_ratio`、`compressed_chunk_count`、`raw_chunk_count`、`has_zstd_dict`、`zstd_dict_bytes`；CLI `eb repo-stats` / Workbench `repo_info_json` |
| v31 | v0.9.7 | **`EB_BACKUP_FLAG_VSS`**；备份报告 JSON `vss_used` / `vss_consistency` / `vss_snapshot_set_id` / `vss_volumes`；CLI `--vss` / `--vss-fallback-live` |
| v32 | v0.9.8 | **`EB_BACKUP_FLAG_VSS_APP`**；`eb_backup_set_vss_mode`；报告 `vss_mode` / `vss_cross_volume` / `vss_shadow_storage_ok` / `vss_writers[]`；CLI `--vss-mode` / `--vss-no-junction-volumes` |
| v33 | v0.9.9 | NTFS **稀疏文件**（`--sparse auto\|off`）；manifest sparse runs；报告 `sparse_file_count` |
| v34 | v0.10.0 | **`crypto.envelope.json`** 恢复密钥；`eb_backup_unwrap_with_recovery_key` / `eb_backup_rotate_password`；CLI `eb init --encrypt` / `eb unlock` / `eb rotate-password` |
| v35 | v0.10.1 | VSS **影子存储**（`vssadmin`/`Win32_ShadowStorage`）；`eb vss status`；job `quiesce_profile` / `vss_app_failure_policy`；报告 `vss_shadow_storage_bytes[]` |
| v36 | v0.10.2 | **EFS** Tier A skip + Tier B `--efs-export-keys`；`post_backup_webhook_url`；sync status `failed_chunks` / `backoff_until` |
| v37 | v0.10.3 | Manifest **`kMetaEfs`**；EFS 密钥恢复；`EB_BACKUP_FLAG_SPARSE_OFF` / `EB_BACKUP_FLAG_EFS_EXPORT_KEYS`；Workbench 密钥 JSON API；`recovery_key_issued` 报告字段 |

### v37 Phase 16–19 收尾

| 主题 | 说明 |
|------|------|
| Manifest EFS | `kMetaEfs`：`efs_encrypted` + `efs_key_blob_b64` 二进制/文本 manifest |
| EFS restore | `WriteEncryptedFileRaw`；issue `efs_restored` / `efs_key_import_failed` |
| VSS WMI | `Win32_ShadowStorage` 查询优先；`vssadmin` fallback |
| Workbench | `ebbackup_workbench_init_encrypt_json` / `_unwrap_recovery_key_json` / `_rotate_password_json` / `_upgrade_legacy_envelope_json` |
| Flags | `0x4000` sparse off；`0x8000` efs export keys |
| 交付 | `ebrecover-portable.zip` CI artifact |

### v36 EFS + 告警 + 云可观测

| 主题 | 说明 |
|------|------|
| EFS Tier A | 检测 `FILE_ATTRIBUTE_ENCRYPTED`；skip 内容；issue `efs_encrypted_skipped`；`efs_skipped_count` |
| EFS Tier B | `--efs-export-keys` → `ReadEncryptedFileRaw` → manifest `efs_key_blob_b64` |
| Webhook | job/daemon `post_backup_webhook_url`；备份后 POST 报告 JSON |
| sync_cpp | `eb-sync status --json` 增 `failed_chunks`、`backoff_until_unix`、`last_error` |

### v35 VSS 运维深化

| 主题 | 说明 |
|------|------|
| 影子存储 | `vss_shadow_storage.cc` — `vssadmin list shadowstorage` 解析 |
| CLI | `eb vss status` — 只读诊断 JSON |
| Job | `quiesce_profile`（none/system/sql/exchange/custom）；`vss_app_failure_policy`（fail_job/degraded/fallback_live） |

### v34 恢复密钥 envelope (GAP-KEYS-01)

| 主题 | 说明 |
|------|------|
| 文件 | `{repo}/crypto.envelope.json` — 主密钥双包裹（密码 + 恢复密钥） |
| 向后兼容 | 无 envelope 时仍走 `crypto.salt` + PBKDF2；首次 `--encrypt` 自动升级 |
| C API | `EB_BACKUP_INIT_RECOVERY_KEY`；`eb_backup_unwrap_with_recovery_key`；`eb_backup_rotate_password` |
| ebrecover | `--password` / `--recovery-key`；`list` 快照摘要 |

### v33 NTFS 稀疏文件 (GAP-WIN-05)

| 主题 | 说明 |
|------|------|
| 检测 | `FILE_ATTRIBUTE_SPARSE` + `FSCTL_GET_RETRIEVAL_POINTERS` |
| 备份 | 按 run 切块；manifest `sparse_runs` + `sparse_chunk_offsets` |
| 恢复 | `FSCTL_SET_SPARSE` + seek 写入 + `SetEndOfFile` |
| CLI | `--sparse auto\|off`（Windows 默认 auto） |

### v32 VSS deepen (GAP-CONSIST-01 file-level done)

| 主题 | 说明 |
|------|------|
| 标志 | `EB_BACKUP_FLAG_VSS`（crash）；`\| EB_BACKUP_FLAG_VSS_APP`（app）；auto 经 `eb_backup_set_vss_mode` |
| 生命周期 | 读完成后 `FinishBackup` → `BackupComplete`；`End` 删除快照 |
| 报告 | 见 [`VSS.md`](VSS.md) |

### v31 VSS snapshot read (GAP-CONSIST-01 partial)

| 主题 | 说明 |
|------|------|
| 标志 | `EB_BACKUP_FLAG_VSS`（0x0800）；Windows 需管理员或 SeBackupPrivilege |
| 一致性 | MVP 为 **crash-consistent**；不含 VSS Writer 应用一致 |
| 报告 | `reports/<txn>.json` 扩展 VSS 字段；manifest 仍存逻辑相对路径 |

### v30 Repo-stats compression metrics

| 字段 | 含义 |
|------|------|
| `live_uncompressed_bytes` | manifest 引用的 live chunk 逻辑未压缩字节 |
| `live_stored_payload_bytes` | 同上 chunk 存储 payload（不含 header/pack 开销） |
| `compress_ratio` | `stored / uncompressed`（越小越好；无数据时为 1.0） |
| `compressed_chunk_count` / `raw_chunk_count` | 压缩 vs 原样 chunk 计数 |
| `has_zstd_dict` / `zstd_dict_bytes` | 仓库字典 `{repo}/meta/zstd_dict.bin` |

压缩策略（CompressTier、LDM、字典训练）见 [COMPRESSION.md](COMPRESSION.md)。**v30 不包含云同步 API**（仍在外侧 `sync_cpp/`）。

### v29 Backup window (GAP-JOB-05)

| 主题 | 说明 |
|------|------|
| Job 配置 | `window_start` / `window_end`（`HH:MM` 本地时区）；`deadline_grace_seconds`（默认 300）；`durability_adaptive` |
| RunJob | 窗外返回 `Conflict("outside backup window")`；schedule/queue 跳过不记失败 |
| 中途自适应 | 距 `window_end` ≤ grace 时 `SetDurabilityMode(Balanced)`；超时 graceful 截断并写 report |
| 报告 | `durability_downgraded`、`window_truncated`、`window_end_unix` |

### v28 Smart exclude suggestions

| 主题 | 说明 |
|------|------|
| Shallow scan | 源目录浅层遍历（默认深度 4），识别 `.git`、`node_modules`、`__pycache__`、`Thumbs.db` 等常见可排除项 |
| 建议类型 | `exclude_path`（子树前缀）与 `exclude_glob`（basename 模式）；不自动生效，须用户确认采纳 |
| Job 配置 | `jobs.json` 字段 `"exclude_paths": ["..."]`（与既有 `exclude_globs` 并存）；`RunJob` 合并进 `BackupFilterOptions` |
| C API | `eb_backup_suggest_exclude_filters_json({"source_path":"...","max_depth":4,"existing":{...}})` → `items[]` + `recommended` |
| CLI | `eb suggest-excludes <source> [--json] [--max-depth N] [--filter-file PATH]` |
| GUI | 备份页「分析源目录」→ 建议表格 →「全部采纳」写入作业或会话 filter（`setFilterJson`） |

### v27 Vertical backup plugins

| 主题 | 说明 |
|------|------|
| Plugin lifecycle | `Quiesce()` → scan（含 `ExtraScanRoots` + `ScanHints`）→ `Thaw()`；RAII `PluginSession` |
| Job 配置 | `jobs.json` 字段 `"plugins": ["sqlite_checkpoint"]` |
| C API | `eb_backup_set_plugins_json(eng, "{\"plugins\":[...]}")` |
| sqlite_checkpoint | 源树内 `*.db`/`*.sqlite` 执行 `PRAGMA wal_checkpoint(FULL)`；跳过 `-wal`/`-shm` |
| registry_hive | Windows：`RegSaveKey` 导出至 `<source>/.ebbackup/registry/` |
| vhdx_scan | Windows：`Mount-Vhd -ReadOnly` 挂载至 `<source>/.ebbackup/vhdx/<n>/` |
| Backup report | `reports/<txn>.json` 可选 `"plugins":[{...}]` 数组（各插件 `PluginReportJson` 摘要）；可选 `plugin_skipped` / `plugin_failed` 计数（Wave Q，仍属 v27 schema 扩展） |
| Schedule | JSON/INI：`plugins=sqlite_checkpoint,registry_hive`（逗号分隔，与 `job_ids` 同风格） |

### v26 Platform service / Workbench profiles

| 主题 | 说明 |
|------|------|
| Daemon stop | C++ `RequestDaemonStop()` / `ResetDaemonStop()`；schedule/watch/queue_drain 循环 1s 分片 sleep 可响应停止 |
| Windows Service | `eb service install --config PATH` 注册 SCM；`eb service run` 作为 ServiceMain 入口；`scripts/install_service.ps1` |
| systemd | `deploy/ebbackup.service` + `ebbackup.timer`；`ExecStart=eb schedule /etc/ebbackup/queue_drain.json --once` |
| Workbench profiles | Tauri `profiles.json` + `profiles/<id>/settings.json|state.json`；Ribbon Profile 切换；设置抽屉 Profile 管理 |

### v25 Orphan explain / ops audit

| 主题 | 说明 |
|------|------|
| Orphan explain JSON | `total_orphans`, `total_orphan_bytes`, `unreferenced_count`, `tombstoned_count`, `interrupted_hint_count`, `samples[]`（`chunk_hex`, `reason`, `bytes`, `last_referenced_txn`） |
| Ops audit body | `{"kind":"ops","op":"prune|gc_orphans|compact|in_place_apply",...}` append-only 至 `audit/rar.chain`；可选 CARL 签名（`set_audit_key`） |
| CLI | `eb orphan-explain <repo> [--json] [--limit N]`；`eb audit-ops list <repo> [--json]` |

### v24 Reachability / RPO observability

| 主题 | 说明 |
|------|------|
| Reachability JSON | `txn_id`, `reachable`, `files_checked`, `chunks_checked`, `missing_chunk_count`, `missing_chunks[]`（最多 32 样本） |
| RPO summary JSON | `last_success_txn`, `last_success_unix`, `days_since_last_success`, `snapshot_count`, `worm_protected_count`, `jobs[]`（每 job 含 `last_report_ok`, `retention_tag`） |
| CLI | `eb verify-chain <repo> [--at TXN] [--json]`；`eb rpo-summary <repo> [--json]`；`eb queue drain <repo>` |
| Daemon schedule | JSON/ini：`mode: queue_drain` 或 `drain_queue: true`；`repo_path`；可选 `job_ids` 预入队 |

### v23 In-place three-way merge

| 主题 | 说明 |
|------|------|
| Preview options | `{"base_txn_id":N,"use_three_way":true}` — `base_txn_id=0` 自动取 target 前一快照 |
| Preview fields | `base_txn_id`, `three_way`, entries: `base_action` (`absent`/`same`/`changed`), `live_state` (`missing`/`matches_base`/`matches_target`/`diverged`), `action` 含 `both_changed` |
| Apply options | `{"conflict_policy":"skip|fail|overwrite","orphan_policy":"skip|delete","dry_run":true,"base_txn_id":N}` |
| Apply summary | `overwritten_count`（冲突覆盖写回）；`dry_run` 时不写盘 |

### v22 Manifest browse index / job queue

| 主题 | 说明 |
|------|------|
| Browse sidecar | `catalog/manifest_browse/<txn_id>.mbi`；commit 时自动写入；`eb path-index --rebuild` 可重建 |
| 清单分页 | `eb_backup_list_manifest_files_page_json` 优先读 sidecar；JSON 含 `index_source`: `sidecar` \| `full_manifest` |
| Job 队列 | 持久化 `catalog/job_queue.jsonl`；CLI `eb queue list/add/run`；C API enqueue/run/status JSON |

### v16 Manifest v5 / Windows 元数据

| 字段 | 说明 |
|------|------|
| `security_descriptor_b64` | DACL/Owner SD 二进制 Base64 |
| `inode_id` | NTFS 文件索引（硬链 dedup 锚点） |
| `reparse_tag` | 重解析点标签（junction/symlink） |
| `reparse_target` | junction 目标路径（Wave E；`reparse_tag` 非 0 时） |
| `stream_name` | ADS 流名（非 `::$DATA`） |

恢复 remap JSON（可与 `mode` 同对象）：

```json
{
  "acl_policy": "inherit" | "preserve" | "skip" | "best_effort",
  "reparse_policy": "skip" | "recreate"
}
```

- `best_effort`：ACL 恢复失败时写入验收报告 `issues[]`，不中断恢复。
- `reparse_policy`：`skip` 仅还原联接点目标目录内容；`recreate` 在内容就绪后重建 junction。

### v15 JSON 扩展（ABI 仍为 15，schema 向后兼容扩展）

| 主题 | 字段 / 参数 |
|------|-------------|
| 路径历史分页 | `eb_backup_query_path_history_json(path, offset, limit)` → `total`, `offset`, `history[]` |
| 清单分页 | `eb_backup_list_manifest_files_page_json(txn, prefix, offset, limit)` → `total`, `files[]` |
| Merkle Diff | `added[]` / `modified[]` 含 `subset_merkle`, `merkle_proof[]`；顶层 `diff_merkle_root`, `chunk_reuse_ratio` |
| 恢复验收 | `export_restore_report` → `merkle_proofs[]`, `subset_merkle_root`, `verified_files` |
| 备份复用率 | `run_backup` stats → `chunks_written`, `chunks_reused`, `reuse_pct` |

---

## Feature Flags（`backup_features`，uint32_t since v0.6）

| 常量 | 值 | 含义 |
|------|-----|------|
| `kBackupFeatureMeta` | 0x01 | 扩展文件元数据 |
| `kBackupFeatureSpecialFiles` | 0x02 | 符号链接、FIFO 等 |
| `kBackupFeatureEncrypted` | 0x04 | AES-GCM 内容加密 |
| `kBackupFeatureDigestStandard` | 0x08 | NIST/RFC digest 栈 |
| `kBackupFeaturePersistentIndex` | 0x10 | `data/chunk.idx` |
| `kBackupFeatureManifestBinary` | 0x20 | Manifest v4 |
| `kBackupFeatureBalancedDurability` | 0x40 | balanced 耐久性 |
| `kBackupFeatureSnapshots` | 0x80 | 时间旅行 snapshot |
| `kBackupFeatureEbPack` | 0x100 | Pack 存储（v0.6+ 默认） |
| `kBackupFeatureCoalescedMeta` | 0x200 | 合并 superblock 写入（v0.6+ 默认） |

v0.6 修复：`backup_features` 从 `uint8_t` 扩为 `uint32_t`，避免 0x100/0x200 截断。

---

## Init 标志

| 标志 | 效果 |
|------|------|
| `EB_BACKUP_FLAG_LEGACY_DIGEST` | Legacy digest 栈 |
| `EB_BACKUP_FLAG_NO_PIPELINE` | 禁用自动 pipeline |
| `EB_BACKUP_FLAG_VSS` | Windows VSS 卷影快照读（crash-consistent） |
| `EB_BACKUP_FLAG_VSS_APP` | 与 `VSS` 组合：应用一致 Writer 路径（v32） |
| `EB_BACKUP_INIT_LEGACY` | 最简 legacy repo（无 v0.3+ 现代特性） |

---

## Init 默认行为

| 入口 | 默认特性 |
|------|----------|
| `eb init` / `eb_backup_init_repo_ex(0)` | Standard digest + persistent index + manifest v4 + compress-auto + snapshots + **EbPack + coalesced meta** |
| C++ `InitRepo(legacy=true)` | 最简 legacy（测试兼容） |
| `InitV03Repo()` / `--legacy-init` | v0.3 legacy chunks，无 EbPack |
| `InitDefaultRepo()`（测试） | 等同 v0.6 `InitV05Repo` |

---

## Digest 栈

| 栈 | 路由 | 说明 |
|----|------|------|
| **Legacy** | `DigestAlgo::kLegacy` | 冻结兼容栈 |
| **Standard** | `DigestAlgo::kStandard` | NIST/RFC；CLI/C API init 默认 |
| **SHA-NI** | runtime CPUID（v0.7+） | x64 硬件 SHA256，self-test 后 dispatch |

Content ID 始终为 **SHA256(content)**，与 digest 栈选择无关（栈影响 wire format / 互操作，不改变 content hash 语义）。

---

## 恢复标志

- `EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY` — 跳过逐 chunk 内容校验（加速）
- CLI：`--verify-content` / `--skip-content-verify`
- 选择性恢复 + glob/path filter 与 backup 共用 loader；v13 起支持 `RestorePathRemap`（strip/flatten/remap）与 JSON filter setter

---

## 相关文档

- [`engine_cpp/README.md`](../../engine_cpp/README.md) — CLI 与 C API 速查
- [COMPRESSION.md](COMPRESSION.md) — CompressTier 与仓库字典
- [STORAGE_AND_DURABILITY.md](STORAGE_AND_DURABILITY.md)
- [VERSION_HISTORY.md](../VERSION_HISTORY.md) — ABI 演进表
- [CAPABILITY_ROADMAP.md](CAPABILITY_ROADMAP.md) — 分阶段 ABI 规划

---

## 规划中的 ABI（v22+）

以下 API 在 [CAPABILITY_ROADMAP.md](CAPABILITY_ROADMAP.md) 中分阶段落地；**签名与语义以合并时的头文件为准**，本节仅作索引占位。

| ABI | 规划 Phase | 主题 | 规划 API（摘要） |
|-----|------------|------|------------------|
| v22+ | 7+ | 规模 / 平台 | 见 CAPABILITY_ROADMAP Phase 7–9 |
