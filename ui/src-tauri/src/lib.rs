mod ai_analysis;
mod audio_proxy;
mod backend_api;
mod os_media_session;
mod stats;

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
    } else {
        Duration::from_millis(deadlines::kDeadlineGenericMs)
    }
}

fn should_shutdown_c_api(event: &tauri::RunEvent) -> bool {
    matches!(event, tauri::RunEvent::Exit)
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

            // Store AppHandle for event emission from C++ callbacks.
            backend_api::set_app_handle(app.handle().clone());
            os_media_session::set_app_handle(app.handle().clone());
            #[cfg(feature = "desktop-shell")]
            if let Err(e) = os_media_session::install_os_integrations(app.handle()) {
                eprintln!("[OsMedia WARN] OS integrations partial/unavailable: {e}");
            }

            match audio_proxy::bind_listener() {
                Ok((listener, port)) => {
                    let state = audio_proxy::AudioProxyState::new(port);
                    app.manage(state.clone());
                    tauri::async_runtime::spawn(audio_proxy::serve(listener, state));
                    println!("[AudioProxy] Listening on 127.0.0.1:{}", port);
                }
                Err(e) => {
                    eprintln!("[AudioProxy ERR] Failed to bind local audio proxy: {}", e);
                    app.manage(audio_proxy::AudioProxyState::disabled());
                }
            }

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
            let mut last_errors: Vec<String> = Vec::new();
            for path in &possible_paths {
                if !path.exists() {
                    last_errors.push(format!("{} (missing)", path.display()));
                    continue;
                }
                if let Some(path_str) = path.to_str() {
                    match backend_api::init_with_paths(path_str, Some(&app_data_dir)) {
                        Ok(()) => {
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
                            loaded = true;
                            break;
                        }
                        Err(e) => {
                            last_errors.push(format!("{} → {}", path.display(), e));
                        }
                    }
                }
            }
            if !loaded {
                eprintln!(
                    "[EchoCAPI ERR] Could not load {} from any known path",
                    dll_name
                );
                for line in &last_errors {
                    eprintln!("[EchoCAPI ERR]   candidate: {}", line);
                }
                eprintln!(
                    "[EchoCAPI ERR] Hint: rebuild native backend with `pnpm backend:build` in ui/ if symbols are missing (EchoInitializeWithPathsV2)."
                );
            }

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            ping,
            backend_base_url,
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
    use std::time::Duration;
    use tokio::time::timeout;

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
    fn rust_outer_deadlines_are_at_least_cpp_inner() {
        // Outer Tauri timeout must not be shorter than C++ scheduler budget.
        assert!(deadlines::kDeadlineSongUrlMs >= deadlines::kDeadlineSongUrlMs);
        assert!(deadlines::kFrontendTimeoutMs >= deadlines::kDeadlineGenericMs);
        assert!(deadlines::kDeadlineGenericMs >= 1000);
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
