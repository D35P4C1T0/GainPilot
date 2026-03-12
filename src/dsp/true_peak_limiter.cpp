#include "gainpilot/dsp/true_peak_limiter.hpp"

#include <algorithm>
#include <cmath>

namespace gainpilot::dsp {

namespace {

constexpr std::size_t kOversampleFactor = 8;

float dbToLinear(float valueDb) {
  return std::pow(10.0f, valueDb / 20.0f);
}

float catmullRom(float p0, float p1, float p2, float p3, float t) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

}  // namespace

void TruePeakLimiter::prepare(double sampleRate, std::size_t channelCount, double latencySeconds) {
  sampleRate_ = sampleRate;
  channelCount_ = channelCount;
  lookaheadSamples_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(sampleRate_ * latencySeconds)));
  delayLines_.assign(channelCount_, std::vector<float>(lookaheadSamples_ + 1, 0.0f));
  sampleHistory_.assign(channelCount_, std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
  sampleHistoryCount_.assign(channelCount_, 0);

  const float attackSeconds = 0.0015f;
  const float releaseSeconds = 0.100f;
  attackCoeff_ = std::exp(-1.0f / (attackSeconds * static_cast<float>(sampleRate_)));
  releaseCoeff_ = std::exp(-1.0f / (releaseSeconds * static_cast<float>(sampleRate_)));
  reset();
}

void TruePeakLimiter::reset() {
  sampleIndex_ = 0;
  writeIndex_ = 0;
  envelopeGain_ = 1.0f;
  requiredGainWindow_.clear();
  for (auto& delayLine : delayLines_) {
    std::fill(delayLine.begin(), delayLine.end(), 0.0f);
  }
  for (auto& history : sampleHistory_) {
    history.fill(0.0f);
  }
  std::fill(sampleHistoryCount_.begin(), sampleHistoryCount_.end(), 0);
}

void TruePeakLimiter::setCeilingDb(float ceilingDb) {
  ceilingLinear_ = dbToLinear(ceilingDb);
}

std::size_t TruePeakLimiter::latencySamples() const {
  return lookaheadSamples_;
}

void TruePeakLimiter::pushHistorySample(std::size_t channel, float sample) {
  auto& history = sampleHistory_[channel];
  history[0] = history[1];
  history[1] = history[2];
  history[2] = history[3];
  history[3] = sample;
  sampleHistoryCount_[channel] = std::min<std::size_t>(sampleHistoryCount_[channel] + 1, history.size());
}

float TruePeakLimiter::estimatePeak(std::size_t channel) const {
  const auto& history = sampleHistory_[channel];
  const std::size_t count = sampleHistoryCount_[channel];
  if (count == 0) {
    return 0.0f;
  }
  if (count < 4) {
    const float previous = history[2];
    const float current = history[3];
    float peak = std::max(std::fabs(previous), std::fabs(current));
    for (std::size_t step = 1; step < kOversampleFactor; ++step) {
      const float t = static_cast<float>(step) / static_cast<float>(kOversampleFactor);
      peak = std::max(peak, std::fabs(previous + t * (current - previous)));
    }
    return peak;
  }

  float peak = std::max(std::fabs(history[1]), std::fabs(history[2]));
  for (std::size_t step = 1; step < kOversampleFactor; ++step) {
    const float t = static_cast<float>(step) / static_cast<float>(kOversampleFactor);
    peak = std::max(peak, std::fabs(catmullRom(history[0], history[1], history[2], history[3], t)));
  }
  return peak;
}

void TruePeakLimiter::pushRequiredGain(float requiredGain) {
  while (!requiredGainWindow_.empty() && requiredGain <= requiredGainWindow_.back().gain) {
    requiredGainWindow_.pop_back();
  }

  requiredGainWindow_.push_back({sampleIndex_, requiredGain});

  const std::size_t windowStart =
      sampleIndex_ > lookaheadSamples_ ? sampleIndex_ - lookaheadSamples_ : 0;
  while (!requiredGainWindow_.empty() && requiredGainWindow_.front().index < windowStart) {
    requiredGainWindow_.pop_front();
  }
}

void TruePeakLimiter::processFrame(const float* input, float* output, float preGainLinear) {
  float instantaneousRequiredGain = 1.0f;

  for (std::size_t channel = 0; channel < channelCount_; ++channel) {
    const float scaled = input[channel] * preGainLinear;
    pushHistorySample(channel, scaled);
    instantaneousRequiredGain =
        std::min(instantaneousRequiredGain, ceilingLinear_ / std::max(estimatePeak(channel), 1.0e-9f));
  }

  pushRequiredGain(instantaneousRequiredGain);
  const float requiredGain = requiredGainWindow_.empty() ? 1.0f : requiredGainWindow_.front().gain;

  if (requiredGain < envelopeGain_) {
    envelopeGain_ = requiredGain + attackCoeff_ * (envelopeGain_ - requiredGain);
  } else {
    envelopeGain_ = requiredGain + releaseCoeff_ * (envelopeGain_ - requiredGain);
  }

  const std::size_t readIndex = (writeIndex_ + 1) % (lookaheadSamples_ + 1);

  for (std::size_t channel = 0; channel < channelCount_; ++channel) {
    const float scaled = input[channel] * preGainLinear;
    delayLines_[channel][writeIndex_] = scaled;
    output[channel] = delayLines_[channel][readIndex] * envelopeGain_;
  }

  writeIndex_ = readIndex;
  ++sampleIndex_;
}

}  // namespace gainpilot::dsp
