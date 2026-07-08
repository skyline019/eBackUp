use once_cell::sync::OnceCell;
use std::path::PathBuf;
use std::process::Command;

static RUNTIME_DIR: OnceCell<PathBuf> = OnceCell::new();

pub fn set_runtime_dir(dir: PathBuf) {
    let _ = RUNTIME_DIR.set(dir);
}

fn eb_sync_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    if let Some(dir) = RUNTIME_DIR.get() {
        paths.push(dir.join("eb-sync.exe"));
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(parent) = exe.parent() {
            paths.push(parent.join("eb-sync.exe"));
            paths.push(parent.join("runtime/eb-sync.exe"));
            paths.push(parent.join("bin/eb-sync.exe"));
        }
    }
    if let Ok(manifest) = std::env::var("CARGO_MANIFEST_DIR") {
        paths.push(PathBuf::from(manifest).join("bin/eb-sync.exe"));
    }
    paths.push(PathBuf::from("eb-sync.exe"));
    paths
}

pub fn find_eb_sync() -> Result<PathBuf, String> {
    eb_sync_paths()
        .into_iter()
        .find(|p| p.is_file())
        .ok_or_else(|| "eb-sync.exe not found; run npm run sync:runtime".to_string())
}

pub fn run_eb_sync(args: &[&str]) -> Result<String, String> {
    let exe = find_eb_sync()?;
    let output = Command::new(&exe)
        .args(args)
        .output()
        .map_err(|e| format!("spawn eb-sync: {e}"))?;
    let stdout = String::from_utf8_lossy(&output.stdout).to_string();
    let stderr = String::from_utf8_lossy(&output.stderr).to_string();
    if !output.status.success() {
        let msg = if stderr.trim().is_empty() {
            stdout.trim().to_string()
        } else {
            stderr.trim().to_string()
        };
        return Err(if msg.is_empty() {
            format!("eb-sync exited with {}", output.status)
        } else {
            msg
        });
    }
    Ok(stdout)
}
