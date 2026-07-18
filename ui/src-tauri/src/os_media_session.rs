//! OS media session (T1a) — deep module for system Now Playing control.

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

/// Called from `lib.rs` setup with the real `AppHandle`.
///
/// Intentionally a no-op for now: using `tauri::Emitter` here previously made
/// the cargo-test harness fail to start (`STATUS_ENTRYPOINT_NOT_FOUND`). Button
/// inject therefore queues into `pending_buttons` for tests. **T1-SMTC** will
/// store the handle and emit `os-media-button` when the WinRT port lands.
pub fn set_app_handle(_app: tauri::AppHandle) {
    // T1-SMTC: retain AppHandle for live SMTC / media-key emit.
}

pub fn inject_button(button: MediaButton) -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    if !g.bound {
        return Err("session_not_bound".into());
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bind_then_metadata_round_trip() {
        // Isolate: unbind resets global.
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
            assert!(!g.controls.prev);
        }
        inject_button(MediaButton::Next).unwrap();
        assert_eq!(take_pending_buttons(), vec![MediaButton::Next]);
        os_media_unbind().unwrap();
    }
}
