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
