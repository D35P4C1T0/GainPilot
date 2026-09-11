#include "gainpilot/dsp/true_peak_limiter.hpp"

#include <algorithm>
#include <cmath>

namespace gainpilot::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Allows for finite reconstruction length, phase spacing and gain modulation.
// 0.1 dB alone failed independent 16x reconstruction on broadband bursts.
// Retain 0.3 dB so reconstruction/filter differences still leave headroom.
constexpr float kPeakSafetyDb = 0.3f;

float dbToLinear(float valueDb) { return std::pow(10.0f, valueDb / 20.0f); }

} // namespace

void TruePeakLimiter::prepare(double sampleRate, std::size_t channelCount,
                              double latencySeconds) {
  sampleRate_ = sampleRate;
  channelCount_ = channelCount;
  lookaheadSamples_ = std::max<std::size_t>(
      kFilterTaps,
      static_cast<std::size_t>(std::ceil(sampleRate_ * latencySeconds)));
  delayLines_.assign(channelCount_,
                     std::vector<float>(lookaheadSamples_ + 1, 0.0f));
  sampleHistory_.assign(channelCount_, {});
  requiredGainWindow_.resize(lookaheadSamples_ + kFilterTaps + 2);
  const auto makeInterpolation = [](auto &filters) {
    const auto taps = filters[0].size();
    for (std::size_t phase = 1; phase < kPhases; ++phase) {
      double sum = 0.0;
      for (std::size_t tap = 0; tap < taps; ++tap) {
        const double offset =
            static_cast<double>(tap) -
            (taps / 2 - 1 + static_cast<double>(phase) / kPhases);
        const double sinc = std::sin(kPi * offset) / (kPi * offset);
        const double window = 0.5 + 0.5 * std::cos(2.0 * kPi * offset / taps);
        filters[phase - 1][tap] = static_cast<float>(sinc * window);
        sum += sinc * window;
      }
      for (float &coefficient : filters[phase - 1])
        coefficient = static_cast<float>(coefficient / sum);
    }
  };
  makeInterpolation(interpolation_);
  makeInterpolation(shortInterpolation_);
  makeInterpolation(mediumInterpolation_);
  // A reconstruction with a transition band below Nyquist can ring higher than
  // full-band sinc interpolation on short, broadband bursts.
  for (size_t phase = 0; phase < kPhases; ++phase) {
    double sum = 0;
    for (size_t tap = 0; tap < kFilterTaps; ++tap) {
      const double x = static_cast<double>(tap) -
          (kFilterTaps / 2 - 1 + static_cast<double>(phase) / kPhases);
      const double value = (x == 0 ? .95 : std::sin(kPi * .95 * x) / (kPi * x)) *
          (.5 + .5 * std::cos(2 * kPi * x / kFilterTaps));
      bandlimitedInterpolation_[phase][tap] = static_cast<float>(value);
      sum += value;
    }
    for (float& value : bandlimitedInterpolation_[phase]) value /= static_cast<float>(sum);
  }

  const float releaseSeconds = 0.100f;
  releaseCoeff_ =
      std::exp(-1.0f / (releaseSeconds * static_cast<float>(sampleRate_)));
  reset();
}

void TruePeakLimiter::reset() {
  sampleIndex_ = 0;
  writeIndex_ = 0;
  envelopeGain_ = 1.0f;
  queueHead_ = queueSize_ = historyWrite_ = 0;
  for (auto &delayLine : delayLines_) {
    std::fill(delayLine.begin(), delayLine.end(), 0.0f);
  }
  for (auto &history : sampleHistory_) {
    history.fill(0.0f);
  }
}

void TruePeakLimiter::setCeilingDb(float ceilingDb) {
  ceilingLinear_ = dbToLinear(ceilingDb - kPeakSafetyDb);
}

std::size_t TruePeakLimiter::latencySamples() const {
  return lookaheadSamples_;
}

void TruePeakLimiter::pushHistorySample(std::size_t channel, float sample) {
  auto &history = sampleHistory_[channel];
  history[historyWrite_] = history[historyWrite_ + kFilterTaps] = sample;
}

float TruePeakLimiter::estimatePeak(std::size_t channel) const {
  const auto &history = sampleHistory_[channel];
  const float *samples = history.data() + (historyWrite_ + 1) % kFilterTaps;
  float peak = std::fabs(history[historyWrite_]);
  const auto measure = [&](const auto &filters) {
    const auto taps = filters[0].size();
    for (const auto &phase : filters) {
      float reconstructed = 0.0f;
      for (std::size_t tap = 0; tap < taps; ++tap)
        reconstructed += phase[tap] * samples[kFilterTaps - taps + tap];
      peak = std::max(peak, std::fabs(reconstructed));
    }
  };
  measure(interpolation_);
  measure(shortInterpolation_);
  measure(mediumInterpolation_);
  measure(bandlimitedInterpolation_);
  return peak;
}

void TruePeakLimiter::pushRequiredGain(float inversePeak) {
  const auto capacity = requiredGainWindow_.size();
  // Retain a detected peak until its complete FIR support has left the delay.
  const auto horizon = lookaheadSamples_ + kFilterTaps;
  while (queueSize_ > 0 && sampleIndex_ > horizon &&
         requiredGainWindow_[queueHead_].index < sampleIndex_ - horizon) {
    queueHead_ = (queueHead_ + 1) % capacity;
    --queueSize_;
  }
  while (queueSize_ > 0 &&
         inversePeak <=
             requiredGainWindow_[(queueHead_ + queueSize_ - 1) % capacity].gain)
    --queueSize_;
  requiredGainWindow_[(queueHead_ + queueSize_) % capacity] = {sampleIndex_,
                                                               inversePeak};
  ++queueSize_;
}

void TruePeakLimiter::processFrame(const float *input, float *output,
                                   float preGainLinear) {
  float instantaneousRequiredGain = 1.0e9f;

  for (std::size_t channel = 0; channel < channelCount_; ++channel) {
    const float scaled = input[channel] * preGainLinear;
    pushHistorySample(channel, scaled);
    instantaneousRequiredGain =
        std::min(instantaneousRequiredGain,
                 1.0f / std::max(estimatePeak(channel), 1.0e-9f));
  }

  pushRequiredGain(instantaneousRequiredGain);
  historyWrite_ = (historyWrite_ + 1) % kFilterTaps;
  const float requiredGain =
      std::min(1.0f, ceilingLinear_ * requiredGainWindow_[queueHead_].gain);

  if (requiredGain < envelopeGain_) {
    envelopeGain_ = requiredGain;
  } else {
    envelopeGain_ =
        requiredGain + releaseCoeff_ * (envelopeGain_ - requiredGain);
  }

  // Rounding during release must never override the peak constraint, including
  // when the host lowers the ceiling with audio already in the lookahead
  // buffer.
  envelopeGain_ = std::min(envelopeGain_, requiredGain);

  const std::size_t readIndex = (writeIndex_ + 1) % (lookaheadSamples_ + 1);

  for (std::size_t channel = 0; channel < channelCount_; ++channel) {
    const float scaled = input[channel] * preGainLinear;
    delayLines_[channel][writeIndex_] = scaled;
    output[channel] = delayLines_[channel][readIndex] * envelopeGain_;
  }

  writeIndex_ = readIndex;
  ++sampleIndex_;
}

} // namespace gainpilot::dsp
