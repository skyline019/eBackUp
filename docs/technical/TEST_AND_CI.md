# 测试与 CI

本文归档 ebbackup 测试分层、fixture、bench 门禁与 CI 工作流。

---

## 测试规模（v0.9.6）

- **393** GoogleTest 用例（`ebbackup_tests`）
- **6** `ebsync_tests`（`sync_cpp/`，ctest 注册）
- 独立 C API 测试 target：`eb_run_tests_capi`
- Bench 硬门禁：`ebbackup_bench_check`（ctest）
- Workbench：**13** Rust 集成测试（`workbench_integration.rs`，Windows CI）

---

## 测试分层

| 层级 | 目录/文件 | 说明 |
|------|-----------|------|
| 单元 | `tests/chunk/`、`tests/common/`、`tests/store/` | 算法、digest、EbPack |
| Pipeline | `tests/pipeline/` | v3/v4、streaming、parity |
| 集成 | `tests/integration/` | 端到端 backup/restore |
| CLI | `tests/cli/` | 子进程 `eb` 命令 |
| 失败注入 | `tests/failure/powerfail_test.cc` | `EBTEST_ABORT_AFTER` kill 点 |
| Fuzz / 解码 | `tests/fuzz/decode_corruption_test.cc` | chunk/EbPack payload 变异，`Get` 不 crash |
| Chaos | `tests/failure/` + `chaos_util` | 确定性 seed=42 |
| Bench | `tests/bench/bench_check.cc` | L1–L7 floor gate |
| Workbench | `gui/src-tauri/tests/workbench_integration.rs` | DLL JSON shim roundtrip（`npm run test:rust`） |

---

## Workbench GUI 测试（v0.9.5+）

```powershell
cmake --build build --config Release --target ebbackup_workbench
cd gui
npm run sync:runtime
npm run test:rust
```

依赖：`gui/src-tauri/bin/ebbackup_workbench.dll`（由 `sync_runtime_binaries.ps1` 从 `build/engine_cpp/Release/` 复制）。

文档：[`product/WORKBENCH_GUI.md`](../product/WORKBENCH_GUI.md)

| Fixture | 路径 | 用途 |
|---------|------|------|
| mixed / unicode / nested | `tests/fixtures/` | 基础 E2E |
| real_world | `tests/fixtures/real_world/full/` | 多类型 + 5 层嵌套 |
| media | checked-in JPEG/WebP/MP4/ZIP 等 | ContentClass |

环境变量：

- `EBTEST_FIXTURE_DIR` → fixtures 根
- `EBTEST_ENGINE_ROOT` → `engine_cpp/`
- `EBTEST_TMPDIR` → 临时输出（默认 `engine_cpp/test_output/`）

---

## Bench 门禁（ctest）

**Target**：`ebbackup_bench_check`

**Floor 文件**：[`engine_cpp/bench/baselines/ci_floor.json`](../../engine_cpp/bench/baselines/ci_floor.json)（Stage 3.1，CI blocking）

| Level | 门禁类型 |
|-------|----------|
| L1/L2 | 绝对 MB/s + reuse ≥ 90% |
| L3a/L3b | pipeline ratio ≥ 0.90（**immutable**） |
| L5/L6/L7 | 绝对 MB/s（Stage 3.1 ~70% 实测） |
| L4 | ampl_ratio + ContentClass CPU |

**Stage 3.2**（[`ci_floor_stage32.json`](../../engine_cpp/bench/baselines/ci_floor_stage32.json)）：aspirational，nightly/manual only。

**Override**：`EB_BENCH_FLOOR_PATH`

Bench 默认设置 `EBBACKUP_DIGEST_THREADS=4`。

---

## CI 工作流

**文件**：[`.github/workflows/ebbackup.yml`](../../.github/workflows/ebbackup.yml)

- Windows + Linux Release 构建（含 `sync_cpp` / `ebsync_tests`）
- `ctest` 含 `ebbackup_tests` + `ebbackup_bench_check` + `ebsync_tests`
- ASan job（v0.3.1+）
- **GUI**：`npm run build`（Linux + Windows）
- **Workbench IT**（Windows）：`ebbackup_workbench` + `cargo test workbench_integration`

触发路径含 `engine_cpp/**`、`sync_cpp/**`、`gui/**`。

---

## Powerfail 测试

`EBTEST_ABORT_AFTER=<enum>` 在指定 FSM 阶段注入 abort；子进程运行 `eb backup`，验证：

- txn → `kAborted`
- 前一 snapshot 仍可 verify
- strict/balanced pipeline kill 点覆盖

---

## 算法变更策略

修改 `eb_hcrbo.cc`、`fast_cdc.cc`、`backup_pipeline.cc` 必须：

1. 全部 gtest parity 通过
2. `ebbackup_bench_check` 通过
3. 不改变 Content ID / SHA256 语义

---

## 相关文档

- [PERF_BASELINE.md](../reference/PERF_BASELINE.md)
- [ENVIRONMENT_VARIABLES.md](ENVIRONMENT_VARIABLES.md)
- [`engine_cpp/README.md`](../../engine_cpp/README.md) — 本地构建与 ctest
