# S4 — C++ Playback Core + Equalizer Design

**Date**: 2026-06-24
**Status**: Ready for implementation plan
**Depends on**: S1 (FFI boundary discipline)
**Feeds**: S5 (play events come from PlaybackController)

## 1. Goal

Replace the high-level MFPlay-based `PlaybackController` with an `IMFMediaSession` pipeline that can accept a custom DSP `IMFTransform`, then insert a 5-band biquad equalizer into that pipeline. The existing HTML5 Audio path is preserved as a fallback.

**User stories** (from v2 umbrella PRD `docs/superpowers/prd/2026-06-23-bottleplayer-v2-prd.md`):

| ID | Story | Acceptance |
|---|---|---|
| 25 | Native C++ MF pipeline | Play, pause, stop, seek, volume, rate all work via native path |
| 26 | Same controls as today | Manual smoke test on existing test tracks |
| 28 | Lyrics in sync | `currentTime` from native event drives `LyricView` highlight |
| 29 | HTML5 fallback | Kill native (flip flag off) → audio continues via HTML5 |
| 30 | EQ with 5+ bands | Slider per band, 4 presets (Flat/Bass/Vocal/Rock), enable toggle |

## 2. Key Design Decisions

| # | Decision | Rationale |
|---|---|---|
| Migration | Dual path + runtime flag | Keep existing MFPlay code as fallback; new IMFMediaSession path runs in parallel |
| Backend selection | Native is default, HTML5 is fallback | Per PRD #29 "fall back to HTML5 if native fails to initialize" |
| Runtime flag | `EchoPlaybackInitialize(backend)` enum param | Backend choice is a startup decision, not a user toggle |
| Position updates | C++ internal 10Hz polling of `IMFPresentationClock::GetTime` | MF does not emit periodic position events; a background thread polls and emits to frontend |
| EQ design | 5 biquad peak filters (RBJ Audio EQ Cookbook) | One band = one 2nd-order IIR; standard, well-understood |
| Existing test compat | Pimpl idiom on `PlaybackController` | `basic_contract_tests.cpp:942-968` directly instantiates `PlaybackController`; Pimpl preserves the public class signature |
| MFS service access | Three separate services | Volume→`IMFSimpleAudioVolume`; Rate→`IMFRateControl`; Seek→`session->Start` with `VT_I8` |
| MFS async semantics | Eventual consistency | `Pause`/`Stop`/`SetTopology` return immediately; state changes confirmed via session events |
| EQ MFT registration | No COM registration | `new EqualizerMFT()` + `AddRef`, passed to `IMFTopologyNode::SetObject` |
| EQ MFT threading | Single-threaded DSP | MF calls `ProcessInput`/`ProcessOutput` on the decoder thread; `std::mutex` protects band state |
| EQ state startup sync | Frontend pushes to backend on init | `App.vue onMounted` reads localStorage `eqBands/eqEnabled` and calls `invoke('playback_set_eq_bands', ...)` |
| Frontend target | Current Rust/Tauri frontend (Newsprint skin) | Aurora skin adaptation is future work, not S4 |

## 3. Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  UI Layer (Vue 3)                                                   │
│  PlayerBar.vue ─── EqualizerPanel.vue (collapsible, below PlayerBar)│
│       │                                                             │
│       ▼                                                             │
│  playerStore (reactive) ──► activeBackend: PlayerBackend            │
│                                                                     │
│  PlayerBackend (interface)                                          │
│  ├─ Html5AudioBackend   (existing Audio element, fallback)          │
│  └─ NativePlaybackBackend (new, calls invoke('playback_*'))         │
│       │                       listen('playback_event')              │
└─────────────────────────────────────────────────────────────────────┘
        │ Tauri IPC
        ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Rust FFI (src-tauri/src/backend_api.rs)                           │
│  CApiHandle gains: playback_* fn pointers + ffi_event_callback      │
│  Tauri commands (13): playback_initialize / play_url / pause /     │
│    resume / stop / seek / set_volume / set_rate / get_state /      │
│    shutdown / set_eq_enabled / set_eq_bands / get_eq_bands         │
│  Event channel: app.emit("playback_event", jsonPayload)            │
└─────────────────────────────────────────────────────────────────────┘
        │ extern "C" (FFI boundary)
        ▼
┌─────────────────────────────────────────────────────────────────────┐
│  C++ Core (EchoCAPI.dll, links EchoPlayback + mf libs)             │
│                                                                     │
│  C API exports (14):                                                │
│    EchoPlaybackInitialize(backend) ◄── enum: MFP=0 / MFS=1         │
│    EchoPlaybackPlayUrl / Pause / Resume / Stop / Seek              │
│    EchoPlaybackSetVolume / SetRate / GetState / Shutdown           │
│    EchoPlaybackSetEqEnabled / SetEqBand / SetEqBands / GetEqBands  │
│                                                                     │
│  PlaybackController (public class, Pimpl idiom, ABI preserved)     │
│    └─ impl_: unique_ptr<PlaybackControllerImpl>                    │
│                                                                     │
│  PlaybackControllerImpl (abstract base)                            │
│  ├─ PlaybackControllerMFP  (existing MFPlay code, moved)           │
│  └─ PlaybackControllerMFS  (new, IMFMediaSession + EQ MFT)         │
│                                                                     │
│  EqualizerMFT (new, IMFTransform)                                  │
│    └─ 5× BiquadFilter (peak filter, RBJ Audio EQ Cookbook)        │
│                                                                     │
│  BiquadFilter (pure math class, directly unit-testable)            │
│    └─ ProcessSample(float) / SetParams(freq, gain, Q, sampleRate)  │
└─────────────────────────────────────────────────────────────────────┘
```

## 4. C++ Core Layer

### 4.1 PlaybackController (Pimpl public class)

```cpp
// native/include/echo/playback/PlaybackController.h
#pragma once
#include <memory>
#include "echo/core/Dto.h"

namespace echo::playback {

enum class Backend { MFP, MFS };

class PlaybackControllerImpl;

class PlaybackController {
 public:
  PlaybackController();
  ~PlaybackController();

  // Explicit backend selection. Returns false on failure (caller falls back to HTML5).
  bool Initialize(Backend backend = Backend::MFS);

  // Preserved ABI — basic_contract_tests.cpp:942-968 unchanged
  bool PlayUrl(const std::string& url);
  void Pause();
  void Resume();
  void Stop();
  void Seek(double seconds);
  void SetVolume(double volume);   // 0.0 - 1.0
  void SetRate(double rate);       // 0.5 - 2.0
  echo::core::PlaybackState GetState() const;

  // New: EQ control (MFS only; MFP is no-op)
  void SetEqEnabled(bool enabled);
  void SetEqBand(int bandIndex, double gainDb);
  void SetEqBands(const double gainsDb[5]);
  void GetEqBands(double outGainsDb[5]) const;

  // New: event callback (position/state changes)
  using EventCallback = void (*)(const char* jsonPayload, void* userData);
  void SetEventCallback(EventCallback cb, void* userData);

 private:
  std::unique_ptr<PlaybackControllerImpl> impl_;
};

}  // namespace echo::playback
```

The Pimpl indirection lets `basic_contract_tests.cpp:942-968` keep compiling and running unchanged. `Initialize()` defaults to `Backend::MFS`; if MFS init fails internally, the controller tries `Backend::MFP` before returning false.

### 4.2 PlaybackControllerImpl (abstract base)

```cpp
// native/include/echo/playback/PlaybackControllerImpl.h
namespace echo::playback {

class PlaybackControllerImpl {
 public:
  virtual ~PlaybackControllerImpl() = default;
  virtual bool Initialize() = 0;
  virtual bool PlayUrl(const std::string& url) = 0;
  virtual void Pause() = 0;
  virtual void Resume() = 0;
  virtual void Stop() = 0;
  virtual void Seek(double seconds) = 0;
  virtual void SetVolume(double volume) = 0;
  virtual void SetRate(double rate) = 0;
  virtual echo::core::PlaybackState GetState() const = 0;

  // EQ default no-op (MFP backend does not implement EQ)
  virtual void SetEqEnabled(bool) {}
  virtual void SetEqBand(int, double) {}
  virtual void SetEqBands(const double[5]) {}
  virtual void GetEqBands(double out[5]) const {
    for (int i = 0; i < 5; ++i) out[i] = 0.0;
  }

  virtual void SetEventCallback(PlaybackController::EventCallback, void*) {}
};

}  // namespace echo::playback
```

### 4.3 PlaybackControllerMFP (existing code, moved)

The entire current `PlaybackController.cpp` body moves to `PlaybackControllerMFP.cpp`. The class inherits `PlaybackControllerImpl`. EQ methods use the base class defaults (no-op). Event callback is stored but only invoked for state transitions (not position — MFP has no clean position clock accessor, and MFP is the fallback path).

### 4.4 PlaybackControllerMFS (new, IMFMediaSession)

```cpp
// native/playback/PlaybackControllerMFS.h
namespace echo::playback {

class EqualizerMFT;
class MfsEventCallback;  // IMFAsyncCallback impl

class PlaybackControllerMFS : public PlaybackControllerImpl {
 public:
  PlaybackControllerMFS();
  ~PlaybackControllerMFS() override;

  bool Initialize() override;
  bool PlayUrl(const std::string& url) override;
  void Pause() override;
  void Resume() override;
  void Stop() override;
  void Seek(double seconds) override;
  void SetVolume(double volume) override;
  void SetRate(double rate) override;
  echo::core::PlaybackState GetState() const override;

  void SetEqEnabled(bool enabled) override;
  void SetEqBand(int bandIndex, double gainDb) override;
  void SetEqBands(const double gainsDb[5]) override;
  void GetEqBands(double outGainsDb[5]) const override;

  void SetEventCallback(PlaybackController::EventCallback cb,
                        void* userData) override;

 private:
  // MF objects (raw pointers; released in destructor)
  IMFMediaSession* session_ = nullptr;
  IMFPresentationClock* clock_ = nullptr;
  IMFSimpleAudioVolume* audioVolume_ = nullptr;
  IMFRateControl* rateControl_ = nullptr;
  IMFMediaSource* mediaSource_ = nullptr;
  IMFTopology* topology_ = nullptr;
  EqualizerMFT* eqMft_ = nullptr;

  // State
  mutable std::mutex mutex_;
  echo::core::PlaybackState state_;
  bool comInitialized_ = false;
  bool mfStarted_ = false;
  bool eqEnabled_ = false;
  double eqGains_[5] = {0, 0, 0, 0, 0};
  double duration_ = 0.0;

  // Position polling thread (10Hz)
  std::thread positionThread_;
  std::atomic<bool> positionStop_{false};
  PlaybackController::EventCallback eventCb_ = nullptr;
  void* eventUserData_ = nullptr;

  // Helpers
  HRESULT BuildTopology(const std::string& url, IMFTopology** out);
  HRESULT InsertEqNode(IMFTopology* topo, IMFTopologyNode* decoderNode,
                       IMFTopologyNode* sinkNode);
  void PositionPollLoop();
  void EmitEvent(const char* type, double position, double duration,
                 const char* state);
  void OnSessionEvent(IMFMediaEvent* event, MediaEventType metype);
};

}  // namespace echo::playback
```

#### Initialize()

1. `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`
2. `MFStartup(MF_VERSION)`
3. `MFCreateMediaSession(nullptr, &session_)` — nullptr attributes = default config
4. `session_->BeginGetEvent(mfsEventCallback_, nullptr)` — register async event handler
5. Get services:
   - `MFGetService(session_, MR_AUDIO_POLICY_SERVICE, IID_PPV_ARGS(&audioVolume_))`
   - `MFGetService(session_, MF_RATE_CONTROL_SERVICE, IID_PPV_ARGS(&rateControl_))`
6. `session_->GetPresentationClock(&clock_)` — for position polling
7. Create `eqMft_ = new EqualizerMFT(); eqMft_->AddRef();`
8. Start `positionThread_` (10Hz polling loop)

#### PlayUrl(url)

1. `MFCreateSourceResolver()` → `CreateObjectFromURL(wideUrl, MF_RESOLUTION_MEDIASOURCE, ...)`
   - MF pulls the HTTP stream itself; no raw bytes cross the FFI boundary
2. `BuildTopology()` — see below
3. `session_->SetTopology(0, topology_)` — returns immediately
4. Wait for `MESessionTopologyReady` event (async; handled in `OnSessionEvent`)
5. On topology ready: `session_->Start(GUID_NULL, nullptr)` — start from beginning

#### BuildTopology(url)

1. `MFCreateTopology(&topo)`
2. Source node:
   - `MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &srcNode)`
   - `srcNode->SetUnknown(MF_TOPONODE_SOURCE, mediaSource_)`
   - `srcNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pd)`
   - `srcNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, sd)`
3. Decoder node:
   - `MFCreateTopologyNode(MF_TOPOLOGY_TRANSFORM_NODE, &decNode)`
   - Use `MFCreateTransformActivate()` or manually create decoder MFT based on source's media type
4. EQ node:
   - `MFCreateTopologyNode(MF_TOPOLOGY_TRANSFORM_NODE, &eqNode)`
   - `eqNode->SetObject(eqMft_)`
5. Sink node:
   - `MFCreateAudioRendererActivate(&sinkActivate)`
   - `MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &sinkNode)`
   - `sinkNode->SetObject(sinkActivate)`
6. Connect: `srcNode->ConnectOutput(0, decNode, 0)` → `decNode->ConnectOutput(0, eqNode, 0)` → `eqNode->ConnectOutput(0, sinkNode, 0)`
7. `topo->AddNode(srcNode)` / `AddNode(decNode)` / `AddNode(eqNode)` / `AddNode(sinkNode)`

#### Pause / Resume / Stop / Seek

- `Pause()` → `session_->Pause()` — returns immediately; `MESessionPaused` event confirms
- `Resume()` → `session_->Start(GUID_NULL, nullptr)` — NULL position = current
- `Stop()` → `session_->Stop()` — returns immediately; `MESessionStopped` event confirms
- `Seek(t)`:
  ```cpp
  PROPVARIANT var;
  var.vt = VT_I8;
  var.hVal.QuadPart = static_cast<LONGLONG>(t * 1e7);  // seconds → 100ns units
  session_->Start(GUID_NULL, &var);
  ```

#### SetVolume / SetRate

- `audioVolume_->SetMasterVolume(volume)` — per-stream, does not touch system volume
- `rateControl_->SetRate(FALSE, rate)` — FALSE = no thinning (preserve audio quality at non-1x rates)

#### PositionPollLoop (10Hz)

```cpp
void PlaybackControllerMFS::PositionPollLoop() {
  while (!positionStop_.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (state_ != PlaybackState::Playing) continue;
    MFTIME pos = 0;
    if (SUCCEEDED(clock_->GetTime(&pos))) {
      double posSec = pos / 1e7;
      EmitEvent("position", posSec, duration_, "playing");
    }
  }
}
```

#### EmitEvent

Serializes JSON: `{"type":"position","position":123.45,"duration":240.0,"state":"playing"}`
Calls `eventCb_(jsonStr, eventUserData_)`.

#### OnSessionEvent

Handles `MESessionTopologyReady` (start playback), `MESessionStarted`, `MESessionPaused`, `MESessionStopped`, `MESessionEnded` (trigger `next`), `MESessionClosed`. Updates `state_` and emits state events.

### 4.5 EqualizerMFT (new, IMFTransform)

```cpp
// native/playback/EqualizerMFT.h
namespace echo::playback {

class EqualizerMFT : public IMFTransform {
 public:
  EqualizerMFT();
  ~EqualizerMFT() override;

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;

  // IMFTransform — key methods implemented, others return E_NOTIMPL
  HRESULT STDMETHODCALLTYPE GetStreamLimits(DWORD*, DWORD*, DWORD*, DWORD*) override;
  HRESULT STDMETHODCALLTYPE GetStreamCount(DWORD*, DWORD*) override;
  HRESULT STDMETHODCALLTYPE GetStreamIDs(DWORD, DWORD*, DWORD, DWORD*) override;
  HRESULT STDMETHODCALLTYPE GetMediaTypeList(DWORD, DWORD, IMFMediaType***) override;
  HRESULT STDMETHODCALLTYPE GetMediaTypeAvailable(DWORD, DWORD, IMFMediaType**) override;
  HRESULT STDMETHODCALLTYPE GetCurrentMediaType(DWORD, BOOL, IMFMediaType**) override;
  HRESULT STDMETHODCALLTYPE SetInputType(DWORD, IMFMediaType*, DWORD) override;
  HRESULT STDMETHODCALLTYPE SetOutputType(DWORD, IMFMediaType*, DWORD) override;
  HRESULT STDMETHODCALLTYPE GetInputStatus(DWORD, DWORD*) override;
  HRESULT STDMETHODCALLTYPE GetOutputStatus(DWORD, DWORD*) override;
  HRESULT STDMETHODCALLTYPE ProcessInput(DWORD, IMFSample*, DWORD) override;
  HRESULT STDMETHODCALLTYPE ProcessOutput(DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER*, DWORD*) override;
  // ... remaining IMFTransform methods return E_NOTIMPL

  // EQ control
  void SetEnabled(bool enabled);
  void SetBandGain(int bandIndex, double gainDb);
  void SetAllBandGains(const double gainsDb[5]);

 private:
  volatile long refCount_ = 1;
  std::mutex mutex_;

  BiquadFilter bands_[5];
  bool enabled_ = false;

  IMFMediaType* inputType_ = nullptr;
  IMFMediaType* outputType_ = nullptr;

  // Default band center frequencies (Hz)
  static constexpr double kBandFreqs[5] = {60.0, 230.0, 910.0, 3600.0, 14000.0};
  static constexpr double kQ = 0.70710678;  // 1/sqrt(2), RBJ default
};

}  // namespace echo::playback
```

#### ProcessOutput detail

1. Receive `MFT_OUTPUT_DATA_BUFFER` containing `IMFSample`
2. `sample->ConvertToContiguousBuffer(&mediaBuffer)`
3. `mediaBuffer->Lock(&pData, &maxLen, &curLen)` — get raw bytes (PCM float32)
4. Interpret as `float*` samples
5. For each sample:
   ```cpp
   if (enabled_) {
     for (int b = 0; b < 5; ++b) {
       sample = bands_[b].ProcessSample(sample);
     }
   }
   // else: passthrough, no processing
   ```
6. `mediaBuffer->Unlock()`
7. Set the same sample as output (in-place processing)

#### SetMediaType validation

Accept only `MFAudioFormat_Float` (PCM 32-bit float), 1 or 2 channels, 44100 or 48000 Hz. Reject anything else — the decoder upstream should negotiate to float32.

#### Threading

Single-threaded DSP. MF calls `ProcessInput`/`ProcessOutput` on the decoder thread. `std::mutex` protects `bands_` state (user may call `SetBandGain` at any time from the UI thread).

### 4.6 BiquadFilter (pure math, unit-testable)

```cpp
// native/include/echo/playback/BiquadFilter.h
namespace echo::playback {

class BiquadFilter {
 public:
  BiquadFilter() = default;

  // Calculate coefficients for peak filter (RBJ Audio EQ Cookbook)
  void SetParams(double sampleRate, double freqHz, double gainDb, double Q);

  // Process one sample (Direct Form II Transposed, numerically stable for audio)
  float ProcessSample(float in);

  // Reset internal state (call on pause/seek to avoid artifacts)
  void Reset();

  // Test accessor: return current coefficients
  struct Coeffs { double b0, b1, b2, a1, a2; };
  Coeffs GetCoeffs() const { return coeffs_; }

 private:
  Coeffs coeffs_ = {1, 0, 0, 0, 0};  // default passthrough
  double z1_ = 0, z2_ = 0;            // DF2T state
};

}  // namespace echo::playback
```

#### RBJ peak filter coefficient formula

```
A  = 10^(gainDb/40)
w0 = 2π * freqHz / sampleRate
cosw0 = cos(w0)
sinw0 = sin(w0)
alpha = sinw0 / (2 * Q)

b0 = 1 + alpha * A
b1 = -2 * cosw0
b2 = 1 - alpha * A
a0 = 1 + alpha / A
a1 = -2 * cosw0
a2 = 1 - alpha / A

// Normalize (divide by a0)
b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
```

#### Direct Form II Transposed implementation

```cpp
float BiquadFilter::ProcessSample(float in) {
  double out = coeffs_.b0 * in + z1_;
  z1_ = coeffs_.b1 * in - coeffs_.a1 * out + z2_;
  z2_ = coeffs_.b2 * in - coeffs_.a2 * out;
  return static_cast<float>(out);
}
```

DF2T is chosen because it is numerically stable for high-frequency audio biquads and has low sensitivity to coefficient quantization.

## 5. C API Surface (C_API.cpp extension)

### 5.1 Process-global state

```cpp
// C_API.cpp new globals
static std::shared_ptr<PlaybackController> g_playback;
static std::mutex g_playback_mutex;
```

### 5.2 New exports

```cpp
typedef enum EchoPlaybackBackend {
  ECHO_PLAYBACK_MFP = 0,  // MFPlay, no EQ, fallback
  ECHO_PLAYBACK_MFS = 1,  // IMFMediaSession + EQ, default
} EchoPlaybackBackend;

// Initialization with explicit backend. Returns false on failure.
ECHO_API bool EchoPlaybackInitialize(EchoPlaybackBackend backend);

// Playback control
ECHO_API bool EchoPlaybackPlayUrl(const char* url);
ECHO_API void EchoPlaybackPause();
ECHO_API void EchoPlaybackResume();
ECHO_API void EchoPlaybackStop();
ECHO_API void EchoPlaybackSeek(double seconds);
ECHO_API void EchoPlaybackSetVolume(double volume);
ECHO_API void EchoPlaybackSetRate(double rate);

// State query (synchronous, for frontend first-load)
ECHO_API const char* EchoPlaybackGetState();  // JSON: {state, position, duration}

// EQ
ECHO_API void EchoPlaybackSetEqEnabled(bool enabled);
ECHO_API void EchoPlaybackSetEqBand(int bandIndex, double gainDb);
ECHO_API void EchoPlaybackSetEqBands(const double gainsDb[5]);
ECHO_API void EchoPlaybackGetEqBands(double outGainsDb[5]);

// Shutdown
ECHO_API void EchoPlaybackShutdown();

// Event callback (existing stub, gets real body)
ECHO_API void EchoSetEventCallback(EchoEventCallback cb, void* user_data);
```

### 5.3 EchoPlaybackInitialize implementation

```cpp
bool EchoPlaybackInitialize(EchoPlaybackBackend backend) {
  std::lock_guard lock(g_playback_mutex);
  if (g_playback) return true;
  auto pc = std::make_shared<PlaybackController>();
  bool ok = pc->Initialize(static_cast<PlaybackController::Backend>(backend));
  if (!ok && backend == ECHO_PLAYBACK_MFS) {
    // Auto-fallback: MFS failed, try MFP
    ok = pc->Initialize(PlaybackController::Backend::MFP);
  }
  if (!ok) return false;
  g_playback = pc;
  return true;
}
```

### 5.4 EchoSetEventCallback wiring

```cpp
void EchoSetEventCallback(EchoEventCallback cb, void* user_data) {
  std::lock_guard lock(g_playback_mutex);
  if (g_playback) {
    g_playback->SetEventCallback(cb, user_data);
  }
}
```

### 5.5 CMakeLists.txt changes

```cmake
# EchoPlayback already exists as a static lib; just add new sources
add_library(EchoPlayback STATIC
  playback/PlaybackController.cpp    # Pimpl wrapper
  playback/PlaybackControllerMFP.cpp # moved from PlaybackController.cpp
  playback/PlaybackControllerMFS.cpp # new
  playback/EqualizerMFT.cpp          # new
  playback/BiquadFilter.cpp          # new
)

# Link EchoPlayback into EchoCAPI.dll (currently only EchoCore is linked)
target_link_libraries(EchoCAPI PRIVATE EchoCore EchoPlayback)
# mf libs come transitively from EchoPlayback's existing mfplay dependency
```

## 6. Rust FFI Bindings (backend_api.rs)

### 6.1 CApiHandle extension

```rust
pub struct CApiHandle {
    _lib: Library,
    handle_req: unsafe extern "C" fn(...) -> ...,
    free_str: unsafe extern "C" fn(...),
    // New playback fn pointers
    playback_initialize: unsafe extern "C" fn(c_int) -> bool,
    playback_play_url: unsafe extern "C" fn(*const c_char) -> bool,
    playback_pause: unsafe extern "C" fn(),
    playback_resume: unsafe extern "C" fn(),
    playback_stop: unsafe extern "C" fn(),
    playback_seek: unsafe extern "C" fn(f64),
    playback_set_volume: unsafe extern "C" fn(f64),
    playback_set_rate: unsafe extern "C" fn(f64),
    playback_get_state: unsafe extern "C" fn() -> *mut c_char,
    playback_shutdown: unsafe extern "C" fn(),
    playback_set_eq_enabled: unsafe extern "C" fn(c_int),
    playback_set_eq_bands: unsafe extern "C" fn(*const f64),
    playback_get_eq_bands: unsafe extern "C" fn(*mut f64),
    set_event_callback: unsafe extern "C" fn(
        Option<unsafe extern "C" fn(*const c_char, *mut c_void)>,
        *mut c_void,
    ),
}
```

### 6.2 Event callback bridge

```rust
static APP_HANDLE: OnceLock<AppHandle> = OnceLock::new();

unsafe extern "C" fn ffi_event_callback(json: *const c_char, _user: *mut c_void) {
    if json.is_null() { return; }
    if let Ok(s) = CStr::from_ptr(json).to_str() {
        if let Some(handle) = APP_HANDLE.get() {
            let _ = handle.emit("playback_event", s.to_string());
        }
    }
}

// In setup():
APP_HANDLE.set(app.handle().clone()).ok();
init_with_paths(dll_path, Some(app_data_dir))?;
set_event_callback(Some(ffi_event_callback), std::ptr::null_mut()).ok();
```

### 6.3 Tauri commands

```rust
#[tauri::command]
fn playback_initialize(backend: i32) -> Result<bool, String> { ... }

#[tauri::command]
fn playback_play_url(url: String) -> Result<bool, String> { ... }

#[tauri::command]
fn playback_pause() -> Result<(), String> { ... }

#[tauri::command]
fn playback_resume() -> Result<(), String> { ... }

#[tauri::command]
fn playback_stop() -> Result<(), String> { ... }

#[tauri::command]
fn playback_seek(seconds: f64) -> Result<(), String> { ... }

#[tauri::command]
fn playback_set_volume(volume: f64) -> Result<(), String> { ... }

#[tauri::command]
fn playback_set_rate(rate: f64) -> Result<(), String> { ... }

#[tauri::command]
fn playback_get_state() -> Result<String, String> { ... }

#[tauri::command]
fn playback_shutdown() -> Result<(), String> { ... }

#[tauri::command]
fn playback_set_eq_enabled(enabled: bool) -> Result<(), String> { ... }

#[tauri::command]
fn playback_set_eq_bands(gains: Vec<f64>) -> Result<(), String> {
    if gains.len() != 5 { return Err("expected 5 bands".into()); }
    // ...
}

#[tauri::command]
fn playback_get_eq_bands() -> Result<Vec<f64>, String> { ... }
```

### 6.4 invoke_handler registration

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

## 7. Frontend Layer

### 7.1 PlayerBackend interface

```typescript
// ui/src/api/playerBackend.ts
export interface PlayerBackend {
  readonly kind: 'html5' | 'native';
  initialize(): Promise<boolean>;
  playUrl(url: string): Promise<boolean>;
  pause(): Promise<void>;
  resume(): Promise<void>;
  stop(): Promise<void>;
  seek(seconds: number): Promise<void>;
  setVolume(v: number): Promise<void>;
  setRate(r: number): Promise<void>;
  getState(): Promise<{ state: string; position: number; duration: number }>;
  shutdown(): Promise<void>;
  onEvent(cb: (e: PlaybackEvent) => void): () => void;
}

export interface PlaybackEvent {
  type: 'position' | 'state' | 'ended' | 'error';
  position?: number;
  duration?: number;
  state?: string;
  error?: string;
}
```

### 7.2 Html5AudioBackend

Wraps the existing `HTMLAudioElement`. All methods delegate to the element. `onEvent` wires `timeupdate`/`play`/`pause`/`ended`/`error` listeners.

### 7.3 NativePlaybackBackend

Calls `invoke('playback_*')` for all operations. `initialize()` tries MFS first (backend=1), then MFP (backend=0) as auto-fallback, then returns false (caller falls back to HTML5). `onEvent` calls `listen('playback_event')` and parses JSON payloads.

### 7.4 playerStore changes

New fields on `PlayerState`:
- `backend: 'html5' | 'native' | null` — null until initialized
- `eqEnabled: boolean`
- `eqBands: number[]` — 5 values in dB
- `activePreset: string`

New function `initPlayerBackend()`:
1. Try `NativePlaybackBackend.initialize()`
2. If success: `playerStore.backend = 'native'`, wire event listener
3. If fail: `playerStore.backend = 'html5'`, create `Html5AudioBackend(playerStore.audio)`, wire event listener
4. On native: push EQ state from localStorage to backend (`invoke('playback_set_eq_enabled')` + `invoke('playback_set_eq_bands')`)

Existing `playTrack`/`togglePlay`/`next`/`prev`/`seek`/`setVolume` delegate to `activeBackend` instead of directly touching `playerStore.audio`.

`handlePlaybackEvent(e)`:
- `type === 'position'` → update `playerStore.currentTime` + `duration`
- `type === 'state'` → update `playerStore.isPlaying`
- `type === 'ended'` → call `next()`
- `type === 'error'` → set `playerStore.errorMsg`

### 7.5 EqualizerPanel.vue

New component, placed below `PlayerBar.vue` as a collapsible panel.

Features:
- Toggle button (collapsed/expanded) showing `EQ ON` / `EQ OFF`
- Enable checkbox
- Preset dropdown (Flat / Bass Boost / Vocal / Rock)
- 5 vertical range sliders (−12 to +12 dB, step 0.5)
- Band labels: `60Hz / 230Hz / 910Hz / 3.6kHz / 14kHz`
- Gain readout per band (`+6.0dB` / `−3.5dB`)
- Disabled state when `playerStore.backend !== 'native'` (shows hint: "Native 后端未启用, EQ 不可用")

Presets:
```typescript
const presets: Record<string, number[]> = {
  'Flat':       [0, 0, 0, 0, 0],
  'Bass Boost': [6, 4, 0, 0, 0],
  'Vocal':      [0, 2, 4, 2, 0],
  'Rock':       [4, 2, -2, 2, 4],
};
```

Persistence:
- `player_eq_bands` → JSON array in localStorage
- `player_eq_enabled` → 'true'/'false' in localStorage
- `player_eq_preset` → preset name in localStorage

On slider change: update `playerStore.eqBands`, write localStorage, call `invoke('playback_set_eq_bands', { gains })` if native.

### 7.6 PlayerBar.vue integration

Add `<EqualizerPanel v-model="eqExpanded" />` below the existing player bar content. `eqExpanded` defaults to `false` (collapsed).

## 8. Migration Phases

### Phase 4.1a — C++ MFS pipeline (no EQ)

- New files: `PlaybackControllerImpl.h`, `PlaybackControllerMFS.{h,cpp}`
- Move existing `PlaybackController.cpp` body to `PlaybackControllerMFP.cpp`
- `PlaybackController` → Pimpl
- MFS topology: Source → Decoder → SAR (no EQ node yet)
- **Checkpoint**: unit test `PlaybackControllerMFS` plays a local WAV file end-to-end
- **Not touched**: C API, Rust, frontend

### Phase 4.1b — C API + Rust FFI + Tauri commands

- `C_API.cpp` adds 14 exports
- `backend_api.rs` extends `CApiHandle` + 13 Tauri commands
- `EchoSetEventCallback` wired to `PlaybackController`
- Position polling thread started in `Initialize()`
- **Checkpoint**: Rust unit test calls `playback_initialize(MFS)` + `playback_play_url(wav)`, receives position event

### Phase 4.1c — Frontend NativePlaybackBackend

- `PlayerBackend` interface + `Html5AudioBackend` + `NativePlaybackBackend`
- `playerStore` gains `backend` field + `initPlayerBackend()`
- Event subscription drives `playerStore.currentTime`
- Existing `playTrack`/`togglePlay`/etc delegate to `activeBackend`
- **Checkpoint**: frontend switches to native, plays a song, lyrics scroll in sync
- **Fallback test**: force `playback_initialize` to return false, verify HTML5 takes over

### Phase 4.2a — BiquadFilter pure math + unit tests

- `BiquadFilter.{h,cpp}` pure math class
- Unit test: generate 1s chirp (20Hz–20kHz linear sweep) → process through BiquadFilter with each band set to +6dB → FFT → verify magnitude at band center frequency is +6dB ±0.5dB, and far from center is ~0dB ±1dB
- **Checkpoint**: 5 band frequency response curves verified

### Phase 4.2b — EqualizerMFT (IMFTransform)

- `EqualizerMFT.{h,cpp}` wraps 5 BiquadFilters
- Implements `ProcessInput`/`ProcessOutput` for PCM float32
- Inserts into MFS topology (Source → Decoder → EQ → SAR)
- C API exports + Rust bindings + Tauri commands for EQ
- **Checkpoint**: play music, `SetEqBands([6,0,0,0,0])` produces audible bass boost

### Phase 4.2c — EqualizerPanel.vue

- New component + PlayerBar integration
- localStorage persistence
- 4 preset buttons
- **Checkpoint**: switching presets produces audible difference, restart restores EQ state

### Phase 4.3 — Polish

- Verify position events at 10Hz (lyric sync < 200ms drift)
- Verify seek updates position immediately
- End-to-end HTML5 fallback test
- Cleanup: if MFS is stable, mark MFP code as deprecated (do not delete yet)
- **Checkpoint**: all tests pass, manual smoke test on real music

## 9. Test Strategy

### 9.1 C++ unit tests (CTest, highest seam)

**BiquadFilter test** (`native/tests/biquad_filter_test.cpp`):
- Generate 1s chirp (20Hz–20kHz linear sweep) at 44100 Hz
- Process through BiquadFilter with `SetParams(44100, 910, 6.0, 0.707)`
- FFT the output
- Assert: magnitude at 910 Hz ≈ +6dB ±0.5dB
- Assert: magnitude at 60 Hz and 14000 Hz ≈ 0dB ±1dB
- Repeat for each of the 5 band frequencies

**EqualizerMFT test** (`native/tests/equalizer_mft_test.cpp`):
- Create EqualizerMFT, set input/output type = PCM float32 44100Hz mono
- Generate 1s sine wave at 910 Hz
- Wrap in IMFSample, call ProcessInput + ProcessOutput
- Assert: output RMS is ~+6dB higher than input when band 2 is set to +6dB
- Assert: output equals input (bit-exact) when EQ is disabled

**PlaybackControllerMFS test** (extend `basic_contract_tests.cpp:942-968`):
- `PlaybackController pc; pc.Initialize(Backend::MFS);`
- `pc.PlayUrl("file:///C:/path/to/test.wav");`
- Sleep 500ms, assert `GetState()` is Playing
- `pc.Stop();`

### 9.2 Rust unit tests

```rust
#[test]
fn test_playback_initialize_and_play() {
    init_c_api();
    let ok = unsafe { playback_initialize(1) };  // MFS
    assert!(ok, "native playback init should succeed");
    let url = std::ffi::CString::new("file:///C:/test.wav").unwrap();
    let ok = unsafe { playback_play_url(url.as_ptr()) };
    assert!(ok);
    std::thread::sleep(std::time::Duration::from_millis(500));
    let state_ptr = unsafe { playback_get_state() };
    let state = unsafe { CStr::from_ptr(state_ptr).to_str().unwrap() };
    assert!(state.contains("playing"));
    unsafe { playback_shutdown(); }
}
```

### 9.3 Frontend tests (Vitest + jsdom)

**PlayerBackend mock test**:
- Create mock `PlayerBackend` with `vi.fn()` spies
- Wire into `playerStore`
- Call `playTrack(mockTrack)`
- Assert `mockBackend.playUrl` was called with `mockTrack.url`

**EqualizerPanel component test**:
- Mount `EqualizerPanel` with `playerStore.backend = 'native'`
- Simulate slider drag on band 0
- Assert `invoke('playback_set_eq_bands')` was called
- Assert `localStorage.player_eq_bands` was updated

**Fallback test**:
- Mock `invoke('playback_initialize')` to return false
- Call `initPlayerBackend()`
- Assert `playerStore.backend === 'html5'`

## 10. Scope Discipline (YAGNI)

### In scope

- IMFMediaSession pipeline (Source → Decoder → EQ → SAR)
- 5-band EQ MFT (biquad peak filter)
- C API + Rust FFI + Tauri commands
- PlayerBackend abstraction + HTML5 fallback
- EqualizerPanel.vue (collapsible panel below PlayerBar)
- 4 presets (Flat / Bass Boost / Vocal / Rock)
- localStorage persistence for EQ settings
- 10Hz position events via C++ polling thread
- Frontend changes target the current Rust/Tauri frontend (Newsprint skin)

### Out of scope

- Output device selection (default device only)
- 3D positional audio / spatial audio
- Gapless playback
- ReplayGain / volume normalization
- Cross-platform (Windows only — Media Foundation is a Windows API)
- S5 content (play statistics, dashboard)
- Modifying S1–S3 surface area
- Aurora skin adaptation of EqualizerPanel (future work)

## 11. Dependencies

- **S4 depends on S1**: shared FFI boundary discipline (C_API.cpp pattern, shared_ptr lifetime)
- **S4 does not depend on S2/S3**: auto-update and skin system are independent
- **S5 depends on S4**: play events come from PlaybackController via the event callback

## 12. Risks

| Risk | Mitigation |
|---|---|
| MFS topology build fails for some codecs | Auto-fallback to MFP, then HTML5 |
| EQ MFT format negotiation fails | Log + passthrough (disable EQ, keep playing) |
| Position polling drifts on slow machines | Acceptable — 10Hz is fast enough for lyrics (< 200ms drift) |
| MFStartup/Shutdown ordering crashes | Follow MSDN lifecycle exactly; test under rapid init/shutdown |
| Audio glitches when SetBandGain called mid-playback | BiquadFilter uses atomic coefficient swap (set new coeffs, then process — no partial state) |

## 13. Known Limitations (S4.2b → S4.3)

As of the S4.2b checkpoint:

1. **EqualizerMFT is not yet inserted into the MFS topology.** The custom MFT
   is implemented and unit-testable in isolation, but Media Foundation's
   topology loader does not easily allow inserting a custom MFT between the
   auto-inserted decoder and the SAR. Two follow-up paths:
   - (a) Explicit topology construction using IMFTopologyNode::ConnectOutput
     and MF_TOPOLOGY_HELPER_METHOD_PRESERVE_ID (more code, more reliable).
   - (b) Post-decode buffer processing via WASAPI exclusive mode (more
     invasive, requires architectural change).
   Until this is resolved, the EQ is reachable via the C API but the MFS
   pipeline plays without EQ applied. UI shows the panel but sliders are
   effectively no-ops on the audio path. This is a known issue and tracked
   for a future S4.x follow-up.

2. **MFP code is not deleted** in this phase. It is marked as deprecated
   with a comment pointing to the new MFS path. Deletion is a follow-up
   once MFS is verified stable in production for at least one full release
   cycle.

3. **Output device selection is not implemented.** SAR uses the default
   Windows audio device. Switching to a specific device (e.g., USB DAC)
   is a future enhancement.
