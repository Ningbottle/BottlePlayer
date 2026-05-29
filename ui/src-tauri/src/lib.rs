mod backend_api;

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
            use tauri::Manager;

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
            for path in &possible_paths {
                if let Some(path_str) = path.to_str() {
                    if backend_api::init_with_paths(path_str, Some(&app_data_dir)).is_ok() {
                        println!(
                            "[EchoCAPI] Loaded native library from {} (data: {})",
                            path.display(),
                            app_data_dir
                        );
                        if let Err(e) = backend_api::set_log_callback() {
                            eprintln!("[EchoCAPI WARN] Failed to set log callback: {}", e);
                        }
                        loaded = true;
                        break;
                    }
                }
            }
            if !loaded {
                eprintln!(
                    "[EchoCAPI ERR] Could not load {} from any known path",
                    dll_name
                );
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
