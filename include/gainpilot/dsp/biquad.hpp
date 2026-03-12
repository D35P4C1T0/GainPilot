#pragma once

#include <cmath>

namespace gainpilot::dsp {

struct BiquadCoefficients {
  double b0{};
  double b1{};
  double b2{};
  double a1{};
  double a2{};
};

class Biquad {
public:
  void setCoefficients(BiquadCoefficients coeffs) { coeffs_ = coeffs; }

  void reset() {
    z1_ = 0.0;
    z2_ = 0.0;
  }

  [[nodiscard]] float process(float sample) {
    const double x = sample;
    const double y = coeffs_.b0 * x + z1_;
    z1_ = coeffs_.b1 * x - coeffs_.a1 * y + z2_;
    z2_ = coeffs_.b2 * x - coeffs_.a2 * y;
    return static_cast<float>(y);
  }

private:
  BiquadCoefficients coeffs_{};
  double z1_{};
  double z2_{};
};

[[nodiscard]] inline BiquadCoefficients makeHighPass(double sampleRate, double cutoffHz, double q) {
  const double w0 = 2.0 * M_PI * cutoffHz / sampleRate;
  const double cosw0 = std::cos(w0);
  const double sinw0 = std::sin(w0);
  const double alpha = sinw0 / (2.0 * q);

  const double b0 = (1.0 + cosw0) / 2.0;
  const double b1 = -(1.0 + cosw0);
  const double b2 = (1.0 + cosw0) / 2.0;
  const double a0 = 1.0 + alpha;
  const double a1 = -2.0 * cosw0;
  const double a2 = 1.0 - alpha;

  return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

[[nodiscard]] inline BiquadCoefficients makeHighShelf(double sampleRate, double cutoffHz, double gainDb, double slope) {
  const double a = std::pow(10.0, gainDb / 40.0);
  const double w0 = 2.0 * M_PI * cutoffHz / sampleRate;
  const double cosw0 = std::cos(w0);
  const double sinw0 = std::sin(w0);
  const double alpha = sinw0 / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
  const double beta = 2.0 * std::sqrt(a) * alpha;

  const double b0 = a * ((a + 1.0) + (a - 1.0) * cosw0 + beta);
  const double b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw0);
  const double b2 = a * ((a + 1.0) + (a - 1.0) * cosw0 - beta);
  const double a0 = (a + 1.0) - (a - 1.0) * cosw0 + beta;
  const double a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosw0);
  const double a2 = (a + 1.0) - (a - 1.0) * cosw0 - beta;

  return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

}  // namespace gainpilot::dsp
