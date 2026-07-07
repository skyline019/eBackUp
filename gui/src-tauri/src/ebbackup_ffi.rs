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
type JsonVerifyFn =
    unsafe extern "C" fn(*mut EbBackupEngine, u64, c_uint, *mut c_char, usize) -> c_int;
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

pub struct EbbackupApi {
    _lib: Library,
    pub open_ex: OpenExFn,
    pub close: CloseFn,
    pub set_password: SetPasswordFn,
    pub load_filter_file: LoadFilterFn,
    pub init_repo_json: JsonInitFn,
    pub repo_info_json: JsonEngFn,
    pub list_snapshots_json: JsonEngFn,
    pub run_backup_json: JsonBackupFn,
    pub run_restore_json: JsonRestoreFn,
    pub verify_json: JsonVerifyFn,
    pub recover_json: JsonEngFn,
    pub compact_json: JsonDryRunFn,
    pub gc_orphans_json: JsonDryRunFn,
    pub prune_snapshots_json: JsonPruneFn,
    pub runtime_info_json: JsonPlainFn,
    pub last_error_json: JsonEngFn,
    pub get_stats_json: JsonEngFn,
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
            load_filter_file: load_sym!(lib, "eb_backup_load_filter_file"),
            init_repo_json: load_sym!(lib, "ebbackup_workbench_init_repo_json"),
            repo_info_json: load_sym!(lib, "ebbackup_workbench_repo_info_json"),
            list_snapshots_json: load_sym!(lib, "ebbackup_workbench_list_snapshots_json"),
            run_backup_json: load_sym!(lib, "ebbackup_workbench_run_backup_json"),
            run_restore_json: load_sym!(lib, "ebbackup_workbench_run_restore_json"),
            verify_json: load_sym!(lib, "ebbackup_workbench_verify_json"),
            recover_json: load_sym!(lib, "ebbackup_workbench_recover_json"),
            compact_json: load_sym!(lib, "ebbackup_workbench_compact_json"),
            gc_orphans_json: load_sym!(lib, "ebbackup_workbench_gc_orphans_json"),
            prune_snapshots_json: load_sym!(lib, "ebbackup_workbench_prune_snapshots_json"),
            runtime_info_json: load_sym!(lib, "ebbackup_workbench_runtime_info_json"),
            last_error_json: load_sym!(lib, "ebbackup_workbench_last_error_json"),
            get_stats_json: load_sym!(lib, "ebbackup_workbench_get_stats_json"),
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
