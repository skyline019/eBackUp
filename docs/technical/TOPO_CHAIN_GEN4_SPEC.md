# TopoChain Gen4 规格（Phase 2 + Limit Push）

TopoChain 是 Gear-free 的 CDC 切分内核（Gen4），与 Gen3 Hom-0（`EBBACKUP_CDC_ALGO=topocdc`）**独立 dedup 域**，不可就地迁移。

## 标识

| 字段 | 值 / 含义 |
|------|-----------|
| `backup_features` | `kBackupFeatureTopoChain = 0x40000` |
| `topo_variant` | `2`（Hom=0, Tri=1, Chain=2） |
| `chunk_algo_id` | `2`（与 TopoCDC 同族 ID，由 variant 区分） |
| 运行时 | `EBBACKUP_CDC_ALGO=topochain` |

## 切点语义（Phase 2：Tier-0 + Tier-1 + Tier-2）

```
Q(b)     = b >> quant_q          // quant_q ∈ {0,1,2}，默认 0
S_t      = rotl(S_{t-1}, 1) ^ Q(b_t)
strideOK = (S_t & stride_mask) == 0    // stride_mask = 2^log - 1
topoOK   = Δβ̂₀ ≠ 0                     // 1-骨架边差分（与 Hom PrimaryTopoDelta 同构）
beta1OK  = β̂₁_gf2 > 0  或 beta1 关闭   // Tier-2 环检测（InitRepo 默认开启）
切点     = strideOK ∧ topoOK ∧ beta1OK ∧ pos ≥ min_size
```

**实现优化不改切点**：Limit Push 热路径改写（环缓冲增量、β̂₁ bitset、并行预热等）必须与本公式 **bit-identical**。

### 热路径复杂度（实现）

| 步骤 | 非 stride 字节 | stride 命中 |
|------|----------------|-------------|
| Tier-0 XOR-LFSR | O(1) | O(1) |
| 窗口键重建 + Δβ̂₀ | — | O(w) |
| β̂₁_gf2（bitset + 触碰列表） | — | O(w) |
| 8 字节批处理 + prefetch | 摊销 | 摊销 |

> Limit Push：在 **不改变切点** 前提下加速 β̂₁、并行预热、resume 信任 `S`；stride 间不维护每字节窗口（避免 O(1)×N 总开销压过 lazy 探测收益）。


## Superblock 持久化

| 字段 | Chain 用途 |
|------|------------|
| `topo_table_seed` | LFSR 初始状态（非 Gear 表种子） |
| `topo_reserved[0:1]` | `chain_stride_log`（M = 2^log），合法范围 **[8, 22]** |
| `topo_reserved[2]` | `chain_quant_q`，合法范围 **[0, 7]** |
| `topo_reserved[3]` | `kChainFeatureBeta1 = 0x01`（Phase 2 默认 InitRepo 置位） |

InitRepo 对 1MB 样本调用 `CalibrateChainStrideLog`（含 beta1），使 mean chunk ∈ [0.85, 1.15]×avg。

## 安全拒绝矩阵（RunBackup / Verify）

| 条件 | 结果 |
|------|------|
| TopoChain 仓 + 无 `topochain` | Fail |
| FastCDC 仓 + `topochain` + 已有 manifest | Fail |
| `topo_variant ≠ 2`（Chain 仓） | Fail |
| `chain_stride_log ∉ [8,22]` | Fail |
| `chain_quant_q > 7` | Fail |
| MVP 仓（无 beta1 bit）就地置 beta1 | Fail（须新 InitRepo） |

**MVP→beta1 升级**：已有 MVP 仓不可就地升级；须新 InitRepo。

## 并行 scan

| 变量 | 值 | 效果 |
|------|-----|------|
| `EBBACKUP_TOPOCHAIN_PARALLEL_SCAN` | `1` | 256KiB 分段并行 scan，取全局最早切点 |

- 串行预热各段首 LFSR `s_at_seg[i]`，避免每线程 O(span) 重放。
- 自左向右 join：左段命中后 cancel 右段（语义仍取最早 cut）。
- streaming 跨 feed 有 carry/resume 时强制串行（`force_serial`）；resume 持久化 `S` + 线性 keys 窗口。

## 互斥矩阵

| 仓库 | 运行时 | 结果 |
|------|--------|------|
| TopoChain (`0x40000`) | 无 `topochain` | Fail |
| FastCDC | `topochain` + 已有 manifest | Fail（禁止就地转换） |
| TopoCDC (`0x20000`) | `topochain` | Fail |
| TopoChain | `topocdc` / `gtcdc` | Fail |
| G-TCDC | `topochain` | Fail |

`topochain`、`topocdc`、`gtcdc` 三者运行时互斥。

## Pipeline

```
if CdcTopoChainEnabled():
    ChunkFileStreamingTopoChain(...)
elif CdcTopoCdcEnabled():
    ChunkFileStreamingTopoCdc(...)
...
```

`TopoCdcSlice` 在 `variant=kChain` 时不调用 `InitGearTable`。

## 与 Hom-0 同构性（草图）

Hom-0 使用 Gear 键化 `gear[b]`；Chain 使用量化键 `Q(b)`。两者在固定窗口 w 上构造相同形式的 1-骨架边计数 `ed`，`PrimaryTopoDelta` / `ChainPrimaryDelta` 对 slide 的边差分更新公式一致，故 β̂₀ 事件检测逻辑同构；Tier-0 仅替换 stride 触发机制（Gear mask → XOR-LFSR stride）。

## 验收 SSOT（C++）

| 层级 | 工具 |
|------|------|
| 正确性 | `ebbackup_tests` — `TopoCdcChainTest.*`（含 adversarial / beta1 parity） |
| 切分 eval | `ebbackup_topo_cdc_eval`（五族：stream/gtcdc/topo/tri/chain） |
| 性能 | `ebbackup_bench_check` — `L5_topochain_ab`（`vs_stream ≥ 0.97`，`scan_ns_per_probe ≤ 1.30`） |

Limit Push 微基准（8MB 随机，`ebbackup_topo_cdc_eval`）：`chain.scan_ms` 约 **10ms**（Phase2 未优化 β̂₁/并行预热前典型约 33ms）。
| 一键 | `engine_cpp/bench/scripts/run_topochain_proof.ps1` |

## 与 Tri 共存评估

见 [TOPO_TRI_CHAIN_COEVAL.md](TOPO_TRI_CHAIN_COEVAL.md)。Tri 仍在 Hom dedup 域（`0x20000` + `topo_variant=1`），与 Chain（`0x40000`）独立。
