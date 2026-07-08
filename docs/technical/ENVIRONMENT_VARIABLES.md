# 环境变量参考

运行时、测试与 bench 使用的环境变量汇总。

---

## 备份 Pipeline

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `EBBACKUP_PIPELINE_WORKERS` | 0 | 显式 pipeline worker 数；>0 强制 Pipeline v4 |
| `EBBACKUP_PIPELINE_PROFILE` | off | `1` 时备份结束打印完整 phase 毫秒 breakdown |
| `EBBACKUP_DIGEST_THREADS` | `max(4, hw/2)` | `DigestPool` 线程数；bench 设为 4 |
| `EBBACKUP_CDC_FAST_SLICE` | off | `1` 启用 Phase A 整文件 `FastCdcSlice` 路由（v0.9.4） |
| `EBBACKUP_CDC_HYBRID` | on (default) | `0` 禁用；默认启用 Sprint 5 Hybrid CDC（stream-feed 引擎；`hybrid_stream_ratio_min` ≥ 0.95） |
| `EBBACKUP_FORCE_STREAM_CDC` | off | `1` 强制 stream-feed CDC，禁用 fast slice |
| `EBBACKUP_AUDIT_KEY` | — | 设置后启用审计相关路径（开发/测试） |

---

## Bench

| 变量 | 说明 |
|------|------|
| `EB_BENCH_FLOOR_PATH` | 覆盖 `ci_floor.json` 路径 |

---

## 测试

| 变量 | 说明 |
|------|------|
| `EBTEST_TMPDIR` | 测试临时目录 |
| `EBTEST_FIXTURE_DIR` | fixture 根目录 |
| `EBTEST_ENGINE_ROOT` | engine_cpp 根（CopyEngineSourceSample 等） |
| `EBTEST_ABORT_AFTER` | powerfail 注入：FSM 阶段 enum 整数值 |
| `EBTEST_CI` | CI 构建标记（缩减部分 extended 测试） |

编译期（CMake）：

- `EBTEST_EB_EXE` — `eb` 可执行路径（子进程测试）
- `EBTEST_OUTPUT_DIR` — 默认 test output

---

## 使用示例

```powershell
# L5 完整 phase profile
$env:EBBACKUP_PIPELINE_PROFILE = "1"
.\build\engine_cpp\Release\ebbackup_bench_check.exe

# 对比 CDC fast slice vs 默认 stream（parity 测试用）
$env:EBBACKUP_CDC_FAST_SLICE = "1"
$env:EBBACKUP_FORCE_STREAM_CDC = "0"

# 自定义 bench floor
$env:EB_BENCH_FLOOR_PATH = "e:\recoveryProjects\engine_cpp\bench\baselines\ci_floor_profile.json"
```

---

## 相关文档

- [CHUNK_AND_CDC.md](CHUNK_AND_CDC.md) — CDC 路径与环境变量交互
- [TEST_AND_CI.md](TEST_AND_CI.md)
- [PERF_BASELINE.md](../reference/PERF_BASELINE.md)
