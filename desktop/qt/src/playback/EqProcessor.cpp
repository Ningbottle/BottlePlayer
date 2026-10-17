#include "EqProcessor.h"
#include <cmath>
#include <algorithm>
#include <numbers>

void EqProcessor::configure(int rate, int channels) {
    rate_ = std::clamp(rate, 8000, 192000); channels_ = std::clamp(channels, 1, 8);
    states_.assign(size_t(channels_), {}); setGains(gains_);
}
void EqProcessor::setGains(const std::array<double, 10>& values) {
    for (size_t band = 0; band < values.size(); ++band) {
        gains_[band] = std::isfinite(values[band]) ? std::clamp(values[band], -6.0, 6.0) : 0;
        // Same RBJ peaking filters and Q as the existing AudioWorklet.
        const double frequency = std::clamp(frequencies[band], 20.0, rate_ * .5 * .95);
        const double omega = 2 * std::numbers::pi * frequency / rate_;
        const double amplitude = std::pow(10.0, gains_[band] / 40);
        const double alpha = std::sin(omega) / (2 / std::sqrt(2.0));
        const double denominator = 1 + alpha / amplitude;
        coefficients_[band] = {(1 + alpha * amplitude) / denominator, -2 * std::cos(omega) / denominator,
            (1 - alpha * amplitude) / denominator, -2 * std::cos(omega) / denominator, (1 - alpha / amplitude) / denominator};
    }
}
void EqProcessor::reset() { for (auto& channels : states_) for (auto& state : channels) state = {}; }
void EqProcessor::process(float* samples, int frames) {
    if (!samples || frames <= 0 || states_.empty()) return;
    const bool flat = std::all_of(gains_.begin(), gains_.end(), [](double gain) { return gain == 0; });
    if (flat) { for (int i = 0; i < frames * channels_; ++i) if (!std::isfinite(samples[i])) samples[i] = 0; return; }
    for (int frame = 0; frame < frames; ++frame) for (int channel = 0; channel < channels_; ++channel) {
        double value = samples[frame * channels_ + channel];
        if (!std::isfinite(value)) value = 0;
        for (size_t band = 0; band < coefficients_.size(); ++band) {
            const auto& c = coefficients_[band]; auto& state = states_[size_t(channel)][band];
            const double filtered = c.b0 * value + c.b1 * state.x1 + c.b2 * state.x2 - c.a1 * state.y1 - c.a2 * state.y2;
            state.x2 = state.x1; state.x1 = value; state.y2 = state.y1; state.y1 = filtered;
            value = filtered;
        }
        samples[frame * channels_ + channel] = float(std::clamp(value, -1.0, 1.0));
    }
}
