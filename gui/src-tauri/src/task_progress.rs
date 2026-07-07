//! Tauri event bridge for `eb_backup_set_progress`.
//!
//! C++ pipeline workers call the progress callback from background threads.
//! All `AppHandle::emit` calls must be marshalled to the main thread.

use parking_lot::Mutex;
use std::os::raw::c_void;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::thread;
use std::time::Duration;
use tauri::{AppHandle, Emitter};

static PROGRESS_APP: Mutex<Option<AppHandle>> = Mutex::new(None);
static PROGRESS_KIND: Mutex<String> = Mutex::new(String::new());
static LATEST_PERMILLE: AtomicU64 = AtomicU64::new(0);
static POLL_ACTIVE: AtomicBool = AtomicBool::new(false);

fn emit_on_main(app: &AppHandle, event: &str, payload: serde_json::Value) {
    let app = app.clone();
    let event = event.to_string();
    let _ = app.clone().run_on_main_thread(move || {
        let _ = app.emit(&event, payload);
    });
}

pub fn begin_progress(app: AppHandle, kind: &str) {
    *PROGRESS_APP.lock() = Some(app.clone());
    *PROGRESS_KIND.lock() = kind.to_string();
    LATEST_PERMILLE.store(0, Ordering::Relaxed);
    emit_on_main(&app, "task-started", serde_json::json!({ "kind": kind }));

    if POLL_ACTIVE
        .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
        .is_ok()
    {
        thread::spawn(progress_poll_loop);
    }
}

pub fn end_progress(ok: bool) {
    POLL_ACTIVE.store(false, Ordering::Relaxed);
    let kind = PROGRESS_KIND.lock().clone();
    if let Some(app) = PROGRESS_APP.lock().take() {
        let permille = LATEST_PERMILLE.load(Ordering::Relaxed);
        emit_on_main(
            &app,
            "task-progress",
            serde_json::json!({ "kind": kind, "permille": permille }),
        );
        emit_on_main(
            &app,
            "task-finished",
            serde_json::json!({ "kind": kind, "ok": ok }),
        );
    }
    PROGRESS_KIND.lock().clear();
}

fn progress_poll_loop() {
    let mut last_sent = u64::MAX;
    while POLL_ACTIVE.load(Ordering::Relaxed) {
        let permille = LATEST_PERMILLE.load(Ordering::Relaxed);
        if permille != last_sent {
            let kind = PROGRESS_KIND.lock().clone();
            if let Some(app) = PROGRESS_APP.lock().as_ref() {
                emit_on_main(
                    app,
                    "task-progress",
                    serde_json::json!({ "kind": kind, "permille": permille }),
                );
            }
            last_sent = permille;
        }
        thread::sleep(Duration::from_millis(80));
    }
}

unsafe extern "C" fn progress_trampoline(permille: u64, _: *mut c_void) {
    LATEST_PERMILLE.store(permille, Ordering::Relaxed);
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
