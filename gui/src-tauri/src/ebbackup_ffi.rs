//! Dynamic loader for `ebbackup_workbench.dll`.

use libloading::Library;
use once_cell::sync::OnceCell;
use parking_lot::Mutex;
use std::ffi::CString;
use std::os::raw::{c_char, c_int, c_uint, c_void};
use std::path::PathBuf;

pub type EbBackupEngine = c_void;

type OpenExFn = unsafe extern "C" fn(*const c_char, *mut c_int) -> *mut EbBackupEngine;
type CloseFn = unsafe extern "C" fn(*mut EbBackupEngine);
type SetPasswordFn = unsafe extern "C" fn(*mut EbBackupEngine, *const c_char);
type SetAuditKeyFn = unsafe extern "C" fn(*mut EbBackupEngine, *const c_char);
type LoadFilterFn = unsafe extern "C" fn(*mut EbBackupEngine, *const c_char) -> c_int;
type JsonEngFn = unsafe extern "C" fn(*mut EbBackupEngine, *mut c_char, usize) -> c_int;
type JsonInitFn =
    unsafe extern "C" fn(*const c_char, c_uint, *mut c_char, usize) -> c_int;
type JsonBackupFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    *const c_char,
    c_int,
    c_uint,
    *mut c_char,
    usize,
) -> c_int;
type JsonRestoreFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    *const c_char,
    u64,
    c_uint,
    *mut c_char,
    usize,
) -> c_int;
type JsonRestoreExFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    *const c_char,
    u64,
    c_uint,
    *const c_char,
    *const c_char,
    *mut c_char,
    usize,
) -> c_int;
type JsonPreviewRestoreFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    u64,
    *const c_char,
    *mut c_char,
    usize,
) -> c_int;
type JsonPreviewInPlaceFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    u64,
    *const c_char,
    *const c_char,
    *const c_char,
    *mut c_char,
    usize,
) -> c_int;
type JsonApplyInPlaceFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    u64,
    *const c_char,
    *const c_char,
    *const c_char,
    *const c_char,
    *const c_char,
    *mut c_char,
    usize,
) -> c_int;
type JsonExportDeltaFn = unsafe extern "C" fn(
    *const c_char,
    *const c_char,
    u64,
    u64,
    *const c_char,
    *mut c_char,
    usize,
) -> c_int;
type JsonImportDeltaFn = unsafe extern "C" fn(
    *const c_char,
    *const c_char,
    *const c_char,
    *mut c_char,
    usize,
) -> c_int;
type JsonMaintenanceWizardFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    *const c_char,
    *mut c_char,
    usize,
) -> c_int;
type JsonVerifyFn =
    unsafe extern "C" fn(*mut EbBackupEngine, u64, c_uint, *mut c_char, usize) -> c_int;
type JsonManifestFn =
    unsafe extern "C" fn(*mut EbBackupEngine, u64, *mut c_char, usize) -> c_int;
type JsonPathHistoryFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    *const c_char,
    u64,
    u64,
    *mut c_char,
    usize,
) -> c_int;
type JsonManifestPageFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    u64,
    *const c_char,
    u64,
    u64,
    *mut c_char,
    usize,
) -> c_int;
type JsonDiffFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    u64,
    u64,
    *mut c_char,
    usize,
) -> c_int;
type JsonPathIndexFn =
    unsafe extern "C" fn(*mut EbBackupEngine, c_int, *mut c_char, usize) -> c_int;
type JsonDryRunFn =
    unsafe extern "C" fn(*mut EbBackupEngine, c_int, *mut c_char, usize) -> c_int;
type JsonPruneFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    *const c_char,
    c_int,
    c_int,
    *mut c_char,
    usize,
) -> c_int;
type JsonPlainFn = unsafe extern "C" fn(*mut c_char, usize) -> c_int;
type JsonHooksFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    *const c_char,
    *mut c_char,
    usize,
) -> c_int;
type SetProgressFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    Option<unsafe extern "C" fn(u64, *mut c_void)>,
    *mut c_void,
);
type ListJobsJsonFn = unsafe extern "C" fn(*const c_char) -> *mut c_char;
type RepoJsonFn = unsafe extern "C" fn(*const c_char, *const c_char) -> *mut c_char;
type EngTxnJsonFn = unsafe extern "C" fn(*mut EbBackupEngine, u64) -> *mut c_char;
type EngOpJsonFn = unsafe extern "C" fn(*mut EbBackupEngine, *const c_char) -> *mut c_char;
type EngOptionsJsonFn = unsafe extern "C" fn(*mut EbBackupEngine, *const c_char) -> *mut c_char;
type EngPlainJsonFn = unsafe extern "C" fn(*mut EbBackupEngine) -> *mut c_char;
type UpsertJobFn = unsafe extern "C" fn(*const c_char, *const c_char) -> c_int;
type DeleteJobFn = unsafe extern "C" fn(*const c_char, *const c_char) -> c_int;
type RunJobFn =
    unsafe extern "C" fn(*mut EbBackupEngine, *const c_char, c_int, c_uint) -> c_int;
type FreeStringFn = unsafe extern "C" fn(*mut c_char);
type SetFilterJsonFn = unsafe extern "C" fn(*mut EbBackupEngine, *const c_char) -> c_int;
type SuggestExcludeFiltersJsonFn = unsafe extern "C" fn(*const c_char) -> *mut c_char;

pub struct EbbackupApi {
    _lib: Library,
    pub open_ex: OpenExFn,
    pub close: CloseFn,
    pub set_password: SetPasswordFn,
    pub set_audit_key: SetAuditKeyFn,
    pub load_filter_file: LoadFilterFn,
    pub init_repo_json: JsonInitFn,
    pub repo_info_json: JsonEngFn,
    pub list_snapshots_json: JsonEngFn,
    pub list_manifest_files_json: JsonManifestFn,
    pub run_backup_json: JsonBackupFn,
    pub run_restore_json: JsonRestoreFn,
    pub run_restore_ex_json: JsonRestoreExFn,
    pub preview_restore_json: JsonPreviewRestoreFn,
    pub preview_in_place_json: JsonPreviewInPlaceFn,
    pub apply_in_place_json: JsonApplyInPlaceFn,
    pub export_delta_json: JsonExportDeltaFn,
    pub import_delta_json: JsonImportDeltaFn,
    pub run_maintenance_wizard_json: JsonMaintenanceWizardFn,
    pub verify_json: JsonVerifyFn,
    pub recover_json: JsonEngFn,
    pub compact_json: JsonDryRunFn,
    pub gc_orphans_json: JsonDryRunFn,
    pub prune_snapshots_json: JsonPruneFn,
    pub runtime_info_json: JsonPlainFn,
    pub last_error_json: JsonEngFn,
    pub get_stats_json: JsonEngFn,
    pub build_path_index_json: JsonPathIndexFn,
    pub query_path_history_json: JsonPathHistoryFn,
    pub list_manifest_page_json: JsonManifestPageFn,
    pub diff_snapshots_json: JsonDiffFn,
    pub export_restore_report_json: JsonEngFn,
    pub get_backup_report_json: JsonManifestFn,
    pub set_backup_hooks_json: JsonHooksFn,
    pub set_progress: SetProgressFn,
    pub list_jobs_json: ListJobsJsonFn,
    pub upsert_job_json: UpsertJobFn,
    pub delete_job: DeleteJobFn,
    pub run_job: RunJobFn,
    pub enqueue_job_json: RepoJsonFn,
    pub run_job_queue_json: EngOptionsJsonFn,
    pub job_queue_status_json: ListJobsJsonFn,
    pub snapshot_reachability_json: EngTxnJsonFn,
    pub rpo_summary_json: EngPlainJsonFn,
    pub orphan_explain_json: EngTxnJsonFn,
    pub append_ops_audit_json: EngOpJsonFn,
    pub list_ops_audit_json: EngPlainJsonFn,
    pub set_filter_json: SetFilterJsonFn,
    pub suggest_exclude_filters_json: SuggestExcludeFiltersJsonFn,
    pub free_string: FreeStringFn,
}

static API: OnceCell<EbbackupApi> = OnceCell::new();
static RUNTIME_DIR: OnceCell<PathBuf> = OnceCell::new();

pub fn set_runtime_dir(dir: PathBuf) {
    let _ = RUNTIME_DIR.set(dir);
}

fn dll_search_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    if let Some(dir) = RUNTIME_DIR.get() {
        paths.push(dir.join("ebbackup_workbench.dll"));
    }
    if let Ok(manifest) = std::env::var("CARGO_MANIFEST_DIR") {
        paths.push(PathBuf::from(manifest).join("bin/ebbackup_workbench.dll"));
    }
    paths.push(PathBuf::from("ebbackup_workbench.dll"));
    paths
}

macro_rules! load_sym {
    ($lib:expr, $name:expr) => {
        *$lib
            .get(concat!($name, "\0").as_bytes())
            .map_err(|e| format!("symbol {}: {e}", $name))?
    };
}

pub fn api() -> Result<&'static EbbackupApi, String> {
    if let Some(api) = API.get() {
        return Ok(api);
    }
    let dll_path = dll_search_paths()
        .into_iter()
        .find(|p| p.is_file())
        .ok_or_else(|| "ebbackup_workbench.dll not found; run npm run sync:runtime".to_string())?;

    unsafe {
        let lib = Library::new(&dll_path).map_err(|e| format!("load DLL: {e}"))?;
        let api = EbbackupApi {
            open_ex: load_sym!(lib, "eb_backup_open_ex"),
            close: load_sym!(lib, "eb_backup_close"),
            set_password: load_sym!(lib, "eb_backup_set_password"),
            set_audit_key: load_sym!(lib, "eb_backup_set_audit_key"),
            load_filter_file: load_sym!(lib, "eb_backup_load_filter_file"),
            init_repo_json: load_sym!(lib, "ebbackup_workbench_init_repo_json"),
            repo_info_json: load_sym!(lib, "ebbackup_workbench_repo_info_json"),
            list_snapshots_json: load_sym!(lib, "ebbackup_workbench_list_snapshots_json"),
            list_manifest_files_json: load_sym!(lib, "ebbackup_workbench_list_manifest_files_json"),
            run_backup_json: load_sym!(lib, "ebbackup_workbench_run_backup_json"),
            run_restore_json: load_sym!(lib, "ebbackup_workbench_run_restore_json"),
            run_restore_ex_json: load_sym!(lib, "ebbackup_workbench_run_restore_ex_json"),
            preview_restore_json: load_sym!(lib, "ebbackup_workbench_preview_restore_json"),
            preview_in_place_json: load_sym!(lib, "ebbackup_workbench_preview_in_place_json"),
            apply_in_place_json: load_sym!(lib, "ebbackup_workbench_apply_in_place_json"),
            export_delta_json: load_sym!(lib, "ebbackup_workbench_export_delta_json"),
            import_delta_json: load_sym!(lib, "ebbackup_workbench_import_delta_json"),
            run_maintenance_wizard_json: load_sym!(
                lib,
                "ebbackup_workbench_run_maintenance_wizard_json"
            ),
            verify_json: load_sym!(lib, "ebbackup_workbench_verify_json"),
            recover_json: load_sym!(lib, "ebbackup_workbench_recover_json"),
            compact_json: load_sym!(lib, "ebbackup_workbench_compact_json"),
            gc_orphans_json: load_sym!(lib, "ebbackup_workbench_gc_orphans_json"),
            prune_snapshots_json: load_sym!(lib, "ebbackup_workbench_prune_snapshots_json"),
            runtime_info_json: load_sym!(lib, "ebbackup_workbench_runtime_info_json"),
            last_error_json: load_sym!(lib, "ebbackup_workbench_last_error_json"),
            get_stats_json: load_sym!(lib, "ebbackup_workbench_get_stats_json"),
            build_path_index_json: load_sym!(lib, "ebbackup_workbench_build_path_index_json"),
            query_path_history_json: load_sym!(lib, "ebbackup_workbench_query_path_history_json"),
            list_manifest_page_json: load_sym!(lib, "ebbackup_workbench_list_manifest_page_json"),
            diff_snapshots_json: load_sym!(lib, "ebbackup_workbench_diff_snapshots_json"),
            export_restore_report_json: load_sym!(
                lib,
                "ebbackup_workbench_export_restore_report_json"
            ),
            get_backup_report_json: load_sym!(lib, "ebbackup_workbench_get_backup_report_json"),
            set_backup_hooks_json: load_sym!(lib, "ebbackup_workbench_set_backup_hooks_json"),
            set_progress: load_sym!(lib, "eb_backup_set_progress"),
            list_jobs_json: load_sym!(lib, "eb_backup_list_jobs_json"),
            upsert_job_json: load_sym!(lib, "eb_backup_upsert_job_json"),
            delete_job: load_sym!(lib, "eb_backup_delete_job"),
            run_job: load_sym!(lib, "eb_backup_run_job"),
            enqueue_job_json: load_sym!(lib, "eb_backup_enqueue_job_json"),
            run_job_queue_json: load_sym!(lib, "eb_backup_run_job_queue_json"),
            job_queue_status_json: load_sym!(lib, "eb_backup_job_queue_status_json"),
            snapshot_reachability_json: load_sym!(lib, "eb_backup_snapshot_reachability_json"),
            rpo_summary_json: load_sym!(lib, "eb_backup_rpo_summary_json"),
            orphan_explain_json: load_sym!(lib, "eb_backup_orphan_explain_json"),
            append_ops_audit_json: load_sym!(lib, "eb_backup_append_ops_audit_json"),
            list_ops_audit_json: load_sym!(lib, "eb_backup_list_ops_audit_json"),
            set_filter_json: load_sym!(lib, "eb_backup_set_filter_json"),
            suggest_exclude_filters_json: load_sym!(lib, "eb_backup_suggest_exclude_filters_json"),
            free_string: load_sym!(lib, "eb_backup_free_string"),
            _lib: lib,
        };
        API.set(api).map_err(|_| "API init race".to_string())?;
        Ok(API.get().unwrap())
    }
}

pub struct EnginePtr(pub *mut EbBackupEngine);
unsafe impl Send for EnginePtr {}
unsafe impl Sync for EnginePtr {}

pub struct Session {
    #[allow(dead_code)]
    pub profile_id: String,
    pub path: String,
    pub eng: EnginePtr,
}

pub static SESSION: Mutex<Option<Session>> = Mutex::new(None);

pub fn cstr(s: &str) -> Result<CString, String> {
    CString::new(s).map_err(|e| e.to_string())
}

pub fn buf_string(cap: usize) -> Vec<u8> {
    vec![0u8; cap]
}

pub fn buf_to_string(buf: &[u8]) -> String {
    let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    String::from_utf8_lossy(&buf[..end]).into_owned()
}

pub fn call_json<F>(mut f: F) -> Result<serde_json::Value, String>
where
    F: FnMut(*mut u8, usize) -> c_int,
{
    let mut buf = buf_string(1024 * 1024);
    let rc = f(buf.as_mut_ptr(), buf.len());
    let text = buf_to_string(&buf);
    let v: serde_json::Value = serde_json::from_str(&text).unwrap_or(serde_json::json!({
        "ok": false,
        "error": text
    }));
    if rc != 0 || v.get("ok").and_then(|x| x.as_bool()) == Some(false) {
        return Err(v
            .get("error")
            .and_then(|x| x.as_str())
            .unwrap_or("operation failed")
            .to_string());
    }
    Ok(v)
}

pub fn call_jobs_json(repo_path: &str) -> Result<serde_json::Value, String> {
    let api = api()?;
    let path = cstr(repo_path)?;
    unsafe {
        let ptr = (api.list_jobs_json)(path.as_ptr());
        if ptr.is_null() {
            return Err("list_jobs_json returned null".to_string());
        }
        let text = std::ffi::CStr::from_ptr(ptr)
            .to_string_lossy()
            .into_owned();
        (api.free_string)(ptr);
        let v: serde_json::Value = serde_json::from_str(&text)
            .map_err(|e| format!("list_jobs json parse: {e}"))?;
        if v.get("ok").and_then(|x| x.as_bool()) == Some(false) {
            return Err(v
                .get("error")
                .and_then(|x| x.as_str())
                .unwrap_or("list_jobs failed")
                .to_string());
        }
        Ok(v)
    }
}

pub fn call_free_json<F>(f: F) -> Result<serde_json::Value, String>
where
    F: FnOnce() -> *mut c_char,
{
    let api = api()?;
    unsafe {
        let ptr = f();
        if ptr.is_null() {
            return Err("C API returned null".to_string());
        }
        let text = std::ffi::CStr::from_ptr(ptr)
            .to_string_lossy()
            .into_owned();
        (api.free_string)(ptr);
        let v: serde_json::Value = serde_json::from_str(&text)
            .map_err(|e| format!("json parse: {e}"))?;
        if v.get("ok").and_then(|x| x.as_bool()) == Some(false) {
            return Err(v
                .get("error")
                .and_then(|x| x.as_str())
                .unwrap_or("operation failed")
                .to_string());
        }
        Ok(v)
    }
}
