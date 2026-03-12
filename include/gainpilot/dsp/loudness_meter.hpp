#pragma once

#include <cstddef>
#include <vector>

#include "gainpilot/parameters.hpp"

#if GAINPILOT_USE_LIBEBUR128
#include <ebur128.h>
#else
#include "gainpilot/dsp/k_weighting_filter.hpp"
#endif

namespace gainpilot::dsp {

class LoudnessMeter {
public:
  ~LoudnessMeter();
  void prepare(double sampleRate, std::size_t channelCount);
  void reset();
  void resetIntegrated();
  [[nodiscard]] bool processFrame(const float* frame);

  [[nodiscard]] float momentaryLufs() const;
  [[nodiscard]] float shortTermLufs() const;
  [[nodiscard]] float integratedLufs() const;
  [[nodiscard]] std::size_t integratedBlockCount() const;
  [[nodiscard]] bool momentaryReady() const;
  [[nodiscard]] bool shortTermReady() const;
  [[nodiscard]] float controlLufs() const;
  [[nodiscard]] float loudnessForMode(MeterMode mode) const;

private:
  [[nodiscard]] float loudnessFromEnergy(double meanEnergy) const;
  void pushWindowSample(std::vector<double>& window, std::size_t& index, double sample, double& runningSum);
  void updateIntegratedState();

  double sampleRate_{48000.0};
  std::size_t channelCount_{2};
  std::size_t momentarySamples_{1};
  std::size_t shortTermSamples_{1};
  std::size_t hopSamples_{1};
  std::size_t sampleCounter_{0};
  std::size_t momentaryIndex_{0};
  std::size_t shortTermIndex_{0};
  double momentaryEnergySum_{0.0};
  double shortTermEnergySum_{0.0};
  float controlLufs_{-70.0f};
  std::vector<double> momentaryWindow_{};
  std::vector<double> shortTermWindow_{};
  std::vector<double> integratedBlocks_{};
#if GAINPILOT_USE_LIBEBUR128
  ::ebur128_state* shortTermState_{};
  ::ebur128_state* integratedState_{};
  std::vector<float> interleavedFrame_{};
  std::size_t integratedSampleCounter_{0};
  std::size_t integratedBlockCount_{0};
#else
  KWeightingFilter weightingFilter_{};
#endif
};

}  // namespace gainpilot::dsp
