#include "echo/playback/EqualizerMFT.h"
#include <mfapi.h>
#include <mfobjects.h>

namespace echo::playback {

EqualizerMFT::EqualizerMFT() = default;

EqualizerMFT::~EqualizerMFT() = default;

HRESULT STDMETHODCALLTYPE EqualizerMFT::QueryInterface(REFIID riid, void** object) {
  if (!object) return E_POINTER;
  if (riid == IID_IUnknown || riid == IID_IMFTransform) {
    *object = static_cast<IMFTransform*>(this);
    AddRef();
    return S_OK;
  }
  *object = nullptr;
  return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE EqualizerMFT::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&refCount_));
}

ULONG STDMETHODCALLTYPE EqualizerMFT::Release() {
  ULONG c = static_cast<ULONG>(InterlockedDecrement(&refCount_));
  if (c == 0) delete this;
  return c;
}

// Most IMFTransform methods return E_NOTIMPL for now
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamLimits(DWORD*, DWORD*, DWORD*, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamCount(DWORD*, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamIDs(DWORD, DWORD*, DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputStreamInfo(DWORD, MFT_INPUT_STREAM_INFO*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputStreamInfo(DWORD, MFT_OUTPUT_STREAM_INFO*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetAttributes(IMFAttributes**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputStreamAttributes(DWORD, IMFAttributes**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputStreamAttributes(DWORD, IMFAttributes**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::DeleteInputStream(DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::AddInputStreams(DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputAvailableType(DWORD, DWORD, IMFMediaType**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputAvailableType(DWORD, DWORD, IMFMediaType**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetInputType(DWORD, IMFMediaType*, DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetOutputType(DWORD, IMFMediaType*, DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputCurrentType(DWORD, IMFMediaType**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputCurrentType(DWORD, IMFMediaType**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputStatus(DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputStatus(DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetOutputBounds(LONGLONG, LONGLONG) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessEvent(DWORD, IMFMediaEvent*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessMessage(MFT_MESSAGE_TYPE, ULONG_PTR) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessInput(DWORD, IMFSample*, DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessOutput(DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER*, DWORD*) { return E_NOTIMPL; }

void EqualizerMFT::SetEnabled(bool enabled) {
  std::lock_guard lock(mutex_);
  enabled_ = enabled;
}

void EqualizerMFT::SetBandGain(int bandIndex, double gainDb) {
  if (bandIndex < 0 || bandIndex >= 5) return;
  if (gainDb > kMaxGainDb) gainDb = kMaxGainDb;
  if (gainDb < -kMaxGainDb) gainDb = -kMaxGainDb;
  std::lock_guard lock(mutex_);
  bands_[bandIndex].SetParams(44100.0, kBandFreqs[bandIndex], gainDb, kQ);
}

void EqualizerMFT::SetAllBandGains(const double gainsDb[5]) {
  for (int i = 0; i < 5; ++i) {
    SetBandGain(i, gainsDb[i]);
  }
}

}  // namespace echo::playback
