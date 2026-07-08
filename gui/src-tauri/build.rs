fn main() {
    tauri_build::build();
    if let Err(e) = sync_runtime_from_build_tree() {
        println!("cargo:warning=ebbackup runtime sync skipped: {e}");
    }
}

fn build_roots(manifest_dir: &std::path::Path) -> Vec<std::path::PathBuf> {
    let mut roots = Vec::new();
    if let Ok(dir) = std::env::var("EBBACKUP_BUILD_DIR") {
        if !dir.is_empty() {
            roots.push(std::path::PathBuf::from(dir));
        }
    }
    roots.push(manifest_dir.join("../../build/engine_cpp/Release"));
    roots.push(manifest_dir.join("../../build/engine_cpp"));
    roots.push(manifest_dir.join("../../build"));
    roots
}

fn sync_cpp_roots(manifest_dir: &std::path::Path) -> Vec<std::path::PathBuf> {
    let mut roots = Vec::new();
    if let Ok(dir) = std::env::var("EBSYNC_BUILD_DIR") {
        if !dir.is_empty() {
            roots.push(std::path::PathBuf::from(dir));
        }
    }
    roots.push(manifest_dir.join("../../build/sync_cpp/Release"));
    roots.push(manifest_dir.join("../../build/sync_cpp"));
    roots.push(manifest_dir.join("../../build"));
    roots
}

fn sync_runtime_from_build_tree() -> Result<(), String> {
    let manifest_dir = std::path::PathBuf::from(
        std::env::var("CARGO_MANIFEST_DIR").map_err(|e| e.to_string())?,
    );
    let dest = manifest_dir.join("bin");
    std::fs::create_dir_all(&dest).map_err(|e| e.to_string())?;

    let mut copied = false;

    let dll_leaf = "ebbackup_workbench.dll";
    if let Some(src) = find_under_roots(&build_roots(&manifest_dir), dll_leaf) {
        let dst = dest.join(dll_leaf);
        std::fs::copy(&src, &dst).map_err(|e| format!("copy {}: {e}", dst.display()))?;
        println!("cargo:rerun-if-changed={}", src.display());
        copied = true;
    }

    let sync_leaf = "eb-sync.exe";
    if let Some(src) = find_under_roots(&sync_cpp_roots(&manifest_dir), sync_leaf) {
        let dst = dest.join(sync_leaf);
        std::fs::copy(&src, &dst).map_err(|e| format!("copy {}: {e}", dst.display()))?;
        println!("cargo:rerun-if-changed={}", src.display());
        copied = true;
    }

    if copied {
        Ok(())
    } else {
        Err(
            "no runtime binaries found (cmake --build build --target ebbackup_workbench eb-sync)"
                .into(),
        )
    }
}

fn find_under_roots(roots: &[std::path::PathBuf], leaf: &str) -> Option<std::path::PathBuf> {
    for root in roots {
        if !root.is_dir() {
            continue;
        }
        let direct = root.join(leaf);
        if direct.is_file() {
            return Some(direct);
        }
        if let Ok(entries) = std::fs::read_dir(root) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_dir() {
                    let nested = path.join(leaf);
                    if nested.is_file() {
                        return Some(nested);
                    }
                }
            }
        }
    }
    None
}
