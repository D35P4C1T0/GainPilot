#include <algorithm>
#include <cmath>
#include <array>
#include <cstring>
#include <iostream>
#include <utility>
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

float measureMonoIntegratedLufs(const std::vector<float>& signal, double sampleRate) {
  gainpilot::dsp::LoudnessMeter meter;
  meter.prepare(sampleRate, 1);
  float frame[1]{};
  for (const float sample : signal) {
    frame[0] = sample;
    (void)meter.processFrame(frame);
  }
  return meter.integratedLufs();
}

float measureStereoIntegratedLufs(const std::vector<float>& left,
                                  const std::vector<float>& right,
                                  double sampleRate) {
  gainpilot::dsp::LoudnessMeter meter;
  meter.prepare(sampleRate, 2);
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

std::pair<std::vector<float>, std::vector<float>> processStereo(
    gainpilot::ParameterState state,
    const std::vector<float>& inLeft,
    const std::vector<float>& inRight,
    std::size_t blockSize,
    bool offline) {
  gainpilot::dsp::GainPilotProcessor processor;
  processor.prepare(48000.0, 2, blockSize);
  processor.setParameters(state);
  processor.setOfflineMode(offline);

  std::vector<float> outLeft(inLeft.size(), 0.0f);
  std::vector<float> outRight(inRight.size(), 0.0f);
  for (std::size_t offset = 0; offset < inLeft.size(); offset += blockSize) {
    const auto frames = std::min(blockSize, inLeft.size() - offset);
    const float* inputs[]{inLeft.data() + offset, inRight.data() + offset};
    float* outputs[]{outLeft.data() + offset, outRight.data() + offset};
    processor.process({
        .inputs = inputs,
        .outputs = outputs,
        .channels = 2,
        .frames = frames,
    });
  }
  return {std::move(outLeft), std::move(outRight)};
}

}  // namespace

int main() {
  constexpr float kPi = 3.14159265358979323846f;

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
    std::cerr << "Latency does not match the expected fixed-latency range\n";
    return 1;
  }

  gainpilot::ParameterState state;
  state.set(gainpilot::ParamId::targetLevel, -14.0f);
  state.set(gainpilot::ParamId::inputTrim, 3.0f);
  state.set(gainpilot::ParamId::programMode, 1.0f);
  state.set(gainpilot::ParamId::meterMode, 2.0f);
  state.set(gainpilot::ParamId::meterReset, 1.0f);
  state.set(gainpilot::ParamId::meterValue, -12.0f);
  state.set(gainpilot::ParamId::inputIntegratedValue, -18.0f);
  state.set(gainpilot::ParamId::outputIntegratedValue, -16.0f);
  state.set(gainpilot::ParamId::outputShortTermValue, -15.0f);
  state.set(gainpilot::ParamId::gainReductionValue, 4.0f);
  const auto serialized = gainpilot::serializeState(state);
  const auto restored = gainpilot::deserializeState(serialized);
  if (!restored || restored->get(gainpilot::ParamId::targetLevel) != -14.0f) {
    std::cerr << "State serialization roundtrip failed\n";
    return 1;
  }
  if (restored->get(gainpilot::ParamId::inputTrim) != 3.0f ||
      restored->get(gainpilot::ParamId::programMode) != 1.0f) {
    std::cerr << "New control state did not survive serialization\n";
    return 1;
  }
  if (restored->get(gainpilot::ParamId::channelMode) !=
      static_cast<float>(gainpilot::ChannelMode::stereo)) {
    std::cerr << "New sessions do not default to stereo mode\n";
    return 1;
  }

  state.set(gainpilot::ParamId::channelMode, static_cast<float>(gainpilot::ChannelMode::mono));
  const auto monoStateRestored = gainpilot::deserializeState(gainpilot::serializeState(state));
  if (!monoStateRestored ||
      monoStateRestored->get(gainpilot::ParamId::channelMode) !=
          static_cast<float>(gainpilot::ChannelMode::mono)) {
    std::cerr << "Channel Mode did not survive state serialization\n";
    return 1;
  }
  auto legacyV2State = gainpilot::serializeState(state);
  const std::uint32_t legacyVersion = 2;
  const std::uint32_t legacyCount = 11;
  std::memcpy(legacyV2State.data() + 4, &legacyVersion, sizeof(legacyVersion));
  std::memcpy(legacyV2State.data() + 8, &legacyCount, sizeof(legacyCount));
  legacyV2State.resize(12 + sizeof(float) * legacyCount);
  const auto legacyV2Restored = gainpilot::deserializeState(legacyV2State);
  if (!legacyV2Restored ||
      legacyV2Restored->get(gainpilot::ParamId::channelMode) !=
          static_cast<float>(gainpilot::ChannelMode::stereo)) {
    std::cerr << "Legacy state does not safely default Channel Mode to stereo\n";
    return 1;
  }
  if (restored->get(gainpilot::ParamId::meterMode) != 2.0f ||
      restored->get(gainpilot::ParamId::meterReset) != 0.0f ||
      restored->get(gainpilot::ParamId::meterValue) != -70.0f ||
      restored->get(gainpilot::ParamId::inputIntegratedValue) != -70.0f ||
      restored->get(gainpilot::ParamId::outputIntegratedValue) != -70.0f ||
      restored->get(gainpilot::ParamId::outputShortTermValue) != -70.0f ||
      restored->get(gainpilot::ParamId::gainReductionValue) != 0.0f) {
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

  constexpr std::size_t kFreezeFrames = 48000 * 24;
  std::vector<float> freezeLeft(kFreezeFrames, 0.0f);
  std::vector<float> freezeRight(kFreezeFrames, 0.0f);
  for (std::size_t i = 0; i < kFreezeFrames; ++i) {
    const float carrier = std::sin(static_cast<float>(i) * 0.012f);
    const std::size_t section = i / (48000 * 8);
    const float amplitude = section == 1 ? 0.020f : 0.160f;
    freezeLeft[i] = amplitude * carrier;
    freezeRight[i] = amplitude * carrier;
  }

  const float freezeInputIntegrated = measureIntegratedLufs(freezeLeft, freezeRight);
  gainpilot::ParameterState freezeState;
  freezeState.set(gainpilot::ParamId::inputLevel, freezeInputIntegrated);
  freezeState.set(gainpilot::ParamId::targetLevel, std::min(-14.0f, freezeInputIntegrated + 4.0f));
  freezeState.set(gainpilot::ParamId::truePeak, -1.0f);
  freezeState.set(gainpilot::ParamId::maxGain, 20.0f);
  freezeState.set(gainpilot::ParamId::correctionHigh, 100.0f);
  freezeState.set(gainpilot::ParamId::correctionLow, 100.0f);
  freezeState.set(gainpilot::ParamId::corrMixMode, 0.0f);

  auto freezeLooseState = freezeState;
  freezeLooseState.set(gainpilot::ParamId::freezeLevel, -60.0f);
  const float freezeLooseIntegrated = processAndMeasureIntegrated(freezeLooseState, freezeLeft, freezeRight);

  auto legacyFreezeHot = freezeState;
  legacyFreezeHot.set(gainpilot::ParamId::freezeLevel, -20.0f);
  const float hotIntegrated = processAndMeasureIntegrated(legacyFreezeHot, freezeLeft, freezeRight);

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
  auto trimOnlyState = defaultState;
  trimOnlyState.set(gainpilot::ParamId::correctionHigh, 0.0f);
  trimOnlyState.set(gainpilot::ParamId::correctionLow, 0.0f);
  trimOnlyState.set(gainpilot::ParamId::inputTrim, 6.0f);
  const float trimOnlyIntegrated = processAndMeasureIntegrated(trimOnlyState, longDynLeft, longDynRight);

  const float targetLevel = dynamicState.get(gainpilot::ParamId::targetLevel);
  if (std::abs(looseIntegrated - targetLevel) > 2.5f) {
    std::cerr << "Leveler misses the target too much on dynamic program material\n";
    return 1;
  }
  if (std::abs(hotIntegrated - freezeLooseIntegrated) > 0.35f) {
    std::cerr << "Legacy Freeze still changes output loudness too much\n";
    return 1;
  }
  if (std::abs(defaultIntegrated - defaultState.get(gainpilot::ParamId::targetLevel)) > 3.0f) {
    std::cerr << "Default preset still misses integrated loudness target too much\n";
    return 1;
  }
  if (std::abs(lowerTargetIntegrated - lowerTargetState.get(gainpilot::ParamId::targetLevel)) > 1.5f) {
    std::cerr << "Lower target still misses integrated loudness target too much\n";
    return 1;
  }
  if (std::abs(defaultIntegrated - lowerTargetIntegrated) < 4.0f) {
    std::cerr << "Target Level does not materially change processed loudness\n";
    return 1;
  }
  if (trimOnlyIntegrated - defaultIntegrated < 4.5f) {
    std::cerr << "Input Trim does not materially affect the signal path\n";
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

  processor.reset();
  processor.setParameters(state);
  processor.process(buffer);
  if (!std::isfinite(processor.currentGainReductionDb()) || processor.currentGainReductionDb() < 0.0f) {
    std::cerr << "Gain reduction readout is invalid\n";
    return 1;
  }

  constexpr std::array<double, 5> kSampleRates{44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
  for (const double sampleRate : kSampleRates) {
    const auto measurementFrames = static_cast<std::size_t>(sampleRate * 4.0);
    std::vector<float> monoSignal(measurementFrames, 0.0f);
    std::vector<float> silenceSignal(measurementFrames, 0.0f);
    for (std::size_t i = 0; i < measurementFrames; ++i) {
      monoSignal[i] =
          0.1f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(i) / static_cast<float>(sampleRate));
    }

    const float monoLufs = measureMonoIntegratedLufs(monoSignal, sampleRate);
    const float duplicatedStereoLufs = measureStereoIntegratedLufs(monoSignal, monoSignal, sampleRate);
    const float leftOnlyLufs = measureStereoIntegratedLufs(monoSignal, silenceSignal, sampleRate);
    const float rightOnlyLufs = measureStereoIntegratedLufs(silenceSignal, monoSignal, sampleRate);
    std::vector<float> uncorrelatedSignal(measurementFrames, 0.0f);
    for (std::size_t i = 0; i < measurementFrames; ++i) {
      uncorrelatedSignal[i] =
          0.1f * std::sin(2.0f * kPi * 631.0f * static_cast<float>(i) / static_cast<float>(sampleRate));
    }
    const float uncorrelatedStereoLufs =
        measureStereoIntegratedLufs(monoSignal, uncorrelatedSignal, sampleRate);
    const float silenceLufs = measureMonoIntegratedLufs(silenceSignal, sampleRate);
    if (std::abs((duplicatedStereoLufs - monoLufs) - 3.0103f) > 0.15f) {
      std::cerr << "Duplicated stereo energy is not approximately 3 LU above native mono at "
                << sampleRate << " Hz\n";
      return 1;
    }
    if (std::abs(leftOnlyLufs - monoLufs) > 0.15f || std::abs(rightOnlyLufs - monoLufs) > 0.15f) {
      std::cerr << "Single-sided stereo does not match native mono loudness at " << sampleRate << " Hz\n";
      return 1;
    }
    if (std::abs(uncorrelatedStereoLufs - duplicatedStereoLufs) > 0.2f) {
      std::cerr << "Uncorrelated stereo energy is not combined consistently at " << sampleRate << " Hz\n";
      return 1;
    }
    if (silenceLufs > -69.9f || !std::isfinite(silenceLufs)) {
      std::cerr << "Silence does not produce the finite LUFS floor at " << sampleRate << " Hz\n";
      return 1;
    }
  }

  constexpr std::size_t kModeFrames = 48000 * 2;
  std::vector<float> modeLeft(kModeFrames, 0.0f);
  std::vector<float> modeRight(kModeFrames, 0.0f);
  std::vector<float> modeOutLeft(kModeFrames, 0.0f);
  std::vector<float> modeOutRight(kModeFrames, 0.0f);
  for (std::size_t i = 0; i < kModeFrames; ++i) {
    const float carrier = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 48000.0f);
    modeLeft[i] = 0.1f * carrier;
    modeRight[i] = 0.04f * carrier;
  }

  gainpilot::ParameterState monoModeState;
  monoModeState.set(gainpilot::ParamId::channelMode, static_cast<float>(gainpilot::ChannelMode::mono));
  monoModeState.set(gainpilot::ParamId::correctionHigh, 0.0f);
  monoModeState.set(gainpilot::ParamId::correctionLow, 0.0f);
  monoModeState.set(gainpilot::ParamId::truePeak, 0.0f);
  gainpilot::dsp::GainPilotProcessor monoModeProcessor;
  monoModeProcessor.prepare(48000.0, 2, 256);
  monoModeProcessor.setParameters(monoModeState);
  const float* modeInputs[]{modeLeft.data(), modeRight.data()};
  float* modeOutputs[]{modeOutLeft.data(), modeOutRight.data()};
  monoModeProcessor.process({
      .inputs = modeInputs,
      .outputs = modeOutputs,
      .channels = 2,
      .frames = kModeFrames,
  });
  for (std::size_t i = monoModeProcessor.latencySamples() + 1200; i < kModeFrames; ++i) {
    if (std::abs(modeOutLeft[i] - modeOutRight[i]) > 1.0e-6f) {
      std::cerr << "Mono mode does not send an identical signal to both stereo outputs\n";
      return 1;
    }
  }

  constexpr std::size_t kSwitchBlockFrames = 48000;
  std::vector<float> switchLeft(kSwitchBlockFrames, 0.35f);
  std::vector<float> switchRight(kSwitchBlockFrames, -0.05f);
  std::vector<float> switchOutLeft(kSwitchBlockFrames, 0.0f);
  std::vector<float> switchOutRight(kSwitchBlockFrames, 0.0f);
  const float* switchInputs[]{switchLeft.data(), switchRight.data()};
  float* switchOutputs[]{switchOutLeft.data(), switchOutRight.data()};
  gainpilot::ParameterState switchState;
  switchState.set(gainpilot::ParamId::correctionHigh, 0.0f);
  switchState.set(gainpilot::ParamId::correctionLow, 0.0f);
  switchState.set(gainpilot::ParamId::truePeak, 0.0f);
  gainpilot::dsp::GainPilotProcessor switchProcessor;
  switchProcessor.prepare(48000.0, 2, 256);
  switchProcessor.setParameters(switchState);
  switchProcessor.process({
      .inputs = switchInputs,
      .outputs = switchOutputs,
      .channels = 2,
      .frames = kSwitchBlockFrames,
  });

  const float previousLeft = switchOutLeft.back();
  const float previousRight = switchOutRight.back();
  std::fill(switchOutLeft.begin(), switchOutLeft.end(), 0.0f);
  std::fill(switchOutRight.begin(), switchOutRight.end(), 0.0f);
  switchState.set(gainpilot::ParamId::channelMode, static_cast<float>(gainpilot::ChannelMode::mono));
  switchProcessor.setParameters(switchState);
  switchProcessor.process({
      .inputs = switchInputs,
      .outputs = switchOutputs,
      .channels = 2,
      .frames = kSwitchBlockFrames,
  });

  float maxSwitchDelta = std::max(std::abs(switchOutLeft.front() - previousLeft),
                                  std::abs(switchOutRight.front() - previousRight));
  for (std::size_t i = 1; i < kSwitchBlockFrames; ++i) {
    maxSwitchDelta = std::max(maxSwitchDelta, std::abs(switchOutLeft[i] - switchOutLeft[i - 1]));
    maxSwitchDelta = std::max(maxSwitchDelta, std::abs(switchOutRight[i] - switchOutRight[i - 1]));
  }
  if (maxSwitchDelta > 0.01f) {
    std::cerr << "Stereo/mono mode switching introduces an abrupt discontinuity\n";
    return 1;
  }
  for (std::size_t i = switchProcessor.latencySamples() + 1200; i < kSwitchBlockFrames; ++i) {
    if (std::abs(switchOutLeft[i] - switchOutRight[i]) > 1.0e-5f) {
      std::cerr << "Stereo/mono crossfade does not converge to linked mono output\n";
      return 1;
    }
  }

  for (std::size_t i = 0; i < kModeFrames; ++i) {
    modeRight[i] = -modeLeft[i];
  }
  std::fill(modeOutLeft.begin(), modeOutLeft.end(), 0.0f);
  std::fill(modeOutRight.begin(), modeOutRight.end(), 0.0f);
  monoModeProcessor.reset();
  monoModeProcessor.setParameters(monoModeState);
  monoModeProcessor.process({
      .inputs = modeInputs,
      .outputs = modeOutputs,
      .channels = 2,
      .frames = kModeFrames,
  });
  for (std::size_t i = monoModeProcessor.latencySamples(); i < kModeFrames; ++i) {
    if (std::abs(modeOutLeft[i]) > 1.0e-6f || std::abs(modeOutRight[i]) > 1.0e-6f) {
      std::cerr << "Mono downmix does not cancel opposite-polarity stereo as 0.5 * (L + R)\n";
      return 1;
    }
  }

  gainpilot::ParameterState quietState;
  quietState.set(gainpilot::ParamId::targetLevel, -10.0f);
  quietState.set(gainpilot::ParamId::inputLevel, -40.0f);
  quietState.set(gainpilot::ParamId::maxGain, 30.0f);
  std::vector<float> quietLeft(48000, 1.0e-6f);
  std::vector<float> quietRight(48000, -1.0e-6f);
  std::vector<float> quietOutLeft(48000, 0.0f);
  std::vector<float> quietOutRight(48000, 0.0f);
  const float* quietInputs[]{quietLeft.data(), quietRight.data()};
  float* quietOutputs[]{quietOutLeft.data(), quietOutRight.data()};
  gainpilot::dsp::GainPilotProcessor quietProcessor;
  quietProcessor.prepare(48000.0, 2, 256);
  quietProcessor.setParameters(quietState);
  quietProcessor.process({
      .inputs = quietInputs,
      .outputs = quietOutputs,
      .channels = 2,
      .frames = quietLeft.size(),
  });
  if (quietProcessor.currentAppliedGainDb() > 0.01f) {
    std::cerr << "Near-silence safety allows background noise gain-up\n";
    return 1;
  }

  constexpr std::size_t kConsistencyFrames = 48000 * 3;
  std::vector<float> consistencyLeft(kConsistencyFrames, 0.0f);
  std::vector<float> consistencyRight(kConsistencyFrames, 0.0f);
  for (std::size_t i = 0; i < kConsistencyFrames; ++i) {
    const float carrier = std::sin(2.0f * kPi * 523.25f * static_cast<float>(i) / 48000.0f);
    consistencyLeft[i] = 0.12f * carrier;
    consistencyRight[i] = -0.048f * carrier;
  }
  gainpilot::ParameterState consistencyState;
  consistencyState.set(gainpilot::ParamId::inputLevel, -16.0f);
  consistencyState.set(gainpilot::ParamId::targetLevel, -16.0f);
  consistencyState.set(gainpilot::ParamId::truePeak, 0.0f);
  consistencyState.set(gainpilot::ParamId::maxGain, 0.0f);
  consistencyState.set(gainpilot::ParamId::correctionHigh, 0.0f);
  consistencyState.set(gainpilot::ParamId::correctionLow, 0.0f);

  const auto realtime64 =
      processStereo(consistencyState, consistencyLeft, consistencyRight, 64, false);
  const auto realtime511 =
      processStereo(consistencyState, consistencyLeft, consistencyRight, 511, false);
  const auto offline257 =
      processStereo(consistencyState, consistencyLeft, consistencyRight, 257, true);
  float maxRenderDifference = 0.0f;
  float maxLinkError = 0.0f;
  for (std::size_t i = 0; i < kConsistencyFrames; ++i) {
    maxRenderDifference =
        std::max(maxRenderDifference, std::abs(realtime64.first[i] - realtime511.first[i]));
    maxRenderDifference =
        std::max(maxRenderDifference, std::abs(realtime64.second[i] - realtime511.second[i]));
    maxRenderDifference =
        std::max(maxRenderDifference, std::abs(realtime64.first[i] - offline257.first[i]));
    maxRenderDifference =
        std::max(maxRenderDifference, std::abs(realtime64.second[i] - offline257.second[i]));
    maxLinkError =
        std::max(maxLinkError, std::abs(realtime64.second[i] + 0.4f * realtime64.first[i]));
  }
  if (maxRenderDifference > 1.0e-6f) {
    std::cerr << "Rendering changes with buffer size or offline mode\n";
    return 1;
  }
  if (maxLinkError > 1.0e-5f) {
    std::cerr << "Stereo processing does not preserve linked channel balance and phase\n";
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
    const float sample = 0.97f * std::sin(2.0f * kPi * 19000.0f * static_cast<float>(i) / 48000.0f);
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
