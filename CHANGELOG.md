# Changelog

## M9 (wrap-up) — unreleased

- Streaming `VerifyRestoredFileChunks` (chunk-sized reads, no full-file buffer)
- Full restore opt-in content verification: CLI `--verify-content`, `RestoreOptions.verify_restored_content`
- CLI `--skip-content-verify` (skip wins if both flags are passed)
- `EbHcrboStats.cfi_rolling_skip_hits` exposed in bench/CLI output
- Root README, CLI integration tests

## M8 — ABI v8

- `EB_BACKUP_ABI_VERSION` **8**
- `EB_RESTORE_FLAG_SKIP_CONTENT_VERIFY` on `eb_backup_restore_ex()`
- Post-restore **content Merkle** for selective restore (disk hash vs manifest)
- Glob full-path semantics (`src/*.cpp` vs `*.tmp` basename)
- CFI offset index for incremental chunking
- NIST AES-256-GCM test vectors (portable path)

C integrators should use `#if eb_backup_abi_version() >= 8` before passing restore flags.

## M7 — ABI v7

- Selective restore via `BackupFilterOptions` / manifest filter
- `eb_backup_load_filter_file()` applies to backup **and** restore
- CFI rolling checksum fast-reject gate
- Unified filter loader for CLI, watch, schedule

## M5–M6

- HCRBO incremental backup, pipeline mode, encryption, audit chain, daemon/schedule/watch
