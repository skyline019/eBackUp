# ebbackup Workbench

桌面备份仓库工作台（Tauri 2 + Vue 3）。

**完整文档（归档）**：[`docs/product/WORKBENCH_GUI.md`](../docs/product/WORKBENCH_GUI.md)

## 快速开始

```powershell
# 1. 编译 DLL
cmake --build E:\recoveryProjects\build --config Release --target ebbackup_workbench

# 2. 启动（必须用 tauri:dev，浏览器 dev 无法调用 API）
cd E:\recoveryProjects\gui
npm install
npm run tauri:dev
```

## 常用命令

| 命令 | 说明 |
|------|------|
| `npm run tauri:dev` | 开发（桌面） |
| `npm run build:desktop` | Release NSIS + 便携 exe |
| `npm run test:rust` | DLL JSON shim 集成测试（Windows；需 `EBBACKUP_DLL_DIR` 或 `sync:runtime`） |
| `npm run sync:runtime` | 同步 `ebbackup_workbench.dll` / `eb-sync.exe` |
| `npm run sync:fonts` | 从 eB-Tree 同步字体 |

## CI

GitHub Actions（Windows）会构建 `ebbackup_workbench` 并运行 `workbench_integration` 测试。Linux CI 仅 `npm run build`（前端类型检查）。详见 [`docs/technical/TEST_AND_CI.md`](../docs/technical/TEST_AND_CI.md)。

## 压缩档位（v0.9.6+）

引擎 CLI/daemon 支持 `--compress-tier`；**Workbench 备份页尚未暴露该选项**（可通过 CLI 或后续版本接入）。仓库统计页可通过 `repo_info` 查看 `compress_ratio`（ABI v30）。

## 多 Profile（Wave O+）

桌面版在 Ribbon 提供 Profile 切换：每个 Profile 在 `%AppData%/…/profiles/<id>/` 下独立保存 `settings.json`（主题/壁纸/告警）与 `state.json`（最近仓库、上次源路径）。切换 Profile 会关闭当前仓库并加载对应设置。

## 垂直插件（Wave P / ABI v27）

备份页「高级选项」与作业编辑支持勾选 `sqlite_checkpoint`、`registry_hive`（Win）、`vhdx_scan`（Win）。作业级插件保存在 `jobs.json` 的 `plugins` 字段。
