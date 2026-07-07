# ebbackup 备份内核工程技术文档

XeLaTeX 中文技术手册（**v1.1 完整扩写**）：**Part I** 覆盖 `engine_cpp/` 备份内核 16 章；**Part II** 覆盖 bench 门禁、C API、compact/GC 与测试矩阵 4 章。目标 **120+ 页** PDF。

## 构建

### WSL / Linux

```bash
sudo apt install -y texlive-xetex texlive-lang-chinese texlive-latex-extra \
  texlive-latex-recommended texlive-fonts-recommended
cd /mnt/e/recoveryProjects/docs/engineering/kernel-manual
mkdir -p build
xelatex -output-directory=build -interaction=nonstopmode main.tex
xelatex -output-directory=build -interaction=nonstopmode main.tex
# => build/main.pdf
```

或使用 `make pdf`（需安装 `make`）。

## 目录结构

```
kernel-manual/
  main.tex
  preamble.tex
  chapters/          # 00 + 01–20 + 99
  Makefile
  build/             # PDF 输出
```

## 章节（v1.1）

| 章 | 内容 |
|----|------|
| 01 | 概述与设计目标 |
| 02 | 总体架构 |
| 03 | 不变量与耐久 |
| 04 | BackupEngine 与 FSM |
| 05 | FastCDC / Gear 分块 |
| 06 | HCRBO 增量与 CFI |
| 07 | Digest / 加密 |
| 08 | ChunkStore |
| 09 | EbPack / HXID 索引 |
| 10 | Manifest v1–v4 格式 |
| 11 | Snapshot / GFS 保留 |
| 12 | Pipeline v4 |
| 13 | StreamingChunkCpuPipeline / Sprint 4 |
| 14 | 恢复与验证 |
| 15 | RAR / Merkle |
| 16 | CLI / Daemon |
| **Part II** | |
| 17 | Bench L1–L7 |
| 18 | C API ABI v12 |
| 19 | Compact / GC |
| 20 | 测试与 CI |
| 99 | 附录 |

参考模板：eB-Tree [`kernel-manual`](../../../../DBProject/Docs/engineering/kernel-manual)

Markdown 归档：[`docs/README.md`](../../README.md)
