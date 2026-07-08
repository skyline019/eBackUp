# Windows VSS 卷影快照读

文件级备份在 Windows 上可通过 VSS（Volume Shadow Copy Service）在**快照设备**上读取数据，改善打开/锁定文件可读性，并在多卷闭包内提供时间点一致的 crash/app 语义。

**实现**：`engine_cpp/src/winmeta/vss_session.cc`、`vss_volume_closure.cc`

---

## 模式

| 模式 | CLI / Job | C API flags | 行为 |
|------|-----------|-------------|------|
| **crash** | `--vss`（默认） | `EB_BACKUP_FLAG_VSS` | 不等待 Writer quiesce；`DoSnapshotSet` 后直接读 shadow |
| **app** | `--vss --vss-mode app` | `EB_BACKUP_FLAG_VSS \| EB_BACKUP_FLAG_VSS_APP` | `GatherWriterMetadata` → 快照 → 读数据 → `GatherWriterStatus` → `BackupComplete` |
| **auto** | `--vss --vss-mode auto` | `EB_BACKUP_FLAG_VSS` + `eb_backup_set_vss_mode("auto")` | 尝试 app 路径；Writer 失败时降为 crash 并写入 `vss_writer_degraded` issue |

读数据**之后**才调用 `FinishBackup()`（Writer 状态 + `BackupComplete`），再 `End()` 删除快照。

---

## 权限

- 推荐以**管理员**运行，或进程具备 `SeBackupPrivilege` / `SeRestorePrivilege`
- 非 elevated 环境：集成测试 `VssLockedFileBackupTest` 会 `GTEST_SKIP`

---

## 多卷闭包（junction）

默认对源路径 + `extra_scan_roots`（插件附加根）求卷，并在源树**有限深度**（默认 2）内探测 `IO_REPARSE_TAG_MOUNT_POINT`，将联接目标所在卷并入快照集。

- 禁用：`--vss-no-junction-volumes` 或 job `vss_include_junction_volumes: false`
- 报告：`vss_cross_volume=true` 当闭包 size > 1；`vss_volumes[]` 列出全部卷 GUID

---

## 影子空间预检

`Begin()` 前对闭包内各卷挂载点调用 `GetDiskFreeSpaceEx`；剩余空间低于阈值（默认 **512MB**）则失败。

- `--vss-fallback-live`：失败时降级活扫描，issue `vss_shadow_storage_low`
- 报告：`vss_shadow_storage_ok`（bool）

---

## 备份报告字段（ABI v32）

| 字段 | 说明 |
|------|------|
| `vss_used` | 是否启用并成功 Begin |
| `vss_mode` | 请求模式 crash/app/auto |
| `vss_consistency` | 实际一致性 crash/app/app_degraded |
| `vss_cross_volume` | 多卷闭包 |
| `vss_shadow_storage_ok` | 预检是否通过 |
| `vss_writers[]` | `{id,name,state}` Writer 摘要（app/auto） |
| `vss_snapshot_set_id` | 快照集 GUID |
| `vss_volumes[]` | 纳入快照的卷 |

---

## 明确不做

- 块级 raw 镜像 / 裸金属恢复
- 影子空间管理 GUI
- SQL Server 等专用 Writer 插件（依赖系统已注册 Writer）
