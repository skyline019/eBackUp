import type { UiSettings } from "@/utils/themeSettings";
import { invokeSafe, requireTauri, TauriNotAvailableError } from "@/utils/invokeSafe";
import { isTauriRuntime } from "@/utils/tauriRuntime";

export interface RepoInfoDto {
  open: boolean;
  path: string;
  abi_version: number;
  physical_bytes: number;
  live_bytes: number;
  orphan_bytes: number;
  manifest_bytes: number;
  unique_chunks: number;
  tombstoned_chunks: number;
  ampl_ratio: number;
}

export interface SnapshotDto {
  txn_id: number;
  created_at_unix: number;
  manifest_crc32: number;
  file_count: number;
}

export interface BackupStatsDto {
  files_processed: number;
  chunks_written: number;
  chunks_reused: number;
  chunks_reused_from_cfi: number;
  bytes_processed: number;
  orphan_chunks_hint: number;
}

export interface RuntimeInfoDto {
  abi_version: number;
  workbench: string;
}

export async function runtimeInfo() {
  requireTauri();
  return invokeSafe<RuntimeInfoDto>("runtime_info");
}

export async function getBackupStats() {
  requireTauri();
  return invokeSafe<BackupStatsDto>("get_backup_stats");
}

export async function lastError() {
  requireTauri();
  return invokeSafe<string>("last_error");
}

export async function initRepo(path: string, flags?: number) {
  requireTauri();
  return invokeSafe<{ ok: boolean }>("init_repo", { path, flags });
}

export async function openRepo(path: string) {
  requireTauri();
  return invokeSafe<void>("open_repo", { path });
}

export async function closeRepo() {
  requireTauri();
  return invokeSafe<void>("close_repo");
}

export async function repoInfo() {
  requireTauri();
  return invokeSafe<RepoInfoDto>("repo_info", undefined, { silent: true });
}

export async function listSnapshots() {
  requireTauri();
  return invokeSafe<SnapshotDto[]>("list_snapshots");
}

export async function pathExists(path: string) {
  requireTauri();
  return invokeSafe<boolean>("path_exists", { path });
}

export async function runBackup(sourcePath: string, incremental?: boolean, flags?: number) {
  requireTauri();
  return invokeSafe<BackupStatsDto>("run_backup", {
    sourcePath,
    incremental,
    flags,
  }, { silent: true });
}

export async function runRestore(destPath: string, txnId?: number, flags?: number) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("run_restore", {
    destPath,
    txnId,
    flags,
  });
}

export async function verifyRepo(txnId?: number, flags?: number) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("verify_repo", { txnId, flags });
}

export async function recoverRepo() {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("recover_repo");
}

export async function compactRepo(dryRun?: boolean) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("compact_repo", { dryRun });
}

export async function gcOrphans(dryRun?: boolean) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("gc_orphans", { dryRun });
}

export async function pruneSnapshots(
  retentionTiers: string,
  retainMin?: number,
  dryRun?: boolean
) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("prune_snapshots", {
    retentionTiers,
    retainMin,
    dryRun,
  });
}

export async function setPassword(password: string) {
  requireTauri();
  return invokeSafe<void>("set_password", { password });
}

export async function setAuditKey(auditKey: string) {
  requireTauri();
  return invokeSafe<void>("set_audit_key", { auditKey });
}

export async function loadFilterFile(path: string) {
  requireTauri();
  return invokeSafe<void>("load_filter_file", { path });
}

export async function pickDirectory() {
  requireTauri();
  return invokeSafe<string | null>("pick_directory");
}

export async function pickFile() {
  requireTauri();
  return invokeSafe<string | null>("pick_file");
}

export async function getUiSettings() {
  if (!isTauriRuntime()) return null;
  return invokeSafe<UiSettings>("get_ui_settings");
}

export async function setUiSettings(settings: UiSettings) {
  requireTauri();
  return invokeSafe<void>("set_ui_settings", { settings });
}

export async function uiSettingsPath() {
  if (!isTauriRuntime()) return "";
  return invokeSafe<string>("ui_settings_path");
}

export async function uiSettingsExists() {
  if (!isTauriRuntime()) return false;
  return invokeSafe<boolean>("ui_settings_exists");
}

export { TauriNotAvailableError };
