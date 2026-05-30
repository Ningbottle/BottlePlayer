use libloading::{Library, Symbol};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::sync::{OnceLock, RwLock};

// Resolved entry points from the DLL. The Library handle is kept alive so the
// function pointers stay valid. A read guard is held for the duration of each
// C++ call: concurrent reads allow concurrent calls (once the C++ side is made
// thread-safe), while shutdown takes a write guard and therefore waits for all
// in-flight calls to drain before unloading the library.
pub struct CApiHandle {
    #[allow(dead_code)]
    _lib: Library,
    handle_req: unsafe extern "C" fn(
        method: *const c_char,
        path: *const c_char,
        query_json: *const c_char,
        headers_json: *const c_char,
        body: *const c_char,
        out_response: *mut *mut c_char,
    ),
    free_str: unsafe extern "C" fn(str: *mut c_char),
}

static C_API_HANDLE: OnceLock<RwLock<Option<CApiHandle>>> = OnceLock::new();

fn get_handle() -> &'static RwLock<Option<CApiHandle>> {
    C_API_HANDLE.get_or_init(|| RwLock::new(None))
}

/// Load the DLL and initialize C++ backend with an explicit app data directory.
/// `app_data_dir` controls where SQLite (`bottlemusic.db`) is created.
pub fn init_with_paths(dll_path: &str, app_data_dir: Option<&str>) -> Result<(), String> {
    let mut guard = get_handle().write().unwrap();
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

        *guard = Some(CApiHandle {
            _lib: lib,
            handle_req: handle_req_ptr,
            free_str: free_str_ptr,
        });
    }
    Ok(())
}

pub fn shutdown_c_api() {
    // Write guard: blocks until every in-flight handle_request read guard is
    // released, so the library is never unloaded while a C++ call is running.
    let mut guard = get_handle().write().unwrap();
    if let Some(handle) = guard.take() {
        // Call C++ shutdown before dropping the library
        unsafe {
            // We re-resolve the symbol because shutdown is rarely called
            // and keeping it in the vtable is not worth the extra field.
            if let Ok(shutdown_func) =
                handle._lib.get::<Symbol<unsafe extern "C" fn()>>(b"EchoShutdown")
            {
                shutdown_func();
            }
        }
        // Library is dropped here automatically
        drop(handle);
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
    let guard = get_handle().read().unwrap();
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

// 日志文件：优先 D:\BottleMusic\logs（固定路径、好找）；若该盘不可写
// （如别人机器没 D 盘）则回退到可执行文件同级的 logs/。按天分文件避免无限增长。
fn log_file() -> &'static std::sync::Mutex<Option<std::fs::File>> {
    static LOG_FILE: OnceLock<std::sync::Mutex<Option<std::fs::File>>> = OnceLock::new();
    LOG_FILE.get_or_init(|| {
        let dir = {
            let preferred = std::path::PathBuf::from("D:\\BottleMusic\\logs");
            if std::fs::create_dir_all(&preferred).is_ok() {
                preferred
            } else {
                let fallback = std::env::current_exe()
                    .ok()
                    .and_then(|p| p.parent().map(|p| p.join("logs")))
                    .unwrap_or_else(|| std::path::PathBuf::from("logs"));
                let _ = std::fs::create_dir_all(&fallback);
                fallback
            }
        };
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
    let lib_guard = get_handle().read().unwrap();
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
        let dll_path = if cfg!(target_os = "windows") {
            "../../native/out/bottlemusic-check/EchoCAPI.dll"
        } else {
            "../../native/out/bottlemusic-check/libEchoCAPI.so"
        };
        
        let app_data_dir = std::env::temp_dir().join("bottlemusic_test");
        std::fs::create_dir_all(&app_data_dir).unwrap();
        
        // This will block/panic if the library cannot be found. Run via `cargo test`.
        init_with_paths(dll_path, Some(app_data_dir.to_str().unwrap())).expect("Failed to init C API");
        set_log_callback().ok();

        let mut handles = vec![];
        
        // Spawn 20 threads, each making 50 requests
        for _ in 0..20 {
            let handle = thread::spawn(|| {
                for _ in 0..50 {
                    // /health: memory only, /playlist/tags: SQLite DB access
                    let res_health = handle_request("GET", "/health", None, None, None).unwrap();
                    assert!(res_health.contains("200"));
                    
                    let res_db = handle_request("GET", "/playlist/tags", None, None, None).unwrap();
                    assert!(res_db.contains("200"));
                }
            });
            handles.push(handle);
        }
        
        for h in handles {
            h.join().unwrap();
        }
        
        shutdown_c_api();
    }
}
