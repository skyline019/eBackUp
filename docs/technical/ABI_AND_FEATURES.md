# C API 与 Feature Flags

本文归档 C API ABI v7–v12 演进、Feature Flags 与 Init 默认行为。

**头文件**：`engine_cpp/include/ebbackup/eb_backup.h`

---

## ABI 版本

当前：**`EB_BACKUP_ABI_VERSION = 12`**（v0.6.0 引入，v0.7–v0.9.4 无 breaking 变更）

| ABI | 引入 | 关键 API / 行为 |
|-----|------|-----------------|
| v7 | M7 | `BackupFilterOptions`；`eb_backup_load_filter_file` |
| v8 | M8 | `EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY`；内容 Merkle 校验 |
| v9 | v0.2 | `eb_backup_init_repo_ex`；`EB_BACKUP_FLAG_LEGACY_DIGEST` |
| v10 | v0.3 | `EB_BACKUP_INIT_LEGACY`；`eb_backup_compact`；`eb_backup_repo_stats` |
| v11 | v0.4 | `eb_backup_list_snapshots`；`eb_backup_prune_snapshots`；restore/verify `--at` |
| v12 | v0.6 | 默认 EbPack + coalesced meta；`EB_BACKUP_FLAG_NO_PIPELINE` |

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
- 选择性恢复 + glob/path filter 与 backup 共用 loader

---

## 相关文档

- [`engine_cpp/README.md`](../../engine_cpp/README.md) — CLI 与 C API 速查
- [STORAGE_AND_DURABILITY.md](STORAGE_AND_DURABILITY.md)
- [VERSION_HISTORY.md](../VERSION_HISTORY.md) — ABI 演进表
