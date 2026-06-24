#include "echo/playback/PlaybackControllerMFS.h"

#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstdio>
#include <future>

#include "echo/diagnostics/EchoDiagnostics.h"

namespace echo::playback {

class MfsEventCallback final : public IMFAsyncCallback {
 public:
  explicit MfsEventCallback(PlaybackControllerMFS* owner) : owner_(owner) {}
  MfsEventCallback(const MfsEventCallback&) = delete;
  MfsEventCallback& operator=(const MfsEventCallback&) = delete;

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (!object) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IMFAsyncCallback) {
      *object = static_cast<IMFAsyncCallback*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&refCount_));
  }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG c = static_cast<ULONG>(InterlockedDecrement(&refCount_));
    if (c == 0) delete this;
    return c;
  }

  // IMFAsyncCallback
  HRESULT STDMETHODCALLTYPE GetParameters(DWORD* flags, DWORD* queue) override {
    *flags = 0;
    *queue = MFASYNC_CALLBACK_QUEUE_STANDARD;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult* result) override {
    if (!owner_) return S_OK;
    IMFMediaEvent* event = nullptr;
    if (SUCCEEDED(result->GetStatus()) &&
        SUCCEEDED(result->GetObject(reinterpret_cast<IUnknown**>(&event)))) {
      MediaEventType metype = MEUnknown;
      event->GetType(&metype);
      owner_->OnSessionEvent(metype);
      event->Release();
    }
    return S_OK;
  }

  void Detach() { owner_ = nullptr; }

 private:
  ~MfsEventCallback() = default;
  volatile long refCount_ = 1;
  PlaybackControllerMFS* owner_ = nullptr;
};

PlaybackControllerMFS::PlaybackControllerMFS() = default;

PlaybackControllerMFS::~PlaybackControllerMFS() {
  positionStop_.store(true, std::memory_order_release);
  if (positionThread_.joinable()) {
    // Bounded join: 1s deadline. Mirrors RequestScheduler::Shutdown(maxWait):
    // a helper thread owns the std::thread lifetime via new/delete, so the
    // main thread can detach the helper on timeout without leaving the
    // std::thread in a joinable state at scope exit.
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    auto* thr = new std::thread(std::move(positionThread_));
    std::thread helper([thr, done] {
      thr->join();
      done->set_value();
      delete thr;
    });
    if (fut.wait_for(std::chrono::milliseconds(1000)) !=
        std::future_status::ready) {
      helper.detach();
    } else {
      helper.join();
    }
  }
  if (clock_) {
    clock_->Release();
    clock_ = nullptr;
  }
  if (audioVolume_) {
    audioVolume_->Release();
    audioVolume_ = nullptr;
  }
  if (rateControl_) {
    rateControl_->Release();
    rateControl_ = nullptr;
  }
  if (session_) {
    session_->Close();
    session_->Shutdown();
    ULONG refCount = session_->Release();
    while (refCount > 0) refCount = session_->Release();
  }
  if (eventCallback_) {
    eventCallback_->Detach();
    eventCallback_->Release();
  }
  if (mfStarted_) MFShutdown();
  if (comInitialized_) CoUninitialize();
}

bool PlaybackControllerMFS::Initialize() {
  std::lock_guard lock(mutex_);
  if (mfStarted_) return true;
  const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(comHr)) {
    comInitialized_ = true;
  } else if (comHr != RPC_E_CHANGED_MODE) {
    ECHO_LOG("PlaybackMFS", "CoInitializeEx failed");
    return false;
  }
  HRESULT hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    ECHO_LOG("PlaybackMFS", "MFStartup failed");
    return false;
  }
  mfStarted_ = true;
  hr = MFCreateMediaSession(nullptr, &session_);
  if (FAILED(hr)) {
    ECHO_LOG("PlaybackMFS", "MFCreateMediaSession failed");
    return false;
  }
  eventCallback_ = new MfsEventCallback(this);
  hr = session_->BeginGetEvent(eventCallback_, nullptr);
  if (FAILED(hr)) {
    ECHO_LOG("PlaybackMFS", "BeginGetEvent failed");
    return false;
  }
  // Service accessors. Failures are non-fatal: the controller still plays,
  // it just cannot adjust volume/rate or query the presentation clock.
  if (session_) {
    if (FAILED(MFGetService(session_, MR_AUDIO_POLICY_SERVICE,
                            IID_PPV_ARGS(&audioVolume_)))) {
      audioVolume_ = nullptr;
    }
    if (FAILED(MFGetService(session_, MF_RATE_CONTROL_SERVICE,
                            IID_PPV_ARGS(&rateControl_)))) {
      rateControl_ = nullptr;
    }
    if (FAILED(session_->GetPresentationClock(&clock_))) {
      clock_ = nullptr;
    }
  }
  return true;
}

HRESULT PlaybackControllerMFS::BuildTopology(const std::string& url,
                                              IMFTopology** out) {
  // Convert URL to wide string
  int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
  std::wstring wideUrl(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wideUrl.data(), wlen);

  // Resolve URL to media source
  IMFSourceResolver* resolver = nullptr;
  HRESULT hr = MFCreateSourceResolver(&resolver);
  if (FAILED(hr)) return hr;
  IUnknown* sourceUnk = nullptr;
  MF_OBJECT_TYPE objType = {};
  hr = resolver->CreateObjectFromURL(wideUrl.c_str(), MF_RESOLUTION_MEDIASOURCE,
                                     nullptr, &objType, &sourceUnk);
  resolver->Release();
  if (FAILED(hr)) return hr;
  hr = sourceUnk->QueryInterface(IID_PPV_ARGS(&mediaSource_));
  sourceUnk->Release();
  if (FAILED(hr)) return hr;

  // Create presentation descriptor
  IMFPresentationDescriptor* pd = nullptr;
  hr = mediaSource_->CreatePresentationDescriptor(&pd);
  if (FAILED(hr)) return hr;

  // Create topology
  hr = MFCreateTopology(out);
  if (FAILED(hr)) {
    pd->Release();
    return hr;
  }

  // For simplicity: take the first audio stream and let MF's topology
  // loader auto-insert decoder + SAR. (EQ insertion is Phase 4.2b.)
  DWORD streamCount = 0;
  pd->GetStreamDescriptorCount(&streamCount);
  for (DWORD i = 0; i < streamCount; ++i) {
    BOOL selected = FALSE;
    IMFStreamDescriptor* sd = nullptr;
    pd->GetStreamDescriptorByIndex(i, &selected, &sd);
    if (selected) {
      // Source node
      IMFTopologyNode* srcNode = nullptr;
      if (SUCCEEDED(MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE,
                                          &srcNode))) {
        srcNode->SetUnknown(MF_TOPONODE_SOURCE, mediaSource_);
        srcNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pd);
        srcNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, sd);
        (*out)->AddNode(srcNode);
        srcNode->Release();
      }
    }
    if (sd) sd->Release();
  }
  pd->Release();

  // Let MF topology loader complete the rest (decoder + SAR)
  return S_OK;
}

bool PlaybackControllerMFS::PlayUrl(const std::string& url) {
  std::lock_guard lock(mutex_);
  if (!session_) return false;
  if (topology_) {
    topology_->Release();
    topology_ = nullptr;
  }
  HRESULT hr = BuildTopology(url, &topology_);
  if (FAILED(hr)) {
    ECHO_LOG("PlaybackMFS", "BuildTopology failed");
    return false;
  }
  hr = session_->SetTopology(0, topology_);
  if (FAILED(hr)) {
    ECHO_LOG("PlaybackMFS", "SetTopology failed");
    return false;
  }
  return true;
}

void PlaybackControllerMFS::OnSessionEvent(MediaEventType metype) {
  const char* stateStr = nullptr;
  {
    std::lock_guard lock(mutex_);
    switch (metype) {
      case MESessionStarted:
        state_.kind = echo::core::PlaybackStateKind::Playing;
        stateStr = "playing";
        break;
      case MESessionPaused:
        state_.kind = echo::core::PlaybackStateKind::Paused;
        stateStr = "paused";
        break;
      case MESessionStopped:
      case MESessionEnded:
        state_.kind = echo::core::PlaybackStateKind::Stopped;
        stateStr = "stopped";
        break;
      default: break;
    }
  }
  if (stateStr) EmitEvent("state", 0, 0, stateStr);

  if (session_ && eventCallback_) {
    HRESULT hr = session_->BeginGetEvent(eventCallback_, nullptr);
    if (FAILED(hr)) {
      ECHO_LOG("PlaybackMFS", "BeginGetEvent re-arm failed");
    }
  }
}

void PlaybackControllerMFS::Pause() {
  std::lock_guard lock(mutex_);
  if (!session_) return;
  HRESULT hr = session_->Pause();
  if (FAILED(hr)) ECHO_LOG("PlaybackMFS", "Pause failed");
}

void PlaybackControllerMFS::Resume() {
  std::lock_guard lock(mutex_);
  if (!session_) return;
  HRESULT hr = session_->Start(GUID_NULL, nullptr);
  if (FAILED(hr)) ECHO_LOG("PlaybackMFS", "Resume failed");
}

void PlaybackControllerMFS::Stop() {
  std::lock_guard lock(mutex_);
  if (!session_) return;
  HRESULT hr = session_->Stop();
  if (FAILED(hr)) ECHO_LOG("PlaybackMFS", "Stop failed");
}

void PlaybackControllerMFS::Seek(double seconds) {
  std::lock_guard lock(mutex_);
  if (!session_) return;
  PROPVARIANT var;
  PropVariantInit(&var);
  var.vt = VT_I8;
  var.hVal.QuadPart = static_cast<LONGLONG>(seconds * 1e7);
  HRESULT hr = session_->Start(GUID_NULL, &var);
  PropVariantClear(&var);
  if (FAILED(hr)) ECHO_LOG("PlaybackMFS", "Seek failed");
}

void PlaybackControllerMFS::EmitEvent(const char* type, double position,
                                      double duration, const char* state) {
  PlaybackController::EventCallback cb;
  void* userData;
  {
    std::lock_guard lock(mutex_);
    cb = eventCb_;
    userData = eventUserData_;
  }
  if (!cb) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "{\"type\":\"%s\",\"position\":%.3f,\"duration\":%.3f,"
                "\"state\":\"%s\"}",
                type, position, duration, state);
  cb(buf, userData);
}

void PlaybackControllerMFS::SetVolume(double volume) {
  std::lock_guard lock(mutex_);
  if (audioVolume_) {
    audioVolume_->SetMasterVolume(static_cast<float>(volume));
  }
}

void PlaybackControllerMFS::SetRate(double rate) {
  std::lock_guard lock(mutex_);
  if (rateControl_) {
    rateControl_->SetRate(FALSE, static_cast<float>(rate));
  }
}

void PlaybackControllerMFS::SetEventCallback(
    PlaybackController::EventCallback cb, void* userData) {
  std::lock_guard lock(mutex_);
  eventCb_ = cb;
  eventUserData_ = userData;
  if (cb && !positionThread_.joinable()) {
    positionStop_.store(false, std::memory_order_release);
    positionThread_ = std::thread([this] { PositionPollLoop(); });
  }
}

void PlaybackControllerMFS::PositionPollLoop() {
  while (!positionStop_.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    PlaybackController::EventCallback cb;
    void* userData;
    echo::core::PlaybackStateKind curKind;
    {
      std::lock_guard lock(mutex_);
      if (!clock_ || !eventCb_) continue;
      cb = eventCb_;
      userData = eventUserData_;
      curKind = state_.kind;
    }
    if (curKind != echo::core::PlaybackStateKind::Playing) continue;
    MFTIME pos = 0;
    if (SUCCEEDED(clock_->GetTime(&pos))) {
      EmitEvent("position", pos / 1e7, duration_, "playing");
    }
  }
}

echo::core::PlaybackState PlaybackControllerMFS::GetState() const { return state_; }

std::unique_ptr<PlaybackControllerImpl> CreateMfsImpl() {
  return std::make_unique<PlaybackControllerMFS>();
}

}  // namespace echo::playback
