# G-TCDC 规格说明（v6）

G-TCDC（Gear-inspired Tensor CDC）是 ebbackup 的 **opt-in** 内容定义分块算法。启用后切点语义与 FastCDC **不互通**；同一明文在不同算法下会产生不同的 chunk 边界与 dedup 结果。

**证明专册**：[`docs/engineering/gtcdc-proof/`](../engineering/gtcdc-proof/README.md)

## 启用方式

```bash
export EBBACKUP_CDC_ALGO=gtcdc
```

默认（未设置或非 `gtcdc`）仍使用 FastCDC/Hybrid/Stream 路径，行为不变。

## 算法标识与内核版本

| 常量 | 值 | 含义 |
|------|-----|------|
| `kFastCdcAlgoId` | 0 | FastCDC（默认） |
| `kGtCdcAlgoId` | 1 | G-TCDC |
| `kBackupFeatureGtCdc` | 0x1000 | G-TCDC 仓库 |
| `kBackupFeatureGtCdcGear` | 0x2000 | v3 Gear 扫描内核（遗留） |
| `kBackupFeatureGtCdcNative` | 0x4000 | v4 Native 内核（冻结只读） |
| `kBackupFeatureGtCdcAnGear` | 0x8000 | v5 AN-Gear 内核（冻结只读） |
| `kBackupFeatureGtCdcTwoFGear` | 0x10000 | **v6 2F-Gear 生产内核** |

| 仓库 flags | 内核 | 说明 |
|---|---|---|
| `0x1000` only | Rabin v2 | 兼容已有仓库；性能 SLA 不适用 |
| `0x1000 \| 0x2000` | Gear v3 | 遗留仓库；只读/增量兼容，不升级 |
| `0x1000 \| 0x4000` | Native v4 | 冻结；继续服务已有 v4 仓库 |
| `0x1000 \| 0x8000` | AN-Gear v5 | 冻结；继续服务已有 v5 仓库 |
| `0x1000 \| 0x10000` | **2F-Gear v6** | **新仓库默认**；性能验收对象 |

内核解析优先级：`0x10000` → `kTwoFGear`；`0x8000` → `kAnGear`；`0x4000` → `kNative`；`0x2000` → `kGear`；仅 `0x1000` → `kRabin`。

新 G-TCDC 仓库 `InitRepoEx` 写入 `0x1000 | 0x10000`，并持久化 `gtcdc_table_seed`（非零随机）与 `gtcdc_nc_level = 2`（norm 通道 bit 分配参数）。

## v6 2F-Gear 内核（Release 热路径）

- **双指纹解耦**：`h_gear`（`beta_table` 标准 gear 滑窗）+ `h_norm`（`norm_table`，`InitKeyedGearTable(seed ^ 0x9E3779B9)`）
- **非重叠 bit 域双零检测**：将 `log2(avg)` bit 预算拆分为 `mask_lo`（低域）与 `mask_hi`（高域，`norm_shift = lo_bits`）
  - 切点：`(h_gear & mask_lo) == 0 && ((h_norm >> norm_shift) & mask_hi) == 0`
  - 期望 hit 率 ≈ `2^-(lo_bits+hi_bits) ≈ 1/avg`
- 无 `SelectPhaseMask` / `MixSeedHash`；扫描成本接近 FastCDC gear
- Streaming：`Process2FGearChunkCut` + 双 hash resume（`tf_gear_h` / `tf_norm_h`）；512KB 多 feed parity 已验收

## v5 AN-Gear 内核（冻结）

- **独立 Gear 同族**：`gtcdc_internal` 自实现 Gear 扫描（**零** `fastcdc_internal` 依赖；**不修改** `fast_cdc.cc`）
- **标准 gear 表** + **seed-mix 域隔离**：`h' = h ^ rotl(seed ^ (pos>>6), (seed&15)+1)`；`table_seed` 来自 superblock
- **三阶段自适应 mask（AN）**：按块内距离 `d = pos - chunk_start` 切换 mask 松紧
  - 严格（`d < avg >> nc_level`）：`BuildMask(avg << 1)`
  - 目标（`d < avg << nc_level`）：`BuildMask(avg)`
  - 宽松（else）：`BuildMask(avg >> 1)`
  - 切点：`(MixSeedHash(h, seed, pos) & phase_mask) == 0`
- 滑窗：`h = (h<<1) + gear[b] - gear[b-w]`

## v4 Native 内核（冻结）

- Keyed gear table + 双 mask NC（遗留语义；**不再用于新仓库**）
- 已有 `0x4000` 仓库继续服务；不得静默升级

## v3 Gear 内核（遗留）

- 单 mask Gear；默认 gear 表（`table_seed = 0`）
- 仅 `0x2000` 仓库继续服务

## v2 Rabin 内核（兼容）

从 chunk 起点 `pos` 起，令 `h_pos = 0`，对字节 `b_i`（`i >= pos`）：

\[
h_{i+1} = \alpha \cdot h_i + \beta(b_i) \pmod{2^{32}}
\]

- `alpha = 0x00010001`；`beta_table` 为基 gear 表（无 seed）
- 块 leapfrog 张量路径（AVX2）；scalar golden 用于 CI parity

## 切点规则（共用框架）

`ChunkProfile` → `GtCdcConfigForFileSize` / `GtCdcConfigForRepo`：

- `scan_start = pos + min_size`
- `cut_limit = min(pos + max_size, len)`
- Gear v3：单 mask；Native v4：双 mask NC；AN-Gear v5：三阶段 mask + seed-mix；2F-Gear v6：双指纹非重叠 bit 域
- 未命中且剩余超过 `max_size`：硬切于 `pos + max_size`；否则文件末尾

## Superblock 扩展字段

`BackupSuperBlockExtension`（自 `ext_padding` carve，不改 `format_version`）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `gtcdc_table_seed` | `uint32` | v6 norm 表种子 / v5 seed-mix / v4 keyed 表种子 |
| `gtcdc_nc_level` | `uint8` | v6 norm bit 分配 / v5 phase 宽度 / v4 NC 级别；新仓库默认 2 |
| `gtcdc_reserved` | `uint8[3]` | 保留 |

## 增量备份（HCRBO-GT）

- `EbHcrboGtChunker`：镜像 FastCDC HCRBO 的 CFI 遍历/复用逻辑，内层 `GtCdcSlice`
- Pipeline：`ChunkFileFullGtCdc` 支持 `kIncremental`（与 FastCDC 一致，增量大文件不走 streaming）
- v5/v4 增量使用 repo 级 `table_seed` / `gtcdc_nc_level`

## Streaming

- **2F-Gear v6 / AN-Gear v5 / Native v4 / Gear v3**：Gear 族 view 扫描 + `GtCdcSegmentView` 双段虚拟视图
- **2F/AN 多段 feed（512KB）**：切点 resume + 双段 `pos_bias`；parity 已验收（`GtCdc2FTest` / `GtCdcAnTest`）
- **Rabin v2**：`FeedViewRangeCompose` + `GtCdcScanView` 张量 scan
- `digest_base` 强制；`EBBACKUP_PIPELINE_PROFILE=1` 时启用 sub-timing

## 性能证明（v6）

| 工具 | 指标 |
|------|------|
| `ebbackup_gtcdc_bench` | `tensor_vs_scalar` p50（Rabin parity，参考） |
| `bench_check` `L5_gtcdc_ab` | `gtcdc_vs_stream_ratio`（主门禁）、`scan_ns_per_probe_ratio`（次门禁）、`scan_ns_ratio`（诊断） |
| `bench_check` `L6_gtcdc_incr_ab` | `incr_gtcdc_vs_stream_ratio` |
| `run_gtcdc_proof.ps1` | 10 次 L5 + microbench；双门禁 ratio≥1.0、scan_ns_per_probe≤1.05 |

CI 键：`gtcdc_vs_stream_ratio_min`、`gtcdc_scan_ns_per_probe_ratio_max`（`gtcdc_scan_ns_ratio_max` 默认 0，仅诊断）

## nofallback 合约

1. **路由**：`EBBACKUP_CDC_ALGO=gtcdc` 时不得进入 Hybrid/FastSlice/FastCDC stream
2. **扫描**：Rabin 路径 AVX2 不可用时 → `Unimplemented`；Gear/AN 路径使用 `gtcdc_internal` 自有扫描
3. **Digest**：流式 `digest_base == nullptr` → `InvalidArgument`

## 迁移矩阵

| 源仓库 | 操作 | 结果 |
|--------|------|------|
| FastCDC | 启用 gtcdc | 错误（不变） |
| v2 Rabin `0x1000` | 继续 backup | Rabin，性能 SLA 不适用 |
| v3 Gear `0x2000` | 继续 backup | v3 Gear，**不升级** |
| v4 Native `0x4000` | 继续 backup | v4 Native，**不升级** |
| v5 AN-Gear `0x8000` | 继续 backup | v5 AN-Gear，**不升级** |
| 新建 + `EBBACKUP_CDC_ALGO=gtcdc` | InitRepo | v6 `0x1000\|0x10000` + seed |
| v5 → v6 | 新建仓库 + full re-backup | **无就地升级** |

## 硬件要求

Release G-TCDC Rabin 张量路径需要 **AVX2**。2F-Gear v6 / AN-Gear v5 / Native v4 / Gear v3 与 FastCDC 相同 SIMD 要求。
