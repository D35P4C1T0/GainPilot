#include "gainpilot/dsp/k_weighting_filter.hpp"

namespace gainpilot::dsp {

void KWeightingFilter::prepare(double sampleRate, std::size_t channelCount) {
  highShelves_.assign(channelCount, {});
  highPasses_.assign(channelCount, {});

  // BS.1770 pre-filter and RLB coefficients (bilinear transform).
  // The RLB numerator is intentionally unnormalized, unlike a generic HPF.
  const double shelfK = std::tan(kPi * 1681.974450955533 / sampleRate);
  const double vh = std::pow(10.0, 3.999843853973347 / 20.0);
  const double vb = std::pow(vh, 0.4996667741545416);
  const double shelfQ = 0.7071752369554196;
  const double a0 = 1.0 + shelfK / shelfQ + shelfK * shelfK;
  const BiquadCoefficients shelf{
      (vh + vb * shelfK / shelfQ + shelfK * shelfK) / a0,
      2.0 * (shelfK * shelfK - vh) / a0,
      (vh - vb * shelfK / shelfQ + shelfK * shelfK) / a0,
      2.0 * (shelfK * shelfK - 1.0) / a0,
      (1.0 - shelfK / shelfQ + shelfK * shelfK) / a0};
  const double hpK = std::tan(kPi * 38.13547087602444 / sampleRate);
  const double hpQ = 0.5003270373238773;
  const double hpA0 = 1.0 + hpK / hpQ + hpK * hpK;
  const BiquadCoefficients highPass{1.0, -2.0, 1.0,
      2.0 * (hpK * hpK - 1.0) / hpA0,
      (1.0 - hpK / hpQ + hpK * hpK) / hpA0};

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
