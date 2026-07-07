# 存储与耐久性

本文归档 ebbackup 仓库布局、EbPack、索引、快照、GC/compact 与提交点耐久性设计。

---

## 仓库目录布局

```
repo/
├── superblock.bin          # 双 4KB slot，CRC 选举，FSM + backup_features
├── manifest                # 当前 txn manifest（v4 二进制）
├── snapshots/
│   ├── index               # EBSNAPIDX1
│   └── <txn>.manifest      # 历史 snapshot
├── data/
│   ├── chunk.idx           # 持久化索引 EBCHIDX1 / HXID v2
│   ├── chunks/             # Legacy append-only（非 EbPack repo）
│   └── packs/
│       └── pack-<txn>-s<N>.ebpack   # v0.7+ 16-shard
├── audit/rar.chain         # 哈希链审计
└── crypto.salt             # 加密仓库（可选）
```

---

## 存储后端

### Legacy chunks（v0.3 前 / `--legacy-init`）

- `data/chunks/` append-only 文件
- `chunk.idx` v1（EBCHIDX1）
- compact 重写 chunk 文件

### EbPack（v0.5 可选 → v0.6+ 默认）

**实现**：`ebpack_writer.cc`、`chunk_store.cc`

- **Pack blob**：默认 8MB spill 到 `data/packs/*.ebpack`
- **Index v2**：HXID + `pack_name` + offset/length
- **16-shard**（v0.7）：`hash[0] & 0x0F` 选 shard，移除全局 `append_mu_`
- **Append-only writer**（v0.8）：增量 record append + header patch，避免 spill 时整 pack 重写

### ChunkStore 并发（v0.9.0 Wave 5）

- `shard_index_[16]` + `shard_index_mu_[16]` 独立 map
- `ForEachRecord`：锁内 snapshot，锁外回调（避免 compact 重入死锁）
- `tombstones_mu_` 与 shard 锁固定顺序
- `index_entries_mu_` 跟踪持久化索引条目

---

## Durability 模式

| 模式 | Feature | Pack flush 阈值 | manifest commit |
|------|---------|-----------------|-----------------|
| **strict** | 默认 | 4MiB / 32 records | 必须 fsync |
| **balanced** | `kBackupFeatureBalancedDurability` | 16MiB / 64 records | 必须 fsync |

v0.4.6+：**两种模式**均在 manifest commit 前 fsync。中途 crash → txn `kAborted`。

**Coalesced meta**：备份中 superblock 延迟落盘；abort/idle 时写入，与 commit-point chunk fsync 正交。

---

## Manifest 与 Snapshot

- **Manifest v4**（v0.3+）：二进制，32B chunk hash
- **Snapshots**（v0.4+）：`snapshots/<txn>.manifest` + GFS 保留策略
- **GC/Compact**：引用 hash = 所有**保留 snapshot** 的并集（非仅 latest）

CLI：`list-snapshots`、`prune-snapshots`、`restore --at`、`gc-orphans`、`compact`

---

## 运维指标

- `eb repo-stats`：`ampl_ratio`（物理/逻辑字节比）
- **告警**：`ampl_ratio > 1.3` 关注；`> 1.5` 建议 compact
- **L4 bench**：orphan inject → compact → `ampl_ratio_after ≤ 1.05`

---

## 加密

- AES-256-GCM 内容加密（`kBackupFeatureEncrypted`）
- PBKDF2 密钥派生；`crypto.salt`  per-repo
- NIST 测试向量（portable + 可选硬件路径）

---

## 相关文档

- [ABI_AND_FEATURES.md](ABI_AND_FEATURES.md) — Feature flags
- [ARCHITECTURE.md](ARCHITECTURE.md)
- [VERSION_HISTORY.md](../VERSION_HISTORY.md) — 仓库布局演进表
