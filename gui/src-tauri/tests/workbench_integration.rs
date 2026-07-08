//! Integration tests against `ebbackup_workbench.dll` JSON shim.
//!
//! Requires a built DLL:
//!   cmake --build ../../build --config Release --target ebbackup_workbench
//!   npm run sync:runtime

use libloading::Library;
use std::ffi::CString;
use std::fs;
use std::os::raw::{c_char, c_int, c_uint, c_void};
use std::path::{Path, PathBuf};

type EbBackupEngine = c_void;
type OpenExFn = unsafe extern "C" fn(*const c_char, *mut c_int) -> *mut EbBackupEngine;
type CloseFn = unsafe extern "C" fn(*mut EbBackupEngine);
type JsonInitFn = unsafe extern "C" fn(*const c_char, c_uint, *mut c_char, usize) -> c_int;
type JsonEngFn = unsafe extern "C" fn(*mut EbBackupEngine, *mut c_char, usize) -> c_int;
type JsonBackupFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    *const c_char,
    c_int,
    c_uint,
    *mut c_char,
    usize,
) -> c_int;
type JsonVerifyFn =
    unsafe extern "C" fn(*mut EbBackupEngine, u64, c_uint, *mut c_char, usize) -> c_int;
type JsonManifestFn =
    unsafe extern "C" fn(*mut EbBackupEngine, u64, *mut c_char, usize) -> c_int;
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
type JsonMaintenanceWizardFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    *const c_char,
    *mut c_char,
    usize,
) -> c_int;
type JsonPlainFn = unsafe extern "C" fn(*mut c_char, usize) -> c_int;
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
type JsonReportFn = unsafe extern "C" fn(
    *mut EbBackupEngine,
    u64,
    *mut c_char,
    usize,
) -> c_int;
type ListJobsJsonFn = unsafe extern "C" fn(*const c_char) -> *mut c_char;
type RepoJsonFn = unsafe extern "C" fn(*const c_char, *const c_char) -> *mut c_char;
type EngOptionsJsonFn = unsafe extern "C" fn(*mut EbBackupEngine, *const c_char) -> *mut c_char;
type EngTxnJsonFn = unsafe extern "C" fn(*mut EbBackupEngine, u64) -> *mut c_char;
type EngOpJsonFn = unsafe extern "C" fn(*mut EbBackupEngine, *const c_char) -> *mut c_char;
type EngPlainJsonFn = unsafe extern "C" fn(*mut EbBackupEngine) -> *mut c_char;
type UpsertJobFn = unsafe extern "C" fn(*const c_char, *const c_char) -> c_int;
type DeleteJobFn = unsafe extern "C" fn(*const c_char, *const c_char) -> c_int;
type RunJobFn =
    unsafe extern "C" fn(*mut EbBackupEngine, *const c_char, c_int, c_uint) -> c_int;
type FreeStringFn = unsafe extern "C" fn(*mut c_char);
type SuggestExcludeFiltersJsonFn = unsafe extern "C" fn(*const c_char) -> *mut c_char;

struct WorkbenchDll {
    _lib: Library,
    open_ex: OpenExFn,
    close: CloseFn,
    init_repo_json: JsonInitFn,
    repo_info_json: JsonEngFn,
    list_snapshots_json: JsonEngFn,
    list_manifest_files_json: JsonManifestFn,
    run_backup_json: JsonBackupFn,
    verify_json: JsonVerifyFn,
    preview_restore_json: JsonPreviewRestoreFn,
    preview_in_place_json: JsonPreviewInPlaceFn,
    apply_in_place_json: JsonApplyInPlaceFn,
    run_restore_ex_json: JsonRestoreExFn,
    run_maintenance_wizard_json: JsonMaintenanceWizardFn,
    runtime_info_json: JsonPlainFn,
    get_stats_json: JsonEngFn,
    query_path_history_json: JsonPathHistoryFn,
    list_manifest_page_json: JsonManifestPageFn,
    diff_snapshots_json: JsonDiffFn,
    get_backup_report_json: JsonReportFn,
    list_jobs_json: ListJobsJsonFn,
    upsert_job_json: UpsertJobFn,
    delete_job: DeleteJobFn,
    run_job: RunJobFn,
    enqueue_job_json: RepoJsonFn,
    run_job_queue_json: EngOptionsJsonFn,
    job_queue_status_json: ListJobsJsonFn,
    snapshot_reachability_json: EngTxnJsonFn,
    rpo_summary_json: EngPlainJsonFn,
    orphan_explain_json: EngTxnJsonFn,
    append_ops_audit_json: EngOpJsonFn,
    list_ops_audit_json: EngPlainJsonFn,
    suggest_exclude_filters_json: SuggestExcludeFiltersJsonFn,
    free_string: FreeStringFn,
}

impl WorkbenchDll {
    fn load() -> Result<Self, String> {
        let path = find_dll().ok_or_else(|| {
            "ebbackup_workbench.dll not found — build target ebbackup_workbench and run sync:runtime"
                .to_string()
        })?;
        unsafe {
            let lib = Library::new(&path).map_err(|e| format!("load {}: {e}", path.display()))?;
            Ok(Self {
                open_ex: *lib.get(b"eb_backup_open_ex\0").map_err(|e| e.to_string())?,
                close: *lib.get(b"eb_backup_close\0").map_err(|e| e.to_string())?,
                init_repo_json: *lib
                    .get(b"ebbackup_workbench_init_repo_json\0")
                    .map_err(|e| e.to_string())?,
                repo_info_json: *lib
                    .get(b"ebbackup_workbench_repo_info_json\0")
                    .map_err(|e| e.to_string())?,
                list_snapshots_json: *lib
                    .get(b"ebbackup_workbench_list_snapshots_json\0")
                    .map_err(|e| e.to_string())?,
                list_manifest_files_json: *lib
                    .get(b"ebbackup_workbench_list_manifest_files_json\0")
                    .map_err(|e| e.to_string())?,
                run_backup_json: *lib
                    .get(b"ebbackup_workbench_run_backup_json\0")
                    .map_err(|e| e.to_string())?,
                verify_json: *lib
                    .get(b"ebbackup_workbench_verify_json\0")
                    .map_err(|e| e.to_string())?,
                preview_restore_json: *lib
                    .get(b"ebbackup_workbench_preview_restore_json\0")
                    .map_err(|e| e.to_string())?,
                preview_in_place_json: *lib
                    .get(b"ebbackup_workbench_preview_in_place_json\0")
                    .map_err(|e| e.to_string())?,
                apply_in_place_json: *lib
                    .get(b"ebbackup_workbench_apply_in_place_json\0")
                    .map_err(|e| e.to_string())?,
                run_restore_ex_json: *lib
                    .get(b"ebbackup_workbench_run_restore_ex_json\0")
                    .map_err(|e| e.to_string())?,
                run_maintenance_wizard_json: *lib
                    .get(b"ebbackup_workbench_run_maintenance_wizard_json\0")
                    .map_err(|e| e.to_string())?,
                runtime_info_json: *lib
                    .get(b"ebbackup_workbench_runtime_info_json\0")
                    .map_err(|e| e.to_string())?,
                get_stats_json: *lib
                    .get(b"ebbackup_workbench_get_stats_json\0")
                    .map_err(|e| e.to_string())?,
                query_path_history_json: *lib
                    .get(b"ebbackup_workbench_query_path_history_json\0")
                    .map_err(|e| e.to_string())?,
                list_manifest_page_json: *lib
                    .get(b"ebbackup_workbench_list_manifest_page_json\0")
                    .map_err(|e| e.to_string())?,
                diff_snapshots_json: *lib
                    .get(b"ebbackup_workbench_diff_snapshots_json\0")
                    .map_err(|e| e.to_string())?,
                get_backup_report_json: *lib
                    .get(b"ebbackup_workbench_get_backup_report_json\0")
                    .map_err(|e| e.to_string())?,
                list_jobs_json: *lib
                    .get(b"eb_backup_list_jobs_json\0")
                    .map_err(|e| e.to_string())?,
                upsert_job_json: *lib
                    .get(b"eb_backup_upsert_job_json\0")
                    .map_err(|e| e.to_string())?,
                delete_job: *lib
                    .get(b"eb_backup_delete_job\0")
                    .map_err(|e| e.to_string())?,
                run_job: *lib
                    .get(b"eb_backup_run_job\0")
                    .map_err(|e| e.to_string())?,
                enqueue_job_json: *lib
                    .get(b"eb_backup_enqueue_job_json\0")
                    .map_err(|e| e.to_string())?,
                run_job_queue_json: *lib
                    .get(b"eb_backup_run_job_queue_json\0")
                    .map_err(|e| e.to_string())?,
                job_queue_status_json: *lib
                    .get(b"eb_backup_job_queue_status_json\0")
                    .map_err(|e| e.to_string())?,
                snapshot_reachability_json: *lib
                    .get(b"eb_backup_snapshot_reachability_json\0")
                    .map_err(|e| e.to_string())?,
                rpo_summary_json: *lib
                    .get(b"eb_backup_rpo_summary_json\0")
                    .map_err(|e| e.to_string())?,
                orphan_explain_json: *lib
                    .get(b"eb_backup_orphan_explain_json\0")
                    .map_err(|e| e.to_string())?,
                append_ops_audit_json: *lib
                    .get(b"eb_backup_append_ops_audit_json\0")
                    .map_err(|e| e.to_string())?,
                list_ops_audit_json: *lib
                    .get(b"eb_backup_list_ops_audit_json\0")
                    .map_err(|e| e.to_string())?,
                suggest_exclude_filters_json: *lib
                    .get(b"eb_backup_suggest_exclude_filters_json\0")
                    .map_err(|e| e.to_string())?,
                free_string: *lib
                    .get(b"eb_backup_free_string\0")
                    .map_err(|e| e.to_string())?,
                _lib: lib,
            })
        }
    }
}

fn find_dll() -> Option<PathBuf> {
    let mut candidates = Vec::new();
    if let Ok(dir) = std::env::var("EBBACKUP_DLL_DIR") {
        candidates.push(PathBuf::from(dir).join("ebbackup_workbench.dll"));
    }
    if let Ok(manifest) = std::env::var("CARGO_MANIFEST_DIR") {
        let m = PathBuf::from(manifest);
        candidates.push(m.join("bin/ebbackup_workbench.dll"));
        candidates.push(m.join("../../build/engine_cpp/Release/ebbackup_workbench.dll"));
        candidates.push(m.join("../../build/engine_cpp/Debug/ebbackup_workbench.dll"));
    }
    candidates.push(PathBuf::from("ebbackup_workbench.dll"));
    candidates.into_iter().find(|p| p.is_file())
}

fn cstr(s: &str) -> CString {
    CString::new(s).expect("nul in test string")
}

fn call_json_plain(f: impl FnOnce(*mut c_char, usize) -> c_int) -> serde_json::Value {
    let mut buf = vec![0u8; 1024 * 1024];
    let rc = f(buf.as_mut_ptr() as *mut c_char, buf.len());
    let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    let text = String::from_utf8_lossy(&buf[..end]);
    let v: serde_json::Value =
        serde_json::from_str(&text).unwrap_or_else(|_| serde_json::json!({"ok": false, "raw": text.to_string()}));
    assert_eq!(rc, 0, "json rc={rc} body={text}");
    assert_eq!(v.get("ok").and_then(|x| x.as_bool()), Some(true), "body={text}");
    v
}

fn call_json_eng(
    eng: *mut EbBackupEngine,
    f: impl FnOnce(*mut EbBackupEngine, *mut c_char, usize) -> c_int,
) -> serde_json::Value {
    let mut buf = vec![0u8; 1024 * 1024];
    let rc = f(eng, buf.as_mut_ptr() as *mut c_char, buf.len());
    let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    let text = String::from_utf8_lossy(&buf[..end]);
    let v: serde_json::Value =
        serde_json::from_str(&text).unwrap_or_else(|_| serde_json::json!({"ok": false, "raw": text.to_string()}));
    assert_eq!(rc, 0, "json rc={rc} body={text}");
    assert_eq!(v.get("ok").and_then(|x| x.as_bool()), Some(true), "body={text}");
    v
}

struct TempFixture {
    root: PathBuf,
    repo: PathBuf,
    source: PathBuf,
}

impl TempFixture {
    fn new() -> Self {
        let root = std::env::temp_dir().join(format!(
            "ebbackup_wb_it_{}",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        let repo = root.join("repo");
        let source = root.join("source");
        fs::create_dir_all(&source).expect("create source");
        fs::write(source.join("hello.txt"), b"ebbackup workbench integration test\n").expect("write source");
        Self { root, repo, source }
    }

    fn repo_str(&self) -> CString {
        cstr(&to_win_path(&self.repo))
    }

    fn source_str(&self) -> CString {
        cstr(&to_win_path(&self.source))
    }
}

impl Drop for TempFixture {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.root);
    }
}

fn to_win_path(p: &Path) -> String {
    p.to_string_lossy().replace('/', "\\")
}

#[test]
fn runtime_info_reports_abi() {
    let dll = WorkbenchDll::load().expect("dll");
    let v = unsafe { call_json_plain(|buf, cap| (dll.runtime_info_json)(buf, cap)) };
    assert!(v.get("abi_version").and_then(|x| x.as_u64()).unwrap_or(0) >= 17);
    assert_eq!(
        v.get("workbench").and_then(|x| x.as_str()),
        Some("ebbackup")
    );
}

#[test]
fn backup_verify_roundtrip() {
    let dll = WorkbenchDll::load().expect("dll");
    let fx = TempFixture::new();

    unsafe {
        let repo = fx.repo_str();
        call_json_plain(|buf, cap| (dll.init_repo_json)(repo.as_ptr(), 0, buf, cap));

        let mut err: i32 = 0;
        let eng = (dll.open_ex)(repo.as_ptr(), &mut err);
        assert!(!eng.is_null(), "open failed status={err}");

        let src = fx.source_str();
        let backup = call_json_eng(eng, |e, buf, cap| {
            (dll.run_backup_json)(e, src.as_ptr(), 0, 0, buf, cap)
        });
        assert!(
            backup
                .get("stats")
                .and_then(|s| s.get("files_processed"))
                .and_then(|x| x.as_u64())
                .unwrap_or(0)
                >= 1,
            "expected files_processed >= 1"
        );

        let snaps = call_json_eng(eng, |e, buf, cap| (dll.list_snapshots_json)(e, buf, cap));
        assert!(
            snaps.get("count").and_then(|x| x.as_u64()).unwrap_or(0) >= 1,
            "expected at least one snapshot"
        );

        let manifest = call_json_eng(eng, |e, buf, cap| {
            (dll.list_manifest_files_json)(e, 0, buf, cap)
        });
        assert!(
            manifest.get("count").and_then(|x| x.as_u64()).unwrap_or(0) >= 1,
            "expected manifest file list"
        );
        let files = manifest
            .get("files")
            .and_then(|x| x.as_array())
            .cloned()
            .unwrap_or_default();
        assert!(
            files.iter().any(|f| {
                f.get("relative_path")
                    .and_then(|x| x.as_str())
                    .is_some_and(|p| p.contains("hello"))
            }),
            "expected hello.txt in manifest"
        );

        call_json_eng(eng, |e, buf, cap| (dll.verify_json)(e, 0, 0, buf, cap));

        let info = call_json_eng(eng, |e, buf, cap| (dll.repo_info_json)(e, buf, cap));
        assert!(
            info.get("repo_stats")
                .and_then(|s| s.get("live_bytes"))
                .and_then(|x| x.as_u64())
                .unwrap_or(0)
                > 0
        );

        let stats = call_json_eng(eng, |e, buf, cap| (dll.get_stats_json)(e, buf, cap));
        assert!(stats.get("stats").is_some() || stats.get("files_processed").is_some());

        (dll.close)(eng);
    }
}

#[test]
fn backup_report_after_commit() {
    let dll = WorkbenchDll::load().expect("dll");
    let fx = TempFixture::new();

    unsafe {
        let repo = fx.repo_str();
        call_json_plain(|buf, cap| (dll.init_repo_json)(repo.as_ptr(), 0, buf, cap));

        let mut err: i32 = 0;
        let eng = (dll.open_ex)(repo.as_ptr(), &mut err);
        assert!(!eng.is_null(), "open failed status={err}");

        let src = fx.source_str();
        call_json_eng(eng, |e, buf, cap| {
            (dll.run_backup_json)(e, src.as_ptr(), 0, 0, buf, cap)
        });

        let snaps = call_json_eng(eng, |e, buf, cap| (dll.list_snapshots_json)(e, buf, cap));
        let txn_id = snaps
            .get("snapshots")
            .and_then(|x| x.as_array())
            .and_then(|a| a.first())
            .and_then(|s| s.get("txn_id"))
            .and_then(|x| x.as_u64())
            .unwrap_or(1);

        let report = call_json_eng(eng, |e, buf, cap| {
            (dll.get_backup_report_json)(e, txn_id, buf, cap)
        });
        assert_eq!(report.get("ok").and_then(|x| x.as_bool()), Some(true));
        assert_eq!(
            report.get("txn_id").and_then(|x| x.as_u64()),
            Some(txn_id)
        );
        assert!(report.get("issues").and_then(|x| x.as_array()).is_some());

        (dll.close)(eng);
    }
}

#[test]
fn double_init_then_open_succeeds() {
    let dll = WorkbenchDll::load().expect("dll");
    let fx = TempFixture::new();
    unsafe {
        let repo = fx.repo_str();
        call_json_plain(|buf, cap| (dll.init_repo_json)(repo.as_ptr(), 0, buf, cap));
        // Second init should not crash; engine may treat existing repo as ok.
        let mut buf = vec![0u8; 65536];
        let _ = (dll.init_repo_json)(repo.as_ptr(), 0, buf.as_mut_ptr() as *mut c_char, buf.len());

        let mut err: i32 = 0;
        let eng = (dll.open_ex)(repo.as_ptr(), &mut err);
        assert!(!eng.is_null(), "open after double init failed status={err}");
        (dll.close)(eng);
    }
}

#[test]
fn backup_unicode_source_path() {
    let dll = WorkbenchDll::load().expect("dll");
    let src_path = PathBuf::from(r"e:\BaiduNetdiskDownload\小爱限定工程");
    if !src_path.is_dir() {
        eprintln!("skip backup_unicode_source_path: {} not found", src_path.display());
        return;
    }

    let root = std::env::temp_dir().join(format!(
        "ebbackup_wb_unicode_{}",
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    let repo = root.join("repo");
    let _ = fs::remove_dir_all(&root);

    unsafe {
        let repo_c = cstr(&to_win_path(&repo));
        call_json_plain(|buf, cap| (dll.init_repo_json)(repo_c.as_ptr(), 0, buf, cap));

        let mut err: i32 = 0;
        let eng = (dll.open_ex)(repo_c.as_ptr(), &mut err);
        assert!(!eng.is_null(), "open failed status={err}");

        let src_c = cstr(&to_win_path(&src_path));
        let backup = call_json_eng(eng, |e, buf, cap| {
            (dll.run_backup_json)(e, src_c.as_ptr(), 0, 0x0002, buf, cap)
        });
        assert!(
            backup
                .get("stats")
                .and_then(|s| s.get("files_processed"))
                .and_then(|x| x.as_u64())
                .unwrap_or(0)
                > 0,
            "expected files_processed > 0"
        );

        (dll.close)(eng);
    }

    let _ = fs::remove_dir_all(&root);
}

#[test]
fn selective_restore_preview_and_wizard_dry_run() {
    let dll = WorkbenchDll::load().expect("dll");
    let fx = TempFixture::new();
    unsafe {
        let repo = fx.repo_str();
        call_json_plain(|buf, cap| (dll.init_repo_json)(repo.as_ptr(), 0, buf, cap));

        let mut err: i32 = 0;
        let eng = (dll.open_ex)(repo.as_ptr(), &mut err);
        assert!(!eng.is_null(), "open failed status={err}");

        let src = fx.source_str();
        call_json_eng(eng, |e, buf, cap| {
            (dll.run_backup_json)(e, src.as_ptr(), 0, 0, buf, cap)
        });

        let filter = cstr(r#"{"include_paths":["hello.txt"]}"#);
        let preview = call_json_eng(eng, |e, buf, cap| {
            (dll.preview_restore_json)(e, 0, filter.as_ptr(), buf, cap)
        });
        assert!(
            preview.get("file_count").and_then(|x| x.as_u64()).unwrap_or(0) >= 1,
            "preview should include hello.txt"
        );

        let dest = fx.root.join("dest");
        fs::create_dir_all(&dest).expect("dest dir");
        let dest_c = cstr(&to_win_path(&dest));
        let remap = cstr(r#"{"mode":"keep"}"#);
        call_json_eng(eng, |e, buf, cap| {
            (dll.run_restore_ex_json)(
                e,
                dest_c.as_ptr(),
                0,
                0,
                filter.as_ptr(),
                remap.as_ptr(),
                buf,
                cap,
            )
        });
        assert!(dest.join("hello.txt").is_file(), "selective restore file missing");

        let wizard_opts = cstr(r#"{"dry_run_only":true}"#);
        let wizard = call_json_eng(eng, |e, buf, cap| {
            (dll.run_maintenance_wizard_json)(e, wizard_opts.as_ptr(), buf, cap)
        });
        assert!(wizard.get("repo_stats").is_some());
        assert!(wizard.get("stats_after").is_some());

        (dll.close)(eng);
    }
}

#[test]
fn in_place_preview_and_apply_roundtrip() {
    let dll = WorkbenchDll::load().expect("dll");
    let fx = TempFixture::new();
    unsafe {
        let repo = fx.repo_str();
        call_json_plain(|buf, cap| (dll.init_repo_json)(repo.as_ptr(), 0, buf, cap));

        let mut err: i32 = 0;
        let eng = (dll.open_ex)(repo.as_ptr(), &mut err);
        assert!(!eng.is_null(), "open failed status={err}");

        let src = fx.source_str();
        call_json_eng(eng, |e, buf, cap| {
            (dll.run_backup_json)(e, src.as_ptr(), 0, 0, buf, cap)
        });

        std::fs::write(fx.source.join("hello.txt"), b"modified in place\n").expect("modify source");

        let target = fx.source_str();
        let preview = call_json_eng(eng, |e, buf, cap| {
            (dll.preview_in_place_json)(
                e,
                0,
                target.as_ptr(),
                std::ptr::null(),
                std::ptr::null(),
                buf,
                cap,
            )
        });
        assert!(
            preview.get("summary").and_then(|s| s.get("modify_count")).and_then(|x| x.as_u64()).unwrap_or(0) >= 1
                || preview.get("entries").and_then(|x| x.as_array()).map(|a| !a.is_empty()).unwrap_or(false),
            "in-place preview expected modify or entries"
        );

        let policy = cstr("overwrite");
        let applied = call_json_eng(eng, |e, buf, cap| {
            (dll.apply_in_place_json)(
                e,
                0,
                target.as_ptr(),
                policy.as_ptr(),
                std::ptr::null(),
                std::ptr::null(),
                std::ptr::null(),
                buf,
                cap,
            )
        });
        assert!(
            applied.get("summary").and_then(|s| s.get("applied_count")).and_then(|x| x.as_u64()).unwrap_or(0) >= 1
                || applied.get("summary").and_then(|s| s.get("modify_count")).and_then(|x| x.as_u64()).unwrap_or(0) >= 1,
            "in-place apply expected changes"
        );
        let content = std::fs::read_to_string(fx.source.join("hello.txt")).expect("read hello");
        assert!(
            content.contains("ebbackup workbench integration test"),
            "in-place apply should restore snapshot content, got: {content}"
        );

        (dll.close)(eng);
    }
}

#[test]
fn path_history_and_diff_roundtrip() {
    let dll = WorkbenchDll::load().expect("dll");
    let fx = TempFixture::new();
    unsafe {
        let repo = fx.repo_str();
        call_json_plain(|buf, cap| (dll.init_repo_json)(repo.as_ptr(), 0, buf, cap));

        let mut err: i32 = 0;
        let eng = (dll.open_ex)(repo.as_ptr(), &mut err);
        assert!(!eng.is_null(), "open failed status={err}");

        let src = fx.source_str();
        call_json_eng(eng, |e, buf, cap| {
            (dll.run_backup_json)(e, src.as_ptr(), 0, 0, buf, cap)
        });
        std::fs::write(fx.root.join("source").join("hello.txt"), "v2").expect("write");
        call_json_eng(eng, |e, buf, cap| {
            (dll.run_backup_json)(e, src.as_ptr(), 1, 0, buf, cap)
        });

        let path = cstr("hello.txt");
        let history = call_json_eng(eng, |e, buf, cap| {
            (dll.query_path_history_json)(e, path.as_ptr(), 0, 100, buf, cap)
        });
        assert!(
            history.get("count").and_then(|x| x.as_u64()).unwrap_or(0) >= 1,
            "path history expected"
        );
        assert!(
            history.get("total").and_then(|x| x.as_u64()).unwrap_or(0) >= 1,
            "path history total expected"
        );

        let page = call_json_eng(eng, |e, buf, cap| {
            (dll.list_manifest_page_json)(e, 0, std::ptr::null(), 0, 50, buf, cap)
        });
        assert!(
            page.get("total").and_then(|x| x.as_u64()).unwrap_or(0) >= 1,
            "manifest page total expected"
        );

        let snaps = call_json_eng(eng, |e, buf, cap| (dll.list_snapshots_json)(e, buf, cap));
        let arr = snaps.get("snapshots").and_then(|x| x.as_array()).cloned().unwrap_or_default();
        if arr.len() >= 2 {
            let txn_a = arr[0].get("txn_id").and_then(|x| x.as_u64()).unwrap_or(0);
            let txn_b = arr[1].get("txn_id").and_then(|x| x.as_u64()).unwrap_or(0);
            let diff = call_json_eng(eng, |e, buf, cap| {
                (dll.diff_snapshots_json)(e, txn_a, txn_b, buf, cap)
            });
            assert!(diff.get("added").is_some() || diff.get("modified").is_some());
        }

        (dll.close)(eng);
    }
}

fn call_jobs_json(dll: &WorkbenchDll, repo: &CString) -> serde_json::Value {
    unsafe {
        let ptr = (dll.list_jobs_json)(repo.as_ptr());
        assert!(!ptr.is_null(), "list_jobs_json null");
        let text = std::ffi::CStr::from_ptr(ptr).to_string_lossy().into_owned();
        (dll.free_string)(ptr);
        serde_json::from_str(&text).expect("jobs json")
    }
}

fn call_repo_json(dll: &WorkbenchDll, f: impl FnOnce() -> *mut c_char) -> serde_json::Value {
    unsafe {
        let ptr = f();
        assert!(!ptr.is_null(), "repo json null");
        let text = std::ffi::CStr::from_ptr(ptr).to_string_lossy().into_owned();
        (dll.free_string)(ptr);
        serde_json::from_str(&text).expect("repo json parse")
    }
}

fn call_eng_free_json(dll: &WorkbenchDll, f: impl FnOnce() -> *mut c_char) -> serde_json::Value {
    call_repo_json(dll, f)
}

#[test]
fn jobs_api_v18_roundtrip() {
    let dll = WorkbenchDll::load().expect("dll");
    let fx = TempFixture::new();
    unsafe {
        let repo = fx.repo_str();
        call_json_plain(|buf, cap| (dll.init_repo_json)(repo.as_ptr(), 0, buf, cap));

        let src = fx.source_str();
        let job_json = format!(
            r#"{{"id":"docs","name":"Docs","source_path":"{}","retention_tag":3,"immutability_days":0,"worm":false,"exclude_globs":[]}}"#,
            src.to_string_lossy().replace('\\', "\\\\")
        );
        let job = cstr(&job_json);
        assert_eq!((dll.upsert_job_json)(repo.as_ptr(), job.as_ptr()), 0);

        let listed = call_jobs_json(&dll, &repo);
        assert_eq!(listed.get("ok").and_then(|x| x.as_bool()), Some(true));
        let jobs = listed.get("jobs").and_then(|x| x.as_array()).cloned().unwrap_or_default();
        assert_eq!(jobs.len(), 1);
        assert_eq!(jobs[0].get("id").and_then(|x| x.as_str()), Some("docs"));

        let mut err: i32 = 0;
        let eng = (dll.open_ex)(repo.as_ptr(), &mut err);
        assert!(!eng.is_null(), "open failed status={err}");

        let job_id = cstr("docs");
        assert_eq!((dll.run_job)(eng, job_id.as_ptr(), 0, 0), 0);

        let snaps = call_json_eng(eng, |e, buf, cap| (dll.list_snapshots_json)(e, buf, cap));
        let arr = snaps.get("snapshots").and_then(|x| x.as_array()).cloned().unwrap_or_default();
        assert!(!arr.is_empty(), "snapshot expected");
        assert_eq!(arr[0].get("job_id").and_then(|x| x.as_str()), Some("docs"));

        let txn = arr[0].get("txn_id").and_then(|x| x.as_u64()).unwrap_or(0);
        let report = call_json_eng(eng, |e, buf, cap| {
            (dll.get_backup_report_json)(e, txn, buf, cap)
        });
        assert_eq!(report.get("job_id").and_then(|x| x.as_str()), Some("docs"));

        (dll.close)(eng);
        assert_eq!((dll.delete_job)(repo.as_ptr(), job_id.as_ptr()), 0);
    }
}

#[test]
fn job_queue_and_observability_v24_roundtrip() {
    let dll = WorkbenchDll::load().expect("dll");
    let fx = TempFixture::new();
    unsafe {
        let repo = fx.repo_str();
        call_json_plain(|buf, cap| (dll.init_repo_json)(repo.as_ptr(), 0, buf, cap));

        let src = fx.source_str();
        let job_json = format!(
            r#"{{"id":"docs","name":"Docs","source_path":"{}","retention_tag":0,"immutability_days":0,"worm":false,"exclude_globs":[]}}"#,
            src.to_string_lossy().replace('\\', "\\\\")
        );
        let job = cstr(&job_json);
        assert_eq!((dll.upsert_job_json)(repo.as_ptr(), job.as_ptr()), 0);

        let enq_payload = cstr(r#"{"job_id":"docs","incremental":false,"flags":0}"#);
        let enq = call_repo_json(&dll, || {
            (dll.enqueue_job_json)(repo.as_ptr(), enq_payload.as_ptr())
        });
        assert_eq!(enq.get("ok").and_then(|x| x.as_bool()), Some(true));

        let status = call_repo_json(&dll, || (dll.job_queue_status_json)(repo.as_ptr()));
        assert_eq!(
            status.get("pending_count").and_then(|x| x.as_u64()),
            Some(1)
        );

        let mut err: i32 = 0;
        let eng = (dll.open_ex)(repo.as_ptr(), &mut err);
        assert!(!eng.is_null(), "open failed status={err}");

        let run_opts = cstr(r#"{"drain":false}"#);
        let run = call_eng_free_json(&dll, || {
            (dll.run_job_queue_json)(eng, run_opts.as_ptr())
        });
        assert_eq!(run.get("ok").and_then(|x| x.as_bool()), Some(true));

        let reach = call_eng_free_json(&dll, || (dll.snapshot_reachability_json)(eng, 0));
        assert_eq!(reach.get("reachable").and_then(|x| x.as_bool()), Some(true));

        let rpo = call_eng_free_json(&dll, || (dll.rpo_summary_json)(eng));
        assert_eq!(rpo.get("ok").and_then(|x| x.as_bool()), Some(true));
        assert!(rpo.get("snapshot_count").and_then(|x| x.as_u64()).unwrap_or(0) >= 1);

        let explain = call_eng_free_json(&dll, || (dll.orphan_explain_json)(eng, 32));
        assert_eq!(explain.get("ok").and_then(|x| x.as_bool()), Some(true));

        let audit_payload = cstr(r#"{"op":"gc_orphans","dry_run":false,"orphan_count":0}"#);
        let append = call_eng_free_json(&dll, || {
            (dll.append_ops_audit_json)(eng, audit_payload.as_ptr())
        });
        assert_eq!(append.get("ok").and_then(|x| x.as_bool()), Some(true));

        let ops = call_eng_free_json(&dll, || (dll.list_ops_audit_json)(eng));
        assert_eq!(ops.get("ok").and_then(|x| x.as_bool()), Some(true));
        assert!(ops.get("entries").and_then(|x| x.as_array()).map(|a| !a.is_empty()) == Some(true));

        (dll.close)(eng);
    }
}

#[test]
fn suggest_exclude_filters_roundtrip() {
    let dll = WorkbenchDll::load().expect("dll");
    let fx = TempFixture::new();
    std::fs::create_dir_all(fx.source.join(".git")).expect("git dir");
    std::fs::write(fx.source.join(".git/HEAD"), "ref").expect("git head");
    std::fs::create_dir_all(fx.source.join("node_modules/pkg")).expect("nm");
    std::fs::write(fx.source.join("node_modules/pkg/index.js"), "x").expect("nm file");

    let payload = serde_json::json!({
        "source_path": fx.source.to_string_lossy(),
    })
    .to_string();
    let opt = cstr(&payload);
    let json_ptr = unsafe { (dll.suggest_exclude_filters_json)(opt.as_ptr()) };
    assert!(!json_ptr.is_null());
    let text = unsafe { std::ffi::CStr::from_ptr(json_ptr).to_string_lossy().into_owned() };
    unsafe { (dll.free_string)(json_ptr) };
    let v: serde_json::Value = serde_json::from_str(&text).expect("json");
    assert_eq!(v.get("ok").and_then(|x| x.as_bool()), Some(true));
    let items = v.get("items").and_then(|x| x.as_array()).cloned().unwrap_or_default();
    assert!(!items.is_empty());
}
