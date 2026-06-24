# S4 Implementation Plan — Part 3: C API + Rust FFI (Phase 4.1b)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose the C++ PlaybackController through the existing `EchoCAPI.dll` FFI boundary. Wire the Rust Tauri runtime to load these symbols and dispatch Tauri commands to them. Events flow back via `EchoSetEventCallback` → Tauri event emission.

**Architecture:** `C_API.cpp` adds 14 new exports. `CApiHandle` in `backend_api.rs` gains 14 corresponding fn pointers. A new `playback.rs` Tauri command module registers 13 commands. The FFI event callback bridges C→Rust→Tauri event channel.

**Tech Stack:** Rust 1.96, Tauri 2, libloading, tauri-plugin-process (already in package.json from S2), Cargo test framework.

## Global Constraints

(All S4 constraints from Parts 1-2 apply: C++17, Windows 10+, no mocking of internal collaborators, etc.)

Additional S4.1b constraints:
- **FFI boundary is the test seam.** Frontend mocks `invoke`; Rust tests load the real DLL; C++ tests use the real C++ objects. No mocking at the C↔Rust boundary.
- **C string lifetime.** `EchoPlaybackGetState` returns a heap-allocated `char*` that Rust must free via the existing `free_str` symbol. (Reuse the existing pattern from `handle_request`.)
- **Process-global state guarded by mutex.** `g_playback` in `C_API.cpp` is protected by `g_playback_mutex` (S1-style shared-mutex pattern).
- **Tauri command signatures use camelCase.** Tauri's invoke serialization converts snake_case Rust args to camelCase automatically.
- **Test DLL availability.** `cargo test` requires `ECHO_CAPI_DLL` env var or DLL in `target/debug/`. The Cargo build script copies it automatically (existing pattern).

## File Map

| File | Responsibility |
|---|---|
| `native/core/C_API.cpp` | Add 14 EchoPlayback* exports + g_playback state. |
| `native/include/echo/core/C_API.h` | Add new extern "C" declarations. |
| `ui/src-tauri/src/playback.rs` | New Tauri command module (13 commands). |
| `ui/src-tauri/src/lib.rs` | Register new commands in invoke_handler. |
| `ui/src-tauri/src/backend_api.rs` | Extend CApiHandle with 14 fn pointers. |
| `ui/src-tauri/src/main.rs` | Register AppHandle for event bridge. |
| `ui/src-tauri/tests/playback_ffi_test.rs` | Rust integration test: load DLL, init, play, verify event. |

---

### Task 15: C API process-global state + playback exports

**Files:**
- Modify: `native/core/C_API.cpp` — add 14 new exports + g_playback state
- Modify: `native/include/echo/core/C_API.h` — add new declarations

**Interfaces:**
- Consumes: `PlaybackController` (Part 1, Tasks 1-3) + `Backend` enum
- Produces: 14 C-linkage exports callable from Rust

- [ ] **Step 1: Add declarations to `C_API.h`**

Add after the existing declarations in `C_API.h`:

```c
typedef enum EchoPlaybackBackend {
  ECHO_PLAYBACK_MFP = 0,
  ECHO_PLAYBACK_MFS = 1,
} EchoPlaybackBackend;

ECHO_API bool EchoPlaybackInitialize(EchoPlaybackBackend backend);
ECHO_API bool EchoPlaybackPlayUrl(const char* url);
ECHO_API void EchoPlaybackPause(void);
ECHO_API void EchoPlaybackResume(void);
ECHO_API void EchoPlaybackStop(void);
ECHO_API void EchoPlaybackSeek(double seconds);
ECHO_API void EchoPlaybackSetVolume(double volume);
ECHO_API void EchoPlaybackSetRate(double rate);
ECHO_API const char* EchoPlaybackGetState(void);
ECHO_API void EchoPlaybackShutdown(void);

ECHO_API void EchoPlaybackSetEqEnabled(bool enabled);
ECHO_API void EchoPlaybackSetEqBand(int bandIndex, double gainDb);
ECHO_API void EchoPlaybackSetEqBands(const double gainsDb[5]);
ECHO_API void EchoPlaybackGetEqBands(double outGainsDb[5]);
```

(Note: `EchoSetEventCallback` already exists; the implementation in Task 17 wires it up.)

- [ ] **Step 2: Add includes and process-global state to `C_API.cpp`**

Add at the top of `C_API.cpp`:

```cpp
#include "echo/playback/PlaybackController.h"
```

Add to the global state section (after `g_api`):

```cpp
static std::shared_ptr<echo::playback::PlaybackController> g_playback;
static std::mutex g_playback_mutex;
```

- [ ] **Step 3: Implement `EchoPlaybackInitialize`**

```cpp
ECHO_API bool EchoPlaybackInitialize(EchoPlaybackBackend backend) {
  std::lock_guard lock(g_playback_mutex);
  if (g_playback) return true;  // already initialized
  auto pc = std::make_shared<echo::playback::PlaybackController>();
  bool ok = pc->Initialize(static_cast<echo::playback::Backend>(backend));
  if (!ok && backend == ECHO_PLAYBACK_MFS) {
    // Auto-fallback: MFS failed, try MFP
    ok = pc->Initialize(echo::playback::Backend::MFP);
  }
  if (!ok) return false;
  g_playback = pc;
  return true;
}
```

- [ ] **Step 4: Implement play/pause/resume/stop/seek**

```cpp
ECHO_API bool EchoPlaybackPlayUrl(const char* url) {
  std::lock_guard lock(g_playback_mutex);
  if (!g_playback) return false;
  return g_playback->PlayUrl(url ? url : "");
}

ECHO_API void EchoPlaybackPause(void) {
  std::lock_guard lock(g_playback_mutex);
  if (g_playback) g_playback->Pause();
}

ECHO_API void EchoPlaybackResume(void) {
  std::lock_guard lock(g_playback_mutex);
  if (g_playback) g_playback->Resume();
}

ECHO_API void EchoPlaybackStop(void) {
  std::lock_guard lock(g_playback_mutex);
  if (g_playback) g_playback->Stop();
}

ECHO_API void EchoPlaybackSeek(double seconds) {
  std::lock_guard lock(g_playback_mutex);
  if (g_playback) g_playback->Seek(seconds);
}
```

- [ ] **Step 5: Implement volume/rate**

```cpp
ECHO_API void EchoPlaybackSetVolume(double volume) {
  std::lock_guard lock(g_playback_mutex);
  if (g_playback) g_playback->SetVolume(volume);
}

ECHO_API void EchoPlaybackSetRate(double rate) {
  std::lock_guard lock(g_playback_mutex);
  if (g_playback) g_playback->SetRate(rate);
}
```

- [ ] **Step 6: Implement `EchoPlaybackGetState` (returns heap-allocated JSON)**

```cpp
ECHO_API const char* EchoPlaybackGetState(void) {
  if (!g_playback) {
    // Caller must free via free_str
    char* out = new char[64];
    std::strcpy(out, R"({"state":"uninitialized","position":0,"duration":0})");
    return out;
  }
  auto state = g_playback->GetState();
  // Convert to JSON (state struct fields may differ — adapt to actual PlaybackState definition)
  std::ostringstream os;
  os << R"({"state":")" << state.name << R"(",)"
     << R"("position":)" << state.position
     << R"(,"duration":)" << state.duration
     << "}";
  std::string s = os.str();
  char* out = new char[s.size() + 1];
  std::strcpy(out, s.c_str());
  return out;
}
```

(Check the actual `echo::core::PlaybackState` struct in `native/include/echo/core/Dto.h` for field names; adjust accordingly.)

- [ ] **Step 7: Implement shutdown**

```cpp
ECHO_API void EchoPlaybackShutdown(void) {
  std::lock_guard lock(g_playback_mutex);
  g_playback.reset();  // releases the controller
}
```

- [ ] **Step 8: Build**

Run: `cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCAPI 2>&1 | Select-Object -Last 5`
Expected: clean build, all 14 new symbols exported.

- [ ] **Step 9: Verify exports with `dumpbin`**

Run: `dumpbin /exports C:\BottleMusic\native\out\bottlemusic-check\EchoCAPI.dll 2>&1 | Select-String "EchoPlayback" | Select-Object -First 15`
Expected: shows all 14 `EchoPlayback*` symbols.

- [ ] **Step 10: Commit**

```bash
git add native/core/C_API.cpp native/include/echo/core/C_API.h
git commit -m "feat(s4): add 14 EchoPlayback C API exports"
```

---

### Task 16: C API EchoSetEventCallback wiring

**Files:**
- Modify: `native/core/C_API.cpp` — make the existing `EchoSetEventCallback` actually forward to PlaybackController

**Interfaces:**
- Consumes: `EchoEventCallback` (existing C-ABI type), `PlaybackController::EventCallback`
- Produces: Event callback chain `PlaybackControllerMFS → g_playback → C API → Rust → Tauri event`

- [ ] **Step 1: Find existing `EchoSetEventCallback` impl**

The existing implementation is at `native/core/C_API.cpp:256` and is a no-op:

```cpp
void EchoSetEventCallback(EchoEventCallback cb, void* user_data) {
    // ABI placeholder — not yet wired. Reserved for playback / download events.
    (void)cb;
    (void)user_data;
}
```

Replace it with the wired implementation.

- [ ] **Step 2: Wire it to PlaybackController**

```cpp
ECHO_API void EchoSetEventCallback(EchoEventCallback cb, void* user_data) {
  std::lock_guard lock(g_playback_mutex);
  if (!g_playback) return;
  // EchoEventCallback has the same signature as PlaybackController::EventCallback
  using PcbCallback = echo::playback::PlaybackController::EventCallback;
  g_playback->SetEventCallback(reinterpret_cast<PcbCallback>(cb), user_data);
}
```

- [ ] **Step 3: Build**

Run: `cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCAPI 2>&1 | Select-Object -Last 3`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add native/core/C_API.cpp
git commit -m "feat(s4): wire EchoSetEventCallback to PlaybackController"
```

---

### Task 17: Rust CApiHandle extension + symbol loading

**Files:**
- Modify: `ui/src-tauri/src/backend_api.rs` — add 14 new fn pointers to `CApiHandle`

**Interfaces:**
- Consumes: DLL loaded via `libloading`
- Produces: `CApiHandle` with 14 new fn pointers resolvable from the loaded library

- [ ] **Step 1: Find the existing `CApiHandle` struct**

The struct is in `ui/src-tauri/src/backend_api.rs`. Find it and the `init_with_paths` function that loads symbols.

- [ ] **Step 2: Add 14 new fn pointer fields to `CApiHandle`**

```rust
pub struct CApiHandle {
    pub _lib: Library,
    pub handle_req: unsafe extern "C" fn(
        *const c_char, *const c_char, *const c_char,
        *const c_char, *const c_char, *mut *mut c_char,
    ) -> *mut c_char,
    pub free_str: unsafe extern "C" fn(*mut c_char),
    // New S4 fields
    pub playback_initialize: unsafe extern "C" fn(c_int) -> bool,
    pub playback_play_url: unsafe extern "C" fn(*const c_char) -> bool,
    pub playback_pause: unsafe extern "C" fn(),
    pub playback_resume: unsafe extern "C" fn(),
    pub playback_stop: unsafe extern "C" fn(),
    pub playback_seek: unsafe extern "C" fn(f64),
    pub playback_set_volume: unsafe extern "C" fn(f64),
    pub playback_set_rate: unsafe extern "C" fn(f64),
    pub playback_get_state: unsafe extern "C" fn() -> *mut c_char,
    pub playback_shutdown: unsafe extern "C" fn(),
    pub playback_set_eq_enabled: unsafe extern "C" fn(c_int),
    pub playback_set_eq_bands: unsafe extern "C" fn(*const f64),
    pub playback_get_eq_bands: unsafe extern "C" fn(*mut f64),
    pub set_event_callback: unsafe extern "C" fn(
        Option<unsafe extern "C" fn(*const c_char, *mut c_void)>,
        *mut c_void,
    ),
}
```

- [ ] **Step 3: Load the new symbols in `init_with_paths`**

In the existing symbol-loading block, add:

```rust
unsafe {
    playback_initialize: lib.get(b"EchoPlaybackInitialize")?,
    playback_play_url: lib.get(b"EchoPlaybackPlayUrl")?,
    playback_pause: lib.get(b"EchoPlaybackPause")?,
    playback_resume: lib.get(b"EchoPlaybackResume")?,
    playback_stop: lib.get(b"EchoPlaybackStop")?,
    playback_seek: lib.get(b"EchoPlaybackSeek")?,
    playback_set_volume: lib.get(b"EchoPlaybackSetVolume")?,
    playback_set_rate: lib.get(b"EchoPlaybackSetRate")?,
    playback_get_state: lib.get(b"EchoPlaybackGetState")?,
    playback_shutdown: lib.get(b"EchoPlaybackShutdown")?,
    playback_set_eq_enabled: lib.get(b"EchoPlaybackSetEqEnabled")?,
    playback_set_eq_bands: lib.get(b"EchoPlaybackSetEqBands")?,
    playback_get_eq_bands: lib.get(b"EchoPlaybackGetEqBands")?,
    set_event_callback: lib.get(b"EchoSetEventCallback")?,
}
```

- [ ] **Step 4: Build**

Run: `cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml 2>&1 | Select-Object -Last 5`
Expected: clean build (or only expected warnings about unused fields).

- [ ] **Step 5: Commit**

```bash
git add ui/src-tauri/src/backend_api.rs
git commit -m "feat(s4): extend CApiHandle with 14 playback fn pointers"
```

---

### Task 18: Rust event callback bridge + AppHandle registration

**Files:**
- Modify: `ui/src-tauri/src/backend_api.rs` — add `ffi_event_callback` + `APP_HANDLE` static
- Modify: `ui/src-tauri/src/main.rs` — register AppHandle after Tauri setup

**Interfaces:**
- Consumes: C event callback fired by C++ PlaybackController
- Produces: Tauri event emission to all listening frontend windows

- [ ] **Step 1: Add `APP_HANDLE` static to `backend_api.rs`**

```rust
use std::sync::OnceLock;
use tauri::AppHandle;

static APP_HANDLE: OnceLock<AppHandle> = OnceLock::new();

pub fn set_app_handle(handle: AppHandle) {
    let _ = APP_HANDLE.set(handle);
}
```

- [ ] **Step 2: Add `ffi_event_callback`**

```rust
pub unsafe extern "C" fn ffi_event_callback(
    json: *const c_char,
    _user: *mut c_void,
) {
    if json.is_null() {
        return;
    }
    let cstr = unsafe { CStr::from_ptr(json) };
    if let Ok(s) = cstr.to_str() {
        if let Some(handle) = APP_HANDLE.get() {
            let _ = handle.emit("playback_event", s.to_string());
        }
    }
}
```

- [ ] **Step 3: Register AppHandle in `main.rs` after Tauri setup**

Find the `.setup()` block in `main.rs` and add:

```rust
.setup(|app| {
    backend_api::set_app_handle(app.handle().clone());

    // ... existing setup code ...

    Ok(())
})
```

- [ ] **Step 4: Register the event callback in `init_with_paths`**

After the existing `set_log_callback().ok();` call, add:

```rust
let handle = APP_HANDLE.get().ok_or("AppHandle not set")?;
unsafe {
    (handle.set_event_callback)(Some(ffi_event_callback), std::ptr::null_mut());
}
```

(Adjust based on actual CApiHandle API; the callback registration likely already has a wrapper function in `init_with_paths` — extend it.)

- [ ] **Step 5: Build**

Run: `cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml 2>&1 | Select-Object -Last 5`
Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add ui/src-tauri/src/backend_api.rs ui/src-tauri/src/main.rs
git commit -m "feat(s4): add Rust event bridge (C callback -> Tauri emit)"
```

---

### Task 19: Rust Tauri commands (playback_*)

**Files:**
- Create: `ui/src-tauri/src/playback.rs` — 13 Tauri commands

**Interfaces:**
- Consumes: `CApiHandle` (Task 17)
- Produces: 13 `#[tauri::command]` functions callable from frontend

- [ ] **Step 1: Create `playback.rs`**

```rust
// ui/src-tauri/src/playback.rs
use std::ffi::{CStr, CString};
use tauri::State;
use crate::backend_api::CApiHandle;

const ERR_NO_HANDLE: &str = "C API not initialized";

#[tauri::command]
pub fn playback_initialize(backend: i32, handle: State<CApiHandle>) -> Result<bool, String> {
    let result = unsafe { (handle.playback_initialize)(backend as i32) };
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
```

(Note: the `free_str` cleanup for `playback_get_state`'s return is wrong above — use `handle.free_str` instead of `CString::from_raw`.)

- [ ] **Step 2: Build**

Run: `cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml 2>&1 | Select-Object -Last 5`
Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add ui/src-tauri/src/playback.rs
git commit -m "feat(s4): add 13 Rust Tauri commands for playback control"
```

---

### Task 20: Register commands in invoke_handler

**Files:**
- Modify: `ui/src-tauri/src/lib.rs` (or `main.rs`)

- [ ] **Step 1: Find `invoke_handler`**

In `ui/src-tauri/src/lib.rs` (or wherever the Tauri builder is configured), find the `tauri::generate_handler!` invocation.

- [ ] **Step 2: Add the 13 playback commands**

```rust
.invoke_handler(tauri::generate_handler![
    // existing commands...
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
```

(Add `mod playback;` at the top of the file if not already present.)

- [ ] **Step 3: Build and run Rust tests**

Run:
```bash
cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml 2>&1 | Select-Object -Last 5
cargo test --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml --lib 2>&1 | Select-Object -Last 10
```
Expected: existing Rust tests still pass (S1 backend tests); compilation succeeds.

- [ ] **Step 4: Commit**

```bash
git add ui/src-tauri/src/lib.rs ui/src-tauri/src/main.rs
git commit -m "feat(s4): register 13 playback Tauri commands"
```

---

### Task 21: Rust integration test — playback FFI end-to-end

**Files:**
- Create: `ui/src-tauri/tests/playback_ffi_test.rs`

**Interfaces:**
- Consumes: DLL with `EchoPlayback*` symbols (auto-loaded by build script)
- Produces: Test that exercises the full FFI path

- [ ] **Step 1: Create the test**

```rust
// ui/src-tauri/tests/playback_ffi_test.rs
use std::ffi::CString;
use std::path::PathBuf;
use std::time::Duration;

fn load_dll() -> PathBuf {
    let candidates = [
        std::env::var("ECHO_CAPI_DLL").ok(),
        Some(format!("{}/target/debug/EchoCAPI.dll", env!("CARGO_MANIFEST_DIR"))),
        Some(format!("{}/../../native/out/bottlemusic-check/EchoCAPI.dll",
                     env!("CARGO_MANIFEST_DIR"))),
    ];
    for c in candidates.into_iter().flatten() {
        let p = std::path::Path::new(&c);
        if p.exists() { return p.to_path_buf(); }
    }
    panic!("Could not find EchoCAPI.dll");
}

#[test]
fn test_playback_initialize_and_query_state() {
    use libloading::Library;
    let path = load_dll();
    let lib = unsafe { Library::new(&path) }.expect("load DLL");

    unsafe {
        let init: libloading::Symbol<unsafe extern "C" fn(i32) -> bool> =
            lib.get(b"EchoPlaybackInitialize").expect("find init");

        // Try MFS first
        let ok = init(1);
        if !ok {
            // MFS may fail in CI without audio device; try MFP
            let ok2 = init(0);
            assert!(ok2, "Both MFS and MFP init failed");
        }

        // Query state — should return valid JSON
        let get_state: libloading::Symbol<unsafe extern "C" fn() -> *mut i8> =
            lib.get(b"EchoPlaybackGetState").expect("find get_state");
        let ptr = get_state();
        assert!(!ptr.is_null());
        let json = CString::from_raw(ptr).into_string().expect("utf8");
        assert!(json.contains("state"));

        // Shutdown
        let shutdown: libloading::Symbol<unsafe extern "C" fn()> =
            lib.get(b"EchoPlaybackShutdown").expect("find shutdown");
        shutdown();
    }
}
```

- [ ] **Step 2: Build and run**

Run:
```bash
cargo test --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml --test playback_ffi_test 2>&1 | Select-Object -Last 10
```
Expected: test passes. The DLL is loaded, MFS init is attempted (may fall back to MFP in headless CI), state query returns JSON, shutdown completes.

- [ ] **Step 3: Commit**

```bash
git add ui/src-tauri/tests/playback_ffi_test.rs
git commit -m "test(s4): add playback FFI integration test"
```

---

**End of Part 3 — Phase 4.1b complete. Continue with Part 4 (Frontend).**
