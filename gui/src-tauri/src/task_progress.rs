//! Tauri event bridge for `eb_backup_set_progress`.

use parking_lot::Mutex;
use std::os::raw::c_void;
use tauri::{AppHandle, Emitter};

static PROGRESS_APP: Mutex<Option<AppHandle>> = Mutex::new(None);
static PROGRESS_KIND: Mutex<String> = Mutex::new(String::new());

pub fn begin_progress(app: AppHandle, kind: &str) {
    *PROGRESS_APP.lock() = Some(app.clone());
    *PROGRESS_KIND.lock() = kind.to_string();
    let _ = app.emit("task-started", serde_json::json!({ "kind": kind }));
}

pub fn end_progress(ok: bool) {
    let kind = PROGRESS_KIND.lock().clone();
    if let Some(app) = PROGRESS_APP.lock().take() {
        let _ = app.emit(
            "task-finished",
            serde_json::json!({ "kind": kind, "ok": ok }),
        );
    }
    PROGRESS_KIND.lock().clear();
}

unsafe extern "C" fn progress_trampoline(permille: u64, _: *mut c_void) {
    let kind = PROGRESS_KIND.lock().clone();
    if let Some(app) = PROGRESS_APP.lock().as_ref() {
        let _ = app.emit(
            "task-progress",
            serde_json::json!({ "kind": kind, "permille": permille }),
        );
    }
}

pub unsafe fn attach_progress(
    set_progress: unsafe extern "C" fn(
        *mut crate::ebbackup_ffi::EbBackupEngine,
        Option<unsafe extern "C" fn(u64, *mut c_void)>,
        *mut c_void,
    ),
    eng: *mut crate::ebbackup_ffi::EbBackupEngine,
) {
    set_progress(eng, Some(progress_trampoline), std::ptr::null_mut());
}

pub unsafe fn detach_progress(
    set_progress: unsafe extern "C" fn(
        *mut crate::ebbackup_ffi::EbBackupEngine,
        Option<unsafe extern "C" fn(u64, *mut c_void)>,
        *mut c_void,
    ),
    eng: *mut crate::ebbackup_ffi::EbBackupEngine,
) {
    set_progress(eng, None, std::ptr::null_mut());
}
