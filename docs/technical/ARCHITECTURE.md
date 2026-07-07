# 架构概览

本文描述 ebbackup 备份引擎的分层结构、备份 FSM、Pipeline 拓扑演进与关键模块边界。实现位于 [`engine_cpp/`](../../engine_cpp/)。

---

## 分层结构

```
┌─────────────────────────────────────────────────────────┐
│  CLI (eb) / Daemon (schedule, watch) / C API (eb_backup) │
├─────────────────────────────────────────────────────────┤
│  BackupEngine — FSM、事务、manifest、审计、恢复           │
├─────────────────────────────────────────────────────────┤
│  BackupPipeline — 文件调度、分块、压缩、存储队列           │
├──────────────┬──────────────┬──────────────┬──────────────┤
│ Chunk/HCRBO  │ Compress     │ ChunkStore   │ Snapshot/GC  │
│ FastCDC/CFI  │ LZ4/zstd     │ EbPack/Legacy│ Compact      │
├──────────────┴──────────────┴──────────────┴──────────────┤
│  Crypto (AES-GCM) · Digest (Legacy/Standard/SHA-NI)       │
│  Durability · Superblock · Persistent Index                 │
└─────────────────────────────────────────────────────────┘
```

---

## 备份 FSM

`BackupEngine` 驱动状态机：

```
Idle → Scanning → Chunking → Storing → CommittingMeta → Auditing → Complete
  ↑                                                                    │
  └──────────────────────── Aborted ← (powerfail / error) ────────────┘
```

**提交点耐久性**（v0.4.6+）：manifest 写入 superblock 前必须 `chunk_store_->Flush()`（fsync）。中途 crash 时 txn 标记 `kAborted`，前一 snapshot 仍可 verify/restore。

**Coalesced meta**（v0.5+）：备份进行中 superblock 仅内存更新；正常完成或 abort 时落盘，减少 fsync 次数。

---

## Pipeline 拓扑演进

| 版本 | 拓扑 | 适用场景 |
|------|------|----------|
| v0.4.6 | 4-stage ×1，mmap FileView | 单线程 pipeline |
| v0.6 v2 | 4-stage + **2 Store workers** | 默认 EbPack |
| v0.7 v3 | **FileScheduler** + N×(R/C/C) + 共享 encode queue | 多文件并行 |
| v0.8 v4 | **全局 chunk/encoded 队列** + M Store + FileAggregator | L7 multi-file |
| v0.9.0 | ≤32MB **inline sequential** 快路径 | L3a ratio 稳定 |
| v0.9.2 | >32MB **StreamingChunkCpuPipeline** | L5 单文件大文件 |
| v0.9.4 | Phase B seg1 bulk + Phase A FastCdcSlice opt-in | CDC 优化 |

### 路由决策（`backup_pipeline.cc`，v0.9.4）

单文件 full backup、`worker_count==0`、无 `EBBACKUP_PIPELINE_WORKERS`：

1. **≤32MB** → `RunSingleFileInlinePipeline`
2. **>32MB** 且 `UseCdcFastPath()`（`EBBACKUP_CDC_FAST_SLICE=1`，mmap，非 force-stream）→ `ChunkFileStreamingFastSlice`
3. **>32MB** 默认 → `RunSingleFileStreamingChunkPipeline`
4. **多文件 / 增量 / workers>0** → `RunBackupPipeline`（Pipeline v4）

`EBBACKUP_FORCE_STREAM_CDC=1` 禁用 Phase A，强制 stream-feed CDC。

---

## 关键源文件

| 模块 | 路径 |
|------|------|
| 引擎 FSM | `engine_cpp/src/engine/backup_engine.cc` |
| Pipeline 路由 | `engine_cpp/src/pipeline/backup_pipeline.cc` |
| 文件调度 | `engine_cpp/src/pipeline/file_scheduler.cc` |
| 流式 CDC | `engine_cpp/src/chunk/fast_cdc_streaming.cc` |
| 整文件 CDC | `engine_cpp/src/chunk/fast_cdc.cc` |
| HCRBO/CFI | `engine_cpp/src/chunk/eb_hcrbo.cc` |
| ChunkStore | `engine_cpp/src/store/chunk_store.cc` |
| EbPack | `engine_cpp/src/store/ebpack_writer.cc` |
| Digest | `engine_cpp/src/common/digest_*.cc` |
| Phase 统计 | `engine_cpp/include/ebbackup/pipeline/pipeline_phase_stats.h` |

---

## 数据流（v0.9.4 默认单文件 >32MB）

```
mmap FileView
    │
    ▼
FastCdcStreamFeed (32MB blocks, main thread)
    │  cuts + DigestPool hash (digest_base on carry)
    ▼
ChunkTask queue ──► 2× Encode worker (LZ4/zstd)
    │
    ▼
EncodedChunkTask ──► 1× Store worker ──► EbPack shard append
    │
    ▼
FileAggregator ──► manifest entry ──► Flush/fsync ──► superblock commit
```

---

## 相关文档

- [CHUNK_AND_CDC.md](CHUNK_AND_CDC.md) — FastCDC、HCRBO、流式路径细节
- [STORAGE_AND_DURABILITY.md](STORAGE_AND_DURABILITY.md) — 仓库布局与耐久性
- [VERSION_HISTORY.md](../VERSION_HISTORY.md) — 版本演进时间线
- [PERF_BASELINE.md](../reference/PERF_BASELINE.md) — L1–L7 门禁
