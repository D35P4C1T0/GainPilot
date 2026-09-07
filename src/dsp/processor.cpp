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
  if (!std::isfinite(value))
    return parameterSpec(id).defaultValue;
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
constexpr float kFastTransientThresholdDb = 1.5f;
constexpr float kFastMaxAttenuationDb = 6.0f;
constexpr float kMediumMaxAttenuationDb = 10.0f;
constexpr float kMediumMaxGainDb = 10.0f;
constexpr float kMediumMomentaryWeight = 0.65f;
constexpr float kSpeechMediumMomentaryWeight = 0.35f;
constexpr std::size_t kInputLevelReadyBlocks = 30;
constexpr float kAutoFreezeOffsetLufs = 27.0f;
constexpr float kAutoFreezeMinLufs = -60.0f;
constexpr float kAutoFreezeMaxLufs = -35.0f;
constexpr float kBrakeKneeLufs = 12.0f;
constexpr float kSpeechFastTransientThresholdDb = 3.0f;
constexpr float kSpeechFastMaxAttenuationDb = 3.0f;
constexpr float kModeCrossfadeSeconds = 0.020f;
constexpr float kBaselineSmoothingSeconds = 0.050f;
constexpr float kServoTrackingSeconds = 0.100f;
constexpr float kOutputServoSeconds = 5.0f;
constexpr float kSpeechOutputServoSeconds = 7.0f;
// A leaky supervisor supplies long-term bias without retaining the full programme
// history of an integrated LUFS measurement.
constexpr float kOutputSupervisorSeconds = 30.0f;
constexpr float kOutputSupervisorWeight = 0.15f;
constexpr float kControlHopSeconds = 0.1f;
constexpr float kMaxServoStepDb = 0.5f;
constexpr float kFeedForwardSettleToleranceDb = 0.5f;
constexpr std::uint32_t kOutputFeedbackSettleHops = 30;

float dbToLinear(float valueDb) {
  return std::pow(10.0f, valueDb / 20.0f);
}

float makeCoeff(double sampleRate, float seconds) {
  return std::exp(-1.0f / (seconds * static_cast<float>(sampleRate)));
}

float curvedMix(float value, bool logarithmic) {
  value = std::clamp(value, 0.0f, 1.0f);
  return logarithmic ? value * value : value;
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
  stereoInputMeter_.prepare(sampleRate_, 2);
  monoInputMeter_.prepare(sampleRate_, 1);
  stereoOutputMeter_.prepare(sampleRate_, 2);
  monoOutputMeter_.prepare(sampleRate_, 1);
  limiter_.prepare(sampleRate_, channelCount_, 0.035375);
  frameInput_.assign(channelCount_, 0.0f);
  frameOutput_.assign(channelCount_, 0.0f);
  fastAttackCoeff_ = makeCoeff(sampleRate_, 0.015f);
  fastReleaseCoeff_ = makeCoeff(sampleRate_, 0.120f);
  mediumAttackCoeff_ = makeCoeff(sampleRate_, 0.120f);
  mediumReleaseCoeff_ = makeCoeff(sampleRate_, 1.200f);
  slowAttackCoeff_ = makeCoeff(sampleRate_, kServoTrackingSeconds);
  slowReleaseCoeff_ = slowAttackCoeff_;
  baselineSmoothingCoeff_ = makeCoeff(sampleRate_, kBaselineSmoothingSeconds);
  speechMediumAttackCoeff_ = makeCoeff(sampleRate_, 0.180f);
  speechMediumReleaseCoeff_ = makeCoeff(sampleRate_, 1.800f);
  speechSlowAttackCoeff_ = slowAttackCoeff_;
  speechSlowReleaseCoeff_ = slowReleaseCoeff_;
  const double controlHopSeconds = std::ceil(sampleRate_ * 0.1) / sampleRate_;
  inputLevelAttackCoeff_ = static_cast<float>(std::exp(-controlHopSeconds / 3.0));
  inputLevelReleaseCoeff_ = static_cast<float>(std::exp(-controlHopSeconds / 12.0));
  monoMixStep_ = 1.0f / std::max(1.0f, kModeCrossfadeSeconds * static_cast<float>(sampleRate_));
  autoHoldHops_ = kFreezeHoldHops;
  (void) maxBlockSize;
  reset();
}

void GainPilotProcessor::reset() {
  meterResetCount_ = (meterResetCount_ + 1) & 0xffffff;
  stereoInputMeter_.reset();
  monoInputMeter_.reset();
  stereoOutputMeter_.reset();
  monoOutputMeter_.reset();
  limiter_.reset();
  fastGainDb_ = 0.0f;
  mediumGainDb_ = 0.0f;
  slowGainDb_ = 0.0f;
  baselineGainDb_ = 0.0f;
  currentGainReductionDb_ = 0.0f;
  fastTargetGainDb_ = 0.0f;
  mediumTargetGainDb_ = 0.0f;
  slowTargetGainDb_ = 0.0f;
  currentAppliedGainDb_ = 0.0f;
  learnedStereoInputLevelLufs_ = parameters_.get(ParamId::inputLevel);
  learnedMonoInputLevelLufs_ = parameters_.get(ParamId::inputLevel);
  outputSupervisorLufs_ = -70.0f;
  currentMeterValue_ = -70.0f;
  currentLatencySamples_ = static_cast<float>(limiter_.latencySamples());
  resetWasHigh_ = false;
  resetPending_ = false;
  autoHoldGateOpen_ = false;
  outputSupervisorReady_ = false;
  controlMonoMode_ = monoModeEnabled();
  monoMix_ = monoModeEnabled() ? 1.0f : 0.0f;
  autoHoldHopsRemaining_ = 0;
  outputFeedbackSettleHopsRemaining_ = kOutputFeedbackSettleHops;
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

float GainPilotProcessor::minimumGainDb() const {
  return std::min(-parameters_.get(ParamId::maxCut), parameters_.get(ParamId::maxGain));
}

float GainPilotProcessor::effectiveInputLevelLufs() const {
  if (parameters_.get(ParamId::referenceMode) >= 0.5f)
    return parameters_.get(ParamId::lockedReference);
  if (inputMeter().integratedBlockCount() >= kInputLevelReadyBlocks) {
    return monoModeEnabled() ? learnedMonoInputLevelLufs_ : learnedStereoInputLevelLufs_;
  }
  return parameters_.get(ParamId::inputLevel);
}

float GainPilotProcessor::currentInputReferenceLufs() const {
  return effectiveInputLevelLufs();
}

float GainPilotProcessor::freezeThresholdLufs() const {
  return std::clamp(effectiveInputLevelLufs() - kAutoFreezeOffsetLufs, kAutoFreezeMinLufs, kAutoFreezeMaxLufs);
}

float GainPilotProcessor::servoBrake(float detectorLufs) const {
  if (!autoHoldGateOpen_) {
    return 0.0f;
  }
  const float normalized =
      std::clamp((detectorLufs - (freezeThresholdLufs() - kBrakeKneeLufs)) / kBrakeKneeLufs, 0.0f, 1.0f);
  return normalized * normalized * (3.0f - 2.0f * normalized);
}

bool GainPilotProcessor::speechModeEnabled() const {
  return static_cast<ProgramMode>(static_cast<int>(parameters_.get(ParamId::programMode))) == ProgramMode::speech;
}

bool GainPilotProcessor::monoModeEnabled() const {
  if (channelCount_ == 1) {
    return true;
  }
  return static_cast<ChannelMode>(static_cast<int>(parameters_.get(ParamId::channelMode))) == ChannelMode::mono;
}

LoudnessMeter& GainPilotProcessor::inputMeter() {
  return monoModeEnabled() ? monoInputMeter_ : stereoInputMeter_;
}

const LoudnessMeter& GainPilotProcessor::inputMeter() const {
  return monoModeEnabled() ? monoInputMeter_ : stereoInputMeter_;
}

LoudnessMeter& GainPilotProcessor::outputMeter() {
  return monoModeEnabled() ? monoOutputMeter_ : stereoOutputMeter_;
}

const LoudnessMeter& GainPilotProcessor::outputMeter() const {
  return monoModeEnabled() ? monoOutputMeter_ : stereoOutputMeter_;
}

float GainPilotProcessor::correctionMix(bool useHighBranch) const {
  const float correctionPercent =
      parameters_.get(useHighBranch ? ParamId::correctionHigh : ParamId::correctionLow);
  const int mode = static_cast<int>(parameters_.get(ParamId::corrMixMode));
  const bool logarithmic = useHighBranch ? (mode == 2 || mode == 3) : (mode == 1 || mode == 3);
  return curvedMix(correctionPercent / 100.0f, logarithmic);
}

void GainPilotProcessor::updateMeterResetLatch() {
  const bool resetHigh = parameters_.get(ParamId::meterReset) >= 0.5f;
  if (resetPending_ || (resetHigh && !resetWasHigh_)) {
    resetPending_ = false;
    meterResetCount_ = (meterResetCount_ + 1) & 0xffffff;
    stereoInputMeter_.resetIntegrated();
    monoInputMeter_.resetIntegrated();
    stereoOutputMeter_.resetIntegrated();
    monoOutputMeter_.resetIntegrated();
    learnedStereoInputLevelLufs_ = parameters_.get(ParamId::inputLevel);
    learnedMonoInputLevelLufs_ = parameters_.get(ParamId::inputLevel);
    outputSupervisorLufs_ = -70.0f;
    outputSupervisorReady_ = false;
    slowTargetGainDb_ = 0.0f;
    outputFeedbackSettleHopsRemaining_ = kOutputFeedbackSettleHops;
    currentMeterValue_ = -70.0f;
    currentGainReductionDb_ = 0.0f;
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
  const bool speechMode = speechModeEnabled();
  const bool monoMode = monoModeEnabled();
  if (monoMode != controlMonoMode_) {
    controlMonoMode_ = monoMode;
    slowTargetGainDb_ = slowGainDb_;
    outputSupervisorLufs_ = -70.0f;
    outputSupervisorReady_ = false;
    outputFeedbackSettleHopsRemaining_ = kOutputFeedbackSettleHops;
  }
  const auto meterMode = static_cast<MeterMode>(static_cast<int>(parameters_.get(ParamId::meterMode)));
  const bool fixedGainOnly =
      parameters_.get(ParamId::correctionHigh) <= 0.0f && parameters_.get(ParamId::correctionLow) <= 0.0f;
  const float inputTrimLinear = dbToLinear(parameters_.get(ParamId::inputTrim));
  if (fixedGainOnly) {
    fastTargetGainDb_ = 0.0f;
    mediumTargetGainDb_ = 0.0f;
    slowTargetGainDb_ = 0.0f;
  }

  for (std::size_t frame = 0; frame < buffer.frames; ++frame) {
    stereoInputFrame_[0] = buffer.inputs[0][frame] * inputTrimLinear;
    stereoInputFrame_[1] =
        channelCount_ >= 2 ? buffer.inputs[1][frame] * inputTrimLinear : 0.0f;
    monoInputFrame_[0] =
        channelCount_ >= 2 ? 0.5f * (stereoInputFrame_[0] + stereoInputFrame_[1]) : stereoInputFrame_[0];

    const float targetMonoMix = monoMode ? 1.0f : 0.0f;
    if (monoMix_ < targetMonoMix) {
      monoMix_ = std::min(targetMonoMix, monoMix_ + monoMixStep_);
    } else if (monoMix_ > targetMonoMix) {
      monoMix_ = std::max(targetMonoMix, monoMix_ - monoMixStep_);
    }
    for (std::size_t channel = 0; channel < channelCount_; ++channel) {
      frameInput_[channel] =
          stereoInputFrame_[channel] + monoMix_ * (monoInputFrame_[0] - stereoInputFrame_[channel]);
    }

    const bool stereoInputControlHop = stereoInputMeter_.processFrame(stereoInputFrame_.data());
    const bool monoInputControlHop = monoInputMeter_.processFrame(monoInputFrame_.data());
    const bool inputControlHop = monoMode ? monoInputControlHop : stereoInputControlHop;
    if (!fixedGainOnly && inputControlHop) {
      if (parameters_.get(ParamId::referenceMode) < 0.5f &&
          stereoInputMeter_.integratedBlockCount() >= kInputLevelReadyBlocks) {
        learnedStereoInputLevelLufs_ = smoothTowards(learnedStereoInputLevelLufs_,
                                                     stereoInputMeter_.integratedLufs(),
                                                     inputLevelAttackCoeff_,
                                                     inputLevelReleaseCoeff_);
      }
      if (parameters_.get(ParamId::referenceMode) < 0.5f &&
          monoInputMeter_.integratedBlockCount() >= kInputLevelReadyBlocks) {
        learnedMonoInputLevelLufs_ = smoothTowards(learnedMonoInputLevelLufs_,
                                                   monoInputMeter_.integratedLufs(),
                                                   inputLevelAttackCoeff_,
                                                   inputLevelReleaseCoeff_);
      }

      const float inputSlowDetectorLufs = inputMeter().controlLufs();
      const float inputFastDetectorLufs = inputMeter().momentaryLufs();
      const float inputReferenceLufs = effectiveInputLevelLufs();
      const float highMix = correctionMix(true);
      const float lowMix = correctionMix(false);
      const float targetLevel = parameters_.get(ParamId::targetLevel);
      const float mediumMomentaryWeight = speechMode ? kSpeechMediumMomentaryWeight : kMediumMomentaryWeight;
      const float mediumDetectorLufs =
          inputSlowDetectorLufs + mediumMomentaryWeight * (inputFastDetectorLufs - inputSlowDetectorLufs);
      updateAutoHoldGate(inputSlowDetectorLufs);

      const float fastExcessDb = inputFastDetectorLufs - std::max(inputSlowDetectorLufs, inputReferenceLufs);
      const float fastTransientThresholdDb = speechMode ? kSpeechFastTransientThresholdDb : kFastTransientThresholdDb;
      const float fastMaxAttenuationDb = speechMode ? kSpeechFastMaxAttenuationDb : kFastMaxAttenuationDb;
      fastTargetGainDb_ =
          -highMix * std::clamp(fastExcessDb - fastTransientThresholdDb, 0.0f, fastMaxAttenuationDb);

      const float mediumErrorDb = inputReferenceLufs - mediumDetectorLufs;
      if (mediumErrorDb >= 0.0f) {
        mediumTargetGainDb_ = std::clamp(mediumErrorDb * lowMix, 0.0f, kMediumMaxGainDb);
      } else {
        mediumTargetGainDb_ = -std::clamp(-mediumErrorDb * highMix, 0.0f, kMediumMaxAttenuationDb);
      }

      const float baselineTargetGainDb =
          std::clamp(fixedGainDb(), minimumGainDb(), parameters_.get(ParamId::maxGain));
      const float feedForwardGainDb = baselineGainDb_ + fastGainDb_ + mediumGainDb_;
      const float feedForwardTargetGainDb = baselineTargetGainDb + fastTargetGainDb_ + mediumTargetGainDb_;
      // Do not integrate a stale three-second output window while the faster
      // feed-forward branches are still responding to a programme transition.
      if (std::fabs(feedForwardTargetGainDb - feedForwardGainDb) > kFeedForwardSettleToleranceDb) {
        outputFeedbackSettleHopsRemaining_ = kOutputFeedbackSettleHops;
        outputSupervisorReady_ = false;
      } else if (outputFeedbackSettleHopsRemaining_ > 0) {
        --outputFeedbackSettleHopsRemaining_;
      }

      if (outputMeter().shortTermReady() && autoHoldGateOpen_ && outputFeedbackSettleHopsRemaining_ == 0) {
        const float outputShortTermLufs = outputMeter().shortTermLufs();
        if (!outputSupervisorReady_) {
          outputSupervisorLufs_ = outputShortTermLufs;
          outputSupervisorReady_ = true;
        } else {
          const float supervisorCoeff = std::exp(-kControlHopSeconds / kOutputSupervisorSeconds);
          outputSupervisorLufs_ =
              supervisorCoeff * outputSupervisorLufs_ + (1.0f - supervisorCoeff) * outputShortTermLufs;
        }

        const float shortTermErrorDb = targetLevel - outputShortTermLufs;
        const float supervisorErrorDb = targetLevel - outputSupervisorLufs_;
        const float servoErrorDb =
            (1.0f - kOutputSupervisorWeight) * shortTermErrorDb + kOutputSupervisorWeight * supervisorErrorDb;
        const float servoMix = servoErrorDb < 0.0f ? highMix : lowMix;
        const float servoSeconds = speechMode ? kSpeechOutputServoSeconds : kOutputServoSeconds;
        const float servoStepDb = std::clamp(servoErrorDb * servoMix * servoBrake(inputFastDetectorLufs) *
                                                 (kControlHopSeconds / servoSeconds),
                                             -kMaxServoStepDb,
                                             kMaxServoStepDb);
        slowTargetGainDb_ += servoStepDb;
      }

      const float totalWithoutServoDb = baselineGainDb_ + fastTargetGainDb_ + mediumTargetGainDb_;
      // Clamp the integrator itself, rather than only its audible result, so it
      // cannot wind up behind either gain boundary.
      slowTargetGainDb_ = std::clamp(slowTargetGainDb_,
                                     minimumGainDb() - totalWithoutServoDb,
                                     parameters_.get(ParamId::maxGain) - totalWithoutServoDb);
    }

    const bool inputActive =
        inputMeter().momentaryReady() && inputMeter().momentaryLufs() >= freezeThresholdLufs();
    if (inputActive || fixedGainOnly) {
      const float baselineTargetGainDb =
          std::clamp(fixedGainDb(), minimumGainDb(), parameters_.get(ParamId::maxGain));
      baselineGainDb_ = smoothTowards(
          baselineGainDb_, baselineTargetGainDb, baselineSmoothingCoeff_, baselineSmoothingCoeff_);
    }

    fastGainDb_ = smoothTowards(fastGainDb_, fastTargetGainDb_, fastAttackCoeff_, fastReleaseCoeff_);
    mediumGainDb_ = smoothTowards(
        mediumGainDb_,
        mediumTargetGainDb_,
        speechMode ? speechMediumAttackCoeff_ : mediumAttackCoeff_,
        speechMode ? speechMediumReleaseCoeff_ : mediumReleaseCoeff_);
    slowGainDb_ = smoothTowards(
        slowGainDb_,
        slowTargetGainDb_,
        speechMode ? speechSlowAttackCoeff_ : slowAttackCoeff_,
        speechMode ? speechSlowReleaseCoeff_ : slowReleaseCoeff_);
    float totalGainDb = std::clamp(
        baselineGainDb_ + fastGainDb_ + mediumGainDb_ + slowGainDb_,
        minimumGainDb(),
        parameters_.get(ParamId::maxGain));
    if (!inputActive && !fixedGainOnly) {
      const float appliedGainDb = std::min(totalGainDb, 0.0f);
      if (totalGainDb > 0.0f) {
        // Reconcile every positive hidden state to the audible zero-gain state.
        // On resume, the normal smoothers rebuild gain instead of revealing a
        // precharged controller in one sample.
        baselineGainDb_ = 0.0f;
        fastGainDb_ = 0.0f;
        mediumGainDb_ = 0.0f;
        slowGainDb_ = 0.0f;
        fastTargetGainDb_ = 0.0f;
        mediumTargetGainDb_ = 0.0f;
        slowTargetGainDb_ = 0.0f;
      } else {
        fastTargetGainDb_ = fastGainDb_;
        mediumTargetGainDb_ = mediumGainDb_;
        slowTargetGainDb_ = slowGainDb_;
      }
      outputFeedbackSettleHopsRemaining_ = kOutputFeedbackSettleHops;
      outputSupervisorReady_ = false;
      totalGainDb = appliedGainDb;
    }
    currentAppliedGainDb_ = totalGainDb;
    limiter_.processFrame(frameInput_.data(), frameOutput_.data(), dbToLinear(totalGainDb));
    stereoOutputFrame_[0] = frameOutput_[0];
    stereoOutputFrame_[1] = channelCount_ >= 2 ? frameOutput_[1] : 0.0f;
    monoOutputFrame_[0] =
        channelCount_ >= 2 ? 0.5f * (stereoOutputFrame_[0] + stereoOutputFrame_[1]) : stereoOutputFrame_[0];
    (void)stereoOutputMeter_.processFrame(stereoOutputFrame_.data());
    (void)monoOutputMeter_.processFrame(monoOutputFrame_.data());

    for (std::size_t channel = 0; channel < channelCount_; ++channel) {
      buffer.outputs[channel][frame] = frameOutput_[channel];
    }
  }

  currentMeterValue_ = inputMeter().loudnessForMode(meterMode);
  currentGainReductionDb_ =
      std::max(0.0f,
               -(std::min(0.0f, fastGainDb_) + std::min(0.0f, mediumGainDb_) + std::min(0.0f, slowGainDb_)));
}

float GainPilotProcessor::currentMeterValue() const {
  return currentMeterValue_;
}

float GainPilotProcessor::currentLatencySamples() const {
  return currentLatencySamples_;
}

float GainPilotProcessor::currentAppliedGainDb() const {
  return currentAppliedGainDb_;
}

float GainPilotProcessor::currentInputShortTermLufs() const {
  return inputMeter().shortTermLufs();
}

float GainPilotProcessor::currentInputIntegratedLufs() const {
  return inputMeter().integratedLufs();
}

float GainPilotProcessor::currentOutputIntegratedLufs() const {
  return outputMeter().integratedLufs();
}

float GainPilotProcessor::currentOutputShortTermLufs() const {
  return outputMeter().shortTermLufs();
}

float GainPilotProcessor::currentGainReductionDb() const {
  return currentGainReductionDb_;
}

}  // namespace gainpilot::dsp
