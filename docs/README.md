# ebbackup 文档归档（v0.1 → v0.9+）

本目录为 **recoveryProjects / ebbackup** 的正式技术文档归档，汇总从 M5 内核里程碑到 v0.9.4 的功能演进、架构、ABI、性能门禁、测试矩阵与实现细节。

**权威变更日志（逐条 commit 级明细）** 仍以仓库根目录 [`CHANGELOG.md`](../CHANGELOG.md) 为准；本目录提供结构化归档与交叉索引。

---

## 快速导航

| 文档 | 说明 |
|------|------|
| [engineering/kernel-manual/](engineering/kernel-manual/) | **LaTeX 工程技术手册**（v1.1 完整扩写，193 页 PDF，XeLaTeX） |
| [engineering/ecosystem-evolution/](engineering/ecosystem-evolution/) | **LaTeX 生态理念与本土化发展实录**（压力驱动时间线、剪枝、流程反思；**第 12 章 L1–L7 实测数据**） |
| [engineering/defense-presentation/](engineering/defense-presentation/) | **答辩演讲稿**（Beamer 幻灯片 + 逐字稿双 PDF；课程 8–10 min / 技术 15–20 min） |
| [WAVE_ARCHIVE.md](WAVE_ARCHIVE.md) | **Wave 1–T 深化归档**（性能 Wave + 能力 Wave A–T、ABI v14–v29） |
| [VERSION_HISTORY.md](VERSION_HISTORY.md) | **主归档**：v0.1–v0.9.5 时间线、逐版本技术档案、ABI/Feature/Bench 演进 |
| [CHANGELOG_ARCHIVE.md](CHANGELOG_ARCHIVE.md) | CHANGELOG 完整镜像（与根目录同步归档） |
| [reference/PERF_BASELINE.md](reference/PERF_BASELINE.md) | L1–L7 性能基线、CI 门禁、Stage 3.1/3.2、各版本实测表 |
| [technical/ARCHITECTURE.md](technical/ARCHITECTURE.md) | 分层架构、备份 FSM、Pipeline 拓扑演进 |
| [technical/STORAGE_AND_DURABILITY.md](technical/STORAGE_AND_DURABILITY.md) | 仓库布局、EbPack、索引、快照、提交点耐久性 |
| [technical/CHUNK_AND_CDC.md](technical/CHUNK_AND_CDC.md) | FastCDC、HCRBO、CFI、流式 CDC、Sprint 4 双路径 |
| [technical/ABI_AND_FEATURES.md](technical/ABI_AND_FEATURES.md) | C API ABI v7–v29、Feature Flags、Init 默认行为 |
| [technical/TEST_AND_CI.md](technical/TEST_AND_CI.md) | 测试分层、fixture、CI/ASan、bench ctest 门禁 |
| [technical/ENVIRONMENT_VARIABLES.md](technical/ENVIRONMENT_VARIABLES.md) | 运行时/测试/ bench 环境变量 |
| [product/WORKBENCH_GUI.md](product/WORKBENCH_GUI.md) | 桌面 Workbench GUI（Tauri + Vue） |
| [product/BACKUP_CAPABILITY_GAPS.md](product/BACKUP_CAPABILITY_GAPS.md) | **备份能力缺口表**（非网络、创新命题、状态跟踪） |
| [technical/CAPABILITY_ROADMAP.md](technical/CAPABILITY_ROADMAP.md) | **能力创新路线图**（分阶段 API/文件/测试） |

---

## 产品手册（用户向）

| 文档 | 说明 |
|------|------|
| [product/WORKBENCH_GUI.md](product/WORKBENCH_GUI.md) | **桌面 Workbench GUI** 归档（构建、Activity、透明度、shim API、测试、排错） |
| [../engine_cpp/README.md](../engine_cpp/README.md) | CLI、`eb init/backup/restore`、filter、加密、C API 速查 |
| [../gui/README.md](../gui/README.md) | Workbench 快速入口 |
| [../README.md](../README.md) | 仓库根说明、构建与 ctest 入门 |

---

## 版本快照（Release Windows, Stage 3.1）

| 版本 | L5 256MB | L3b ratio | L7 multi | 备注 |
|------|----------|-----------|----------|------|
| v0.9.4 | ~170–172 MB/s | ~1.01–1.05 | ~646–658 MB/s | Sprint 4 CDC Phase B；Phase A opt-in |
| v0.9.3 | ~159–163 MB/s | ~1.01–1.13 | ~596–627 MB/s | digest_base 批处理 |
| v0.9.2 | ~154 MB/s | ~1.04 | ~601 MB/s | StreamingChunkCpuPipeline |
| v0.9.0 | ~142–149 MB/s | ~0.96–0.98 | ~598 MB/s | Wave 5 并发修复 + Stage 3 分层 |
| v0.8.0 | ~146 MB/s | — | ~554 MB/s | Pipeline v4 + L7 门禁 |

当前：**C API ABI v29** · ctest **363** gtest（+ C API + bench 门禁）· Wave A–T 能力 sprint 已归档（见 [WAVE_ARCHIVE.md](WAVE_ARCHIVE.md)）。

---

## 恒定约束（全版本不变）

| 约束 | 说明 |
|------|------|
| Content ID | `SHA256(content)`，不因 digest 栈或 pipeline 路径改变 |
| 提交点耐久性 | manifest commit 前 `Flush`/fsync；中断 txn → `kAborted` / 前一 snapshot |
| Immutable CI | `reuse_pct_min≥90`、`pipeline_ratio*_min≥0.90` 不可降低 |

---

*归档维护：与 CHANGELOG 新版本发布同步更新 `VERSION_HISTORY.md` 与 `CHANGELOG_ARCHIVE.md`。*
