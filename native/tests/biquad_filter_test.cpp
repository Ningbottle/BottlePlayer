// BiquadFilter frequency response: chirp input -> process -> FFT -> verify
// magnitude at the target band center matches the requested gain.

#define _USE_MATH_DEFINES
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
