use crate::ebbackup_ffi::{self, cstr, EnginePtr, SESSION};
use crate::profile_store;
use crate::sync_runner;
use crate::task_progress;
use serde::{Deserialize, Serialize};
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

#[derive(Serialize, Deserialize, Clone)]
pub struct SnapshotDto {
    pub txn_id: u64,
    pub created_at_unix: i64,
    pub manifest_crc32: u32,
    pub file_count: u32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub job_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub retention_tag: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub immutable_until_unix: Option<i64>,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct BackupJobDto {
    pub id: String,
    pub name: String,
    pub source_path: String,
    #[serde(default)]
    pub retention_tag: u32,
    #[serde(default)]
    pub immutability_days: i32,
    #[serde(default)]
    pub worm: bool,
    #[serde(default)]
    pub exclude_globs: Vec<String>,
    #[serde(default)]
    pub exclude_paths: Vec<String>,
  #[serde(default)]
  pub plugins: Vec<String>,
  #[serde(default, skip_serializing_if = "String::is_empty")]
  pub window_start: String,
  #[serde(default, skip_serializing_if = "String::is_empty")]
  pub window_end: String,
  #[serde(default, skip_serializing_if = "is_default_grace")]
  pub deadline_grace_seconds: i32,
  #[serde(default, skip_serializing_if = "std::ops::Not::not")]
  pub durability_adaptive: bool,
}

fn is_default_grace(v: &i32) -> bool {
    *v == 300 || *v == 0
}

#[derive(Serialize)]
pub struct ManifestFileDto {
    pub relative_path: String,
    pub size: u64,
    pub file_type: String,
    pub mtime_unix: i64,
    pub chunk_count: u32,
}

#[derive(Serialize)]
pub struct ManifestFilesDto {
    pub txn_id: u64,
    pub count: u64,
    pub total_bytes: u64,
    pub files: Vec<ManifestFileDto>,
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

fn session_repo_path() -> Result<String, String> {
    let guard = SESSION.lock();
    guard
        .as_ref()
        .map(|s| s.path.clone())
        .ok_or_else(|| "repository not open".to_string())
}

fn parse_snapshot_dto(s: &serde_json::Value) -> SnapshotDto {
    SnapshotDto {
        txn_id: s.get("txn_id").and_then(|x| x.as_u64()).unwrap_or(0),
        created_at_unix: s.get("created_at_unix").and_then(|x| x.as_i64()).unwrap_or(0),
        manifest_crc32: s.get("manifest_crc32").and_then(|x| x.as_u64()).unwrap_or(0) as u32,
        file_count: s.get("file_count").and_then(|x| x.as_u64()).unwrap_or(0) as u32,
        job_id: s
            .get("job_id")
            .and_then(|x| x.as_str())
            .filter(|x| !x.is_empty())
            .map(|x| x.to_string()),
        retention_tag: s.get("retention_tag").and_then(|x| x.as_u64()).map(|x| x as u32),
        immutable_until_unix: s.get("immutable_until_unix").and_then(|x| x.as_i64()),
    }
}

fn parse_backup_job(j: &serde_json::Value) -> BackupJobDto {
    BackupJobDto {
        id: j.get("id").and_then(|x| x.as_str()).unwrap_or("").to_string(),
        name: j.get("name").and_then(|x| x.as_str()).unwrap_or("").to_string(),
        source_path: j
            .get("source_path")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string(),
        retention_tag: j.get("retention_tag").and_then(|x| x.as_u64()).unwrap_or(0) as u32,
        immutability_days: j
            .get("immutability_days")
            .and_then(|x| x.as_i64())
            .unwrap_or(0) as i32,
        worm: j.get("worm").and_then(|x| x.as_bool()).unwrap_or(false),
        exclude_globs: j
            .get("exclude_globs")
            .and_then(|x| x.as_array())
            .map(|arr| {
                arr.iter()
                    .filter_map(|x| x.as_str().map(|s| s.to_string()))
                    .collect()
            })
            .unwrap_or_default(),
        exclude_paths: j
            .get("exclude_paths")
            .and_then(|x| x.as_array())
            .map(|arr| {
                arr.iter()
                    .filter_map(|x| x.as_str().map(|s| s.to_string()))
                    .collect()
            })
            .unwrap_or_default(),
        plugins: j
            .get("plugins")
            .and_then(|x| x.as_array())
            .map(|arr| {
                arr.iter()
                    .filter_map(|x| x.as_str().map(|s| s.to_string()))
                    .collect()
            })
            .unwrap_or_default(),
        window_start: j
            .get("window_start")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string(),
        window_end: j
            .get("window_end")
            .and_then(|x| x.as_str())
            .unwrap_or("")
            .to_string(),
        deadline_grace_seconds: j
            .get("deadline_grace_seconds")
            .and_then(|x| x.as_i64())
            .unwrap_or(300) as i32,
        durability_adaptive: j
            .get("durability_adaptive")
            .and_then(|x| x.as_bool())
            .unwrap_or(false),
    }
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

fn try_append_ops_audit(payload: &serde_json::Value) {
    let Ok(json) = serde_json::to_string(payload) else {
        return;
    };
    let _ = with_eng(|api, eng| {
        let s = cstr(&json)?;
        unsafe {
            ebbackup_ffi::call_free_json(|| (api.append_ops_audit_json)(eng, s.as_ptr()))?;
        }
        Ok(())
    });
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
pub async fn open_repo(app: tauri::AppHandle, path: String) -> Result<(), String> {
    let profile_id = profile_store::get_active_profile(app.clone())?;
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
                profile_id: profile_id.clone(),
                path: path.clone(),
                eng: EnginePtr(eng),
            });
        }
        let _ = profile_store::set_profile_last_repo(app, Some(profile_id), path);
        Ok(())
    })
    .await
    .map_err(|e| format!("open join: {e}"))?
}

#[tauri::command]
pub async fn switch_profile(app: tauri::AppHandle, profile_id: String) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        close_repo_inner()?;
        profile_store::set_active_profile(app.clone(), profile_id.clone())?;
        let state = profile_store::get_profile_state(app.clone(), Some(profile_id.clone()));
        let settings = crate::ui_settings::get_ui_settings(app.clone(), Some(profile_id.clone()));
        Ok(serde_json::json!({
            "ok": true,
            "profileId": profile_id,
            "state": state,
            "settings": settings,
        }))
    })
    .await
    .map_err(|e| format!("switch profile join: {e}"))?
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
            .map(|s| parse_snapshot_dto(&s))
            .collect())
    })
    .await
    .map_err(|e| format!("snapshots join: {e}"))?
}

#[tauri::command]
pub async fn list_manifest_files(txn_id: Option<u64>) -> Result<ManifestFilesDto, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let txn = txn_id.unwrap_or(0);
        let v = with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.list_manifest_files_json)(eng, txn, buf as *mut _, cap)
            })
        })?;
        let files_arr = v
            .get("files")
            .and_then(|x| x.as_array())
            .cloned()
            .unwrap_or_default();
        let files = files_arr
            .into_iter()
            .map(|f| ManifestFileDto {
                relative_path: f
                    .get("relative_path")
                    .and_then(|x| x.as_str())
                    .unwrap_or("")
                    .to_string(),
                size: f.get("size").and_then(|x| x.as_u64()).unwrap_or(0),
                file_type: f
                    .get("file_type")
                    .and_then(|x| x.as_str())
                    .unwrap_or("file")
                    .to_string(),
                mtime_unix: f.get("mtime_unix").and_then(|x| x.as_i64()).unwrap_or(0),
                chunk_count: f
                    .get("chunk_count")
                    .and_then(|x| x.as_u64())
                    .unwrap_or(0) as u32,
            })
            .collect::<Vec<ManifestFileDto>>();
        Ok(ManifestFilesDto {
            txn_id: v.get("txn_id").and_then(|x| x.as_u64()).unwrap_or(txn),
            count: v.get("count").and_then(|x| x.as_u64()).unwrap_or(files.len() as u64),
            total_bytes: v.get("total_bytes").and_then(|x| x.as_u64()).unwrap_or(0),
            files,
        })
    })
    .await
    .map_err(|e| format!("manifest files join: {e}"))?
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

fn attach_acceptance_report(
    api: &ebbackup_ffi::EbbackupApi,
    eng: *mut ebbackup_ffi::EbBackupEngine,
    base: serde_json::Value,
) -> Result<serde_json::Value, String> {
    if let Ok(report) = ebbackup_ffi::call_json(|buf, cap| unsafe {
        (api.export_restore_report_json)(eng, buf as *mut _, cap)
    }) {
        return Ok(serde_json::json!({
            "ok": true,
            "acceptance_report": report
        }));
    }
    Ok(base)
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
                let restore_result = ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.run_restore_json)(eng, dest.as_ptr(), txn, fl, buf as *mut _, cap)
                })?;
                attach_acceptance_report(api, eng, restore_result)
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("restore join: {e}"))?
}

#[tauri::command]
pub async fn run_restore_ex(
    app: AppHandle,
    dest_path: String,
    txn_id: Option<u64>,
    flags: Option<u32>,
    filter_json: Option<String>,
    remap_json: Option<String>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "restore");
        let result = (|| {
            let dest = cstr(&dest_path)?;
            let txn = txn_id.unwrap_or(0);
            let fl = flags.unwrap_or(0);
            let filter = filter_json.as_deref().map(cstr).transpose()?;
            let remap = remap_json.as_deref().map(cstr).transpose()?;
            with_eng(|api, eng| {
                let restore_result = ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.run_restore_ex_json)(
                        eng,
                        dest.as_ptr(),
                        txn,
                        fl,
                        filter.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                        remap.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                        buf as *mut _,
                        cap,
                    )
                })?;
                attach_acceptance_report(api, eng, restore_result)
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("restore join: {e}"))?
}

#[tauri::command]
pub async fn preview_restore(
    txn_id: Option<u64>,
    filter_json: Option<String>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let txn = txn_id.unwrap_or(0);
        let filter = filter_json.as_deref().map(cstr).transpose()?;
        with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.preview_restore_json)(
                    eng,
                    txn,
                    filter.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                    buf as *mut _,
                    cap,
                )
            })
        })
    })
    .await
    .map_err(|e| format!("preview join: {e}"))?
}

#[tauri::command]
pub async fn preview_in_place(
    target_root: String,
    txn_id: Option<u64>,
    filter_json: Option<String>,
    in_place_options_json: Option<String>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let txn = txn_id.unwrap_or(0);
        let target = cstr(&target_root)?;
        let filter = filter_json.as_deref().map(cstr).transpose()?;
        let options = in_place_options_json.as_deref().map(cstr).transpose()?;
        with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.preview_in_place_json)(
                    eng,
                    txn,
                    target.as_ptr(),
                    filter.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                    options.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                    buf as *mut _,
                    cap,
                )
            })
        })
    })
    .await
    .map_err(|e| format!("preview_in_place join: {e}"))?
}

#[tauri::command]
pub async fn apply_in_place(
    app: AppHandle,
    target_root: String,
    txn_id: Option<u64>,
    conflict_policy: Option<String>,
    orphan_policy: Option<String>,
    filter_json: Option<String>,
    in_place_options_json: Option<String>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "apply_in_place");
        let result = (|| {
            let txn = txn_id.unwrap_or(0);
            let target = cstr(&target_root)?;
            let policy = conflict_policy.as_deref().map(cstr).transpose()?;
            let orphan = orphan_policy.as_deref().map(cstr).transpose()?;
            let filter = filter_json.as_deref().map(cstr).transpose()?;
            let options = in_place_options_json.as_deref().map(cstr).transpose()?;
            with_eng(|api, eng| {
                ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.apply_in_place_json)(
                        eng,
                        txn,
                        target.as_ptr(),
                        policy.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                        orphan.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                        filter.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                        options.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                        buf as *mut _,
                        cap,
                    )
                })
            })
        })();
        if result.is_ok() {
            if let Ok(ref val) = result {
                let dry = val.get("dry_run").and_then(|x| x.as_bool()).unwrap_or(false);
                if !dry {
                    let mut payload = val.clone();
                    if let Some(obj) = payload.as_object_mut() {
                        obj.insert("op".to_string(), serde_json::json!("in_place_apply"));
                        obj.insert("dry_run".to_string(), serde_json::json!(false));
                        obj.insert("txn_id".to_string(), serde_json::json!(txn_id.unwrap_or(0)));
                    }
                    try_append_ops_audit(&payload);
                }
            }
        }
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("apply_in_place join: {e}"))?
}

#[tauri::command]
pub async fn export_delta(
    base_txn_id: u64,
    bundle_path: String,
    target_txn_id: Option<u64>,
    password: Option<String>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let repo = session_repo_path()?;
        let repo_c = cstr(&repo)?;
        let bundle = cstr(&bundle_path)?;
        let pwd = password.as_deref().map(cstr).transpose()?;
        let api = ebbackup_ffi::api()?;
        ebbackup_ffi::call_json(|buf, cap| unsafe {
            (api.export_delta_json)(
                repo_c.as_ptr(),
                bundle.as_ptr(),
                base_txn_id,
                target_txn_id.unwrap_or(0),
                pwd.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                buf as *mut _,
                cap,
            )
        })
    })
    .await
    .map_err(|e| format!("export_delta join: {e}"))?
}

#[tauri::command]
pub async fn import_delta(
    base_path: String,
    delta_path: String,
    out_repo_path: String,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let base = cstr(&base_path)?;
        let delta = cstr(&delta_path)?;
        let out = cstr(&out_repo_path)?;
        let api = ebbackup_ffi::api()?;
        ebbackup_ffi::call_json(|buf, cap| unsafe {
            (api.import_delta_json)(
                base.as_ptr(),
                delta.as_ptr(),
                out.as_ptr(),
                buf as *mut _,
                cap,
            )
        })
    })
    .await
    .map_err(|e| format!("import_delta join: {e}"))?
}

#[tauri::command]
pub async fn list_manifest_page(
    txn_id: Option<u64>,
    prefix: Option<String>,
    offset: Option<u64>,
    limit: Option<u64>,
) -> Result<ManifestFilesDto, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let txn = txn_id.unwrap_or(0);
        let off = offset.unwrap_or(0);
        let lim = limit.unwrap_or(200);
        let pfx = prefix.unwrap_or_default();
        let p = cstr(&pfx)?;
        let v = with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.list_manifest_page_json)(eng, txn, p.as_ptr(), off, lim, buf as *mut _, cap)
            })
        })?;
        let files_arr = v
            .get("files")
            .and_then(|x| x.as_array())
            .cloned()
            .unwrap_or_default();
        let files = files_arr
            .into_iter()
            .map(|f| ManifestFileDto {
                relative_path: f
                    .get("relative_path")
                    .and_then(|x| x.as_str())
                    .unwrap_or("")
                    .to_string(),
                size: f.get("size").and_then(|x| x.as_u64()).unwrap_or(0),
                file_type: f
                    .get("file_type")
                    .and_then(|x| x.as_str())
                    .unwrap_or("file")
                    .to_string(),
                mtime_unix: f.get("mtime_unix").and_then(|x| x.as_i64()).unwrap_or(0),
                chunk_count: f
                    .get("chunk_count")
                    .and_then(|x| x.as_u64())
                    .unwrap_or(0) as u32,
            })
            .collect::<Vec<ManifestFileDto>>();
        Ok(ManifestFilesDto {
            txn_id: v.get("txn_id").and_then(|x| x.as_u64()).unwrap_or(txn),
            count: v.get("total").and_then(|x| x.as_u64()).unwrap_or(files.len() as u64),
            total_bytes: files.iter().map(|f| f.size).sum(),
            files,
        })
    })
    .await
    .map_err(|e| format!("manifest page join: {e}"))?
}

#[tauri::command]
pub async fn run_maintenance_wizard(
    app: AppHandle,
    options_json: Option<String>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "compact");
        let result = (|| {
            let opts = options_json.as_deref().map(cstr).transpose()?;
            with_eng(|api, eng| {
                ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.run_maintenance_wizard_json)(
                        eng,
                        opts.as_ref().map_or(std::ptr::null(), |c| c.as_ptr()),
                        buf as *mut _,
                        cap,
                    )
                })
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("maintenance wizard join: {e}"))?
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
        if result.is_ok() && !dry_run.unwrap_or(false) {
            if let Ok(ref val) = result {
                let mut payload = val.clone();
                if let Some(obj) = payload.as_object_mut() {
                    obj.insert("op".to_string(), serde_json::json!("compact"));
                    obj.insert("dry_run".to_string(), serde_json::json!(false));
                }
                try_append_ops_audit(&payload);
            }
        }
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
        if result.is_ok() && !dry_run.unwrap_or(false) {
            if let Ok(ref val) = result {
                let mut payload = val.clone();
                if let Some(obj) = payload.as_object_mut() {
                    obj.insert("op".to_string(), serde_json::json!("gc_orphans"));
                    obj.insert("dry_run".to_string(), serde_json::json!(false));
                }
                try_append_ops_audit(&payload);
            }
        }
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
        if result.is_ok() && !dry_run.unwrap_or(false) {
            if let Ok(ref val) = result {
                let mut payload = val.clone();
                if let Some(obj) = payload.as_object_mut() {
                    obj.insert("op".to_string(), serde_json::json!("prune"));
                    obj.insert("dry_run".to_string(), serde_json::json!(false));
                    obj.insert(
                        "retention_tiers".to_string(),
                        serde_json::json!(retention_tiers),
                    );
                    obj.insert("retain_min".to_string(), serde_json::json!(retain_min.unwrap_or(3)));
                }
                try_append_ops_audit(&payload);
            }
        }
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
pub async fn set_audit_key(audit_key: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        with_eng(|api, eng| {
            let key = cstr(&audit_key)?;
            unsafe {
                (api.set_audit_key)(eng, key.as_ptr());
            }
            Ok(())
        })
    })
    .await
    .map_err(|e| format!("audit_key join: {e}"))?
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
pub async fn set_filter_json(filter_json: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        with_eng(|api, eng| {
            let fj = cstr(&filter_json)?;
            let st = unsafe { (api.set_filter_json)(eng, fj.as_ptr()) };
            if st != 0 {
                return Err(format!("set_filter_json failed (status={st})"));
            }
            Ok(())
        })
    })
    .await
    .map_err(|e| format!("set filter join: {e}"))?
}

#[tauri::command]
pub async fn suggest_exclude_filters(
    source_path: String,
    max_depth: Option<i32>,
    include_ide_dirs: Option<bool>,
    existing_json: Option<String>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let api = ebbackup_ffi::api()?;
        let mut payload = serde_json::json!({
            "source_path": source_path,
            "max_depth": max_depth.unwrap_or(4),
            "include_ide_dirs": include_ide_dirs.unwrap_or(false),
        });
        if let Some(existing) = existing_json {
            let ex: serde_json::Value =
                serde_json::from_str(&existing).map_err(|e| e.to_string())?;
            payload["existing"] = ex;
        }
        let body = serde_json::to_string(&payload).map_err(|e| e.to_string())?;
        let opt = cstr(&body)?;
        let json_ptr = unsafe { (api.suggest_exclude_filters_json)(opt.as_ptr()) };
        if json_ptr.is_null() {
            return Err("suggest_exclude_filters returned null".into());
        }
        let text = unsafe {
            std::ffi::CStr::from_ptr(json_ptr).to_string_lossy().into_owned()
        };
        unsafe { (api.free_string)(json_ptr) };
        serde_json::from_str(&text).map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| format!("suggest excludes join: {e}"))?
}

#[tauri::command]
pub async fn sync_runtime_binaries(build_dir: Option<String>) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let manifest_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap_or_default());
        let build = build_dir
            .map(PathBuf::from)
            .unwrap_or_else(|| manifest_dir.join("../../build/engine_cpp/Release"));
        let sync_build = manifest_dir.join("../../build/sync_cpp/Release");
        let dest = manifest_dir.join("bin");
        std::fs::create_dir_all(&dest).map_err(|e| e.to_string())?;
        let leaf = "ebbackup_workbench.dll";
        let src = build.join(leaf);
        if !src.is_file() {
            return Err(format!("missing {leaf} under {}", build.display()));
        }
        std::fs::copy(&src, dest.join(leaf)).map_err(|e| e.to_string())?;
        let sync_leaf = "eb-sync.exe";
        let sync_src = sync_build.join(sync_leaf);
        if sync_src.is_file() {
            std::fs::copy(&sync_src, dest.join(sync_leaf)).map_err(|e| e.to_string())?;
        }
        Ok(serde_json::json!({"ok": true, "message": format!("copied {} and {}", src.display(), sync_src.display())}))
    })
    .await
    .map_err(|e| format!("sync join: {e}"))?
}

#[tauri::command]
pub async fn query_path_history(
    path: String,
    offset: Option<u64>,
    limit: Option<u64>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let p = cstr(&path)?;
        let off = offset.unwrap_or(0);
        let lim = limit.unwrap_or(100);
        with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.query_path_history_json)(eng, p.as_ptr(), off, lim, buf as *mut _, cap)
            })
        })
    })
    .await
    .map_err(|e| format!("path history join: {e}"))?
}

#[tauri::command]
pub async fn diff_snapshots(txn_a: u64, txn_b: u64) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.diff_snapshots_json)(eng, txn_a, txn_b, buf as *mut _, cap)
            })
        })
    })
    .await
    .map_err(|e| format!("diff join: {e}"))?
}

#[tauri::command]
pub async fn build_path_index(full_rebuild: Option<bool>) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let rebuild = if full_rebuild.unwrap_or(false) { 1 } else { 0 };
        with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.build_path_index_json)(eng, rebuild, buf as *mut _, cap)
            })
        })
    })
    .await
    .map_err(|e| format!("path index join: {e}"))?
}

#[tauri::command]
pub async fn export_restore_report() -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(|| {
        with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.export_restore_report_json)(eng, buf as *mut _, cap)
            })
        })
    })
    .await
    .map_err(|e| format!("restore report join: {e}"))?
}

#[tauri::command]
pub async fn get_backup_report(txn_id: u64) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.get_backup_report_json)(eng, txn_id, buf as *mut _, cap)
            })
        })
    })
    .await
    .map_err(|e| format!("backup report join: {e}"))?
}

#[tauri::command]
pub async fn set_backup_hooks(
    pre_cmd: String,
    post_cmd: String,
    plugins: Option<Vec<String>>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let payload = serde_json::json!({
            "pre_backup_cmd": pre_cmd,
            "post_backup_cmd": post_cmd,
            "plugins": plugins.unwrap_or_default(),
        })
        .to_string();
        let json = cstr(&payload)?;
        with_eng(|api, eng| {
            ebbackup_ffi::call_json(|buf, cap| unsafe {
                (api.set_backup_hooks_json)(eng, json.as_ptr(), buf as *mut _, cap)
            })
        })
    })
    .await
    .map_err(|e| format!("set hooks join: {e}"))?
}

#[tauri::command]
pub async fn list_jobs() -> Result<Vec<BackupJobDto>, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let path = session_repo_path()?;
        let v = ebbackup_ffi::call_jobs_json(&path)?;
        let jobs = v
            .get("jobs")
            .and_then(|x| x.as_array())
            .cloned()
            .unwrap_or_default();
        Ok(jobs.iter().map(parse_backup_job).collect())
    })
    .await
    .map_err(|e| format!("list jobs join: {e}"))?
}

#[tauri::command]
pub async fn upsert_job(job: BackupJobDto) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let path = session_repo_path()?;
        let api = ebbackup_ffi::api()?;
        let payload = serde_json::to_string(&job).map_err(|e| e.to_string())?;
        let json = cstr(&payload)?;
        let repo = cstr(&path)?;
        let st = unsafe { (api.upsert_job_json)(repo.as_ptr(), json.as_ptr()) };
        if st != 0 {
            return Err(format!("upsert_job failed (status={st})"));
        }
        Ok(())
    })
    .await
    .map_err(|e| format!("upsert job join: {e}"))?
}

#[tauri::command]
pub async fn delete_job(job_id: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let path = session_repo_path()?;
        let api = ebbackup_ffi::api()?;
        let repo = cstr(&path)?;
        let id = cstr(&job_id)?;
        let st = unsafe { (api.delete_job)(repo.as_ptr(), id.as_ptr()) };
        if st != 0 {
            return Err(format!("delete_job failed (status={st})"));
        }
        Ok(())
    })
    .await
    .map_err(|e| format!("delete job join: {e}"))?
}

#[tauri::command]
pub async fn run_job(
    app: AppHandle,
    job_id: String,
    incremental: Option<bool>,
    flags: Option<u32>,
) -> Result<BackupStatsDto, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "backup");
        let result = (|| {
            let id = cstr(&job_id)?;
            let inc = if incremental.unwrap_or(true) { 1 } else { 0 };
            let fl = flags.unwrap_or(0);
            with_eng(|api, eng| {
                unsafe { task_progress::attach_progress(api.set_progress, eng) };
                let st = unsafe { (api.run_job)(eng, id.as_ptr(), inc, fl) };
                unsafe { task_progress::detach_progress(api.set_progress, eng) };
                if st != 0 {
                    let err = ebbackup_ffi::call_json(|buf, cap| unsafe {
                        (api.last_error_json)(eng, buf as *mut _, cap)
                    })
                    .ok()
                    .and_then(|v| {
                        v.get("error")
                            .and_then(|x| x.as_str())
                            .map(|s| s.to_string())
                    })
                    .unwrap_or_else(|| format!("run_job failed (status={st})"));
                    return Err(err);
                }
                let v = ebbackup_ffi::call_json(|buf, cap| unsafe {
                    (api.get_stats_json)(eng, buf as *mut _, cap)
                })?;
                let s = v.get("stats").cloned().unwrap_or(v);
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
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("run job join: {e}"))?
}

#[tauri::command]
pub async fn enqueue_job(
    job_id: String,
    incremental: Option<bool>,
    flags: Option<u32>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let path = session_repo_path()?;
        let api = ebbackup_ffi::api()?;
        let payload = serde_json::json!({
            "job_id": job_id,
            "incremental": incremental.unwrap_or(false),
            "flags": flags.unwrap_or(0),
        })
        .to_string();
        let repo = cstr(&path)?;
        let json = cstr(&payload)?;
        unsafe {
            ebbackup_ffi::call_free_json(|| {
                (api.enqueue_job_json)(repo.as_ptr(), json.as_ptr())
            })
        }
    })
    .await
    .map_err(|e| format!("enqueue job join: {e}"))?
}

#[tauri::command]
pub async fn job_queue_status() -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let path = session_repo_path()?;
        let api = ebbackup_ffi::api()?;
        let repo = cstr(&path)?;
        unsafe {
            ebbackup_ffi::call_free_json(|| (api.job_queue_status_json)(repo.as_ptr()))
        }
    })
    .await
    .map_err(|e| format!("queue status join: {e}"))?
}

#[tauri::command]
pub async fn run_job_queue(
    app: AppHandle,
    drain: Option<bool>,
    flags: Option<u32>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        task_progress::begin_progress(app.clone(), "backup");
        let result = (|| {
            let payload = serde_json::json!({
                "drain": drain.unwrap_or(false),
                "flags": flags.unwrap_or(0),
            })
            .to_string();
            let json = cstr(&payload)?;
            with_eng(|api, eng| unsafe {
                ebbackup_ffi::call_free_json(|| {
                    (api.run_job_queue_json)(eng, json.as_ptr())
                })
            })
        })();
        task_progress::end_progress(result.is_ok());
        result
    })
    .await
    .map_err(|e| format!("run queue join: {e}"))?
}

#[tauri::command]
pub async fn snapshot_reachability(txn_id: u64) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        with_eng(|api, eng| unsafe {
            ebbackup_ffi::call_free_json(|| {
                (api.snapshot_reachability_json)(eng, txn_id)
            })
        })
    })
    .await
    .map_err(|e| format!("reachability join: {e}"))?
}

#[tauri::command]
pub async fn rpo_summary() -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(|| {
        with_eng(|api, eng| unsafe {
            ebbackup_ffi::call_free_json(|| (api.rpo_summary_json)(eng))
        })
    })
    .await
    .map_err(|e| format!("rpo summary join: {e}"))?
}

#[tauri::command]
pub async fn orphan_explain(sample_limit: Option<u64>) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let limit = sample_limit.unwrap_or(64);
        with_eng(|api, eng| unsafe {
            ebbackup_ffi::call_free_json(|| (api.orphan_explain_json)(eng, limit))
        })
    })
    .await
    .map_err(|e| format!("orphan explain join: {e}"))?
}

#[tauri::command]
pub async fn list_ops_audit() -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(|| {
        with_eng(|api, eng| unsafe {
            ebbackup_ffi::call_free_json(|| (api.list_ops_audit_json)(eng))
        })
    })
    .await
    .map_err(|e| format!("list ops audit join: {e}"))?
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

#[tauri::command]
pub async fn sync_status() -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let repo = session_repo_path()?;
        let out = sync_runner::run_eb_sync(&["status", "--repo", &repo])?;
        serde_json::from_str(&out).map_err(|e| format!("sync status json: {e}"))
    })
    .await
    .map_err(|e| format!("sync_status join: {e}"))?
}

#[tauri::command]
pub async fn sync_push(once: Option<bool>) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let repo = session_repo_path()?;
        let mut args = vec!["push", "--repo", repo.as_str()];
        let once_flag = if once.unwrap_or(true) { "--once" } else { "--drain" };
        args.push(once_flag);
        let out = sync_runner::run_eb_sync(&args)?;
        Ok(serde_json::json!({
            "ok": true,
            "message": out.trim(),
        }))
    })
    .await
    .map_err(|e| format!("sync_push join: {e}"))?
}

#[tauri::command]
pub async fn sync_ferry_export(
    out_dir: String,
    auto_base: Option<bool>,
    base_txn_id: Option<u64>,
    target_txn_id: Option<u64>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let repo = session_repo_path()?;
        let mut owned: Vec<String> = vec![
            "ferry".into(),
            "export".into(),
            "--repo".into(),
            repo,
            "--out-dir".into(),
            out_dir,
        ];
        if auto_base.unwrap_or(true) {
            owned.push("--auto-base".into());
        }
        if let Some(base) = base_txn_id {
            if base > 0 {
                owned.push("--base-at".into());
                owned.push(base.to_string());
            }
        }
        if let Some(target) = target_txn_id {
            if target > 0 {
                owned.push("--target-at".into());
                owned.push(target.to_string());
            }
        }
        let args: Vec<&str> = owned.iter().map(|s| s.as_str()).collect();
        let out = sync_runner::run_eb_sync(&args)?;
        Ok(serde_json::json!({
            "ok": true,
            "message": out.trim(),
        }))
    })
    .await
    .map_err(|e| format!("sync_ferry_export join: {e}"))?
}

#[tauri::command]
pub async fn sync_init_local(mirror_path: String) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let repo = session_repo_path()?;
        let out = sync_runner::run_eb_sync(&[
            "init",
            "--repo",
            repo.as_str(),
            "--local-mirror",
            mirror_path.as_str(),
        ])?;
        Ok(serde_json::json!({
            "ok": true,
            "message": out.trim(),
        }))
    })
    .await
    .map_err(|e| format!("sync_init_local join: {e}"))?
}

#[tauri::command]
pub async fn sync_init_ferry() -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let repo = session_repo_path()?;
        let out = sync_runner::run_eb_sync(&["init", "--repo", repo.as_str(), "--mode", "ferry"])?;
        Ok(serde_json::json!({
            "ok": true,
            "message": out.trim(),
        }))
    })
    .await
    .map_err(|e| format!("sync_init_ferry join: {e}"))?
}

#[tauri::command]
pub async fn sync_init_pds(
    domain_id: String,
    credentials_path: String,
    root_prefix: Option<String>,
) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let repo = session_repo_path()?;
        let mut args = vec![
            "init".to_string(),
            "--repo".to_string(),
            repo.clone(),
            "--pds".to_string(),
            "--domain".to_string(),
            domain_id,
            "--credentials".to_string(),
            credentials_path,
        ];
        if let Some(prefix) = root_prefix {
            if !prefix.trim().is_empty() {
                args.push("--pds-prefix".to_string());
                args.push(prefix);
            }
        }
        let arg_refs: Vec<&str> = args.iter().map(|s| s.as_str()).collect();
        let out = sync_runner::run_eb_sync(&arg_refs)?;
        Ok(serde_json::json!({
            "ok": true,
            "message": out.trim(),
        }))
    })
    .await
    .map_err(|e| format!("sync_init_pds join: {e}"))?
}

#[tauri::command]
pub async fn sync_pds_auth_url() -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let repo = session_repo_path()?;
        let out = sync_runner::run_eb_sync(&["pds", "auth-url", "--repo", repo.as_str()])?;
        Ok(serde_json::json!({
            "ok": true,
            "url": out.trim(),
        }))
    })
    .await
    .map_err(|e| format!("sync_pds_auth_url join: {e}"))?
}

#[tauri::command]
pub async fn sync_pds_auth(code: String) -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let repo = session_repo_path()?;
        let out = sync_runner::run_eb_sync(&[
            "pds",
            "auth",
            "--repo",
            repo.as_str(),
            "--code",
            code.as_str(),
        ])?;
        Ok(serde_json::json!({
            "ok": true,
            "message": out.trim(),
        }))
    })
    .await
    .map_err(|e| format!("sync_pds_auth join: {e}"))?
}

#[tauri::command]
pub async fn sync_maintenance_check() -> Result<serde_json::Value, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let repo = session_repo_path()?;
        let out = sync_runner::run_eb_sync(&["maintenance-check", "--repo", &repo, "--json"])?;
        serde_json::from_str(&out).map_err(|e| format!("maintenance-check json: {e}"))
    })
    .await
    .map_err(|e| format!("sync_maintenance_check join: {e}"))?
}
