#include "echo/playback/PlaybackControllerMFS.h"

#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

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
  std::lock_guard lock(mutex_);
  switch (metype) {
    case MESessionStarted:
      state_.kind = echo::core::PlaybackStateKind::Playing;
      break;
    case MESessionPaused:
      state_.kind = echo::core::PlaybackStateKind::Paused;
      break;
    case MESessionStopped:
    case MESessionEnded:
      state_.kind = echo::core::PlaybackStateKind::Stopped;
      break;
    default: break;
  }
}

void PlaybackControllerMFS::EmitEvent(const char*, double, double, const char*) {
  // Real implementation in Task 8
}

void PlaybackControllerMFS::Pause() {}
void PlaybackControllerMFS::Resume() {}
void PlaybackControllerMFS::Stop() {}
void PlaybackControllerMFS::Seek(double) {}
void PlaybackControllerMFS::SetVolume(double) {}
void PlaybackControllerMFS::SetRate(double) {}
echo::core::PlaybackState PlaybackControllerMFS::GetState() const { return state_; }

std::unique_ptr<PlaybackControllerImpl> CreateMfsImpl() {
  return std::make_unique<PlaybackControllerMFS>();
}

}  // namespace echo::playback
