//! OS media session — T1a core + T1b/T1c integrations (tray + media keys).
//!
//! Desktop shell (tray / shortcuts / live emit) is behind the `desktop-shell`
//! Cargo feature so `cargo test --lib --no-default-features` stays free of
//! tray-icon link issues (STATUS_ENTRYPOINT_NOT_FOUND).

use serde::{Deserialize, Serialize};
use std::sync::{Mutex, OnceLock};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub enum MediaButton {
    Play,
    Pause,
    PlayPause,
    Next,
    Prev,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub enum PlaybackStatus {
    Playing,
    Paused,
    #[default]
    Stopped,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct NowPlaying {
    pub title: String,
    pub artist: String,
    #[serde(default)]
    pub album: Option<String>,
    #[serde(default)]
    pub artwork_url: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
pub struct EnabledControls {
    pub play_pause: bool,
    pub next: bool,
    pub prev: bool,
}

#[derive(Default)]
struct SessionState {
    bound: bool,
    now_playing: Option<NowPlaying>,
    status: PlaybackStatus,
    controls: EnabledControls,
    pending_buttons: Vec<MediaButton>,
}

static SESSION: OnceLock<Mutex<SessionState>> = OnceLock::new();

fn session() -> &'static Mutex<SessionState> {
    SESSION.get_or_init(|| Mutex::new(SessionState::default()))
}

#[cfg(feature = "desktop-shell")]
static APP: OnceLock<tauri::AppHandle> = OnceLock::new();

/// Store AppHandle from setup (desktop-shell only; no-op body without feature).
pub fn set_app_handle(app: tauri::AppHandle) {
    #[cfg(feature = "desktop-shell")]
    {
        let _ = APP.set(app);
    }
    #[cfg(not(feature = "desktop-shell"))]
    {
        let _ = app;
    }
}

/// Push a button to the frontend.
pub fn inject_button(button: MediaButton) -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    if !g.bound {
        return Err("session_not_bound".into());
    }

    #[cfg(feature = "desktop-shell")]
    if let Some(app) = APP.get() {
        use tauri::Emitter;
        drop(g);
        if let Err(e) = app.emit("os-media-button", button) {
            eprintln!("[OsMedia] emit failed: {e}");
        }
        return Ok(());
    }

    g.pending_buttons.push(button);
    Ok(())
}

#[cfg(test)]
fn take_pending_buttons() -> Vec<MediaButton> {
    let mut g = session().lock().expect("session lock");
    std::mem::take(&mut g.pending_buttons)
}

#[tauri::command]
pub fn os_media_bind() -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    g.bound = true;
    Ok(())
}

#[tauri::command]
pub fn os_media_unbind() -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    *g = SessionState::default();
    Ok(())
}

#[tauri::command]
pub fn os_media_set_now_playing(now_playing: NowPlaying) -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    if !g.bound {
        return Err("session_not_bound".into());
    }
    g.now_playing = Some(now_playing);
    Ok(())
}

#[tauri::command]
pub fn os_media_set_playback_status(status: PlaybackStatus) -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    if !g.bound {
        return Err("session_not_bound".into());
    }
    g.status = status;
    Ok(())
}

#[tauri::command]
pub fn os_media_set_enabled_controls(controls: EnabledControls) -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    if !g.bound {
        return Err("session_not_bound".into());
    }
    g.controls = controls;
    Ok(())
}

#[tauri::command]
pub fn os_media_inject_button(button: MediaButton) -> Result<(), String> {
    inject_button(button)
}

#[tauri::command]
pub fn os_media_debug_snapshot() -> Result<serde_json::Value, String> {
    let g = session().lock().map_err(|e| e.to_string())?;
    Ok(serde_json::json!({
        "bound": g.bound,
        "status": g.status,
        "has_track": g.now_playing.is_some(),
        "controls": g.controls,
        "pending_len": g.pending_buttons.len(),
        "desktop_shell": cfg!(feature = "desktop-shell"),
    }))
}

// ── Desktop shell: tray + media keys ────────────────────────────────────────

#[cfg(feature = "desktop-shell")]
pub fn install_os_integrations(app: &tauri::AppHandle) -> Result<(), String> {
    install_tray(app)?;
    install_media_key_shortcuts(app)?;
    Ok(())
}

#[cfg(feature = "desktop-shell")]
fn install_tray(app: &tauri::AppHandle) -> Result<(), String> {
    use tauri::{
        menu::{Menu, MenuItem},
        tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
        Manager,
    };

    let play = MenuItem::with_id(app, "play_pause", "播放/暂停", true, None::<&str>)
        .map_err(|e| e.to_string())?;
    let next =
        MenuItem::with_id(app, "next", "下一首", true, None::<&str>).map_err(|e| e.to_string())?;
    let prev =
        MenuItem::with_id(app, "prev", "上一首", true, None::<&str>).map_err(|e| e.to_string())?;
    let show = MenuItem::with_id(app, "show", "显示窗口", true, None::<&str>)
        .map_err(|e| e.to_string())?;
    let quit =
        MenuItem::with_id(app, "quit", "退出", true, None::<&str>).map_err(|e| e.to_string())?;
    let menu = Menu::with_items(app, &[&play, &next, &prev, &show, &quit]).map_err(|e| e.to_string())?;

    let app_handle = app.clone();
    let icon = app
        .default_window_icon()
        .cloned()
        .ok_or_else(|| "no_window_icon".to_string())?;

    let _tray = TrayIconBuilder::new()
        .icon(icon)
        .menu(&menu)
        .tooltip("BottleMusic")
        .on_menu_event(move |_app, event| {
            match event.id.as_ref() {
                "play_pause" => {
                    let _ = inject_button(MediaButton::PlayPause);
                }
                "next" => {
                    let _ = inject_button(MediaButton::Next);
                }
                "prev" => {
                    let _ = inject_button(MediaButton::Prev);
                }
                "show" => {
                    if let Some(w) = app_handle.get_webview_window("main") {
                        let _ = w.show();
                        let _ = w.set_focus();
                    }
                }
                "quit" => {
                    crate::backend_api::shutdown_c_api();
                    app_handle.exit(0);
                }
                _ => {}
            }
        })
        .on_tray_icon_event(|tray, event| {
            if let TrayIconEvent::Click {
                button: MouseButton::Left,
                button_state: MouseButtonState::Up,
                ..
            } = event
            {
                let app = tray.app_handle();
                if let Some(w) = app.get_webview_window("main") {
                    let _ = w.show();
                    let _ = w.set_focus();
                }
            }
        })
        .build(app)
        .map_err(|e| e.to_string())?;

    println!("[OsMedia] Tray installed");
    Ok(())
}

#[cfg(feature = "desktop-shell")]
fn install_media_key_shortcuts(app: &tauri::AppHandle) -> Result<(), String> {
    use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Shortcut, ShortcutState};

    let play = Shortcut::new(None, Code::MediaPlayPause);
    let next = Shortcut::new(None, Code::MediaTrackNext);
    let prev = Shortcut::new(None, Code::MediaTrackPrevious);

    app.global_shortcut()
        .on_shortcut(play, |_app, _s, event| {
            if event.state == ShortcutState::Pressed {
                let _ = inject_button(MediaButton::PlayPause);
            }
        })
        .map_err(|e| e.to_string())?;
    app.global_shortcut()
        .on_shortcut(next, |_app, _s, event| {
            if event.state == ShortcutState::Pressed {
                let _ = inject_button(MediaButton::Next);
            }
        })
        .map_err(|e| e.to_string())?;
    app.global_shortcut()
        .on_shortcut(prev, |_app, _s, event| {
            if event.state == ShortcutState::Pressed {
                let _ = inject_button(MediaButton::Prev);
            }
        })
        .map_err(|e| e.to_string())?;

    println!("[OsMedia] Media-key shortcuts registered");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bind_then_metadata_round_trip() {
        let _ = os_media_unbind();
        assert!(os_media_set_now_playing(NowPlaying {
            title: "t".into(),
            artist: "a".into(),
            ..Default::default()
        })
        .is_err());
        os_media_bind().unwrap();
        os_media_set_now_playing(NowPlaying {
            title: "Song A".into(),
            artist: "Artist X".into(),
            album: Some("Al".into()),
            artwork_url: None,
        })
        .unwrap();
        os_media_set_playback_status(PlaybackStatus::Playing).unwrap();
        os_media_set_enabled_controls(EnabledControls {
            play_pause: true,
            next: true,
            prev: false,
        })
        .unwrap();
        {
            let g = session().lock().unwrap();
            assert_eq!(g.now_playing.as_ref().unwrap().title, "Song A");
            assert_eq!(g.status, PlaybackStatus::Playing);
            assert!(g.controls.next);
        }
        inject_button(MediaButton::Next).unwrap();
        assert_eq!(take_pending_buttons(), vec![MediaButton::Next]);
        os_media_unbind().unwrap();
    }

    #[test]
    fn debug_snapshot_reports_bound() {
        let _ = os_media_unbind();
        os_media_bind().unwrap();
        let snap = os_media_debug_snapshot().unwrap();
        assert_eq!(snap["bound"], true);
        assert_eq!(snap["desktop_shell"], cfg!(feature = "desktop-shell"));
        os_media_unbind().unwrap();
    }
}
