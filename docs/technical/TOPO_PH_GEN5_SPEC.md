# TopoPH Gen5 规格（独立 Tri-v2 + PH-H0）

TopoPH 是 ebbackup **第五代** opt-in CDC，与 FastCDC / G-TCDC / TopoCDC Hom / **TopoChain** **独立 dedup 域**。  
**禁止**修改 TopoChain 内核；Chain 代码树冻结。

> **定位**：Gen5.0 为 **Gear primary ∧ 事件过滤** 混合态，**不是**无 Gear 的 CDC 原生替代。  
> Native 范式 B（H0/Tri 事件作真切点、无 Gear）见 [TOPO_PH_NATIVE_SPEC.md](TOPO_PH_NATIVE_SPEC.md)（`0x100000` / `topophn`）。本规格（`0x80000`）切点语义冻结，勿再改。

## 标识

| 字段 | 值 |
|------|-----|
| `kBackupFeatureTopoPh` | `0x80000` |
| `kTopoPhAlgoId` | `3` |
| `topo_variant` | `3` = Tri-v2，`4` = PH-H0 |
| 运行时 | `EBBACKUP_CDC_ALGO=topoph` |
| 子核 | `EBBACKUP_TOPOPH_KERNEL=tri\|ph`（缺省 `tri`） |

旧 Gen3 Tri（`0x20000` + `topo_variant=1` + `EBBACKUP_TOPO_VARIANT=tri`）**deprecated**，仅只读保留。

## 切点语义

### 公共 Layer-1（Gear primary）

```
primaryOK = (h & mask) == 0
h 为 Gear 滑窗滚动指纹（与 Hom 同形，seed=topo_table_seed）
```

Landmarks `L_t`：仅在 **primaryOK** 时压入最近 K 点（K≤24）；坐标定点 `(t mod Qt, h mod Qh)`。

### Tri-v2（variant=3）

```
triOK = ΔFlip(L_{t-1}→L_t) ≠ 0
切点  = primaryOK ∧ triOK ∧ pos ≥ min_size
```

`Flip` 为定点方向谓词计数（整数 cross ≤0）；与 Gen3 浮点 `DelaunayFlipCount` **不要求** bit-identical。

### PH-H0（variant=4）

```
对固定整数半径平方网格 {R²_i} 建 VR_ε 图，β0_i = 连通分量数
ph0OK = ∃i: β0_i(t) ≠ β0_i(t-1)
切点  = primaryOK ∧ ph0OK ∧ pos ≥ min_size
```

首版 **仅 H0**；非全局 PH；非 TDA 外包库。

## Superblock

| 字段 | 用途 |
|------|------|
| `topo_table_seed` | Gear 种子 |
| `topo_variant` | 3 或 4 |
| `topo_reserved[0:1]` | `topo_calib_permille`（反推 mask） |
| `topo_reserved[2]` | `k_points`（默认 16，钳制到 [4,24]） |

## 互斥 / 拒迁

| 仓 | 运行时 | 结果 |
|----|--------|------|
| TopoPh | 非 `topoph` | Fail |
| FastCDC / G-TCDC / Topo / **Chain** | `topoph` + 已有 manifest | Fail |
| TopoPh Tri(v=3) | `TOPOPH_KERNEL=ph` | Fail |
| TopoPh PH(v=4) | `TOPOPH_KERNEL=tri` | Fail |
| `topoph` | 同时 `topochain`/`topocdc`/`gtcdc` | Fail |

## 验收

| 层 | 工具 |
|----|------|
| 单测 | `TopoPhTriTest.*` / `TopoPhH0Test.*` |
| eval | `ebbackup_topo_cdc_eval` 增加 `tri_v2` / `ph_h0` |
| 门禁 | `L5_topoph_ab`：`vs_stream≥0.95`，`scan_ns/probe≤2.0` |
| 一键 | `engine_cpp/bench/scripts/run_topoph_proof.ps1` |

## 相关

- [CDC_ALGO_MATRIX.md](CDC_ALGO_MATRIX.md)
- [TOPO_CDC_TRI_RESEARCH.md](TOPO_CDC_TRI_RESEARCH.md)（Gen3 Tri deprecated）
- **不**修改 [TOPO_CHAIN_GEN4_SPEC.md](TOPO_CHAIN_GEN4_SPEC.md) 切点
