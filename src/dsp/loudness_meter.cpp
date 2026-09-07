#include "gainpilot/dsp/loudness_meter.hpp"

#include <algorithm>
#include <cmath>

namespace gainpilot::dsp {
namespace {
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kLoudnessOffset = -0.691;
constexpr double kAbsoluteGateEnergy = 1.172465304582296e-7;
}

void LoudnessMeter::prepare(double sampleRate, std::size_t channelCount) {
  channelCount_ = channelCount;
  momentarySamples_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(sampleRate * 0.4)));
  shortTermSamples_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(sampleRate * 3.0)));
  hopSamples_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(sampleRate * 0.1)));
  momentaryWindow_.assign(momentarySamples_, 0.0);
  shortTermWindow_.assign(shortTermSamples_, 0.0);
  integratedHistogram_.resize(kHistogramBins);
  weightingFilter_.prepare(sampleRate, channelCount_);
  reset();
}

void LoudnessMeter::reset() {
  weightingFilter_.reset();
  momentaryIndex_ = shortTermIndex_ = sampleCounter_ = 0;
  momentaryEnergySum_ = shortTermEnergySum_ = 0.0;
  momentaryLufs_ = shortTermLufs_ = controlLufs_ = -70.0f;
  std::fill(momentaryWindow_.begin(), momentaryWindow_.end(), 0.0);
  std::fill(shortTermWindow_.begin(), shortTermWindow_.end(), 0.0);
  resetIntegrated();
}

void LoudnessMeter::resetIntegrated() {
  std::fill(integratedHistogram_.begin(), integratedHistogram_.end(), EnergyBin{});
  integratedSampleCounter_ = integratedBlockCount_ = absoluteBlockCount_ = 0;
  absoluteEnergySum_ = 0.0;
  integratedLufs_ = -70.0f;
}

void LoudnessMeter::pushWindowSample(std::vector<double>& window,
                                    std::size_t& index, double sample, double& runningSum) {
  runningSum += sample - window[index];
  window[index] = sample;
  index = (index + 1) % window.size();
}

bool LoudnessMeter::processFrame(const float* frame) {
  double weightedEnergy = 0.0;
  for (std::size_t channel = 0; channel < channelCount_; ++channel) {
    const float weighted = weightingFilter_.processSample(channel, frame[channel]);
    weightedEnergy += static_cast<double>(weighted) * weighted;
  }
  pushWindowSample(momentaryWindow_, momentaryIndex_, weightedEnergy, momentaryEnergySum_);
  pushWindowSample(shortTermWindow_, shortTermIndex_, weightedEnergy, shortTermEnergySum_);
  ++sampleCounter_;
  ++integratedSampleCounter_;
  if (sampleCounter_ % hopSamples_ != 0)
    return false;

  momentaryLufs_ = loudnessFromEnergy(momentaryEnergySum_ / momentarySamples_);
  shortTermLufs_ = loudnessFromEnergy(shortTermEnergySum_ / shortTermSamples_);
  updateIntegratedState();
  controlLufs_ = shortTermReady() ? shortTermLufs_ : (momentaryReady() ? momentaryLufs_ : -70.0f);
  return true;
}

void LoudnessMeter::updateIntegratedState() {
  // A reset must collect a complete fresh block before reusing the rolling window.
  if (integratedSampleCounter_ < momentarySamples_)
    return;
  ++integratedBlockCount_;
  const double energy = std::max(0.0, momentaryEnergySum_ / momentarySamples_);
  if (energy >= kAbsoluteGateEnergy) {
    const double binPosition = (loudnessFromEnergy(energy) - kAbsoluteGateLufs) * 100.0;
    const auto index = static_cast<std::size_t>(std::clamp(binPosition, 0.0, static_cast<double>(kHistogramBins - 1)));
    integratedHistogram_[index].sum += energy;
    ++integratedHistogram_[index].count;
    absoluteEnergySum_ += energy;
    ++absoluteBlockCount_;
  }
  if (absoluteBlockCount_ == 0)
    return;

  const double gateLufs = loudnessFromEnergy(absoluteEnergySum_ / absoluteBlockCount_) - 10.0;
  const auto firstBin = static_cast<std::size_t>(std::clamp(
      std::round((gateLufs - kAbsoluteGateLufs) * 100.0), 0.0,
      static_cast<double>(kHistogramBins - 1)));
  double sum = 0.0;
  std::uint64_t count = 0;
  for (std::size_t i = firstBin; i < integratedHistogram_.size(); ++i) {
    sum += integratedHistogram_[i].sum;
    count += integratedHistogram_[i].count;
  }
  integratedLufs_ = count > 0 ? loudnessFromEnergy(sum / count) : -70.0f;
}

float LoudnessMeter::loudnessFromEnergy(double energy) {
  return static_cast<float>(std::max(kAbsoluteGateLufs,
      kLoudnessOffset + 10.0 * std::log10(std::max(energy, 1.0e-12))));
}
float LoudnessMeter::momentaryLufs() const { return momentaryLufs_; }
float LoudnessMeter::shortTermLufs() const { return shortTermLufs_; }
float LoudnessMeter::integratedLufs() const { return integratedLufs_; }
std::size_t LoudnessMeter::integratedBlockCount() const { return integratedBlockCount_; }
bool LoudnessMeter::momentaryReady() const { return sampleCounter_ >= momentarySamples_; }
bool LoudnessMeter::shortTermReady() const { return sampleCounter_ >= shortTermSamples_; }
float LoudnessMeter::controlLufs() const { return controlLufs_; }
float LoudnessMeter::loudnessForMode(MeterMode mode) const {
  switch (mode) {
    case MeterMode::momentary: return momentaryLufs_;
    case MeterMode::shortTerm: return shortTermLufs_;
    case MeterMode::integrated: return integratedLufs_;
  }
  return momentaryLufs_;
}
}  // namespace gainpilot::dsp
