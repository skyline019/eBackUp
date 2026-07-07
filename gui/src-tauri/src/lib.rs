mod commands;
mod ebbackup_ffi;
mod task_progress;
mod ui_settings;

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            use tauri::path::BaseDirectory;
            if let Ok(runtime_dir) = app.path().resolve("runtime", BaseDirectory::Resource) {
                ebbackup_ffi::set_runtime_dir(runtime_dir);
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::runtime_info,
            commands::init_repo,
            commands::open_repo,
            commands::close_repo,
            commands::repo_info,
            commands::list_snapshots,
            commands::run_backup,
            commands::run_restore,
            commands::verify_repo,
            commands::recover_repo,
            commands::compact_repo,
            commands::gc_orphans,
            commands::prune_snapshots,
            commands::set_password,
            commands::load_filter_file,
            commands::last_error,
            commands::get_backup_stats,
            commands::sync_runtime_binaries,
            commands::pick_directory,
            commands::pick_file,
            commands::path_exists,
            ui_settings::get_ui_settings,
            ui_settings::set_ui_settings,
            ui_settings::ui_settings_path,
            ui_settings::ui_settings_exists,
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|_app, event| {
            if let tauri::RunEvent::ExitRequested { .. } = event {
                let _ = commands::close_repo_inner();
            }
        });
}
