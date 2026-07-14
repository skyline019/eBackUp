# CDC 算法三族对照矩阵

本文档汇总 FastCDC（Gen1）、G-TCDC v6（Gen2）、TopoCDC（Gen3）的标识、路由与迁移规则。

## 总览

| 叙事代 | 产品 | `chunk_algo_id` | 运行时 env | 仓库 flag | 代码域 |
|--------|------|-----------------|------------|-----------|--------|
| 第一代 | FastCDC Stream | 0 | （默认，无 env） | 无 G-TCDC / 无 Topo | `fast_cdc*.cc` |
| 第二代 | G-TCDC v6 2F-Gear | 1 | `EBBACKUP_CDC_ALGO=gtcdc` | `0x1000 \| 0x10000` | `gt_cdc*.cc`（**v6 冻结**） |
| 第三代 | TopoCDC Hom/Tri | 2 | `EBBACKUP_CDC_ALGO=topocdc` | `0x20000` | `topo_cdc/*.cc` |
| 第四代 | TopoChain | 2 (`topo_variant=2`) | `EBBACKUP_CDC_ALGO=topochain` | `0x40000` | `topo_chain_*.cc`（**冻结，勿改切点**） |
| 第五代 | TopoPH Tri-v2 / PH-H0（Gear 混合） | 3 | `EBBACKUP_CDC_ALGO=topoph` | `0x80000` | `topo_ph/*.cc`（**冻结混合态，非 Native**） |
| 第五·一代 | TopoPH-Native Tri / PH-H0 | 4 | `EBBACKUP_CDC_ALGO=topophn` | `0x100000` | `topo_phn/*.cc` |

## 环境变量

| 变量 | 值 | 效果 |
|------|-----|------|
| （未设置） | — | FastCDC 默认路径 |
| `EBBACKUP_CDC_ALGO` | `gtcdc` | G-TCDC pipeline；InitRepo 写 G-TCDC flags |
| `EBBACKUP_CDC_ALGO` | `topocdc` | TopoCDC pipeline；InitRepo 写 `0x20000` |
| `EBBACKUP_CDC_ALGO` | `topochain` | TopoChain Gen4 pipeline；InitRepo 写 `0x40000` |
| `EBBACKUP_CDC_ALGO` | `topoph` | TopoPH Gen5 Gear+事件；InitRepo 写 `0x80000` |
| `EBBACKUP_TOPOPH_KERNEL` | `tri` / `ph` | Gen5 子核（缺省 `tri` → variant=3；`ph` → variant=4） |
| `EBBACKUP_CDC_ALGO` | `topophn` | TopoPH-Native Gen5.1；InitRepo 写 `0x100000` |
| `EBBACKUP_TOPOPHN_KERNEL` | `tri` / `ph` | Native 子核（缺省 `tri` → variant=5；`ph` → variant=6） |
| `EBBACKUP_TOPO_VARIANT` | `tri` | InitRepo 写 `topo_variant=1`（**Gen3 deprecated**；须配合 `topocdc`） |
| `EBBACKUP_TOPOCHAIN_PARALLEL_SCAN` | `1` | Chain 256KiB 分段并行 scan |
| `EBBACKUP_CDC_ALGO` | 其他 | 无效，等同 FastCDC |

**互斥**：同一进程内 `gtcdc`、`topocdc`、`topochain`、`topoph`、`topophn` 不可同时生效。

## 仓库 feature flags

| Flag | 值 | 族 |
|------|-----|-----|
| `kBackupFeatureGtCdc` | `0x1000` | G-TCDC 基标记 |
| `kBackupFeatureGtCdcTwoFGear` | `0x10000` | G-TCDC v6 内核 |
| `kBackupFeatureTopoCdc` | `0x20000` | TopoCDC Hom/Tri（**独立**，不含 `0x1000`） |
| `kBackupFeatureTopoChain` | `0x40000` | TopoChain Gen4（**独立**，与 `0x20000` 不互通；**代码冻结**） |
| `kBackupFeatureTopoPh` | `0x80000` | TopoPH Gen5 Tri-v2 / PH-H0（Gear 混合） |
| `kBackupFeatureTopoPhNative` | `0x100000` | TopoPH-Native Gen5.1 |

## Pipeline 路由（`backup_pipeline.cc`）

```
if CdcTopoPhnEnabled():
    ChunkFileStreamingTopoPhn(...)
elif CdcTopoPhEnabled():
    ChunkFileStreamingTopoPh(...)
elif CdcTopoChainEnabled():
    ChunkFileStreamingTopoChain(...)
elif CdcTopoCdcEnabled():
    ChunkFileStreamingTopoCdc(...)
elif CdcGtCdcEnabled():
    ChunkFileStreamingGtCdc(...)
elif hybrid / fast_slice / stream:
    ...
```

## InitRepo / RunBackup 校验（`backup_engine.cc`）

| 仓库状态 | 运行时 | 结果 |
|----------|--------|------|
| Topo (`0x20000`) | 无 `topocdc` | **Fail**：要求 `EBBACKUP_CDC_ALGO=topocdc` |
| FastCDC | `topocdc` + 已有 manifest | **Fail**：禁止就地转换 |
| G-TCDC | `topocdc` | **Fail**：dedup 域不兼容 |
| Topo | `gtcdc` | **Fail**：dedup 域不兼容 |
| 无 manifest | `topocdc` | InitRepo 写 `0x20000` |

RunBackup 恢复 flags 时：**Topo 仓必须恢复 `0x20000` + `topo_table_seed`**，禁止误清为 FastCDC。

## dedup 隔离

三种仓库的 chunk hash 域 **互不互通**：

- FastCDC 仓 → 仅 FastCDC 切点产生的 Content ID
- G-TCDC 仓 → 仅 G-TCDC 切点
- Topo 仓 → 仅 TopoCDC 切点

跨族复用率为 **0%**（by design）。

## 迁移表

| 源 | 目标 | 允许 | 操作 |
|----|------|------|------|
| FastCDC | G-TCDC | 否（就地） | 新仓 + 全量重备 |
| FastCDC | Topo | 否（就地） | 新仓 + `topocdc` + 全量重备 |
| G-TCDC | Topo | 否（就地） | 新仓 + 全量重备 |
| G-TCDC | G-TCDC | 是 | 继续增量 |
| Topo | Topo | 是 | 继续增量 |
| TopoChain | TopoChain | 是 | 继续增量 |
| 任意 | 任意（跨族） | 否 | 必须新 InitRepo |

## Bench 命名空间

| 前缀 | 族 | 示例键 |
|------|-----|--------|
| （默认 L1–L4） | FastCDC | `pipeline_ratio` |
| `gtcdc_*` | G-TCDC | `gtcdc_vs_stream_ratio` |
| `topo_*` | TopoCDC | `topo_vs_stream_ratio` |
| `topochain_*` | TopoChain Gen4 | `topochain_vs_stream_ratio` |
| `topoph_*` | TopoPH Gen5（Gear 混合） | `topoph_vs_stream_ratio` |
| `topophn_*` | TopoPH-Native Gen5.1 | `topophn_vs_stream_ratio`（floor≥0.80）；eval `reuse_1byte_pct`（Tri≥80）；**Tri≠PH**；PH `scan_ms`≤300；滑动 EmbedY4 + pow2 mask；pipeline 按 `avg_size` 缓存 runtime；可选 `D:\CUDA` smoke |

**禁止**在 G-TCDC bench 键下混入 Topo 指标。

**Hom v1.1 标定**：`InitRepoEx` 写入 `topo_calib_permille`（`topo_reserved[0:1]`）；mask v2 由 `P_topo` 反推。RunBackup 恢复时一并读回。

## 验收 SSOT（C++ only）

| 层级 | 工具 |
|------|------|
| 正确性 | `ebbackup_tests` — `TopoCdcHomTest.*`, `TopoGearParityTest.*`, `TopoPhn*` |
| 切分/微基准 | `ebbackup_topo_cdc_eval`（含 `tri_native`/`ph_native`、`reuse_1byte`；Tri≠PH） |
| 性能 | `ebbackup_bench_check` — `L5_topo_ab` / `L5_topophn_ab` |
| 一键 Native | `engine_cpp/bench/scripts/run_topophn_proof.ps1`（含可选 `run_topophn_cuda_prod.ps1`） |
| 一键 Topo | `engine_cpp/bench/scripts/run_topo_proof.ps1` |

`tools/topo_cdc_eval/`（Python）**已废弃**，不得用于 CI 或 Go/No-Go。

## 规格 SSOT

| 族 | 文档 |
|----|------|
| FastCDC | [CHUNK_AND_CDC.md](CHUNK_AND_CDC.md) |
| G-TCDC | [GT_CDC_SPEC.md](GT_CDC_SPEC.md) |
| TopoCDC | [TOPO_CDC_SPEC.md](TOPO_CDC_SPEC.md)（Hom 仍为 Gen3 默认） |
| TopoChain | [TOPO_CHAIN_GEN4_SPEC.md](TOPO_CHAIN_GEN4_SPEC.md)（Gen4 opt-in；**勿再改内核**） |
| TopoPH | [TOPO_PH_GEN5_SPEC.md](TOPO_PH_GEN5_SPEC.md)（Gen5 Gear+事件；**非** CDC 原生替代） |
| TopoPH-Native | [TOPO_PH_NATIVE_SPEC.md](TOPO_PH_NATIVE_SPEC.md)（Gen5.1 H0 过滤事件） |
| Tri/Chain eval | [TOPO_TRI_CHAIN_COEVAL.md](TOPO_TRI_CHAIN_COEVAL.md) |
