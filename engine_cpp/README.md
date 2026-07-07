# ebbackup (engine_cpp)

Content-defined backup engine with incremental HCRBO chunking, **ContentClass adaptive compression** (LZ4/zstd), AES-256-GCM encryption, audit chain, persistent chunk index, EbPack compaction, and C API.

## Build

```bash
cmake -S .. -B ../build -DEBBACKUP_BUILD_TESTS=ON
cmake --build ../build --config Release
```

On Windows (Visual Studio generator):

```powershell
cmake -S e:\recoveryProjects -B e:\recoveryProjects\build -DEBBACKUP_BUILD_TESTS=ON
cmake --build e:\recoveryProjects\build --config Release
e:\recoveryProjects\build\engine_cpp\Release\ebbackup_tests.exe
```

## Repository layout

| Path | Purpose |
|------|---------|
| `manifest` | Latest file index and chunk hash references (v2/v3 text or v4 binary) |
| `snapshots/index` | EBSNAPIDX1 snapshot catalog (v0.4+ repos) |
| `snapshots/<txn>.manifest` | Archived manifest per commit txn_id |
| `data/chunks` | Legacy content-addressed chunk store (empty for EbPack-only repos) |
| `data/packs/` | EbPack blob store (default v0.6+ repos) |
| `data/chunk.idx` | Persistent chunk index (HXID v1/v2, v0.3+ repos) |
| `superblock.bin` | Dual-slot backup phase state |
| `audit/rar.chain` | Hash chain audit log |
| `crypto.salt` | Encryption salt (if encrypted) |

## CLI examples

```bash
# Initialize v0.6 repo (EbPack + coalesced meta + v0.4 storage defaults)
eb init ./repo

# Legacy-compatible init (v0.3 storage, no EbPack)
eb init ./repo --legacy-init

# Pipeline backup (auto-enabled on EbPack repos; use --no-pipeline to disable)
eb backup ./repo ./source --pipeline
eb backup ./repo ./source --no-pipeline

# Full backup with filters
eb backup ./repo ./source --filter-file ./filters.conf --include-glob "*.txt"

# Repo maintenance
eb repo-stats ./repo
eb gc-orphans ./repo --dry-run
eb compact ./repo --dry-run
eb compact ./repo
eb compact ./repo --wait-idle 300

# Time-travel restore / verify at archived txn
eb list-snapshots ./repo
eb restore ./repo ./dest --at 42
eb verify ./repo --at 42
eb prune-snapshots ./repo
eb gc-orphans ./repo --latest-only

# Selective restore (subset + ancestor directories)
eb restore ./repo ./dest --include keep/ --exclude-glob "*.tmp"

# Full restore with optional post-write content verification
eb restore ./repo ./dest --verify-content

# Skip content Merkle verification (selective or full)
eb restore ./repo ./dest --include keep/ --skip-content-verify

# Watch with same filter semantics as backup
eb watch ./repo ./source --filter-file ./filters.conf --once

# Scheduled backup (JSON or key=value config)
eb schedule ./schedule.json --once
```

### Filter file format (key=value)

```
include_path=keep
exclude_glob=*.tmp
include_glob=*.txt
ext=.log
min_size=1024
mtime_after=1700000000
```

CLI flags override filter-file entries when both are specified (file loaded first, then flags merged).

**Glob semantics:** patterns without `/` match the file **basename** only (e.g. `*.tmp` matches `foo/bar.tmp`). Patterns containing `/` or `\` match the full **relative path** with slashes normalized to `/` (e.g. `src/*.cpp` matches `src/a.cpp` but not `lib/src/a.cpp`).

### Restore content verification

After restore, the engine can re-hash restored files on disk and compare against the manifest Merkle root:

| Scenario | Default | Override |
|----------|---------|----------|
| Selective restore (filter active) | Content verify **on** | `--skip-content-verify` |
| Full restore (no filter) | Content verify **off** | `--verify-content` |

Verification reads restored files in chunk-sized segments (streaming); it does not load entire large files into memory.

## C API (ABI v12)

- `eb_backup_load_filter_file()` applies to subsequent **backup and restore** on the same engine handle.
- `EB_BACKUP_ABI_VERSION` is **12**.
- `eb_backup_init_repo_ex()` creates **v0.6** repos by default (EbPack, coalesced meta, persistent index, manifest v4, snapshots, compress auto). Pass `EB_BACKUP_INIT_LEGACY` for v0.3 storage without EbPack.
- EbPack repos **auto-enable pipeline** on backup unless `EB_BACKUP_FLAG_NO_PIPELINE` is set.
- New in v0.4: `eb_backup_list_snapshots()`, `eb_backup_restore_at()`, `eb_backup_verify_at()`, `eb_backup_prune_snapshots()`.
- Flags: `EB_BACKUP_FLAG_COMPRESS_AUTO`, `EB_BACKUP_FLAG_COMPRESS_ZSTD`, `EB_BACKUP_FLAG_BALANCED_DURABILITY`, `EB_BACKUP_FLAG_MANIFEST_BINARY`, `EB_BACKUP_FLAG_NO_PIPELINE`.
- `eb_backup_compact()`, `eb_backup_repo_stats()` for offline compaction and storage metrics.
- `eb_backup_restore_ex()` honors `EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY` to skip post-restore content Merkle verification (default verifies when a filter is active). Full-restore opt-in verify is CLI/C++ only (`verify_restored_content`).

## Digest algorithms

Repos record their digest mode in the superblock (`kBackupFeatureDigestStandard`):

| Creation path | Default digest | v0.3 storage | Override |
|---------------|----------------|--------------|----------|
| CLI `eb init` | **Standard** (NIST/RFC) | **v0.6** (EbPack + coalesced + v0.4 storage) | `--legacy-digest`, `--legacy-init` |
| C++ `BackupEngine::InitRepo(path)` | **Legacy** (frozen golden) | none | `InitRepoEx({...})` |
| C API `eb_backup_init_repo()` | **Standard** | v0.3 defaults | `EB_BACKUP_INIT_LEGACY`, `EB_BACKUP_FLAG_LEGACY_DIGEST` |

Legacy and standard stacks produce identical output on common SHA256/PBKDF2 test vectors but remain formally separated for repo routing and future-proofing. **Do not mix digest modes within a repo** — chunk Content IDs and encryption keys depend on the mode selected at init.

## Encryption

- Content encryption uses **AES-256-GCM** with a per-chunk wire format: **12-byte nonce** + ciphertext + **16-byte tag**.
- Keys are derived via **PBKDF2-SHA256** with **100,000** iterations over the repo `crypto.salt` file, using the repo's digest algorithm.
- Set password via CLI `--password` or `eb_backup_set_password()` before backup/restore.

## Tests

Test temp files go to `engine_cpp/test_output/` (override with `EBTEST_TMPDIR`).

CI and local `ctest` include **`ebbackup_bench_check`** — a hard performance gate (L1, L3a/L3b, L5–L7, L4) against [`bench/baselines/ci_floor.json`](bench/baselines/ci_floor.json) (Stage 3.1). Aspirational Stage 3.2 targets live in [`ci_floor_stage32.json`](bench/baselines/ci_floor_stage32.json). Throughput floors use **SI MB/s (10⁶ B/s)**. See [`docs/reference/PERF_BASELINE.md`](../docs/reference/PERF_BASELINE.md) (full archive: [`docs/README.md`](../docs/README.md)).

### Layout (v0.4.2-test + v0.4.3-real)

| Layer | Files | Focus |
|-------|-------|-------|
| Success E2E | `tests/engine/e2e_success_test.cc` | incremental chains, snapshot restore-at, pipeline, encryption, CLI round-trip |
| Failure E2E | `tests/engine/e2e_failure_test.cc` | corrupt verify, wrong password, missing source, busy-repo conflicts |
| Nested trees | `tests/engine/nested_tree_test.cc` | deep/wide dirs, Unicode paths, selective restore |
| Pipeline | `tests/pipeline/pipeline_resilience_test.cc` | parity, incremental reuse, subprocess kill, balanced powerfail |
| Powerfail | `tests/failure/powerfail_extended_test.cc` | incremental kill, commit/audit abort, dual-slot, compact interrupt |
| Chaos | `tests/failure/chaos_test.cc` | 8 deterministic trials (`seed=42`): random phase, chunk flip, interleaved backup/verify, manifest truncation |
| Composite | `tests/engine/composite_workflow_test.cc` | backup → prune → gc → compact → verify → restore-at → bundle export/import |
| Real fixtures | `tests/engine/real_fixture_test.cc` | checked-in trees under `tests/fixtures/real_world/` |
| Media fixtures | `tests/engine/media_fixture_test.cc` | real JPEG/WebP/GIF/MP4 roundtrip + recursive composite maintenance |

**Fixtures** (`tests/fixtures/real_world/`):

| Subdir | Purpose |
|--------|---------|
| `mixed/` | text + JSON + binary stub (legacy) |
| `unicode/` | UTF-8 / space paths |
| `nested/` | 5-level tree + `nested_manifest.json` |
| `full/` | **v0.4.3** unified multi-type + nested + Unicode tree; `full_manifest.json` for SHA256 golden restore asserts |
| `media/` | **v0.4.4** downloaded JPEG/WebP/GIF/MP4/**ZIP**/**tar.gz**/**EXE**; `media_manifest.json` |

Refresh media binaries (optional):

```powershell
engine_cpp/tests/fixtures/scripts/fetch_media_fixtures.ps1
# China / mirror-first:
engine_cpp/tests/fixtures/scripts/fetch_media_fixtures.ps1 -UseMirror
```

Shared helpers live in `tests/helpers/` (`tree_util`, `fixture_util`, `chaos_util`, `subprocess_util`). Compile-time paths:

- `EBTEST_FIXTURE_DIR` → `tests/fixtures/real_world`
- `EBTEST_ENGINE_ROOT` → `engine_cpp/` (for `CopyEngineSourceSample`)

All cases run in the single `ebbackup_tests` ctest target (~196 tests, ~58s on Release Windows).

```bash
ctest --test-dir ../build -C Release
# or run ebbackup_tests directly
```

## Schedule config (JSON)

Scheduled backups use a **single repo** at `<repo_base>/current` (v0.4 init: persistent index, manifest v4, snapshots). The first run is full; subsequent runs are incremental. The `retain` field applies only to legacy `repo-*` sibling directories (if any remain from older deployments); snapshot retention uses `retention_policy` / `auto_prune` / `auto_gc_after_prune`.

```json
{
  "interval_seconds": 3600,
  "source": "/path/to/source",
  "repo_base": "/path/to/repos",
  "retain": 3,
  "retention_policy": "1h:24,1d:7,7d:4,30d:6",
  "auto_prune": true,
  "auto_gc_after_prune": true,
  "compress": "auto",
  "cpu_budget": 60,
  "durability": "strict",
  "pipeline": true,
  "encrypt": false,
  "filter_file": "/path/to/filters.conf",
  "include_glob": "*.txt",
  "exclude_glob": "*.tmp"
}
```

KV format (`source=...`, `repo_base=...`, `compress=auto`, `durability=balanced`, etc.) is also supported.

### Durability

Both modes use **commit-point** semantics: chunk data is fsync'd before manifest commit; a completed backup is always fully verifiable. A crash during an in-flight backup loses only the uncommitted txn (recovery → `kAborted` or prior snapshot).

| Mode | Pack spill threshold | Notes |
|------|---------------------|-------|
| `strict` (default) | 4 MiB / 32 records | Smaller in-memory pack before spilling to chunk file |
| `balanced` | 16 MiB / 64 records | Larger packs, fewer spill syscalls |

Persistent chunk index (`chunk.idx`) is written once at backup completion (not per chunk).

## Desktop Workbench (optional)

CMake target **`ebbackup_workbench`** builds a SHARED DLL with JSON shim for the Tauri GUI:

```powershell
cmake --build build --config Release --target ebbackup_workbench
```

Full documentation: [`docs/product/WORKBENCH_GUI.md`](../docs/product/WORKBENCH_GUI.md) · quick start: [`gui/README.md`](../gui/README.md).
