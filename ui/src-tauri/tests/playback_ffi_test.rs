use std::ffi::CStr;
use std::os::raw::c_char;
use std::path::PathBuf;

fn load_dll() -> PathBuf {
    let candidates = [
        std::env::var("ECHO_CAPI_DLL").ok(),
        Some(format!(
            "{}/target/debug/EchoCAPI.dll",
            env!("CARGO_MANIFEST_DIR")
        )),
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
fn test_playback_initialize_and_query_state() {
    use libloading::Library;
    let path = load_dll();
    let lib = unsafe { Library::new(&path) }.expect("load DLL");

    unsafe {
        let init: libloading::Symbol<unsafe extern "C" fn(i32) -> bool> =
            lib.get(b"EchoPlaybackInitialize").expect("find init");

        // Try MFS first (backend=1), fall back to MFP (backend=0) for headless CI
        let ok = init(1);
        if !ok {
            let ok2 = init(0);
            assert!(ok2, "Both MFS and MFP init failed");
        }

        // Query state — should return valid JSON allocated by C++
        let get_state: libloading::Symbol<unsafe extern "C" fn() -> *mut c_char> =
            lib.get(b"EchoPlaybackGetState").expect("find get_state");
        let free_str: libloading::Symbol<unsafe extern "C" fn(*mut c_char)> =
            lib.get(b"EchoFreeString").expect("find free_str");

        let ptr = get_state();
        assert!(!ptr.is_null(), "GetState returned null");
        let json = CStr::from_ptr(ptr)
            .to_str()
            .expect("utf8")
            .to_owned();
        free_str(ptr);

        assert!(json.contains("state"), "JSON missing 'state' field: {}", json);

        // Shutdown playback subsystem
        let shutdown: libloading::Symbol<unsafe extern "C" fn()> =
            lib.get(b"EchoPlaybackShutdown").expect("find shutdown");
        shutdown();
    }
}
