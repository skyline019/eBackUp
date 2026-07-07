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
type JsonPlainFn = unsafe extern "C" fn(*mut c_char, usize) -> c_int;

struct WorkbenchDll {
    _lib: Library,
    open_ex: OpenExFn,
    close: CloseFn,
    init_repo_json: JsonInitFn,
    repo_info_json: JsonEngFn,
    list_snapshots_json: JsonEngFn,
    run_backup_json: JsonBackupFn,
    verify_json: JsonVerifyFn,
    runtime_info_json: JsonPlainFn,
    get_stats_json: JsonEngFn,
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
                run_backup_json: *lib
                    .get(b"ebbackup_workbench_run_backup_json\0")
                    .map_err(|e| e.to_string())?,
                verify_json: *lib
                    .get(b"ebbackup_workbench_verify_json\0")
                    .map_err(|e| e.to_string())?,
                runtime_info_json: *lib
                    .get(b"ebbackup_workbench_runtime_info_json\0")
                    .map_err(|e| e.to_string())?,
                get_stats_json: *lib
                    .get(b"ebbackup_workbench_get_stats_json\0")
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
    assert!(v.get("abi_version").and_then(|x| x.as_u64()).unwrap_or(0) >= 12);
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
