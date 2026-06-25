use std::ffi::CStr;
use std::os::raw::c_char;
use std::path::PathBuf;

fn load_dll() -> Option<PathBuf> {
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
            return Some(p.to_path_buf());
        }
    }
    None
}

fn make_temp_app_dir() -> Option<PathBuf> {
    let mut dir = std::env::temp_dir();
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    dir.push(format!("bottlemusic-stats-ffi-{}", stamp));
    if std::fs::create_dir_all(&dir).is_err() {
        return None;
    }
    Some(dir)
}

fn cstr(ptr: *mut c_char) -> String {
    assert!(!ptr.is_null(), "FFI returned null string");
    let s = unsafe { CStr::from_ptr(ptr) }
        .to_str()
        .expect("utf8")
        .to_owned();
    s
}

#[test]
fn test_stats_record_and_query() {
    use libloading::Library;

    let Some(path) = load_dll() else {
        eprintln!("EchoCAPI.dll not found — skipping stats FFI test");
        return;
    };
    let Some(app_dir) = make_temp_app_dir() else {
        eprintln!("Could not create temp app dir — skipping");
        return;
    };

    let lib = match unsafe { Library::new(&path) } {
        Ok(l) => l,
        Err(e) => {
            eprintln!("Failed to load DLL: {} — skipping", e);
            let _ = std::fs::remove_dir_all(&app_dir);
            return;
        }
    };

    let result = run_stats_scenario(&lib, &app_dir);
    let _ = std::fs::remove_dir_all(&app_dir);

    if let Err(msg) = result {
        panic!("stats FFI scenario failed: {}", msg);
    }
}

fn run_stats_scenario(
    lib: &libloading::Library,
    app_dir: &std::path::Path,
) -> Result<(), String> {
    use libloading::Symbol;

    unsafe {
        let init_with_paths: Symbol<unsafe extern "C" fn(*const c_char)> = lib
            .get(b"EchoInitializeWithPaths")
            .map_err(|e| format!("find EchoInitializeWithPaths: {}", e))?;

        let dir_str = app_dir.to_str().ok_or("non-utf8 temp dir")?;
        let dir_c = std::ffi::CString::new(dir_str).map_err(|e| e.to_string())?;
        init_with_paths(dir_c.as_ptr());

        let record_play: Symbol<unsafe extern "C" fn(*const c_char)> = lib
            .get(b"EchoStatsRecordPlay")
            .map_err(|e| format!("find EchoStatsRecordPlay: {}", e))?;

        let now_ms = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis() as i64)
            .unwrap_or(0);
        let record = format!(
            r#"{{"song_hash":"abc123","song_name":"FFI Test Song","singer_name":"FFI Artist","album_id":"album1","album_name":"FFI Album","cover_url":"","duration_seconds":200,"completed":true,"listened_seconds":200,"quality":"320","played_at":{}}}"#,
            now_ms
        );
        let record_c = std::ffi::CString::new(record).map_err(|e| e.to_string())?;
        record_play(record_c.as_ptr());

        let free_str: Symbol<unsafe extern "C" fn(*mut c_char)> = lib
            .get(b"EchoFreeString")
            .map_err(|e| format!("find EchoFreeString: {}", e))?;

        let get_summary: Symbol<unsafe extern "C" fn(*const c_char) -> *mut c_char> = lib
            .get(b"EchoStatsGetSummary")
            .map_err(|e| format!("find EchoStatsGetSummary: {}", e))?;
        let summary_c = std::ffi::CString::new("all").unwrap();
        let summary_ptr = get_summary(summary_c.as_ptr());
        let summary_json = cstr(summary_ptr);
        free_str(summary_ptr);

        let v: serde_json::Value = serde_json::from_str(&summary_json)
            .map_err(|e| format!("summary not valid JSON: {} in '{}'", e, summary_json))?;
        if v.get("total_plays").is_none() {
            return Err(format!("summary missing total_plays: {}", summary_json));
        }
        if v.get("range").and_then(|r| r.as_str()) != Some("all") {
            return Err(format!("summary range mismatch: {}", summary_json));
        }

        let get_recent: Symbol<unsafe extern "C" fn(i32, i32) -> *mut c_char> = lib
            .get(b"EchoStatsGetRecent")
            .map_err(|e| format!("find EchoStatsGetRecent: {}", e))?;
        let recent_ptr = get_recent(10, 0);
        let recent_json = cstr(recent_ptr);
        free_str(recent_ptr);

        let recent: serde_json::Value = serde_json::from_str(&recent_json)
            .map_err(|e| format!("recent not valid JSON: {} in '{}'", e, recent_json))?;
        let items = recent
            .get("items")
            .and_then(|i| i.as_array())
            .ok_or_else(|| format!("recent missing items array: {}", recent_json))?;
        if items.is_empty() {
            return Err(format!("expected at least one recent item: {}", recent_json));
        }
        let first = &items[0];
        if first.get("name").and_then(|n| n.as_str()) != Some("FFI Test Song") {
            return Err(format!("first recent item name mismatch: {}", recent_json));
        }
        if first.get("song_hash").and_then(|h| h.as_str()) != Some("abc123") {
            return Err(format!("first recent item song_hash mismatch: {}", recent_json));
        }

        let shutdown: Symbol<unsafe extern "C" fn()> = lib
            .get(b"EchoShutdown")
            .map_err(|e| format!("find EchoShutdown: {}", e))?;
        shutdown();
    }

    Ok(())
}
