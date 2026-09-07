#pragma once

#include <array>
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
  void requestMeterReset() { resetPending_ = true; }
  void setParameters(const ParameterState& state);
  void setOfflineMode(bool offlineMode);
  [[nodiscard]] std::size_t latencySamples() const;
  void process(const ProcessBuffer& buffer);
  [[nodiscard]] float currentMeterValue() const;
  [[nodiscard]] float currentLatencySamples() const;
  [[nodiscard]] float currentAppliedGainDb() const;
  [[nodiscard]] float currentInputShortTermLufs() const;
  [[nodiscard]] float currentInputIntegratedLufs() const;
  [[nodiscard]] float currentInputReferenceLufs() const;
  [[nodiscard]] float meterResetCount() const { return static_cast<float>(meterResetCount_); }
  [[nodiscard]] float currentOutputIntegratedLufs() const;
  [[nodiscard]] float currentOutputShortTermLufs() const;
  [[nodiscard]] float currentGainReductionDb() const;

private:
  [[nodiscard]] float correctionMix(bool useHighBranch) const;
  [[nodiscard]] float fixedGainDb() const;
  [[nodiscard]] float minimumGainDb() const;
  [[nodiscard]] float effectiveInputLevelLufs() const;
  [[nodiscard]] float freezeThresholdLufs() const;
  [[nodiscard]] float servoBrake(float detectorLufs) const;
  [[nodiscard]] bool speechModeEnabled() const;
  [[nodiscard]] bool monoModeEnabled() const;
  [[nodiscard]] LoudnessMeter& inputMeter();
  [[nodiscard]] const LoudnessMeter& inputMeter() const;
  [[nodiscard]] LoudnessMeter& outputMeter();
  [[nodiscard]] const LoudnessMeter& outputMeter() const;
  void updateMeterResetLatch();
  void updateAutoHoldGate(float detectorLufs);

  double sampleRate_{48000.0};
  std::size_t channelCount_{2};
  ParameterState parameters_{};
  LoudnessMeter stereoInputMeter_{};
  LoudnessMeter monoInputMeter_{};
  LoudnessMeter stereoOutputMeter_{};
  LoudnessMeter monoOutputMeter_{};
  TruePeakLimiter limiter_{};
  float fastGainDb_{0.0f};
  float mediumGainDb_{0.0f};
  float slowGainDb_{0.0f};
  float baselineGainDb_{0.0f};
  float currentGainReductionDb_{0.0f};
  float fastTargetGainDb_{0.0f};
  float mediumTargetGainDb_{0.0f};
  float slowTargetGainDb_{0.0f};
  float currentAppliedGainDb_{0.0f};
  float fastAttackCoeff_{0.0f};
  float fastReleaseCoeff_{0.0f};
  float mediumAttackCoeff_{0.0f};
  float mediumReleaseCoeff_{0.0f};
  float slowAttackCoeff_{0.0f};
  float slowReleaseCoeff_{0.0f};
  float baselineSmoothingCoeff_{0.0f};
  float speechMediumAttackCoeff_{0.0f};
  float speechMediumReleaseCoeff_{0.0f};
  float speechSlowAttackCoeff_{0.0f};
  float speechSlowReleaseCoeff_{0.0f};
  float learnedStereoInputLevelLufs_{-23.0f};
  float learnedMonoInputLevelLufs_{-23.0f};
  float outputSupervisorLufs_{-70.0f};
  float inputLevelAttackCoeff_{0.0f};
  float inputLevelReleaseCoeff_{0.0f};
  float currentMeterValue_{-70.0f};
  float currentLatencySamples_{0.0f};
  bool offlineMode_{false};
  bool resetWasHigh_{false};
  bool resetPending_{false};
  std::uint32_t meterResetCount_{0};
  bool autoHoldGateOpen_{false};
  bool outputSupervisorReady_{false};
  bool controlMonoMode_{false};
  float monoMix_{0.0f};
  float monoMixStep_{1.0f};
  std::uint32_t autoHoldHops_{0};
  std::uint32_t autoHoldHopsRemaining_{0};
  std::uint32_t outputFeedbackSettleHopsRemaining_{0};
  std::vector<float> frameInput_{};
  std::vector<float> frameOutput_{};
  std::array<float, 2> stereoInputFrame_{};
  std::array<float, 2> stereoOutputFrame_{};
  std::array<float, 1> monoInputFrame_{};
  std::array<float, 1> monoOutputFrame_{};
};

}  // namespace gainpilot::dsp
