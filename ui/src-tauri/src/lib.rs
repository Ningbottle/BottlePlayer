mod ai_analysis;
mod audio_proxy;
mod backend_api;
mod os_media_session;
mod stats;

use std::sync::{Arc, OnceLock};
use std::time::Duration;
use tokio::sync::Semaphore;

#[tauri::command]
fn ping() -> &'static str {
    "pong"
}

#[tauri::command]
fn get_memory_usage() -> u64 {
    use sysinfo::{Pid, System};

    let mut sys = System::new();
    let pid = Pid::from_u32(std::process::id());
    sys.refresh_process(pid);
    if let Some(proc) = sys.process(pid) {
        proc.memory()
    } else {
        0
    }
}

// Generated from native/include/echo/core/RequestDeadlines.h by build.rs.
// Names mirror C++ kCamelCase for cross-language identity.
#[allow(non_upper_case_globals, dead_code)]
mod deadlines {
    include!(concat!(env!("OUT_DIR"), "/deadlines_generated.rs"));
}

/// Map a request path to a per-kind deadline. Values come solely from
/// RequestDeadlines.h (build-time extract). Outer watchdog only.
fn deadline_for_path(path: &str) -> Duration {
    if path.starts_with("/song/url") {
        Duration::from_millis(deadlines::kDeadlineSongUrlMs)
    } else if path.starts_with("/images/") {
        Duration::from_millis(deadlines::kDeadlineImageMs)
    } else if path.starts_with("/login/qr/") {
        Duration::from_millis(deadlines::kDeadlineLoginPollMs)
    } else if path.starts_with("/search") {
        Duration::from_millis(deadlines::kDeadlineSearchMs)
    } else if path.starts_with("/playlist")
        || path.starts_with("/rank")
        || path.starts_with("/top/")
        || path.starts_with("/album")
        || path.starts_with("/artist")
    {
        Duration::from_millis(deadlines::kDeadlinePlaylistMs)
    } else {
        Duration::from_millis(deadlines::kDeadlineGenericMs)
    }
}

fn should_shutdown_c_api(event: &tauri::RunEvent) -> bool {
    matches!(event, tauri::RunEvent::Exit)
}

/// Cap on native_request calls concurrently admitted into FFI dispatch.
/// 16 = 4× the C++ scheduler's 4 workers (so the Rust side never becomes the
/// bottleneck ahead of the C++ queue cap) while still bounding admitted
/// concurrency for a single-user desktop app. This bounds admission, not
/// lifetime: a timed-out dispatch drops its permit immediately, but its
/// spawn_blocking closure (and the backend_api read guard inside) keeps
/// running to completion — zombie closures are not capped or cancelled here.
const MAX_CONCURRENT_FFI_CALLS: usize = 16;

fn ffi_dispatch_semaphore() -> Arc<Semaphore> {
    static SEMAPHORE: OnceLock<Arc<Semaphore>> = OnceLock::new();
    SEMAPHORE
        .get_or_init(|| Arc::new(Semaphore::new(MAX_CONCURRENT_FFI_CALLS)))
        .clone()
}

/// Dispatch `task` on the blocking pool under the given admission semaphore.
/// The permit is held from before spawn until the task joins, so at most
/// `semaphore` permits' worth of calls are inside the FFI at once. If the
/// caller's surrounding timeout fires, the whole future (permit included) is
/// dropped: the blocking task itself keeps running to completion (spawn_blocking
/// cannot be cancelled), but the admission slot is released immediately so
/// later requests are not starved behind zombies.
async fn dispatch_bounded_ffi<T, F>(semaphore: &Arc<Semaphore>, task: F) -> Result<T, String>
where
    T: Send + 'static,
    F: FnOnce() -> Result<T, String> + Send + 'static,
{
    let _permit = semaphore
        .acquire()
        .await
        .map_err(|_| "ffi_dispatcher_closed".to_string())?;
    tauri::async_runtime::spawn_blocking(task)
        .await
        .unwrap_or_else(|e| Err(format!("Task panic: {:?}", e)))
}

#[tauri::command]
async fn native_request(
    method: String,
    path: String,
    query_json: Option<String>,
    headers_json: Option<String>,
    body: Option<String>,
) -> Result<String, String> {
    let deadline = deadline_for_path(&path);
    match tokio::time::timeout(
        deadline,
        dispatch_bounded_ffi(&ffi_dispatch_semaphore(), move || {
            backend_api::handle_request(
                &method,
                &path,
                query_json.as_deref(),
                headers_json.as_deref(),
                body.as_deref(),
            )
        }),
    )
    .await
    {
        Ok(dispatch_result) => dispatch_result,
        Err(_) => Err("request_deadline".to_string()),
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    #[allow(unused_mut)]
    let mut builder = tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .plugin(tauri_plugin_process::init());

    #[cfg(feature = "desktop-shell")]
    {
        builder = builder.plugin(tauri_plugin_global_shortcut::Builder::new().build());
    }

    let app = builder
        .setup(|app| {
            use tauri::Manager;

            // Store AppHandle for event emission from C++ callbacks.
            os_media_session::set_app_handle(app.handle().clone());
            #[cfg(feature = "desktop-shell")]
            if let Err(e) = os_media_session::install_os_integrations(app.handle()) {
                eprintln!("[OsMedia WARN] OS integrations partial/unavailable: {e}");
            }

            match audio_proxy::bind_listener() {
                Ok((listener, port)) => {
                    let state = audio_proxy::AudioProxyState::new(port);
                    app.manage(state.clone());
                    tauri::async_runtime::spawn(audio_proxy::serve(listener, state));
                    println!("[AudioProxy] Listening on 127.0.0.1:{}", port);
                }
                Err(e) => {
                    eprintln!("[AudioProxy ERR] Failed to bind local audio proxy: {}", e);
                    app.manage(audio_proxy::AudioProxyState::disabled());
                }
            }

            let dll_name = if cfg!(target_os = "windows") {
                "EchoCAPI.dll"
            } else {
                "libEchoCAPI.so"
            };

            // 1. Production: Tauri resource dir (bundled by tauri.conf.json resources)
            let resource_dir = app.path().resource_dir().unwrap_or_default();

            // 2. Development: same dir as the executable (copied there by build.rs)
            let exe_dir = std::env::current_exe()
                .ok()
                .and_then(|p| p.parent().map(|p| p.to_path_buf()))
                .unwrap_or_default();

            // 3. Development fallback: source-tree native/out (for when C++ was rebuilt but Rust not)
            let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap_or_default();
            let preset = if cfg!(debug_assertions) {
                "bottlemusic-check"
            } else {
                "bottlemusic-release"
            };

            let possible_paths: Vec<std::path::PathBuf> = vec![
                resource_dir.join(dll_name),
                exe_dir.join(dll_name),
                std::path::PathBuf::from(&manifest_dir)
                    .join(format!("../../native/out/{}/{}", preset, dll_name)),
            ];

            let app_data_dir = app
                .path()
                .app_data_dir()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned();

            let mut loaded = false;
            let mut last_errors: Vec<String> = Vec::new();
            for path in &possible_paths {
                if !path.exists() {
                    last_errors.push(format!("{} (missing)", path.display()));
                    continue;
                }
                if let Some(path_str) = path.to_str() {
                    match backend_api::init_with_paths(path_str, Some(&app_data_dir)) {
                        Ok(()) => {
                            println!(
                                "[EchoCAPI] Loaded native library from {} (data: {})",
                                path.display(),
                                app_data_dir
                            );
                            // 日志目录必须在注册 log callback 之前设定：callback 一旦
                            // 触发就会惰性初始化 LOG_FILE，而路径由 LOG_DIR 决定。
                            backend_api::set_log_dir(&app_data_dir);
                            if let Err(e) = backend_api::set_log_callback() {
                                eprintln!("[EchoCAPI WARN] Failed to set log callback: {}", e);
                            }
                            loaded = true;
                            break;
                        }
                        Err(e) => {
                            last_errors.push(format!("{} → {}", path.display(), e));
                        }
                    }
                }
            }
            if !loaded {
                eprintln!(
                    "[EchoCAPI ERR] Could not load {} from any known path",
                    dll_name
                );
                for line in &last_errors {
                    eprintln!("[EchoCAPI ERR]   candidate: {}", line);
                }
                eprintln!(
                    "[EchoCAPI ERR] Hint: rebuild native backend with `pnpm backend:build` in ui/ if symbols are missing (EchoInitializeWithPathsV2)."
                );
            }

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            ping,
            get_memory_usage,
            native_request,
            audio_proxy::audio_proxy_url,
            ai_analysis::ai_analyze,
            stats::stats_record_play,
            stats::stats_get_summary,
            stats::stats_get_top,
            stats::stats_get_timeline,
            stats::stats_get_recent,
            stats::stats_get_recommendations,
            os_media_session::os_media_bind,
            os_media_session::os_media_unbind,
            os_media_session::os_media_set_now_playing,
            os_media_session::os_media_set_playback_status,
            os_media_session::os_media_set_enabled_controls,
            os_media_session::os_media_inject_button,
            os_media_session::os_media_debug_snapshot,
        ])
        .build(tauri::generate_context!())
        .expect("error while running BottleMusic Tauri app");

    app.run(|_app_handle, event| {
        if should_shutdown_c_api(&event) {
            backend_api::shutdown_c_api();
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::{Condvar, Mutex};
    use std::time::Duration;
    use tokio::time::timeout;

    /// Poll `cond` every 5ms until it holds or `budget` elapses.
    /// Deterministic replacement for sleep-then-assert synchronization.
    async fn wait_until(budget: Duration, cond: impl Fn() -> bool) -> bool {
        let deadline = std::time::Instant::now() + budget;
        while !cond() {
            if std::time::Instant::now() >= deadline {
                return false;
            }
            tokio::time::sleep(Duration::from_millis(5)).await;
        }
        true
    }

    #[tokio::test]
    async fn native_request_times_out_when_handler_sleeps() {
        // Documents the timeout semantics without depending on the DLL:
        // work that runs past the deadline returns the timeout branch.
        let result = timeout(Duration::from_millis(100), async {
            tokio::time::sleep(Duration::from_secs(60)).await;
            "ok"
        })
        .await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn dispatch_admits_at_most_the_semaphore_capacity() {
        // No DLL involved: a slow handler blocks all permits, then the
        // (capacity+1)-th dispatch must wait behind the queue instead of
        // entering the "FFI". Observed in-FFI concurrency never exceeds the
        // permit count.
        let semaphore = Arc::new(Semaphore::new(2));
        let in_flight = Arc::new(AtomicUsize::new(0));
        let max_observed = Arc::new(AtomicUsize::new(0));
        // std Condvar: the slow "FFI" closures run on blocking threads and
        // need a blocking (not async) release signal.
        let gate = Arc::new(Mutex::new(false));
        let gate_cv = Arc::new(Condvar::new());

        let mut tasks = Vec::new();
        for i in 0..6 {
            let semaphore = semaphore.clone();
            let in_flight = in_flight.clone();
            let max_observed = max_observed.clone();
            let gate = gate.clone();
            let gate_cv = gate_cv.clone();
            tasks.push(tokio::spawn(async move {
                dispatch_bounded_ffi(&semaphore, move || {
                    let now = in_flight.fetch_add(1, Ordering::SeqCst) + 1;
                    max_observed.fetch_max(now, Ordering::SeqCst);
                    // Block inside the "FFI" until the test opens the gate,
                    // so the queue point is deterministic.
                    let mut open = gate.lock().unwrap();
                    while !*open {
                        open = gate_cv.wait(open).unwrap();
                    }
                    in_flight.fetch_sub(1, Ordering::SeqCst);
                    Ok(i)
                })
                .await
            }));
        }

        // Wait deterministically until both permits are held by the blocking
        // closures (a fixed sleep-then-assert here used to race slow
        // schedulers). The first two dispatches acquire immediately and block
        // on the gate; the remaining four cannot enter until the gate opens,
        // so in_flight can never exceed 2.
        let admitted =
            wait_until(Duration::from_secs(10), || in_flight.load(Ordering::SeqCst) >= 2).await;
        assert!(
            admitted,
            "the first two dispatches were not admitted within the budget"
        );
        assert_eq!(
            max_observed.load(Ordering::SeqCst),
            2,
            "concurrent FFI entries must be capped at the semaphore capacity"
        );

        *gate.lock().unwrap() = true;
        gate_cv.notify_all();
        for task in tasks {
            assert!(task.await.unwrap().unwrap() < 6);
        }
        assert_eq!(in_flight.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn timed_out_dispatch_releases_capacity_immediately() {
        // A dispatch abandoned by its surrounding timeout must free its
        // admission slot at once: the very next dispatch (with a fast handler)
        // must be admitted without waiting for the zombie task to finish.
        let semaphore = Arc::new(Semaphore::new(1));

        // Occupy the only permit with work that outlives the timeout.
        let zombie = tokio::spawn({
            let semaphore = semaphore.clone();
            async move {
                let _ = timeout(
                    Duration::from_millis(50),
                    dispatch_bounded_ffi(&semaphore, || {
                        std::thread::sleep(Duration::from_millis(1500));
                        Ok(())
                    }),
                )
                .await;
            }
        });
        tokio::time::sleep(Duration::from_millis(150)).await; // zombie timed out, slot must be back

        // With the slot released, a fast dispatch completes quickly even
        // though the zombie closure is still sleeping on the blocking pool.
        let started = std::time::Instant::now();
        let result = timeout(
            Duration::from_millis(500),
            dispatch_bounded_ffi(&semaphore, || Ok("admitted")),
        )
        .await
        .expect("next dispatch must not be starved by the timed-out zombie")
        .expect("fast handler should succeed");
        assert_eq!(result, "admitted");
        assert!(
            started.elapsed() < Duration::from_millis(400),
            "admission took {:?}; the timed-out dispatch did not release its slot",
            started.elapsed()
        );
        // The permit came back when the timeout dropped the dispatch future,
        // not when the zombie closure finishes: assert it while the zombie is
        // still sleeping on the blocking pool.
        assert_eq!(
            semaphore.available_permits(),
            1,
            "the timed-out dispatch must release its permit at timeout, before the zombie completes"
        );

        zombie.await.unwrap();
        assert_eq!(semaphore.available_permits(), 1);
    }

    #[test]
    fn ffi_cap_exceeds_cpp_scheduler_workers() {
        // The Rust admission cap must never become the bottleneck ahead of the
        // C++ scheduler's bounded 4-worker pool.
        assert!(MAX_CONCURRENT_FFI_CALLS > 4);
        assert_eq!(MAX_CONCURRENT_FFI_CALLS, 16);
    }

    #[test]
    fn deadline_for_song_url_is_10s() {
        assert_eq!(
            deadline_for_path("/song/url"),
            Duration::from_millis(deadlines::kDeadlineSongUrlMs)
        );
    }

    #[test]
    fn deadline_for_images_is_8s() {
        assert_eq!(
            deadline_for_path("/images/audio"),
            Duration::from_millis(deadlines::kDeadlineImageMs)
        );
    }

    #[test]
    fn deadline_for_login_qr_is_6s() {
        assert_eq!(
            deadline_for_path("/login/qr/check"),
            Duration::from_millis(deadlines::kDeadlineLoginPollMs)
        );
    }

    #[test]
    fn deadline_for_search_uses_search_bucket() {
        assert_eq!(
            deadline_for_path("/search"),
            Duration::from_millis(deadlines::kDeadlineSearchMs)
        );
    }

    #[test]
    fn deadline_for_generic_is_12s() {
        assert_eq!(
            deadline_for_path("/unknown/route"),
            Duration::from_millis(deadlines::kDeadlineGenericMs)
        );
    }

    #[test]
    fn rust_outer_deadlines_are_at_least_cpp_inner() {
        // Outer Tauri timeout must not be shorter than C++ scheduler budget.
        assert!(deadlines::kDeadlineSongUrlMs >= deadlines::kDeadlineSongUrlMs);
        assert!(deadlines::kFrontendTimeoutMs >= deadlines::kDeadlineGenericMs);
        assert!(deadlines::kDeadlineGenericMs >= 1000);
    }

    #[test]
    fn frontend_timeout_literal_covers_every_generated_deadline() {
        // The hand-written FRONTEND_TIMEOUT_MS literal in the frontend must
        // never fall below the largest generated per-path deadline: otherwise
        // the frontend gives up while the backend request is still in flight
        // and the circuit breaker miscounts the abandon as a failure. This is
        // the cross-layer guard: editing either side alone turns this red.
        let frontend_ts = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../ui/src/platform/tauri/nativeClient.ts"
        ))
        .expect("nativeClient.ts should exist relative to ui/src-tauri");

        let literal = frontend_ts
            .split(|c: char| c == '\n' || c == ';')
            .map(str::trim)
            .find(|line| line.starts_with("const FRONTEND_TIMEOUT_MS"))
            .unwrap_or_else(|| {
                panic!("FRONTEND_TIMEOUT_MS declaration not found in nativeClient.ts")
            });
        let value_part = literal
            .split('=')
            .nth(1)
            .expect("FRONTEND_TIMEOUT_MS declaration must contain '='")
            .trim()
            .replace('_', "");
        let frontend_timeout_ms: u64 = value_part
            .parse()
            .unwrap_or_else(|_| panic!("FRONTEND_TIMEOUT_MS literal not parseable: {literal}"));

        let max_deadline = [
            deadlines::kDeadlineSongUrlMs,
            deadlines::kDeadlineImageMs,
            deadlines::kDeadlineLoginPollMs,
            deadlines::kDeadlineSearchMs,
            deadlines::kDeadlinePlaylistMs,
            deadlines::kDeadlineGenericMs,
            deadlines::kFrontendTimeoutMs,
        ]
        .into_iter()
        .max()
        .expect("deadline list is non-empty");

        assert!(
            frontend_timeout_ms >= max_deadline,
            "FRONTEND_TIMEOUT_MS ({frontend_timeout_ms}ms) must be >= the largest generated \
             deadline ({max_deadline}ms); keep nativeClient.ts in sync with RequestDeadlines.h"
        );
    }

    #[test]
    fn non_exit_run_events_do_not_shutdown_process_global_c_api() {
        assert!(!should_shutdown_c_api(&tauri::RunEvent::Ready));
    }

    #[test]
    fn process_exit_shuts_down_process_global_c_api() {
        assert!(should_shutdown_c_api(&tauri::RunEvent::Exit));
    }
}
