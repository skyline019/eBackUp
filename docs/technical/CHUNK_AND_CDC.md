# 分块与 CDC 技术细节

本文归档 FastCDC、HCRBO/CFI、流式 CDC 与 Sprint 4 双路径的实现要点与 parity 约束。

---

## Content ID 语义（不可变）

所有 pipeline 路径、CDC 实现、digest 栈必须产出相同的 **chunk 边界** 与 **SHA256(content)** Content ID。任何优化必须通过 parity 测试验证，不得改变 manifest hash。

---

## FastCDC

**实现**：`engine_cpp/src/chunk/fast_cdc.cc`、`fast_cdc_simd.cc`（AVX2）

- Gear hash rolling window，参数由 `ChunkProfile` 按文件大小调节（min/avg/max chunk size）
- `FastCdcSlice::Chunk()` — 整文件 mmap 一次 CDC
- `FastCdcSlice::ChunkCuts()` — v0.9.4 新增，仅返回 cut 点列表（供 Phase A 批处理 digest）

### FastCdcStreamFeed（v0.7+）

**实现**：`engine_cpp/src/chunk/fast_cdc_streaming.cc`

- 按 **32MB feed** 增量扫描，与 `FastCdcSlice::Chunk` **bit-identical cut points**
- **Carry tail**：跨 feed 边界的 rolling state；v0.9.2 起用 `StreamSegmentView` 零拷贝虚拟段 + bounded ring
- **digest_base**（v0.9.3）：carry feed 上对 mmap 基址做 file-absolute `DigestPool::HashRegions`

### Phase B — seg1 bulk scan（v0.9.4 Sprint 4）

`ChunkCarryPrefixVirtual` 处理 carry 前缀虚拟段；当 `pos >= view.len0`（carry 消耗完毕）后，切换为与 unified loop 相同的 **seg1-only contiguous CDC loop**，避免在已 contiguous 区域继续 virtual 扫描。

**Parity 要点**：`boundary_limit = view.len0 + w` 类简化边界会导致 cut 偏移；正确做法是 handoff 到 inline seg1 loop，与 unified loop 的 seg1 分支一致。

### Phase A — FastCdcSlice 整文件路由（opt-in）

**环境变量**：`EBBACKUP_CDC_FAST_SLICE=1`

**实现**：`ChunkFileStreamingFastSlice()` in `backup_pipeline.cc`

1. `FastCdcSlice::ChunkCuts()` 一次得到全文件 cuts
2. 按 ~32MB batch 调用 `DigestPool` hash + push encode queue

**默认关闭原因**：whole-file CDC 在主线程完成后再 push，**失去** 8×32MB feed 与 2 encode worker 的 overlap；实测 ~105 MB/s vs 默认 stream ~172 MB/s。

**Opt-out**：`EBBACKUP_FORCE_STREAM_CDC=1`

---

## HCRBO 增量分块

**实现**：`engine_cpp/src/chunk/eb_hcrbo.cc`

- **CFI（Content Fingerprint Index）**：锚点 + offset index，快速定位可能变更区域
- **CFI rolling checksum**：rolling 预检失败则扩展 stale 窗口（v0.1 batch skip）
- **CFI Bloom**（v0.5 可选）：负向预滤，减少 false anchor 查找

增量备份时：unchanged chunk 直接 reuse manifest hash，跳过 read/encode/store。

**Bench L2**：1-byte edit @ 5MB 后 reuse ≥ 90%（immutable floor）。

---

## Pipeline 阶段统计

`PipelinePhaseStats`（`pipeline_phase_stats.h`）：

| 字段 | 含义 |
|------|------|
| `chunk_ns` | 分块 + digest 总时间 |
| `encode_ns` | 压缩 |
| `store_ns` | EbPack 写入 |
| `stream_cdc_ns` | 流式 CDC 扫描 |
| `stream_digest_ns` | 流式 digest |
| `stream_carry_ns` | carry 缓冲管理 |

L5 bench 打印 `profile_pct` 与 `stream_sub` 占比。`EBBACKUP_PIPELINE_PROFILE=1` 输出完整毫秒 breakdown。

---

## v0.9.4 L5 瓶颈画像

256MB StreamingChunkCpuPipeline @ ~171 MB/s：

| Phase | 占比 |
|-------|------|
| chunk (CDC+digest) | ~82% |
| encode | ~16% |
| store | ~1% |

Stream sub：cdc ~82%，digest ~16%，carry ~1.7%

**后续优化方向**（文档记录，非承诺）：Hybrid ChunkCuts + feed-timed replay（Sprint 5）以恢复 encode overlap。

---

## 测试与 parity

| 测试文件 | 覆盖 |
|----------|------|
| `fast_cdc_streaming_test.cc` | Stream vs slice parity |
| `streaming_chunk_pipeline_test.cc` | 256MB manifest parity、topology 断言 |
| `pipeline_v4_*_test.cc` | v4 chunk 边界、multi-file |

算法变更策略：见 [`PERF_BASELINE.md`](../reference/PERF_BASELINE.md) Algorithm change policy。

---

## 相关文档

- [ARCHITECTURE.md](ARCHITECTURE.md)
- [ENVIRONMENT_VARIABLES.md](ENVIRONMENT_VARIABLES.md)
- [VERSION_HISTORY.md](../VERSION_HISTORY.md) — v0.9.2–v0.9.4 条目
