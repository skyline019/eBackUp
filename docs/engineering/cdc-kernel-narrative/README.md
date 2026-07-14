# ebbackup CDC 内核叙事专册

FastCDC、Gear mask 推导、流式防漂移、HCRBO/CFI 增量 reuse、Pipeline 五路径路由 — 形象化 LaTeX 专册。

## 构建（WSL）

```bash
cd docs/engineering/cdc-kernel-narrative
make pdf
# 输出: build/main.pdf
```

## 章节

| 文件 | 内容 |
|------|------|
| `chapters/01-narrative-overview.tex` | 内核叙事总览、数据平面分层 |
| `chapters/02-fastcdc-gear.tex` | FastCDC、Gear、mask 数学推导 |
| `chapters/03-streaming-drift.tex` | StreamFeed、carry、Phase B、漂移防护 |
| `chapters/04-hcrbo-cfi.tex` | CFI 锚点、Bloom、增量 skip |
| `chapters/05-pipeline-routing.tex` | 五路径路由决策树与时序 |
| `chapters/06-invariants-parity.tex` | 不变量、parity 测试、环境变量 |
| `chapters/99-appendix.tex` | 源码索引、答辩口播 |

## 关联文档

- [`../kernel-manual/`](../kernel-manual/) — 工程技术手册（第 5/6/12/13 章）
- [`../../technical/CHUNK_AND_CDC.md`](../../technical/CHUNK_AND_CDC.md) — 实现要点
- [`../defense-presentation/PIPELINE_CDC_DEEP_DIVE.md`](../defense-presentation/PIPELINE_CDC_DEEP_DIVE.md) — 答辩深挖
