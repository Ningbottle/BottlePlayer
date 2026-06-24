#include "echo/playback/EqualizerMFT.h"
#include <mfapi.h>
#include <mfobjects.h>

namespace echo::playback {

EqualizerMFT::EqualizerMFT() = default;

EqualizerMFT::~EqualizerMFT() {
  if (inputType_) inputType_->Release();
  if (outputType_) outputType_->Release();
}

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
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamLimits(DWORD* minIn, DWORD* maxIn, DWORD* minOut, DWORD* maxOut) {
  if (!minIn || !maxIn || !minOut || !maxOut) return E_POINTER;
  *minIn = *maxIn = 1;
  *minOut = *maxOut = 1;
  return S_OK;
}
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamCount(DWORD* in, DWORD* out) {
  if (!in || !out) return E_POINTER;
  *in = *out = 1;
  return S_OK;
}
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamIDs(DWORD, DWORD*, DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputStreamInfo(DWORD, MFT_INPUT_STREAM_INFO*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputStreamInfo(DWORD, MFT_OUTPUT_STREAM_INFO*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetAttributes(IMFAttributes**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputStreamAttributes(DWORD, IMFAttributes**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputStreamAttributes(DWORD, IMFAttributes**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::DeleteInputStream(DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::AddInputStreams(DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputAvailableType(DWORD id, DWORD index, IMFMediaType** mt) {
  if (!mt) return E_POINTER;
  if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (index > 1) return MF_E_NO_MORE_TYPES;
  UINT32 sr = index == 0 ? 44100 : 48000;
  IMFMediaType* t = nullptr;
  HRESULT hr = MFCreateMediaType(&t);
  if (FAILED(hr)) return hr;
  t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
  t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sr);
  t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
  t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
  t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 2 * 4);
  t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sr * 2 * 4);
  t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  *mt = t;
  return S_OK;
}
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputAvailableType(DWORD id, DWORD index, IMFMediaType** mt) {
  return GetInputAvailableType(id, index, mt);
}
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetInputType(DWORD, IMFMediaType* mt, DWORD flags) {
  if (flags & ~MF_SET_ALL_TYPES) return E_INVALIDARG;
  if (!mt) {
    if (inputType_) inputType_->Release();
    inputType_ = nullptr;
    typesSet_ = false;
    return S_OK;
  }
  if (!ValidateAudioType(mt)) return MF_E_INVALIDMEDIATYPE;
  UINT32 sr = 0, ch = 0;
  mt->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
  mt->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
  std::lock_guard lock(mutex_);
  if (inputType_) inputType_->Release();
  inputType_ = mt;
  inputType_->AddRef();
  sampleRate_ = sr;
  channels_ = ch;
  for (int i = 0; i < 5; ++i) {
    bands_[i].SetParams(sampleRate_, kBandFreqs[i], 0.0, kQ);
  }
  typesSet_ = true;
  return S_OK;
}
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetOutputType(DWORD, IMFMediaType* mt, DWORD flags) {
  if (flags & ~MF_SET_ALL_TYPES) return E_INVALIDARG;
  if (!mt) {
    if (outputType_) outputType_->Release();
    outputType_ = nullptr;
    return S_OK;
  }
  if (!ValidateAudioType(mt)) return MF_E_INVALIDMEDIATYPE;
  std::lock_guard lock(mutex_);
  if (outputType_) outputType_->Release();
  outputType_ = mt;
  outputType_->AddRef();
  return S_OK;
}
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputCurrentType(DWORD id, IMFMediaType** mt) {
  if (!mt) return E_POINTER;
  if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!inputType_) return MF_E_NOT_INITIALIZED;
  *mt = inputType_;
  inputType_->AddRef();
  return S_OK;
}
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputCurrentType(DWORD id, IMFMediaType** mt) {
  if (!mt) return E_POINTER;
  if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!outputType_) return MF_E_NOT_INITIALIZED;
  *mt = outputType_;
  outputType_->AddRef();
  return S_OK;
}
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputStatus(DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputStatus(DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetOutputBounds(LONGLONG, LONGLONG) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessEvent(DWORD, IMFMediaEvent*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessMessage(MFT_MESSAGE_TYPE, ULONG_PTR) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessInput(DWORD, IMFSample*, DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessOutput(DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER*, DWORD*) { return E_NOTIMPL; }

bool EqualizerMFT::ValidateAudioType(IMFMediaType* mt) const {
  if (!mt) return false;
  GUID subtype = {};
  if (FAILED(mt->GetGUID(MF_MT_SUBTYPE, &subtype))) return false;
  if (subtype != MFAudioFormat_Float) return false;
  UINT32 sr = 0;
  if (FAILED(mt->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr))) return false;
  if (sr != 44100 && sr != 48000) return false;
  UINT32 ch = 0;
  if (FAILED(mt->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch))) return false;
  if (ch != 1 && ch != 2) return false;
  return true;
}

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
