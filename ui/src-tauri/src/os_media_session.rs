//! OS media session (T1a) — deep module for system Now Playing control.
//!
//! Platform surfaces (SMTC, etc.) sit behind [`MediaSessionPort`]. CI and
//! headless runs use [`InMemoryMediaPort`]; production installs the default port
//! (currently the same in-memory port until a WinRT adapter is wired — commands
//! and state machine are still the real shipped path).

use serde::{Deserialize, Serialize};
use std::sync::{Mutex, OnceLock};
use tauri::{AppHandle, Emitter};

/// Button events emitted to the frontend (event name: `os-media-button`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub enum MediaButton {
    Play,
    Pause,
    PlayPause,
    Next,
    Prev,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "PascalCase")]
pub enum PlaybackStatus {
    Playing,
    Paused,
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

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct EnabledControls {
    pub play_pause: bool,
    pub next: bool,
    pub prev: bool,
}

impl Default for EnabledControls {
    fn default() -> Self {
        Self {
            play_pause: false,
            next: false,
            prev: false,
        }
    }
}

/// Platform adapter. One real implementation can own WinRT SMTC later.
pub trait MediaSessionPort: Send {
    fn bind(&mut self) -> Result<(), String>;
    fn unbind(&mut self);
    fn set_now_playing(&mut self, np: &NowPlaying) -> Result<(), String>;
    fn set_playback_status(&mut self, status: PlaybackStatus) -> Result<(), String>;
    fn set_enabled_controls(&mut self, controls: EnabledControls) -> Result<(), String>;
}

/// Testable / degrade path: stores last values; no OS calls.
#[derive(Debug, Default)]
pub struct InMemoryMediaPort {
    pub bound: bool,
    pub now_playing: Option<NowPlaying>,
    pub status: PlaybackStatus,
    pub controls: EnabledControls,
}

impl Default for PlaybackStatus {
    fn default() -> Self {
        PlaybackStatus::Stopped
    }
}

impl MediaSessionPort for InMemoryMediaPort {
    fn bind(&mut self) -> Result<(), String> {
        self.bound = true;
        Ok(())
    }

    fn unbind(&mut self) {
        self.bound = false;
        self.now_playing = None;
        self.status = PlaybackStatus::Stopped;
        self.controls = EnabledControls::default();
    }

    fn set_now_playing(&mut self, np: &NowPlaying) -> Result<(), String> {
        if !self.bound {
            return Err("session_not_bound".into());
        }
        self.now_playing = Some(np.clone());
        Ok(())
    }

    fn set_playback_status(&mut self, status: PlaybackStatus) -> Result<(), String> {
        if !self.bound {
            return Err("session_not_bound".into());
        }
        self.status = status;
        Ok(())
    }

    fn set_enabled_controls(&mut self, controls: EnabledControls) -> Result<(), String> {
        if !self.bound {
            return Err("session_not_bound".into());
        }
        self.controls = controls;
        Ok(())
    }
}

struct SessionInner {
    port: Box<dyn MediaSessionPort>,
    app: Option<AppHandle>,
    bound: bool,
}

impl SessionInner {
    fn with_default_port() -> Self {
        Self {
            port: Box::new(InMemoryMediaPort::default()),
            app: None,
            bound: false,
        }
    }
}

static SESSION: OnceLock<Mutex<SessionInner>> = OnceLock::new();

fn session() -> &'static Mutex<SessionInner> {
    SESSION.get_or_init(|| Mutex::new(SessionInner::with_default_port()))
}

/// Inject a port (tests). Replaces the global session.
#[cfg(test)]
pub fn install_port_for_test(port: Box<dyn MediaSessionPort>) {
    let mut g = session().lock().expect("session lock");
    *g = SessionInner {
        port,
        app: None,
        bound: false,
    };
}

pub fn set_app_handle(app: AppHandle) {
    if let Ok(mut g) = session().lock() {
        g.app = Some(app);
    }
}

/// Simulate a system button (SMTC / media key). Emits `os-media-button` when bound.
pub fn inject_button(button: MediaButton) -> Result<(), String> {
    let g = session().lock().map_err(|e| e.to_string())?;
    if !g.bound {
        return Err("session_not_bound".into());
    }
    if let Some(app) = g.app.as_ref() {
        app.emit("os-media-button", button)
            .map_err(|e| format!("emit: {e}"))?;
    }
    Ok(())
}

#[tauri::command]
pub fn os_media_bind() -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    g.port.bind()?;
    g.bound = true;
    Ok(())
}

#[tauri::command]
pub fn os_media_unbind() -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    g.port.unbind();
    g.bound = false;
    Ok(())
}

#[tauri::command]
pub fn os_media_set_now_playing(now_playing: NowPlaying) -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    g.port.set_now_playing(&now_playing)
}

#[tauri::command]
pub fn os_media_set_playback_status(status: PlaybackStatus) -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    g.port.set_playback_status(status)
}

#[tauri::command]
pub fn os_media_set_enabled_controls(controls: EnabledControls) -> Result<(), String> {
    let mut g = session().lock().map_err(|e| e.to_string())?;
    g.port.set_enabled_controls(controls)
}

/// Test/dev helper: inject a button as if SMTC fired (requires bind + app handle for emit).
#[tauri::command]
pub fn os_media_inject_button(button: MediaButton) -> Result<(), String> {
    inject_button(button)
}

// ── Unit tests (shipped code path) ──────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bind_then_set_now_playing_stores_metadata() {
        let mut port = InMemoryMediaPort::default();
        assert!(port.bind().is_ok());
        let np = NowPlaying {
            title: "Song A".into(),
            artist: "Artist X".into(),
            album: Some("Album".into()),
            artwork_url: None,
        };
        port.set_now_playing(&np).unwrap();
        assert_eq!(port.now_playing.as_ref().unwrap().title, "Song A");
        assert_eq!(port.now_playing.as_ref().unwrap().artist, "Artist X");
        port.set_playback_status(PlaybackStatus::Playing).unwrap();
        assert_eq!(port.status, PlaybackStatus::Playing);
        port.set_enabled_controls(EnabledControls {
            play_pause: true,
            next: true,
            prev: false,
        })
        .unwrap();
        assert!(port.controls.next);
        assert!(!port.controls.prev);
        port.unbind();
        assert!(!port.bound);
        assert!(port.set_now_playing(&np).is_err());
    }

    #[test]
    fn commands_require_bind_before_metadata() {
        // Fresh global may be polluted by parallel tests; reinstall port.
        install_port_for_test(Box::new(InMemoryMediaPort::default()));
        let err = os_media_set_now_playing(NowPlaying {
            title: "t".into(),
            artist: "a".into(),
            ..Default::default()
        });
        assert!(err.is_err());
        os_media_bind().unwrap();
        os_media_set_now_playing(NowPlaying {
            title: "t".into(),
            artist: "a".into(),
            ..Default::default()
        })
        .unwrap();
        os_media_set_playback_status(PlaybackStatus::Paused).unwrap();
        os_media_set_enabled_controls(EnabledControls {
            play_pause: true,
            next: true,
            prev: true,
        })
        .unwrap();
        os_media_unbind().unwrap();
    }

    #[test]
    fn inject_button_fails_when_unbound() {
        install_port_for_test(Box::new(InMemoryMediaPort::default()));
        assert!(inject_button(MediaButton::Next).is_err());
        os_media_bind().unwrap();
        // No AppHandle → inject still ok (bound) but emit is skipped.
        assert!(inject_button(MediaButton::Next).is_ok());
        os_media_unbind().unwrap();
    }
}
