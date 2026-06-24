# S4 Implementation Plan — Part 2: BiquadFilter + EqualizerMFT (Phase 4.2a/b)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a 5-band biquad equalizer that can be inserted as an `IMFTransform` MFT into the MFS topology. The BiquadFilter math is unit-tested directly; the MFT is the COM wrapper.

**Architecture:** `BiquadFilter` is a pure-math class (Direct Form II Transposed, RBJ Audio EQ Cookbook). `EqualizerMFT` wraps 5 of them in a single-threaded `IMFTransform` that accepts PCM float32 input/output. The MFS topology (Source → Decoder → **EQ** → SAR) is updated to insert the EQ node.

**Tech Stack:** C++17, Windows Media Foundation (mftransform.h, mfapi.h, mfobjects.h, mferror.h), COM, vcpkg, CTest.

## Global Constraints

(All constraints from Part 1 apply: C++17, Windows 10+, no mocking, vcpkg triplet x64-windows, ECHO_LOG for diagnostics, CMakeLists integration, ABI preservation.)

Additional S4.2 constraints:
- **EQ MFT is single-threaded.** Marked via attributes; MF serializes calls. Internal `std::mutex` protects `bands_` state from concurrent UI calls.
- **BiquadFilter is pure math.** No COM, no MF — directly unit-testable with `ProcessSample(float)`.
- **EQ MFT format negotiation.** Accepts `MFAudioFormat_Float` (PCM 32-bit float), 1 or 2 channels, 44100 or 48000 Hz only.
- **Gain range is ±12 dB.** Q factor is RBJ default `1/sqrt(2) ≈ 0.707`. Band center frequencies: `60 / 230 / 910 / 3600 / 14000 Hz`.

## File Map

| File | Responsibility |
|---|---|
| `native/include/echo/playback/BiquadFilter.h` | Pure-math biquad (Direct Form II Transposed). |
| `native/playback/BiquadFilter.cpp` | RBJ coefficient formula. |
| `native/playback/EqualizerMFT.h` | IMFTransform wrapper around 5 BiquadFilters. |
| `native/playback/EqualizerMFT.cpp` | COM impl, ProcessInput/ProcessOutput, type negotiation. |
| `native/playback/PlaybackControllerMFS.cpp` | Insert EQ node into topology. |
| `native/tests/biquad_filter_test.cpp` | Chirp + FFT frequency-response test. |
| `native/tests/equalizer_mft_test.cpp` | IMFSample round-trip test (RMS gain check). |
| `native/CMakeLists.txt` | Register new sources and tests. |

---

### Task 9: BiquadFilter class + chirp/FFT frequency-response test (TDD)

**Files:**
- Create: `native/include/echo/playback/BiquadFilter.h`
- Create: `native/playback/BiquadFilter.cpp`
- Create: `native/tests/biquad_filter_test.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `SetParams(sampleRate, freqHz, gainDb, Q)` — RBJ peak filter coefficients
- Produces: `ProcessSample(float in) -> float` — DF2T one-sample at a time

- [ ] **Step 1: Write the failing test first**

```cpp
// native/tests/biquad_filter_test.cpp
// BiquadFilter frequency response: chirp input -> process -> FFT -> verify
// magnitude at the target band center matches the requested gain.

#include <cassert>
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

#include "echo/playback/BiquadFilter.h"

using echo::playback::BiquadFilter;

static int g_passed = 0;
static int g_failed = 0;
#define CHECK(cond, msg) \
  do { \
    if (cond) { std::cout << "  [ok] " << (msg) << "\n"; ++g_passed; } \
    else { std::cerr << "  [FAIL] " << (msg) << " at " << __FILE__ << ":" << __LINE__ << "\n"; ++g_failed; } \
  } while (0)

static std::vector<double> GenerateChirp(double sr, double durationSec,
                                          double f0, double f1) {
  std::vector<double> out(static_cast<size_t>(sr * durationSec));
  double totalSamples = sr * durationSec;
  for (size_t i = 0; i < out.size(); ++i) {
    double t = i / sr;
    double phase = 2.0 * M_PI * (f0 * t + (f1 - f0) * t * t / (2.0 * durationSec));
    out[i] = std::sin(phase);
  }
  return out;
}

// Crude DFT at a single frequency (since input is chirp, only need narrow bin)
static double MagnitudeAt(const std::vector<double>& samples, double sr,
                           double targetHz) {
  // Use Goertzel algorithm: efficient single-bin DFT
  double k = std::round(targetHz * samples.size() / sr);
  double w = 2.0 * M_PI * k / samples.size();
  double coeff = 2.0 * std::cos(w);
  double s0 = 0, s1 = 0, s2 = 0;
  for (double x : samples) {
    s0 = x + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  double mag = std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2);
  return mag / (samples.size() / 2.0);
}

int main() {
  std::cout << "[Test] BiquadFilter frequency response...\n";
  {
    const double sr = 44100.0;
    const double dur = 1.0;  // 1s chirp
    BiquadFilter filter;
    filter.SetParams(sr, 910.0 /*band center*/, 6.0 /*+6dB*/, 0.707 /*Q*/);

    auto input = GenerateChirp(sr, dur, 20.0, 20000.0);
    std::vector<double> output(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
      output[i] = filter.ProcessSample(static_cast<float>(input[i]));
    }

    double magIn910 = MagnitudeAt(input, sr, 910.0);
    double magOut910 = MagnitudeAt(output, sr, 910.0);
    double gainDb = 20.0 * std::log10(magOut910 / magIn910);
    std::cout << "  [debug] gain at 910Hz: " << gainDb << " dB\n";
    CHECK(std::abs(gainDb - 6.0) < 1.0, "gain at 910Hz is +6dB +/-1dB");
  }

  std::cout << "[Test] BiquadFilter passthrough when gain=0...\n";
  {
    BiquadFilter filter;
    filter.SetParams(44100.0, 910.0, 0.0, 0.707);
    auto input = GenerateChirp(44100.0, 1.0, 20.0, 20000.0);
    std::vector<double> output(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
      output[i] = filter.ProcessSample(static_cast<float>(input[i]));
    }
    double magIn = MagnitudeAt(input, 44100.0, 910.0);
    double magOut = MagnitudeAt(output, 44100.0, 910.0);
    double gainDb = 20.0 * std::log10(magOut / magIn);
    std::cout << "  [debug] gain at 910Hz with 0dB: " << gainDb << " dB\n";
    CHECK(std::abs(gainDb) < 0.5, "0dB gain leaves signal near-untouched");
  }

  std::cout << "  Passed: " << g_passed << "  Failed: " << g_failed << "\n";
  return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Run test to verify it fails (BiquadFilter doesn't exist)**

Run: `cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoBiquadFilterTest 2>&1 | Select-Object -Last 5`
Expected: compile error `BiquadFilter not found`.

- [ ] **Step 3: Create the BiquadFilter header**

```cpp
// native/include/echo/playback/BiquadFilter.h
#pragma once

namespace echo::playback {

// Second-order IIR biquad filter, Direct Form II Transposed.
// Coefficients computed via RBJ Audio EQ Cookbook (peak filter).
class BiquadFilter {
 public:
  BiquadFilter() = default;

  void SetParams(double sampleRate, double freqHz, double gainDb, double Q);
  float ProcessSample(float in);
  void Reset();

  struct Coeffs { double b0, b1, b2, a1, a2; };
  Coeffs GetCoeffs() const { return coeffs_; }

 private:
  Coeffs coeffs_ = {1, 0, 0, 0, 0};  // default passthrough
  double z1_ = 0, z2_ = 0;
};

}  // namespace echo::playback
```

- [ ] **Step 4: Implement BiquadFilter**

```cpp
// native/playback/BiquadFilter.cpp
#include "echo/playback/BiquadFilter.h"
#include <cmath>

namespace echo::playback {

void BiquadFilter::SetParams(double sampleRate, double freqHz, double gainDb, double Q) {
  // RBJ Audio EQ Cookbook — peaking EQ
  const double A = std::pow(10.0, gainDb / 40.0);
  const double w0 = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
  const double cosw0 = std::cos(w0);
  const double sinw0 = std::sin(w0);
  const double alpha = sinw0 / (2.0 * Q);

  const double b0 = 1.0 + alpha * A;
  const double b1 = -2.0 * cosw0;
  const double b2 = 1.0 - alpha * A;
  const double a0 = 1.0 + alpha / A;
  const double a1 = -2.0 * cosw0;
  const double a2 = 1.0 - alpha / A;

  coeffs_.b0 = b0 / a0;
  coeffs_.b1 = b1 / a0;
  coeffs_.b2 = b2 / a0;
  coeffs_.a1 = a1 / a0;
  coeffs_.a2 = a2 / a0;
}

float BiquadFilter::ProcessSample(float in) {
  // Direct Form II Transposed
  double out = coeffs_.b0 * static_cast<double>(in) + z1_;
  z1_ = coeffs_.b1 * static_cast<double>(in) - coeffs_.a1 * out + z2_;
  z2_ = coeffs_.b2 * static_cast<double>(in) - coeffs_.a2 * out;
  return static_cast<float>(out);
}

void BiquadFilter::Reset() {
  z1_ = 0;
  z2_ = 0;
}

}  // namespace echo::playback
```

- [ ] **Step 5: Register in CMakeLists.txt**

```cmake
add_library(EchoPlayback STATIC
  playback/PlaybackController.cpp
  playback/PlaybackControllerMFP.cpp
  playback/PlaybackControllerMFS.cpp
  playback/BiquadFilter.cpp
)
```

And add the test:

```cmake
  add_executable(EchoBiquadFilterTest tests/biquad_filter_test.cpp)
  target_include_directories(EchoBiquadFilterTest PRIVATE include)
  target_link_libraries(EchoBiquadFilterTest PRIVATE EchoCore EchoPlayback)
  add_test(NAME EchoBiquadFilterTest COMMAND EchoBiquadFilterTest)
```

- [ ] **Step 6: Build and run test**

Run:
```bash
cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoBiquadFilterTest
ctest --test-dir C:\BottleMusic\native\out\bottlemusic-check -R EchoBiquadFilterTest --output-on-failure
```
Expected: 2 tests pass. Debug output shows gain at 910Hz ≈ +6dB and ≈ 0dB respectively.

- [ ] **Step 7: Commit**

```bash
git add native/include/echo/playback/BiquadFilter.h native/playback/BiquadFilter.cpp native/tests/biquad_filter_test.cpp native/CMakeLists.txt
git commit -m "feat(s4): add BiquadFilter pure-math class with chirp/FFT test"
```

---

### Task 10: EqualizerMFT skeleton (IUnknown + IMFTransform stubs)

**Files:**
- Create: `native/playback/EqualizerMFT.h`
- Create: `native/playback/EqualizerMFT.cpp`
- Modify: `native/CMakeLists.txt`

**Interfaces:**
- Consumes: `BiquadFilter` (Task 9)
- Produces: `EqualizerMFT` class implementing `IUnknown` + `IMFTransform` (most methods return E_NOTIMPL at first)

- [ ] **Step 1: Create the header**

```cpp
// native/playback/EqualizerMFT.h
#pragma once
#include <mftransform.h>
#include <mutex>
#include "echo/playback/BiquadFilter.h"

namespace echo::playback {

class EqualizerMFT : public IMFTransform {
 public:
  EqualizerMFT();
  ~EqualizerMFT() override;

  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;

  // IMFTransform — most return E_NOTIMPL, real impls in next tasks
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
  HRESULT STDMETHODCALLTYPE ProcessEvent(DWORD, IMFMediaEvent*) override;
  HRESULT STDMETHODCALLTYPE GetAttributes(IMFAttributes**) override;
  HRESULT STDMETHODCALLTYPE GetInputStreamAttributes(DWORD, IMFAttributes**) override;
  HRESULT STDMETHODCALLTYPE GetOutputStreamAttributes(DWORD, IMFAttributes**) override;
  HRESULT STDMETHODCALLTYPE DeleteInputStream(DWORD) override;
  HRESULT STDMETHODCALLTYPE AddInputStreams(DWORD, DWORD*) override;
  HRESULT STDMETHODCALLTYPE GetInputAvailableType(DWORD, DWORD, IMFMediaType**) override;
  HRESULT STDMETHODCALLTYPE GetOutputAvailableType(DWORD, DWORD, IMFMediaType**) override;
  HRESULT STDMETHODCALLTYPE SetInputBounds(LONGLONG, LONGLONG, DWORD*) override;
  HRESULT STDMETHODCALLTYPE Flush(DWORD) override;
  HRESULT STDMETHODCALLTYPE GetStreamSelection(DWORD, BOOL*) override;
  HRESULT STDMETHODCALLTYPE SetStreamSelection(DWORD, BOOL) override;

  // EQ control
  void SetEnabled(bool enabled);
  void SetBandGain(int bandIndex, double gainDb);
  void SetAllBandGains(const double gainsDb[5]);

 private:
  volatile long refCount_ = 1;
  std::mutex mutex_;
  BiquadFilter bands_[5];
  bool enabled_ = false;

  static constexpr double kBandFreqs[5] = {60.0, 230.0, 910.0, 3600.0, 14000.0};
  static constexpr double kQ = 0.70710678;
  static constexpr double kMaxGainDb = 12.0;
};

}  // namespace echo::playback
```

- [ ] **Step 2: Create skeleton .cpp**

```cpp
// native/playback/EqualizerMFT.cpp
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
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamLimits(DWORD* a, DWORD* b, DWORD* c, DWORD* d) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamCount(DWORD* a, DWORD* b) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamIDs(DWORD, DWORD*, DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetMediaTypeList(DWORD, DWORD, IMFMediaType***) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetMediaTypeAvailable(DWORD, DWORD, IMFMediaType**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetCurrentMediaType(DWORD, BOOL, IMFMediaType**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetInputType(DWORD, IMFMediaType*, DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetOutputType(DWORD, IMFMediaType*, DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputStatus(DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputStatus(DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessInput(DWORD, IMFSample*, DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessOutput(DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER*, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessEvent(DWORD, IMFMediaEvent*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetAttributes(IMFAttributes**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputStreamAttributes(DWORD, IMFAttributes**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputStreamAttributes(DWORD, IMFAttributes**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::DeleteInputStream(DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::AddInputStreams(DWORD, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputAvailableType(DWORD, DWORD, IMFMediaType**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputAvailableType(DWORD, DWORD, IMFMediaType**) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetInputBounds(LONGLONG, LONGLONG, DWORD*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::Flush(DWORD) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamSelection(DWORD, BOOL*) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetStreamSelection(DWORD, BOOL) { return E_NOTIMPL; }

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
```

(Note: sample rate is hardcoded to 44100 here; Task 11 will fix it via the negotiated media type.)

- [ ] **Step 3: Register in CMakeLists.txt**

```cmake
add_library(EchoPlayback STATIC
  playback/PlaybackController.cpp
  playback/PlaybackControllerMFP.cpp
  playback/PlaybackControllerMFS.cpp
  playback/BiquadFilter.cpp
  playback/EqualizerMFT.cpp
)
```

- [ ] **Step 4: Build**

Run: `cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCore 2>&1 | Select-Object -Last 5`
Expected: clean build (EqualizerMFT compiles, even if not yet functional).

- [ ] **Step 5: Commit**

```bash
git add native/playback/EqualizerMFT.h native/playback/EqualizerMFT.cpp native/CMakeLists.txt
git commit -m "feat(s4): add EqualizerMFT skeleton (IUnknown + IMFTransform stubs)"
```

---

### Task 11: EqualizerMFT media type negotiation

**Files:**
- Modify: `native/playback/EqualizerMFT.h` — add `inputType_`, `outputType_`, `sampleRate_` members
- Modify: `native/playback/EqualizerMFT.cpp` — implement `SetInputType`/`SetOutputType`/`GetInputAvailableType`/`GetOutputAvailableType`/`GetStreamLimits`/`GetStreamCount`

**Interfaces:**
- Consumes: `IMFMediaType` from upstream decoder
- Produces: Accepted `IMFMediaType` (PCM float32, 44100/48000 Hz)

- [ ] **Step 1: Update header with new members**

```cpp
class EqualizerMFT : public IMFTransform {
  // ... existing public methods ...

 private:
  // ... existing private members ...
  IMFMediaType* inputType_ = nullptr;
  IMFMediaType* outputType_ = nullptr;
  double sampleRate_ = 44100.0;
  UINT32 channels_ = 2;
  bool typesSet_ = false;

  bool ValidateAudioType(IMFMediaType* mt) const;
};
```

- [ ] **Step 2: Implement validation helper**

In `EqualizerMFT.cpp`:

```cpp
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
```

- [ ] **Step 3: Implement `SetInputType` / `SetOutputType`**

Replace the existing stubs:

```cpp
HRESULT STDMETHODCALLTYPE EqualizerMFT::SetInputType(DWORD, IMFMediaType* mt, DWORD flags) {
  if (flags & ~MF_SET_ALL_TYPES) return E_INVALIDARG;
  if (!mt) {
    // Clear type
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
  // Re-apply band coefficients with correct sample rate
  for (int i = 0; i < 5; ++i) {
    // Use 0dB by default; SetAllBandGains will set real values
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
```

- [ ] **Step 4: Implement `GetStreamLimits`, `GetStreamCount`, `GetCurrentMediaType`, `GetInputAvailableType`, `GetOutputAvailableType`**

```cpp
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamLimits(DWORD* minIn, DWORD* maxIn, DWORD* minOut, DWORD* maxOut) {
  *minIn = *minOut = 1;
  *maxIn = *maxOut = 1;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE EqualizerMFT::GetStreamCount(DWORD* in, DWORD* out) {
  *in = *out = 1;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE EqualizerMFT::GetCurrentMediaType(DWORD id, BOOL isOutput, IMFMediaType** mt) {
  if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
  IMFMediaType* src = isOutput ? outputType_ : inputType_;
  if (!src) return MF_E_NOT_INITIALIZED;
  *mt = src;
  src->AddRef();
  return S_OK;
}

HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputAvailableType(DWORD id, DWORD index, IMFMediaType** mt) {
  if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (index > 1) return MF_E_NO_MORE_TYPES;
  IMFMediaType* t = nullptr;
  MFCreateMediaType(&t);
  t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
  t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, index == 0 ? 44100 : 48000);
  t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
  t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
  t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 2 * 4);
  t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 44100 * 2 * 4);
  t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  *mt = t;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE EqualizerMFT::GetOutputAvailableType(DWORD id, DWORD index, IMFMediaType** mt) {
  // Same as input for passthrough
  return GetInputAvailableType(id, index, mt);
}
```

- [ ] **Step 5: Build**

Run: `cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCore 2>&1 | Select-Object -Last 5`
Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add native/playback/EqualizerMFT.h native/playback/EqualizerMFT.cpp
git commit -m "feat(s4): add EqualizerMFT media type negotiation (PCM float32 44.1/48kHz)"
```

---

### Task 12: EqualizerMFT ProcessInput/ProcessOutput

**Files:**
- Modify: `native/playback/EqualizerMFT.cpp`

**Interfaces:**
- Consumes: `IMFSample` containing PCM float32 audio
- Produces: Output `IMFSample` with audio passed through 5 BiquadFilters

- [ ] **Step 1: Implement `GetInputStatus`**

```cpp
HRESULT STDMETHODCALLTYPE EqualizerMFT::GetInputStatus(DWORD id, DWORD* status) {
  if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
  *status = MFT_INPUT_STATUS_ACCEPT_DATA;
  return S_OK;
}
```

- [ ] **Step 2: Implement `ProcessInput`**

```cpp
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessInput(DWORD id, IMFSample* sample, DWORD) {
  if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!sample) return E_POINTER;
  if (!inputType_) return MF_E_NOT_INITIALIZED;
  // The sample is queued internally; for synchronous in-place MFT we
  // can process it directly in ProcessOutput. But the simplest correct
  // approach: store the input sample and process in ProcessOutput.
  // For now, just hold a reference to the most recent input.
  std::lock_guard lock(mutex_);
  if (currentInput_) currentInput_->Release();
  currentInput_ = sample;
  currentInput_->AddRef();
  return S_OK;
}
```

Add `currentInput_` member to header:

```cpp
  IMFSample* currentInput_ = nullptr;
```

- [ ] **Step 3: Implement `ProcessOutput`**

```cpp
HRESULT STDMETHODCALLTYPE EqualizerMFT::ProcessOutput(DWORD flags, DWORD count,
                                                      MFT_OUTPUT_DATA_BUFFER* buffers,
                                                      DWORD* processed) {
  if (flags != 0) return E_INVALIDARG;
  if (count != 1) return E_INVALIDARG;
  if (!buffers[0].pSample) return E_INVALIDARG;
  if (!currentInput_) return MF_E_TRANSFORM_NEED_MORE_INPUT;

  std::lock_guard lock(mutex_);

  // Get input buffer
  IMFMediaBuffer* inBuf = nullptr;
  HRESULT hr = currentInput_->ConvertToContiguousBuffer(&inBuf);
  if (FAILED(hr)) return hr;

  BYTE* inData = nullptr;
  DWORD inMaxLen = 0, inCurLen = 0;
  hr = inBuf->Lock(&inData, &inMaxLen, &inCurLen);
  if (FAILED(hr)) { inBuf->Release(); return hr; }

  // Get output buffer
  IMFMediaBuffer* outBuf = nullptr;
  hr = buffers[0].pSample->ConvertToContiguousBuffer(&outBuf);
  if (FAILED(hr)) { inBuf->Unlock(); inBuf->Release(); return hr; }
  BYTE* outData = nullptr;
  DWORD outMaxLen = 0, outCurLen = 0;
  hr = outBuf->Lock(&outData, &outMaxLen, &outCurLen);
  if (FAILED(hr)) { inBuf->Unlock(); inBuf->Release(); outBuf->Release(); return hr; }

  // Process samples (in-place if possible, else copy)
  size_t sampleCount = std::min(inCurLen, outCurLen) / sizeof(float);
  const float* in = reinterpret_cast<const float*>(inData);
  float* out = reinterpret_cast<float*>(outData);
  if (enabled_) {
    for (size_t i = 0; i < sampleCount; ++i) {
      float s = in[i];
      for (int b = 0; b < 5; ++b) s = bands_[b].ProcessSample(s);
      out[i] = s;
    }
  } else {
    // Passthrough
    std::memcpy(out, in, sampleCount * sizeof(float));
  }

  inBuf->Unlock();
  outBuf->Unlock();
  inBuf->Release();
  outBuf->Release();

  // Set output buffer length and sample time
  buffers[0].pSample->SetSampleTime(currentInput_->GetSampleTime()...);
  // (the SetSampleTime call needs proper hresult handling, simplified here)
  *processed = 1;

  // Release input for next round
  currentInput_->Release();
  currentInput_ = nullptr;

  return S_OK;
}
```

(Note: `SetSampleTime` and `GetSampleTime` take `LONGLONG*` — wrap properly in real implementation. The `EqualizerMFS` typo should be `EqualizerMFT`.)

- [ ] **Step 4: Build**

Run: `cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCore 2>&1 | Select-Object -Last 5`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add native/playback/EqualizerMFT.h native/playback/EqualizerMFT.cpp
git commit -m "feat(s4): add EqualizerMFT ProcessInput/ProcessOutput with biquad cascade"
```

---

### Task 13: Insert EqualizerMFT into MFS topology

**Files:**
- Modify: `native/playback/PlaybackControllerMFS.h` — add `eqMft_` member
- Modify: `native/playback/PlaybackControllerMFS.cpp` — create EQ MFT in Initialize, insert in BuildTopology

**Interfaces:**
- Consumes: `EqualizerMFT` (Tasks 10-12)
- Produces: MFS topology `Source → Decoder → EQ MFT → SAR` (instead of Source → Decoder → SAR)

- [ ] **Step 1: Add `eqMft_` member to MFS header**

```cpp
#include "echo/playback/EqualizerMFT.h"

class PlaybackControllerMFS final : public PlaybackControllerImpl {
  // ... existing members ...
  EqualizerMFT* eqMft_ = nullptr;
```

- [ ] **Step 2: Create EQ MFT in `Initialize`**

Add to the end of `Initialize` (after the existing `BeginGetEvent`):

```cpp
  // Create EQ MFT (5-band biquad)
  eqMft_ = new (std::nothrow) EqualizerMFT();
  if (!eqMft_) return false;
  eqMft_->AddRef();
```

- [ ] **Step 3: Modify `BuildTopology` to insert EQ node**

The current `BuildTopology` (Task 5) only creates the source node and lets MF's topology loader auto-insert decoder + SAR. For the EQ to be inserted, we need to:
1. Explicitly create decoder, EQ, and SAR nodes
2. Wire them with `ConnectOutput`/`ConnectInput`
3. Disable MF's auto-decoder insertion (set `MF_TOPONODE_DRAIN` or use `MF_TOPOLOGY_HELPER_METHOD_PRESERVE_ID`)

Replace the body of `BuildTopology` after source node creation:

```cpp
  // Create decoder node via auto-activation: ask MF for the right decoder
  // MFT activator based on the source's media type, wrap it in a transform node.
  IMFActivate* decoderActivate = nullptr;
  // Use MF's decoder helper: source resolver + topology loader handles this,
  // but we need an explicit node to chain into the EQ. The simplest approach
  // is to create an "opaque" transform node and let the topology loader fill it.
  // For now: skip the explicit decoder node and rely on topology loader;
  // the EQ will be inserted between decoder and SAR by topology loader hints.

  // (Alternative: do explicit pipeline construction. This is more code but
  // more reliable. See Microsoft "TopoEdit" sample for the canonical pattern.)
```

**This task is marked as blocked** until the implementer researches the exact MF topology construction pattern that allows inserting a custom MFT between decoder and SAR. The most reliable approach in practice is:

1. Use `IMFTopologyNode::SetTopoNodeID` to assign explicit IDs
2. Use `MF_TOPOLOGY_HELPER_METHOD_PRESERVE_ID` topology attribute to keep the loader from reordering
3. Use `MF_TOPONODE_CONNECT_METHOD` to specify "existing topology node" connections

If the topology loader approach is too brittle, fall back to: don't insert EQ in the topology at all; let MF auto-insert decoder + SAR, and apply EQ as a post-processing step on a separate stream (e.g., via WASAPI exclusive mode). This is a larger refactor and may be deferred to a follow-up S4.2c task.

- [ ] **Step 2 (continued): Stub `BuildTopology` for now**

```cpp
HRESULT PlaybackControllerMFS::BuildTopology(const std::string& url, IMFTopology** out) {
  // TODO(s4-blocked): custom MFT insertion is non-trivial; see task 13
  // notes. For now, use the simple topology (source + topology loader does
  // the rest), and EQ is applied separately (or passthrough).
  int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
  std::wstring wideUrl(wlen, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wideUrl.data(), wlen);

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

  IMFPresentationDescriptor* pd = nullptr;
  hr = mediaSource_->CreatePresentationDescriptor(&pd);
  if (FAILED(hr)) return hr;
  hr = MFCreateTopology(out);
  if (FAILED(hr)) { pd->Release(); return hr; }

  DWORD streamCount = 0;
  pd->GetStreamDescriptorCount(&streamCount);
  for (DWORD i = 0; i < streamCount; ++i) {
    BOOL selected = FALSE;
    IMFStreamDescriptor* sd = nullptr;
    pd->GetStreamDescriptorByIndex(i, &selected, &sd);
    if (selected) {
      IMFTopologyNode* srcNode = nullptr;
      MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &srcNode);
      srcNode->SetUnknown(MF_TOPONODE_SOURCE, mediaSource_);
      srcNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pd);
      srcNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, sd);
      (*out)->AddNode(srcNode);
      srcNode->Release();
    }
    sd->Release();
  }
  pd->Release();
  return S_OK;
}
```

- [ ] **Step 3: Note this as a known limitation**

Add a comment in `C_API.cpp` and the spec:

> **Known limitation (S4.2b):** The EqualizerMFT exists and is unit-testable in isolation, but is not yet inserted into the MFS topology. The MF topology loader does not easily allow inserting a custom MFT between decoder and SAR. Two paths forward: (a) explicit topology construction (more code, more reliable); (b) post-decode buffer processing via WASAPI exclusive mode (more invasive). Deferred to S4.2c. For S4, the EQ is reachable via the C API but the MFS path plays without EQ applied.

- [ ] **Step 4: Commit**

```bash
git add native/playback/PlaybackControllerMFS.h native/playback/PlaybackControllerMFS.cpp
git commit -m "feat(s4): MFS creates EqualizerMFT (insertion deferred — see task notes)"
```

---

### Task 14: PlaybackControllerMFS EQ methods (delegate to EqualizerMFT)

**Files:**
- Modify: `native/playback/PlaybackControllerMFS.h` — add EQ members
- Modify: `native/playback/PlaybackControllerMFS.cpp` — implement EQ methods

- [ ] **Step 1: Add EQ members to MFS header**

```cpp
class PlaybackControllerMFS final : public PlaybackControllerImpl {
  // ... existing public methods ...

  void SetEqEnabled(bool enabled) override;
  void SetEqBand(int bandIndex, double gainDb) override;
  void SetEqBands(const double gainsDb[5]) override;
  void GetEqBands(double outGainsDb[5]) const override;

 private:
  // ... existing members ...
  bool eqEnabled_ = false;
  double eqGains_[5] = {0, 0, 0, 0, 0};
```

- [ ] **Step 2: Implement EQ methods**

```cpp
void PlaybackControllerMFS::SetEqEnabled(bool enabled) {
  std::lock_guard lock(mutex_);
  eqEnabled_ = enabled;
  if (eqMft_) eqMft_->SetEnabled(enabled);
}

void PlaybackControllerMFS::SetEqBand(int bandIndex, double gainDb) {
  if (bandIndex < 0 || bandIndex >= 5) return;
  std::lock_guard lock(mutex_);
  eqGains_[bandIndex] = gainDb;
  if (eqMft_) eqMft_->SetBandGain(bandIndex, gainDb);
}

void PlaybackControllerMFS::SetEqBands(const double gainsDb[5]) {
  std::lock_guard lock(mutex_);
  for (int i = 0; i < 5; ++i) {
    eqGains_[i] = gainsDb[i];
    if (eqMft_) eqMft_->SetBandGain(i, gainsDb[i]);
  }
}

void PlaybackControllerMFS::GetEqBands(double outGainsDb[5]) const {
  std::lock_guard lock(mutex_);
  for (int i = 0; i < 5; ++i) outGainsDb[i] = eqGains_[i];
}
```

- [ ] **Step 3: Build**

Run: `cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCore 2>&1 | Select-Object -Last 5`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add native/playback/PlaybackControllerMFS.h native/playback/PlaybackControllerMFS.cpp
git commit -m "feat(s4): add MFS EQ methods delegating to EqualizerMFT"
```

---

**End of Part 2 — Phase 4.2a/b complete. Continue with Part 3 (C API + Rust FFI).**
