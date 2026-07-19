use std::path::PathBuf;

fn load_dll() -> PathBuf {
    // Prefer target/debug first: build.rs colocates sqlite3.dll next to EchoCAPI
    // for LoadLibrary. ECHO_CAPI_DLL may point at a bare CMake out/ tree without
    // runtime deps (valid for ABI tests, not for the sidecar layout check).
    let candidates = [
        Some(format!(
            "{}/target/debug/EchoCAPI.dll",
            env!("CARGO_MANIFEST_DIR")
        )),
        Some(format!("{}/libs/EchoCAPI.dll", env!("CARGO_MANIFEST_DIR"))),
        std::env::var("ECHO_CAPI_DLL").ok(),
        Some(format!(
            "{}/../../native/out/bottlemusic-check/EchoCAPI.dll",
            env!("CARGO_MANIFEST_DIR")
        )),
    ];
    for c in candidates.into_iter().flatten() {
        let p = std::path::Path::new(&c);
        if p.exists() {
            return p.to_path_buf();
        }
    }
    panic!("Could not find EchoCAPI.dll");
}

#[test]
fn test_runtime_dependencies_are_next_to_echo_capi() {
    // Layout under test is the packaged/debug-run layout produced by build.rs.
    let packaged = PathBuf::from(format!(
        "{}/target/debug/EchoCAPI.dll",
        env!("CARGO_MANIFEST_DIR")
    ));
    let libs = PathBuf::from(format!(
        "{}/libs/EchoCAPI.dll",
        env!("CARGO_MANIFEST_DIR")
    ));
    let path = [packaged, libs]
        .into_iter()
        .find(|p| p.exists())
        .unwrap_or_else(load_dll);
    let dir = path.parent().expect("EchoCAPI.dll has parent dir");

    assert!(
        dir.join("sqlite3.dll").exists(),
        "sqlite3.dll must be next to EchoCAPI.dll so LoadLibrary can resolve runtime dependencies (dir={})",
        dir.display()
    );
}

#[test]
fn test_core_exports_present_without_playback() {
    use libloading::Library;
    use std::os::raw::c_char;

    let path = load_dll();
    let lib = unsafe { Library::new(&path) }.expect("load DLL");

    unsafe {
        // Core request path must remain exported.
        let _: libloading::Symbol<
            unsafe extern "C" fn(
                *const c_char,
                *const c_char,
                *const c_char,
                *const c_char,
                *const c_char,
                *mut *mut c_char,
            ),
        > = lib
            .get(b"EchoHandleRequest")
            .expect("EchoHandleRequest must exist");

        let _: libloading::Symbol<unsafe extern "C" fn() -> i32> =
            lib.get(b"EchoShutdown").expect("EchoShutdown must exist");

        // Media Foundation playback ABI must be gone after stage 2c.
        assert!(
            lib.get::<unsafe extern "C" fn(i32) -> bool>(b"EchoPlaybackInitialize")
                .is_err(),
            "EchoPlaybackInitialize must not be exported"
        );
    }
}
