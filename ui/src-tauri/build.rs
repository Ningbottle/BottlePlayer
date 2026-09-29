use std::env;
use std::fs;
use std::path::{Path, PathBuf};

mod build_support;

/// Extract `inline constexpr long kName = N;` from RequestDeadlines.h.
fn generate_deadlines_rs() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap_or_default());
    let header = manifest_dir.join("../../native/include/echo/core/RequestDeadlines.h");
    println!("cargo:rerun-if-changed={}", header.display());

    let text = fs::read_to_string(&header).unwrap_or_else(|e| {
        panic!(
            "Failed to read RequestDeadlines.h at {}: {}",
            header.display(),
            e
        );
    });

    let mut constants: Vec<(String, u64)> = Vec::new();
    for line in text.lines() {
        let line = line.trim();
        // inline constexpr long kDeadlineSongUrlMs = 10000;
        if !line.contains("inline constexpr long k") {
            continue;
        }
        let Some(name_start) = line.find('k') else {
            continue;
        };
        let rest = &line[name_start..];
        let Some(eq) = rest.find('=') else {
            continue;
        };
        let name = rest[..eq].trim().to_string();
        let val_part = rest[eq + 1..].trim().trim_end_matches(';').trim();
        let Ok(val) = val_part.parse::<u64>() else {
            continue;
        };
        if name.starts_with('k') {
            constants.push((name, val));
        }
    }

    let required = [
        "kDeadlineSongUrlMs",
        "kDeadlineImageMs",
        "kDeadlineLoginPollMs",
        "kDeadlineSearchMs",
        "kDeadlinePlaylistMs",
        "kDeadlineGenericMs",
        "kFrontendTimeoutMs",
    ];
    for req in required {
        if !constants.iter().any(|(n, _)| n == req) {
            panic!(
                "RequestDeadlines.h missing required constant {}; extract failed",
                req
            );
        }
    }

    let mut out = String::from(
        "// @generated from native/include/echo/core/RequestDeadlines.h — do not edit\n",
    );
    for (name, val) in &constants {
        out.push_str(&format!("pub const {}: u64 = {};\n", name, val));
    }

    let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR not set"));
    let out_path = out_dir.join("deadlines_generated.rs");
    fs::write(&out_path, out).expect("write deadlines_generated.rs");
}

fn copy_runtime_dll(src: &Path, dst: &Path, name: &str) {
    build_support::copy_runtime_dll(src, dst).unwrap_or_else(|e| {
        panic!(
            "Failed to stage native runtime {name}: {} -> {}: {e}",
            src.display(),
            dst.display()
        )
    });
    println!(
        "cargo:warning=Native runtime ready: {} → {}",
        src.display(),
        dst.display()
    );
}

fn verify_native_runtime(manifest_dir: &Path, native_root: &Path, dll: &Path) {
    let verifier = manifest_dir.join("../scripts/verify-native-runtime.ps1");
    println!("cargo:rerun-if-changed={}", verifier.display());
    println!(
        "cargo:rerun-if-changed={}",
        native_root.join("CMakeLists.txt").display()
    );
    // Track only source-owned trees. Watching build/, out/, or
    // vcpkg_installed/ makes generated objects and dependency extraction
    // invalidate every Rust build that verifies the native runtime.
    for source_dir in [
        "async",
        "core",
        "diagnostics",
        "image",
        "include",
        "stats",
        "storage",
        "tests",
        "tools",
    ] {
        println!(
            "cargo:rerun-if-changed={}",
            native_root.join(source_dir).display()
        );
    }
    let output = std::process::Command::new("powershell.exe")
        .args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-File"])
        .arg(&verifier)
        .arg("-Dll")
        .arg(dll)
        .arg("-NativeRoot")
        .arg(native_root)
        .output()
        .expect("run native source-fingerprint verifier");
    if !output.status.success() {
        panic!(
            "Native runtime verification failed before staging. Rebuild the selected native preset.\n{}\n{}",
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr)
        );
    }
    for line in String::from_utf8_lossy(&output.stdout).lines() {
        println!("cargo:warning={line}");
    }
}

fn main() {
    generate_deadlines_rs();

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
    let sqlite_src = manifest_dir.join("../../native/vcpkg_installed/x64-windows/bin/sqlite3.dll");

    if src.is_file() {
        if cfg!(target_os = "windows") && !sqlite_src.is_file() {
            panic!("Required SQLite runtime not found at {}. Rebuild the native dependencies before packaging.", sqlite_src.display());
        }
        if cfg!(target_os = "windows")
            && fs::metadata(&sqlite_src)
                .expect("read required SQLite runtime metadata")
                .len()
                == 0
        {
            panic!(
                "Required SQLite runtime is empty at {}",
                sqlite_src.display()
            );
        }
        if cfg!(target_os = "windows") {
            verify_native_runtime(&manifest_dir, &manifest_dir.join("../../native"), &src);
        }
        // OUT_DIR ≈ target/debug/build/ui-<hash>/out
        // Go up three levels to reach target/debug/ or target/release/
        let out_dir = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR not set"));
        let target_profile_dir = out_dir
            .parent() // build/ui-<hash>/
            .and_then(|p| p.parent()) // build/
            .and_then(|p| p.parent()) // debug/ or release/
            .expect("OUT_DIR structure unexpected");
        let dst = target_profile_dir.join(dll_name);

        copy_runtime_dll(&src, &dst, dll_name);

        // Also copy it to a stable staging directory for tauri.conf.json bundling.
        let staging_dir = manifest_dir.join("libs");
        std::fs::create_dir_all(&staging_dir).expect("create native runtime staging directory");
        let staging_dst = staging_dir.join(dll_name);
        copy_runtime_dll(&src, &staging_dst, dll_name);

        if cfg!(target_os = "windows") && sqlite_src.exists() {
            let sqlite_name = "sqlite3.dll";
            copy_runtime_dll(
                &sqlite_src,
                &target_profile_dir.join(sqlite_name),
                sqlite_name,
            );
            copy_runtime_dll(&sqlite_src, &staging_dir.join(sqlite_name), sqlite_name);
            println!("cargo:rerun-if-changed={}", sqlite_src.display());
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
