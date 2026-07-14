# G-TCDC v2 证明专册

LaTeX 专册：块指纹合成定理、v2 Vector8/leapfrog 内核、L5 A/B 实验协议。

## 编译

```bash
cd docs/engineering/gtcdc-proof
make
```

需 XeLaTeX + ctex。

## 实测数据

```powershell
.\engine_cpp\bench\scripts\run_gtcdc_proof.ps1
```

将 `gtcdc_proof_summary.csv` 填入 `chapters/05-data.tex`。
