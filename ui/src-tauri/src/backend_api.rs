use libloading::{Library, Symbol};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
#[cfg(test)]
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize};
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::{OnceLock, RwLock};
use std::time::{Duration, Instant};

// Resolved entry points from the DLL. The Library handle is kept alive so the
// function pointers stay valid. A read guard is held for the duration of each
// C++ call: concurrent reads allow concurrent calls (once the C++ side is made
// thread-safe), while shutdown takes a write guard and therefore waits for all
// in-flight calls to drain before unloading the library.
pub struct CApiHandle {
    #[allow(dead_code)]
    _lib: Library,
    pub(crate) handle_req: unsafe extern "C" fn(
        method: *const c_char,
        path: *const c_char,
        query_json: *const c_char,
        headers_json: *const c_char,
        body: *const c_char,
        out_response: *mut *mut c_char,
    ),
    pub(crate) free_str: unsafe extern "C" fn(str: *mut c_char),
    shutdown: unsafe extern "C" fn() -> c_int,
    // EchoStats C API exports
    // B06: RecordPlay reports its outcome (see C_API.h EchoStatsRecordStatus).
    pub(crate) stats_record_play: unsafe extern "C" fn(*const c_char) -> c_int,
    pub(crate) stats_get_summary: unsafe extern "C" fn(*const c_char) -> *mut c_char,
    pub(crate) stats_get_top:
        unsafe extern "C" fn(*const c_char, *const c_char, c_int) -> *mut c_char,
    pub(crate) stats_get_timeline: unsafe extern "C" fn(*const c_char) -> *mut c_char,
    pub(crate) stats_get_recent: unsafe extern "C" fn(c_int, c_int) -> *mut c_char,
    pub(crate) stats_get_recommendations: unsafe extern "C" fn(c_int) -> *mut c_char,
}

static C_API_HANDLE: OnceLock<RwLock<Option<CApiHandle>>> = OnceLock::new();

const REQUEST_PENDING: u8 = 0;
const REQUEST_STARTED: u8 = 1;
const REQUEST_CANCELLED: u8 = 2;
#[cfg(test)]
const DEFAULT_API_LOCK_TIMEOUT: Duration = Duration::from_secs(3);
#[cfg(test)]
const DEFAULT_HANDLE_REQUEST_TIMEOUT: Duration = Duration::from_secs(12);

/// Caller cancellation and the final FFI-start decision share one atomic
/// state. `Started` is the linearization point after which the DLL call must
/// finish with its read guard intact, even if its async caller has left.
pub(crate) struct RequestControl {
    deadline: Instant,
    state: AtomicU8,
    #[cfg(test)]
    read_lock_contended: AtomicBool,
}

impl RequestControl {
    pub(crate) fn new(deadline: Instant) -> std::sync::Arc<Self> {
        std::sync::Arc::new(Self {
            deadline,
            state: AtomicU8::new(REQUEST_PENDING),
            #[cfg(test)]
            read_lock_contended: AtomicBool::new(false),
        })
    }

    pub(crate) fn deadline(&self) -> Instant {
        self.deadline
    }

    pub(crate) fn cancel_pending(&self) {
        let _ = self.state.compare_exchange(
            REQUEST_PENDING,
            REQUEST_CANCELLED,
            Ordering::AcqRel,
            Ordering::Acquire,
        );
    }

    pub(crate) fn ensure_pending(&self) -> Result<(), String> {
        if Instant::now() >= self.deadline {
            self.cancel_pending();
            return Err("request_deadline".into());
        }
        if self.state.load(Ordering::Acquire) == REQUEST_PENDING {
            Ok(())
        } else {
            Err("request_deadline".into())
        }
    }

    /// Win the pending/cancelled race immediately before entering the DLL.
    /// The DLL itself starts a separate native deadline after this point.
    pub(crate) fn claim_dll_start(&self) -> Result<(), String> {
        if Instant::now() >= self.deadline {
            self.cancel_pending();
            return Err("request_deadline".into());
        }
        self.state
            .compare_exchange(
                REQUEST_PENDING,
                REQUEST_STARTED,
                Ordering::AcqRel,
                Ordering::Acquire,
            )
            .map(|_| ())
            .map_err(|_| "request_deadline".into())
    }
}

#[cfg(test)]
pub(crate) struct ApiCallTestMetrics {
    pub wrapper_started: AtomicUsize,
    pub read_lock_attempted: AtomicUsize,
    pub read_lock_contended_requests: AtomicUsize,
    pub read_lock_acquired: AtomicUsize,
    pub read_lock_timed_out: AtomicUsize,
    pub read_lock_wait_nanos: AtomicU64,
    pub dll_entered: AtomicUsize,
    pub dll_returned: AtomicUsize,
    pub native_status_200: AtomicUsize,
    pub native_status_404: AtomicUsize,
    pub native_status_504: AtomicUsize,
    pub native_status_other: AtomicUsize,
    pub dll_string_freed: AtomicUsize,
}

#[cfg(test)]
pub(crate) static API_CALL_TEST_METRICS: ApiCallTestMetrics = ApiCallTestMetrics {
    wrapper_started: AtomicUsize::new(0),
    read_lock_attempted: AtomicUsize::new(0),
    read_lock_contended_requests: AtomicUsize::new(0),
    read_lock_acquired: AtomicUsize::new(0),
    read_lock_timed_out: AtomicUsize::new(0),
    read_lock_wait_nanos: AtomicU64::new(0),
    dll_entered: AtomicUsize::new(0),
    dll_returned: AtomicUsize::new(0),
    native_status_200: AtomicUsize::new(0),
    native_status_404: AtomicUsize::new(0),
    native_status_504: AtomicUsize::new(0),
    native_status_other: AtomicUsize::new(0),
    dll_string_freed: AtomicUsize::new(0),
};

#[cfg(test)]
pub(crate) type ApiTestHook = std::sync::Arc<dyn Fn() + Send + Sync>;

#[cfg(test)]
static BEFORE_DLL_START_TEST_HOOK: std::sync::Mutex<Option<ApiTestHook>> =
    std::sync::Mutex::new(None);
#[cfg(test)]
static AFTER_DLL_RETURN_TEST_HOOK: std::sync::Mutex<Option<ApiTestHook>> =
    std::sync::Mutex::new(None);

#[cfg(test)]
pub(crate) fn set_before_dll_start_test_hook(hook: Option<ApiTestHook>) {
    *BEFORE_DLL_START_TEST_HOOK
        .lock()
        .unwrap_or_else(|p| p.into_inner()) = hook;
}

#[cfg(test)]
pub(crate) fn set_after_dll_return_test_hook(hook: Option<ApiTestHook>) {
    *AFTER_DLL_RETURN_TEST_HOOK
        .lock()
        .unwrap_or_else(|p| p.into_inner()) = hook;
}

#[cfg(test)]
fn run_api_test_hook(slot: &std::sync::Mutex<Option<ApiTestHook>>) {
    let hook = slot.lock().unwrap_or_else(|p| p.into_inner()).clone();
    if let Some(hook) = hook {
        hook();
    }
}

#[cfg(test)]
pub(crate) static TEST_C_API_GUARD: std::sync::Mutex<()> = std::sync::Mutex::new(());

#[cfg(test)]
pub(crate) fn lock_test_c_api() -> std::sync::MutexGuard<'static, ()> {
    TEST_C_API_GUARD.lock().unwrap_or_else(|p| p.into_inner())
}

fn get_handle() -> &'static RwLock<Option<CApiHandle>> {
    C_API_HANDLE.get_or_init(|| RwLock::new(None))
}

/// Fast health check for the Tauri ping command. A contended or poisoned
/// handle state fails closed so the UI cannot report a healthy backend while
/// native initialization or shutdown is unresolved.
pub fn is_initialized() -> bool {
    match get_handle().try_read() {
        Ok(guard) => guard.is_some(),
        Err(std::sync::TryLockError::Poisoned(_)) => false,
        Err(std::sync::TryLockError::WouldBlock) => false,
    }
}

#[cfg(test)]
pub(crate) fn lock_api_for_test() -> std::sync::RwLockWriteGuard<'static, Option<CApiHandle>> {
    get_handle().write().unwrap_or_else(|p| p.into_inner())
}

#[cfg(test)]
pub(crate) fn api_write_lock_is_blocked_for_test() -> bool {
    matches!(
        get_handle().try_write(),
        Err(std::sync::TryLockError::WouldBlock)
    )
}

#[cfg(test)]
pub(crate) fn reset_api_call_test_metrics() {
    API_CALL_TEST_METRICS
        .wrapper_started
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .read_lock_attempted
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .read_lock_contended_requests
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .read_lock_acquired
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .read_lock_timed_out
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .read_lock_wait_nanos
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS.dll_entered.store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .dll_returned
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .native_status_200
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .native_status_404
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .native_status_504
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .native_status_other
        .store(0, Ordering::SeqCst);
    API_CALL_TEST_METRICS
        .dll_string_freed
        .store(0, Ordering::SeqCst);
}

/// Get a read guard on the C API handle. Multiple concurrent readers are
/// allowed; the guard must be held for the duration of any C API call. This
/// default helper bounds only time spent waiting for the Rust lock.
#[cfg(test)]
pub fn api_handle() -> Result<std::sync::RwLockReadGuard<'static, Option<CApiHandle>>, String> {
    api_handle_until(Instant::now() + DEFAULT_API_LOCK_TIMEOUT, None)
}

pub(crate) fn api_handle_for_request(
    control: &RequestControl,
) -> Result<std::sync::RwLockReadGuard<'static, Option<CApiHandle>>, String> {
    api_handle_until(control.deadline(), Some(control))
}

fn api_handle_until(
    deadline: Instant,
    control: Option<&RequestControl>,
) -> Result<std::sync::RwLockReadGuard<'static, Option<CApiHandle>>, String> {
    #[cfg(test)]
    API_CALL_TEST_METRICS
        .read_lock_attempted
        .fetch_add(1, Ordering::SeqCst);
    #[cfg(test)]
    let wait_started = Instant::now();
    let lock_timeout = || {
        #[cfg(test)]
        {
            API_CALL_TEST_METRICS
                .read_lock_timed_out
                .fetch_add(1, Ordering::SeqCst);
            API_CALL_TEST_METRICS.read_lock_wait_nanos.fetch_add(
                wait_started.elapsed().as_nanos().min(u64::MAX as u128) as u64,
                Ordering::SeqCst,
            );
        }
        Err("request_deadline".to_string())
    };

    loop {
        if Instant::now() >= deadline
            || control.is_some_and(|request| request.ensure_pending().is_err())
        {
            if let Some(request) = control {
                request.cancel_pending();
            }
            return lock_timeout();
        }

        match get_handle().try_read() {
            Ok(guard) => {
                #[cfg(test)]
                {
                    API_CALL_TEST_METRICS
                        .read_lock_acquired
                        .fetch_add(1, Ordering::SeqCst);
                    API_CALL_TEST_METRICS.read_lock_wait_nanos.fetch_add(
                        wait_started.elapsed().as_nanos().min(u64::MAX as u128) as u64,
                        Ordering::SeqCst,
                    );
                }
                // Do not let a lock acquired after caller cancellation turn a
                // pending command into a late native side effect.
                if Instant::now() >= deadline
                    || control.is_some_and(|request| request.ensure_pending().is_err())
                {
                    drop(guard);
                    if let Some(request) = control {
                        request.cancel_pending();
                    }
                    return Err("request_deadline".into());
                }
                if guard.is_none() {
                    return Err("C API not loaded".into());
                }
                return Ok(guard);
            }
            Err(std::sync::TryLockError::Poisoned(poisoned)) => {
                // A poisoned lock still carries its guard; preserve the
                // existing recovery policy and never retry Poisoned as if it
                // were ordinary contention.
                let guard = poisoned.into_inner();
                #[cfg(test)]
                {
                    API_CALL_TEST_METRICS
                        .read_lock_acquired
                        .fetch_add(1, Ordering::SeqCst);
                    API_CALL_TEST_METRICS.read_lock_wait_nanos.fetch_add(
                        wait_started.elapsed().as_nanos().min(u64::MAX as u128) as u64,
                        Ordering::SeqCst,
                    );
                }
                if Instant::now() >= deadline
                    || control.is_some_and(|request| request.ensure_pending().is_err())
                {
                    drop(guard);
                    if let Some(request) = control {
                        request.cancel_pending();
                    }
                    return Err("request_deadline".into());
                }
                if guard.is_none() {
                    return Err("C API not loaded".into());
                }
                return Ok(guard);
            }
            Err(std::sync::TryLockError::WouldBlock) => {
                #[cfg(test)]
                if let Some(request) = control {
                    if !request.read_lock_contended.swap(true, Ordering::AcqRel) {
                        API_CALL_TEST_METRICS
                            .read_lock_contended_requests
                            .fetch_add(1, Ordering::SeqCst);
                    }
                }
                let now = Instant::now();
                if now >= deadline {
                    if let Some(request) = control {
                        request.cancel_pending();
                    }
                    return lock_timeout();
                }
                let remaining = deadline.saturating_duration_since(now);
                std::thread::sleep(remaining.min(Duration::from_millis(2)));
            }
        }
    }
}

/// Load the DLL and initialize C++ backend with an explicit app data directory.
/// `app_data_dir` controls where SQLite (`bottlemusic.db`) is created.
pub fn init_with_paths(dll_path: &str, app_data_dir: Option<&str>) -> Result<(), String> {
    let mut guard = get_handle().write().unwrap_or_else(|p| p.into_inner());
    if guard.is_some() {
        return Ok(());
    }

    unsafe {
        let lib = Library::new(dll_path).map_err(|e| e.to_string())?;

        // Resolve every required symbol before initialization. If the DLL is
        // incompatible, dropping Library is safe because no C++ thread exists.
        let init_with_paths_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*const c_char) -> c_int> = lib
                .get(b"EchoInitializeWithPathsV2")
                .map_err(|e| e.to_string())?;
            *sym
        };
        let init_ptr = {
            let sym: Symbol<unsafe extern "C" fn() -> c_int> =
                lib.get(b"EchoInitializeV2").map_err(|e| e.to_string())?;
            *sym
        };
        let get_last_error_ptr = {
            let sym: Symbol<unsafe extern "C" fn() -> *mut c_char> =
                lib.get(b"EchoGetLastError").map_err(|e| e.to_string())?;
            *sym
        };
        let shutdown_ptr = {
            let sym: Symbol<unsafe extern "C" fn() -> c_int> =
                lib.get(b"EchoShutdown").map_err(|e| e.to_string())?;
            *sym
        };

        let handle_req_ptr = {
            let sym: Symbol<
                unsafe extern "C" fn(
                    *const c_char,
                    *const c_char,
                    *const c_char,
                    *const c_char,
                    *const c_char,
                    *mut *mut c_char,
                ),
            > = lib.get(b"EchoHandleRequest").map_err(|e| e.to_string())?;
            *sym
        };

        let free_str_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*mut c_char)> =
                lib.get(b"EchoFreeString").map_err(|e| e.to_string())?;
            *sym
        };

        // EchoStats symbols
        let stats_record_play_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*const c_char) -> c_int> =
                lib.get(b"EchoStatsRecordPlay").map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_summary_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*const c_char) -> *mut c_char> =
                lib.get(b"EchoStatsGetSummary").map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_top_ptr = {
            let sym: Symbol<
                unsafe extern "C" fn(*const c_char, *const c_char, c_int) -> *mut c_char,
            > = lib.get(b"EchoStatsGetTop").map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_timeline_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*const c_char) -> *mut c_char> = lib
                .get(b"EchoStatsGetTimeline")
                .map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_recent_ptr = {
            let sym: Symbol<unsafe extern "C" fn(c_int, c_int) -> *mut c_char> =
                lib.get(b"EchoStatsGetRecent").map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_recommendations_ptr = {
            let sym: Symbol<unsafe extern "C" fn(c_int) -> *mut c_char> = lib
                .get(b"EchoStatsGetRecommendations")
                .map_err(|e| e.to_string())?;
            *sym
        };

        let init_status = if let Some(dir) = app_data_dir {
            let c_dir = CString::new(dir).map_err(|e| e.to_string())?;
            init_with_paths_ptr(c_dir.as_ptr())
        } else {
            init_ptr()
        };
        if init_status != 0 {
            let error_ptr = get_last_error_ptr();
            let message = if error_ptr.is_null() {
                format!("C API initialization failed with status {init_status}")
            } else {
                let text = CStr::from_ptr(error_ptr).to_string_lossy().into_owned();
                free_str_ptr(error_ptr);
                text
            };
            let shutdown_status = shutdown_ptr();
            if shutdown_status > 0 {
                std::mem::forget(lib);
            }
            return Err(message);
        }

        *guard = Some(CApiHandle {
            _lib: lib,
            handle_req: handle_req_ptr,
            free_str: free_str_ptr,
            shutdown: shutdown_ptr,
            stats_record_play: stats_record_play_ptr,
            stats_get_summary: stats_get_summary_ptr,
            stats_get_top: stats_get_top_ptr,
            stats_get_timeline: stats_get_timeline_ptr,
            stats_get_recent: stats_get_recent_ptr,
            stats_get_recommendations: stats_get_recommendations_ptr,
        });
    }
    Ok(())
}

pub fn shutdown_c_api() {
    // Bounded shutdown: try to acquire the write guard for up to 5 seconds
    // using only non-blocking try_write. If we can't get it in time, we
    // do NOT fall back to blocking write() — that would hang forever if
    // in-flight spawn_blocking tasks still hold read guards. Instead we leave
    // the initialized handle and DLL mapping alive for the OS to reclaim at
    // process exit.
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
    let mut guard = loop {
        match get_handle().try_write() {
            Ok(g) => break g,
            Err(std::sync::TryLockError::Poisoned(p)) => break p.into_inner(),
            Err(std::sync::TryLockError::WouldBlock) => {
                if std::time::Instant::now() >= deadline {
                    // Could not acquire — give up rather than block.
                    // The OS reclaims the library at process exit.
                    eprintln!("[EchoCAPI WARN] shutdown_c_api: could not acquire write guard in 5s, skipping EchoShutdown");
                    return;
                }
                std::thread::sleep(std::time::Duration::from_millis(50));
            }
        }
    };
    if let Some(handle) = guard.take() {
        // P0-B: a non-zero status means C++ workers or lock holders may still
        // execute inside the DLL. FreeLibrary would unmap their code pages, so
        // retain the mapping until process exit (the OS reclaims it).
        let shutdown_status = unsafe { (handle.shutdown)() };
        if shutdown_status != 0 {
            eprintln!(
                "[EchoCAPI WARN] shutdown_c_api: unsafe-to-unload status {shutdown_status}; \
                 leaking DLL mapping to avoid use-after-unload"
            );
            std::mem::forget(handle);
        } else {
            drop(handle);
        }
    }
}

#[cfg(test)]
pub fn handle_request(
    method: &str,
    path: &str,
    query_json: Option<&str>,
    headers_json: Option<&str>,
    body: Option<&str>,
) -> Result<String, String> {
    let control = RequestControl::new(Instant::now() + DEFAULT_HANDLE_REQUEST_TIMEOUT);
    handle_request_with_control(method, path, query_json, headers_json, body, control)
}

pub(crate) fn handle_request_with_control(
    method: &str,
    path: &str,
    query_json: Option<&str>,
    headers_json: Option<&str>,
    body: Option<&str>,
    control: std::sync::Arc<RequestControl>,
) -> Result<String, String> {
    #[cfg(test)]
    API_CALL_TEST_METRICS
        .wrapper_started
        .fetch_add(1, Ordering::SeqCst);
    control.ensure_pending()?;
    // Build the C strings first — no lock needed, and it keeps the read-guard
    // window as small as possible.
    let c_method = CString::new(method).map_err(|e| e.to_string())?;
    let c_path = CString::new(path).map_err(|e| e.to_string())?;
    let c_query = query_json
        .map(|s| CString::new(s).map_err(|e| e.to_string()))
        .transpose()?;
    let c_headers = headers_json
        .map(|s| CString::new(s).map_err(|e| e.to_string()))
        .transpose()?;
    let c_body = body
        .map(|s| CString::new(s).map_err(|e| e.to_string()))
        .transpose()?;

    let ptr_method = c_method.as_ptr();
    let ptr_path = c_path.as_ptr();
    let ptr_query = c_query.as_ref().map_or(std::ptr::null(), |s| s.as_ptr());
    let ptr_headers = c_headers.as_ref().map_or(std::ptr::null(), |s| s.as_ptr());
    let ptr_body = c_body.as_ref().map_or(std::ptr::null(), |s| s.as_ptr());

    // Hold the read guard for the whole C++ call. Multiple requests can hold the
    // read guard at once (concurrent calls), but shutdown's write guard waits for
    // them all to release — so the library can never be unloaded mid-call.
    let guard = api_handle_for_request(&control)?;
    let handle = guard.as_ref().ok_or("C API not loaded")?;

    unsafe {
        let mut out_response: *mut c_char = std::ptr::null_mut();

        // The caller-drop/deadline race is resolved while the read guard is
        // held and immediately before the non-cancellable DLL call. If cancel
        // wins Pending→Cancelled, no native route side effect starts.
        #[cfg(test)]
        run_api_test_hook(&BEFORE_DLL_START_TEST_HOOK);
        control.claim_dll_start()?;
        #[cfg(test)]
        API_CALL_TEST_METRICS
            .dll_entered
            .fetch_add(1, Ordering::SeqCst);
        (handle.handle_req)(
            ptr_method,
            ptr_path,
            ptr_query,
            ptr_headers,
            ptr_body,
            &mut out_response,
        );
        #[cfg(test)]
        API_CALL_TEST_METRICS
            .dll_returned
            .fetch_add(1, Ordering::SeqCst);

        if out_response.is_null() {
            return Err("Empty response from C API".to_string());
        }

        #[cfg(test)]
        run_api_test_hook(&AFTER_DLL_RETURN_TEST_HOOK);

        let resp_str = CStr::from_ptr(out_response).to_string_lossy().into_owned();
        (handle.free_str)(out_response);
        #[cfg(test)]
        API_CALL_TEST_METRICS
            .dll_string_freed
            .fetch_add(1, Ordering::SeqCst);

        #[cfg(test)]
        match serde_json::from_str::<serde_json::Value>(&resp_str)
            .ok()
            .and_then(|value| value.get("status").and_then(serde_json::Value::as_u64))
        {
            Some(200) => {
                API_CALL_TEST_METRICS
                    .native_status_200
                    .fetch_add(1, Ordering::SeqCst);
            }
            Some(404) => {
                API_CALL_TEST_METRICS
                    .native_status_404
                    .fetch_add(1, Ordering::SeqCst);
            }
            Some(504) => {
                API_CALL_TEST_METRICS
                    .native_status_504
                    .fetch_add(1, Ordering::SeqCst);
            }
            _ => {
                API_CALL_TEST_METRICS
                    .native_status_other
                    .fetch_add(1, Ordering::SeqCst);
            }
        }

        Ok(resp_str)
    }
}

// 由宿主（lib.rs setup）在初始化时指定的可写日志目录，通常是 Tauri 的
// app_data_dir（跨平台、用户可写、与 SQLite 同根）。空表示未设置，会走回退。
static LOG_DIR: OnceLock<std::path::PathBuf> = OnceLock::new();

/// 设置日志目录。应在 `init_with_paths` 之后、`set_log_callback` 之前调用一次。
/// 接收与 C++ 后端相同的 app_data_dir；日志落在 `<app_data_dir>/logs/`。
/// 若未调用或传入空串，`log_file()` 会回退到 exe 同级 / 当前目录。
pub fn set_log_dir(dir: &str) {
    if dir.is_empty() {
        return;
    }
    // 已设置则忽略后续调用 —— 第一次写入决定路径（与 log_file 的 OnceLock 语义对齐）。
    let _ = LOG_DIR.set(std::path::PathBuf::from(dir));
}

// 日志文件：优先用宿主指定的 app_data_dir/logs（跨平台、用户可写、与 DB 同根，
// 也是安装版唯一可靠的写入位置）；若未指定或不可写，回退到 exe 同级 logs/，
// 最后回退到当前工作目录 logs/。
//
// B11: 文件按「写入当天」选择 —— 旧实现用 OnceLock 缓存第一次打开的 File，
// 文件名里的日期只在初始化时决定，长期运行跨日后所有日志仍写进旧文件。
// 现在每次写入比较当前日期与已打开文件的日期，跨日自动轮转到新文件。
// 轮转时顺带清理 14 天前的旧日志（保留策略）；轮转失败（新文件打不开）
// 会向 stderr 发一条警告并继续写旧文件 —— 写入永不静默丢失。
//
// 可注入时钟（测试用）：LOG_NOW_SECS 覆盖为 >0 的 epoch 秒时，日期判断使用
// 该值；生产恒为 0，走系统时钟。
static LOG_NOW_SECS: std::sync::atomic::AtomicI64 = std::sync::atomic::AtomicI64::new(0);

/// 测试专用：覆盖日志时钟。传 0 恢复系统时钟。
#[cfg(test)]
pub fn set_log_clock_for_test(epoch_secs: i64) {
    LOG_NOW_SECS.store(epoch_secs, std::sync::atomic::Ordering::Release);
}

fn log_today() -> chrono::DateTime<chrono::Local> {
    let override_secs = LOG_NOW_SECS.load(std::sync::atomic::Ordering::Acquire);
    if override_secs > 0 {
        return chrono::DateTime::<chrono::Utc>::from_timestamp(override_secs, 0)
            .unwrap_or_default()
            .with_timezone(&chrono::Local);
    }
    chrono::Local::now()
}

struct LogSink {
    file: Option<std::fs::File>,
    /// 已打开文件所属的日期（%Y%m%d）。
    day: String,
    /// 当前文件完整路径；None 表示从未成功打开（例如目录不可写）。
    path: Option<std::path::PathBuf>,
}

static LOG_SINK: OnceLock<std::sync::Mutex<LogSink>> = OnceLock::new();

/// 日志保留策略：轮转时删除该目录下早于 14 天的 bottlemusic-*.log。
const LOG_RETENTION_DAYS: i64 = 14;

fn open_log_file_for(day: &str) -> Option<(std::path::PathBuf, std::fs::File)> {
    // 已设置则忽略后续调用 —— 第一次写入决定路径（与 LOG_SINK 的语义对齐）。
    let mut candidates: Vec<std::path::PathBuf> = Vec::new();
    if let Some(host_dir) = LOG_DIR.get() {
        candidates.push(host_dir.join("logs"));
    }
    if let Some(exe) = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|p| p.to_path_buf()))
    {
        candidates.push(exe.join("logs"));
    }
    candidates.push(std::path::PathBuf::from("logs"));

    let dir = candidates
        .iter()
        .map(|d| (d.clone(), std::fs::create_dir_all(d)))
        .find(|(_, r)| r.is_ok())
        .map(|(d, _)| d)
        .unwrap_or_else(|| candidates[0].clone());

    let path = dir.join(format!("bottlemusic-{day}.log"));
    let file = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
        .ok()?;
    Some((path, file))
}

fn log_sink() -> &'static std::sync::Mutex<LogSink> {
    LOG_SINK.get_or_init(|| {
        let today = log_today().format("%Y%m%d").to_string();
        let (path, file) = match open_log_file_for(&today) {
            Some(ok) => (Some(ok.0), Some(ok.1)),
            None => (None, None),
        };
        if let Some(path) = &path {
            println!("[EchoCAPI] 日志写入: {}", path.display());
        }
        std::sync::Mutex::new(LogSink {
            file,
            day: today,
            path,
        })
    })
}

/// 跨日轮转 + 旧日志清理。返回仍可写入的 File（旧文件或新文件）。
/// 失败路径：新文件打不开 -> 保留旧文件继续写，并向 stderr 发一条警告。
fn rotate_log_if_needed(sink: &mut LogSink) {
    let today = log_today().format("%Y%m%d").to_string();
    if today == sink.day && sink.file.is_some() {
        return;
    }
    match open_log_file_for(&today) {
        Some((path, file)) => {
            sink.file = Some(file);
            sink.day = today;
            sink.path = Some(path);
            prune_old_logs(sink);
        }
        None => {
            // 失败反馈：不静默。旧文件（若有）继续承载写入。
            eprintln!(
                "[EchoCAPI] warn [logging] log rotation to {today} failed; continuing with previous log file"
            );
            if sink.file.is_some() {
                sink.day = today; // 标记已尝试，避免每行日志都刷警告
            }
        }
    }
}

fn prune_old_logs(sink: &LogSink) {
    let Some(path) = sink.path.as_ref() else {
        return;
    };
    let Some(dir) = path.parent() else {
        return;
    };
    let cutoff = (log_today() - chrono::Duration::days(LOG_RETENTION_DAYS))
        .format("%Y%m%d")
        .to_string();
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let name = entry.file_name();
        let Some(name) = name.to_str() else {
            continue;
        };
        let Some(day) = name
            .strip_prefix("bottlemusic-")
            .and_then(|r| r.strip_suffix(".log"))
        else {
            continue;
        };
        if day.len() == 8 && day < cutoff.as_str() {
            let _ = std::fs::remove_file(entry.path());
        }
    }
}

fn write_log_line(line: &str) {
    use std::io::Write as _;
    if let Ok(mut sink) = log_sink().lock() {
        rotate_log_if_needed(&mut sink);
        if let Some(f) = sink.file.as_mut() {
            let _ = writeln!(f, "{}", line);
            let _ = f.flush();
        }
    }
}

extern "C" fn ffi_log_callback(
    level: c_int,
    tag: *const c_char,
    msg: *const c_char,
    _ud: *mut c_void,
) {
    if tag.is_null() || msg.is_null() {
        return;
    }
    let tag_str = unsafe { CStr::from_ptr(tag) }.to_string_lossy();
    let msg_str = unsafe { CStr::from_ptr(msg) }.to_string_lossy();
    let level_str = match level {
        0 => "debug",
        1 => "info ",
        2 => "warn ",
        _ => "error",
    };
    let ts = chrono::Local::now().format("%Y-%m-%d %H:%M:%S%.3f");
    let line = format!("{} [{}][{}] {}", ts, level_str, tag_str, msg_str);
    // 控制台（dev 终端可见）
    if level >= 2 {
        eprintln!("{}", line);
    } else {
        println!("{}", line);
    }
    // 文件（release 无控制台时也能查）
    write_log_line(&line);
}

/// Register a log callback so C++ diagnostic output is forwarded to Rust stdout.
/// Call this after `init_with_paths` succeeds.
pub fn set_log_callback() -> Result<(), String> {
    let lib_guard = get_handle().read().unwrap_or_else(|p| p.into_inner());
    let handle = lib_guard.as_ref().ok_or("C API not loaded")?;
    unsafe {
        let set_cb: Symbol<
            unsafe extern "C" fn(
                cb: unsafe extern "C" fn(c_int, *const c_char, *const c_char, *mut c_void),
                *mut c_void,
            ),
        > = handle
            ._lib
            .get(b"EchoSetLogCallback")
            .map_err(|e| e.to_string())?;
        set_cb(ffi_log_callback, std::ptr::null_mut());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::LOG_DIR;

    // 与生产 log_today 相同的日期推导（Local 时区 %Y%m%d）。
    fn day_name(epoch_secs: i64) -> String {
        chrono::DateTime::<chrono::Utc>::from_timestamp(epoch_secs, 0)
            .unwrap_or_default()
            .with_timezone(&chrono::Local)
            .format("%Y%m%d")
            .to_string()
    }

    fn set_log_dir_checked(dir: &str) -> bool {
        super::set_log_dir(dir);
        LOG_DIR.get().is_some()
    }

    // B11: cross-day log rotation, rotation-failure feedback, retention.
    // One combined #[test] because the log sink (LOG_DIR OnceLock + LOG_SINK)
    // is process-global; phases run sequentially against one unique directory.
    #[test]
    fn log_file_rotates_by_write_day_with_feedback_and_retention() {
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let dir =
            std::env::temp_dir().join(format!("bm-log-rotate-{}-{}", std::process::id(), stamp));
        std::fs::create_dir_all(&dir).unwrap();
        let _ = set_log_dir_checked(&dir.to_string_lossy());

        let day1 = 1700000000i64; // fixed epoch; date derived via Local tz
        let day2 = day1 + 86_400 * 3; // three days later, same clock seam

        set_log_clock_for_test(day1);
        write_log_line("day-one-line");
        let day1_name = format!("bottlemusic-{}.log", day_name(day1));
        let day1_path = dir.join("logs").join(&day1_name);
        assert!(
            day1_path.exists(),
            "day1 log file must exist at {:?}",
            day1_path
        );
        let day1_after_first = std::fs::read_to_string(&day1_path).unwrap();
        assert!(day1_after_first.contains("day-one-line"));

        set_log_clock_for_test(day2);
        write_log_line("day-two-line");

        let day2_name = format!("bottlemusic-{}.log", day_name(day2));
        let day2_path = dir.join("logs").join(&day2_name);
        assert!(
            day2_path.exists(),
            "cross-day write must create a NEW file for {day2_name}"
        );
        let day2_body = std::fs::read_to_string(&day2_path).unwrap();
        assert!(day2_body.contains("day-two-line"));
        assert!(
            !day2_body.contains("day-one-line"),
            "day1 lines must not leak into the day2 file"
        );
        let day1_body = std::fs::read_to_string(&day1_path).unwrap();
        assert!(
            day1_body.contains("day-one-line") && !day1_body.contains("day-two-line"),
            "the old day1 file must stay intact"
        );

        // Retention: a 20-day-old log is pruned on rotation, a recent one kept.
        let old_day = day2 - 86_400 * 20;
        let old_name = format!("bottlemusic-{}.log", day_name(old_day));
        std::fs::write(dir.join("logs").join(&old_name), "ancient").unwrap();
        let day3 = day2 + 86_400;
        set_log_clock_for_test(day3);
        write_log_line("day-three-line");
        assert!(
            !dir.join("logs").join(&old_name).exists(),
            "log older than 14 days must be pruned"
        );
        assert!(day1_path.exists(), "recent day1 log must be kept");
        assert!(dir
            .join("logs")
            .join(format!("bottlemusic-{}.log", day_name(day3)))
            .exists());

        // Rotation-failure feedback: occupy the target filename with a
        // DIRECTORY so the new file cannot be opened. The line must still
        // land in the previous day's file (no silent loss).
        let day4 = day3 + 86_400;
        let day4_path = dir
            .join("logs")
            .join(format!("bottlemusic-{}.log", day_name(day4)));
        std::fs::create_dir_all(&day4_path).unwrap();
        set_log_clock_for_test(day4);
        write_log_line("day-four-line-blocked");
        let day3_path = dir
            .join("logs")
            .join(format!("bottlemusic-{}.log", day_name(day3)));
        let day3_body = std::fs::read_to_string(&day3_path).unwrap();
        assert!(
            day3_body.contains("day-four-line-blocked"),
            "blocked rotation must keep writing to the previous file"
        );
        assert!(!day4_path.join("anything").exists());

        set_log_clock_for_test(0);
        let _ = std::fs::remove_dir_all(&dir);
    }

    use super::*;
    use std::thread;

    fn test_dll_path() -> String {
        let candidates: Vec<String> = {
            let mut paths: Vec<String> = std::env::var("ECHO_CAPI_DLL").ok().into_iter().collect();
            if cfg!(target_os = "windows") {
                paths.push(format!(
                    "{}/../../native/out/bottlemusic-check/EchoCAPI.dll",
                    env!("CARGO_MANIFEST_DIR")
                ));
                paths.push("../../native/out/bottlemusic-check/EchoCAPI.dll".into());
                paths.push(format!(
                    "{}/target/debug/EchoCAPI.dll",
                    env!("CARGO_MANIFEST_DIR")
                ));
                paths.push(format!("{}/EchoCAPI.dll", env!("CARGO_MANIFEST_DIR")));
            } else {
                paths.push(format!(
                    "{}/../../native/out/bottlemusic-check/libEchoCAPI.so",
                    env!("CARGO_MANIFEST_DIR")
                ));
                paths.push("../../native/out/bottlemusic-check/libEchoCAPI.so".into());
                paths.push(format!(
                    "{}/target/debug/libEchoCAPI.so",
                    env!("CARGO_MANIFEST_DIR")
                ));
            }
            paths
        };
        candidates
            .iter()
            .find(|path| std::path::Path::new(path.as_str()).exists())
            .cloned()
            .unwrap_or_else(|| {
                panic!("Could not find EchoCAPI library in candidates: {candidates:?}")
            })
    }

    #[test]
    fn initialization_failure_is_returned_without_publishing_a_handle() {
        let _lock = lock_test_c_api();
        let dll_path = test_dll_path();
        let invalid_dir = std::env::temp_dir().join(format!(
            "bottlemusic-ffi-invalid-dir-{}",
            std::process::id(),
        ));
        let _ = std::fs::remove_dir_all(&invalid_dir);
        let _ = std::fs::remove_file(&invalid_dir);
        std::fs::write(&invalid_dir, b"not-a-directory").unwrap();

        let error = init_with_paths(&dll_path, invalid_dir.to_str())
            .expect_err("an ordinary file cannot be used as app_data_dir");

        assert!(
            error.contains("initialize failed"),
            "unexpected error: {error}"
        );
        assert!(
            api_handle().is_err(),
            "failed initialization published a handle"
        );
        std::fs::remove_file(&invalid_dir).unwrap();
    }

    #[test]
    fn test_m3_concurrency() {
        let _lock = lock_test_c_api();

        let dll_path = test_dll_path();
        eprintln!("[test_m3_concurrency] using dll: {}", dll_path);

        let app_data_dir = std::env::temp_dir().join(format!(
            "bottlemusic-ffi-concurrency-{}",
            std::process::id(),
        ));
        let _ = std::fs::remove_dir_all(&app_data_dir);
        std::fs::create_dir_all(&app_data_dir).unwrap();

        // This will block/panic if the library cannot be found. Run via `cargo test`.
        init_with_paths(&dll_path, Some(app_data_dir.to_str().unwrap()))
            .expect("Failed to init C API");

        // RequestScheduler maxQueue = workers*4; under 20 concurrent callers the
        // queue may return 504 queue_full. That is backpressure, not a crash —
        // each logical op must still succeed after retry. Zero thread panics.
        fn request_until_ok(method: &str, path: &str) {
            for attempt in 0..80u32 {
                let res = handle_request(method, path, None, None, None)
                    .unwrap_or_else(|e| panic!("{path} handle_request err: {e}"));
                let response: serde_json::Value = serde_json::from_str(&res)
                    .unwrap_or_else(|e| panic!("{path} invalid JSON response: {e}; {res}"));
                let status = response.get("status").and_then(serde_json::Value::as_u64);
                if status == Some(200) {
                    return;
                }
                let transient = status == Some(504) || response.to_string().contains("queue_full");
                if !transient {
                    panic!(
                        "{path} unexpected response: {}",
                        res.chars().take(500).collect::<String>()
                    );
                }
                std::thread::sleep(std::time::Duration::from_millis(
                    2 + u64::from(attempt.min(20)),
                ));
            }
            panic!("{path}: exhausted retries under scheduler backpressure");
        }

        let mut handles = vec![];

        // Spawn 20 threads, each making 50 successful request pairs (retry ok).
        for _ in 0..20 {
            let handle = thread::spawn(|| {
                for _ in 0..50 {
                    // /settings/device executes DeviceRepository reads (and the
                    // first caller may create the device), so this pair exercises
                    // both the scheduler and the Storage Actor through FFI.
                    request_until_ok("GET", "/health");
                    request_until_ok("GET", "/settings/device");
                }
            });
            handles.push(handle);
        }

        let mut thread_failures = 0usize;
        for h in handles {
            if h.join().is_err() {
                thread_failures += 1;
            }
        }
        eprintln!(
            "[test_m3_concurrency] {} of 20 threads panicked",
            thread_failures
        );
        // Zero tolerated panics: every thread must complete all ops (with retry).
        assert_eq!(
            thread_failures, 0,
            "concurrent C API stress: {} of 20 threads panicked",
            thread_failures
        );

        shutdown_c_api();
        std::fs::remove_dir_all(&app_data_dir).expect("failed to clean FFI test database");
    }
}
