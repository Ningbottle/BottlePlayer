#pragma once
#include <array>
#include <vector>

class EqProcessor {
public:
    static constexpr std::array<double, 10> frequencies{31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    void configure(int sampleRate, int channels);
    void setGains(const std::array<double, 10>& gains);
    void process(float* samples, int frames);
    void reset();
private:
    struct Coeff { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    std::array<double, 10> gains_{};
    std::array<Coeff, 10> coefficients_{};
    std::vector<std::array<State, 10>> states_;
    int rate_ = 48000, channels_ = 2;
};
