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
| `npm run test:rust` | DLL JSON shim 集成测试 |
| `npm run sync:runtime` | 同步 `ebbackup_workbench.dll` |
| `npm run sync:fonts` | 从 eB-Tree 同步字体 |
