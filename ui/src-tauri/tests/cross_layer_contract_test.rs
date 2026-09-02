//! Cross-layer contract tests for the Rust <-> C++ boundary (audit item B4).
//!
//! These tests load the real EchoCAPI.dll and pin the C ABI contract that the
//! Rust layer (src/backend_api.rs, src/stats.rs) and the Vue frontend depend
//! on. Any C++ change to the JSON shapes below turns these tests red.
//!
//! Coverage:
//! * Stats round trip: EchoStatsRecordPlay(JSON) -> EchoStatsGetSummary must
//!   return exactly the six-field contract shape (the key set of the fallback
//!   literal in native/core/C_API.cpp EchoStatsGetSummary and of
//!   native/stats/PlayStatsService.cpp GetSummary) with self-consistent
//!   counters after writes.
//! * Error envelopes out of EchoHandleRequest:
//!   - uninitialized backend -> structured 500 envelope, and
//!   - unregistered route -> structured 404 "Unknown route" envelope.
//!   Both are deterministic local decisions (see each test for the source) and
//!   never depend on network reachability. A watchdog fails the test instead
//!   of letting it hang.
//!
//! Hermetic guarantees: every test initializes the backend into a fresh
//! temp directory (never the real app data directory) and removes it
//! afterwards; no test performs any network I/O or requires login state.
//!
//! Note on boundaries: src/backend_api.rs and src/stats.rs are private
//! modules of the app crate, so this integration test exercises the same C
//! ABI those modules resolve (the actual inter-layer boundary) rather than
//! calling the private Rust wrappers. The Rust marshalling layer itself is
//! pinned against the same DLL by the in-crate unit tests
//! (stats::tests::test_stats_ffi_end_to_end,
//! backend_api::tests::test_m3_concurrency).

use libloading::{Library, Symbol};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::Duration;

/// All tests here share the process-global DLL state; serialize them.
static CONTRACT_LOCK: Mutex<()> = Mutex::new(());

fn lock_contract() -> std::sync::MutexGuard<'static, ()> {
    CONTRACT_LOCK.lock().unwrap_or_else(|p| p.into_inner())
}

/// Same candidate chain as tests/playback_ffi_test.rs: packaged debug build
/// first, then libs/, then the ECHO_CAPI_DLL override, then the CMake tree.
fn find_dll() -> PathBuf {
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
        let p = PathBuf::from(c);
        if p.exists() {
            return p;
        }
    }
    panic!("Could not find EchoCAPI.dll");
}

/// Fresh per-run temp data dir: pid + nanos make collisions across runs
/// effectively impossible; any leftover from a crashed run is reclaimed by the
/// OS temp policy and never lands inside the repository.
fn fresh_app_dir(tag: &str) -> PathBuf {
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let dir = std::env::temp_dir().join(format!(
        "bottlemusic-contract-{tag}-{}-{nanos}",
        std::process::id()
    ));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).expect("create temp app data dir");
    dir
}

fn now_millis() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

/// Resolved EchoCAPI entry points. The Library handle is kept alive so the
/// function pointers stay valid (mirrors src/backend_api.rs CApiHandle).
struct EchoCapi {
    _lib: Library,
    handle_request: unsafe extern "C" fn(
        *const c_char,
        *const c_char,
        *const c_char,
        *const c_char,
        *const c_char,
        *mut *mut c_char,
    ),
    free_str: unsafe extern "C" fn(*mut c_char),
    shutdown: unsafe extern "C" fn() -> c_int,
    stats_record_play: unsafe extern "C" fn(*const c_char),
    stats_get_summary: unsafe extern "C" fn(*const c_char) -> *mut c_char,
}

impl EchoCapi {
    /// Load the DLL and resolve symbols WITHOUT initializing the backend, so
    /// tests can first observe the uninitialized state.
    fn load(dll_path: &Path) -> Self {
        let lib = unsafe { Library::new(dll_path) }.expect("load EchoCAPI.dll");

        unsafe {
            let handle_request = *lib
                .get::<unsafe extern "C" fn(
                    *const c_char,
                    *const c_char,
                    *const c_char,
                    *const c_char,
                    *const c_char,
                    *mut *mut c_char,
                )>(b"EchoHandleRequest")
                .expect("EchoHandleRequest must exist");
            let free_str = *lib
                .get::<unsafe extern "C" fn(*mut c_char)>(b"EchoFreeString")
                .expect("EchoFreeString must exist");
            let shutdown = *lib
                .get::<unsafe extern "C" fn() -> c_int>(b"EchoShutdown")
                .expect("EchoShutdown must exist");
            let stats_record_play = *lib
                .get::<unsafe extern "C" fn(*const c_char)>(b"EchoStatsRecordPlay")
                .expect("EchoStatsRecordPlay must exist");
            let stats_get_summary = *lib
                .get::<unsafe extern "C" fn(*const c_char) -> *mut c_char>(
                    b"EchoStatsGetSummary",
                )
                .expect("EchoStatsGetSummary must exist");

            EchoCapi {
                _lib: lib,
                handle_request,
                free_str,
                shutdown,
                stats_record_play,
                stats_get_summary,
            }
        }
    }

    /// EchoInitializeWithPathsV2 against a throwaway app data dir.
    fn init_with_app_dir(&self, app_dir: &Path) {
        unsafe {
            let init: Symbol<unsafe extern "C" fn(*const c_char) -> c_int> = self
                ._lib
                .get(b"EchoInitializeWithPathsV2")
                .expect("EchoInitializeWithPathsV2 must exist");
            let get_last_error: Symbol<unsafe extern "C" fn() -> *mut c_char> = self
                ._lib
                .get(b"EchoGetLastError")
                .expect("EchoGetLastError must exist");

            let c_dir = CString::new(app_dir.to_str().expect("temp dir is valid UTF-8"))
                .expect("temp dir has no interior NUL");
            let status = init(c_dir.as_ptr());
            if status != 0 {
                let message = {
                    let ptr = get_last_error();
                    if ptr.is_null() {
                        format!("EchoInitializeWithPathsV2 failed with status {status}")
                    } else {
                        let text = CStr::from_ptr(ptr).to_string_lossy().into_owned();
                        (self.free_str)(ptr);
                        text
                    }
                };
                panic!("C API initialization failed: {message}");
            }
        }
    }

    fn record_play(&self, json: &str) {
        let c = CString::new(json).expect("record JSON has no interior NUL");
        unsafe { (self.stats_record_play)(c.as_ptr()) };
    }

    fn get_summary(&self, range: &str) -> serde_json::Value {
        let c = CString::new(range).expect("range has no interior NUL");
        let raw = unsafe {
            let ptr = (self.stats_get_summary)(c.as_ptr());
            assert!(!ptr.is_null(), "EchoStatsGetSummary returned NULL");
            let text = CStr::from_ptr(ptr).to_string_lossy().into_owned();
            (self.free_str)(ptr);
            text
        };
        serde_json::from_str(&raw)
            .unwrap_or_else(|e| panic!("summary is not valid JSON: {e}; got: {raw}"))
    }
}

impl Drop for EchoCapi {
    fn drop(&mut self) {
        // Best-effort shutdown so the next test (and the OS) sees a clean
        // state; a non-zero status only means workers were still draining and
        // the OS reclaims everything at process exit.
        unsafe {
            (self.shutdown)();
        }
    }
}

/// Call EchoHandleRequest with only function pointers (Send + 'static) so the
/// watchdog thread owns no references into the test stack.
fn raw_request(
    handle_request: unsafe extern "C" fn(
        *const c_char,
        *const c_char,
        *const c_char,
        *const c_char,
        *const c_char,
        *mut *mut c_char,
    ),
    free_str: unsafe extern "C" fn(*mut c_char),
    method: &str,
    path: &str,
) -> String {
    let c_method = CString::new(method).unwrap();
    let c_path = CString::new(path).unwrap();
    unsafe {
        let mut out: *mut c_char = std::ptr::null_mut();
        handle_request(
            c_method.as_ptr(),
            c_path.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            &mut out,
        );
        assert!(!out.is_null(), "EchoHandleRequest produced no response");
        let text = CStr::from_ptr(out).to_string_lossy().into_owned();
        free_str(out);
        text
    }
}

/// Run a request under a watchdog: the test FAILS (rather than hangs) if the
/// call does not return within `timeout`.
fn request_with_watchdog(capi: &EchoCapi, method: &str, path: &str, timeout: Duration) -> serde_json::Value {
    let handle_request = capi.handle_request;
    let free_str = capi.free_str;
    let method = method.to_string();
    let path = path.to_string();
    let message = format!("EchoHandleRequest({method} {path})");
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        let outcome = std::panic::catch_unwind(|| raw_request(handle_request, free_str, &method, &path));
        let _ = tx.send(outcome);
    });
    match rx.recv_timeout(timeout) {
        Ok(Ok(raw)) => serde_json::from_str(&raw)
            .unwrap_or_else(|e| panic!("envelope is not valid JSON: {e}; got: {raw}")),
        Ok(Err(_)) => panic!("{message} panicked"),
        Err(_) => panic!("{message} hung past the {timeout:?} watchdog"),
    }
}

/// The summary contract: exactly these six fields, no more, no fewer.
/// Source: the fallback literal in native/core/C_API.cpp
/// (EchoStatsGetSummary) and the key set built by
/// native/stats/PlayStatsService.cpp GetSummary.
fn assert_summary_contract_shape(value: &serde_json::Value, context: &str) {
    let obj = value
        .as_object()
        .unwrap_or_else(|| panic!("{context}: summary must be a JSON object, got {value}"));
    let mut keys: Vec<&str> = obj.keys().map(String::as_str).collect();
    keys.sort_unstable();
    assert_eq!(
        keys,
        vec![
            "completion_rate",
            "range",
            "total_listened_seconds",
            "total_plays",
            "unique_artists",
            "unique_songs",
        ],
        "{context}: summary field set drifted from the C++ contract"
    );
    for numeric in [
        "total_plays",
        "total_listened_seconds",
        "unique_songs",
        "unique_artists",
        "completion_rate",
    ] {
        assert!(
            obj[numeric].is_number(),
            "{context}: {numeric} must be numeric, got {}",
            obj[numeric]
        );
    }
    assert!(
        obj["range"].is_string(),
        "{context}: range must be a string, got {}",
        obj["range"]
    );
}

/// A play record shaped exactly like the frontend/Rust payload consumed by
/// EchoStatsRecordPlay (field names from native/core/C_API.cpp). listened
/// must exceed kMinCountedListenedSeconds (60s, PlayStatsService.cpp) or the
/// record is deliberately not counted.
fn play_record(song_hash: &str, song_name: &str, singer: &str, listened: f64, completed: bool) -> String {
    serde_json::json!({
        "song_hash": song_hash,
        "song_name": song_name,
        "singer_name": singer,
        "album_id": "contract-album-1",
        "album_name": "Contract Album",
        "cover_url": "",
        "duration_seconds": 240.0,
        "completed": completed,
        "listened_seconds": listened,
        "quality": "128",
        "played_at": now_millis()
    })
    .to_string()
}

#[test]
fn stats_round_trip_matches_cpp_summary_contract_shape() {
    let _guard = lock_contract();
    let dll = find_dll();
    let dir = fresh_app_dir("stats");
    let capi = EchoCapi::load(&dll);
    capi.init_with_app_dir(&dir);

    // Empty database: values must all be zero within the contract shape.
    let before = capi.get_summary("all");
    assert_summary_contract_shape(&before, "empty-db summary");
    assert_eq!(before["total_plays"].as_u64(), Some(0), "empty-db total_plays");
    assert_eq!(before["unique_songs"].as_u64(), Some(0), "empty-db unique_songs");
    assert_eq!(before["unique_artists"].as_u64(), Some(0), "empty-db unique_artists");
    assert!(
        (before["total_listened_seconds"].as_f64().unwrap_or(-1.0)).abs() < 1e-9,
        "empty-db total_listened_seconds: {}",
        before["total_listened_seconds"]
    );
    assert!(
        (before["completion_rate"].as_f64().unwrap_or(-1.0)).abs() < 1e-9,
        "empty-db completion_rate: {}",
        before["completion_rate"]
    );
    assert_eq!(before["range"].as_str(), Some("all"), "summary echoes the requested range");

    // Write one completed play (240s listened > 60s minimum, so it counts).
    capi.record_play(&play_record(
        "contract-hash-1",
        "Contract Song A",
        "Contract Artist X",
        240.0,
        true,
    ));

    let after_one = capi.get_summary("all");
    assert_summary_contract_shape(&after_one, "one-play summary");
    assert_eq!(after_one["total_plays"].as_u64(), Some(1));
    assert_eq!(after_one["unique_songs"].as_u64(), Some(1));
    assert_eq!(after_one["unique_artists"].as_u64(), Some(1));
    assert!(
        (after_one["total_listened_seconds"].as_f64().unwrap_or(f64::NAN) - 240.0).abs() < 1e-6,
        "total_listened_seconds after one play: {}",
        after_one["total_listened_seconds"]
    );
    assert!(
        (after_one["completion_rate"].as_f64().unwrap_or(f64::NAN) - 1.0).abs() < 1e-6,
        "one completed play must give completion_rate 1.0: {}",
        after_one["completion_rate"]
    );

    // Second play: same artist, different song, not completed, half listened.
    // Counters must move consistently and the rate must drop to exactly 0.5.
    capi.record_play(&play_record(
        "contract-hash-2",
        "Contract Song B",
        "Contract Artist X",
        120.0,
        false,
    ));

    let after_two = capi.get_summary("all");
    assert_summary_contract_shape(&after_two, "two-play summary");
    assert_eq!(after_two["total_plays"].as_u64(), Some(2));
    assert_eq!(after_two["unique_songs"].as_u64(), Some(2));
    assert_eq!(after_two["unique_artists"].as_u64(), Some(1));
    assert!(
        (after_two["total_listened_seconds"].as_f64().unwrap_or(f64::NAN) - 360.0).abs() < 1e-6,
        "total_listened_seconds after two plays: {}",
        after_two["total_listened_seconds"]
    );
    assert!(
        (after_two["completion_rate"].as_f64().unwrap_or(f64::NAN) - 0.5).abs() < 1e-6,
        "one of two completed must give completion_rate 0.5: {}",
        after_two["completion_rate"]
    );

    // The frontend only ever requests 1d/7d/30d (StatsRange) — the range
    // field must echo whatever the caller asked for.
    let weekly = capi.get_summary("7d");
    assert_summary_contract_shape(&weekly, "7d summary");
    assert_eq!(weekly["range"].as_str(), Some("7d"));
    assert_eq!(weekly["total_plays"].as_u64(), Some(2), "both plays happened just now, so 7d sees them");

    drop(capi);
    std::fs::remove_dir_all(&dir).expect("clean up contract test data dir");
}

#[test]
fn error_envelopes_are_structured_and_network_independent() {
    let _guard = lock_contract();
    let dll = find_dll();
    let dir = fresh_app_dir("envelope");
    let capi = EchoCapi::load(&dll);

    // (1) Uninitialized backend -> structured 500 envelope.
    // Determinism source: native/core/C_API.cpp EchoHandleRequest checks
    // `!Ctx().api` BEFORE any routing or network work and serializes
    // {"status":500,...,"body":{"error":"C API is not initialized or was
    // shut down"}}. No socket is opened on this path, so the result cannot
    // depend on external connectivity.
    let uninitialized = request_with_watchdog(&capi, "GET", "/health", Duration::from_secs(30));
    assert_eq!(uninitialized["status"].as_u64(), Some(500), "uninitialized envelope status");
    assert_eq!(
        uninitialized["body"]["error"].as_str(),
        Some("C API is not initialized or was shut down"),
        "uninitialized envelope body: {}",
        uninitialized["body"]
    );
    assert!(
        uninitialized["headers"]["Content-Type"].is_string(),
        "envelope must carry headers.Content-Type: {uninitialized}"
    );

    capi.init_with_app_dir(&dir);

    // (2) Unregistered route -> structured 404 "Unknown route" envelope.
    // Determinism source: native/core/CompatApi.cpp Handle() performs a pure
    // GetRouteTable() hash-map lookup; a miss returns 404 before any
    // HttpClient object exists (CompatApi.cpp: "Paths NOT in this table are
    // unknown (404)"). The path below shares no prefix with any registered
    // route, so the outcome is identical online or offline.
    let not_found = request_with_watchdog(
        &capi,
        "GET",
        "/contract-test/nonexistent-route",
        Duration::from_secs(30),
    );
    assert_eq!(not_found["status"].as_u64(), Some(404), "404 envelope status");
    assert_eq!(not_found["body"]["error"].as_str(), Some("Unknown route"));
    assert_eq!(not_found["body"]["error_code"].as_u64(), Some(404));
    assert_eq!(not_found["body"]["status"].as_u64(), Some(0));
    assert!(
        not_found["headers"]["Content-Type"].is_string(),
        "404 envelope must carry headers.Content-Type: {not_found}"
    );

    // (3) A registered diagnostics route still succeeds: proves the 404 above
    // is a routing decision, not a broken dispatcher. /health is served
    // locally by native/core/compat_routes/DiagnosticsRoutes.cpp with no
    // upstream call, so this stays deterministic offline.
    let health = request_with_watchdog(&capi, "GET", "/health", Duration::from_secs(30));
    assert_eq!(health["status"].as_u64(), Some(200), "health envelope status");
    assert_eq!(health["body"]["status"].as_u64(), Some(1), "health body status");
    assert_eq!(health["body"]["data"]["state"].as_str(), Some("ok"), "health body: {}", health["body"]);

    drop(capi);
    std::fs::remove_dir_all(&dir).expect("clean up contract test data dir");
}
