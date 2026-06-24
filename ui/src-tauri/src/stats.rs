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
pub fn stats_get_recent(offset: i32, limit: i32) -> Result<String, String> {
    let guard = backend_api::api_handle()?;
    let handle = guard.as_ref().unwrap();
    let ptr = unsafe { (handle.stats_get_recent)(offset, limit) };
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
