# recoveryProjects / ebbackup

Content-defined backup engine with incremental HCRBO chunking, selective restore,
LZ4 compression, AES-256-GCM encryption, audit chain, and a stable C API.

The primary implementation lives in [`engine_cpp/`](engine_cpp/).

## Repository layout

| Path | Purpose |
|------|---------|
| [`engine_cpp/`](engine_cpp/) | Backup kernel, CLI (`eb`), tests, and product documentation |
| [`gtest_capi/`](gtest_capi/) | Bundled GoogleTest for CI and local builds |
| [`.github/workflows/ebbackup.yml`](.github/workflows/ebbackup.yml) | Windows + Linux CI (build + ctest) |

## Build and test

From the repository root:

```bash
cmake -S . -B build -DEBBACKUP_BUILD_TESTS=ON
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

- Engine, CLI, filters, encryption: [`engine_cpp/README.md`](engine_cpp/README.md)
- Performance baseline (L1–L3) and CI bench gate: [`engine_cpp/docs/PERF_BASELINE.md`](engine_cpp/docs/PERF_BASELINE.md)
- Release / ABI notes: [`CHANGELOG.md`](CHANGELOG.md)

## Quick start

```bash
eb init ./repo
eb backup ./repo ./source
eb verify ./repo
eb restore ./repo ./dest
```

See [`engine_cpp/README.md`](engine_cpp/README.md) for selective restore, `--verify-content`, and schedule/watch commands.
