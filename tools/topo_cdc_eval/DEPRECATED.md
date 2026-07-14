# DEPRECATED — Python TopoCDC eval

**本目录已废弃。** 不得用于 CI、Go/No-Go 或产品验收。

## 替代路径（C++ SSOT）

| 用途 | 工具 |
|------|------|
| 正确性 | `ebbackup_tests` — `TopoCdcHomTest.*`, `TopoGearParityTest.*` |
| 三族切分/扫描耗时 | `ebbackup_topo_cdc_eval` |
| 端到端性能 | `ebbackup_bench_check` — `L5_topo_ab` |
| 一键证明 | `engine_cpp/bench/scripts/run_topo_proof.ps1` |

## 规格

- [TOPO_CDC_SPEC.md](../../docs/technical/TOPO_CDC_SPEC.md) v1.2
- [CDC_ALGO_MATRIX.md](../../docs/technical/CDC_ALGO_MATRIX.md)

## 历史脚本（勿运行）

`eval_corpus.py`, `gear_parity_test.py`, `run_tracked_eval.ps1`, `watch_progress*.ps1`, `agent_progress_loop.ps1`
