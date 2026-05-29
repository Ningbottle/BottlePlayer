mod backend_api;

use tauri::Manager;

#[tauri::command]
fn ping() -> &'static str {
    "pong"
}

#[tauri::command]
fn backend_base_url() -> &'static str {
    "native-ipc" // Returning a dummy value to avoid breaking frontend immediately
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

#[tauri::command]
async fn native_request(
    method: String,
    path: String,
    query_json: Option<String>,
    headers_json: Option<String>,
    body: Option<String>,
) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || {
        backend_api::handle_request(
            &method,
            &path,
            query_json.as_deref(),
            headers_json.as_deref(),
            body.as_deref(),
        )
    })
    .await
    .unwrap_or_else(|e| Err(format!("Task panic: {:?}", e)))
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_shell::init())
        .setup(|app| {
            // Locate the native EchoCAPI.dll
            let dll_name = if cfg!(target_os = "windows") { "EchoCAPI.dll" } else { "libEchoCAPI.so" };
            
            // Try loading from some paths
            let possible_paths = [
                format!("{}", dll_name),
                format!("../../native/out/bottlemusic-check/{}", dll_name),
                format!("native/out/bottlemusic-check/{}", dll_name),
                format!("../../../native/out/bottlemusic-check/{}", dll_name),
                format!("d:/KuGouMusic/native/out/bottlemusic-check/{}", dll_name),
            ];

            let mut loaded = false;
            for path in &possible_paths {
                if backend_api::load_c_api(path).is_ok() {
                    println!("[EchoCAPI] Loaded native library from {}", path);
                    loaded = true;
                    break;
                }
            }
            if !loaded {
                eprintln!("[EchoCAPI ERR] Could not load {} from any known path", dll_name);
            }

            Ok(())
        })
        .on_window_event(|_window, event| {
            if let tauri::WindowEvent::CloseRequested { .. } = event {
                backend_api::shutdown_c_api();
            }
        })
        .invoke_handler(tauri::generate_handler![
            ping,
            backend_base_url,
            get_memory_usage,
            native_request
        ])
        .run(tauri::generate_context!())
        .expect("error while running BottleMusic Tauri app");
}
