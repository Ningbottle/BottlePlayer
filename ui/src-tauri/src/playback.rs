use std::ffi::CStr;
use std::ffi::CString;
use tauri::State;
use crate::backend_api::CApiHandle;

#[tauri::command]
pub fn playback_initialize(backend: i32, handle: State<CApiHandle>) -> Result<bool, String> {
    let result = unsafe { (handle.playback_initialize)(backend) };
    Ok(result)
}

#[tauri::command]
pub fn playback_play_url(url: String, handle: State<CApiHandle>) -> Result<bool, String> {
    let cstr = CString::new(url).map_err(|e| e.to_string())?;
    let result = unsafe { (handle.playback_play_url)(cstr.as_ptr()) };
    Ok(result)
}

#[tauri::command]
pub fn playback_pause(handle: State<CApiHandle>) -> Result<(), String> {
    unsafe { (handle.playback_pause)() };
    Ok(())
}

#[tauri::command]
pub fn playback_resume(handle: State<CApiHandle>) -> Result<(), String> {
    unsafe { (handle.playback_resume)() };
    Ok(())
}

#[tauri::command]
pub fn playback_stop(handle: State<CApiHandle>) -> Result<(), String> {
    unsafe { (handle.playback_stop)() };
    Ok(())
}

#[tauri::command]
pub fn playback_seek(seconds: f64, handle: State<CApiHandle>) -> Result<(), String> {
    unsafe { (handle.playback_seek)(seconds) };
    Ok(())
}

#[tauri::command]
pub fn playback_set_volume(volume: f64, handle: State<CApiHandle>) -> Result<(), String> {
    unsafe { (handle.playback_set_volume)(volume) };
    Ok(())
}

#[tauri::command]
pub fn playback_set_rate(rate: f64, handle: State<CApiHandle>) -> Result<(), String> {
    unsafe { (handle.playback_set_rate)(rate) };
    Ok(())
}

#[tauri::command]
pub fn playback_get_state(handle: State<CApiHandle>) -> Result<String, String> {
    let ptr = unsafe { (handle.playback_get_state)() };
    if ptr.is_null() {
        return Err("null state".into());
    }
    let result = unsafe { CStr::from_ptr(ptr).to_string_lossy().into_owned() };
    unsafe { (handle.free_str)(ptr) };
    Ok(result)
}

#[tauri::command]
pub fn playback_shutdown(handle: State<CApiHandle>) -> Result<(), String> {
    unsafe { (handle.playback_shutdown)() };
    Ok(())
}

#[tauri::command]
pub fn playback_set_eq_enabled(enabled: bool, handle: State<CApiHandle>) -> Result<(), String> {
    unsafe { (handle.playback_set_eq_enabled)(if enabled { 1 } else { 0 }) };
    Ok(())
}

#[tauri::command]
pub fn playback_set_eq_bands(gains: Vec<f64>, handle: State<CApiHandle>) -> Result<(), String> {
    if gains.len() != 5 {
        return Err("expected 5 bands".into());
    }
    unsafe { (handle.playback_set_eq_bands)(gains.as_ptr()) };
    Ok(())
}

#[tauri::command]
pub fn playback_get_eq_bands(handle: State<CApiHandle>) -> Result<Vec<f64>, String> {
    let mut bands = [0.0f64; 5];
    unsafe { (handle.playback_get_eq_bands)(bands.as_mut_ptr()) };
    Ok(bands.to_vec())
}
