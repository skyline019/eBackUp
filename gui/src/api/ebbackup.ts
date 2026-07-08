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
  job_id?: string;
  retention_tag?: number;
  immutable_until_unix?: number;
}

export interface BackupJobDto {
  id: string;
  name: string;
  source_path: string;
  retention_tag?: number;
  immutability_days?: number;
  worm?: boolean;
  exclude_globs?: string[];
  exclude_paths?: string[];
  plugins?: string[];
  use_vss?: boolean;
  vss_mode?: string;
  vss_fallback_live?: boolean;
  vss_include_junction_volumes?: boolean;
  quiesce_profile?: string;
  vss_app_failure_policy?: string;
  post_backup_webhook_url?: string;
  window_start?: string;
  window_end?: string;
  deadline_grace_seconds?: number;
  durability_adaptive?: boolean;
}

export interface ExcludeFilterSuggestionDto {
  apply_as: string;
  pattern: string;
  kind: string;
  reason: string;
  example_path: string;
  hit_count: number;
}

export interface ExcludeFilterSuggestionsDto {
  ok?: boolean;
  items: ExcludeFilterSuggestionDto[];
  recommended?: {
    exclude_paths?: string[];
    exclude_globs?: string[];
  };
}

export const BUILTIN_PLUGINS = [
  { id: "sqlite_checkpoint", label: "SQLite WAL checkpoint", platforms: "all" as const },
  { id: "registry_hive", label: "Registry hive 导出", platforms: "win" as const },
  { id: "vhdx_scan", label: "VHDX 只读扫描", platforms: "win" as const },
];

export interface ManifestFileDto {
  relative_path: string;
  size: number;
  file_type: string;
  mtime_unix: number;
  chunk_count: number;
}

export interface RestorePreviewDto {
  file_count: number;
  dir_count: number;
  total_bytes: number;
  txn_id: number;
}

export interface InPlacePreviewEntryDto {
  path: string;
  action: string;
  reason?: string;
  base_action?: string;
  live_state?: string;
}

export interface InPlacePreviewSummaryDto {
  add_count: number;
  modify_count: number;
  unchanged_count: number;
  conflict_count: number;
  both_changed_count?: number;
  skip_count: number;
  bytes_to_write: number;
  orphan_count?: number;
}

export interface InPlacePreviewDto {
  ok: boolean;
  txn_id: number;
  base_txn_id?: number;
  three_way?: boolean;
  target_root: string;
  summary: InPlacePreviewSummaryDto;
  entries: InPlacePreviewEntryDto[];
}

export interface InPlaceApplyDto extends InPlacePreviewDto {
  dry_run?: boolean;
  summary: InPlacePreviewSummaryDto & {
    applied_count?: number;
    skipped_count?: number;
    overwritten_count?: number;
    failed_count?: number;
    bytes_written?: number;
  };
}

export interface RestoreSelectionPrefill {
  txnId: number;
  includePaths: string[];
}

export interface ManifestFilesDto {
  txn_id: number;
  count: number;
  total_bytes: number;
  files: ManifestFileDto[];
}

export interface PathHistoryEntryDto {
  path: string;
  txn_id: number;
  size: number;
  content_hash: string;
  file_type: string;
  mtime: number;
}

export interface PathHistoryDto {
  ok: boolean;
  history: PathHistoryEntryDto[];
  count: number;
  total?: number;
  offset?: number;
}

export interface BackupStatsDto {
  files_processed: number;
  chunks_written: number;
  chunks_reused: number;
  chunks_reused_from_cfi: number;
  bytes_processed: number;
  orphan_chunks_hint: number;
}

export interface JobQueueStatusDto {
  ok: boolean;
  pending_count: number;
  state: string;
  jobs?: Array<{ job_id: string; incremental?: boolean; flags?: number }>;
}

export interface SnapshotReachabilityDto {
  ok: boolean;
  txn_id: number;
  reachable: boolean;
  files_checked: number;
  chunks_checked: number;
  missing_chunk_count: number;
  missing_chunks?: string[];
}

export interface RpoJobSummaryDto {
  job_id: string;
  name: string;
  last_success_txn: number;
  last_success_unix: number;
  last_report_ok: boolean;
  retention_tag: number;
}

export interface RpoSummaryDto {
  ok: boolean;
  last_success_txn: number;
  last_success_unix: number;
  days_since_last_success: number;
  snapshot_count: number;
  worm_protected_count: number;
  jobs: RpoJobSummaryDto[];
}

export interface SyncStatusDto {
  latest_txn: number;
  synced_txn: number;
  pending_txn: number;
  last_export_base_txn: number;
  last_ferry_target_txn?: number;
  pending_chunk_count: number;
  failed_chunks?: number;
  remote_lag_txn: number;
  generation: number;
  last_success_unix: number;
  backoff_until_unix: number;
  last_error: string;
  maintenance_blocked: boolean;
  transport: string;
  remote_type?: string;
  local_mirror_root?: string;
  pds_domain_id?: string;
  pds_drive_id?: string;
  pds_authed?: boolean;
  sync_mode_label?: string;
}

export interface SyncMaintenanceCheckDto {
  blocked: boolean;
  reason: string;
}

export interface OrphanExplainSampleDto {
  chunk_hex: string;
  reason: "unreferenced" | "tombstoned" | "interrupted_hint";
  bytes: number;
  last_referenced_txn: number;
}

export interface OrphanExplainDto {
  ok: boolean;
  total_orphans: number;
  total_orphan_bytes: number;
  unreferenced_count: number;
  tombstoned_count: number;
  interrupted_hint_count: number;
  samples: OrphanExplainSampleDto[];
}

export interface OpsAuditEntryDto {
  sequence: number;
  generated_at_unix: number;
  body_json: string;
  signature?: string;
}

export interface OpsAuditListDto {
  ok: boolean;
  entries: OpsAuditEntryDto[];
}

export interface BackupPathIssueDto {
  path: string;
  reason: string;
}

export interface PluginReportDto {
  id: string;
  checkpointed?: number;
  exported?: number;
  mounted?: number;
  failed?: number;
  note?: string;
  [key: string]: unknown;
}

export interface BackupReportDto {
  ok: boolean;
  txn_id: number;
  backed_up: number;
  skipped: number;
  locked: number;
  permission_denied: number;
  reparse_junction?: number;
  hook_failed?: number;
  plugin_skipped?: number;
  plugin_failed?: number;
  chunks_written: number;
  chunks_reused: number;
  bytes_processed: number;
  reuse_pct: number;
  issues: BackupPathIssueDto[];
  plugins?: PluginReportDto[];
  job_id?: string;
  retention_tag?: number;
  immutable_until_unix?: number;
  durability_downgraded?: boolean;
  window_truncated?: boolean;
  window_end_unix?: number;
  vss_used?: boolean;
  vss_consistency?: string;
  vss_snapshot_set_id?: string;
  vss_volumes?: string[];
  vss_mode?: string;
  vss_cross_volume?: boolean;
  vss_shadow_storage_ok?: boolean;
  vss_shadow_storage_bytes?: number[];
  sparse_file_count?: number;
  efs_skipped_count?: number;
  recovery_key_issued?: string;
  vss_writers?: Array<{ id: string; name: string; state: string }>;
}

export interface VssBackupOptionsDto {
  mode?: "crash" | "app" | "auto";
  include_junction_volumes?: boolean;
  fallback_live?: boolean;
}

export interface RuntimeInfoDto {
  abi_version: number;
  workbench: string;
}

export interface ProfileDto {
  id: string;
  name: string;
  createdAtUnix: number;
  lastRepoPath?: string;
}

export interface ProfilesListDto {
  ok: boolean;
  activeProfileId: string;
  profiles: ProfileDto[];
}

export interface ProfileStateDto {
  recentRepos: string[];
  lastSourcePath: string;
  syncFerryOutDirs?: Record<string, string>;
  syncMirrorDirs?: Record<string, string>;
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

export async function initRepoEncrypt(path: string, password: string) {
  requireTauri();
  return invokeSafe<{ ok: boolean; recovery_key?: string }>("init_repo_encrypt", {
    path,
    password,
  });
}

export async function unwrapRecoveryKey(recoveryKey: string) {
  requireTauri();
  return invokeSafe<void>("unwrap_recovery_key", { recoveryKey });
}

export async function rotatePassword(oldPassword: string, newPassword: string) {
  requireTauri();
  return invokeSafe<void>("rotate_password", { oldPassword, newPassword });
}

export async function upgradeLegacyEnvelope(password: string) {
  requireTauri();
  return invokeSafe<{ ok: boolean; recovery_key?: string }>("upgrade_legacy_envelope", {
    password,
  });
}

export async function testWebhook(url: string) {
  requireTauri();
  return invokeSafe<void>("test_webhook", { url });
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

export async function listManifestFiles(txnId?: number) {
  requireTauri();
  return invokeSafe<ManifestFilesDto>("list_manifest_files", { txnId });
}

export async function listManifestPage(
  txnId?: number,
  prefix?: string,
  offset?: number,
  limit?: number
) {
  requireTauri();
  return invokeSafe<ManifestFilesDto>("list_manifest_page", {
    txnId,
    prefix,
    offset,
    limit,
  });
}

export async function queryPathHistory(path: string, offset?: number, limit?: number) {
  requireTauri();
  return invokeSafe<PathHistoryDto>("query_path_history", { path, offset, limit });
}

export async function diffSnapshots(txnA: number, txnB: number) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("diff_snapshots", { txnA, txnB });
}

export async function buildPathIndex(fullRebuild?: boolean) {
  requireTauri();
  return invokeSafe<{ ok: boolean }>("build_path_index", { fullRebuild });
}

export async function exportRestoreReport() {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("export_restore_report");
}

export async function getBackupReport(txnId: number) {
  requireTauri();
  return invokeSafe<BackupReportDto>("get_backup_report", { txnId });
}

export async function setBackupHooks(
  preCmd?: string,
  postCmd?: string,
  plugins?: string[]
) {
  requireTauri();
  return invokeSafe<{ ok: boolean }>("set_backup_hooks", {
    preCmd: preCmd ?? "",
    postCmd: postCmd ?? "",
    plugins: plugins ?? [],
  });
}

export async function pathExists(path: string) {
  requireTauri();
  return invokeSafe<boolean>("path_exists", { path });
}

export async function listJobs() {
  requireTauri();
  return invokeSafe<BackupJobDto[]>("list_jobs");
}

export async function upsertJob(job: BackupJobDto) {
  requireTauri();
  return invokeSafe<void>("upsert_job", { job });
}

export async function deleteJob(jobId: string) {
  requireTauri();
  return invokeSafe<void>("delete_job", { jobId });
}

export async function runJob(jobId: string, incremental?: boolean, flags?: number) {
  requireTauri();
  return invokeSafe<BackupStatsDto>("run_job", { jobId, incremental, flags }, { silent: true });
}

export async function enqueueJob(jobId: string, incremental?: boolean, flags?: number) {
  requireTauri();
  return invokeSafe<{ ok: boolean; job_id?: string }>("enqueue_job", {
    jobId,
    incremental,
    flags,
  });
}

export async function jobQueueStatus() {
  requireTauri();
  return invokeSafe<JobQueueStatusDto>("job_queue_status");
}

export async function runJobQueue(drain?: boolean, flags?: number) {
  requireTauri();
  return invokeSafe<{ ok: boolean; drain?: boolean }>("run_job_queue", { drain, flags }, {
    silent: true,
  });
}

export async function snapshotReachability(txnId: number) {
  requireTauri();
  return invokeSafe<SnapshotReachabilityDto>("snapshot_reachability", { txnId });
}

export async function rpoSummary() {
  requireTauri();
  return invokeSafe<RpoSummaryDto>("rpo_summary");
}

export async function syncStatus() {
  requireTauri();
  return invokeSafe<SyncStatusDto>("sync_status");
}

export async function syncPush(once = true) {
  requireTauri();
  return invokeSafe<{ ok: boolean; message: string }>("sync_push", { once });
}

export async function syncFerryExport(
  outDir: string,
  options?: { autoBase?: boolean; baseTxnId?: number; targetTxnId?: number }
) {
  requireTauri();
  return invokeSafe<{ ok: boolean; message: string }>("sync_ferry_export", {
    outDir,
    autoBase: options?.autoBase ?? true,
    baseTxnId: options?.baseTxnId,
    targetTxnId: options?.targetTxnId,
  });
}

export async function syncMaintenanceCheck() {
  requireTauri();
  return invokeSafe<SyncMaintenanceCheckDto>("sync_maintenance_check");
}

export async function syncInitLocal(mirrorPath: string) {
  requireTauri();
  return invokeSafe<{ ok: boolean; message: string }>("sync_init_local", { mirrorPath });
}

export async function syncInitFerry() {
  requireTauri();
  return invokeSafe<{ ok: boolean; message: string }>("sync_init_ferry");
}

export async function syncInitPds(
  domainId: string,
  credentialsPath: string,
  rootPrefix?: string
) {
  requireTauri();
  return invokeSafe<{ ok: boolean; message: string }>("sync_init_pds", {
    domainId,
    credentialsPath,
    rootPrefix,
  });
}

export async function syncPdsAuthUrl() {
  requireTauri();
  return invokeSafe<{ ok: boolean; url: string }>("sync_pds_auth_url");
}

export async function syncPdsAuth(code: string) {
  requireTauri();
  return invokeSafe<{ ok: boolean; message: string }>("sync_pds_auth", { code });
}

export async function orphanExplain(sampleLimit?: number) {
  requireTauri();
  return invokeSafe<OrphanExplainDto>("orphan_explain", { sampleLimit });
}

export async function listOpsAudit() {
  requireTauri();
  return invokeSafe<OpsAuditListDto>("list_ops_audit");
}

export async function runBackup(
  sourcePath: string,
  incremental?: boolean,
  flags?: number,
  vssOptions?: VssBackupOptionsDto
) {
  requireTauri();
  return invokeSafe<BackupStatsDto>("run_backup", {
    sourcePath,
    incremental,
    flags,
    vssMode: vssOptions?.mode,
    vssIncludeJunctionVolumes: vssOptions?.include_junction_volumes,
    vssFallbackLive: vssOptions?.fallback_live,
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

export async function runRestoreEx(
  destPath: string,
  txnId?: number,
  flags?: number,
  filterJson?: string,
  remapJson?: string
) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("run_restore_ex", {
    destPath,
    txnId,
    flags,
    filterJson,
    remapJson,
  });
}

export async function previewRestore(txnId?: number, filterJson?: string) {
  requireTauri();
  return invokeSafe<RestorePreviewDto>("preview_restore", { txnId, filterJson });
}

export async function previewInPlace(
  targetRoot: string,
  txnId?: number,
  filterJson?: string,
  inPlaceOptionsJson?: string
) {
  requireTauri();
  return invokeSafe<InPlacePreviewDto>("preview_in_place", {
    targetRoot,
    txnId,
    filterJson,
    inPlaceOptionsJson,
  });
}

export async function applyInPlace(
  targetRoot: string,
  txnId?: number,
  conflictPolicy?: string,
  filterJson?: string,
  orphanPolicy?: string,
  inPlaceOptionsJson?: string
) {
  requireTauri();
  return invokeSafe<InPlaceApplyDto>("apply_in_place", {
    targetRoot,
    txnId,
    conflictPolicy,
    orphanPolicy,
    filterJson,
    inPlaceOptionsJson,
  });
}

export async function exportDelta(
  baseTxnId: number,
  bundlePath: string,
  targetTxnId?: number,
  password?: string
) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("export_delta", {
    baseTxnId,
    bundlePath,
    targetTxnId,
    password,
  });
}

export async function importDelta(basePath: string, deltaPath: string, outRepoPath: string) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("import_delta", {
    basePath,
    deltaPath,
    outRepoPath,
  });
}

export async function runMaintenanceWizard(optionsJson?: string) {
  requireTauri();
  return invokeSafe<Record<string, unknown>>("run_maintenance_wizard", {
    optionsJson,
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

export async function setFilterJson(filter: {
  exclude_paths?: string[];
  exclude_globs?: string[];
}) {
  requireTauri();
  return invokeSafe<void>("set_filter_json", { filterJson: JSON.stringify(filter) });
}

export async function suggestExcludeFilters(
  sourcePath: string,
  options?: {
    maxDepth?: number;
    includeIdeDirs?: boolean;
    existing?: { exclude_paths?: string[]; exclude_globs?: string[] };
  }
) {
  requireTauri();
  return invokeSafe<ExcludeFilterSuggestionsDto>("suggest_exclude_filters", {
    sourcePath,
    maxDepth: options?.maxDepth,
    includeIdeDirs: options?.includeIdeDirs,
    existingJson: options?.existing ? JSON.stringify(options.existing) : undefined,
  });
}

export async function pickDirectory() {
  requireTauri();
  return invokeSafe<string | null>("pick_directory");
}

export async function pickFile() {
  requireTauri();
  return invokeSafe<string | null>("pick_file");
}

export async function getUiSettings(profileId?: string) {
  if (!isTauriRuntime()) return null;
  return invokeSafe<UiSettings>("get_ui_settings", { profileId });
}

export async function setUiSettings(settings: UiSettings, profileId?: string) {
  requireTauri();
  return invokeSafe<void>("set_ui_settings", { settings, profileId });
}

export async function uiSettingsPath(profileId?: string) {
  if (!isTauriRuntime()) return "";
  return invokeSafe<string>("ui_settings_path", { profileId });
}

export async function listProfiles() {
  requireTauri();
  return invokeSafe<ProfilesListDto>("list_profiles");
}

export async function getActiveProfile() {
  requireTauri();
  return invokeSafe<string>("get_active_profile");
}

export async function createProfile(name: string) {
  requireTauri();
  return invokeSafe<{ ok: boolean; profile: ProfileDto }>("create_profile", { name });
}

export async function renameProfile(profileId: string, name: string) {
  requireTauri();
  return invokeSafe<void>("rename_profile", { profileId, name });
}

export async function deleteProfile(profileId: string) {
  requireTauri();
  return invokeSafe<void>("delete_profile", { profileId });
}

export async function switchProfile(profileId: string) {
  requireTauri();
  return invokeSafe<{
    ok: boolean;
    profileId: string;
    state?: ProfileStateDto;
    settings?: UiSettings;
  }>("switch_profile", { profileId });
}

export async function getProfileState(profileId?: string) {
  requireTauri();
  return invokeSafe<ProfileStateDto>("get_profile_state", { profileId });
}

export async function setProfileState(profileId: string | undefined, state: ProfileStateDto) {
  requireTauri();
  return invokeSafe<void>("set_profile_state", { profileId, state });
}

export async function uiSettingsExists(profileId?: string) {
  if (!isTauriRuntime()) return false;
  return invokeSafe<boolean>("ui_settings_exists", { profileId });
}

export { TauriNotAvailableError };
