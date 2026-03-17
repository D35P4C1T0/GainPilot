#include "gainpilot/dsp/processor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gainpilot {

namespace {

constexpr std::size_t toIndex(ParamId id) {
  return static_cast<std::size_t>(id);
}

}  // namespace

ParameterState::ParameterState() {
  for (const auto& spec : kParameterSpecs) {
    values_[toIndex(spec.id)] = spec.defaultValue;
  }
}

float ParameterState::get(ParamId id) const {
  return values_[toIndex(id)];
}

void ParameterState::set(ParamId id, float value) {
  values_[toIndex(id)] = sanitizePlainValue(id, value);
}

void ParameterState::setNormalized(ParamId id, float normalized) {
  set(id, normalizedToPlain(id, normalized));
}

float ParameterState::getNormalized(ParamId id) const {
  return plainToNormalized(id, get(id));
}

std::span<const float, kNumParameters> ParameterState::values() const {
  return values_;
}

std::span<float, kNumParameters> ParameterState::values() {
  return values_;
}

const ParameterSpec& parameterSpec(ParamId id) {
  return kParameterSpecs[toIndex(id)];
}

float clampToSpec(ParamId id, float value) {
  const auto& spec = parameterSpec(id);
  return std::clamp(value, spec.minValue, spec.maxValue);
}

float sanitizePlainValue(ParamId id, float value) {
  value = clampToSpec(id, value);
  if (id == ParamId::inputLevel && value >= parameterSpec(id).maxValue) {
    return std::nextafter(parameterSpec(id).maxValue, parameterSpec(id).minValue);
  }
  return value;
}

float normalizedToPlain(ParamId id, float normalized) {
  const auto& spec = parameterSpec(id);
  normalized = std::clamp(normalized, 0.0f, 1.0f);
  return sanitizePlainValue(id, spec.minValue + normalized * (spec.maxValue - spec.minValue));
}

float plainToNormalized(ParamId id, float plain) {
  const auto& spec = parameterSpec(id);
  if (spec.maxValue <= spec.minValue) {
    return 0.0f;
  }
  return (sanitizePlainValue(id, plain) - spec.minValue) / (spec.maxValue - spec.minValue);
}

}  // namespace gainpilot

namespace gainpilot::dsp {

namespace {

constexpr float kFreezeCloseHysteresisLufs = 2.0f;
constexpr std::uint32_t kFreezeHoldHops = 8;
constexpr float kMinGainDb = -24.0f;
constexpr float kFastTransientThresholdDb = 1.5f;
constexpr float kFastMaxAttenuationDb = 6.0f;
constexpr float kMediumMaxAttenuationDb = 10.0f;
constexpr float kMediumMaxGainDb = 10.0f;
constexpr float kSlowMaxAttenuationDb = 6.0f;
constexpr float kSlowMaxGainDb = 6.0f;
constexpr float kMediumMomentaryWeight = 0.65f;
constexpr float kIntegratedTrimMaxDb = 8.0f;
constexpr std::size_t kIntegratedTrimReadyBlocks = 30;
constexpr std::size_t kInputLevelReadyBlocks = 30;
constexpr float kAutoFreezeOffsetLufs = 27.0f;
constexpr float kAutoFreezeMinLufs = -60.0f;
constexpr float kAutoFreezeMaxLufs = -35.0f;

float dbToLinear(float valueDb) {
  return std::pow(10.0f, valueDb / 20.0f);
}

float makeCoeff(double sampleRate, float seconds) {
  return std::exp(-1.0f / (seconds * static_cast<float>(sampleRate)));
}

float smoothTowards(float current, float target, float attackCoeff, float releaseCoeff) {
  if (target < current) {
    return target + attackCoeff * (current - target);
  }
  return target + releaseCoeff * (current - target);
}

}  // namespace

void GainPilotProcessor::prepare(double sampleRate, std::size_t channelCount, std::size_t maxBlockSize) {
  sampleRate_ = sampleRate;
  channelCount_ = channelCount;
  meter_.prepare(sampleRate_, channelCount_);
  outputMeter_.prepare(sampleRate_, channelCount_);
  limiter_.prepare(sampleRate_, channelCount_, 0.035375);
  frameInput_.assign(channelCount_, 0.0f);
  frameOutput_.assign(channelCount_, 0.0f);
  fastAttackCoeff_ = makeCoeff(sampleRate_, 0.015f);
  fastReleaseCoeff_ = makeCoeff(sampleRate_, 0.120f);
  mediumAttackCoeff_ = makeCoeff(sampleRate_, 0.120f);
  mediumReleaseCoeff_ = makeCoeff(sampleRate_, 1.200f);
  slowAttackCoeff_ = makeCoeff(sampleRate_, 0.750f);
  slowReleaseCoeff_ = makeCoeff(sampleRate_, 4.000f);
  integratedTrimAttackCoeff_ = makeCoeff(sampleRate_, 2.000f);
  integratedTrimReleaseCoeff_ = makeCoeff(sampleRate_, 10.000f);
  inputLevelAttackCoeff_ = makeCoeff(sampleRate_, 3.000f);
  inputLevelReleaseCoeff_ = makeCoeff(sampleRate_, 12.000f);
  autoHoldHops_ = kFreezeHoldHops;
  (void) maxBlockSize;
  reset();
}

void GainPilotProcessor::reset() {
  meter_.reset();
  outputMeter_.reset();
  limiter_.reset();
  fastGainDb_ = 0.0f;
  mediumGainDb_ = 0.0f;
  slowGainDb_ = 0.0f;
  integratedTrimGainDb_ = 0.0f;
  fastTargetGainDb_ = 0.0f;
  mediumTargetGainDb_ = 0.0f;
  slowTargetGainDb_ = 0.0f;
  integratedTrimTargetGainDb_ = 0.0f;
  learnedInputLevelLufs_ = parameters_.get(ParamId::inputLevel);
  currentMeterValue_ = -70.0f;
  currentLatencySamples_ = static_cast<float>(limiter_.latencySamples());
  resetWasHigh_ = false;
  autoHoldGateOpen_ = false;
  autoHoldHopsRemaining_ = 0;
}

void GainPilotProcessor::setParameters(const ParameterState& state) {
  parameters_ = state;
}

void GainPilotProcessor::setOfflineMode(bool offlineMode) {
  offlineMode_ = offlineMode;
}

std::size_t GainPilotProcessor::latencySamples() const {
  return limiter_.latencySamples();
}

float GainPilotProcessor::fixedGainDb() const {
  return parameters_.get(ParamId::targetLevel) - effectiveInputLevelLufs();
}

float GainPilotProcessor::effectiveInputLevelLufs() const {
  if (meter_.integratedBlockCount() >= kInputLevelReadyBlocks) {
    return learnedInputLevelLufs_;
  }
  return parameters_.get(ParamId::inputLevel);
}

float GainPilotProcessor::freezeThresholdLufs() const {
  return std::clamp(effectiveInputLevelLufs() - kAutoFreezeOffsetLufs, kAutoFreezeMinLufs, kAutoFreezeMaxLufs);
}

float GainPilotProcessor::correctionMix(bool useHighBranch) const {
  (void)useHighBranch;
  return 1.0f;
}

float GainPilotProcessor::computeDesiredTotalGainDb(float detectorLufs) const {
  const float baselineGainDb = std::clamp(fixedGainDb(), kMinGainDb, parameters_.get(ParamId::maxGain));
  const float inputReferenceLufs = effectiveInputLevelLufs();
  const float controlledGainDb = parameters_.get(ParamId::targetLevel) - detectorLufs;
  const bool useHighBranch = detectorLufs >= inputReferenceLufs;
  const float mix = correctionMix(useHighBranch);
  const float desiredGainDb = baselineGainDb + mix * (controlledGainDb - baselineGainDb);
  return std::clamp(desiredGainDb, kMinGainDb, parameters_.get(ParamId::maxGain));
}

void GainPilotProcessor::updateMeterResetLatch() {
  const bool resetHigh = parameters_.get(ParamId::meterReset) >= 0.5f;
  if (resetHigh && !resetWasHigh_) {
    meter_.resetIntegrated();
    outputMeter_.resetIntegrated();
  }
  resetWasHigh_ = resetHigh;
}

void GainPilotProcessor::updateAutoHoldGate(float detectorLufs) {
  const float holdThreshold = freezeThresholdLufs();

  if (detectorLufs >= holdThreshold) {
    autoHoldGateOpen_ = true;
    autoHoldHopsRemaining_ = autoHoldHops_;
    return;
  }

  if (autoHoldGateOpen_ && detectorLufs >= holdThreshold - kFreezeCloseHysteresisLufs) {
    autoHoldHopsRemaining_ = autoHoldHops_;
    return;
  }

  if (autoHoldHopsRemaining_ > 0) {
    --autoHoldHopsRemaining_;
    return;
  }

  autoHoldGateOpen_ = false;
}

void GainPilotProcessor::process(const ProcessBuffer& buffer) {
  updateMeterResetLatch();
  limiter_.setCeilingDb(parameters_.get(ParamId::truePeak));
  const auto meterMode = static_cast<MeterMode>(static_cast<int>(parameters_.get(ParamId::meterMode)));
  const bool limiterOnly =
      parameters_.get(ParamId::correctionHigh) <= 0.0f && parameters_.get(ParamId::correctionLow) <= 0.0f;
  if (limiterOnly) {
    fastTargetGainDb_ = 0.0f;
    mediumTargetGainDb_ = 0.0f;
    slowTargetGainDb_ = 0.0f;
    integratedTrimTargetGainDb_ = 0.0f;
  }

  for (std::size_t frame = 0; frame < buffer.frames; ++frame) {
    for (std::size_t channel = 0; channel < channelCount_; ++channel) {
      frameInput_[channel] = buffer.inputs[channel][frame];
    }

    const float baselineGainDb = std::clamp(fixedGainDb(), kMinGainDb, parameters_.get(ParamId::maxGain));
    const bool inputControlHop = meter_.processFrame(frameInput_.data());
    if (!limiterOnly && inputControlHop) {
      if (meter_.integratedBlockCount() >= kInputLevelReadyBlocks) {
        learnedInputLevelLufs_ = smoothTowards(
            learnedInputLevelLufs_, meter_.integratedLufs(), inputLevelAttackCoeff_, inputLevelReleaseCoeff_);
      }

      const float inputSlowDetectorLufs = meter_.controlLufs();
      const float inputFastDetectorLufs = meter_.momentaryLufs();
      const float inputReferenceLufs = effectiveInputLevelLufs();
      const float highMix = correctionMix(true);
      const float targetLevel = parameters_.get(ParamId::targetLevel);
      const float mediumDetectorLufs =
          inputSlowDetectorLufs + kMediumMomentaryWeight * (inputFastDetectorLufs - inputSlowDetectorLufs);
      const float slowDesiredTotalGainDb = computeDesiredTotalGainDb(inputSlowDetectorLufs);
      const float mediumDesiredTotalGainDb = computeDesiredTotalGainDb(mediumDetectorLufs);

      updateAutoHoldGate(inputSlowDetectorLufs);

      const float fastExcessDb = inputFastDetectorLufs - std::max(inputSlowDetectorLufs, inputReferenceLufs);
      fastTargetGainDb_ =
          -highMix * std::clamp(fastExcessDb - kFastTransientThresholdDb, 0.0f, kFastMaxAttenuationDb);

      slowTargetGainDb_ =
          std::clamp(slowDesiredTotalGainDb - baselineGainDb, -kSlowMaxAttenuationDb, kSlowMaxGainDb);
      const float mediumResidualGainDb = mediumDesiredTotalGainDb - (baselineGainDb + slowTargetGainDb_);
      mediumTargetGainDb_ = std::clamp(mediumResidualGainDb, -kMediumMaxAttenuationDb, kMediumMaxGainDb);
      if (outputMeter_.integratedBlockCount() >= kIntegratedTrimReadyBlocks) {
        const float integratedErrorDb = targetLevel - outputMeter_.integratedLufs();
        integratedTrimTargetGainDb_ = std::clamp(integratedErrorDb, -kIntegratedTrimMaxDb, kIntegratedTrimMaxDb);
      } else {
        integratedTrimTargetGainDb_ = 0.0f;
      }

      const float currentTotalGainDb =
          baselineGainDb + fastGainDb_ + mediumGainDb_ + slowGainDb_ + integratedTrimGainDb_;
      const float targetTotalGainDb = baselineGainDb + fastTargetGainDb_ + mediumTargetGainDb_ + slowTargetGainDb_ +
                                      integratedTrimTargetGainDb_;
      if (!autoHoldGateOpen_ && targetTotalGainDb > currentTotalGainDb) {
        slowTargetGainDb_ += currentTotalGainDb - targetTotalGainDb;
      }

      const float unclampedTotalTargetDb = baselineGainDb + fastTargetGainDb_ + mediumTargetGainDb_ + slowTargetGainDb_ +
                                           integratedTrimTargetGainDb_;
      const float clampedTotalTargetDb = std::clamp(unclampedTotalTargetDb, kMinGainDb, parameters_.get(ParamId::maxGain));
      slowTargetGainDb_ += clampedTotalTargetDb - unclampedTotalTargetDb;
    }

    fastGainDb_ = smoothTowards(fastGainDb_, fastTargetGainDb_, fastAttackCoeff_, fastReleaseCoeff_);
    mediumGainDb_ = smoothTowards(mediumGainDb_, mediumTargetGainDb_, mediumAttackCoeff_, mediumReleaseCoeff_);
    slowGainDb_ = smoothTowards(slowGainDb_, slowTargetGainDb_, slowAttackCoeff_, slowReleaseCoeff_);
    integratedTrimGainDb_ = smoothTowards(
        integratedTrimGainDb_, integratedTrimTargetGainDb_, integratedTrimAttackCoeff_, integratedTrimReleaseCoeff_);

    const float totalGainDb = std::clamp(
        baselineGainDb + fastGainDb_ + mediumGainDb_ + slowGainDb_ + integratedTrimGainDb_,
        kMinGainDb,
        parameters_.get(ParamId::maxGain));
    limiter_.processFrame(frameInput_.data(), frameOutput_.data(), dbToLinear(totalGainDb));
    (void)outputMeter_.processFrame(frameOutput_.data());

    for (std::size_t channel = 0; channel < channelCount_; ++channel) {
      buffer.outputs[channel][frame] = frameOutput_[channel];
    }
  }

  currentMeterValue_ = meter_.loudnessForMode(meterMode);
}

float GainPilotProcessor::currentMeterValue() const {
  return currentMeterValue_;
}

float GainPilotProcessor::currentLatencySamples() const {
  return currentLatencySamples_;
}

float GainPilotProcessor::currentAppliedGainDb() const {
  return fixedGainDb() + fastGainDb_ + mediumGainDb_ + slowGainDb_ + integratedTrimGainDb_;
}

float GainPilotProcessor::currentInputShortTermLufs() const {
  return meter_.shortTermLufs();
}

float GainPilotProcessor::currentInputIntegratedLufs() const {
  return meter_.integratedLufs();
}

float GainPilotProcessor::currentOutputIntegratedLufs() const {
  return outputMeter_.integratedLufs();
}

float GainPilotProcessor::currentOutputShortTermLufs() const {
  return outputMeter_.shortTermLufs();
}

}  // namespace gainpilot::dsp
