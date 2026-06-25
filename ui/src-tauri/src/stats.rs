use std::ffi::CStr;
use std::ffi::CString;
use crate::backend_api;

#[tauri::command]
pub fn stats_record_play(json: String) -> Result<(), String> {
    let guard = backend_api::api_handle()?;
    let handle = guard.as_ref().unwrap();
    let cstr = CString::new(json).map_err(|e| e.to_string())?;
    unsafe { (handle.stats_record_play)(cstr.as_ptr()) };
    Ok(())
}

#[tauri::command]
pub fn stats_get_summary(range: String) -> Result<String, String> {
    let guard = backend_api::api_handle()?;
    let handle = guard.as_ref().unwrap();
    let cstr = CString::new(range).map_err(|e| e.to_string())?;
    let ptr = unsafe { (handle.stats_get_summary)(cstr.as_ptr()) };
    if ptr.is_null() {
        return Err("null summary".into());
    }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[tauri::command]
pub fn stats_get_top(kind: String, range: String, limit: i32) -> Result<String, String> {
    let guard = backend_api::api_handle()?;
    let handle = guard.as_ref().unwrap();
    let c_kind = CString::new(kind).map_err(|e| e.to_string())?;
    let c_range = CString::new(range).map_err(|e| e.to_string())?;
    let ptr = unsafe { (handle.stats_get_top)(c_kind.as_ptr(), c_range.as_ptr(), limit) };
    if ptr.is_null() {
        return Err("null top".into());
    }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[tauri::command]
pub fn stats_get_timeline(range: String) -> Result<String, String> {
    let guard = backend_api::api_handle()?;
    let handle = guard.as_ref().unwrap();
    let cstr = CString::new(range).map_err(|e| e.to_string())?;
    let ptr = unsafe { (handle.stats_get_timeline)(cstr.as_ptr()) };
    if ptr.is_null() {
        return Err("null timeline".into());
    }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[tauri::command]
pub fn stats_get_recent(limit: i32, offset: i32) -> Result<String, String> {
    let guard = backend_api::api_handle()?;
    let handle = guard.as_ref().unwrap();
    let ptr = unsafe { (handle.stats_get_recent)(limit, offset) };
    if ptr.is_null() {
        return Err("null recent".into());
    }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[tauri::command]
pub fn stats_get_recommendations(limit: i32) -> Result<String, String> {
    let guard = backend_api::api_handle()?;
    let handle = guard.as_ref().unwrap();
    let ptr = unsafe { (handle.stats_get_recommendations)(limit) };
    if ptr.is_null() {
        return Err("null recommendations".into());
    }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    static DLL_GUARD: Mutex<()> = Mutex::new(());

    fn find_dll() -> String {
        let candidates: Vec<String> = {
            let mut v: Vec<String> = std::env::var("ECHO_CAPI_DLL")
                .ok()
                .into_iter()
                .collect();
            if cfg!(target_os = "windows") {
                v.push("../../../native/out/bottlemusic-check/EchoCAPI.dll".into());
                v.push(format!("{}/target/debug/EchoCAPI.dll", env!("CARGO_MANIFEST_DIR")));
                v.push(format!("{}/EchoCAPI.dll", env!("CARGO_MANIFEST_DIR")));
            } else {
                v.push("../../../native/out/bottlemusic-check/libEchoCAPI.so".into());
                v.push(format!("{}/target/debug/libEchoCAPI.so", env!("CARGO_MANIFEST_DIR")));
            }
            v
        };
        candidates
            .iter()
            .find(|p| std::path::Path::new(p.as_str()).exists())
            .cloned()
            .unwrap_or_else(|| {
                panic!("Could not find EchoCAPI library in candidates: {:?}", candidates);
            })
    }

    fn make_record(
        hash: &str,
        name: &str,
        singer: &str,
        album: &str,
        duration: f64,
        completed: bool,
        listened: f64,
        played_at: i64,
    ) -> String {
        serde_json::json!({
            "song_hash": hash,
            "song_name": name,
            "singer_name": singer,
            "album_id": "1",
            "album_name": album,
            "cover_url": "",
            "duration_seconds": duration,
            "completed": completed,
            "listened_seconds": listened,
            "quality": "128",
            "played_at": played_at
        })
        .to_string()
    }

    #[test]
    fn test_stats_ffi_end_to_end() {
        let _lock = DLL_GUARD.lock().unwrap();

        let dll_path = find_dll();
        let app_data_dir = std::env::temp_dir().join("bottlemusic_stats_test");
        let _ = std::fs::remove_dir_all(&app_data_dir);
        std::fs::create_dir_all(&app_data_dir).unwrap();
        backend_api::init_with_paths(&dll_path, Some(app_data_dir.to_str().unwrap()))
            .expect("Failed to init C API");

        let now_ms = chrono::Local::now().timestamp_millis();
        let day1 = now_ms - 86400000;
        let day2 = now_ms;

        let records = vec![
            make_record("hashA", "Song A", "Artist X", "Album One", 240.0, true, 240.0, day1),
            make_record("hashA", "Song A", "Artist X", "Album One", 240.0, true, 240.0, day1 + 1000),
            make_record("hashA", "Song A", "Artist X", "Album One", 240.0, true, 240.0, day2 - 2000),
            make_record("hashB", "Song B", "Artist X", "Album One", 180.0, false, 90.0, day1 + 2000),
            make_record("hashB", "Song B", "Artist X", "Album One", 180.0, false, 90.0, day2 - 1000),
            make_record("hashC", "Song C", "Artist Y", "Album Two", 300.0, true, 300.0, day2),
        ];

        for record in &records {
            stats_record_play(record.clone()).expect("record_play failed");
        }

        let result = stats_get_summary("all".into()).expect("get_summary failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["total_plays"], 6);
        assert_eq!(j["unique_songs"], 3);
        assert_eq!(j["unique_artists"], 2);
        assert_eq!(j["range"], "all");
        assert!((j["total_listened_seconds"].as_f64().unwrap() - 1200.0).abs() < 0.01);

        let result = stats_get_top("song".into(), "all".into(), 10).expect("get_top failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["dim"], "song");
        assert_eq!(j["items"].as_array().unwrap().len(), 3);
        assert_eq!(j["items"][0]["name"], "Song A");
        assert_eq!(j["items"][0]["play_count"], 3);
        assert_eq!(j["items"][1]["name"], "Song B");
        assert_eq!(j["items"][1]["play_count"], 2);
        assert_eq!(j["items"][2]["name"], "Song C");
        assert_eq!(j["items"][2]["play_count"], 1);

        let result = stats_get_top("artist".into(), "all".into(), 10).expect("get_top failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["dim"], "artist");
        assert_eq!(j["items"].as_array().unwrap().len(), 2);
        assert_eq!(j["items"][0]["name"], "Artist X");
        assert_eq!(j["items"][0]["play_count"], 5);
        assert_eq!(j["items"][1]["name"], "Artist Y");
        assert_eq!(j["items"][1]["play_count"], 1);

        let result = stats_get_top("album".into(), "all".into(), 10).expect("get_top failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["dim"], "album");
        assert_eq!(j["items"].as_array().unwrap().len(), 2);
        assert_eq!(j["items"][0]["name"], "Album One");
        assert_eq!(j["items"][0]["play_count"], 5);

        let result = stats_get_timeline("all".into()).expect("get_timeline failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert!(j["items"].is_array());
        assert_eq!(j["items"].as_array().unwrap().len(), 2);
        let total: i64 = j["items"]
            .as_array()
            .unwrap()
            .iter()
            .map(|i| i["count"].as_i64().unwrap())
            .sum();
        assert_eq!(total, 6);

        let result = stats_get_recent(10, 0).expect("get_recent failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["items"].as_array().unwrap().len(), 6);
        assert_eq!(j["items"][0]["song_hash"], "hashC");
        assert_eq!(j["items"][1]["song_hash"], "hashB");

        let result = stats_get_recent(3, 0).expect("get_recent failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["items"].as_array().unwrap().len(), 3);

        let result = stats_get_recommendations(5).expect("get_recommendations failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert!(j["items"].is_array());
        assert!(!j["items"].as_array().unwrap().is_empty());
        assert_eq!(j["items"][0]["singer"], "Artist X");

        stats_record_play("not valid json".into()).expect("invalid json should be no-op");
        let result = stats_get_summary("all".into()).expect("get_summary failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["total_plays"], 6);

        backend_api::shutdown_c_api();
        let _ = std::fs::remove_dir_all(&app_data_dir);
    }

    #[test]
    fn test_stats_returns_err_without_dll() {
        let _lock = DLL_GUARD.lock().unwrap();
        backend_api::shutdown_c_api();
        let result = stats_get_summary("all".into());
        assert!(result.is_err());
    }
}
