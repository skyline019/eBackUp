# E2E IO 稳态饱和证明专册

LaTeX 专册：数学模型、全部实测数据、定理证明与 pgfplots 图例。

## 构建（WSL）

```bash
cd docs/engineering/io-saturation-proof
make pdf
# 输出: build/main.pdf
```

或手动：

```bash
xelatex -output-directory=build -interaction=nonstopmode main.tex
xelatex -output-directory=build -interaction=nonstopmode main.tex
```

## 章节

| 文件 | 内容 |
|------|------|
| `chapters/01-model.tex` | 符号、两阶段模型、min-max 瓶颈 |
| `chapters/02-experiment.tex` | 拓扑图、硬件、workload、Pipeline 修复 |
| `chapters/03-data-nvme.tex` | E:/D:/C: 十次数据与图 |
| `chapters/04-data-usb.tex` | G: USB 十次数据与异盘对照图 |
| `chapters/05-theorems-proofs.tex` | 定理 1–4、推论、答辩口径 |
| `chapters/99-appendix.tex` | 原始向量、公式、源码索引 |
