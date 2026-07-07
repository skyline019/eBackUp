use crate::ebbackup_ffi::{self, cstr, EnginePtr, SESSION};
use crate::task_progress;
use serde::Serialize;
use std::path::PathBuf;
use tauri::AppHandle;

#[derive(Serialize)]
pub struct RepoInfoDto {
    pub open: bool,
    pub path: String,
    pub abi_version: u32,
    pub physical_bytes: u64,
    pub live_bytes: u64,
    pub orphan_bytes: u64,
    pub manifest_bytes: u64,
    pub unique_chunks: u64,
    pub tombstoned_chunks: u64,
    pub ampl_ratio: f64,
}

#[derive(Serialize)]
pub struct SnapshotDto {
    pub txn_id: u64,
    pub created_at_unix: i64,
    pub manifest_crc32: u32,
    pub file_count: u32,
}

#[derive(Serialize)]
pub struct RuntimeInfoDto {
    pub abi_version: u32,
    pub workbench: String,
}

#[derive(Serialize)]
pub struct BackupStatsDto {
    pub files_processed: u64,
    pub chunks_written: u64,
    pub chunks_reused: u64,
    pub chunks_reused_from_cfi: u64,
    pub bytes_processed: u64,
    pub orphan_chunks_hint: u64,
}

pub fn close_repo_inner() -> Result<(), String> {
    let mut guard = SESSION.lock();
    if let Some(sess) = guard.take() {
        let api = ebbackup_ffi::api()?;
        unsafe {
            if !sess.eng.0.is_null() {
                (api.close)(sess.eng.0);
            }
        }
    }
    Ok(())
}

fn with_eng<F, T>(f: F) -> Result<T, String>
where
    F: FnOnce(&ebbackup_ffi::EbbackupApi, *mut ebbackup_ffi::EbBackupEngine) -> Result<T, String>,
{
    let guard = SESSION.lock();
    let sess = guard.as_ref().ok_or_else(|| "repository not open".to_string())?;
    if sess.eng.0.is_null() {
        return Err("repository not open".to_string());
    }
    let api = ebbackup_ffi::api()?;
    f(api, sess.eng.0)
}

#[tauri::command]
pub async fn runtime_info() -> Result<RuntimeInfoDto, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let api = ebbackup_ffi::api()?;
        let v = ebbackup_ffi::call_json(|buf, cap| unsafe {
            (api.runtime_info_json)(buf as *mut _, cap)
        })?;
        Ok(RuntimeInfoDto {
            abi_version: v.get("abi_version").and_then(|x| x.as_u64()).unwrap_or(0) as u32,
            workbench: v
                .get("workbench")
                .and_then(|x| x.as_str())
                .unwrap_or("ebbackup")
                .to_string(),
        })
    })
    .await
    .map_err(|e| format!("runtime join: {e}"))?
}

#[tauri::command]
pub async fn last_error() -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(|| {
        with_eng(|api, eng| {
            let v = ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.last_error_json)(eng, buf as *mut _, cap)
            })?;
            Ok(v.get("error")
                .and_then(|x| x.as_str())
                .unwrap_or("")
                .to_string())
        })
    })
    .await
    .map_err(|e| format!("last_error join: {e}"))?
}

#[tauri::command]
pub async fn get_backup_stats() -> Result<BackupStatsDto, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let v = with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.get_stats_json)(eng, buf as *mut _, cap)
            })
        })?;
        let s = v.get("stats").cloned().unwrap_or(v);
        Ok(BackupStatsDto {
            files_processed: s.get("files_processed").and_then(|x| x.as_u64()).unwrap_or(0),
            chunks_written: s.get("chunks_written").and_then(|x| x.as_u64()).unwrap_or(0),
            chunks_reused: s.get("chunks_reused").and_then(|x| x.as_u64()).unwrap_or(0),
            chunks_reused_from_cfi: s.get("chunks_reused_from_cfi").and_then(|x| x.as_u64()).unwrap_or(0),
            bytes_processed: s.get("bytes_processed").and_then(|x| x.as_u64()).unwrap_or(0),
            orphan_chunks_hint: s.get("orphan_chunks_hint").and_then(|x| x.as_u64()).unwrap_or(0),
        })
    })
    .await
    .map_err(|e| format!("stats join: {e}"))?
}

#[tauri::command]
pub async fn init_repo(path: String, flags: Option<u32>) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let api = ebbackup_ffi::api()?;
        let p = cstr(&path)?;
        let fl = flags.unwrap_or(0);
        ebbackup_ffi::call_json(|buf, cap| unsafe {
            (api.init_repo_json)(p.as_ptr(), fl, buf as *mut _, cap)
        })
    })
    .await
    .map_err(|e| format!("init join: {e}"))?
}

#[tauri::command]
pub async fn open_repo(path: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        close_repo_inner()?;
        let api = ebbackup_ffi::api()?;
        let p = cstr(&path)?;
        unsafe {
            let mut err: i32 = 0;
            let eng = (api.open_ex)(p.as_ptr(), &mut err);
            if eng.is_null() {
                return Err(format!("open failed (status={err})"));
            }
            *SESSION.lock() = Some(ebbackup_ffi::Session {
                path: path.clone(),
                eng: EnginePtr(eng),
            });
        }
        Ok(())
    })
    .await
    .map_err(|e| format!("open join: {e}"))?
}

#[tauri::command]
pub async fn close_repo() -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(close_repo_inner)
        .await
        .map_err(|e| format!("close join: {e}"))?
}

#[tauri::command]
pub async fn repo_info() -> Result<RepoInfoDto, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let path = {
            let guard = SESSION.lock();
            guard
                .as_ref()
                .map(|s| s.path.clone())
                .ok_or_else(|| "repository not open".to_string())?
        };
        let v = with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.repo_info_json)(eng, buf as *mut _, cap)
            })
        })?;
        let rs = v.get("repo_stats").cloned().unwrap_or_default();
        Ok(RepoInfoDto {
            open: true,
            path,
            abi_version: v.get("abi_version").and_then(|x| x.as_u64()).unwrap_or(0) as u32,
            physical_bytes: rs.get("physical_bytes").and_then(|x| x.as_u64()).unwrap_or(0),
            live_bytes: rs.get("live_bytes").and_then(|x| x.as_u64()).unwrap_or(0),
            orphan_bytes: rs.get("orphan_bytes").and_then(|x| x.as_u64()).unwrap_or(0),
            manifest_bytes: rs.get("manifest_bytes").and_then(|x| x.as_u64()).unwrap_or(0),
            unique_chunks: rs.get("unique_chunks").and_then(|x| x.as_u64()).unwrap_or(0),
            tombstoned_chunks: rs.get("tombstoned_chunks").and_then(|x| x.as_u64()).unwrap_or(0),
            ampl_ratio: rs.get("ampl_ratio").and_then(|x| x.as_f64()).unwrap_or(0.0),
        })
    })
    .await
    .map_err(|e| format!("info join: {e}"))?
}

#[tauri::command]
pub async fn list_snapshots() -> Result<Vec<SnapshotDto>, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let v = with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.list_snapshots_json)(eng, buf as *mut _, cap)
            })
        })?;
        let snaps = v
            .get("snapshots")
            .and_then(|x| x.as_array())
            .cloned()
            .unwrap_or_default();
        Ok(snaps
            .into_iter()
            .map(|s| SnapshotDto {
                txn_id: s.get("txn_id").and_then(|x| x.as_u64()).unwrap_or(0),
                created_at_unix: s.get("created_at_unix").and_then(|x| x.as_i64()).unwrap_or(0),
                manifest_crc32: s.get("manifest_crc32").and_then(|x| x.as_u64()).unwrap_or(0) as u32,
                file_count: s.get("file_count").and_then(|x| x.as_u64()).unwrap_or(0) as u32,
            })
            .collect())
    })
    .await
    .map_err(|e| format!("snapshots join: {e}"))?
}

#[tauri::command]
pub async fn run_backup(
    app: AppHandle,
    source_path: String,
    incremental: Option<bool>,
    flags: Option<u32>,
) -> Result<BackupStatsDto, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "backup");
        let result = (|| {
            let src = cstr(&source_path)?;
            let inc = if incremental.unwrap_or(true) { 1 } else { 0 };
            let fl = flags.unwrap_or(0);
            let v = with_eng(|api, eng| {
                unsafe { task_progress::attach_progress(api.set_progress, eng) };
                let out = ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.run_backup_json)(eng, src.as_ptr(), inc, fl, buf as *mut _, cap)
                });
                unsafe { task_progress::detach_progress(api.set_progress, eng) };
                out
            })?;
            let s = v.get("stats").cloned().unwrap_or_default();
            Ok(BackupStatsDto {
                files_processed: s.get("files_processed").and_then(|x| x.as_u64()).unwrap_or(0),
                chunks_written: s.get("chunks_written").and_then(|x| x.as_u64()).unwrap_or(0),
                chunks_reused: s.get("chunks_reused").and_then(|x| x.as_u64()).unwrap_or(0),
                chunks_reused_from_cfi: s
                    .get("chunks_reused_from_cfi")
                    .and_then(|x| x.as_u64())
                    .unwrap_or(0),
                bytes_processed: s.get("bytes_processed").and_then(|x| x.as_u64()).unwrap_or(0),
                orphan_chunks_hint: s
                    .get("orphan_chunks_hint")
                    .and_then(|x| x.as_u64())
                    .unwrap_or(0),
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("backup join: {e}"))?
}

#[tauri::command]
pub async fn run_restore(
    app: AppHandle,
    dest_path: String,
    txn_id: Option<u64>,
    flags: Option<u32>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "restore");
        let result = (|| {
            let dest = cstr(&dest_path)?;
            let txn = txn_id.unwrap_or(0);
            let fl = flags.unwrap_or(0);
            with_eng(|api, eng| {
                ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.run_restore_json)(eng, dest.as_ptr(), txn, fl, buf as *mut _, cap)
                })
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("restore join: {e}"))?
}

#[tauri::command]
pub async fn verify_repo(
    app: AppHandle,
    txn_id: Option<u64>,
    flags: Option<u32>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "verify");
        let result = (|| {
            let txn = txn_id.unwrap_or(0);
            let fl = flags.unwrap_or(0);
            with_eng(|api, eng| {
                ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.verify_json)(eng, txn, fl, buf as *mut _, cap)
                })
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("verify join: {e}"))?
}

#[tauri::command]
pub async fn recover_repo(app: AppHandle) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "recover");
        let result = with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.recover_json)(eng, buf as *mut _, cap)
            })
        });
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("recover join: {e}"))?
}

#[tauri::command]
pub async fn compact_repo(app: AppHandle, dry_run: Option<bool>) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "compact");
        let result = (|| {
            let dr = if dry_run.unwrap_or(false) { 1 } else { 0 };
            with_eng(|api, eng| {
                ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.compact_json)(eng, dr, buf as *mut _, cap)
                })
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("compact join: {e}"))?
}

#[tauri::command]
pub async fn gc_orphans(app: AppHandle, dry_run: Option<bool>) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "gc");
        let result = (|| {
            let dr = if dry_run.unwrap_or(false) { 1 } else { 0 };
            with_eng(|api, eng| {
                ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.gc_orphans_json)(eng, dr, buf as *mut _, cap)
                })
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("gc join: {e}"))?
}

#[tauri::command]
pub async fn prune_snapshots(
    app: AppHandle,
    retention_tiers: String,
    retain_min: Option<i32>,
    dry_run: Option<bool>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "prune");
        let result = (|| {
            let tiers = cstr(&retention_tiers)?;
            let rm = retain_min.unwrap_or(3);
            let dr = if dry_run.unwrap_or(false) { 1 } else { 0 };
            with_eng(|api, eng| {
                ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.prune_snapshots_json)(eng, tiers.as_ptr(), rm, dr, buf as *mut _, cap)
                })
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("prune join: {e}"))?
}

#[tauri::command]
pub async fn set_password(password: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        with_eng(|api, eng| {
            let pw = cstr(&password)?;
            unsafe {
                (api.set_password)(eng, pw.as_ptr());
            }
            Ok(())
        })
    })
    .await
    .map_err(|e| format!("password join: {e}"))?
}

#[tauri::command]
pub async fn load_filter_file(path: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        with_eng(|api, eng| {
            let p = cstr(&path)?;
            unsafe {
                let rc = (api.load_filter_file)(eng, p.as_ptr());
                if rc != 0 {
                    return Err("load_filter_file failed".into());
                }
            }
            Ok(())
        })
    })
    .await
    .map_err(|e| format!("filter join: {e}"))?
}

#[tauri::command]
pub async fn sync_runtime_binaries(build_dir: Option<String>) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let manifest_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap_or_default());
        let build = build_dir
            .map(PathBuf::from)
            .unwrap_or_else(|| manifest_dir.join("../../build/engine_cpp/Release"));
        let dest = manifest_dir.join("bin");
        std::fs::create_dir_all(&dest).map_err(|e| e.to_string())?;
        let leaf = "ebbackup_workbench.dll";
        let src = build.join(leaf);
        if !src.is_file() {
            return Err(format!("missing {leaf} under {}", build.display()));
        }
        std::fs::copy(&src, dest.join(leaf)).map_err(|e| e.to_string())?;
        Ok(serde_json::json!({"ok": true, "message": format!("copied {}", src.display())}))
    })
    .await
    .map_err(|e| format!("sync join: {e}"))?
}

#[tauri::command]
pub async fn path_exists(path: String) -> Result<bool, String> {
    Ok(std::path::Path::new(&path).exists())
}

#[tauri::command]
pub async fn pick_directory(app: tauri::AppHandle) -> Result<Option<String>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        use tauri_plugin_dialog::DialogExt;
        let folder = app.dialog().file().blocking_pick_folder();
        Ok(folder.map(|p| p.to_string()))
    })
    .await
    .map_err(|e| format!("pick join: {e}"))?
}

#[tauri::command]
pub async fn pick_file(app: tauri::AppHandle) -> Result<Option<String>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        use tauri_plugin_dialog::DialogExt;
        let file = app.dialog().file().blocking_pick_file();
        Ok(file.map(|p| p.to_string()))
    })
    .await
    .map_err(|e| format!("pick file join: {e}"))?
}
