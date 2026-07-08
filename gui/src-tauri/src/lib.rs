mod commands;
mod ebbackup_ffi;
mod profile_store;
mod sync_runner;
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
                ebbackup_ffi::set_runtime_dir(runtime_dir.clone());
                sync_runner::set_runtime_dir(runtime_dir);
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::runtime_info,
            commands::init_repo,
            commands::init_repo_encrypt,
            commands::unwrap_recovery_key,
            commands::rotate_password,
            commands::upgrade_legacy_envelope,
            commands::test_webhook,
            commands::open_repo,
            commands::switch_profile,
            commands::close_repo,
            commands::repo_info,
            commands::list_snapshots,
            commands::list_manifest_files,
            commands::list_manifest_page,
            commands::query_path_history,
            commands::diff_snapshots,
            commands::build_path_index,
            commands::export_restore_report,
            commands::get_backup_report,
            commands::set_backup_hooks,
            commands::list_jobs,
            commands::upsert_job,
            commands::delete_job,
            commands::run_job,
            commands::enqueue_job,
            commands::job_queue_status,
            commands::run_job_queue,
            commands::snapshot_reachability,
            commands::rpo_summary,
            commands::orphan_explain,
            commands::list_ops_audit,
            commands::run_backup,
            commands::run_restore,
            commands::run_restore_ex,
            commands::preview_restore,
            commands::preview_in_place,
            commands::apply_in_place,
            commands::export_delta,
            commands::import_delta,
            commands::run_maintenance_wizard,
            commands::verify_repo,
            commands::recover_repo,
            commands::compact_repo,
            commands::gc_orphans,
            commands::prune_snapshots,
            commands::set_password,
            commands::set_audit_key,
            commands::load_filter_file,
            commands::set_filter_json,
            commands::suggest_exclude_filters,
            commands::last_error,
            commands::get_backup_stats,
            commands::sync_runtime_binaries,
            commands::pick_directory,
            commands::pick_file,
            commands::path_exists,
            commands::sync_status,
            commands::sync_push,
            commands::sync_ferry_export,
            commands::sync_init_local,
            commands::sync_init_ferry,
            commands::sync_init_pds,
            commands::sync_pds_auth_url,
            commands::sync_pds_auth,
            commands::sync_maintenance_check,
            profile_store::list_profiles,
            profile_store::get_active_profile,
            profile_store::create_profile,
            profile_store::rename_profile,
            profile_store::delete_profile,
            profile_store::set_active_profile,
            profile_store::get_profile_state,
            profile_store::set_profile_state,
            profile_store::set_profile_last_repo,
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
