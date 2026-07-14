# TopoCDC 规格说明（Gen3）

> **版本**：Hom-0 **v1.2**（2026-07）— 增量 UF slide + C++ 唯一验收链；Python eval 已废弃。

TopoCDC 是 ebbackup 的 **第三代 opt-in** 内容定义分块算法，与 FastCDC（第一代）、G-TCDC v6 2F-Gear（第二代）**并列**，而非 G-TCDC 的后续版本。

**G-TCDC v6 在第二代产品线内冻结**；TopoCDC 使用独立代码树、独立仓库 flag、独立 dedup 域。

Gen4 **TopoChain**（`EBBACKUP_CDC_ALGO=topochain`，`0x40000`）为 Gear-free 可选路径，规格见 [TOPO_CHAIN_GEN4_SPEC.md](TOPO_CHAIN_GEN4_SPEC.md)；本文档仍以 Hom-0 为 Gen3 默认。

## 启用方式

```bash
export EBBACKUP_CDC_ALGO=topocdc
```

与 `gtcdc` **互斥**；未设置或非 `topocdc` 时走 FastCDC / G-TCDC 既有路径。

## 算法标识

| 常量 | 值 | 含义 |
|------|-----|------|
| `kFastCdcAlgoId` | 0 | FastCDC（默认） |
| `kGtCdcAlgoId` | 1 | G-TCDC |
| `kTopoCdcAlgoId` | 2 | **TopoCDC** |
| `kBackupFeatureTopoCdc` | `0x20000` | TopoCDC 仓库（**不含** `0x1000`） |

| 仓库 flags | variant | 说明 |
|---|---|---|
| `0x20000` | `topo_variant=0` | **Hom-0** 生产候选（默认） |
| `0x20000` | `topo_variant=1` | Tri 研究轨（opt-in，Phase 2b+） |

新仓库 `InitRepoEx` 在 `EBBACKUP_CDC_ALGO=topocdc` 时写入 `0x20000`，并持久化 `topo_table_seed`（非零随机）与 `topo_variant=0`。

## 切点语义（Hom-0 v1.2 主线）

```
primaryOK = (h & mask) == 0              # 单路 Gear 滑窗 + mask（v2 标定）
topoOK    = (C_t != C_{t-1})             # 滑动窗口 UF 分量数变化（ΔC）
切点      = primaryOK AND topoOK
```

- **Primary 指纹**：`InitKeyedGearTable(topo_table_seed)`，与 FastCDC / G-TCDC 参数表隔离
- **滑窗** `W_t`：长度 `window_w`（默认 64）；键 `key = gear[b] & 0xFF`
- **UF 语义**：环缓冲 slot 上 **邻接同键 union**；`topoOK` 当滑动一步后连通分量数 `C` 发生变化（birth / death / merge）
- **v1.2 性能**：热路径 **lazy UF**——仅当 `primaryOK` 时对窗口做 O(w) gear 键化 + `PrimaryTopoDelta`（O(1) 三边 `edge_diff` 增量，`C = 1 + edge_diff`）；非 primary 探测与 Stream 相同仅滚动 hash；`SlotUfWindow::Slide` 保留 O(1) 环缓冲增量供增量对照 / 单测；`SlideViaRebuild` / `RebuildComponents` 作黄金对照
- **mask 标定 v2**：`CalibrateTopoPermille` 测 `P_topo` → `mask = BuildMask(avg × P_topo)`；持久化 `topo_calib_permille`（`topo_reserved[0:1]`）；标定样本 **1MB** heap（与 `InitRepoEx` 一致）
- **fallback**：`topo_shift` 仅当 `topo_calib_permille == 0`

### Python eval 废弃

`tools/topo_cdc_eval/` **不得**用于 CI / Go/No-Go。验收见下文「验收 SSOT」。

### 与 G-TCDC v6 2F-Gear 对照

| | G-TCDC v6 | TopoCDC Hom v1.1 |
|--|-----------|------------------|
| 第二条件 | `(h_norm >> s) & mask_hi == 0` | UF ΔC 事件 |
| 第二状态 | 第二 Gear hash | 环 UF + 分量计数 |
| dedup 域 | `0x1000 \| …` | `0x20000` |

## 切点规则（共用框架）

`ChunkProfile` → `TopoCdcConfigForFileSize` / `TopoCdcConfigForRepo`：

- `scan_start = pos + min_size`
- `cut_limit = min(pos + max_size, len)`
- 命中：`primaryOK && topoOK` → cut at `pos`
- 未命中且剩余超过 `max_size`：硬切于 `pos + max_size`；否则文件末尾

## Superblock 扩展字段

自 `BackupSuperBlockExtension.ext_padding` carve（总大小不变）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `topo_table_seed` | `uint32` | Gear 表种子 |
| `topo_variant` | `uint8` | 0=Hom v1.1，1=Tri |
| `topo_reserved[0:1]` | `uint16` | **`topo_calib_permille`** ≈ `P_topo×1000` |
| `topo_reserved[2]` | `uint8` | 保留 |

## Streaming

- **512KB feed** 增量扫描，与整文件 `TopoCdcSlice::ChunkCuts` **bit-identical cut points**
- **Resume 状态** `TopoCdcStreamState`：`{h, scan_rel, window_w}`（`TopoHomResume`；UF 状态不在 resume 中物化，primary 命中时按需重建）
- `digest_base` 强制；`EBBACKUP_PIPELINE_PROFILE=1` 时启用 sub-timing
- **nofallback**：禁用 Hybrid / FastSlice / FastStream

## 硬约束

| 约束 | 说明 |
|------|------|
| Content ID | `SHA256(content)`，与算法路径无关 |
| Parity | 整文件 ≡ 512KB 多 feed 流式 |
| opt-in / nofallback | `topocdc` 时禁止回退 FastCDC / G-TCDC |
| dedup 三域隔离 | FastCDC / G-TCDC / Topo **互不复用** |
| 性能目标 | Hom scan ≤ FastCDC Stream 的 **1.25×**（`topo_scan_ns_per_probe_ratio`） |

## 迁移矩阵

| 源仓 | 目标 | 操作 |
|------|------|------|
| FastCDC | Topo | 新 InitRepo + `topocdc` + 全量重备 |
| G-TCDC v6 | Topo | 新 InitRepo + 全量重备 |
| G-TCDC v6 | G-TCDC v6 | 继续，**冻结** |
| Topo | FastCDC / G-TCDC | **禁止**就地转换 |

## 性能证明（Phase 3+）

### 验收 SSOT（C++ only）

| 层级 | 工具 |
|------|------|
| 正确性 | `ebbackup_tests` — `TopoCdcHomTest.*`, `TopoGearParityTest.*` |
| 切分/微基准 | `ebbackup_topo_cdc_eval`（`--json-out`、1MB 标定、`topo_calib_permille`） |
| 端到端 | `ebbackup_bench_check` — `L5_topo_ab` |
| 一键 | `engine_cpp/bench/scripts/run_topo_proof.ps1` |

| 工具 | 指标 |
|------|------|
| `bench_check` `L5_topo_ab` | `topo_vs_stream_ratio`（主门禁 ≥ 0.97） |
| `bench_check` `L5_topo_ab` | `topo_scan_ns_per_probe_ratio`（次门禁 ≤ 1.25） |
| `ebbackup_topo_cdc_eval` | 三族 `chunks` / `mean_chunk` / `scan_ms` JSON |
| `run_topo_proof.ps1` | 单测 → eval → L5 归档 |

## 相关文档

- [CDC_ALGO_MATRIX.md](CDC_ALGO_MATRIX.md) — 三族对照与路由
- [TOPO_CDC_TRI_RESEARCH.md](TOPO_CDC_TRI_RESEARCH.md) — Tri 研究轨协议
- [CHUNK_AND_CDC.md](CHUNK_AND_CDC.md) — 索引入口
