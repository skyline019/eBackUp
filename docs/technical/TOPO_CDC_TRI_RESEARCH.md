# TopoCDC-Tri 研究轨实验协议

> **Deprecated（Gen3）**：生产/新研究请改用 **Gen5 TopoPH**（[`TOPO_PH_GEN5_SPEC.md`](TOPO_PH_GEN5_SPEC.md)，`EBBACKUP_CDC_ALGO=topoph`）。  
> 本文档与 `topo_variant=1` 路径仅保留只读对照；**不要**在此轨实现真 PH。

TopoCDC **Tri** 变体使用局部 Delaunay flip 事件作为第二切点条件，与 Hom-0 并行评估，**默认不进入生产路径**。

## 切点语义（Tri）

```
primaryOK = (h & mask) == 0
topoOK    = DelaunayFlipCount(K_points) >= 1
切点      = primaryOK AND topoOK
```

- **Primary**：与 Hom 相同（独立 Gear 表 + mask）
- **2D 嵌入**：对滑动窗口内最近 `K` 个探测点，坐标 `(t mod K, h mod Q)`，`Q=65521`
- **Tri 事件**：对窗口内点集执行局部 Delaunay 增量 flip 检测；flip 计数 ≥ 1 则 `topoOK`

## 阶段划分

| Phase | 范围 | 交付 |
|-------|------|------|
| Phase 1 | 离线 only（**已废弃 Python**） | 历史 `tri_scan.py`；验收不含 Tri |
| Phase 2b | Tri 胜出 Go | `topo_tri_scan.cc` + `topo_variant=1` |
| Phase 3+ | bench | `topo_tri_*` 诊断键（非主门禁） |

> **注意**：`tools/topo_cdc_eval/` Python 路径已废弃。Tri 暂不纳入 Hom MVP 验收。

## 离线实验协议（Phase 1）

### Corpus

与 Hom 评估共用：

1. **合成随机**：8MB × seed ∈ {37, 41, 55, 93, 99}
2. **源码树**：`engine_cpp/src/chunk/` 聚合（若存在）
3. **CUDA 树**（可选）：`D:\CUDA` 或 env `TOPO_EVAL_CUDA_ROOT`

### 参数网格

| 参数 | 扫描范围 |
|------|----------|
| `K`（Delaunay 点数） | 8, 16, 32 |
| `topo_shift`（mask 松紧） | 0, 1, 2 |
| `window_w` | 64（固定，与 Hom 对齐） |

### 输出指标

- 切点 CDF（chunk length 分布）
- mean chunk vs `ChunkProfile.avg`（目标 ±15%）
- **1-byte reuse 模拟**：在 5MB 偏移插入 1 字节编辑，统计 unchanged chunk 比例
- 预估 probes/byte（Tri 允许高于 Hom，但记录比值）

### Go/No-Go 标准（相对 Hom + G-TCDC v6）

| 条件 | Go | No-Go |
|------|-----|-------|
| mean chunk | ∈ [0.85×avg, 1.15×avg] | 超出 |
| 1-byte reuse | **显著优于** Hom（+5pp 以上）且 ≥ G-TCDC v6 | 否 |
| probes/byte vs Stream | ≤ 2.0×（研究轨放宽） | > 2.0× |
| 确定性 | 同输入同 cut | 非确定 |

**默认决策**：Phase 1 结束时若 Tri 未同时满足 Go 条件，**Hom 仍为 Topo 默认 variant**；Tri 保留为 research 文档与离线工具。

## C++ 进入条件（Phase 2b — 已实现）

- `topo_tri_scan.cc` + streaming 路由
- superblock `topo_variant=1`（Hom dedup 域 `0x20000`，**非** Chain `0x40000`）
- InitRepo：`EBBACKUP_CDC_ALGO=topocdc` + `EBBACKUP_TOPO_VARIANT=tri`
- Hom↔Tri variant 不可就地切换

## 不做

- 整文件全局 Delaunay
- 全图 persistent homology
- 在 G-TCDC 内核内挂载 Tri 逻辑

## 相关

- [TOPO_CDC_SPEC.md](TOPO_CDC_SPEC.md)
- [CDC_ALGO_MATRIX.md](CDC_ALGO_MATRIX.md)
- `tools/topo_cdc_eval/README.md`
