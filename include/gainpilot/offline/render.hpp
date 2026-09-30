#pragma once
#include <cstddef>
#include <limits>
#include <vector>

namespace gainpilot::offline {
// Whole-file fixed-gain normalization, independent of the live Auto/Speech controller.
struct Audio {
  unsigned long sampleRate{48000};
  std::size_t channels{2};
  std::vector<float> samples; // interleaved
};
struct Settings {
  double targetLufs{-23};
  double ceilingDb{-1};
  double minimumGainDb{-24};
  double maximumGainDb{24};
  double toleranceLu{0.1};
  unsigned maximumPasses{16};
};
struct Measurement {
  double integratedLufs{-std::numeric_limits<double>::infinity()};
  double eburPeakDb{-std::numeric_limits<double>::infinity()};
  double soxrPeakDb{-std::numeric_limits<double>::infinity()};
};
enum class Status { achieved, unattainable, not_converged, silent, unmeasurable };
struct Result {
  Status status{Status::unmeasurable};
  Audio audio;
  Measurement source, output;
  double gainDb{};
  double errorLu{};
  unsigned passes{};
  std::size_t latencySamples{};
  double maximumAttenuationDb{};
};
// Throws for invalid input/settings or failed independent measurement.
Measurement measure(const Audio& audio);
Result render(const Audio& source, const Settings& settings = {});
const char* statusName(Status status);
} // namespace gainpilot::offline
