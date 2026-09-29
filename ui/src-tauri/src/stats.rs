use crate::backend_api;
use crate::dispatch_stats_ffi;
use std::ffi::CStr;
use std::ffi::CString;

// Stage 4 (vip-stability remediation plan / F18): these six commands used to
// be SYNC Tauri commands — the stats FFI ran on the main thread with no
// deadline, freezing the UI on disk contention or the shutdown race. They
// are now async and dispatch through the survival-bounded stats FFI helper
// (lib.rs): dedicated 4-permit cap, permit held by the blocking closure
// until it finishes, and a 3s timeout covering queueing + execution.
//
// B06: EchoStatsRecordPlay now reports its outcome (C_API.h
// EchoStatsRecordStatus). The command maps those codes onto the Result —
// a below-threshold listen stays Ok (a short listen is a normal outcome,
// not an error and must never disturb playback), while invalid input,
// unparseable JSON, missing backend, and storage failures surface as
// `stats_*` errors the UI can log instead of silently pretending success.

// C_API.h EchoStatsRecordStatus codes.
const ECHO_STATS_RECORDED: i32 = 0;
const ECHO_STATS_BELOW_THRESHOLD: i32 = 1;
const ECHO_STATS_INVALID_RECORD: i32 = 2;
const ECHO_STATS_BAD_JSON: i32 = 3;
const ECHO_STATS_NOT_INITIALIZED: i32 = 4;

#[tauri::command]
pub async fn stats_record_play(json: String) -> Result<(), String> {
    dispatch_stats_ffi(move |control| {
        let guard = backend_api::api_handle_for_request(&control)?;
        let handle = guard.as_ref().unwrap();
        let cstr = CString::new(json).map_err(|e| format!("stats_bad_json: {e}"))?;
        control.claim_dll_start()?;
        let status = unsafe { (handle.stats_record_play)(cstr.as_ptr()) };
        match status {
            ECHO_STATS_RECORDED => Ok(()),
            // Normal outcome: the listen is shorter than the counting
            // threshold. Not recorded by design, not an error.
            ECHO_STATS_BELOW_THRESHOLD => Ok(()),
            ECHO_STATS_INVALID_RECORD => Err("stats_invalid_record".into()),
            ECHO_STATS_BAD_JSON => Err("stats_bad_json".into()),
            ECHO_STATS_NOT_INITIALIZED => Err("stats_not_initialized".into()),
            _ => Err("stats_storage_error".into()),
        }
    })
    .await
}

#[tauri::command]
pub async fn stats_get_summary(range: String) -> Result<String, String> {
    dispatch_stats_ffi(move |control| {
        let guard = backend_api::api_handle_for_request(&control)?;
        let handle = guard.as_ref().unwrap();
        let cstr = CString::new(range).map_err(|e| e.to_string())?;
        control.claim_dll_start()?;
        let ptr = unsafe { (handle.stats_get_summary)(cstr.as_ptr()) };
        if ptr.is_null() {
            return Err("null summary".into());
        }
        let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
        unsafe { (handle.free_str)(ptr) };
        Ok(result)
    })
    .await
}

#[tauri::command]
pub async fn stats_get_top(kind: String, range: String, limit: i32) -> Result<String, String> {
    dispatch_stats_ffi(move |control| {
        let guard = backend_api::api_handle_for_request(&control)?;
        let handle = guard.as_ref().unwrap();
        let c_kind = CString::new(kind).map_err(|e| e.to_string())?;
        let c_range = CString::new(range).map_err(|e| e.to_string())?;
        control.claim_dll_start()?;
        let ptr = unsafe { (handle.stats_get_top)(c_kind.as_ptr(), c_range.as_ptr(), limit) };
        if ptr.is_null() {
            return Err("null top".into());
        }
        let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
        unsafe { (handle.free_str)(ptr) };
        Ok(result)
    })
    .await
}

#[tauri::command]
pub async fn stats_get_timeline(range: String) -> Result<String, String> {
    dispatch_stats_ffi(move |control| {
        let guard = backend_api::api_handle_for_request(&control)?;
        let handle = guard.as_ref().unwrap();
        let cstr = CString::new(range).map_err(|e| e.to_string())?;
        control.claim_dll_start()?;
        let ptr = unsafe { (handle.stats_get_timeline)(cstr.as_ptr()) };
        if ptr.is_null() {
            return Err("null timeline".into());
        }
        let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
        unsafe { (handle.free_str)(ptr) };
        Ok(result)
    })
    .await
}

#[tauri::command]
pub async fn stats_get_recent(limit: i32, offset: i32) -> Result<String, String> {
    dispatch_stats_ffi(move |control| {
        let guard = backend_api::api_handle_for_request(&control)?;
        let handle = guard.as_ref().unwrap();
        control.claim_dll_start()?;
        let ptr = unsafe { (handle.stats_get_recent)(limit, offset) };
        if ptr.is_null() {
            return Err("null recent".into());
        }
        let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
        unsafe { (handle.free_str)(ptr) };
        Ok(result)
    })
    .await
}

#[tauri::command]
pub async fn stats_get_recommendations(limit: i32) -> Result<String, String> {
    dispatch_stats_ffi(move |control| {
        let guard = backend_api::api_handle_for_request(&control)?;
        let handle = guard.as_ref().unwrap();
        control.claim_dll_start()?;
        let ptr = unsafe { (handle.stats_get_recommendations)(limit) };
        if ptr.is_null() {
            return Err("null recommendations".into());
        }
        let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
        unsafe { (handle.free_str)(ptr) };
        Ok(result)
    })
    .await
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;
    use std::time::Duration;

    #[test]
    fn stats_commands_are_async_and_dispatch_through_bounded_survival_ffi() {
        // Stage 4 (vip-stability remediation plan): the six stats commands
        // ran as SYNC Tauri commands — the FFI executed on the main thread
        // with no deadline (F18), freezing the UI on disk contention or the
        // shutdown race. Contract: all six commands are async and dispatch
        // through the survival-bounded stats FFI helper (the permit is held
        // by the blocking closure and queueing happens inside the timeout).
        let src = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/src/stats.rs"))
            .expect("stats.rs must be readable");
        // Line-start matching so this test's own assertion strings (which
        // contain the patterns) cannot self-match.
        let async_commands = src
            .lines()
            .filter(|line| line.trim_start().starts_with("pub async fn stats_"))
            .count();
        let sync_commands = src
            .lines()
            .filter(|line| line.trim_start().starts_with("pub fn stats_"))
            .count();
        assert_eq!(
            async_commands, 6,
            "all six stats commands must be async fn (Stage 4 not applied)"
        );
        assert_eq!(
            sync_commands, 0,
            "sync stats commands found — the Stage 4 main-thread freeze is not fixed"
        );
        assert!(
            src.contains(concat!("dispatch_stats_", "ffi")),
            "stats commands must dispatch through the survival-bounded FFI helper"
        );
    }

    fn find_dll() -> String {
        let candidates: Vec<String> = {
            let mut v: Vec<String> = std::env::var("ECHO_CAPI_DLL").ok().into_iter().collect();
            if cfg!(target_os = "windows") {
                v.push("../../../native/out/bottlemusic-check/EchoCAPI.dll".into());
                v.push(format!(
                    "{}/target/debug/EchoCAPI.dll",
                    env!("CARGO_MANIFEST_DIR")
                ));
                v.push(format!("{}/EchoCAPI.dll", env!("CARGO_MANIFEST_DIR")));
            } else {
                v.push("../../../native/out/bottlemusic-check/libEchoCAPI.so".into());
                v.push(format!(
                    "{}/target/debug/libEchoCAPI.so",
                    env!("CARGO_MANIFEST_DIR")
                ));
            }
            v
        };
        candidates
            .iter()
            .find(|p| std::path::Path::new(p.as_str()).exists())
            .cloned()
            .unwrap_or_else(|| {
                panic!(
                    "Could not find EchoCAPI library in candidates: {:?}",
                    candidates
                );
            })
    }

    #[allow(clippy::too_many_arguments)]
    fn make_record(
        hash: &str,
        name: &str,
        singer: &str,
        album_id: &str,
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
            "album_id": album_id,
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

    #[tokio::test]
    // Intentional: the test-only C-API mutex serializes DLL init/shutdown
    // across the WHOLE async body (it is never taken inside the dispatched
    // closures), so holding it across awaits is the point, not a hazard.
    #[allow(clippy::await_holding_lock)]
    async fn test_stats_ffi_end_to_end() {
        let _lock = backend_api::lock_test_c_api();

        let dll_path = find_dll();
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let app_data_dir = std::env::temp_dir().join(format!(
            "bottlemusic_stats_test_{}_{}",
            std::process::id(),
            stamp
        ));
        let _ = std::fs::remove_dir_all(&app_data_dir);
        std::fs::create_dir_all(&app_data_dir).unwrap();

        backend_api::shutdown_c_api();
        backend_api::init_with_paths(&dll_path, Some(app_data_dir.to_str().unwrap()))
            .expect("Failed to init C API");

        let now_ms = chrono::Local::now().timestamp_millis();
        let day1 = now_ms - 86400000;
        let day2 = now_ms;

        let records = vec![
            // album-1: 5 plays across Song A (3) + Song B (2)
            make_record(
                "hashA",
                "Song A",
                "Artist X",
                "album-1",
                "Album One",
                240.0,
                true,
                240.0,
                day1,
            ),
            make_record(
                "hashA",
                "Song A",
                "Artist X",
                "album-1",
                "Album One",
                240.0,
                true,
                240.0,
                day1 + 1000,
            ),
            make_record(
                "hashA",
                "Song A",
                "Artist X",
                "album-1",
                "Album One",
                240.0,
                true,
                240.0,
                day2 - 2000,
            ),
            make_record(
                "hashB",
                "Song B",
                "Artist X",
                "album-1",
                "Album One",
                180.0,
                false,
                90.0,
                day1 + 2000,
            ),
            make_record(
                "hashB",
                "Song B",
                "Artist X",
                "album-1",
                "Album One",
                180.0,
                false,
                90.0,
                day2 - 1000,
            ),
            // album-2: 1 play (Song C). Same display name "Album One" — must
            // NOT merge with album-1 when grouping by album_id.
            make_record(
                "hashC",
                "Song C",
                "Artist Y",
                "album-2",
                "Album One",
                300.0,
                true,
                300.0,
                day2,
            ),
        ];

        for record in &records {
            stats_record_play(record.clone())
                .await
                .expect("record_play failed");
        }

        let result = stats_get_summary("all".into())
            .await
            .expect("get_summary failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["total_plays"], 6);
        assert_eq!(j["unique_songs"], 3);
        assert_eq!(j["unique_artists"], 2);
        assert_eq!(j["range"], "all");
        assert!((j["total_listened_seconds"].as_f64().unwrap() - 1200.0).abs() < 0.01);

        let result = stats_get_top("song".into(), "all".into(), 10)
            .await
            .expect("get_top failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["dim"], "song");
        assert_eq!(j["items"].as_array().unwrap().len(), 3);
        assert_eq!(j["items"][0]["name"], "Song A");
        assert_eq!(j["items"][0]["play_count"], 3);
        assert_eq!(j["items"][1]["name"], "Song B");
        assert_eq!(j["items"][1]["play_count"], 2);
        assert_eq!(j["items"][2]["name"], "Song C");
        assert_eq!(j["items"][2]["play_count"], 1);

        let result = stats_get_top("artist".into(), "all".into(), 10)
            .await
            .expect("get_top failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["dim"], "artist");
        assert_eq!(j["items"].as_array().unwrap().len(), 2);
        assert_eq!(j["items"][0]["name"], "Artist X");
        assert_eq!(j["items"][0]["play_count"], 5);
        assert_eq!(j["items"][1]["name"], "Artist Y");
        assert_eq!(j["items"][1]["play_count"], 1);

        let result = stats_get_top("album".into(), "all".into(), 10)
            .await
            .expect("get_top failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["dim"], "album");
        assert_eq!(j["items"].as_array().unwrap().len(), 2);
        // album-1 (5 plays) outranks album-2 (1 play) — both named "Album One"
        assert_eq!(j["items"][0]["name"], "Album One");
        assert_eq!(j["items"][0]["album_id"], "album-1");
        assert_eq!(j["items"][0]["play_count"], 5);
        assert_eq!(j["items"][1]["name"], "Album One");
        assert_eq!(j["items"][1]["album_id"], "album-2");
        assert_eq!(j["items"][1]["play_count"], 1);

        let result = stats_get_timeline("all".into())
            .await
            .expect("get_timeline failed");
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

        let result = stats_get_recent(10, 0).await.expect("get_recent failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["items"].as_array().unwrap().len(), 6);
        assert_eq!(j["items"][0]["song_hash"], "hashC");
        assert_eq!(j["items"][1]["song_hash"], "hashB");

        let result = stats_get_recent(3, 0).await.expect("get_recent failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["items"].as_array().unwrap().len(), 3);

        let result = stats_get_recommendations(5)
            .await
            .expect("get_recommendations failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert!(j["items"].is_array());
        assert!(!j["items"].as_array().unwrap().is_empty());
        assert_eq!(j["items"][0]["singer"], "Artist X");

        // B06: invalid input is no longer a silent no-op — the command
        // surfaces a diagnosable error and the stored counts stay unchanged.
        let invalid = stats_record_play("not valid json".into()).await;
        assert!(
            matches!(invalid, Err(ref e) if e.contains("stats_bad_json")),
            "invalid json must surface as stats_bad_json, got {:?}",
            invalid
        );
        stats_record_play(make_record(
            "",
            "No Identity",
            "Artist X",
            "album-1",
            "Album One",
            240.0,
            true,
            240.0,
            day2,
        ))
        .await
        .expect_err("empty song identity must be rejected");
        stats_record_play(make_record(
            "hashNeg",
            "Neg",
            "Artist X",
            "album-1",
            "Album One",
            240.0,
            true,
            240.0,
            -5,
        ))
        .await
        .expect_err("negative played_at must be rejected");
        // Below-threshold remains a NORMAL outcome (Ok) — short listens are
        // not counted and must not disturb playback.
        stats_record_play(make_record(
            "hashShort",
            "Short",
            "Artist Z",
            "album-s",
            "Short Album",
            120.0,
            false,
            30.0,
            day2,
        ))
        .await
        .expect("below-threshold listen is a normal outcome");
        let result = stats_get_summary("all".into())
            .await
            .expect("get_summary failed");
        let j: serde_json::Value = serde_json::from_str(&result).unwrap();
        assert_eq!(j["total_plays"], 6);
        assert_eq!(j["unique_songs"], 3);

        backend_api::shutdown_c_api();
        let _ = std::fs::remove_dir_all(&app_data_dir);
    }

    #[tokio::test]
    // Same intentional serialization as test_stats_ffi_end_to_end.
    #[allow(clippy::await_holding_lock)]
    async fn test_stats_returns_err_without_dll() {
        let _lock = backend_api::lock_test_c_api();
        backend_api::shutdown_c_api();
        let result = stats_get_summary("all".into()).await;
        assert!(result.is_err());
    }

    // The test-only C-API mutex is held across awaits on purpose: it
    // serializes DLL init/shutdown for the whole test body, and it is never
    // taken inside the dispatched closures.
    #[tokio::test(flavor = "multi_thread")]
    #[allow(clippy::await_holding_lock)]
    async fn shutdown_stays_bounded_while_abandoned_stats_closure_holds_read_guard() {
        // Stage 4 shutdown-boundary verification: a stats dispatch abandoned
        // at the dispatcher timeout leaves its closure RUNNING with the C API
        // read guard held. backend_api::shutdown_c_api must return in bounded
        // time (5s try-write loop) WITHOUT unloading the DLL, and the gated
        // closure must then finish safely.
        let _lock = backend_api::lock_test_c_api();

        let dll_path = find_dll();
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let app_data_dir = std::env::temp_dir().join(format!(
            "bottlemusic_stats_shutdown_{}_{}",
            std::process::id(),
            stamp
        ));
        let _ = std::fs::remove_dir_all(&app_data_dir);
        std::fs::create_dir_all(&app_data_dir).unwrap();
        backend_api::shutdown_c_api();
        backend_api::init_with_paths(&dll_path, Some(app_data_dir.to_str().unwrap()))
            .expect("Failed to init C API");

        struct GateDrop(Arc<std::sync::atomic::AtomicBool>);
        impl Drop for GateDrop {
            fn drop(&mut self) {
                self.0.store(true, std::sync::atomic::Ordering::Release);
            }
        }
        let gate = Arc::new(AtomicBool::new(false));
        let entered = Arc::new(AtomicBool::new(false));
        let completed = Arc::new(AtomicBool::new(false));
        let _gate_guard = GateDrop(gate.clone());

        let task = tokio::spawn({
            let gate = gate.clone();
            let entered = entered.clone();
            let completed = completed.clone();
            async move {
                // The dispatcher times out at 250ms while the closure waits
                // on the gate — the read guard stays held across that
                // abandonment (the exact shutdown-race shape from the plan).
                let _ = crate::dispatch_stats_ffi_with(
                    &crate::stats_ffi_semaphore(),
                    &crate::stats_ffi_waiter_semaphore(),
                    Duration::from_millis(250),
                    move |control| {
                        let _guard = backend_api::api_handle_for_request(&control)?;
                        control.claim_dll_start()?;
                        entered.store(true, std::sync::atomic::Ordering::Release);
                        while !gate.load(std::sync::atomic::Ordering::Acquire) {
                            std::thread::sleep(Duration::from_millis(5));
                        }
                        completed.store(true, std::sync::atomic::Ordering::Release);
                        Ok(())
                    },
                )
                .await;
            }
        });

        let entered_ok = wait_until(Duration::from_secs(5), || entered.load(Ordering::Acquire));
        assert!(
            entered_ok,
            "the closure did not acquire the read guard in time"
        );
        tokio::time::sleep(Duration::from_millis(400)).await; // dispatcher deadline (250ms) has fired

        // Shutdown while the abandoned closure still holds the read guard:
        // bounded return (5s try-write loop), DLL left mapped.
        let start = std::time::Instant::now();
        backend_api::shutdown_c_api();
        let elapsed = start.elapsed();
        assert!(
            elapsed < Duration::from_secs(8),
            "shutdown returned in {:?}; the 5s try-write loop must stay bounded",
            elapsed
        );

        // The DLL was NOT unloaded: the gated closure finishes safely once
        // the barrier drops (a crashed/unmapped DLL would fail here).
        drop(_gate_guard);
        let completed_ok = wait_until(Duration::from_secs(5), || completed.load(Ordering::Acquire));
        assert!(
            completed_ok,
            "the abandoned closure must finish safely after shutdown"
        );

        task.await.unwrap();
        // Now uncontended: a second shutdown completes the real teardown.
        backend_api::shutdown_c_api();
        let _ = std::fs::remove_dir_all(&app_data_dir);
    }

    fn wait_until(budget: Duration, cond: impl Fn() -> bool) -> bool {
        let deadline = std::time::Instant::now() + budget;
        while !cond() {
            if std::time::Instant::now() >= deadline {
                return false;
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        true
    }
}
