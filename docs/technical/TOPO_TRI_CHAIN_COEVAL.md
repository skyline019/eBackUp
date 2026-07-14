# Topo Tri / Chain 共存评估（Phase 2）

本文档定义 C++ 五族切分 eval 与 Tri/Chain 并行研究轨的 SSOT。

## 五族 eval

工具：`ebbackup_topo_cdc_eval`（Release 构建）

| 族 | 标识 | 说明 |
|----|------|------|
| `stream` | FastCDC | Gen1 基线 |
| `gtcdc` | G-TCDC v6 2F-Gear | Gen2 基线 |
| `topo` | Hom-0 (`topo_variant=0`) | Gen3 默认 Topo |
| `tri` | Tri (`topo_variant=1`) | Gear + Delaunay flip proxy |
| `chain` | TopoChain (`0x40000`, beta1 on) | Gen4 Gear-free |

```powershell
& .\ebbackup_topo_cdc_eval.exe --json-out eval.json
```

输出 JSON：`families.{name}.chunks / mean_chunk / scan_ms`。

## 域隔离

```
Hom-0:  0x20000 / variant=0  → topo_hom_scan.cc
Tri:    0x20000 / variant=1  → topo_tri_scan.cc   (EBBACKUP_TOPO_VARIANT=tri)
Chain:  0x40000 / variant=2  → topo_chain_scan.cc (EBBACKUP_CDC_ALGO=topochain)
```

- Tri 与 Hom **同 dedup 域**，InitRepo 时 `EBBACKUP_TOPO_VARIANT=tri` 写 `topo_variant=1`。
- Chain **独立 dedup 域**；与 Tri/Hom 不可混仓。
- Hom↔Tri 不可就地切换 variant。

## 单元测试

| 套件 | 覆盖 |
|------|------|
| `TopoCdcHomTest.*` | Hom-0 增量 UF、streaming |
| `TopoCdcTriTest.*` | Tri 确定性、streaming parity |
| `TopoCdcChainTest.*` | Chain beta1、parallel scan、streaming |

## 一键 proof

```powershell
engine_cpp/bench/scripts/run_topochain_proof.ps1
```

步骤：Chain+Tri 单测 → 五族 eval → `L5_topochain_ab`。

## 相关

- [TOPO_CHAIN_GEN4_SPEC.md](TOPO_CHAIN_GEN4_SPEC.md)
- [TOPO_CDC_TRI_RESEARCH.md](TOPO_CDC_TRI_RESEARCH.md)
- [CDC_ALGO_MATRIX.md](CDC_ALGO_MATRIX.md)
