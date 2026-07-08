# ebbackup Workbench — 桌面 GUI 归档

本文档为 **ebbackup Workbench**（`gui/`）的正式产品与技术归档。Workbench 是可选桌面入口，通过 `ebbackup_workbench.dll` 调用 C API，无独立网络服务。

**快速入口（源码树）**：[`gui/README.md`](../../gui/README.md)  
**内核与 CLI**：[`engine_cpp/README.md`](../../engine_cpp/README.md)

---

## 1. 定位与边界

| 项目 | 说明 |
|------|------|
| 技术栈 | Tauri 2 + Vue 3 + TypeScript + Rust（FFI 宿主） |
| 视觉 lineage | eB-Tree Workbench（Activity Bar、Ribbon、OutputPanel、暗色玻璃主题） |
| 调用路径 | Vue → Tauri commands → `ebbackup_workbench.dll` JSON shim → `eb_backup_*` C API |
| 提供 | 仓库 init/open、备份/恢复/验证/维护、快照列表与 prune、设置持久化 |
| 不提供 | 云同步、远程仓库、多租户；与内核边界一致 |

---

## 2. 仓库布局

```
gui/
  src/                    Vue 3 应用（Activities + Shell）
  src-tauri/              Tauri 2 宿主、commands、FFI、集成测试
  src-tauri/bin/          运行时 DLL（sync 脚本写入，gitignore）
  public/fonts/           JetBrains Mono NL Nerd + ProggyClean（与 eB-Tree 一致）
  scripts/
    build_desktop.ps1     Release：CMake DLL + sync + tauri build
    sync_runtime_binaries.ps1
    sync_fonts.ps1
engine_cpp/
  workbench/              JSON shim（ebbackup_workbench_shim.cc）
  include/ebbackup/ebbackup_workbench.h
```

---

## 3. 构建与运行

### 3.1 依赖

| 工具 | 要求 |
|------|------|
| Windows | 10/11 x64 |
| Visual Studio 2022 | C++ 桌面开发 |
| CMake | 3.20+ |
| Node.js | 18+ |
| Rust | stable（Tauri 2） |

### 3.2 编译 native DLL

```powershell
cmake -S E:\recoveryProjects -B E:\recoveryProjects\build -DEBBACKUP_BUILD_TESTS=ON
cmake --build E:\recoveryProjects\build --config Release --target ebbackup_workbench
```

产物：`build\engine_cpp\Release\ebbackup_workbench.dll`

### 3.3 开发模式

```powershell
cd E:\recoveryProjects\gui
npm install
npm run tauri:dev
```

- **`npm run tauri:dev`**：桌面窗口，完整 API。
- **`npm run dev`**：仅 Vite 浏览器预览，**无法**调用备份 API。

Vite 开发端口：**1421**。

### 3.4 Release 安装包

```powershell
cd E:\recoveryProjects\gui
npm run build:desktop
```

| 产物 | 路径 |
|------|------|
| NSIS 安装包 | `src-tauri\target\release\bundle\nsis\ebbackup Workbench_0.1.0_x64-setup.exe` |
| 便携 exe | `src-tauri\target\release\ebbackup-workbench.exe` |

---

## 4. 功能面（六个 Activity）

| 快捷键 | Activity | 功能 |
|--------|----------|------|
| Ctrl+1 | 仓库 | 新建 / 打开 / 关闭 repo；最近仓库 |
| Ctrl+2 | 备份 | 全量/增量、LZ4、Pipeline、加密、过滤器 |
| Ctrl+3 | 快照 | 列表、GFS prune |
| Ctrl+4 | 恢复 | 还原到目标目录、time-travel（txn_id） |
| Ctrl+5 | 验证 | verify、recover 中断事务 |
| Ctrl+6 | 维护 | repo-stats、compact、gc-orphans |

全局：**F1** 快捷键帮助 · **Ctrl+J** 折叠/展开输出面板 · **Ctrl+,** 设置抽屉。

---

## 5. UI 设置与透明度

设置持久化：`%APPDATA%\ebbackup-workbench\ui_settings.json`（Tauri 桌面模式）。

| 设置项 | 说明 |
|--------|------|
| `workspaceCardOpacity` | 主工作区卡片（`panel-card`）背景不透明度，**5%–100%** |
| `logPanelOpacity` | 底部输出面板不透明度，**5%–100%** |
| 壁纸模式 | 表格/侧栏仍有可读性下限；卡片与输出面板不受壁纸 floor 限制 |

调节入口：

- 主工作区右上角 **「卡片」** 滑块（内联 `OpacityRegulator`）
- 输出面板工具栏 **「输出」** 滑块
- 设置抽屉 → **布局** 页完整滑块

实现要点：CSS 变量 `--workspace-card-surface` / `--log-panel-surface` 由 JS 预计算 `rgba()` alpha，避免 WebView2 嵌套 `color-mix()` 失效；工作区父层透明以透出壁纸/渐变。

---

## 6. JSON Shim API（DLL 导出）

| C 导出 | Tauri command | 说明 |
|--------|---------------|------|
| `ebbackup_workbench_init_repo_json` | `init_repo` | 初始化仓库 |
| `ebbackup_workbench_repo_info_json` | `repo_info` | 打开后统计（`eb_backup_repo_stats`） |
| `ebbackup_workbench_list_snapshots_json` | `list_snapshots` | 快照列表（含 `job_id` / `immutable_until_unix` meta） |
| `ebbackup_workbench_run_backup_json` | `run_backup` | 快速备份（ad-hoc 源目录） |
| `eb_backup_list_jobs_json` | `list_jobs` | 读取 `jobs.json` |
| `eb_backup_upsert_job_json` | `upsert_job` | 新建/更新作业 |
| `eb_backup_delete_job` | `delete_job` | 删除作业 |
| `eb_backup_run_job` | `run_job` | 按作业 ID 运行备份 |
| `ebbackup_workbench_run_restore_json` | `run_restore` | 恢复 |
| `ebbackup_workbench_verify_json` | `verify_repo` | 验证 |
| `ebbackup_workbench_recover_json` | `recover_repo` | 恢复中断事务 |
| `ebbackup_workbench_compact_json` | `compact_repo` | 压缩 |
| `ebbackup_workbench_gc_orphans_json` | `gc_orphans` | 孤儿块 GC |
| `ebbackup_workbench_prune_snapshots_json` | `prune_snapshots` | GFS prune |
| `ebbackup_workbench_runtime_info_json` | `runtime_info` | ABI / workbench 标识 |
| `ebbackup_workbench_last_error_json` | `last_error` | 末次引擎错误 |
| `ebbackup_workbench_get_stats_json` | `get_backup_stats` | 末次备份统计 |

会话：`open_repo` / `close_repo` 由 Rust `SESSION` 管理 `EbBackupEngine*` 生命周期。

### 6.1 备份页作业管理（Wave G）

- **快速备份**：原有 ad-hoc 源目录 + filter/hooks
- **已保存作业**：表格 CRUD；运行作业调用 `run_job`（自动应用作业内 `exclude_globs` 与 retention/WORM 策略）
- **备份报告 / 快照列表**：展示 `job_id`、`retention_tag`、`immutable_until_unix`
- **Prune**：非 dry-run 时可填 audit key（WORM 仓库）

---

## 7. 测试

### 7.1 Rust 集成测试（DLL roundtrip）

```powershell
cmake --build E:\recoveryProjects\build --config Release --target ebbackup_workbench
cd E:\recoveryProjects\gui
npm run sync:runtime
npm run test:rust
```

测试文件：`gui/src-tauri/tests/workbench_integration.rs`  
覆盖：runtime_info、init → backup → snapshots → verify → repo_info、double-init 稳定性。

### 7.2 引擎回归（空仓库统计）

`ComputeRepoStats` 在 **无 manifest 且 txn_id=0**（刚 init、尚未首次备份）时返回零值统计，不再报错。  
gtest：`RepoStatsTest.EmptyInitializedRepoWithoutManifest`。

---

## 8. 常见问题

| 现象 | 原因 | 处理 |
|------|------|------|
| `source path not found` | 源目录不存在 | 备份页点「浏览」选择有效文件夹 |
| 首次备份勾选增量失败 | 尚无历史 manifest | 空仓库自动全量；或手动取消「增量备份」 |
| `repo_info: cannot open manifest`（旧版） | init 后尚无 manifest | 已修复：空仓库返回零统计 |
| 透明度滑块无效 | 父层不透明 / color-mix 嵌套 | 使用 `--*-surface` 预计算 rgba；更新至当前 gui |
| 浏览器预览无法备份 | 非 Tauri 运行时 | 使用 `npm run tauri:dev` 或安装包 |

---

## 9. 字体同步

UI 字体与 eB-Tree Workbench 一致，位于 `gui/public/fonts/`。

```powershell
cd gui
npm run sync:fonts
```

默认源：`E:\DBProject\gui\public\fonts`（可在 `scripts/sync_fonts.ps1` 修改）。

---

## 10. 版本

| 项 | 值 |
|----|-----|
| Workbench 包版本 | 0.1.0（`gui/package.json` / `tauri.conf.json`） |
| 依赖 C API ABI | v12（`eb_backup_abi_version()`） |
| 归档日期 | 2026-07-07 |

---

*维护：Workbench 功能或 shim API 变更时同步更新本文与 [`CHANGELOG.md`](../../CHANGELOG.md)。*
