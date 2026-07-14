# TopoPH-Native Gen5.1 规格（H0 过滤事件替代 Gear-CDC）

TopoPH-Native 用滑动 landmark 上的 **VR-H0 过滤事件**（+可选稳定寿命阈值）与 **Flip τ 控率** 作为切点判据，**禁止** Gear / Rabin / `(h&mask)` / Chain LFSR-stride。  
Tri-Native 为同动机廉价代理（仅定点 ΔFlip）。

**冻结**：FastCDC 默认、Hom（`0x20000`）、全部 `topo_chain*`、Gen5.0（`0x80000` Gear+事件）切点不变。

## 标识

| 字段 | 值 |
|------|-----|
| `kBackupFeatureTopoPhNative` | `0x100000` |
| `kTopoPhnAlgoId` | `4` |
| `topo_variant` | `5` = Tri-Native，`6` = PH-H0-Native |
| 运行时 | `EBBACKUP_CDC_ALGO=topophn` |
| 子核 | `EBBACKUP_TOPOPHN_KERNEL=tri\|ph`（缺省 `tri`） |

## 定理剪枝

| 保留 | 用途 |
|------|------|
| \(H_0\) / VR 连通分量 | PH 切点门控 \(\Delta\beta_0\)（必过） |
| 过滤 + 寿命阈值 \(\delta\) | 可选；`persist` bit 持久化 |
| 稳定性（bottleneck）叙事 | **仅评测**（1-byte reuse），热路径不算 \(W_\infty\) |
| 定点 Flip | Tri 切点；PH 上作 τ 控率 |

| 剪掉 | 原因 |
|------|------|
| 神经定理 / Künneth / 对偶 / \(H_{k\ge2}\) | 不产可标定切点 |
| 在线 bottleneck / 全局 PH / H1 | 成本或过重 |

## 切点公式

定点嵌入：以 `table_seed` 为种子，在位置 \(p\) 对因果邻域 \(p-3..p\) 做局部 LCG+XOR 得 \(Y\)（**无**跨文件累积 mix；实现可闭合展开为同权多项式，**结果与 4 步迭代 bit-identical**；热路径 AVX2 候选批）。`event_stride` 标定后收成 **最近二的幂**（平局偏小），landmark 条件为 \(Y\,\&\,(\mathrm{stride}-1)=0\)（等价 \(\bmod\)；平均间距≈stride；**内容定相**）。切点仍仅为 Tri=`|ΔFlip|≥τ` / PH=`flipOK∧ph0OK`。时间轴 \(T\) 取本块内相对偏移。切后清空 landmark 环。`Flip` 轴对 `q_mod` 取模。

默认 `k_points`：Tri=8，PH=16。PH ε² 网格为嵌套 `{1,4,16}`（一次边排序 + 分层 UF），`∃j Δβ0`。

### Tri-Native (variant=5)

```
cut = (|Flip(L_t)-Flip(L_{t-1})| ≥ τ) ∧ pos≥min ∧ (距上次切 ≥min)
```

ΔFlip 常为近二值（0/1）：**控率主旋钮为 `event_stride`**（τ≈1），标定以 stride 搜索为主。

### PH-H0-Native (variant=6)

```
β0_j(t) = #π0(VR_{R_j²}(L_t))
ph0OK   = ∃j: β0_j(t)≠β0_j(t-1)
flipOK  = (|Flip(L_t)-Flip(L_{t-1})| ≥ τ)
若 enable_persist_δ: ph0OK 还要求 PersistSpan(β0) ≥ δ
cut = flipOK ∧ ph0OK ∧ gap     // 默认；与 Tri 切点必须分化
```

mean 过大 densify 顺序：**τ → k → stride**；stride 软下界 `max(32, avg/1024)`，仅 τ=1∧k=4 后才逼近 8。

τ 不写入超块：由 `topo_table_seed` + 固定 stride/k/persist 经 `CalibratePhnRuntimeParams` **确定性重导**（backup 进程内 Pipeline 缓存，避免每文件重导）。

## Superblock（Native 仓）

| 字段 | 用途 |
|------|------|
| `topo_table_seed` | 嵌入/标定 RNG 种子（非 Gear） |
| `topo_variant` | 5 或 6 |
| `topo_reserved[0:1]` | `event_stride`（uint16） |
| `topo_reserved[2]` | 低 5 bit = `k_points`（0→16）；bit5 = persist δ 使能 |

InitRepo / Runtime 标定样本 **4 MiB** `FillPhnCalibSample`；标定窗 mean ∈ [0.85,1.15]×avg。Pipeline 按 Small/Default/Large（`avg_size`）各缓存一份 runtime knobs，避免跨档文件重复标定（切点公式不变）。

## 互斥

| 仓 | 运行时 | 结果 |
|----|--------|------|
| Native | 非 `topophn` | Fail |
| FastCDC / G-TCDC / Hom / Chain / Gen5.0 | `topophn`+manifest | Fail |
| Native Tri↔PH | 就地切换 kernel | Fail |
| `topophn` | 同时 `topoph`/`topochain`/… | Fail |

## 验收

| 层 | 工具 |
|----|------|
| 单测 | `TopoPhnTriTest.*` / `TopoPhnH0Test.*`（含多 feed 流式） |
| eval | `tri_native`/`ph_native` 切点不同；`reuse_1byte`≥80（实测→100）；PH mean≤avg×1.6；`scan_ms`≤300 |
| 门禁 | `L5_topophn_ab`：`vs≥0.80`，`scan_ns/probe≤5.0` |
| 一键 | `engine_cpp/bench/scripts/run_topophn_proof.ps1`（含可选 `D:\CUDA` smoke） |
| 语料 | `run_topophn_cuda_prod.ps1`：真实 NVIDIA Toolkit 树 Init+Backup（缺源则 skip） |

## 相关

- Gen5.0（Gear 残留）：[TOPO_PH_GEN5_SPEC.md](TOPO_PH_GEN5_SPEC.md) — **非** CDC 替代
- Chain 冻结：[TOPO_CHAIN_GEN4_SPEC.md](TOPO_CHAIN_GEN4_SPEC.md)
- [CDC_ALGO_MATRIX.md](CDC_ALGO_MATRIX.md)
