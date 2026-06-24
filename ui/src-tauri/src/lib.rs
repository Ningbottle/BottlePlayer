mod backend_api;
mod playback;

use std::time::Duration;

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

/// Map a request path to a per-kind deadline (seconds).
/// Inner WinHTTP total deadline (9s) is set slightly under the shortest middle
/// deadline so the inner layer fails first and the outer layers return a real
/// 504 instead of timing out and wasting their budget.
fn deadline_for_path(path: &str) -> Duration {
    if path.starts_with("/song/url") {
        Duration::from_secs(10)
    } else if path.starts_with("/images/") {
        Duration::from_secs(8)
    } else if path.starts_with("/login/qr/") {
        Duration::from_secs(6)
    } else {
        Duration::from_secs(12)
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
    let deadline = deadline_for_path(&path);
    match tokio::time::timeout(
        deadline,
        tauri::async_runtime::spawn_blocking(move || {
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
        Ok(join_result) => join_result.unwrap_or_else(|e| Err(format!("Task panic: {:?}", e))),
        Err(_) => Err("request_deadline".to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;
    use tokio::time::timeout;

    #[tokio::test]
    async fn native_request_times_out_when_handler_sleeps() {
        // Documents the timeout semantics without depending on the DLL:
        // a spawn_blocking task that sleeps past the deadline is abandoned.
        let never = timeout(Duration::from_millis(100), async {
            tokio::task::spawn_blocking(|| {
                std::thread::sleep(Duration::from_secs(60));
                "ok"
            })
            .await
        });
        let result = never.await;
        assert!(result.is_err());
    }

    #[test]
    fn deadline_for_song_url_is_10s() {
        assert_eq!(deadline_for_path("/song/url"), Duration::from_secs(10));
    }

    #[test]
    fn deadline_for_images_is_8s() {
        assert_eq!(deadline_for_path("/images/audio"), Duration::from_secs(8));
    }

    #[test]
    fn deadline_for_login_qr_is_6s() {
        assert_eq!(deadline_for_path("/login/qr/check"), Duration::from_secs(6));
    }

    #[test]
    fn deadline_for_generic_is_12s() {
        assert_eq!(deadline_for_path("/unknown/route"), Duration::from_secs(12));
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .plugin(tauri_plugin_process::init())
        .setup(|app| {
            use tauri::Manager;

            // Store AppHandle for event emission from C++ callbacks.
            backend_api::set_app_handle(app.handle().clone());

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
                        // 日志目录必须在注册 log callback 之前设定：callback 一旦
                        // 触发就会惰性初始化 LOG_FILE，而路径由 LOG_DIR 决定。
                        backend_api::set_log_dir(&app_data_dir);
                        if let Err(e) = backend_api::set_log_callback() {
                            eprintln!("[EchoCAPI WARN] Failed to set log callback: {}", e);
                        }
                        if let Err(e) = backend_api::set_event_callback() {
                            eprintln!("[EchoCAPI WARN] Failed to set event callback: {}", e);
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
            native_request,
            playback::playback_initialize,
            playback::playback_play_url,
            playback::playback_pause,
            playback::playback_resume,
            playback::playback_stop,
            playback::playback_seek,
            playback::playback_set_volume,
            playback::playback_set_rate,
            playback::playback_get_state,
            playback::playback_shutdown,
            playback::playback_set_eq_enabled,
            playback::playback_set_eq_bands,
            playback::playback_get_eq_bands,
        ])
        .run(tauri::generate_context!())
        .expect("error while running BottleMusic Tauri app");
}
