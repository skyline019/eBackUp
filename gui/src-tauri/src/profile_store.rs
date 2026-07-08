//! Profile registry + per-profile settings/state on disk.

use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};
use tauri::Manager;

use crate::ui_settings::UiSettings;

pub const DEFAULT_PROFILE_ID: &str = "default";

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProfileRecord {
    pub id: String,
    pub name: String,
    pub created_at_unix: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_repo_path: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct ProfileManifest {
    active_profile_id: String,
    profiles: Vec<ProfileRecord>,
}

impl Default for ProfileManifest {
    fn default() -> Self {
        Self {
            active_profile_id: DEFAULT_PROFILE_ID.to_string(),
            profiles: vec![ProfileRecord {
                id: DEFAULT_PROFILE_ID.to_string(),
                name: "Default".to_string(),
                created_at_unix: chrono_like_now(),
                last_repo_path: None,
            }],
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(default, rename_all = "camelCase")]
pub struct ProfileState {
    pub recent_repos: Vec<String>,
    pub last_source_path: String,
}

fn chrono_like_now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

fn config_root(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    app.path()
        .app_config_dir()
        .map_err(|e| format!("app_config_dir failed: {e}"))
}

fn manifest_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(config_root(app)?.join("profiles.json"))
}

fn profile_dir(app: &tauri::AppHandle, profile_id: &str) -> Result<PathBuf, String> {
    Ok(config_root(app)?.join("profiles").join(profile_id))
}

pub fn profile_settings_path(app: &tauri::AppHandle, profile_id: &str) -> Result<PathBuf, String> {
    Ok(profile_dir(app, profile_id)?.join("settings.json"))
}

fn profile_state_path(app: &tauri::AppHandle, profile_id: &str) -> Result<PathBuf, String> {
    Ok(profile_dir(app, profile_id)?.join("state.json"))
}

fn read_manifest(app: &tauri::AppHandle) -> Result<ProfileManifest, String> {
    ensure_initialized(app)?;
    let path = manifest_path(app)?;
    let text = fs::read_to_string(&path).map_err(|e| format!("read manifest: {e}"))?;
    serde_json::from_str(&text).map_err(|e| format!("parse manifest: {e}"))
}

fn write_manifest(app: &tauri::AppHandle, manifest: &ProfileManifest) -> Result<(), String> {
    let path = manifest_path(app)?;
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir).map_err(|e| format!("create manifest dir: {e}"))?;
    }
    let text = serde_json::to_string_pretty(manifest).map_err(|e| e.to_string())?;
    fs::write(&path, text).map_err(|e| format!("write manifest: {e}"))
}

fn migrate_legacy_settings(app: &tauri::AppHandle) -> Result<(), String> {
    let root = config_root(app)?;
    let legacy = root.join("settings.json");
    let target = profile_settings_path(app, DEFAULT_PROFILE_ID)?;
    if legacy.is_file() && !target.is_file() {
        if let Some(dir) = target.parent() {
            fs::create_dir_all(dir).map_err(|e| format!("create profile dir: {e}"))?;
        }
        fs::copy(&legacy, &target).map_err(|e| format!("migrate settings: {e}"))?;
    }
    Ok(())
}

pub fn ensure_initialized(app: &tauri::AppHandle) -> Result<(), String> {
    let path = manifest_path(app)?;
    if path.is_file() {
        return Ok(());
    }
    migrate_legacy_settings(app)?;
    let manifest = ProfileManifest::default();
    write_manifest(app, &manifest)?;
    let settings_path = profile_settings_path(app, DEFAULT_PROFILE_ID)?;
    if !settings_path.is_file() {
        let settings = UiSettings::default();
        write_profile_settings(app, DEFAULT_PROFILE_ID, &settings)?;
    }
    let state_path = profile_state_path(app, DEFAULT_PROFILE_ID)?;
    if !state_path.is_file() {
        write_profile_state(app, DEFAULT_PROFILE_ID, &ProfileState::default())?;
    }
    Ok(())
}

pub fn write_profile_settings(
    app: &tauri::AppHandle,
    profile_id: &str,
    settings: &UiSettings,
) -> Result<(), String> {
    let path = profile_settings_path(app, profile_id)?;
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir).map_err(|e| format!("create settings dir: {e}"))?;
    }
    let text = serde_json::to_string_pretty(settings).map_err(|e| e.to_string())?;
    fs::write(&path, text).map_err(|e| format!("write settings: {e}"))
}

pub fn read_profile_settings(app: &tauri::AppHandle, profile_id: &str) -> UiSettings {
    let Ok(path) = profile_settings_path(app, profile_id) else {
        return UiSettings::default();
    };
    let Ok(text) = fs::read_to_string(path) else {
        return UiSettings::default();
    };
    serde_json::from_str(&text).unwrap_or_default()
}

pub fn write_profile_state(
    app: &tauri::AppHandle,
    profile_id: &str,
    state: &ProfileState,
) -> Result<(), String> {
    let path = profile_state_path(app, profile_id)?;
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir).map_err(|e| format!("create state dir: {e}"))?;
    }
    let text = serde_json::to_string_pretty(state).map_err(|e| e.to_string())?;
    fs::write(&path, text).map_err(|e| format!("write state: {e}"))
}

pub fn read_profile_state(app: &tauri::AppHandle, profile_id: &str) -> ProfileState {
    let Ok(path) = profile_state_path(app, profile_id) else {
        return ProfileState::default();
    };
    let Ok(text) = fs::read_to_string(path) else {
        return ProfileState::default();
    };
    serde_json::from_str(&text).unwrap_or_default()
}

fn sanitize_profile_id(name: &str) -> String {
    let mut out = String::new();
    for ch in name.chars() {
        if ch.is_ascii_alphanumeric() || ch == '-' || ch == '_' {
            out.push(ch.to_ascii_lowercase());
        } else if ch.is_whitespace() {
            out.push('-');
        }
    }
    if out.is_empty() {
        format!("profile-{}", chrono_like_now())
    } else {
        out
    }
}

#[tauri::command]
pub fn list_profiles(app: tauri::AppHandle) -> Result<serde_json::Value, String> {
    let manifest = read_manifest(&app)?;
    Ok(serde_json::json!({
        "ok": true,
        "activeProfileId": manifest.active_profile_id,
        "profiles": manifest.profiles,
    }))
}

#[tauri::command]
pub fn get_active_profile(app: tauri::AppHandle) -> Result<String, String> {
    let manifest = read_manifest(&app)?;
    Ok(manifest.active_profile_id)
}

#[tauri::command]
pub fn create_profile(app: tauri::AppHandle, name: String) -> Result<serde_json::Value, String> {
    let mut manifest = read_manifest(&app)?;
    let id = sanitize_profile_id(&name);
    if manifest.profiles.iter().any(|p| p.id == id) {
        return Err(format!("profile already exists: {id}"));
    }
    let record = ProfileRecord {
        id: id.clone(),
        name: name.trim().to_string(),
        created_at_unix: chrono_like_now(),
        last_repo_path: None,
    };
    manifest.profiles.push(record.clone());
    write_manifest(&app, &manifest)?;
    write_profile_settings(&app, &id, &UiSettings::default())?;
    write_profile_state(&app, &id, &ProfileState::default())?;
    Ok(serde_json::json!({ "ok": true, "profile": record }))
}

#[tauri::command]
pub fn rename_profile(
    app: tauri::AppHandle,
    profile_id: String,
    name: String,
) -> Result<(), String> {
    let mut manifest = read_manifest(&app)?;
    let Some(rec) = manifest.profiles.iter_mut().find(|p| p.id == profile_id) else {
        return Err("profile not found".into());
    };
    rec.name = name.trim().to_string();
    write_manifest(&app, &manifest)
}

#[tauri::command]
pub fn delete_profile(app: tauri::AppHandle, profile_id: String) -> Result<(), String> {
    if profile_id == DEFAULT_PROFILE_ID {
        return Err("cannot delete default profile".into());
    }
    let mut manifest = read_manifest(&app)?;
    let before = manifest.profiles.len();
    manifest.profiles.retain(|p| p.id != profile_id);
    if manifest.profiles.len() == before {
        return Err("profile not found".into());
    }
    if manifest.active_profile_id == profile_id {
        manifest.active_profile_id = DEFAULT_PROFILE_ID.to_string();
    }
    write_manifest(&app, &manifest)?;
    if let Ok(dir) = profile_dir(&app, &profile_id) {
        let _ = fs::remove_dir_all(dir);
    }
    Ok(())
}

#[tauri::command]
pub fn set_active_profile(app: tauri::AppHandle, profile_id: String) -> Result<(), String> {
    let mut manifest = read_manifest(&app)?;
    if !manifest.profiles.iter().any(|p| p.id == profile_id) {
        return Err("profile not found".into());
    }
    manifest.active_profile_id = profile_id;
    write_manifest(&app, &manifest)
}

#[tauri::command]
pub fn get_profile_state(app: tauri::AppHandle, profile_id: Option<String>) -> ProfileState {
    let id = profile_id.unwrap_or_else(|| {
        read_manifest(&app)
            .map(|m| m.active_profile_id)
            .unwrap_or_else(|_| DEFAULT_PROFILE_ID.to_string())
    });
    read_profile_state(&app, &id)
}

#[tauri::command]
pub fn set_profile_state(
    app: tauri::AppHandle,
    profile_id: Option<String>,
    state: ProfileState,
) -> Result<(), String> {
    let id = profile_id.unwrap_or_else(|| {
        read_manifest(&app)
            .map(|m| m.active_profile_id)
            .unwrap_or_else(|_| DEFAULT_PROFILE_ID.to_string())
    });
    write_profile_state(&app, &id, &state)
}

#[tauri::command]
pub fn set_profile_last_repo(
    app: tauri::AppHandle,
    profile_id: Option<String>,
    repo_path: String,
) -> Result<(), String> {
    let id = profile_id.unwrap_or_else(|| {
        read_manifest(&app)
            .map(|m| m.active_profile_id)
            .unwrap_or_else(|_| DEFAULT_PROFILE_ID.to_string())
    });
    let mut manifest = read_manifest(&app)?;
    if let Some(rec) = manifest.profiles.iter_mut().find(|p| p.id == id) {
        rec.last_repo_path = if repo_path.trim().is_empty() {
            None
        } else {
            Some(repo_path)
        };
        write_manifest(&app, &manifest)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sanitize_profile_id_basic() {
        assert_eq!(sanitize_profile_id("My Profile"), "my-profile");
    }
}
