//! Workbench UI settings persisted to `{app_config_dir}/settings.json`.

use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};
use tauri::Manager;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct UiSettings {
    pub accent: String,
    pub text_main: String,
    pub text_regular: String,
    pub text_soft: String,
    pub page_bg: String,
    pub table_bg: String,
    pub bg_mode: String,
    pub bg_image_url: String,
    pub bg_image_opacity: f32,
    pub bg_video_url: String,
    pub bg_video_opacity: f32,
    pub panel_opacity: f32,
    pub workspace_card_opacity: f32,
    pub table_view_opacity: f32,
    pub log_panel_opacity: f32,
    pub sidebar_width: f32,
    pub log_collapsed: bool,
    pub dense_mode: bool,
    pub font_scale: f32,
    pub animations: bool,
    pub corner_scale: f32,
    pub shadow_scale: f32,
    pub log_font_scale: f32,
    pub log_line_height: f32,
    pub border_contrast: f32,
    pub panel_brightness: f32,
    pub log_highlight_intensity: f32,
}

impl Default for UiSettings {
    fn default() -> Self {
        Self {
            accent: "#3b82f6".to_string(),
            text_main: "#dbe7ff".to_string(),
            text_regular: "#c7d2fe".to_string(),
            text_soft: "#93c5fd".to_string(),
            page_bg: "#080d18".to_string(),
            table_bg: "#08121f".to_string(),
            bg_mode: "gradient".to_string(),
            bg_image_url: String::new(),
            bg_image_opacity: 0.58,
            bg_video_url: String::new(),
            bg_video_opacity: 0.58,
            panel_opacity: 0.86,
            workspace_card_opacity: 0.88,
            table_view_opacity: 0.74,
            log_panel_opacity: 0.9,
            sidebar_width: 260.0,
            log_collapsed: false,
            dense_mode: false,
            font_scale: 1.0,
            animations: true,
            corner_scale: 1.0,
            shadow_scale: 1.0,
            log_font_scale: 1.0,
            log_line_height: 1.5,
            border_contrast: 1.0,
            panel_brightness: 1.0,
            log_highlight_intensity: 1.0,
        }
    }
}

fn settings_file_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    let dir = app
        .path()
        .app_config_dir()
        .map_err(|e| format!("app_config_dir failed: {e}"))?;
    Ok(dir.join("settings.json"))
}

#[tauri::command]
pub fn get_ui_settings(app: tauri::AppHandle) -> UiSettings {
    let path = match settings_file_path(&app) {
        Ok(p) => p,
        Err(_) => return UiSettings::default(),
    };
    let Ok(text) = fs::read_to_string(&path) else {
        return UiSettings::default();
    };
    serde_json::from_str::<UiSettings>(&text).unwrap_or_default()
}

#[tauri::command]
pub fn set_ui_settings(app: tauri::AppHandle, settings: UiSettings) -> Result<(), String> {
    let path = settings_file_path(&app)?;
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)
            .map_err(|e| format!("failed to create config dir {}: {e}", dir.display()))?;
    }
    let text = serde_json::to_string_pretty(&settings).map_err(|e| e.to_string())?;
    fs::write(&path, text).map_err(|e| format!("failed to write {}: {e}", path.display()))?;
    Ok(())
}

#[tauri::command]
pub fn ui_settings_path(app: tauri::AppHandle) -> Result<String, String> {
    settings_file_path(&app).map(|p| p.display().to_string())
}

#[tauri::command]
pub fn ui_settings_exists(app: tauri::AppHandle) -> bool {
    settings_file_path(&app)
        .map(|p| p.is_file())
        .unwrap_or(false)
}
