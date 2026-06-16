use std::env;
use std::path::PathBuf;

fn main() {
    let dll_name = if cfg!(target_os = "windows") {
        "EchoCAPI.dll"
    } else {
        "libEchoCAPI.so"
    };
    let preset = if env::var("PROFILE").unwrap_or_default() == "release" {
        "bottlemusic-release"
    } else {
        "bottlemusic-check"
    };

    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap_or_default());
    let src = manifest_dir.join(format!("../../native/out/{}/{}", preset, dll_name));

    if src.exists() {
        // OUT_DIR ≈ target/debug/build/ui-<hash>/out
        // Go up three levels to reach target/debug/ or target/release/
        let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR not set"));
        let target_profile_dir = out_dir
            .parent()                          // build/ui-<hash>/
            .and_then(|p| p.parent())          // build/
            .and_then(|p| p.parent())          // debug/ or release/
            .expect("OUT_DIR structure unexpected");
        let dst = target_profile_dir.join(dll_name);

        if dst.exists() {
            // On Windows, if the DLL is currently loaded by the app, we cannot overwrite it.
            // However, we CAN rename it. By renaming the old DLL out of the way, we allow
            // the new copy to succeed.
            let _ = std::fs::rename(&dst, target_profile_dir.join(format!("{}.old", dll_name)));
        }

        if let Err(e) = std::fs::copy(&src, &dst) {
            println!("cargo:warning=Failed to copy native DLL to target dir: {} (Is the app running?)", e);
        } else {
            println!("cargo:warning=Copied native DLL: {} → {}", src.display(), dst.display());
        }

        // Also copy it to a stable staging directory for tauri.conf.json bundling.
        let staging_dir = manifest_dir.join("libs");
        let _ = std::fs::create_dir_all(&staging_dir);
        let staging_dst = staging_dir.join(dll_name);
        if staging_dst.exists() {
            let _ = std::fs::rename(&staging_dst, staging_dir.join(format!("{}.old", dll_name)));
        }
        if let Err(e) = std::fs::copy(&src, &staging_dst) {
            println!("cargo:warning=Failed to copy native DLL to staging dir: {}", e);
        }

        println!("cargo:rerun-if-changed={}", src.display());
    } else {
        // 找不到原生 DLL 时直接失败：空占位会让打包通过，但运行时只能崩。
        // 先执行 `cd ui && pnpm backend:build`，让 Rust/Tauri 总是绑定真实 EchoCAPI。
        panic!(
            "Native DLL not found at {}. Build the C++ backend first: cd ui && pnpm backend:build",
            src.display()
        );
    }

    tauri_build::build();
}
