#include "gainpilot/dsp/loudness_meter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gainpilot::dsp {

namespace {

constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kLoudnessOffset = -0.691;
constexpr double kFloorEnergy = 1.0e-12;

#if GAINPILOT_USE_LIBEBUR128
constexpr int kShortTermMode = EBUR128_MODE_S;
constexpr int kIntegratedMode = EBUR128_MODE_I;

float sanitizeLibebur128Lufs(double value) {
  if (!std::isfinite(value)) {
    return static_cast<float>(kAbsoluteGateLufs);
  }
  return static_cast<float>(value);
}

void configureChannelMap(::ebur128_state* state, std::size_t channelCount) {
  if (state == nullptr) {
    return;
  }

  if (channelCount >= 1) {
    ebur128_set_channel(state, 0, channelCount == 1 ? EBUR128_DUAL_MONO : EBUR128_LEFT);
  }
  if (channelCount >= 2) {
    ebur128_set_channel(state, 1, EBUR128_RIGHT);
  }
  for (std::size_t channel = 2; channel < channelCount; ++channel) {
    ebur128_set_channel(state, static_cast<unsigned int>(channel), EBUR128_UNUSED);
  }
}
#else
double loudnessToEnergy(double lufs) {
  return std::pow(10.0, (lufs - kLoudnessOffset) / 10.0);
}
#endif

}  // namespace

LoudnessMeter::~LoudnessMeter() {
#if GAINPILOT_USE_LIBEBUR128
  if (shortTermState_ != nullptr) {
    ebur128_destroy(&shortTermState_);
  }
  if (integratedState_ != nullptr) {
    ebur128_destroy(&integratedState_);
  }
#endif
}

void LoudnessMeter::prepare(double sampleRate, std::size_t channelCount) {
  sampleRate_ = sampleRate;
  channelCount_ = channelCount;
  momentarySamples_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(sampleRate_ * 0.4)));
  shortTermSamples_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(sampleRate_ * 3.0)));
  hopSamples_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(sampleRate_ * 0.1)));

#if GAINPILOT_USE_LIBEBUR128
  if (shortTermState_ != nullptr) {
    ebur128_destroy(&shortTermState_);
  }
  if (integratedState_ != nullptr) {
    ebur128_destroy(&integratedState_);
  }

  shortTermState_ =
      ebur128_init(static_cast<unsigned int>(channelCount_), static_cast<unsigned long>(sampleRate_), kShortTermMode);
  integratedState_ =
      ebur128_init(static_cast<unsigned int>(channelCount_), static_cast<unsigned long>(sampleRate_), kIntegratedMode);
  configureChannelMap(shortTermState_, channelCount_);
  configureChannelMap(integratedState_, channelCount_);
  interleavedFrame_.assign(channelCount_, 0.0f);
#else
  momentaryWindow_.assign(momentarySamples_, 0.0);
  shortTermWindow_.assign(shortTermSamples_, 0.0);
  weightingFilter_.prepare(sampleRate_, channelCount_);
#endif

  reset();
}

void LoudnessMeter::reset() {
#if GAINPILOT_USE_LIBEBUR128
  if (shortTermState_ != nullptr) {
    ebur128_destroy(&shortTermState_);
  }
  if (integratedState_ != nullptr) {
    ebur128_destroy(&integratedState_);
  }

  shortTermState_ =
      ebur128_init(static_cast<unsigned int>(channelCount_), static_cast<unsigned long>(sampleRate_), kShortTermMode);
  integratedState_ =
      ebur128_init(static_cast<unsigned int>(channelCount_), static_cast<unsigned long>(sampleRate_), kIntegratedMode);
  configureChannelMap(shortTermState_, channelCount_);
  configureChannelMap(integratedState_, channelCount_);
#else
  weightingFilter_.reset();
  momentaryIndex_ = 0;
  shortTermIndex_ = 0;
  momentaryEnergySum_ = 0.0;
  shortTermEnergySum_ = 0.0;
  std::fill(momentaryWindow_.begin(), momentaryWindow_.end(), 0.0);
  std::fill(shortTermWindow_.begin(), shortTermWindow_.end(), 0.0);
#endif

  sampleCounter_ = 0;
  controlLufs_ = static_cast<float>(kAbsoluteGateLufs);
  resetIntegrated();
}

void LoudnessMeter::resetIntegrated() {
#if GAINPILOT_USE_LIBEBUR128
  if (integratedState_ != nullptr) {
    ebur128_destroy(&integratedState_);
  }
  integratedState_ =
      ebur128_init(static_cast<unsigned int>(channelCount_), static_cast<unsigned long>(sampleRate_), kIntegratedMode);
  configureChannelMap(integratedState_, channelCount_);
  integratedSampleCounter_ = 0;
  integratedBlockCount_ = 0;
#else
  integratedBlocks_.clear();
#endif
}

#if !GAINPILOT_USE_LIBEBUR128
void LoudnessMeter::pushWindowSample(std::vector<double>& window,
                                     std::size_t& index,
                                     double sample,
                                     double& runningSum) {
  runningSum -= window[index];
  window[index] = sample;
  runningSum += sample;
  index = (index + 1) % window.size();
}
#endif

bool LoudnessMeter::processFrame(const float* frame) {
#if GAINPILOT_USE_LIBEBUR128
  for (std::size_t channel = 0; channel < channelCount_; ++channel) {
    interleavedFrame_[channel] = frame[channel];
  }
  if (shortTermState_ != nullptr) {
    ebur128_add_frames_float(shortTermState_, interleavedFrame_.data(), 1);
  }
  if (integratedState_ != nullptr) {
    ebur128_add_frames_float(integratedState_, interleavedFrame_.data(), 1);
  }
  ++sampleCounter_;
  ++integratedSampleCounter_;

  if (sampleCounter_ % hopSamples_ == 0) {
    if (integratedSampleCounter_ >= momentarySamples_) {
      ++integratedBlockCount_;
    }
    if (shortTermReady()) {
      controlLufs_ = shortTermLufs();
    } else if (momentaryReady()) {
      controlLufs_ = momentaryLufs();
    } else {
      controlLufs_ = static_cast<float>(kAbsoluteGateLufs);
    }
    return true;
  }

  return false;
#else
  double weightedEnergy = 0.0;
  for (std::size_t channel = 0; channel < channelCount_; ++channel) {
    const float weighted = weightingFilter_.processSample(channel, frame[channel]);
    weightedEnergy += static_cast<double>(weighted) * static_cast<double>(weighted);
  }

  pushWindowSample(momentaryWindow_, momentaryIndex_, weightedEnergy, momentaryEnergySum_);
  pushWindowSample(shortTermWindow_, shortTermIndex_, weightedEnergy, shortTermEnergySum_);

  ++sampleCounter_;
  if (sampleCounter_ % hopSamples_ == 0) {
    updateIntegratedState();
    if (shortTermReady()) {
      controlLufs_ = shortTermLufs();
    } else if (momentaryReady()) {
      controlLufs_ = momentaryLufs();
    } else {
      controlLufs_ = static_cast<float>(kAbsoluteGateLufs);
    }
    return true;
  }

  return false;
#endif
}

#if !GAINPILOT_USE_LIBEBUR128
void LoudnessMeter::updateIntegratedState() {
  if (!momentaryReady()) {
    return;
  }
  const double meanMomentaryEnergy = momentaryEnergySum_ / static_cast<double>(momentarySamples_);
  integratedBlocks_.push_back(std::max(meanMomentaryEnergy, kFloorEnergy));
}

float LoudnessMeter::loudnessFromEnergy(double meanEnergy) const {
  return static_cast<float>(kLoudnessOffset + 10.0 * std::log10(std::max(meanEnergy, kFloorEnergy)));
}
#else
float LoudnessMeter::loudnessFromEnergy(double meanEnergy) const {
  return static_cast<float>(meanEnergy);
}
#endif

float LoudnessMeter::momentaryLufs() const {
#if GAINPILOT_USE_LIBEBUR128
  double value = -HUGE_VAL;
  if (shortTermState_ == nullptr || ebur128_loudness_momentary(shortTermState_, &value) != EBUR128_SUCCESS) {
    return static_cast<float>(kAbsoluteGateLufs);
  }
  return sanitizeLibebur128Lufs(value);
#else
  return loudnessFromEnergy(momentaryEnergySum_ / static_cast<double>(momentarySamples_));
#endif
}

float LoudnessMeter::shortTermLufs() const {
#if GAINPILOT_USE_LIBEBUR128
  double value = -HUGE_VAL;
  if (shortTermState_ == nullptr || ebur128_loudness_shortterm(shortTermState_, &value) != EBUR128_SUCCESS) {
    return static_cast<float>(kAbsoluteGateLufs);
  }
  return sanitizeLibebur128Lufs(value);
#else
  return loudnessFromEnergy(shortTermEnergySum_ / static_cast<double>(shortTermSamples_));
#endif
}

float LoudnessMeter::integratedLufs() const {
#if GAINPILOT_USE_LIBEBUR128
  double value = -HUGE_VAL;
  if (integratedState_ == nullptr || ebur128_loudness_global(integratedState_, &value) != EBUR128_SUCCESS) {
    return static_cast<float>(kAbsoluteGateLufs);
  }
  return sanitizeLibebur128Lufs(value);
#else
  if (integratedBlocks_.empty()) {
    return static_cast<float>(kAbsoluteGateLufs);
  }

  const double absoluteGateEnergy = loudnessToEnergy(kAbsoluteGateLufs);
  double absoluteSum = 0.0;
  std::size_t absoluteCount = 0;

  for (const double block : integratedBlocks_) {
    if (block >= absoluteGateEnergy) {
      absoluteSum += block;
      ++absoluteCount;
    }
  }

  if (absoluteCount == 0) {
    return static_cast<float>(kAbsoluteGateLufs);
  }

  const double ungatedMean = absoluteSum / static_cast<double>(absoluteCount);
  const double relativeGate = loudnessToEnergy(loudnessFromEnergy(ungatedMean) - 10.0f);

  double gatedSum = 0.0;
  std::size_t gatedCount = 0;

  for (const double block : integratedBlocks_) {
    if (block >= absoluteGateEnergy && block >= relativeGate) {
      gatedSum += block;
      ++gatedCount;
    }
  }

  if (gatedCount == 0) {
    return static_cast<float>(kAbsoluteGateLufs);
  }

  return loudnessFromEnergy(gatedSum / static_cast<double>(gatedCount));
#endif
}

std::size_t LoudnessMeter::integratedBlockCount() const {
#if GAINPILOT_USE_LIBEBUR128
  return integratedBlockCount_;
#else
  return integratedBlocks_.size();
#endif
}

bool LoudnessMeter::momentaryReady() const {
  return sampleCounter_ >= momentarySamples_;
}

bool LoudnessMeter::shortTermReady() const {
  return sampleCounter_ >= shortTermSamples_;
}

float LoudnessMeter::controlLufs() const {
  return controlLufs_;
}

float LoudnessMeter::loudnessForMode(MeterMode mode) const {
  switch (mode) {
    case MeterMode::momentary:
      return momentaryLufs();
    case MeterMode::shortTerm:
      return shortTermLufs();
    case MeterMode::integrated:
      return integratedLufs();
  }

  return momentaryLufs();
}

}  // namespace gainpilot::dsp
