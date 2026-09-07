#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "gainpilot/parameters.hpp"
#include "gainpilot/dsp/k_weighting_filter.hpp"

namespace gainpilot::dsp {

// All storage is allocated in prepare(). Readouts are cached on the 100 ms hop.
class LoudnessMeter {
public:
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
  struct EnergyBin {
    double sum{};
    std::uint64_t count{};
  };
  // 0.01 LU bins from -70 to +100 LUFS; exact sums within each bin.
  // Only the relative-gate boundary is quantized, never the stored energies.
  static constexpr std::size_t kHistogramBins = 17001;
  [[nodiscard]] static float loudnessFromEnergy(double meanEnergy);
  void pushWindowSample(std::vector<double>& window, std::size_t& index, double sample, double& runningSum);
  void updateIntegratedState();

  std::size_t channelCount_{2};
  std::size_t momentarySamples_{1};
  std::size_t shortTermSamples_{1};
  std::size_t hopSamples_{1};
  std::size_t sampleCounter_{0};
  std::size_t integratedSampleCounter_{0};
  std::size_t integratedBlockCount_{0};
  std::size_t momentaryIndex_{0};
  std::size_t shortTermIndex_{0};
  double momentaryEnergySum_{0.0};
  double shortTermEnergySum_{0.0};
  double absoluteEnergySum_{0.0};
  std::uint64_t absoluteBlockCount_{0};
  float momentaryLufs_{-70.0f};
  float shortTermLufs_{-70.0f};
  float integratedLufs_{-70.0f};
  float controlLufs_{-70.0f};
  std::vector<double> momentaryWindow_{};
  std::vector<double> shortTermWindow_{};
  std::vector<EnergyBin> integratedHistogram_{};
  KWeightingFilter weightingFilter_{};
};

}  // namespace gainpilot::dsp
