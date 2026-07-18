use libloading::{Library, Symbol};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::sync::{OnceLock, RwLock};
use tauri::AppHandle;

// Retained so setup can hand us an AppHandle (log/event paths may use later).
#[allow(dead_code)]
static APP_HANDLE: OnceLock<AppHandle> = OnceLock::new();

/// Store the Tauri AppHandle. Safe to call multiple times — only the first takes effect.
pub fn set_app_handle(handle: AppHandle) {
    let _ = APP_HANDLE.set(handle);
}

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
    // EchoStats C API exports
    pub(crate) stats_record_play: unsafe extern "C" fn(*const c_char),
    pub(crate) stats_get_summary: unsafe extern "C" fn(*const c_char) -> *mut c_char,
    pub(crate) stats_get_top: unsafe extern "C" fn(*const c_char, *const c_char, c_int) -> *mut c_char,
    pub(crate) stats_get_timeline: unsafe extern "C" fn(*const c_char) -> *mut c_char,
    pub(crate) stats_get_recent: unsafe extern "C" fn(c_int, c_int) -> *mut c_char,
    pub(crate) stats_get_recommendations: unsafe extern "C" fn(c_int) -> *mut c_char,
}

static C_API_HANDLE: OnceLock<RwLock<Option<CApiHandle>>> = OnceLock::new();

#[cfg(test)]
pub(crate) static TEST_C_API_GUARD: std::sync::Mutex<()> = std::sync::Mutex::new(());

#[cfg(test)]
pub(crate) fn lock_test_c_api() -> std::sync::MutexGuard<'static, ()> {
    TEST_C_API_GUARD.lock().unwrap_or_else(|p| p.into_inner())
}

fn get_handle() -> &'static RwLock<Option<CApiHandle>> {
    C_API_HANDLE.get_or_init(|| RwLock::new(None))
}

/// Get a read guard on the C API handle. Multiple concurrent readers are
/// allowed; the guard must be held for the duration of any C API call.
pub fn api_handle() -> Result<std::sync::RwLockReadGuard<'static, Option<CApiHandle>>, String> {
    let guard = get_handle().read().unwrap_or_else(|p| p.into_inner());
    if guard.is_none() {
        return Err("C API not loaded".into());
    }
    Ok(guard)
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

        if let Some(dir) = app_data_dir {
            let init_func: Symbol<unsafe extern "C" fn(*const c_char)> =
                lib.get(b"EchoInitializeWithPaths").map_err(|e| e.to_string())?;
            let c_dir = CString::new(dir).map_err(|e| e.to_string())?;
            init_func(c_dir.as_ptr());
        } else {
            let init_func: Symbol<unsafe extern "C" fn()> =
                lib.get(b"EchoInitialize").map_err(|e| e.to_string())?;
            init_func();
        }

        let handle_req_ptr = {
            let sym: Symbol<unsafe extern "C" fn(
                *const c_char,
                *const c_char,
                *const c_char,
                *const c_char,
                *const c_char,
                *mut *mut c_char,
            )> = lib.get(b"EchoHandleRequest").map_err(|e| e.to_string())?;
            *sym
        };

        let free_str_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*mut c_char)> =
                lib.get(b"EchoFreeString").map_err(|e| e.to_string())?;
            *sym
        };

        // EchoStats symbols
        let stats_record_play_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*const c_char)> =
                lib.get(b"EchoStatsRecordPlay").map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_summary_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*const c_char) -> *mut c_char> =
                lib.get(b"EchoStatsGetSummary").map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_top_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*const c_char, *const c_char, c_int) -> *mut c_char> =
                lib.get(b"EchoStatsGetTop").map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_timeline_ptr = {
            let sym: Symbol<unsafe extern "C" fn(*const c_char) -> *mut c_char> =
                lib.get(b"EchoStatsGetTimeline").map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_recent_ptr = {
            let sym: Symbol<unsafe extern "C" fn(c_int, c_int) -> *mut c_char> =
                lib.get(b"EchoStatsGetRecent").map_err(|e| e.to_string())?;
            *sym
        };
        let stats_get_recommendations_ptr = {
            let sym: Symbol<unsafe extern "C" fn(c_int) -> *mut c_char> =
                lib.get(b"EchoStatsGetRecommendations").map_err(|e| e.to_string())?;
            *sym
        };

        *guard = Some(CApiHandle {
            _lib: lib,
            handle_req: handle_req_ptr,
            free_str: free_str_ptr,
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
    // in-flight spawn_blocking tasks still hold read guards. Instead we
    // just return; the C++ EchoShutdown has its own bounded lock and will
    // force-tear-down. Residual handle_request calls return Err("C API
    // not loaded") once the handle is eventually taken.
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
        // P0-B: EchoShutdown returns abandoned worker count. If > 0, detached
        // C++ workers are still executing inside the DLL — FreeLibrary via
        // drop(_lib) would unmap their code pages (use-after-unload). Leak
        // the Library mapping until process exit (OS reclaims).
        let abandoned = unsafe {
            match handle
                ._lib
                .get::<Symbol<unsafe extern "C" fn() -> c_int>>(b"EchoShutdown")
            {
                Ok(shutdown_func) => shutdown_func(),
                Err(_) => 0,
            }
        };
        if abandoned != 0 {
            eprintln!(
                "[EchoCAPI WARN] shutdown_c_api: {abandoned} abandoned worker(s); \
                 leaking DLL mapping to avoid use-after-unload"
            );
            std::mem::forget(handle);
        } else {
            drop(handle);
        }
    }
}

pub fn handle_request(
    method: &str,
    path: &str,
    query_json: Option<&str>,
    headers_json: Option<&str>,
    body: Option<&str>,
) -> Result<String, String> {
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
    let guard = get_handle().read().unwrap_or_else(|p| p.into_inner());
    let handle = guard.as_ref().ok_or("C API not loaded")?;

    unsafe {
        let mut out_response: *mut c_char = std::ptr::null_mut();

        (handle.handle_req)(
            ptr_method,
            ptr_path,
            ptr_query,
            ptr_headers,
            ptr_body,
            &mut out_response,
        );

        if out_response.is_null() {
            return Err("Empty response from C API".to_string());
        }

        let resp_str = CStr::from_ptr(out_response).to_string_lossy().into_owned();
        (handle.free_str)(out_response);

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
// 最后回退到当前工作目录 logs/。按天分文件避免无限增长。
fn log_file() -> &'static std::sync::Mutex<Option<std::fs::File>> {
    static LOG_FILE: OnceLock<std::sync::Mutex<Option<std::fs::File>>> = OnceLock::new();
    LOG_FILE.get_or_init(|| {
        // 按优先级枚举候选根目录，第一个 create_dir_all 成功的胜出。
        let mut candidates: Vec<std::path::PathBuf> = Vec::new();
        if let Some(host_dir) = LOG_DIR.get() {
            candidates.push(host_dir.join("logs"));
        }
        if let Some(exe) = std::env::current_exe().ok().and_then(|p| p.parent().map(|p| p.to_path_buf())) {
            candidates.push(exe.join("logs"));
        }
        candidates.push(std::path::PathBuf::from("logs"));

        let (dir, _) = candidates
            .iter()
            .map(|d| (d.clone(), std::fs::create_dir_all(d)))
            .find(|(_, r)| r.is_ok())
            .unwrap_or_else(|| (candidates[0].clone(), Ok(())));

        let name = format!("bottlemusic-{}.log", chrono::Local::now().format("%Y%m%d"));
        let path = dir.join(name);
        let file = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)
            .ok();
        if file.is_some() {
            println!("[EchoCAPI] 日志写入: {}", path.display());
        }
        std::sync::Mutex::new(file)
    })
}

fn write_log_line(line: &str) {
    use std::io::Write as _;
    if let Ok(mut guard) = log_file().lock() {
        if let Some(f) = guard.as_mut() {
            let _ = writeln!(f, "{}", line);
            let _ = f.flush();
        }
    }
}

extern "C" fn ffi_log_callback(level: c_int, tag: *const c_char, msg: *const c_char, _ud: *mut c_void) {
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
        let set_cb: Symbol<unsafe extern "C" fn(
            cb: unsafe extern "C" fn(c_int, *const c_char, *const c_char, *mut c_void),
            *mut c_void,
        )> = handle._lib.get(b"EchoSetLogCallback").map_err(|e| e.to_string())?;
        set_cb(ffi_log_callback, std::ptr::null_mut());
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    #[test]
    fn test_m3_concurrency() {
        let _lock = lock_test_c_api();

        // Try ECHO_CAPI_DLL env var first, then canonical paths.
        let candidates: Vec<String> = {
            let mut v: Vec<String> = std::env::var("ECHO_CAPI_DLL")
                .ok()
                .into_iter()
                .collect();
            if cfg!(target_os = "windows") {
                // From ui/src-tauri: ../../native is the repo (or worktree) native/
                // ../../../native wrongly escapes the worktree and often misses the fresh build.
                v.push(format!(
                    "{}/../../native/out/bottlemusic-check/EchoCAPI.dll",
                    env!("CARGO_MANIFEST_DIR")
                ));
                v.push("../../native/out/bottlemusic-check/EchoCAPI.dll".into());
                v.push(format!("{}/target/debug/EchoCAPI.dll", env!("CARGO_MANIFEST_DIR")));
                v.push(format!("{}/EchoCAPI.dll", env!("CARGO_MANIFEST_DIR")));
            } else {
                v.push(format!(
                    "{}/../../native/out/bottlemusic-check/libEchoCAPI.so",
                    env!("CARGO_MANIFEST_DIR")
                ));
                v.push("../../native/out/bottlemusic-check/libEchoCAPI.so".into());
                v.push(format!("{}/target/debug/libEchoCAPI.so", env!("CARGO_MANIFEST_DIR")));
            }
            v
        };
        let dll_path = candidates
            .iter()
            .find(|p| std::path::Path::new(p.as_str()).exists())
            .cloned()
            .unwrap_or_else(|| {
                panic!("Could not find EchoCAPI.dll in candidates: {:?}", candidates);
            });
        eprintln!("[test_m3_concurrency] using dll: {}", dll_path);
        
        let app_data_dir = std::env::temp_dir().join("bottlemusic_test");
        std::fs::create_dir_all(&app_data_dir).unwrap();
        
        // This will block/panic if the library cannot be found. Run via `cargo test`.
        init_with_paths(&dll_path, Some(app_data_dir.to_str().unwrap())).expect("Failed to init C API");
        set_log_callback().ok();

        // RequestScheduler maxQueue = workers*4; under 20 concurrent callers the
        // queue may return 504 queue_full. That is backpressure, not a crash —
        // each logical op must still succeed after retry. Zero thread panics.
        fn request_until_ok(method: &str, path: &str) {
            for attempt in 0..80u32 {
                let res = handle_request(method, path, None, None, None)
                    .unwrap_or_else(|e| panic!("{path} handle_request err: {e}"));
                // handle_request wraps HTTP status at top-level "status".
                let ok = res.contains("\"status\":200");
                if ok {
                    return;
                }
                let transient = res.contains("queue_full") || res.contains("\"status\":504");
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
                    // /health: memory only, /playlist/tags: SQLite DB access
                    request_until_ok("GET", "/health");
                    request_until_ok("GET", "/playlist/tags");
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
        eprintln!("[test_m3_concurrency] {} of 20 threads panicked", thread_failures);
        // Zero tolerated panics: every thread must complete all ops (with retry).
        assert_eq!(
            thread_failures, 0,
            "concurrent C API stress: {} of 20 threads panicked",
            thread_failures
        );

        shutdown_c_api();
    }
}
