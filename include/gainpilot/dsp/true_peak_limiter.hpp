#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <vector>

namespace gainpilot::dsp {

class TruePeakLimiter {
public:
  void prepare(double sampleRate, std::size_t channelCount, double latencySeconds = 0.020);
  void reset();
  void setCeilingDb(float ceilingDb);
  [[nodiscard]] std::size_t latencySamples() const;
  void processFrame(const float* input, float* output, float preGainLinear);

private:
  struct GainSample {
    std::size_t index{};
    float gain{};
  };

  void pushHistorySample(std::size_t channel, float sample);
  [[nodiscard]] float estimatePeak(std::size_t channel) const;
  void pushRequiredGain(float requiredGain);

  double sampleRate_{48000.0};
  std::size_t channelCount_{2};
  std::size_t lookaheadSamples_{1};
  std::size_t sampleIndex_{0};
  std::size_t writeIndex_{0};
  float ceilingLinear_{1.0f};
  float envelopeGain_{1.0f};
  float attackCoeff_{0.0f};
  float releaseCoeff_{0.0f};
  std::vector<std::vector<float>> delayLines_{};
  std::vector<std::array<float, 4>> sampleHistory_{};
  std::vector<std::size_t> sampleHistoryCount_{};
  std::deque<GainSample> requiredGainWindow_{};
};

}  // namespace gainpilot::dsp
