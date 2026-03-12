#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gainpilot/dsp/loudness_meter.hpp"
#include "gainpilot/dsp/true_peak_limiter.hpp"
#include "gainpilot/parameters.hpp"

namespace gainpilot::dsp {

struct ProcessBuffer {
  const float* const* inputs{};
  float** outputs{};
  std::size_t channels{};
  std::size_t frames{};
};

class GainPilotProcessor {
public:
  void prepare(double sampleRate, std::size_t channelCount, std::size_t maxBlockSize);
  void reset();
  void setParameters(const ParameterState& state);
  void setOfflineMode(bool offlineMode);
  [[nodiscard]] std::size_t latencySamples() const;
  void process(const ProcessBuffer& buffer);
  [[nodiscard]] float currentMeterValue() const;
  [[nodiscard]] float currentLatencySamples() const;
  [[nodiscard]] float currentAppliedGainDb() const;
  [[nodiscard]] float currentInputShortTermLufs() const;
  [[nodiscard]] float currentInputIntegratedLufs() const;
  [[nodiscard]] float currentOutputIntegratedLufs() const;
  [[nodiscard]] float currentOutputShortTermLufs() const;

private:
  [[nodiscard]] float computeDesiredGainDb(float detectorLufs, float currentGainDb) const;
  [[nodiscard]] float correctionMix(bool useHighBranch) const;
  [[nodiscard]] float fixedGainDb() const;
  [[nodiscard]] float autoHoldThresholdLufs() const;
  void updateMeterResetLatch();
  void updateAutoHoldGate(float detectorLufs);

  double sampleRate_{48000.0};
  std::size_t channelCount_{2};
  ParameterState parameters_{};
  LoudnessMeter meter_{};
  LoudnessMeter outputMeter_{};
  TruePeakLimiter limiter_{};
  float fastGainDb_{0.0f};
  float mediumGainDb_{0.0f};
  float slowGainDb_{0.0f};
  float fastTargetGainDb_{0.0f};
  float mediumTargetGainDb_{0.0f};
  float slowTargetGainDb_{0.0f};
  float fastAttackCoeff_{0.0f};
  float fastReleaseCoeff_{0.0f};
  float mediumAttackCoeff_{0.0f};
  float mediumReleaseCoeff_{0.0f};
  float slowAttackCoeff_{0.0f};
  float slowReleaseCoeff_{0.0f};
  float currentMeterValue_{-70.0f};
  float currentLatencySamples_{0.0f};
  bool offlineMode_{false};
  bool resetWasHigh_{false};
  bool autoHoldGateOpen_{false};
  std::uint32_t autoHoldHops_{0};
  std::uint32_t autoHoldHopsRemaining_{0};
  std::vector<float> frameInput_{};
  std::vector<float> frameOutput_{};
};

}  // namespace gainpilot::dsp
