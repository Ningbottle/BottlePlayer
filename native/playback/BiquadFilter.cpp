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
