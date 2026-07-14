# Topo 后三代 CDC 独立技术手册

XeLaTeX 中文手册（v1.1 扩写）：覆盖 **Gen3 TopoCDC Hom**、**Gen4 TopoChain**、**Gen5 TopoPH（Gear 混合）** 与 **Gen5.1 TopoPH-Native**。含形式化切点公式、标定/Embed 数学、热路径复杂度、源码 listings 印证、流式 Resume 对照。版式对齐 [`kernel-manual`](../kernel-manual/)。

## 构建（WSL）

```bash
sudo apt install -y texlive-xetex texlive-lang-chinese texlive-latex-extra \
  texlive-latex-recommended texlive-fonts-recommended
cd /mnt/e/recoveryProjects/docs/engineering/topo-gens-manual
make pdf
# => build/main.pdf
```

或：

```bash
mkdir -p build
xelatex -output-directory=build -interaction=nonstopmode main.tex
xelatex -output-directory=build -interaction=nonstopmode main.tex
```

## 规格 SSOT

| 代 | Markdown |
|----|----------|
| Gen3 | `docs/technical/TOPO_CDC_SPEC.md` |
| Gen4 | `docs/technical/TOPO_CHAIN_GEN4_SPEC.md` |
| Gen5 | `docs/technical/TOPO_PH_GEN5_SPEC.md` |
| Gen5.1 | `docs/technical/TOPO_PH_NATIVE_SPEC.md` |
| 矩阵 | `docs/technical/CDC_ALGO_MATRIX.md` |

形象化拆解（类比 / 玩具例子 / 大量图例）见姊妹册：
[`docs/engineering/topo-illustrated-guide/`](../topo-illustrated-guide/)。
