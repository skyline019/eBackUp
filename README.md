# recoveryProjects / ebbackup

Content-defined backup engine with incremental HCRBO chunking, selective restore,
time-travel snapshots, tiered LZ4/zstd compression (CompressTier + repo dictionary), AES-256-GCM encryption, audit chain, and a stable C API.

The primary implementation lives in [`engine_cpp/`](engine_cpp/).

## Repository layout

| Path | Purpose |
|------|---------|
| [`engine_cpp/`](engine_cpp/) | Backup kernel, CLI (`eb`), tests, and product manual |
| [`sync_cpp/`](sync_cpp/) | Outer cloud sync (`eb-sync`) — ferry, local mirror, S3/PDS |
| [`gui/`](gui/) | Desktop Workbench (Tauri 2 + Vue 3) — optional GUI entry |
| [`docs/`](docs/) | Technical archive: version history, architecture, perf baseline (v0.1–v0.9+) |
| [`gtest_capi/`](gtest_capi/) | Bundled GoogleTest for CI and local builds |
| [`.github/workflows/ebbackup.yml`](.github/workflows/ebbackup.yml) | Windows + Linux CI (engine + sync + GUI + bench) |

## Build and test

From the repository root:

```bash
cmake -S . -B build -DEBBACKUP_BUILD_TESTS=ON -DEBBACKUP_BUILD_SYNC=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows (Visual Studio generator):

```powershell
cmake -S e:\recoveryProjects -B e:\recoveryProjects\build -DEBBACKUP_BUILD_TESTS=ON
cmake --build e:\recoveryProjects\build --config Release
e:\recoveryProjects\build\engine_cpp\Release\ebbackup_tests.exe
```

## Documentation

- **Technical archive (v0.1–v0.9+)**: [`docs/README.md`](docs/README.md)
- Compression tiers & dictionary: [`docs/technical/COMPRESSION.md`](docs/technical/COMPRESSION.md)
- Outer sync (`eb-sync`): [`sync_cpp/README.md`](sync_cpp/README.md)
- Engine, CLI, filters, encryption: [`engine_cpp/README.md`](engine_cpp/README.md)
- Desktop Workbench（完整归档）: [`docs/product/WORKBENCH_GUI.md`](docs/product/WORKBENCH_GUI.md) · 快速入口: [`gui/README.md`](gui/README.md)
- Performance baseline (L1–L7) and CI bench gate: [`docs/reference/PERF_BASELINE.md`](docs/reference/PERF_BASELINE.md)
- Release / ABI notes: [`CHANGELOG.md`](CHANGELOG.md)

## Quick start

```bash
eb init ./repo
eb backup ./repo ./source
eb list-snapshots ./repo
eb verify ./repo
eb restore ./repo ./dest
eb restore ./repo ./dest --at 42
```

See [`engine_cpp/README.md`](engine_cpp/README.md) for selective restore, time-travel snapshots, `--verify-content`, and schedule/watch commands.

## Desktop GUI

```powershell
cmake --build e:\recoveryProjects\build --config Release --target ebbackup_workbench
cd e:\recoveryProjects\gui
npm install
npm run tauri:dev
```
