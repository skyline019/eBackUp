# 外侧云生态（非内核）

本文定义 **ebbackup 内核封顶** 与 **外侧云同步** 的边界、双轨策略、S3 对象布局与弱网语义。实现位于 [`sync_cpp/`](../../sync_cpp/)，**不**扩展 `BackupEngine` FSM 或 ABI v30。

---

## 内核冻结声明（ABI v29）

| 冻结（`engine_cpp/`） | 外侧（`sync_cpp/`） |
|----------------------|---------------------|
| Content ID = `SHA256(content)` | S3 PUT/HEAD/GET、指数退避 |
| manifest commit 前 `ChunkStore::Flush()` | `catalog/sync_state.json` |
| 中断 txn → `kAborted`，前一 snapshot 可 verify | `catalog/sync_outbox.jsonl` |
| on-disk 格式（EbPack / manifest / superblock） | `remote_index.json`、远端 object 布局 |
| 禁止 HTTP/S3/remote 进入 `backup_engine.cc` | `eb-sync` CLI、daemon `sync_drain` |

**核心原则：** 备份成功 ≠ 同步成功。断网时本地仍可 backup / restore / verify；同步失败**不得**映射为 `kAborted`。

### 语义分层

| 层级 | 含义 | 断网时 |
|------|------|--------|
| **local commit-point** | manifest 写入前 chunk fsync | 完全可用 |
| **sync-point** | 远端 `remote_index.json` 已更新 | 暂停 / 重试 |
| **remote restore** | 从 S3 拉取后可 `eb verify` | 依赖 sync-point |

---

## 双轨策略

### 轨道 A — Delta 摆渡（离线 / 间歇连接，**无云首选**）

- 复用 **EBB v2**：`eb export --delta --base-at TXN` 或 `eb-sync ferry export`
- GUI：Workbench **同步** Activity / 快照页 delta 区
- 流程：本地 backup → 导出 `delta.ebb` → 物理搬运（U 盘 / NAS）→ 对端 `eb-sync ferry import` 或 `eb import`
- 初始化：`eb-sync init --repo PATH --mode ferry`
- 维护门控：按 `last_ferry_target_txn` 判断，不再误用 `synced_txn`

### 轨道 B — 在线 Sync Agent（本地镜像 / S3 / PDS）

- **无云**：`eb-sync init --repo PATH --local-mirror D:\mirror` → `eb-sync push`
- **有云（可选）**：S3 PUT/HEAD/GET
- **阿里云 PDS 网盘**：OAuth + PDS File API（`remote_type=pds`）
- 续传：`HEAD` 跳过已存在对象；Outbox 行级重试
- 触发：`post_backup_cmd` 或 daemon `mode=sync_drain`

---

## 无云仓库起步（推荐）

### 场景 A — U 盘 / 物理摆渡

```powershell
eb-sync init --repo C:\ebbackup-data\repo --mode ferry
eb backup ...
eb-sync ferry export --repo C:\ebbackup-data\repo --out-dir D:\ferry-drop --auto-base
# 搬运 delta_*.ebb 到对端
eb-sync ferry import --base C:\base.ebb --delta D:\ferry-drop\delta_1_2.ebb --dest-repo C:\imported
eb verify --repo C:\imported
```

### 场景 B — NAS 本地镜像（同网共享目录）

```powershell
eb-sync init --repo C:\ebbackup-data\repo --local-mirror \\nas\share\eb-mirror
eb backup ...
eb-sync push --repo C:\ebbackup-data\repo --once
# 对端
eb-sync pull --repo C:\ebbackup-data\repo --dest C:\restored-repo
```

Workbench：**同步** Activity → 选择「摆渡」或「本地镜像」→ 保存配置。

### 维护前检查（按 sync_mode）

| `remote_type` | 阻塞条件 | 操作 |
|---------------|----------|------|
| `ferry` | `latest_txn > last_ferry_target_txn` | `ferry export` |
| `local_mirror` / `s3` / `pds` | `latest_txn > synced_txn` | `push` |
| 未 init | 不阻塞 | 可选配置同步 |

---

## S3 对象布局（可选 / 后期）

> 无云环境可跳过本节；使用本地镜像或摆渡即可。

```
s3://{bucket}/{prefix}/
  chunks/{sha256_hex}       # ChunkRecord 字节（与 EBB delta 内 chunks/ 一致）
  meta/superblock.bin
  meta/snapshots/{txn}.manifest
  meta/snapshots/index
  bundles/{base}-{target}.ebb   # 可选归档
  remote_index.json             # synced_txn、generation、chunk_count
```

配置：`{repo}/sync.json` 或环境变量 `EBSYNC_S3_*`（**不进**仓库加密区）。

---

## 阿里云 PDS 网盘布局

> 适用于阿里云网盘（PDS）域；对象语义与 S3 布局一致，映射为网盘文件夹路径。

```
{root_prefix}/
  chunks/{sha256_hex}
  meta/superblock.bin
  meta/snapshots/{txn}.manifest
  meta/snapshots/index
  remote_index.json
```

### 初始化与授权

```powershell
eb-sync init --repo C:\ebbackup-data\repo --pds --domain bj36449 --credentials C:\secure\appSecret.csv
eb-sync pds auth-url --repo C:\ebbackup-data\repo
eb-sync pds auth --repo C:\ebbackup-data\repo --code <OAuth_code>
eb-sync push --repo C:\ebbackup-data\repo --once
```

- RAM OAuth 应用需在控制台配置回调：`https://{domain}.auth.aliyunfile.com/v2/oauth/callback`
- 凭证与 refresh token 写入 `{repo}/sync.json`；文件 ID 缓存 `{repo}/catalog/pds_file_index.json`
- Workbench：**同步** Activity →「阿里云 PDS」→ 保存配置 → 授权 → Push

环境变量：`EBSYNC_PDS_DOMAIN`、`EBSYNC_PDS_CLIENT_ID`、`EBSYNC_PDS_CLIENT_SECRET`、`EBSYNC_PDS_REFRESH_TOKEN`

---

## 弱网策略

- 指数退避：1s → 2s → … → 最大 300s，写入 `sync_state.backoff_until_unix`
- 单 chunk 失败不作废整 job；Outbox 状态：`pending` / `uploading` / `done` / `failed`
- 5xx / 超时可重试；4xx（除 408）标记失败并记录 `last_error`

---

## Compact / GC 门控

- `eb-sync status`：`pending_txn > synced_txn` 时 GUI 维护向导警告「先同步再 compact」
- WORM / immutable snapshot 期间禁止 prune 未同步 txn（运维约定）
- `sync_state.generation` 与远端 `remote_index.generation` 对齐全量重同步

---

## 相关文档

- [CAPABILITY_ROADMAP.md](CAPABILITY_ROADMAP.md) — Phase 12
- [ARCHITECTURE.md](ARCHITECTURE.md) — 内核分层
- [sync_cpp/README.md](../../sync_cpp/README.md) — `eb-sync` CLI
- [product/WORKBENCH_GUI.md](../product/WORKBENCH_GUI.md) — Sync Activity

---

## v1 明确不做

- 内核 ABI v30 云 API
- `BackupEngine` 内嵌 S3 backend
- 多设备双向冲突合并
- 将云端 ack 写入 superblock commit 语义
