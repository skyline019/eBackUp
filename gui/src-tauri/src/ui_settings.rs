//! Workbench UI settings persisted per profile under `{app_config_dir}/profiles/{id}/settings.json`.

use serde::{Deserialize, Serialize};

use crate::profile_store::{
    ensure_initialized, read_profile_settings, write_profile_settings, DEFAULT_PROFILE_ID,
};

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
    pub stale_backup_alert_days: f32,
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
            stale_backup_alert_days: 7.0,
        }
    }
}

fn active_profile_id(app: &tauri::AppHandle) -> String {
    crate::profile_store::get_active_profile(app.clone())
        .unwrap_or_else(|_| DEFAULT_PROFILE_ID.to_string())
}

#[tauri::command]
pub fn get_ui_settings(app: tauri::AppHandle, profile_id: Option<String>) -> UiSettings {
    let _ = ensure_initialized(&app);
    let id = profile_id.unwrap_or_else(|| active_profile_id(&app));
    read_profile_settings(&app, &id)
}

#[tauri::command]
pub fn set_ui_settings(
    app: tauri::AppHandle,
    settings: UiSettings,
    profile_id: Option<String>,
) -> Result<(), String> {
    let id = profile_id.unwrap_or_else(|| active_profile_id(&app));
    write_profile_settings(&app, &id, &settings)
}

#[tauri::command]
pub fn ui_settings_path(app: tauri::AppHandle, profile_id: Option<String>) -> Result<String, String> {
    let id = profile_id.unwrap_or_else(|| active_profile_id(&app));
    crate::profile_store::profile_settings_path(&app, &id).map(|p| p.display().to_string())
}

#[tauri::command]
pub fn ui_settings_exists(app: tauri::AppHandle, profile_id: Option<String>) -> bool {
    let id = profile_id.unwrap_or_else(|| active_profile_id(&app));
    crate::profile_store::profile_settings_path(&app, &id)
        .map(|p| p.is_file())
        .unwrap_or(false)
}
