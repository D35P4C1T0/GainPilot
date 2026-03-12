#include "gainpilot/dsp/k_weighting_filter.hpp"

namespace gainpilot::dsp {

void KWeightingFilter::prepare(double sampleRate, std::size_t channelCount) {
  highShelves_.assign(channelCount, {});
  highPasses_.assign(channelCount, {});

  const auto shelf = makeHighShelf(sampleRate, 1681.974450955533, 4.0, 1.0);
  const auto highPass = makeHighPass(sampleRate, 38.13547087602444, 0.5003270373238773);

  for (std::size_t channel = 0; channel < channelCount; ++channel) {
    highShelves_[channel].setCoefficients(shelf);
    highPasses_[channel].setCoefficients(highPass);
  }

  reset();
}

void KWeightingFilter::reset() {
  for (auto& shelf : highShelves_) {
    shelf.reset();
  }
  for (auto& highPass : highPasses_) {
    highPass.reset();
  }
}

float KWeightingFilter::processSample(std::size_t channel, float sample) {
  return highPasses_[channel].process(highShelves_[channel].process(sample));
}

}  // namespace gainpilot::dsp
