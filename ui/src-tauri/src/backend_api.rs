use libloading::{Library, Symbol};
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::sync::{Mutex, OnceLock};

static C_API_LIB: OnceLock<Mutex<Option<Library>>> = OnceLock::new();

fn get_c_api_lib() -> &'static Mutex<Option<Library>> {
    C_API_LIB.get_or_init(|| Mutex::new(None))
}

// Ensure the signatures match the C_API.h
type EchoInitializeFunc = unsafe extern "C" fn();
type EchoShutdownFunc = unsafe extern "C" fn();
type EchoHandleRequestFunc = unsafe extern "C" fn(
    method: *const c_char,
    path: *const c_char,
    query_json: *const c_char,
    headers_json: *const c_char,
    body: *const c_char,
    out_response: *mut *mut c_char,
);
type EchoFreeStringFunc = unsafe extern "C" fn(str: *mut c_char);

pub fn load_c_api(dll_path: &str) -> Result<(), String> {
    let mut lib_guard = get_c_api_lib().lock().unwrap();
    if lib_guard.is_some() {
        return Ok(());
    }

    unsafe {
        let lib = Library::new(dll_path).map_err(|e| e.to_string())?;
        
        let init_func: Symbol<EchoInitializeFunc> = lib.get(b"EchoInitialize").map_err(|e| e.to_string())?;
        init_func();

        *lib_guard = Some(lib);
    }
    Ok(())
}

pub fn shutdown_c_api() {
    let lib_guard = get_c_api_lib().lock().unwrap();
    if let Some(lib) = lib_guard.as_ref() {
        unsafe {
            if let Ok(shutdown_func) = lib.get::<Symbol<EchoShutdownFunc>>(b"EchoShutdown") {
                shutdown_func();
            }
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
    let lib_guard = get_c_api_lib().lock().unwrap();
    let lib = lib_guard.as_ref().ok_or("C API not loaded")?;

    let c_method = CString::new(method).unwrap();
    let c_path = CString::new(path).unwrap();
    let c_query = query_json.map(|s| CString::new(s).unwrap());
    let c_headers = headers_json.map(|s| CString::new(s).unwrap());
    let c_body = body.map(|s| CString::new(s).unwrap());

    let ptr_method = c_method.as_ptr();
    let ptr_path = c_path.as_ptr();
    let ptr_query = c_query.as_ref().map_or(std::ptr::null(), |s| s.as_ptr());
    let ptr_headers = c_headers.as_ref().map_or(std::ptr::null(), |s| s.as_ptr());
    let ptr_body = c_body.as_ref().map_or(std::ptr::null(), |s| s.as_ptr());

    unsafe {
        let handle_req: Symbol<EchoHandleRequestFunc> = lib.get(b"EchoHandleRequest").map_err(|e| e.to_string())?;
        let free_str: Symbol<EchoFreeStringFunc> = lib.get(b"EchoFreeString").map_err(|e| e.to_string())?;

        let mut out_response: *mut c_char = std::ptr::null_mut();
        
        handle_req(
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
        free_str(out_response);

        Ok(resp_str)
    }
}
