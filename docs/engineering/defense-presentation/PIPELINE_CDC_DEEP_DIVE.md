# Pipeline 从零设计与 CDC 漂移 — 答辩深挖归档

**用途**：回答「Pipeline 怎么从零设计的」「性能怎么跃升的」「CDC 漂移怎么解决的」等**实现级**答辩追问。  
**关联**：[`DEFENSE_QA_ARCHIVE.md`](DEFENSE_QA_ARCHIVE.md) · [`CHUNK_AND_CDC.md`](../../technical/CHUNK_AND_CDC.md) · [`ecosystem-evolution/`](../ecosystem-evolution/) 第 7–8 章

---

## 一、Pipeline 从零：问题与第一版形态

### 1.1 最初要解决什么？

备份 Pipeline 的本质任务：

```
读文件 → 分块(CDC) → 算 hash → 压缩 → 写入 ChunkStore → 汇总 manifest entry
```

**v0.4.x 之前**：引擎偏「扫描 + 顺序读 + 单线程 hash」，没有独立的 **BackupPipeline** 抽象；大文件时 CPU（CDC+digest）与 I/O（读盘、写 pack）**串行**，L5 无绝对门禁。

### 1.2 v0.4.6：第一版 4-stage Pipeline（单线程）

| 阶段 | 职责 |
|------|------|
| Read | mmap `FileView` 映射文件 |
| Chunk | `FastCdcSlice::Chunk()` 整文件 CDC + digest |
| Encode | LZ4/zstd 压缩 |
| Store | 写入 Legacy chunk 或早期 pack |

**设计选择**：
- **mmap FileView**：减少 read syscall，随机访问 cut 边界；
- **单线程 4-stage**：实现简单，parity 易保证（同一线程顺序执行）；
- **与 FSM 解耦**：`BackupEngine` 管事务/manifest，Pipeline 管吞吐。

**局限**：encode/store 空等 chunk；大文件 chunk 阶段占满 CPU，**无 overlap**。

**源码**：`engine_cpp/src/pipeline/backup_pipeline.cc`（演进后仍保留 inline/sequential 路径）

---

## 二、拓扑演进全表（Wave 2 → v0.9.4）

| 版本 | 拓扑 | 关键改动 | L5 256MB（约） | L7 32×32MB（约） |
|------|------|----------|----------------|------------------|
| v0.4.6 | 4-stage ×1 | mmap + 首版 pipeline | 无绝对门禁 | — |
| v0.5 Wave 2 | + DigestPool/SIMD CDC opt-in | L5 floor 84→101 | ~76→101 级 | — |
| **v0.6** | **Pipeline v2：2 Store worker** | EbPack 默认 + coalesced meta | **~139**（+63 级跃升） | — |
| v0.7 Wave 3 | Pipeline v3 + FastCdcStreamFeed | 16-shard EbPack + SHA-NI | ~155 | — |
| **v0.8 Wave 4** | 全局 chunk/encoded 队列 + FileAggregator | 多文件并行 | ~146（停滞） | **~554→646+** |
| v0.9.0 Wave 5 | ChunkStore 并发修复 + ≤32MB inline 快路径 | 正确性优先 | 142–149 | ~598 |
| **v0.9.2** | **StreamingChunkCpuPipeline** | 32MB feed + 2 encode + 1 store | **~154** | ~601 |
| **v0.9.3** | digest_base 批处理 | digest 占 chunk 38%→16% | **159–163** | ~596–627 |
| **v0.9.4 Sprint 4** | Phase B seg1 bulk | CDC carry 后 handoff 实 loop | **170–172** | ~646–658 |
| Wave I | Hybrid CDC 默认 | 复用 stream feed + CI parity | ≥ stream×0.95 | 持平 |

**答辩一句话**：并行化 **Store** 救 L7，不自动救 L5；L5 要靠 **单文件路径分化 + CDC/digest overlap**（v0.9.2–0.9.4 四轮 sprint +18% L5）。

---

## 三、性能是怎么「跃升」的？（分五次跳跃）

### 跳跃 1：v0.6 EbPack + Pipeline v2（~76 → ~139 MB/s）

**机制**：
- **EbPack**：chunk 聚合写入 `.ebpack`，append 顺序 I/O，减少小文件随机写；
- **2 Store worker**：encode 与 store overlap（chunk 仍偏单线程）；
- **Coalesced meta**：备份中 superblock 内存更新，减少 fsync（P2 压力）。

**答辩点**：这是 **存储布局 + 双 store** 的跃升，不是 CDC 算法变了。

---

### 跳跃 2：v0.8 Pipeline v4（L7 ~554 MB/s，L5 仍 ~146）

**机制**：
- **FileScheduler** + N×(Read/Chunk/Chunk) worker；
- **全局 chunk 队列** + **EncodedChunkTask 队列** + M Store；
- **FileAggregator** 等多文件 manifest 汇总。

**为何 L5 不涨？**  
单文件大路径仍走 v4 或类似拓扑时，**调度开销 + 单文件无法吃满多文件并行**；profile 显示 chunk 占 **98%**（v0.9.0）——瓶颈在 CDC+digest，不在 store。

**答辩点**：v4 是 **multi-file 生态** 的正确投资；单文件需 **Wave 5 另开路径**（路由分化，而非 Pipeline v5）。

---

### 跳跃 3：v0.9.2 StreamingChunkCpuPipeline（142 → 154 MB/s）

**问题**：v4 拓扑下 chunk 98% → 单大文件必须 **feed 级流水线**。

**设计**（`RunSingleFileStreamingChunkPipeline`）：

```
主线程: FastCdcStreamFeed (32MB/block) → cuts + digest
           ↓ ChunkTask queue
Worker×2: Encode (LZ4/zstd)
           ↓ EncodedChunkTask queue
Worker×1: Store → EbPack shard
           ↓
FileAggregator → manifest
```

**关键**：CDC 扫下一块 32MB 时，encode worker 压缩**上一块**的 chunk——恢复 **pipeline overlap**。

**数据**：chunk 占比 98% → **83%**；L7 几乎不变（+0.5%）→ 证明 **分化未伤害 multi-file**。

---

### 跳跃 4：v0.9.3 digest_base（154 → 159–163 MB/s）

**问题**：stream_sub 里 **digest 占 chunk 内 ~38%**——carry feed 上重复构造缓冲、逐段 hash 效率差。

**解决**：
- **digest_base**：carry feed 上对 mmap **基址**做 file-absolute `DigestPool::HashRegions`（批处理 region）；
- **DigestPool span 分片**：池内并行 hash slot。

**数据**：digest 38% → **16%**；cdc 升至 ~82%；L5 +5–9 MB/s。

**剪枝**：Phase 3C「carry-boundary 拆 gear」— **parity 失败，0 MB/s 收益，回退**。

---

### 跳跃 5：v0.9.4 Phase B seg1 bulk（159–163 → 170–172 MB/s）

**问题**：carry 前缀用 **virtual segment** 扫描，进入 contiguous 主段后仍走 virtual loop → 多余分支，CDC 变慢。

**解决**（`fast_cdc_streaming.cc`）：
- `ChunkCarryPrefixVirtual` 处理 carry 前缀；
- 当 `pos >= view.len0`（carry 消耗完）→ **handoff** 到与 unified loop 相同的 **seg1-only contiguous CDC loop**；
- **禁止**用 `boundary_limit = view.len0 + w` 类简化边界（会导致 **cut 漂移**）。

**数据**：L5 +6–9% vs v0.9.3；L6 ~186 MB/s。

---

### 负向实验：Phase A FastCdcSlice（为何默认关闭）

**做法**：`EBBACKUP_CDC_FAST_SLICE=1` → 整文件 `FastCdcSlice::ChunkCuts()` 一次切完，再批 push encode。

**结果**：L5 **~105 MB/s**（-39%），因 **主线程扫完才 push**，encode overlap 丧失。

**答辩金句**：L3b **ratio 仍可能 ≥0.90**，但 **immutable ratio 捕不到绝对 MB/s 崩溃**——故守卫 **默认路径 + L5 绝对门禁**（Stage 3.1 floor 105 MB/s）。

---

## 四、CDC「漂移」是什么？怎么解决的？

### 4.1 定义：两类「漂移」

| 类型 | 含义 | 后果 |
|------|------|------|
| **Cut 漂移** | 流式 CDC 与整文件 CDC 的 **切点 offset 不一致** | Content ID 变、dedup 失效、restore 错块 |
| **Gear 状态漂移** | 跨 32MB feed 边界 rolling hash **状态未连续** | 边界附近 cut 偏移 1–N 字节 |

**恒定约束**：`SHA256(content)` Content ID **不可变**；任何路径必须 **bit-identical cut points**。

---

### 4.2 解决 Cut 漂移：parity 立法 + 测试

**策略**（`CHUNK_AND_CDC.md` Algorithm change policy）：
1. 算法改动必须先过 **FastCdcStreamFeed vs FastCdcSlice::Chunk** parity；
2. CI：`fast_cdc_streaming_test.cc`、`streaming_chunk_pipeline_test.cc`（256MB manifest hash 一致）；
3. Sprint 5：`ChunkCutsUntil` vs 全文件 `ChunkCuts`（`fast_cdc_test.cc`）。

**测试示例**（概念）：

```cpp
// fast_cdc_test.cc — ChunkCutsUntilMatchesFullCuts
chunker.ChunkCuts(bytes, size, &full_offsets, &full_lengths);
chunker.ChunkCutsUntil(bytes, size, feed_end, &cursor, &partial_offsets, ...);
// 每个 feed 窗口 partial 与 full 前缀一致
```

**答辩点**：不是「我们相信 stream 是对的」，而是 **每次改 CDC 必跑 parity**，失败就回退（Phase 3C 先例）。

---

### 4.3 解决 Gear 状态漂移：Carry tail + StreamSegmentView

**问题**：32MB feed 切文件时，rolling window 状态必须 **带到下一 feed**。

**实现**（v0.7+ → v0.9.2 强化）：
- **Carry tail**：保存 gear hash 窗口未消化字节 + 内部状态；
- **StreamSegmentView**（v0.9.2）：零拷贝 **虚拟段** 描述「carry 前缀 + 本 feed  contiguous 后缀」；
- **Bounded ring**：carry 缓冲有界，避免大 carry 拷贝爆炸。

**失败案例**：Phase 3C 尝试在 carry 边界 **拆分 gear** 以并行 CDC → **parity 失败** → 证明状态连续性不能随意切。

---

### 4.4 Phase B：virtual → contiguous handoff（Sprint 4 核心）

**错误做法**（导致漂移）：

```
在 contiguous 段仍用 virtual scanner，boundary_limit = len0 + w
→ 与 unified loop 在边界处 cut 决策不一致
```

**正确做法**：

```
carry 段: ChunkCarryPrefixVirtual
contiguous 段:  inline seg1 CDC loop（与 FastCdcSlice 同逻辑）
```

**源码**：`engine_cpp/src/chunk/fast_cdc_streaming.cc` — carry 消耗完毕后 handoff。

---

### 4.5 digest 与 CDC 的耦合漂移

**问题**：cut 点对但 **hash 区间**算错（carry feed 相对 offset vs file-absolute offset）→ hash 变。

**解决 v0.9.3 digest_base**：
- 所有 digest 在 **file-absolute offset** 上批处理；
- carry feed 只提供 **view 映射**，不改变 hash 语义。

---

## 五、当前路由决策（答辩必背）

`backup_pipeline.cc` 单文件 full backup、`worker_count==0`：

| 条件 | 路径 | 函数 |
|------|------|------|
| ≤32MB | Inline sequential | `RunSingleFileInlinePipeline` |
| >32MB，Hybrid 默认开启 | Hybrid stream feed | `ChunkFileStreamingHybrid` |
| >32MB，`EBBACKUP_CDC_FAST_SLICE=1` | Phase A whole-file cuts | `ChunkFileStreamingFastSlice` |
| >32MB，默认 | Streaming feed | `ChunkFileStreamingFeed` |
| 多文件 / 增量 / workers>0 | Pipeline v4 | `RunBackupPipeline` |

**环境变量**：
- `EBBACKUP_CDC_HYBRID=0` — 关 Hybrid（Wave I 默认开）
- `EBBACKUP_CDC_FAST_SLICE=1` — Phase A opt-in
- `EBBACKUP_FORCE_STREAM_CDC=1` — 强制 stream，禁 FastSlice
- `EBBACKUP_PIPELINE_PROFILE=1` — 打印 chunk/encode/store 毫秒分解

---

## 六、L5 profile 怎么读？（评委问「瓶颈在哪」）

v0.9.4 @ ~171 MB/s 典型：

| 层级 | 占比 | 含义 |
|------|------|------|
| chunk | **~82%** | CDC + digest |
| encode | ~16% | 压缩 |
| store | ~1% | EbPack append |
| read | ~0.5% | mmap 已摊销 |

**stream_sub**（chunk 内）：
- cdc **~82%**
- digest **~16%**
- carry **~1.7%**

**结论**：Stage 3.2 目标 280 MB/s 还需 **+109 MB/s**，主要战场仍在 **CDC scan**（Sprint 5 Hybrid 已默认，后续需 SIMD/算法级优化，文档记录为方向非承诺）。

---

## 七、答辩 Q&A 模拟（Pipeline / CDC 专集）

### Q1. Pipeline 从零到 v0.9.4 最关键的三次决策？

**答**：
1. **v0.6 默认 EbPack + 双 Store** — 第一次 L5 量级跃升（存储布局）；
2. **v0.8 Pipeline v4** — 服务 L7 multi-file，但暴露「单文件不能靠堆 worker」；
3. **v0.9.2 单文件 Streaming 分化** — 32MB feed + encode overlap，profile 从 98% chunk 降到可优化结构。

---

### Q2. 为什么不做 Pipeline v5，而是路由分化？

**答**：P5 单人维护 — v4 已服务 multi-file/增量；再加 v5 全量替换 **复杂度过高**。v0.9 用 **按文件大小/模式路由** 到 inline/stream/v4 三条路径，profile 证明 L5/L7 可独立优化。

---

### Q3. CDC 漂移怎么保证不出生产事故？

**答**：
1. **Content ID 不变量**写进文档与 code review 文化；
2. **parity 测试** stream vs slice、256MB manifest hash、ChunkCutsUntil；
3. **负向实验回退**（Phase 3C、Phase A 默认关闭）；
4. **immutable ratio floor** 不够，加 **L5 绝对 MB/s floor**（105 MB/s Stage 3.1）。

---

### Q4. FastCdcStreamFeed 和 FastCdcSlice 关系？

**答**：Slice 是 **golden reference**（mmap 整文件一次 CDC）；StreamFeed 是 **生产路径**（32MB 增量），二者 cut **必须 bit-identical**。StreamFeed 存在是为了 overlap encode，不是不同算法。

---

### Q5. HCRBO/CFI 和 Pipeline 什么关系？

**答**：**增量备份**路径。CFI 锚点 + Bloom 预滤定位变更区域，unchanged chunk **reuse manifest hash**，跳过 read/encode/store。不改变 Content ID；L2 bench 要求 1-byte edit 后 reuse ≥90%。

---

### Q6. Wave I Hybrid CDC 是什么？

**答**：默认启用（`EBBACKUP_CDC_HYBRID=0` opt-out）。复用 `FastCdcStreamFeed` 引擎保持 stream 吞吐；`ChunkCutsUntil` CI 验证与整文件 cuts 一致；`hybrid_stream_ratio_min≥0.95` 门禁。决策：**不再加 Pipeline v5**，在 stream 路径上叠 parity 保障的分窗优化。

---

### Q7. v0.9.0 为何先修 crash 再追分？

**答**：Pipeline v4 多 worker 下 ChunkStore `index_mu_` 导致 **heap corruption / 死锁**。L5 ~146 时若继续 chase 分数，**不可恢复 crash** 归零生态。Wave 5 首先 **ChunkStore 并发修复**，再分化 streaming。

---

### Q8. 增量备份走哪条 Pipeline？

**答**：**Pipeline v4**（`RunBackupPipeline`），HCRBO 决定哪些 chunk reuse。单文件 streaming 路径主要用于 **full backup 大文件**；增量需 CFI/HCRBO 与多文件调度，仍用 v4。

---

## 八、源码与测试索引

| 主题 | 文件 |
|------|------|
| 路由 | `engine_cpp/src/pipeline/backup_pipeline.cc` |
| 流式 CDC | `engine_cpp/src/chunk/fast_cdc_streaming.cc` |
| 整文件 CDC | `engine_cpp/src/chunk/fast_cdc.cc`, `fast_cdc_simd.cc` |
| HCRBO/CFI | `engine_cpp/src/chunk/eb_hcrbo.cc` |
| Phase 统计 | `engine_cpp/include/ebbackup/pipeline/pipeline_phase_stats.h` |
| Parity | `engine_cpp/tests/chunk/fast_cdc_streaming_test.cc` |
| Pipeline parity | `engine_cpp/tests/pipeline/streaming_chunk_pipeline_test.cc` |
| ChunkCutsUntil | `engine_cpp/tests/chunk/fast_cdc_test.cc` |
| 门禁 | `engine_cpp/bench/baselines/ci_floor.json` |

---

## 九、30 秒 / 2 分钟 / 5 分钟口播模板

### 30 秒（Pipeline 演进）

「我们从 v0.4 单线程 4-stage 起步，v0.6 EbPack+双 Store 把 L5 拉到约 139；v0.8 Pipeline v4 把 L7 拉到 550+ 但 L5 卡在 146，profile 显示 chunk 占 98%。v0.9.2 起单文件走 32MB Streaming，让 encode 与 CDC overlap，四轮 sprint 到 171 MB/s。CDC 漂移用 stream vs slice parity 测试立法，改不对就回退。」

### 2 分钟（CDC 漂移）

「CDC 漂移指流式分块和整文件分块的切点不一致，会导致 SHA256 变、dedup 失效。我们用 FastCdcSlice 作 golden，FastCdcStreamFeed 按 32MB feed 扫描，carry tail 保证 gear 状态跨 feed 连续。v0.9.4 Phase B 在 carry 消耗完后 handoff 到 contiguous CDC loop，避免 virtual boundary 简化导致 cut 偏移。每次改算法跑 fast_cdc_streaming_test 和 256MB manifest parity；Phase 3C 和 Phase A 负向实验证明：parity 失败或 overlap 丧失的路径必须默认关闭。」

---

*与 `PERF_BASELINE.md`、`ecosystem-evolution` 第 7–8 章、`CHUNK_AND_CDC.md` 同步维护。*
