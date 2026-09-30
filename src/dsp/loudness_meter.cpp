#include "gainpilot/dsp/loudness_meter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
  energyWindow_.assign(shortTermSamples_, 0.0);
  integratedHistogram_.resize(kHistogramBins);
  weightingFilter_.prepare(sampleRate, channelCount_);
  reset();
}

void LoudnessMeter::reset() {
  weightingFilter_.reset();
  shortTermIndex_ = sampleCounter_ = 0;
  momentaryEnergySum_ = shortTermEnergySum_ = 0.0;
  momentaryLufs_ = shortTermLufs_ = controlLufs_ = -70.0f;
  // Old ring entries are ignored until overwritten. Reset cost therefore does
  // not scale with sample rate or the length of the short-term window.
  resetIntegrated();
}

void LoudnessMeter::resetIntegrated() {
  // Only a group's first insertion after reset clears its bins (at most 128).
  // Stale groups are excluded from queries. Wraparound invalidates the small
  // fixed directory, never the full histogram or rolling energy storage.
  if (histogramGeneration_ == std::numeric_limits<std::uint64_t>::max()) {
    histogramGroups_.fill(HistogramGroup{});
    histogramGeneration_ = 1;
  } else {
    ++histogramGeneration_;
  }
  integratedSampleCounter_ = integratedBlockCount_ = absoluteBlockCount_ = 0;
  absoluteEnergySum_ = 0.0;
  integratedLufs_ = -70.0f;
}

bool LoudnessMeter::processFrame(const float* frame) {
  double weightedEnergy = 0.0;
  for (std::size_t channel = 0; channel < channelCount_; ++channel) {
    const float weighted = weightingFilter_.processSample(channel, frame[channel]);
    weightedEnergy += static_cast<double>(weighted) * weighted;
  }
  const std::size_t momentaryIndex = shortTermIndex_ >= momentarySamples_
      ? shortTermIndex_ - momentarySamples_
      : shortTermIndex_ + shortTermSamples_ - momentarySamples_;
  const double oldMomentary = sampleCounter_ >= momentarySamples_ ? energyWindow_[momentaryIndex] : 0.0;
  momentaryEnergySum_ += weightedEnergy - oldMomentary;
  const double oldShortTerm = sampleCounter_ >= shortTermSamples_ ? energyWindow_[shortTermIndex_] : 0.0;
  shortTermEnergySum_ += weightedEnergy - oldShortTerm;
  energyWindow_[shortTermIndex_] = weightedEnergy;
  if (++shortTermIndex_ == shortTermSamples_) shortTermIndex_ = 0;
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
    auto& group = histogramGroups_[index / kGroupBins];
    if (group.generation != histogramGeneration_) {
      const std::size_t begin = (index / kGroupBins) * kGroupBins;
      const std::size_t end = std::min(begin + kGroupBins, kHistogramBins);
      std::fill(integratedHistogram_.begin() + begin, integratedHistogram_.begin() + end, EnergyBin{});
      group.total = EnergyBin{};
      group.generation = histogramGeneration_;
    }
    integratedHistogram_[index].sum += energy;
    ++integratedHistogram_[index].count;
    group.total.sum += energy;
    ++group.total.count;
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
  const std::size_t firstGroup = firstBin / kGroupBins;
  if (histogramGroups_[firstGroup].generation == histogramGeneration_) {
    const std::size_t end = std::min((firstGroup + 1) * kGroupBins, kHistogramBins);
    for (std::size_t i = firstBin; i < end; ++i) {
      sum += integratedHistogram_[i].sum;
      count += integratedHistogram_[i].count;
    }
  }
  for (std::size_t group = firstGroup + 1; group < kHistogramGroups; ++group) {
    if (histogramGroups_[group].generation == histogramGeneration_) {
      sum += histogramGroups_[group].total.sum;
      count += histogramGroups_[group].total.count;
    }
  }
  integratedLufs_ = count > 0 ? loudnessFromEnergy(sum / count) : -70.0f;
}

float LoudnessMeter::loudnessFromEnergy(double energy) {
  return static_cast<float>(std::max(kAbsoluteGateLufs,
      kLoudnessOffset + 10.0 * std::log10(std::max(energy, 1.0e-12))));
}
std::size_t LoudnessMeter::storageBytes() const {
  return energyWindow_.capacity() * sizeof(double) + integratedHistogram_.capacity() * sizeof(EnergyBin) +
         sizeof(histogramGroups_) + weightingFilter_.storageBytes();
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
