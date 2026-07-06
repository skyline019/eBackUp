# ebbackup (engine_cpp)

Content-defined backup engine with incremental HCRBO chunking, LZ4 compression,
AES-256-GCM encryption, audit chain, and C API.

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
| `manifest` | File index and chunk hash references |
| `data/chunks` | Content-addressed chunk store |
| `superblock.bin` | Dual-slot backup phase state |
| `audit/rar.chain` | Hash chain audit log |
| `crypto.salt` | Encryption salt (if encrypted) |

## CLI examples

```bash
# Full backup with filters
eb backup ./repo ./source --filter-file ./filters.conf --include-glob "*.txt"

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

## C API (ABI v8)

- `eb_backup_load_filter_file()` applies to subsequent **backup and restore** on the same engine handle.
- `EB_BACKUP_ABI_VERSION` is `8`.
- `eb_backup_restore_ex()` honors `EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY` to skip post-restore content Merkle verification (default verifies when a filter is active). Full-restore opt-in verify is CLI/C++ only (`verify_restored_content`).

## Encryption

- Content encryption uses **AES-256-GCM** with a per-chunk wire format: **12-byte nonce** + ciphertext + **16-byte tag**.
- Keys are derived via **PBKDF2-SHA256** with **100,000** iterations over the repo `crypto.salt` file.
- Set password via CLI `--password` or `eb_backup_set_password()` before backup/restore.

### Known limitation: digest interop

The built-in SHA256/HMAC/PBKDF2 stack uses project-specific golden vectors (not NIST-standard SHA256 output). Encrypted repos and chunk Content IDs remain **internally consistent**, but digests and derived keys are **not interoperable** with OpenSSL or other standard libraries. Do not change SHA256 without a migration plan.

## Tests

Test temp files go to `engine_cpp/test_output/` (override with `EBTEST_TMPDIR`).

```bash
ctest --test-dir ../build -C Release
# or run ebbackup_tests directly
```

## Schedule config (JSON)

```json
{
  "interval_seconds": 3600,
  "source": "/path/to/source",
  "repo_base": "/path/to/repos",
  "retain": 3,
  "lz4": true,
  "pipeline": true,
  "encrypt": false,
  "filter_file": "/path/to/filters.conf",
  "include_glob": "*.txt",
  "exclude_glob": "*.tmp"
}
```

KV format (`source=...`, `repo_base=...`, etc.) is also supported.
