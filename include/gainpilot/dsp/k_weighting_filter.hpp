#pragma once

#include <vector>

#include "gainpilot/dsp/biquad.hpp"

namespace gainpilot::dsp {

class KWeightingFilter {
public:
  void prepare(double sampleRate, std::size_t channelCount);
  void reset();
  [[nodiscard]] float processSample(std::size_t channel, float sample);

private:
  std::vector<Biquad> highShelves_{};
  std::vector<Biquad> highPasses_{};
};

}  // namespace gainpilot::dsp
