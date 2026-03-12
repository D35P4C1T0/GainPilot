#include <cmath>
#include <iostream>
#include <vector>

#include "gainpilot/dsp/loudness_meter.hpp"
#include "gainpilot/dsp/processor.hpp"
#include "gainpilot/dsp/true_peak_limiter.hpp"
#include "gainpilot/state.hpp"

namespace {

float measureOversampledPeak(const std::vector<float>& signal) {
  if (signal.empty()) {
    return 0.0f;
  }
  if (signal.size() < 4) {
    float peak = 0.0f;
    for (const float sample : signal) {
      peak = std::max(peak, std::fabs(sample));
    }
    return peak;
  }

  auto catmullRom = [](float p0, float p1, float p2, float p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
  };

  float peak = 0.0f;
  for (std::size_t i = 1; i + 2 < signal.size(); ++i) {
    peak = std::max(peak, std::max(std::fabs(signal[i]), std::fabs(signal[i + 1])));
    for (std::size_t step = 1; step < 8; ++step) {
      const float t = static_cast<float>(step) / 8.0f;
      peak = std::max(peak, std::fabs(catmullRom(signal[i - 1], signal[i], signal[i + 1], signal[i + 2], t)));
    }
  }
  return peak;
}

float measureIntegratedLufs(const std::vector<float>& left, const std::vector<float>& right) {
  gainpilot::dsp::LoudnessMeter meter;
  meter.prepare(48000.0, 2);
  float frame[2]{};
  for (std::size_t i = 0; i < left.size(); ++i) {
    frame[0] = left[i];
    frame[1] = right[i];
    (void)meter.processFrame(frame);
  }
  return meter.integratedLufs();
}

float processAndMeasureIntegrated(gainpilot::ParameterState state,
                                  const std::vector<float>& inLeft,
                                  const std::vector<float>& inRight) {
  gainpilot::dsp::GainPilotProcessor processor;
  processor.prepare(48000.0, 2, 256);
  processor.setParameters(state);

  std::vector<float> outLeft(inLeft.size(), 0.0f);
  std::vector<float> outRight(inRight.size(), 0.0f);

  constexpr std::size_t kBlockSize = 256;
  for (std::size_t offset = 0; offset < inLeft.size(); offset += kBlockSize) {
    const auto frames = std::min<std::size_t>(kBlockSize, inLeft.size() - offset);
    const float* inputs[] = {inLeft.data() + offset, inRight.data() + offset};
    float* outputs[] = {outLeft.data() + offset, outRight.data() + offset};
    const gainpilot::dsp::ProcessBuffer buffer{
        .inputs = inputs,
        .outputs = outputs,
        .channels = 2,
        .frames = frames,
    };
    processor.process(buffer);
  }

  return measureIntegratedLufs(outLeft, outRight);
}

}  // namespace

int main() {
  gainpilot::dsp::GainPilotProcessor processor;
  processor.prepare(48000.0, 2, 256);

  constexpr std::size_t kFrames = 48000;
  std::vector<float> inLeft(kFrames, 0.0f);
  std::vector<float> inRight(kFrames, 0.0f);
  std::vector<float> outLeft(kFrames, 0.0f);
  std::vector<float> outRight(kFrames, 0.0f);

  for (std::size_t i = 0; i < kFrames; ++i) {
    const float sample = 0.1f * std::sin(static_cast<float>(i) * 0.01f);
    inLeft[i] = sample;
    inRight[i] = sample;
  }

  const float* inputs[] = {inLeft.data(), inRight.data()};
  float* outputs[] = {outLeft.data(), outRight.data()};
  const gainpilot::dsp::ProcessBuffer buffer{
      .inputs = inputs,
      .outputs = outputs,
      .channels = 2,
      .frames = kFrames,
  };

  processor.process(buffer);

  if (!std::isfinite(processor.currentMeterValue())) {
    std::cerr << "Meter value is not finite\n";
    return 1;
  }
  if (processor.currentLatencySamples() < 1500.0f || processor.currentLatencySamples() > 1800.0f) {
    std::cerr << "Latency does not match expected LUveler-style range\n";
    return 1;
  }

  gainpilot::ParameterState state;
  state.set(gainpilot::ParamId::targetLevel, -14.0f);
  state.set(gainpilot::ParamId::meterMode, 2.0f);
  state.set(gainpilot::ParamId::meterReset, 1.0f);
  state.set(gainpilot::ParamId::meterValue, -12.0f);
  const auto serialized = gainpilot::serializeState(state);
  const auto restored = gainpilot::deserializeState(serialized);
  if (!restored || restored->get(gainpilot::ParamId::targetLevel) != -14.0f) {
    std::cerr << "State serialization roundtrip failed\n";
    return 1;
  }
  if (restored->get(gainpilot::ParamId::meterMode) != 2.0f ||
      restored->get(gainpilot::ParamId::meterReset) != 0.0f ||
      restored->get(gainpilot::ParamId::meterValue) != -70.0f) {
    std::cerr << "Transient meter state leaked into serialized state\n";
    return 1;
  }

  constexpr std::size_t kProgramFrames = 48000 * 12;
  std::vector<float> dynLeft(kProgramFrames, 0.0f);
  std::vector<float> dynRight(kProgramFrames, 0.0f);
  for (std::size_t i = 0; i < kProgramFrames; ++i) {
    const float carrier = std::sin(static_cast<float>(i) * 0.013f);
    const std::size_t segment = (i / 12000) % 2;
    const float amplitude = segment == 0 ? 0.18f : 0.025f;
    dynLeft[i] = amplitude * carrier;
    dynRight[i] = amplitude * carrier;
  }

  const float inputIntegrated = measureIntegratedLufs(dynLeft, dynRight);
  gainpilot::ParameterState dynamicState;
  dynamicState.set(gainpilot::ParamId::inputLevel, inputIntegrated);
  dynamicState.set(gainpilot::ParamId::targetLevel, std::min(-14.0f, inputIntegrated + 4.0f));
  dynamicState.set(gainpilot::ParamId::truePeak, -1.0f);
  dynamicState.set(gainpilot::ParamId::maxGain, 20.0f);
  dynamicState.set(gainpilot::ParamId::correctionHigh, 100.0f);
  dynamicState.set(gainpilot::ParamId::correctionLow, 100.0f);
  dynamicState.set(gainpilot::ParamId::corrMixMode, 0.0f);

  auto legacyFreezeLoose = dynamicState;
  legacyFreezeLoose.set(gainpilot::ParamId::freezeLevel, -60.0f);
  const float looseIntegrated = processAndMeasureIntegrated(legacyFreezeLoose, dynLeft, dynRight);

  auto legacyFreezeHot = dynamicState;
  legacyFreezeHot.set(gainpilot::ParamId::freezeLevel, -20.0f);
  const float hotIntegrated = processAndMeasureIntegrated(legacyFreezeHot, dynLeft, dynRight);

  std::vector<float> longDynLeft(48000 * 30, 0.0f);
  std::vector<float> longDynRight(48000 * 30, 0.0f);
  for (std::size_t i = 0; i < longDynLeft.size(); ++i) {
    const float carrier = std::sin(static_cast<float>(i) * 0.011f);
    const std::size_t segment = (i / 24000) % 3;
    const float amplitude = segment == 0 ? 0.10f : (segment == 1 ? 0.030f : 0.22f);
    longDynLeft[i] = amplitude * carrier;
    longDynRight[i] = amplitude * carrier;
  }

  gainpilot::ParameterState defaultState;
  const float defaultIntegrated = processAndMeasureIntegrated(defaultState, longDynLeft, longDynRight);
  auto lowerTargetState = defaultState;
  lowerTargetState.set(gainpilot::ParamId::targetLevel, -24.0f);
  const float lowerTargetIntegrated = processAndMeasureIntegrated(lowerTargetState, longDynLeft, longDynRight);

  const float targetLevel = dynamicState.get(gainpilot::ParamId::targetLevel);
  if (std::abs(looseIntegrated - targetLevel) > 2.5f) {
    std::cerr << "Leveler misses the target too much on dynamic program material\n";
    return 1;
  }
  if (std::abs(hotIntegrated - looseIntegrated) > 0.35f) {
    std::cerr << "Legacy Freeze still changes output loudness too much\n";
    return 1;
  }
  if (std::abs(defaultIntegrated - defaultState.get(gainpilot::ParamId::targetLevel)) > 3.0f) {
    std::cerr << "Default preset still misses integrated loudness target too much\n";
    return 1;
  }
  if (std::abs(defaultIntegrated - lowerTargetIntegrated) < 4.0f) {
    std::cerr << "Target Level does not materially change processed loudness\n";
    return 1;
  }

  gainpilot::ParameterState zeroInputState = dynamicState;
  zeroInputState.set(gainpilot::ParamId::inputLevel, 0.0f);
  if (!(zeroInputState.get(gainpilot::ParamId::inputLevel) < 0.0f)) {
    std::cerr << "Input Level top-end safety clamp was not applied\n";
    return 1;
  }
  const float zeroInputIntegrated = processAndMeasureIntegrated(zeroInputState, dynLeft, dynRight);
  if (!std::isfinite(zeroInputIntegrated)) {
    std::cerr << "Input Level at 0 dB causes non-finite processing\n";
    return 1;
  }

  gainpilot::dsp::TruePeakLimiter limiter;
  limiter.prepare(48000.0, 2, 0.035375);
  limiter.setCeilingDb(-3.0f);

  constexpr std::size_t kLimiterFrames = 48000 * 2;
  std::vector<float> limiterOutLeft(kLimiterFrames, 0.0f);
  std::vector<float> limiterOutRight(kLimiterFrames, 0.0f);
  float limiterInFrame[2]{};
  float limiterOutFrame[2]{};
  for (std::size_t i = 0; i < kLimiterFrames; ++i) {
    const float sample =
        0.97f * std::sin(2.0f * static_cast<float>(M_PI) * 19000.0f * static_cast<float>(i) / 48000.0f);
    limiterInFrame[0] = sample;
    limiterInFrame[1] = sample;
    limiter.processFrame(limiterInFrame, limiterOutFrame, 1.5f);
    limiterOutLeft[i] = limiterOutFrame[0];
    limiterOutRight[i] = limiterOutFrame[1];
  }

  const float oversampledPeak = std::max(measureOversampledPeak(limiterOutLeft), measureOversampledPeak(limiterOutRight));
  const float ceilingLinear = std::pow(10.0f, -3.0f / 20.0f);
  if (oversampledPeak > ceilingLinear * 1.05f) {
    std::cerr << "Limiter leaves too much true-peak overshoot\n";
    return 1;
  }

  return 0;
}
