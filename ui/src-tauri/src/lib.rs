// BottleMusic Tauri 前端壳：
//   - 仅负责窗口、sidecar 生命周期、（可选）Rust 端 HTTP 代理
//   - 真正业务由 C++ EchoCompatServer 提供（loopback 127.0.0.1:6609）
//
// 设计：进程启动时拉起 EchoCompatServer 作为 sidecar；关窗 = 后端进程一同关闭。

use std::sync::Mutex;
use tauri::Manager;
use tauri_plugin_shell::process::{CommandChild, CommandEvent};
use tauri_plugin_shell::ShellExt;

/// 后端 sidecar 进程句柄。State 内放 Mutex<Option<...>>：进程退出 / 显式 kill 时 take()。
struct BackendProcess(Mutex<Option<CommandChild>>);

/// 简单的 ping 命令，供前端确认 Rust 壳就绪。
#[tauri::command]
fn ping() -> &'static str {
    "pong"
}

/// 返回后端基地址；前端 fetch 时统一从此读取。
#[tauri::command]
fn backend_base_url() -> &'static str {
    "http://127.0.0.1:6609"
}

/// 获取当前进程占用的物理内存 (Working Set)，单位为字节。
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

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_shell::init())
        .manage(BackendProcess(Mutex::new(None)))
        .setup(|app| {
            // 拉起 EchoCompatServer sidecar。
            let sidecar = app
                .shell()
                .sidecar("EchoCompatServer")
                .expect("EchoCompatServer sidecar not registered")
                .args(["--host", "127.0.0.1", "--port", "6609"]);

            let (mut rx, child) = sidecar.spawn().expect("failed to spawn EchoCompatServer");

            // 保存子进程句柄到 State；窗口关闭时 take() 出来再 kill。
            // 显式作用域避免 MutexGuard 与 State 借用顺序冲突。
            {
                let state: tauri::State<BackendProcess> = app.state();
                let mut slot = state.0.lock().expect("BackendProcess mutex poisoned");
                *slot = Some(child);
            }

            // 透传后端日志，便于排查。
            tauri::async_runtime::spawn(async move {
                while let Some(ev) = rx.recv().await {
                    match ev {
                        CommandEvent::Stdout(line) => {
                            println!("[EchoCompat] {}", String::from_utf8_lossy(&line));
                        }
                        CommandEvent::Stderr(line) => {
                            eprintln!("[EchoCompat ERR] {}", String::from_utf8_lossy(&line));
                        }
                        CommandEvent::Error(err) => {
                            eprintln!("[EchoCompat FATAL] {}", err);
                        }
                        CommandEvent::Terminated(payload) => {
                            eprintln!(
                                "[EchoCompat] terminated, code={:?} signal={:?}",
                                payload.code, payload.signal
                            );
                            break;
                        }
                        _ => {}
                    }
                }
            });

            Ok(())
        })
        .on_window_event(|window, event| {
            // 主窗口关闭时主动 kill sidecar。
            if let tauri::WindowEvent::CloseRequested { .. } = event {
                let taken: Option<CommandChild> = {
                    let state: tauri::State<BackendProcess> = window.state();
                    let mut slot = state.0.lock().expect("BackendProcess mutex poisoned");
                    slot.take()
                };
                if let Some(child) = taken {
                    let _ = child.kill();
                }
            }
        })
        .invoke_handler(tauri::generate_handler![ping, backend_base_url, get_memory_usage])
        .run(tauri::generate_context!())
        .expect("error while running BottleMusic Tauri app");
}
