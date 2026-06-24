#pragma once
#include <mftransform.h>
#include <mfobjects.h>
#include <mutex>
#include "echo/playback/BiquadFilter.h"

namespace echo::playback {

class EqualizerMFT : public IMFTransform {
 public:
  EqualizerMFT();
  ~EqualizerMFT();

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;

  // IMFTransform — most return E_NOTIMPL, real impls in later tasks
  HRESULT STDMETHODCALLTYPE GetStreamLimits(DWORD* pdwInputMinimum, DWORD* pdwInputMaximum,
                                              DWORD* pdwOutputMinimum, DWORD* pdwOutputMaximum) override;
  HRESULT STDMETHODCALLTYPE GetStreamCount(DWORD* pcInputStreams, DWORD* pcOutputStreams) override;
  HRESULT STDMETHODCALLTYPE GetStreamIDs(DWORD dwInputIDArraySize, DWORD* pdwInputIDs,
                                          DWORD dwOutputIDArraySize, DWORD* pdwOutputIDs) override;
  HRESULT STDMETHODCALLTYPE GetInputStreamInfo(DWORD dwInputStreamID, MFT_INPUT_STREAM_INFO* pStreamInfo) override;
  HRESULT STDMETHODCALLTYPE GetOutputStreamInfo(DWORD dwOutputStreamID, MFT_OUTPUT_STREAM_INFO* pStreamInfo) override;
  HRESULT STDMETHODCALLTYPE GetAttributes(IMFAttributes** pAttributes) override;
  HRESULT STDMETHODCALLTYPE GetInputStreamAttributes(DWORD dwInputStreamID, IMFAttributes** pAttributes) override;
  HRESULT STDMETHODCALLTYPE GetOutputStreamAttributes(DWORD dwOutputStreamID, IMFAttributes** pAttributes) override;
  HRESULT STDMETHODCALLTYPE DeleteInputStream(DWORD dwStreamID) override;
  HRESULT STDMETHODCALLTYPE AddInputStreams(DWORD cStreams, DWORD* adwStreamIDs) override;
  HRESULT STDMETHODCALLTYPE GetInputAvailableType(DWORD dwInputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType) override;
  HRESULT STDMETHODCALLTYPE GetOutputAvailableType(DWORD dwOutputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType) override;
  HRESULT STDMETHODCALLTYPE SetInputType(DWORD dwInputStreamID, IMFMediaType* pType, DWORD dwFlags) override;
  HRESULT STDMETHODCALLTYPE SetOutputType(DWORD dwOutputStreamID, IMFMediaType* pType, DWORD dwFlags) override;
  HRESULT STDMETHODCALLTYPE GetInputCurrentType(DWORD dwInputStreamID, IMFMediaType** ppType) override;
  HRESULT STDMETHODCALLTYPE GetOutputCurrentType(DWORD dwOutputStreamID, IMFMediaType** ppType) override;
  HRESULT STDMETHODCALLTYPE GetInputStatus(DWORD dwInputStreamID, DWORD* pdwFlags) override;
  HRESULT STDMETHODCALLTYPE GetOutputStatus(DWORD* pdwFlags) override;
  HRESULT STDMETHODCALLTYPE SetOutputBounds(LONGLONG hnsLowerBound, LONGLONG hnsUpperBound) override;
  HRESULT STDMETHODCALLTYPE ProcessEvent(DWORD dwInputStreamID, IMFMediaEvent* pEvent) override;
  HRESULT STDMETHODCALLTYPE ProcessMessage(MFT_MESSAGE_TYPE eMessage, ULONG_PTR ulParam) override;
  HRESULT STDMETHODCALLTYPE ProcessInput(DWORD dwInputStreamID, IMFSample* pSample, DWORD dwFlags) override;
  HRESULT STDMETHODCALLTYPE ProcessOutput(DWORD dwFlags, DWORD cOutputBufferCount,
                                           MFT_OUTPUT_DATA_BUFFER* pOutputSamples, DWORD* pdwStatus) override;

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
  double sampleRate_ = 44100.0;
  UINT32 channels_ = 2;
  bool typesSet_ = false;
  IMFSample* currentInput_ = nullptr;

  bool ValidateAudioType(IMFMediaType* mt) const;

  static constexpr double kBandFreqs[5] = {60.0, 230.0, 910.0, 3600.0, 14000.0};
  static constexpr double kQ = 0.70710678;
  static constexpr double kMaxGainDb = 12.0;
};

}  // namespace echo::playback
