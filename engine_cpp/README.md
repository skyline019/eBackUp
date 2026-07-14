# ebbackup (engine_cpp)

Content-defined backup engine with incremental HCRBO chunking, **ContentClass adaptive compression** (LZ4/zstd), **CompressTier** (fast/balanced/max) with optional **Zstd LDM** and **repo dictionary**, AES-256-GCM encryption, audit chain, persistent chunk index, EbPack compaction, and C API.

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

Standalone kernel build (from `engine_cpp/` only):

```powershell
cmake -S e:\recoveryProjects\engine_cpp -B e:\recoveryProjects\build\engine_cpp_standalone -DEBBACKUP_BUILD_TESTS=ON
cmake --build e:\recoveryProjects\build\engine_cpp_standalone --config Release --target ebbackup_tests
```

## G-TCDC v6 2F-Gear (opt-in CDC)

G-TCDC is a separate content-defined chunking family from FastCDC. Enable at runtime:

```powershell
$env:EBBACKUP_CDC_ALGO = "gtcdc"
eb init ./repo
eb backup ./repo ./source
```

New repos get superblock flags `0x1000|0x10000` (2F-Gear v6): dual rolling fingerprints on non-overlapping bit domains (`h_gear` + `h_norm`). **FastCDC repos cannot be switched in place**; v5 AN-Gear (`0x8000`), v4 Native (`0x4000`), and v3 Gear repos (`0x2000`) stay frozen on their kernel.

| Requirement | Detail |
|-------------|--------|
| Runtime | `EBBACKUP_CDC_ALGO=gtcdc` required for backup/verify on G-TCDC repos |
| Hardware | AVX2 required for release G-TCDC scan paths |
| Spec | [`docs/technical/GT_CDC_SPEC.md`](../docs/technical/GT_CDC_SPEC.md) |
| Proof script | `engine_cpp/bench/scripts/run_gtcdc_proof.ps1` |

G-TCDC unit tests:

```powershell
ebbackup_tests.exe --gtest_filter="GtCdc2F*:GtCdcAn*:GtCdc*:EbHcrboGt*:GtCdcPipeline*"
```

Bench gates (v6 repos): primary `gtcdc_vs_stream_ratio`; secondary `scan_ns_per_probe_ratio` (normalized per scan probe; raw `scan_ns_ratio` is diagnostic only).

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
# Shows ampl_ratio + compress_ratio (ABI v30) when chunks exist

# Tiered compression (v0.9.6+)
eb backup ./repo ./source --compress-tier balanced
eb backup ./repo ./source --compress-tier max --compress-level 12
eb backup ./repo ./source --compress-tier fast --no-zstd-dict

# Windows VSS snapshot read (requires elevation; crash-consistent)
eb backup ./repo ./source --vss
eb backup ./repo ./source --vss --vss-fallback-live

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

# Selective restore with layout remap (ABI v13)
eb restore ./repo ./dest --include keep/ --strip-prefix keep
eb restore ./repo ./dest --include keep/ --flatten --on-conflict suffix
eb restore ./repo ./dest --preview --include keep/

# In-place restore preview (ABI v19): compare live source tree vs snapshot
eb restore ./repo ./source --in-place --preview

# In-place restore apply (ABI v20+): write snapshot content into live tree
eb restore ./repo ./source --in-place
eb restore ./repo ./source --in-place --in-place-conflict fail
eb restore ./repo ./source --in-place --in-place-conflict overwrite
eb restore ./repo ./source --in-place --in-place-orphans delete
eb restore ./repo ./source --in-place --base-at 1 --preview
eb restore ./repo ./source --in-place --dry-run

# Symlink target remap (restore to new directory)
eb restore ./repo ./dest --symlink-remap-from C:/old --symlink-remap-to D:/new

# Skip content Merkle verification (selective or full)
eb restore ./repo ./dest --include keep/ --skip-content-verify

# Provable backup: path history index, snapshot diff, restore acceptance report
eb path-index ./repo --rebuild
eb browse-page ./repo --prefix src/ --offset 0 --limit 100
eb diff ./repo --at 1 --at 2
eb restore ./repo ./dest --acceptance-report ./acceptance.json

# Watch with same filter semantics as backup
eb watch ./repo ./source --filter-file ./filters.conf --once

# Scheduled backup (JSON or key=value config)
eb schedule ./schedule.json --once

# Multi-job backup (ABI v18+) and job reuse reports (ABI v19)
eb job list ./repo
eb job add ./repo --json '{"id":"docs","name":"Docs","source_path":"./source","retention_tag":3,"immutability_days":7,"worm":false,"exclude_globs":["*.tmp"]}'
eb job remove ./repo docs
eb job reports ./repo docs --limit 20
eb backup ./repo --job docs
eb prune-snapshots ./repo --audit-key <authorized-key>

# Persistent job queue (ABI v22+)
eb queue list ./repo
eb queue add ./repo docs
eb queue add ./repo docs --incremental
eb queue run ./repo --once
eb queue run ./repo --drain
eb queue drain ./repo

# Snapshot chain reachability + RPO summary (ABI v24+)
eb verify-chain ./repo [--at TXN] [--json]
eb rpo-summary ./repo [--json]

# Orphan explain + maintenance ops audit (ABI v25+)
eb orphan-explain ./repo [--json] [--limit N]
eb audit-ops list ./repo [--json]

# Vertical backup plugins (ABI v27+)
eb plugin list
eb backup ./repo ./source --plugins sqlite_checkpoint,registry_hive

# Smart exclude suggestions (ABI v28+)
eb suggest-excludes ./source [--json] [--max-depth 4] [--filter-file ./rules.filter] [--include-ide]

# Job with backup window (ABI v29+)
eb job add ./repo --json '{"id":"nightly","name":"Nightly","source_path":"./source","window_start":"02:00","window_end":"06:00","durability_adaptive":true}'

# Schedule queue drain (JSON: mode=queue_drain, repo_path=..., job_ids=job_a,job_b, plugins=sqlite_checkpoint)
eb schedule ./queue_drain.json --once

# Outer cloud sync (sync_cpp/ — not in kernel ABI)
eb-sync init --repo ./repo --endpoint http://127.0.0.1:9000 --bucket ebbackup --prefix myrepo/ --path-style --access-key KEY --secret-key SECRET
eb-sync status --repo ./repo
eb-sync push --repo ./repo --once
eb-sync ferry export --repo ./repo --out-dir D:\ferry --auto-base

# Daemon sync drain (spawns eb-sync; no network in engine_cpp)
eb schedule ./config/sync_drain.example.json --once

# Windows Service (run as elevated for install/uninstall)
eb service install --config ./config/queue_drain.example.json --name EbbackupDaemon
eb service run --config ./config/queue_drain.example.json
eb service status --name EbbackupDaemon
eb service uninstall --name EbbackupDaemon

# Linux: copy deploy/ebbackup.service + ebbackup.timer to /etc/systemd/system/
# then: systemctl enable --now ebbackup.timer

# Portable bundle export/import
eb export ./repo ./backup.ebb
eb import ./backup.ebb ./restored_repo

# Delta bundle (base snapshot + incremental payload)
eb export ./repo ./delta.ebb --delta --base-at 1
eb import ./base.ebb ./delta.ebb ./restored_repo
eb import --delta ./delta.ebb ./existing_repo
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

## C API (ABI v31)

- `eb_backup_set_filter_json()` / `eb_backup_set_restore_remap()` — handle 级 filter 与路径重整。
- `eb_backup_preview_restore_at()` — 预览选择性恢复子集规模。
- `eb_backup_preview_in_place_json()` — 就地恢复 preview（逐路径 diff，含 orphan）。
- `eb_backup_apply_in_place_json()` — 就地恢复 apply（`conflict_policy`: skip/fail/overwrite；`orphan_policy`: skip/delete）。
- `eb_backup_export_delta_json()` / `eb_backup_import_delta_json()` / `eb_backup_apply_delta_json()` — EBB v2 增量包。
- `eb_backup_list_job_reports_json()` — Job 历史复用报告 sidecar。
- `eb_backup_run_maintenance_wizard()` — Prune → GC → Compact 编排。
- `eb_backup_gc_orphans_ex()` — 返回 orphan/tombstone 计数。
- **v14**: `eb_backup_build_path_index()`；`eb_backup_query_path_history_json()`；`eb_backup_list_manifest_files_page_json()`。
- **v15**: `eb_backup_diff_snapshots_json()`；`eb_backup_export_restore_report_json()`。
- **v16**: manifest v5 Win 元数据（`security_descriptor_b64`、`inode_id`、`reparse_tag`、`reparse_target`、ADS `stream_name`）；restore remap `acl_policy`（含 `best_effort`）与 `reparse_policy`（`skip`/`recreate`）；硬链 inode dedup + `CreateHardLinkW` 恢复。
- **v17**: `eb_backup_get_backup_report_json()`；`BackupOptions::pre_backup_cmd` / `post_backup_cmd`；`eb_backup_set_backup_hooks()`。
- **v18**: `eb_backup_list_jobs_json()` / `eb_backup_upsert_job_json()` / `eb_backup_delete_job()` / `eb_backup_run_job()`；`catalog/snapshot_meta.jsonl`；WORM repo prune 需 audit 授权。
- **v19**: `eb_backup_preview_in_place_json()`；`catalog/jobs/<job_id>.jsonl`；`eb_backup_list_job_reports_json()`。
- **v20**: `eb_backup_apply_in_place_json()`；Hybrid CDC 默认路由（`EBBACKUP_CDC_HYBRID=0` opt-out；bench ratio 1.00）。
- **v21**: delta bundle JSON C API；in-place `orphan_policy`；remap JSON `symlink_remap_from/to`。
- **v28**: `eb_backup_suggest_exclude_filters_json()`；job `exclude_paths[]`；CLI `eb suggest-excludes`。
- **v29**: job `window_start/end` + `durability_adaptive`；备份报告 `durability_downgraded` / `window_truncated`。
- **v30**: `EbRepoStats` 压缩率与字典统计；`eb repo-stats` / `eb_backup_repo_stats()` 扩展字段（见 [`docs/technical/COMPRESSION.md`](../docs/technical/COMPRESSION.md)）。
- **v31**: `EB_BACKUP_FLAG_VSS`；备份报告 `vss_*` 字段；CLI `--vss` / `--vss-fallback-live`（Windows，需 elevation）。
- `EB_BACKUP_ABI_VERSION` is **31**.
- `eb_backup_init_repo_ex()` creates **v0.6** repos by default (EbPack, coalesced meta, persistent index, manifest v4, snapshots, compress auto). Pass `EB_BACKUP_INIT_LEGACY` for v0.3 storage without EbPack.
- EbPack repos **auto-enable pipeline** on backup unless `EB_BACKUP_FLAG_NO_PIPELINE` is set.
- New in v0.4: `eb_backup_list_snapshots()`, `eb_backup_restore_at()`, `eb_backup_verify_at()`, `eb_backup_prune_snapshots()`.
- Flags: `EB_BACKUP_FLAG_COMPRESS_AUTO`, `EB_BACKUP_FLAG_COMPRESS_ZSTD`, `EB_BACKUP_FLAG_BALANCED_DURABILITY`, `EB_BACKUP_FLAG_MANIFEST_BINARY`, `EB_BACKUP_FLAG_NO_PIPELINE`, `EB_BACKUP_FLAG_VSS`.
- `eb_backup_compact()`, `eb_backup_repo_stats()` for offline compaction and storage metrics (v30: includes `compress_ratio`, dictionary info).
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
| Fuzz / 解码 | `tests/fuzz/decode_corruption_test.cc` | ChunkStore / EbPack 变异解码 |
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

All cases run in the single `ebbackup_tests` ctest target (**393** tests, ~60s+ on Release Windows). See also `ebsync_tests` (6) when `EBBACKUP_BUILD_SYNC=ON`.

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
  "compress_tier": "balanced",
  "zstd_dict": true,
  "cpu_budget": 60,
  "durability": "strict",
  "pipeline": true,
  "encrypt": false,
  "filter_file": "/path/to/filters.conf",
  "include_glob": "*.txt",
  "exclude_glob": "*.tmp",
  "plugins": "sqlite_checkpoint,registry_hive"
}
```

KV format (`source=...`, `repo_base=...`, `compress=auto`, `compress_tier=balanced`, `zstd_dict=true`, `durability=balanced`, `plugins=sqlite_checkpoint,registry_hive`, etc.) is also supported.

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
