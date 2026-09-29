mod ai_analysis;
mod audio_proxy;
mod backend_api;
mod os_media_session;
mod stats;

use std::sync::{Arc, OnceLock};
use std::time::{Duration, Instant};
use tokio::sync::{Semaphore, TryAcquireError};

#[cfg(test)]
use std::sync::atomic::{AtomicUsize, Ordering};

#[cfg(test)]
struct FfiDispatchTestMetrics {
    admitted: AtomicUsize,
    blocking_started: AtomicUsize,
    blocking_finished: AtomicUsize,
    blocking_live: AtomicUsize,
    blocking_peak: AtomicUsize,
}

#[cfg(test)]
fn reset_ffi_dispatch_test_metrics(metrics: &FfiDispatchTestMetrics) {
    metrics.admitted.store(0, Ordering::SeqCst);
    metrics.blocking_started.store(0, Ordering::SeqCst);
    metrics.blocking_finished.store(0, Ordering::SeqCst);
    metrics.blocking_live.store(0, Ordering::SeqCst);
    metrics.blocking_peak.store(0, Ordering::SeqCst);
}

#[cfg(test)]
struct BlockingClosureTestGuard(Arc<FfiDispatchTestMetrics>);

#[cfg(test)]
impl BlockingClosureTestGuard {
    fn entered(metrics: Arc<FfiDispatchTestMetrics>) -> Self {
        metrics.blocking_started.fetch_add(1, Ordering::SeqCst);
        let live = metrics.blocking_live.fetch_add(1, Ordering::SeqCst) + 1;
        metrics.blocking_peak.fetch_max(live, Ordering::SeqCst);
        Self(metrics)
    }
}

#[cfg(test)]
impl Drop for BlockingClosureTestGuard {
    fn drop(&mut self) {
        self.0.blocking_live.fetch_sub(1, Ordering::SeqCst);
        self.0.blocking_finished.fetch_add(1, Ordering::SeqCst);
    }
}

#[tauri::command]
fn ping() -> Result<&'static str, String> {
    ping_result(backend_api::is_initialized())
}

fn ping_result(backend_available: bool) -> Result<&'static str, String> {
    if backend_available {
        Ok("pong")
    } else {
        Err("backend_unavailable: C API not loaded".into())
    }
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
    } else if cfg!(debug_assertions) && path.starts_with("/diagnostics/signature-family") {
        // Debug-only diagnostic probe; Release falls through to the generic
        // bucket because the native route does not exist there.
        Duration::from_millis(deadlines::kDeadlineSignatureFamilyProbeMs)
    } else {
        Duration::from_millis(deadlines::kDeadlineGenericMs)
    }
}

fn should_shutdown_c_api(event: &tauri::RunEvent) -> bool {
    matches!(event, tauri::RunEvent::Exit)
}

fn append_native_dll_candidate(
    candidates: &mut Vec<std::path::PathBuf>,
    diagnostics: &mut Vec<String>,
    source: &str,
    candidate: std::path::PathBuf,
) {
    if !candidate.is_absolute() {
        diagnostics.push(format!(
            "{source}_relative_path_rejected: {}",
            candidate.display()
        ));
    } else if !candidate.is_file() {
        diagnostics.push(format!("{source}_dll_missing: {}", candidate.display()));
    } else if !candidates.contains(&candidate) {
        candidates.push(candidate);
    }
}

/// Keep candidate resolution independent: a failed resource-dir lookup must
/// not hide a valid executable-adjacent or source-tree DLL. Only absolute
/// existing files may reach Library::new, so a failed base lookup can never
/// turn the process working directory into a DLL search path.
fn native_dll_candidates(
    resource_dir: Result<std::path::PathBuf, String>,
    executable_path: Result<std::path::PathBuf, String>,
    manifest_dir: Result<std::path::PathBuf, String>,
    dll_name: &str,
    preset: &str,
) -> (Vec<std::path::PathBuf>, Vec<String>) {
    let mut candidates = Vec::new();
    let mut diagnostics = Vec::new();

    match resource_dir {
        Ok(directory) => append_native_dll_candidate(
            &mut candidates,
            &mut diagnostics,
            "resource_dir",
            directory.join(dll_name),
        ),
        Err(error) => diagnostics.push(format!("resource_dir_unavailable: {error}")),
    }

    match executable_path {
        Ok(path) => match path.parent() {
            Some(directory) => append_native_dll_candidate(
                &mut candidates,
                &mut diagnostics,
                "executable_dir",
                directory.join(dll_name),
            ),
            None => diagnostics.push("executable_dir_unavailable: executable has no parent".into()),
        },
        Err(error) => diagnostics.push(format!("executable_path_unavailable: {error}")),
    }

    match manifest_dir {
        Ok(directory) => append_native_dll_candidate(
            &mut candidates,
            &mut diagnostics,
            "manifest_dir",
            directory.join(format!("../../native/out/{preset}/{dll_name}")),
        ),
        Err(error) => diagnostics.push(format!("manifest_dir_unavailable: {error}")),
    }

    (candidates, diagnostics)
}

/// Convert Tauri's per-user storage directory to the UTF-8 path required by
/// EchoInitializeWithPathsV2. An absent, empty, or unrepresentable path must
/// fail closed: passing an empty C string makes C++ create a relative database
/// in the process working directory.
fn encode_app_data_dir(path: Result<std::path::PathBuf, String>) -> Result<String, String> {
    let path = path.map_err(|error| format!("app_data_dir_unavailable: {error}"))?;
    let path = path
        .to_str()
        .filter(|value| !value.is_empty())
        .ok_or_else(|| {
            "app_data_dir_unavailable: path is empty or cannot be encoded as UTF-8".to_string()
        })?;
    Ok(path.to_owned())
}

/// Concurrent request callers allowed to enter the Rust dispatcher.
const MAX_CONCURRENT_FFI_CALLS: usize = 16;
/// Async requests allowed to wait for an active caller slot.
const MAX_QUEUED_FFI_CALLS: usize = 16;
/// Hard cap on live blocking closures, including those queued by Tokio,
/// waiting for the API read lock, or still inside the DLL. The extra cohort
/// allows one full set of 16 timed-out closures to coexist with up to 16 new
/// calls. This is a selected recovery margin, not a native-worker-derived cap.
const MAX_LIVE_FFI_CLOSURES: usize = 32;

struct FfiDispatcher {
    active: Arc<Semaphore>,
    waiters: Arc<Semaphore>,
    live: Arc<Semaphore>,
    #[cfg(test)]
    test_metrics: Arc<FfiDispatchTestMetrics>,
}

impl FfiDispatcher {
    fn new(active: usize, waiters: usize, live: usize) -> Self {
        Self {
            active: Arc::new(Semaphore::new(active)),
            waiters: Arc::new(Semaphore::new(waiters)),
            live: Arc::new(Semaphore::new(live)),
            #[cfg(test)]
            test_metrics: Arc::new(FfiDispatchTestMetrics {
                admitted: AtomicUsize::new(0),
                blocking_started: AtomicUsize::new(0),
                blocking_finished: AtomicUsize::new(0),
                blocking_live: AtomicUsize::new(0),
                blocking_peak: AtomicUsize::new(0),
            }),
        }
    }
}

fn ffi_dispatcher() -> &'static FfiDispatcher {
    static DISPATCHER: OnceLock<FfiDispatcher> = OnceLock::new();
    DISPATCHER.get_or_init(|| {
        FfiDispatcher::new(
            MAX_CONCURRENT_FFI_CALLS,
            MAX_QUEUED_FFI_CALLS,
            MAX_LIVE_FFI_CLOSURES,
        )
    })
}

#[cfg(test)]
fn ffi_dispatch_semaphore() -> Arc<Semaphore> {
    ffi_dispatcher().active.clone()
}

struct CancelPendingOnDrop(Arc<backend_api::RequestControl>);

impl Drop for CancelPendingOnDrop {
    fn drop(&mut self) {
        self.0.cancel_pending();
    }
}

/// Bound caller admission, async waiters, and physical closure lifetime
/// separately. The lifetime permit is moved into the closure before spawn,
/// so it covers Tokio's blocking queue and the backend read-lock wait too.
/// Dropping the caller releases only its active slot; a started DLL call keeps
/// its lifetime permit and read guard until the native call returns.
async fn dispatch_bounded_ffi<T, F>(
    dispatcher: &FfiDispatcher,
    control: Arc<backend_api::RequestControl>,
    task: F,
) -> Result<T, String>
where
    T: Send + 'static,
    F: FnOnce(Arc<backend_api::RequestControl>) -> Result<T, String> + Send + 'static,
{
    let _cancel_if_pending = CancelPendingOnDrop(control.clone());
    control.ensure_pending()?;

    // Active permits belong to the caller future and return at result/deadline.
    // A bounded waiter token limits futures queued behind the active cap.
    let active_permit = match dispatcher.active.clone().try_acquire_owned() {
        Ok(permit) => permit,
        Err(TryAcquireError::Closed) => return Err("ffi_dispatcher_closed".into()),
        Err(TryAcquireError::NoPermits) => {
            let waiter_permit = match dispatcher.waiters.clone().try_acquire_owned() {
                Ok(permit) => permit,
                Err(TryAcquireError::NoPermits) => return Err("ffi_overloaded".into()),
                Err(TryAcquireError::Closed) => return Err("ffi_dispatcher_closed".into()),
            };
            let active = tokio::time::timeout_at(
                tokio::time::Instant::from_std(control.deadline()),
                dispatcher.active.clone().acquire_owned(),
            )
            .await;
            drop(waiter_permit);
            match active {
                Ok(Ok(permit)) => permit,
                Ok(Err(_)) => return Err("ffi_dispatcher_closed".into()),
                Err(_) => return Err("request_deadline".into()),
            }
        }
    };

    // timeout_at may return a ready permit after its deadline. Check the same
    // monotonic deadline explicitly before scheduling native work.
    control.ensure_pending()?;

    // Once active admission succeeds, release the waiter token (in the branch
    // above) before checking the physical closure cap. Waiters do not consume
    // lifetime capacity; an admitted caller that cannot reserve a live slot
    // fails fast rather than accumulating another queue.
    // Do not create an unbounded set of JoinHandles: claim a lifetime slot
    // before any blocking closure can enter Tokio's blocking queue.
    let live_permit = match dispatcher.live.clone().try_acquire_owned() {
        Ok(permit) => permit,
        Err(TryAcquireError::NoPermits) => return Err("ffi_live_capacity".into()),
        Err(TryAcquireError::Closed) => return Err("ffi_dispatcher_closed".into()),
    };

    let deadline = control.deadline();
    let _active_permit = active_permit;
    #[cfg(test)]
    let test_metrics = dispatcher.test_metrics.clone();
    #[cfg(test)]
    test_metrics.admitted.fetch_add(1, Ordering::SeqCst);
    let join = tauri::async_runtime::spawn_blocking(move || {
        let _live_permit = live_permit;
        #[cfg(test)]
        let _measurement = BlockingClosureTestGuard::entered(test_metrics);
        control.ensure_pending()?;
        task(control)
    });

    match tokio::time::timeout_at(tokio::time::Instant::from_std(deadline), join).await {
        Ok(joined) if Instant::now() < deadline => {
            joined.unwrap_or_else(|e| Err(format!("Task panic: {:?}", e)))
        }
        Ok(_) | Err(_) => Err("request_deadline".into()),
    }
}

/// Dedicated cap for stats FFI calls (plan Stage 4 / F18). Retain the existing
/// four-call policy; it is an independent Rust limit and does not describe the
/// native scheduler's capacity.
const MAX_CONCURRENT_STATS_FFI: usize = 4;
/// Bound async stats commands queued behind the four physical stats calls.
const MAX_QUEUED_STATS_FFI: usize = 4;
/// Wall-clock bound for one stats dispatch, covering BOTH queueing (permit
/// acquisition) and execution. Healthy stats queries are sub-millisecond;
/// 3s absorbs pathological disk contention by orders of magnitude while
/// staying well under the frontend's 14s budget.
const STATS_FFI_TIMEOUT_MS: u64 = 3000;

fn stats_ffi_semaphore() -> Arc<Semaphore> {
    static SEMAPHORE: OnceLock<Arc<Semaphore>> = OnceLock::new();
    SEMAPHORE
        .get_or_init(|| Arc::new(Semaphore::new(MAX_CONCURRENT_STATS_FFI)))
        .clone()
}

fn stats_ffi_waiter_semaphore() -> Arc<Semaphore> {
    static SEMAPHORE: OnceLock<Arc<Semaphore>> = OnceLock::new();
    SEMAPHORE
        .get_or_init(|| Arc::new(Semaphore::new(MAX_QUEUED_STATS_FFI)))
        .clone()
}

/// Dispatch a stats FFI closure with SURVIVAL-bounded concurrency (plan
/// Stage 4): the permit is MOVED INTO the blocking closure and held until
/// the closure actually finishes. `timeout` covers queueing and execution —
/// a caller that exceeds it gets an error immediately, but the in-flight
/// closure keeps its permit (spawn_blocking cannot be cancelled), so the
/// number of live stats FFIs never exceeds the cap even when callers stop
/// waiting. A separate bounded waiter semaphore limits queued async callers.
///
/// Shutdown boundary: stats closures hold a read guard over the C API
/// handle. backend_api::shutdown_c_api tries the write lock for at most 5s;
/// if in-flight stats closures still hold read guards past that, it gives
/// up and leaves the DLL mapping alive for the OS to reclaim at process
/// exit — either way the DLL cannot unload mid-call. The dispatch timeout
/// abandons the caller, never the closure.
async fn dispatch_stats_ffi_with<T, F>(
    semaphore: &Arc<Semaphore>,
    waiter_semaphore: &Arc<Semaphore>,
    timeout: Duration,
    task: F,
) -> Result<T, String>
where
    T: Send + 'static,
    F: FnOnce(Arc<backend_api::RequestControl>) -> Result<T, String> + Send + 'static,
{
    let deadline = Instant::now() + timeout;
    let control = backend_api::RequestControl::new(deadline);
    let _cancel_if_pending = CancelPendingOnDrop(control.clone());
    let permit = match semaphore.clone().try_acquire_owned() {
        Ok(permit) => permit,
        Err(TryAcquireError::Closed) => return Err("stats_ffi_dispatcher_closed".into()),
        Err(TryAcquireError::NoPermits) => {
            let waiter = match waiter_semaphore.clone().try_acquire_owned() {
                Ok(permit) => permit,
                Err(TryAcquireError::NoPermits) => return Err("stats_ffi_overloaded".into()),
                Err(TryAcquireError::Closed) => return Err("stats_ffi_dispatcher_closed".into()),
            };
            let acquired = tokio::time::timeout_at(
                tokio::time::Instant::from_std(deadline),
                semaphore.clone().acquire_owned(),
            )
            .await;
            drop(waiter);
            match acquired {
                Ok(Ok(permit)) => permit,
                Ok(Err(_)) => return Err("stats_ffi_dispatcher_closed".into()),
                Err(_) => return Err("stats_ffi_queue_timeout".into()),
            }
        }
    };
    control
        .ensure_pending()
        .map_err(|_| "stats_ffi_timeout".to_string())?;
    let handle = tauri::async_runtime::spawn_blocking(move || {
        let _permit = permit; // held until the closure finishes (survival cap)
        control.ensure_pending()?;
        task(control)
    });
    match tokio::time::timeout_at(tokio::time::Instant::from_std(deadline), handle).await {
        Ok(joined) if Instant::now() < deadline => {
            match joined.unwrap_or_else(|e| Err(format!("Task panic: {:?}", e))) {
                Err(error) if error == "request_deadline" => Err("stats_ffi_timeout".into()),
                result => result,
            }
        }
        Ok(_) => Err("stats_ffi_timeout".into()),
        Err(_) => Err("stats_ffi_timeout".to_string()),
    }
}

async fn dispatch_stats_ffi<T, F>(task: F) -> Result<T, String>
where
    T: Send + 'static,
    F: FnOnce(Arc<backend_api::RequestControl>) -> Result<T, String> + Send + 'static,
{
    dispatch_stats_ffi_with(
        &stats_ffi_semaphore(),
        &stats_ffi_waiter_semaphore(),
        Duration::from_millis(STATS_FFI_TIMEOUT_MS),
        task,
    )
    .await
}

#[tauri::command]
async fn native_request(
    method: String,
    path: String,
    query_json: Option<String>,
    headers_json: Option<String>,
    body: Option<String>,
) -> Result<String, String> {
    let deadline = Instant::now() + deadline_for_path(&path);
    dispatch_native_request(method, path, query_json, headers_json, body, deadline).await
}

async fn dispatch_native_request(
    method: String,
    path: String,
    query_json: Option<String>,
    headers_json: Option<String>,
    body: Option<String>,
    deadline: Instant,
) -> Result<String, String> {
    let control = backend_api::RequestControl::new(deadline);
    dispatch_bounded_ffi(ffi_dispatcher(), control, move |control| {
        backend_api::handle_request_with_control(
            &method,
            &path,
            query_json.as_deref(),
            headers_json.as_deref(),
            body.as_deref(),
            control,
        )
    })
    .await
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

            // T2: run identity before Native init so hangs during setup still have a marker.
            let run_id = format!(
                "r{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|d| d.as_millis())
                    .unwrap_or(0)
            );
            eprintln!(
                "[Boot] run_id={} pid={} milestone=setup_enter",
                run_id,
                std::process::id()
            );

            // Store AppHandle for event emission from C++ callbacks.
            os_media_session::set_app_handle(app.handle().clone());
            eprintln!("[Boot] run_id={} milestone=os_media_handle_set", run_id);
            #[cfg(feature = "desktop-shell")]
            if let Err(e) = os_media_session::install_os_integrations(app.handle()) {
                eprintln!("[OsMedia WARN] OS integrations partial/unavailable: {e}");
            }
            eprintln!("[Boot] run_id={} milestone=os_integrations_done", run_id);

            match audio_proxy::bind_listener() {
                Ok((listener, port)) => {
                    let state = audio_proxy::AudioProxyState::new(port);
                    app.manage(state.clone());
                    tauri::async_runtime::spawn(audio_proxy::serve(listener, state));
                    println!("[AudioProxy] Listening on 127.0.0.1:{}", port);
                    eprintln!(
                        "[Boot] run_id={} milestone=audio_proxy_bound port={}",
                        run_id, port
                    );
                }
                Err(e) => {
                    eprintln!("[AudioProxy ERR] Failed to bind local audio proxy: {}", e);
                    app.manage(audio_proxy::AudioProxyState::disabled());
                    eprintln!("[Boot] run_id={} milestone=audio_proxy_disabled", run_id);
                }
            }

            let dll_name = if cfg!(target_os = "windows") {
                "EchoCAPI.dll"
            } else {
                "libEchoCAPI.so"
            };

            // Native library locations are resolved independently. A failed
            // resolver must never degrade into a relative path rooted at the
            // process working directory.
            let preset = if cfg!(debug_assertions) {
                "bottlemusic-check"
            } else {
                "bottlemusic-release"
            };
            let manifest_dir = if cfg!(debug_assertions) {
                Ok(std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR")))
            } else {
                Err("source-tree fallback disabled outside debug builds".to_string())
            };
            let (possible_paths, mut last_errors) = native_dll_candidates(
                app.path()
                    .resource_dir()
                    .map_err(|error| error.to_string()),
                std::env::current_exe().map_err(|error| error.to_string()),
                manifest_dir,
                dll_name,
                preset,
            );

            let app_data_dir = encode_app_data_dir(
                app.path()
                    .app_data_dir()
                    .map_err(|error| error.to_string()),
            );

            eprintln!(
                "[Boot] run_id={} milestone=native_init_begin data_dir_set={}",
                run_id,
                app_data_dir.is_ok()
            );

            let mut loaded = false;
            let data_dir_available = app_data_dir.is_ok();
            match app_data_dir {
                Ok(app_data_dir) => {
                    for path in &possible_paths {
                        if !path.exists() {
                            last_errors.push(format!("{} (missing)", path.display()));
                            continue;
                        }
                        let Some(path_str) = path.to_str() else {
                            last_errors.push(format!(
                                "{} (native library path cannot be encoded as UTF-8)",
                                path.display()
                            ));
                            continue;
                        };
                        match backend_api::init_with_paths(path_str, Some(&app_data_dir)) {
                            Ok(()) => {
                                println!(
                                    "[EchoCAPI] Loaded native library from {} (data: {})",
                                    path.display(),
                                    app_data_dir
                                );
                                eprintln!(
                                    "[Boot] run_id={} milestone=native_loaded path={}",
                                    run_id,
                                    path.display()
                                );
                                // 日志目录必须在注册 log callback 之前设定：callback 一旦
                                // 触发就会惰性初始化 LOG_FILE，而路径由 LOG_DIR 决定。
                                backend_api::set_log_dir(&app_data_dir);
                                if let Err(e) = backend_api::set_log_callback() {
                                    eprintln!("[EchoCAPI WARN] Failed to set log callback: {}", e);
                                }
                                eprintln!("[Boot] run_id={} milestone=log_callback_ready", run_id);
                                loaded = true;
                                break;
                            }
                            Err(e) => {
                                last_errors.push(format!("{} → {}", path.display(), e));
                            }
                        }
                    }
                }
                Err(error) => {
                    let diagnostic = format!(
                        "backend_unavailable: {error}; native initialization skipped to prevent a relative database path"
                    );
                    eprintln!("[EchoCAPI ERR] {diagnostic}");
                    last_errors.push(diagnostic);
                }
            }
            if !loaded {
                eprintln!(
                    "[EchoCAPI ERR] Native backend unavailable; no C API was initialized ({})",
                    dll_name,
                );
                for line in &last_errors {
                    eprintln!("[EchoCAPI ERR]   candidate: {}", line);
                }
                if data_dir_available {
                    eprintln!(
                        "[EchoCAPI ERR] Hint: rebuild native backend with `pnpm backend:build` in ui/ if symbols are missing (EchoInitializeWithPathsV2)."
                    );
                }
                eprintln!("[Boot] run_id={} milestone=native_load_failed", run_id);
            }

            eprintln!("[Boot] run_id={} milestone=setup_complete", run_id);
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

    #[test]
    fn ping_reports_native_backend_unavailability_to_the_network_banner() {
        assert_eq!(ping_result(true), Ok("pong"));
        assert_eq!(
            ping_result(false).unwrap_err(),
            "backend_unavailable: C API not loaded"
        );
    }

    #[test]
    fn native_dll_resolution_rejects_relative_fallbacks_but_keeps_trusted_sources() {
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        let root = std::env::temp_dir().join(format!(
            "bottlemusic-native-candidates-{}-{stamp}",
            std::process::id()
        ));
        let executable_dir = root.join("bin");
        std::fs::create_dir_all(&executable_dir).expect("create isolated candidate fixture");
        let dll = executable_dir.join("EchoCAPI.dll");
        std::fs::write(&dll, b"fixture").expect("create candidate fixture");

        let (candidates, diagnostics) = native_dll_candidates(
            Err("resource resolver failed".into()),
            Ok(executable_dir.join("BottleMusic.exe")),
            Ok(std::path::PathBuf::from("relative-source-tree")),
            "EchoCAPI.dll",
            "bottlemusic-check",
        );

        assert_eq!(candidates, vec![dll]);
        assert!(
            diagnostics
                .iter()
                .any(|line| line.starts_with("resource_dir_unavailable:")),
            "{diagnostics:?}"
        );
        assert!(
            diagnostics
                .iter()
                .any(|line| line.starts_with("manifest_dir_relative_path_rejected:")),
            "{diagnostics:?}"
        );
        std::fs::remove_dir_all(root).expect("remove isolated candidate fixture");
    }

    #[test]
    fn native_dll_resolution_never_returns_a_relative_candidate() {
        let (candidates, diagnostics) = native_dll_candidates(
            Ok(std::path::PathBuf::from("relative-resource")),
            Ok(std::path::PathBuf::from("relative-bin/BottleMusic.exe")),
            Err("manifest unavailable".into()),
            "EchoCAPI.dll",
            "bottlemusic-check",
        );

        assert!(candidates.is_empty(), "{candidates:?}");
        assert!(
            diagnostics
                .iter()
                .any(|line| line.starts_with("resource_dir_relative_path_rejected:")),
            "{diagnostics:?}"
        );
        assert!(
            diagnostics
                .iter()
                .any(|line| line.starts_with("executable_dir_relative_path_rejected:")),
            "{diagnostics:?}"
        );
    }

    #[test]
    fn app_data_dir_resolver_failure_is_explicit() {
        let error = encode_app_data_dir(Err("path resolver denied access".into()))
            .expect_err("an unavailable per-user data path must be rejected");
        assert!(error.starts_with("app_data_dir_unavailable:"), "{error}");
        assert!(error.contains("path resolver denied access"), "{error}");
    }

    #[test]
    fn app_data_dir_rejects_empty_path() {
        let error = encode_app_data_dir(Ok(std::path::PathBuf::new()))
            .expect_err("an empty path would send SQLite to the process working directory");
        assert!(error.contains("app_data_dir_unavailable"), "{error}");
    }

    #[test]
    fn app_data_dir_preserves_unicode_path_exactly() {
        let path = r"C:\Users\用户\AppData\Roaming\BottleMusic";
        assert_eq!(
            encode_app_data_dir(Ok(std::path::PathBuf::from(path))).unwrap(),
            path
        );
    }

    #[cfg(windows)]
    #[test]
    fn app_data_dir_rejects_unpaired_windows_surrogate() {
        use std::os::windows::ffi::OsStringExt;

        let path = std::path::PathBuf::from(std::ffi::OsString::from_wide(&[
            b'C' as u16,
            b':' as u16,
            b'\\' as u16,
            0xD800,
        ]));
        assert!(path.to_str().is_none(), "fixture must not be valid UTF-8");
        let error = encode_app_data_dir(Ok(path))
            .expect_err("lossy conversion could target a different user directory");
        assert!(error.contains("app_data_dir_unavailable"), "{error}");
    }

    // Release gated closures during unwinding too, so a failed assertion cannot
    // hang Tokio's blocking-pool shutdown.
    struct CondvarGateCleanup(Arc<Mutex<bool>>, Arc<Condvar>);

    impl Drop for CondvarGateCleanup {
        fn drop(&mut self) {
            *self.0.lock().unwrap_or_else(|p| p.into_inner()) = true;
            self.1.notify_all();
        }
    }

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

    type TestGate = Arc<(Mutex<(bool, bool)>, Condvar)>;

    fn new_test_gate() -> (TestGate, backend_api::ApiTestHook) {
        let gate = Arc::new((Mutex::new((false, false)), Condvar::new()));
        let hook_gate = gate.clone();
        let hook: backend_api::ApiTestHook = Arc::new(move || {
            let (state_lock, changed) = &*hook_gate;
            let mut state = state_lock.lock().unwrap_or_else(|p| p.into_inner());
            state.0 = true;
            changed.notify_all();
            while !state.1 {
                state = changed.wait(state).unwrap_or_else(|p| p.into_inner());
            }
        });
        (gate, hook)
    }

    async fn wait_gate_entered(gate: &TestGate) -> bool {
        wait_until(Duration::from_secs(5), || {
            gate.0.lock().unwrap_or_else(|p| p.into_inner()).0
        })
        .await
    }

    fn release_test_gate(gate: &TestGate) {
        let (state_lock, changed) = &**gate;
        let mut state = state_lock.lock().unwrap_or_else(|p| p.into_inner());
        state.1 = true;
        changed.notify_all();
    }

    fn setup_real_dll_app_data(label: &str) -> std::path::PathBuf {
        let dll_path =
            std::env::var("ECHO_CAPI_DLL").expect("real-DLL test requires ECHO_CAPI_DLL");
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let app_data_dir = std::env::temp_dir().join(format!(
            "bottlemusic-{label}-{}-{stamp}",
            std::process::id()
        ));
        std::fs::create_dir_all(&app_data_dir).unwrap();
        backend_api::shutdown_c_api();
        backend_api::init_with_paths(&dll_path, app_data_dir.to_str())
            .expect("initialize the real DLL against isolated app data");
        app_data_dir
    }

    fn call_real_stats_summary(
        control: Arc<backend_api::RequestControl>,
        entries: Arc<AtomicUsize>,
    ) -> Result<String, String> {
        let range = std::ffi::CString::new("all").unwrap();
        let guard = backend_api::api_handle_for_request(&control)?;
        let handle = guard.as_ref().ok_or("C API not loaded")?;
        control.claim_dll_start()?;
        entries.fetch_add(1, Ordering::SeqCst);
        unsafe {
            let response = (handle.stats_get_summary)(range.as_ptr());
            if response.is_null() {
                return Err("Empty stats response from C API".into());
            }
            let body = std::ffi::CStr::from_ptr(response)
                .to_string_lossy()
                .into_owned();
            (handle.free_str)(response);
            Ok(body)
        }
    }

    #[tokio::test]
    // Serialize global DLL init/shutdown. The write guard deliberately blocks
    // wrappers while this async test observes cancellation and physical drain.
    #[allow(clippy::await_holding_lock)]
    async fn r01_real_dll_three_round_lock_wait_is_cancelled_and_recovers() {
        // Exercise three cap-sized cohorts against a real backend writer
        // lock. Each production-dispatch request carries one short absolute
        // deadline through admission and the wrapper's try_read loop. No
        // request may enter the DLL while the writer lock is held.
        const ROUNDS: usize = MAX_CONCURRENT_FFI_CALLS * 3;
        const CALLER_BUDGET: Duration = Duration::from_millis(250);

        let _api_test_lock = backend_api::lock_test_c_api();
        backend_api::shutdown_c_api();
        let dll_path = std::env::var("ECHO_CAPI_DLL")
            .expect("R01 real-DLL measurement requires ECHO_CAPI_DLL");
        let unique = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let app_data_dir = std::env::temp_dir().join(format!(
            "bottlemusic-r01-rust-{}-{unique}",
            std::process::id()
        ));
        std::fs::create_dir_all(&app_data_dir).unwrap();
        backend_api::init_with_paths(&dll_path, app_data_dir.to_str())
            .expect("initialize the real Debug DLL against isolated app data");
        backend_api::reset_api_call_test_metrics();
        let dispatch_metrics = ffi_dispatcher().test_metrics.clone();
        reset_ffi_dispatch_test_metrics(&dispatch_metrics);

        let blocked_readers = backend_api::lock_api_for_test();
        let mut caller_timeouts = 0usize;
        let mut unexpectedly_completed = 0usize;
        let mut closure_start_wait_failures = 0usize;

        for cohort in 0..3 {
            let contended_before = backend_api::API_CALL_TEST_METRICS
                .read_lock_contended_requests
                .load(Ordering::SeqCst);
            let shared_deadline = Instant::now() + CALLER_BUDGET;
            let mut callers = Vec::new();
            for offset in 0..MAX_CONCURRENT_FFI_CALLS {
                let index = cohort * MAX_CONCURRENT_FFI_CALLS + offset;
                let path = match index {
                    0 => "/settings/device",
                    n if n % 3 == 0 => "/r01-local-unknown-route",
                    _ => "/health",
                };
                callers.push(tokio::spawn(dispatch_native_request(
                    "GET".to_string(),
                    path.to_string(),
                    None,
                    None,
                    None,
                    shared_deadline,
                )));
            }
            let reached_contended_lock = wait_until(Duration::from_secs(2), || {
                backend_api::API_CALL_TEST_METRICS
                    .read_lock_contended_requests
                    .load(Ordering::SeqCst)
                    >= contended_before + MAX_CONCURRENT_FFI_CALLS
            })
            .await;
            if !reached_contended_lock {
                closure_start_wait_failures += 1;
            }
            for caller in callers {
                match caller.await.unwrap() {
                    Err(error) if error == "request_deadline" => caller_timeouts += 1,
                    _ => unexpectedly_completed += 1,
                }
            }
            let cohort_drained = wait_until(Duration::from_secs(5), || {
                dispatch_metrics.blocking_live.load(Ordering::SeqCst) == 0
                    && ffi_dispatcher().live.available_permits() == MAX_LIVE_FFI_CLOSURES
            })
            .await;
            if !cohort_drained {
                closure_start_wait_failures += 1;
            }
        }

        let drained_before_unlock = wait_until(Duration::from_secs(5), || {
            dispatch_metrics.blocking_live.load(Ordering::SeqCst) == 0
                && ffi_dispatcher().live.available_permits() == MAX_LIVE_FFI_CLOSURES
        })
        .await;
        let caller_returned_with_full_admission =
            ffi_dispatch_semaphore().available_permits() == MAX_CONCURRENT_FFI_CALLS;
        let admitted_before_unlock = dispatch_metrics.admitted.load(Ordering::SeqCst);
        let live_at_lock_release = dispatch_metrics.blocking_live.load(Ordering::SeqCst);
        let peak_before_lock_release = dispatch_metrics.blocking_peak.load(Ordering::SeqCst);
        let read_attempts_before_unlock = backend_api::API_CALL_TEST_METRICS
            .read_lock_attempted
            .load(Ordering::SeqCst);
        let contended_requests_before_unlock = backend_api::API_CALL_TEST_METRICS
            .read_lock_contended_requests
            .load(Ordering::SeqCst);
        let dll_entries_before_unlock = backend_api::API_CALL_TEST_METRICS
            .dll_entered
            .load(Ordering::SeqCst);

        drop(blocked_readers);
        let drained = wait_until(Duration::from_secs(20), || {
            dispatch_metrics.blocking_live.load(Ordering::SeqCst) == 0
                && ffi_dispatcher().live.available_permits() == MAX_LIVE_FFI_CLOSURES
        })
        .await;

        async fn local_status(path: &str) -> Option<u64> {
            native_request("GET".into(), path.into(), None, None, None)
                .await
                .ok()
                .and_then(|body| serde_json::from_str::<serde_json::Value>(&body).ok())
                .and_then(|json| json.get("status").and_then(serde_json::Value::as_u64))
        }
        let first_health_status = local_status("/health").await;
        let native_route_rejection = local_status("/r01-local-unknown-route").await;
        let recovery_status = local_status("/health").await;

        let wrapper_started = backend_api::API_CALL_TEST_METRICS
            .wrapper_started
            .load(Ordering::SeqCst);
        let read_lock_attempted = backend_api::API_CALL_TEST_METRICS
            .read_lock_attempted
            .load(Ordering::SeqCst);
        let read_lock_acquired = backend_api::API_CALL_TEST_METRICS
            .read_lock_acquired
            .load(Ordering::SeqCst);
        let read_lock_timed_out = backend_api::API_CALL_TEST_METRICS
            .read_lock_timed_out
            .load(Ordering::SeqCst);
        let dll_entered = backend_api::API_CALL_TEST_METRICS
            .dll_entered
            .load(Ordering::SeqCst);
        let dll_returned = backend_api::API_CALL_TEST_METRICS
            .dll_returned
            .load(Ordering::SeqCst);
        let native_200 = backend_api::API_CALL_TEST_METRICS
            .native_status_200
            .load(Ordering::SeqCst);
        let native_404 = backend_api::API_CALL_TEST_METRICS
            .native_status_404
            .load(Ordering::SeqCst);
        let native_504 = backend_api::API_CALL_TEST_METRICS
            .native_status_504
            .load(Ordering::SeqCst);
        let native_other = backend_api::API_CALL_TEST_METRICS
            .native_status_other
            .load(Ordering::SeqCst);
        let read_wait_ms = backend_api::API_CALL_TEST_METRICS
            .read_lock_wait_nanos
            .load(Ordering::SeqCst)
            / 1_000_000;
        let closure_started = dispatch_metrics.blocking_started.load(Ordering::SeqCst);
        let closure_finished = dispatch_metrics.blocking_finished.load(Ordering::SeqCst);
        let closure_live_after_drain = dispatch_metrics.blocking_live.load(Ordering::SeqCst);
        let admission_restored = ffi_dispatch_semaphore().available_permits();
        let lifetime_capacity_restored = ffi_dispatcher().live.available_permits();

        eprintln!(
            "R01_FIXED_LOCK {{\"rust_active_cap\":{},\"attempts\":{},\"caller_timeouts\":{},\
             \"unexpected_completions\":{},\"admitted\":{},\"closure_started\":{},\
             \"closure_peak_before_unlock\":{},\"closure_live_at_unlock\":{},\
             \"closure_finished\":{},\"closure_live_after_drain\":{},\
             \"admission_full_after_timeouts\":{},\"admission_restored\":{},\
             \"live_capacity_restored\":{},\"drained_before_unlock\":{},\
             \"wrapper_started\":{},\"read_attempted_before_unlock\":{},\
             \"read_attempted\":{},\"read_contended_requests\":{},\
             \"read_acquired\":{},\"read_timed_out\":{},\
             \"read_wait_ms_total\":{},\
             \"dll_entered\":{},\"dll_returned\":{},\"native_200\":{},\
             \"native_404\":{},\"native_504\":{},\"native_other\":{},\"drained\":{},\
             \"closure_start_wait_failures\":{},\"dll_before_unlock\":{},\
             \"first_health\":{:?},\"route_rejection\":{:?},\"recovery_status\":{:?}}}",
            MAX_CONCURRENT_FFI_CALLS,
            ROUNDS,
            caller_timeouts,
            unexpectedly_completed,
            admitted_before_unlock,
            closure_started,
            peak_before_lock_release,
            live_at_lock_release,
            closure_finished,
            closure_live_after_drain,
            caller_returned_with_full_admission,
            admission_restored,
            lifetime_capacity_restored,
            drained_before_unlock,
            wrapper_started,
            read_attempts_before_unlock,
            read_lock_attempted,
            contended_requests_before_unlock,
            read_lock_acquired,
            read_lock_timed_out,
            read_wait_ms,
            dll_entered,
            dll_returned,
            native_200,
            native_404,
            native_504,
            native_other,
            drained,
            closure_start_wait_failures,
            dll_entries_before_unlock,
            first_health_status,
            native_route_rejection,
            recovery_status
        );

        // Cleanup occurs before assertions so a failed measurement cannot
        // leave a writer gate, DLL guard, database, or blocking closure behind.
        backend_api::shutdown_c_api();
        let _ = std::fs::remove_dir_all(&app_data_dir);

        assert!(
            drained_before_unlock,
            "cancelled closures must release the held read lock"
        );
        assert!(drained, "all blocking closures must drain after unlock");
        assert_eq!(
            caller_timeouts, ROUNDS,
            "all caller budgets should expire at the held lock"
        );
        assert_eq!(unexpectedly_completed, 0);
        assert_eq!(closure_start_wait_failures, 0);
        assert!(caller_returned_with_full_admission);
        assert_eq!(admitted_before_unlock, ROUNDS);
        assert!(closure_started >= ROUNDS);
        assert!(peak_before_lock_release <= MAX_LIVE_FFI_CLOSURES);
        assert_eq!(live_at_lock_release, 0);
        assert_eq!(wrapper_started, ROUNDS + 3); // three real local probes after unlock
        assert_eq!(read_attempts_before_unlock, ROUNDS);
        assert_eq!(contended_requests_before_unlock, ROUNDS);
        assert_eq!(read_lock_attempted, ROUNDS + 3);
        assert_eq!(read_lock_acquired, 3);
        assert_eq!(read_lock_timed_out, ROUNDS);
        assert_eq!(
            dll_entries_before_unlock, 0,
            "cancelled requests must not enter the DLL"
        );
        assert_eq!(dll_entered, 3);
        assert_eq!(dll_returned, 3);
        assert_eq!(native_404, 1, "unknown local route must reach native 404");
        assert_eq!(native_200, 2, "both local health requests must succeed");
        assert_eq!(
            native_504, 0,
            "serial local probes should not overload native dispatch"
        );
        assert_eq!(
            native_200 + native_404 + native_504 + native_other,
            dll_returned
        );
        assert_eq!(closure_started, closure_finished);
        assert_eq!(closure_live_after_drain, 0);
        assert_eq!(admission_restored, MAX_CONCURRENT_FFI_CALLS);
        assert_eq!(lifetime_capacity_restored, MAX_LIVE_FFI_CLOSURES);
        assert_eq!(first_health_status, Some(200));
        assert_eq!(native_route_rejection, Some(404));
        assert_eq!(recovery_status, Some(200));
    }

    #[tokio::test]
    #[allow(clippy::await_holding_lock)] // Deliberate global DLL/write-lock fixture.
    async fn r01_caller_drop_after_rwlock_contention_drains_without_dll_entry() {
        let _api_test_lock = backend_api::lock_test_c_api();
        let app_data_dir = setup_real_dll_app_data("r01-lock-drop");
        backend_api::reset_api_call_test_metrics();
        let dispatcher = Arc::new(FfiDispatcher::new(1, 1, 1));
        reset_ffi_dispatch_test_metrics(&dispatcher.test_metrics);
        let blocked_readers = backend_api::lock_api_for_test();
        let control = backend_api::RequestControl::new(Instant::now() + Duration::from_secs(30));
        let caller = tokio::spawn({
            let dispatcher = dispatcher.clone();
            let control = control.clone();
            async move {
                dispatch_bounded_ffi(&dispatcher, control, move |control| {
                    backend_api::handle_request_with_control(
                        "GET", "/health", None, None, None, control,
                    )
                })
                .await
            }
        });
        let reached_would_block = wait_until(Duration::from_secs(5), || {
            backend_api::API_CALL_TEST_METRICS
                .read_lock_contended_requests
                .load(Ordering::SeqCst)
                == 1
        })
        .await;
        caller.abort();
        let caller_cancelled = caller.await.is_err();
        let request_cancelled = control.ensure_pending().is_err();
        let drained_while_writer_held = wait_until(Duration::from_secs(5), || {
            dispatcher.live.available_permits() == 1
                && dispatcher.test_metrics.blocking_live.load(Ordering::SeqCst) == 0
                && backend_api::API_CALL_TEST_METRICS
                    .read_lock_timed_out
                    .load(Ordering::SeqCst)
                    == 1
        })
        .await;
        let dll_entries_while_writer_held = backend_api::API_CALL_TEST_METRICS
            .dll_entered
            .load(Ordering::SeqCst);

        drop(blocked_readers);
        let recovery_control =
            backend_api::RequestControl::new(Instant::now() + Duration::from_secs(5));
        let recovery = dispatch_bounded_ffi(&dispatcher, recovery_control, move |control| {
            backend_api::handle_request_with_control("GET", "/health", None, None, None, control)
        })
        .await;
        let recovery_status = recovery
            .ok()
            .and_then(|body| serde_json::from_str::<serde_json::Value>(&body).ok())
            .and_then(|json| json.get("status").and_then(serde_json::Value::as_u64));
        let dll_entries_after_recovery = backend_api::API_CALL_TEST_METRICS
            .dll_entered
            .load(Ordering::SeqCst);
        let live_restored = dispatcher.live.available_permits();

        backend_api::shutdown_c_api();
        let _ = std::fs::remove_dir_all(&app_data_dir);

        assert!(
            reached_would_block,
            "wrapper must observe real RwLock contention"
        );
        assert!(caller_cancelled);
        assert!(request_cancelled);
        assert!(
            drained_while_writer_held,
            "caller drop must drain the physical closure"
        );
        assert_eq!(dll_entries_while_writer_held, 0);
        assert_eq!(recovery_status, Some(200));
        assert_eq!(dll_entries_after_recovery, 1);
        assert_eq!(live_restored, 1);
    }

    #[tokio::test]
    #[allow(clippy::await_holding_lock)] // Serialize global DLL and hook lifetime.
    async fn r01_wrapper_cancel_after_read_guard_suppresses_dll_entry() {
        let _api_test_lock = backend_api::lock_test_c_api();
        let app_data_dir = setup_real_dll_app_data("r01-before-start");
        backend_api::reset_api_call_test_metrics();

        let dispatcher = Arc::new(FfiDispatcher::new(1, 1, 1));
        reset_ffi_dispatch_test_metrics(&dispatcher.test_metrics);
        let (gate, hook) = new_test_gate();
        backend_api::set_before_dll_start_test_hook(Some(hook));

        let control = backend_api::RequestControl::new(Instant::now() + Duration::from_secs(30));
        let caller = tokio::spawn({
            let dispatcher = dispatcher.clone();
            let control = control.clone();
            async move {
                dispatch_bounded_ffi(&dispatcher, control, move |control| {
                    backend_api::handle_request_with_control(
                        "GET", "/health", None, None, None, control,
                    )
                })
                .await
            }
        });
        let reached_final_start = wait_gate_entered(&gate).await;
        let read_guard_held = backend_api::API_CALL_TEST_METRICS
            .read_lock_acquired
            .load(Ordering::SeqCst)
            == 1
            && backend_api::api_write_lock_is_blocked_for_test();
        let live_slot_held = dispatcher.live.available_permits() == 0;
        let dll_entries_before_cancel = backend_api::API_CALL_TEST_METRICS
            .dll_entered
            .load(Ordering::SeqCst);

        caller.abort();
        let caller_cancelled = caller.await.is_err();
        let request_cancelled = control.ensure_pending().is_err();
        release_test_gate(&gate);
        backend_api::set_before_dll_start_test_hook(None);
        let closure_drained = wait_until(Duration::from_secs(5), || {
            dispatcher.live.available_permits() == 1
                && !backend_api::api_write_lock_is_blocked_for_test()
        })
        .await;
        let dll_entries_after_cancel = backend_api::API_CALL_TEST_METRICS
            .dll_entered
            .load(Ordering::SeqCst);
        let deadline_control =
            backend_api::RequestControl::new(Instant::now() + Duration::from_secs(5));
        let recovery = dispatch_bounded_ffi(&dispatcher, deadline_control, move |control| {
            backend_api::handle_request_with_control("GET", "/health", None, None, None, control)
        })
        .await;
        let recovery_status = recovery
            .ok()
            .and_then(|body| serde_json::from_str::<serde_json::Value>(&body).ok())
            .and_then(|json| json.get("status").and_then(serde_json::Value::as_u64));
        let dll_entries_after_recovery = backend_api::API_CALL_TEST_METRICS
            .dll_entered
            .load(Ordering::SeqCst);
        let live_restored = dispatcher.live.available_permits();

        backend_api::shutdown_c_api();
        let _ = std::fs::remove_dir_all(&app_data_dir);

        assert!(
            reached_final_start,
            "wrapper must pause after read guard acquisition"
        );
        assert!(
            read_guard_held,
            "the pause must hold the DLL's Rust read guard"
        );
        assert!(
            live_slot_held,
            "the closure lifetime slot must follow the wrapper"
        );
        assert_eq!(dll_entries_before_cancel, 0);
        assert!(caller_cancelled);
        assert!(request_cancelled, "dropping the caller must cancel Pending");
        assert!(
            closure_drained,
            "the closure and guard must drain after gate release"
        );
        assert_eq!(
            dll_entries_after_cancel, 0,
            "Cancelled must win before DLL entry"
        );
        assert_eq!(recovery_status, Some(200));
        assert_eq!(dll_entries_after_recovery, 1);
        assert_eq!(live_restored, 1);
    }

    #[tokio::test]
    #[allow(clippy::await_holding_lock)] // Serialize global DLL and hook lifetime.
    async fn r01_started_dll_keeps_read_guard_through_response_free() {
        let _api_test_lock = backend_api::lock_test_c_api();
        let app_data_dir = setup_real_dll_app_data("r01-after-return");
        backend_api::reset_api_call_test_metrics();

        let dispatcher = Arc::new(FfiDispatcher::new(1, 1, 1));
        reset_ffi_dispatch_test_metrics(&dispatcher.test_metrics);
        let (gate, hook) = new_test_gate();
        backend_api::set_after_dll_return_test_hook(Some(hook));

        let caller = tokio::spawn({
            let dispatcher = dispatcher.clone();
            async move {
                let control =
                    backend_api::RequestControl::new(Instant::now() + Duration::from_secs(1));
                dispatch_bounded_ffi(&dispatcher, control, move |control| {
                    backend_api::handle_request_with_control(
                        "GET", "/health", None, None, None, control,
                    )
                })
                .await
            }
        });
        let reached_after_return = wait_gate_entered(&gate).await;
        let caller_result = caller.await.unwrap();
        let active_restored_after_deadline = dispatcher.active.available_permits();
        let live_held_after_deadline = dispatcher.live.available_permits() == 0;
        let write_blocked_before_free = backend_api::api_write_lock_is_blocked_for_test();
        let dll_entered = backend_api::API_CALL_TEST_METRICS
            .dll_entered
            .load(Ordering::SeqCst);
        let dll_returned = backend_api::API_CALL_TEST_METRICS
            .dll_returned
            .load(Ordering::SeqCst);
        let strings_freed_before_release = backend_api::API_CALL_TEST_METRICS
            .dll_string_freed
            .load(Ordering::SeqCst);

        release_test_gate(&gate);
        backend_api::set_after_dll_return_test_hook(None);
        let closure_drained = wait_until(Duration::from_secs(5), || {
            dispatcher.live.available_permits() == 1
                && !backend_api::api_write_lock_is_blocked_for_test()
        })
        .await;
        let strings_freed_after_release = backend_api::API_CALL_TEST_METRICS
            .dll_string_freed
            .load(Ordering::SeqCst);
        let live_restored = dispatcher.live.available_permits();

        backend_api::shutdown_c_api();
        let _ = std::fs::remove_dir_all(&app_data_dir);

        assert!(
            reached_after_return,
            "DLL must return before the response-free gate"
        );
        assert_eq!(caller_result.unwrap_err(), "request_deadline");
        assert_eq!(active_restored_after_deadline, 1);
        assert!(
            live_held_after_deadline,
            "started closure keeps its physical slot"
        );
        assert!(
            write_blocked_before_free,
            "read guard must remain held before EchoFreeString"
        );
        assert_eq!(dll_entered, 1);
        assert_eq!(dll_returned, 1);
        assert_eq!(strings_freed_before_release, 0);
        assert!(
            closure_drained,
            "read guard and lifetime slot release after free"
        );
        assert_eq!(strings_freed_after_release, 1);
        assert_eq!(live_restored, 1);
    }

    #[tokio::test]
    async fn timed_out_started_closures_have_two_cohorts_and_a_hard_lifetime_cap() {
        const COHORT: usize = MAX_CONCURRENT_FFI_CALLS;
        let dispatcher = Arc::new(FfiDispatcher::new(
            MAX_CONCURRENT_FFI_CALLS,
            MAX_QUEUED_FFI_CALLS,
            MAX_LIVE_FFI_CLOSURES,
        ));
        let live = Arc::new(AtomicUsize::new(0));
        let started = Arc::new(AtomicUsize::new(0));
        let peak = Arc::new(AtomicUsize::new(0));
        let gate = Arc::new(Mutex::new(false));
        let gate_cv = Arc::new(Condvar::new());
        let mut timeouts = 0usize;
        let _gate_cleanup = CondvarGateCleanup(gate.clone(), gate_cv.clone());
        let mut probe_result = None;
        let mut closure_start_wait_failures = 0usize;

        for cohort_index in 0..2 {
            let started_before_cohort = started.load(Ordering::SeqCst);
            for expected_started in (started_before_cohort + 1)..=started_before_cohort + COHORT {
                let live = live.clone();
                let started_in_task = started.clone();
                let peak = peak.clone();
                let gate = gate.clone();
                let gate_cv = gate_cv.clone();
                let dispatcher_in_task = dispatcher.clone();
                let mut caller = tokio::spawn(async move {
                    let control =
                        backend_api::RequestControl::new(Instant::now() + Duration::from_secs(30));
                    dispatch_bounded_ffi(&dispatcher_in_task, control, move |control| {
                        control.claim_dll_start()?;
                        started_in_task.fetch_add(1, Ordering::SeqCst);
                        let now = live.fetch_add(1, Ordering::SeqCst) + 1;
                        peak.fetch_max(now, Ordering::SeqCst);
                        let mut open = gate.lock().unwrap();
                        while !*open {
                            open = gate_cv.wait(open).unwrap();
                        }
                        live.fetch_sub(1, Ordering::SeqCst);
                        Ok(())
                    })
                    .await
                });
                let closure_started = wait_until(Duration::from_secs(5), || {
                    started.load(Ordering::SeqCst) >= expected_started
                })
                .await;
                // Once Started and gated, an outer caller timeout drops the
                // async waiter while the physical closure keeps its permit.
                let caller_timed_out = timeout(Duration::from_millis(100), &mut caller).await;
                caller.abort();
                let _ = caller.await;
                if caller_timed_out.is_err() {
                    timeouts += 1;
                }
                // timeout borrows the JoinHandle; abort its detached task now
                // so the future's pending-cancel guard releases the active
                // slot, while Started prevents cancellation of the DLL work.
                if !closure_started {
                    closure_start_wait_failures += 1;
                }
            }

            if cohort_index == 0 {
                // One full timed-out cohort must leave capacity for a fresh
                // request while its physical closures are still alive.
                let probe_control =
                    backend_api::RequestControl::new(Instant::now() + Duration::from_secs(5));
                probe_result =
                    Some(dispatch_bounded_ffi(&dispatcher, probe_control, |_control| Ok(7)).await);
            }
        }

        let cap_result = dispatch_bounded_ffi(
            &dispatcher,
            backend_api::RequestControl::new(Instant::now() + Duration::from_secs(5)),
            |_control| Ok(()),
        )
        .await;
        let live_at_cap = live.load(Ordering::SeqCst);
        let peak_at_cap = peak.load(Ordering::SeqCst);

        *gate.lock().unwrap() = true;
        gate_cv.notify_all();
        let drained = wait_until(Duration::from_secs(10), || {
            live.load(Ordering::SeqCst) == 0
                && dispatcher.live.available_permits() == MAX_LIVE_FFI_CLOSURES
        })
        .await;
        let active_restored = dispatcher.active.available_permits();
        let lifetime_restored = dispatcher.live.available_permits();

        assert!(
            drained,
            "every gated blocking closure must finish after release"
        );
        assert_eq!(timeouts, 2 * COHORT);
        assert_eq!(closure_start_wait_failures, 0);
        assert_eq!(probe_result, Some(Ok(7)));
        assert_eq!(started.load(Ordering::SeqCst), MAX_LIVE_FFI_CLOSURES);
        assert_eq!(live_at_cap, MAX_LIVE_FFI_CLOSURES);
        assert_eq!(peak_at_cap, MAX_LIVE_FFI_CLOSURES);
        assert_eq!(cap_result.unwrap_err(), "ffi_live_capacity");
        assert_eq!(active_restored, MAX_CONCURRENT_FFI_CALLS);
        assert_eq!(lifetime_restored, MAX_LIVE_FFI_CLOSURES);
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
    async fn dispatch_admits_at_most_the_active_and_waiter_caps() {
        // No DLL involved: two active callers block, while four more occupy
        // the bounded async waiter queue.
        let dispatcher = Arc::new(FfiDispatcher::new(2, 4, 6));
        let in_flight = Arc::new(AtomicUsize::new(0));
        let max_observed = Arc::new(AtomicUsize::new(0));
        // std Condvar: the slow "FFI" closures run on blocking threads and
        // need a blocking (not async) release signal.
        let gate = Arc::new(Mutex::new(false));
        let gate_cv = Arc::new(Condvar::new());

        let mut tasks = Vec::new();
        let _gate_cleanup = CondvarGateCleanup(gate.clone(), gate_cv.clone());
        for i in 0..6 {
            let dispatcher = dispatcher.clone();
            let in_flight = in_flight.clone();
            let max_observed = max_observed.clone();
            let gate = gate.clone();
            let gate_cv = gate_cv.clone();
            tasks.push(tokio::spawn(async move {
                let control =
                    backend_api::RequestControl::new(Instant::now() + Duration::from_secs(10));
                dispatch_bounded_ffi(&dispatcher, control, move |control| {
                    control.claim_dll_start()?;
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

        // The first two closures hold active slots; the other four wait
        // asynchronously and cannot enter the blocking pool yet.
        let admitted = wait_until(Duration::from_secs(10), || {
            in_flight.load(Ordering::SeqCst) >= 2
        })
        .await;
        let max_before_release = max_observed.load(Ordering::SeqCst);
        let waiters_full = dispatcher.waiters.available_permits() == 0;

        *gate.lock().unwrap() = true;
        gate_cv.notify_all();
        let mut task_results = Vec::new();
        for task in tasks {
            task_results.push(task.await);
        }
        assert_eq!(in_flight.load(Ordering::SeqCst), 0);
        assert_eq!(dispatcher.active.available_permits(), 2);
        assert_eq!(dispatcher.live.available_permits(), 6);
        assert!(
            admitted,
            "the first two dispatches were not admitted within the budget"
        );
        assert_eq!(
            max_before_release, 2,
            "concurrent FFI entries must be capped at the semaphore capacity"
        );
        assert!(
            waiters_full,
            "four queued callers must consume the waiter cap"
        );
        assert!(task_results
            .iter()
            .all(|result| matches!(result, Ok(Ok(value)) if *value < 6)));
    }

    #[tokio::test]
    async fn dispatch_rejects_beyond_its_bounded_waiter_queue() {
        let dispatcher = Arc::new(FfiDispatcher::new(1, 1, 3));
        let gate = Arc::new(Mutex::new(false));
        let gate_cv = Arc::new(Condvar::new());
        let _gate_cleanup = CondvarGateCleanup(gate.clone(), gate_cv.clone());
        let active_gate = gate.clone();
        let active_cv = gate_cv.clone();
        let active = tokio::spawn({
            let dispatcher = dispatcher.clone();
            async move {
                let control =
                    backend_api::RequestControl::new(Instant::now() + Duration::from_secs(10));
                dispatch_bounded_ffi(&dispatcher, control, move |control| {
                    control.claim_dll_start()?;
                    let mut open = active_gate.lock().unwrap();
                    while !*open {
                        open = active_cv.wait(open).unwrap();
                    }
                    Ok(1)
                })
                .await
            }
        });
        let active_entered = wait_until(Duration::from_secs(5), || {
            dispatcher.active.available_permits() == 0 && dispatcher.live.available_permits() == 2
        })
        .await;
        let waiter = tokio::spawn({
            let dispatcher = dispatcher.clone();
            async move {
                let control =
                    backend_api::RequestControl::new(Instant::now() + Duration::from_secs(10));
                dispatch_bounded_ffi(&dispatcher, control, |_control| Ok(2)).await
            }
        });
        let waiter_entered = wait_until(Duration::from_secs(5), || {
            dispatcher.waiters.available_permits() == 0
        })
        .await;
        let overloaded = dispatch_bounded_ffi(
            &dispatcher,
            backend_api::RequestControl::new(Instant::now() + Duration::from_secs(5)),
            |_control| Ok(3),
        )
        .await;

        *gate.lock().unwrap() = true;
        gate_cv.notify_all();
        let active_result = active.await.unwrap();
        let waiter_result = waiter.await.unwrap();
        let active_restored = dispatcher.active.available_permits();
        let waiters_restored = dispatcher.waiters.available_permits();
        let live_restored = dispatcher.live.available_permits();

        assert!(active_entered, "active closure must be admitted");
        assert!(waiter_entered, "one waiter must occupy the bounded queue");
        assert_eq!(active_result, Ok(1));
        assert_eq!(waiter_result, Ok(2));
        assert_eq!(overloaded.unwrap_err(), "ffi_overloaded");
        assert_eq!(active_restored, 1);
        assert_eq!(waiters_restored, 1);
        assert_eq!(live_restored, 3);
    }

    #[test]
    fn ffi_cap_exceeds_cpp_scheduler_workers() {
        // Rust active admission is an independent recovery policy. The native
        // worker count does not determine this Rust-side cap.
        const {
            assert!(MAX_CONCURRENT_FFI_CALLS > 4);
        }
        assert_eq!(MAX_CONCURRENT_FFI_CALLS, 16);
    }

    #[tokio::test]
    async fn stats_ffi_dispatcher_execution_timeout_fires_and_holds_permit() {
        // The dispatcher's OWN execution timeout (not the caller's outer
        // timeout, which the survival test exercises): the closure enters a
        // barrier, the dispatcher returns stats_ffi_timeout at its internal
        // deadline, the permit stays with the still-running closure, and
        // releasing the barrier lets it finish and return the permit.
        let semaphore = Arc::new(Semaphore::new(1));
        let waiter_semaphore = Arc::new(Semaphore::new(1));
        let gate = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let entered = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let completed = Arc::new(std::sync::atomic::AtomicBool::new(false));
        // Releases the barrier on ALL paths (including test failure) so the
        // gated closure can never outlive the test's captured state.
        struct GateDrop(Arc<std::sync::atomic::AtomicBool>);
        impl Drop for GateDrop {
            fn drop(&mut self) {
                self.0.store(true, std::sync::atomic::Ordering::Release);
            }
        }
        let gate_guard = GateDrop(gate.clone());

        let started = std::time::Instant::now();
        let result =
            dispatch_stats_ffi_with(&semaphore, &waiter_semaphore, Duration::from_millis(250), {
                let gate = gate.clone();
                let entered = entered.clone();
                let completed = completed.clone();
                move |control| {
                    control.claim_dll_start()?;
                    entered.store(true, std::sync::atomic::Ordering::Release);
                    while !gate.load(std::sync::atomic::Ordering::Acquire) {
                        std::thread::sleep(Duration::from_millis(2));
                    }
                    completed.store(true, std::sync::atomic::Ordering::Release);
                    Ok(())
                }
            })
            .await;

        let elapsed = started.elapsed();
        assert_eq!(
            result.unwrap_err(),
            "stats_ffi_timeout",
            "the dispatcher's internal execution deadline must surface as stats_ffi_timeout"
        );
        assert!(
            elapsed >= Duration::from_millis(200) && elapsed < Duration::from_millis(2000),
            "dispatch took {:?}; the internal deadline must fire, not the caller's",
            elapsed
        );
        assert!(
            entered.load(std::sync::atomic::Ordering::Acquire),
            "the closure actually entered the FFI"
        );
        // The closure is still gated behind the barrier: the permit stays held.
        assert_eq!(
            semaphore.available_permits(),
            0,
            "the permit stays with the gated closure after the dispatcher timeout"
        );

        // Release: the closure finishes and the permit returns.
        drop(gate_guard);
        let drained = wait_until(Duration::from_secs(5), || {
            semaphore.available_permits() == 1
        })
        .await;
        assert!(drained, "the permit must return once the barrier opens");
        assert!(
            completed.load(std::sync::atomic::Ordering::Acquire),
            "the gated closure ran to completion after the dispatcher timed out"
        );
    }

    #[test]
    fn stats_ffi_survival_constants() {
        // Keep the existing dedicated stats survival cap and bound its async
        // waiter queue. These are Rust-side limits, independent of the native
        // worker count.
        const {
            assert!(MAX_CONCURRENT_STATS_FFI <= MAX_CONCURRENT_FFI_CALLS);
        }
        assert_eq!(MAX_CONCURRENT_STATS_FFI, 4);
        assert_eq!(MAX_QUEUED_STATS_FFI, 4);
        assert_eq!(STATS_FFI_TIMEOUT_MS, 3000);
    }

    #[tokio::test]
    async fn stats_ffi_permit_survives_caller_timeout() {
        // THE Stage 4 discriminator (plan: survival cap vs admission cap):
        // a stats dispatch abandoned by its caller's timeout must NOT give
        // its permit back — the permit lives inside the still-running
        // blocking closure, so the count of live stats FFIs stays bounded
        // even when callers stop waiting. dispatch_bounded_ffi releases the
        // permit at caller timeout; under those semantics this test fails.
        let semaphore = Arc::new(Semaphore::new(1));
        let waiter_semaphore = Arc::new(Semaphore::new(1));
        let gate = Arc::new(Mutex::new(false));
        let gate_cv = Arc::new(Condvar::new());
        let _gate_cleanup = CondvarGateCleanup(gate.clone(), gate_cv.clone());
        let released = Arc::new(AtomicUsize::new(0));
        let entered = Arc::new(std::sync::atomic::AtomicBool::new(false));
        let mut dispatch = tokio::spawn({
            let semaphore = semaphore.clone();
            let waiter_semaphore = waiter_semaphore.clone();
            let gate = gate.clone();
            let gate_cv = gate_cv.clone();
            let released = released.clone();
            let entered = entered.clone();
            async move {
                dispatch_stats_ffi_with(
                    &semaphore,
                    &waiter_semaphore,
                    Duration::from_secs(30),
                    move |control| {
                        control.claim_dll_start()?;
                        entered.store(true, std::sync::atomic::Ordering::Release);
                        let mut open = gate.lock().unwrap();
                        while !*open {
                            open = gate_cv.wait(open).unwrap();
                        }
                        released.fetch_add(1, Ordering::SeqCst);
                        Ok(())
                    },
                )
                .await
            }
        });

        // Wait for the closure's Started claim before beginning the simulated
        // caller timeout; permit acquisition alone happens earlier at spawn.
        let closure_entered = wait_until(Duration::from_secs(5), || {
            entered.load(std::sync::atomic::Ordering::Acquire)
        })
        .await;
        let caller_timed_out = timeout(Duration::from_millis(80), &mut dispatch)
            .await
            .is_err();
        dispatch.abort();
        let _ = dispatch.await;
        let permit_held_after_caller_drop = semaphore.available_permits() == 0;

        // The abandoned closure still runs to completion once released, and
        // only then does the permit return.
        *gate.lock().unwrap() = true;
        gate_cv.notify_all();
        let drained = wait_until(Duration::from_secs(5), || {
            semaphore.available_permits() == 1
        })
        .await;
        let released_count = released.load(Ordering::SeqCst);

        assert!(
            closure_entered,
            "the closure must claim Started before timeout"
        );
        assert!(
            caller_timed_out,
            "the caller should stop waiting at its outer timeout"
        );
        assert!(
            permit_held_after_caller_drop,
            "the permit remains with the live closure"
        );
        assert!(
            drained,
            "the permit must return once the abandoned closure finishes"
        );
        assert_eq!(
            released_count, 1,
            "the abandoned closure must have run to completion (no UAF/hang)"
        );
    }

    #[tokio::test]
    async fn stats_ffi_queue_timeout_when_cap_saturated() {
        // Queueing is inside the timeout: a dispatch that cannot acquire a
        // permit within its budget fails with stats_ffi_queue_timeout
        // instead of hanging behind a saturated cap.
        let semaphore = Arc::new(Semaphore::new(1));
        let waiter_semaphore = Arc::new(Semaphore::new(1));
        let gate = Arc::new(Mutex::new(false));
        let gate_cv = Arc::new(Condvar::new());
        let _gate_cleanup = CondvarGateCleanup(gate.clone(), gate_cv.clone());

        let occupier = tokio::spawn({
            let semaphore = semaphore.clone();
            let waiter_semaphore = waiter_semaphore.clone();
            let gate = gate.clone();
            let gate_cv = gate_cv.clone();
            async move {
                let _ = dispatch_stats_ffi_with(
                    &semaphore,
                    &waiter_semaphore,
                    Duration::from_secs(60),
                    move |control| {
                        control.claim_dll_start()?;
                        let mut open = gate.lock().unwrap();
                        while !*open {
                            open = gate_cv.wait(open).unwrap();
                        }
                        Ok(())
                    },
                )
                .await;
            }
        });
        let entered = wait_until(Duration::from_secs(5), || {
            semaphore.available_permits() == 0
        })
        .await;

        let start = std::time::Instant::now();
        let result = dispatch_stats_ffi_with(
            &semaphore,
            &waiter_semaphore,
            Duration::from_millis(150),
            |_control| Ok(()),
        )
        .await;
        let elapsed = start.elapsed();

        *gate.lock().unwrap() = true;
        gate_cv.notify_all();
        let drained = wait_until(Duration::from_secs(5), || {
            semaphore.available_permits() == 1
        })
        .await;
        let occupier_result = occupier.await;

        assert!(
            entered,
            "the occupying closure must reserve the stats permit"
        );
        assert_eq!(result.unwrap_err(), "stats_ffi_queue_timeout");
        assert!(elapsed < Duration::from_millis(1000));
        assert!(
            drained,
            "the occupier must release its permit after the gate opens"
        );
        assert!(occupier_result.is_ok());
    }

    #[tokio::test]
    async fn stats_ffi_overloads_when_its_bounded_waiter_queue_is_full() {
        let semaphore = Arc::new(Semaphore::new(1));
        let waiter_semaphore = Arc::new(Semaphore::new(MAX_QUEUED_STATS_FFI));
        let gate = Arc::new(Mutex::new(false));
        let gate_cv = Arc::new(Condvar::new());
        let _gate_cleanup = CondvarGateCleanup(gate.clone(), gate_cv.clone());
        let active_gate = gate.clone();
        let active_cv = gate_cv.clone();
        let occupier = tokio::spawn({
            let semaphore = semaphore.clone();
            let waiter_semaphore = waiter_semaphore.clone();
            async move {
                let _ = dispatch_stats_ffi_with(
                    &semaphore,
                    &waiter_semaphore,
                    Duration::from_secs(30),
                    move |control| {
                        control.claim_dll_start()?;
                        let mut open = active_gate.lock().unwrap();
                        while !*open {
                            open = active_cv.wait(open).unwrap();
                        }
                        Ok(())
                    },
                )
                .await;
            }
        });
        let active_entered = wait_until(Duration::from_secs(5), || {
            semaphore.available_permits() == 0
        })
        .await;

        let mut waiters = Vec::new();
        for _ in 0..MAX_QUEUED_STATS_FFI {
            let semaphore = semaphore.clone();
            let waiter_semaphore = waiter_semaphore.clone();
            waiters.push(tokio::spawn(async move {
                dispatch_stats_ffi_with(
                    &semaphore,
                    &waiter_semaphore,
                    Duration::from_secs(10),
                    |control| {
                        control.claim_dll_start()?;
                        Ok(())
                    },
                )
                .await
            }));
        }
        let waiters_entered = wait_until(Duration::from_secs(5), || {
            waiter_semaphore.available_permits() == 0
        })
        .await;
        let overloaded = dispatch_stats_ffi_with(
            &semaphore,
            &waiter_semaphore,
            Duration::from_secs(5),
            |_control| Ok(()),
        )
        .await;

        *gate.lock().unwrap() = true;
        gate_cv.notify_all();
        let occupier_result = occupier.await;
        let mut waiter_results = Vec::new();
        for waiter in waiters {
            waiter_results.push(waiter.await);
        }
        let active_restored = semaphore.available_permits();
        let waiters_restored = waiter_semaphore.available_permits();

        assert!(active_entered, "stats closure must enter before queueing");
        assert!(waiters_entered, "all four stats waiters must be admitted");
        assert!(occupier_result.is_ok());
        assert!(waiter_results.iter().all(|r| matches!(r, Ok(Ok(())))));
        assert_eq!(overloaded.unwrap_err(), "stats_ffi_overloaded");
        assert_eq!(active_restored, 1);
        assert_eq!(waiters_restored, MAX_QUEUED_STATS_FFI);
    }

    #[tokio::test]
    #[allow(clippy::await_holding_lock)] // Controlled write lock plus global DLL guard.
    async fn stats_lock_deadline_prevents_late_dll_entry_and_recovers() {
        let _api_test_lock = backend_api::lock_test_c_api();
        let app_data_dir = setup_real_dll_app_data("r01-stats-lock");
        backend_api::reset_api_call_test_metrics();
        let semaphore = Arc::new(Semaphore::new(1));
        let waiter_semaphore = Arc::new(Semaphore::new(1));
        let entries = Arc::new(AtomicUsize::new(0));

        let blocked_readers = backend_api::lock_api_for_test();
        let blocked_call = {
            let semaphore = semaphore.clone();
            let waiter_semaphore = waiter_semaphore.clone();
            let entries = entries.clone();
            tokio::spawn(async move {
                dispatch_stats_ffi_with(
                    &semaphore,
                    &waiter_semaphore,
                    Duration::from_millis(600),
                    move |control| call_real_stats_summary(control, entries),
                )
                .await
            })
        };
        let reached_wrapper = wait_until(Duration::from_secs(3), || {
            backend_api::API_CALL_TEST_METRICS
                .read_lock_attempted
                .load(Ordering::SeqCst)
                == 1
        })
        .await;
        let timed_out = blocked_call.await.unwrap();
        let lock_closure_drained = wait_until(Duration::from_secs(5), || {
            semaphore.available_permits() == 1
                && backend_api::API_CALL_TEST_METRICS
                    .read_lock_timed_out
                    .load(Ordering::SeqCst)
                    == 1
        })
        .await;
        let entries_before_unlock = entries.load(Ordering::SeqCst);
        let read_attempts = backend_api::API_CALL_TEST_METRICS
            .read_lock_attempted
            .load(Ordering::SeqCst);
        let read_timeouts = backend_api::API_CALL_TEST_METRICS
            .read_lock_timed_out
            .load(Ordering::SeqCst);
        let stats_capacity_after_timeout = semaphore.available_permits();

        drop(blocked_readers);
        let recovered =
            dispatch_stats_ffi_with(&semaphore, &waiter_semaphore, Duration::from_secs(3), {
                let entries = entries.clone();
                move |control| call_real_stats_summary(control, entries)
            })
            .await;
        let entries_after_recovery = entries.load(Ordering::SeqCst);
        let waiter_capacity_restored = waiter_semaphore.available_permits();
        let stats_capacity_restored = semaphore.available_permits();

        backend_api::shutdown_c_api();
        let _ = std::fs::remove_dir_all(&app_data_dir);

        assert_eq!(timed_out.unwrap_err(), "stats_ffi_timeout");
        assert!(
            reached_wrapper,
            "stats closure must reach the locked API wrapper"
        );
        assert!(
            lock_closure_drained,
            "the physical stats closure must release its permit"
        );
        assert_eq!(
            entries_before_unlock, 0,
            "expired stats request must not enter DLL"
        );
        assert_eq!(read_attempts, 1);
        assert_eq!(read_timeouts, 1);
        assert_eq!(
            stats_capacity_after_timeout, 1,
            "lock-wait closure must drain"
        );
        assert!(
            recovered.is_ok(),
            "real stats call must recover after unlock"
        );
        assert_eq!(entries_after_recovery, 1);
        assert_eq!(waiter_capacity_restored, 1);
        assert_eq!(stats_capacity_restored, 1);
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
    fn deadline_for_signature_family_probe_is_debug_only() {
        let got = deadline_for_path("/diagnostics/signature-family");
        if cfg!(debug_assertions) {
            assert_eq!(
                got,
                Duration::from_millis(deadlines::kDeadlineSignatureFamilyProbeMs),
                "debug builds widen the outer watchdog for the diagnostic probe path"
            );
        } else {
            assert_eq!(
                got,
                Duration::from_millis(deadlines::kDeadlineGenericMs),
                "release builds must keep the generic bucket: the native route does not exist"
            );
        }
    }

    #[test]
    fn rust_outer_deadlines_are_at_least_cpp_inner() {
        // Outer Tauri timeout must not be shorter than C++ scheduler budget.
        for (path, native_ms) in [
            ("/song/url", deadlines::kDeadlineSongUrlMs),
            ("/images/test", deadlines::kDeadlineImageMs),
            ("/login/qr/check", deadlines::kDeadlineLoginPollMs),
            ("/search", deadlines::kDeadlineSearchMs),
            ("/playlist", deadlines::kDeadlinePlaylistMs),
            ("/unknown/route", deadlines::kDeadlineGenericMs),
        ] {
            assert!(
                deadline_for_path(path).as_millis() >= native_ms as u128,
                "{path}"
            );
        }
        const {
            assert!(deadlines::kFrontendTimeoutMs >= deadlines::kDeadlineGenericMs);
            assert!(deadlines::kDeadlineGenericMs >= 1000);
        }
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
            .split(['\n', ';'])
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
