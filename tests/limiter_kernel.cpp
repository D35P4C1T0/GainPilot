#include "gainpilot/dsp/true_peak_limiter.hpp"
#include "support/scalar_reference_limiter.hpp"
#include <algorithm>
#include <chrono>
#include <string_view>
#include <vector>
#include <cmath>
#include <iostream>
#include <random>

namespace {
volatile double benchmarkSink;
template<class Limiter>
double timeLimiter(double rate, size_t channels, const std::vector<float>& input) {
  Limiter limiter;
  limiter.prepare(rate, channels);
  limiter.setCeilingDb(-1);
  float output[2]{};
  double sum = 0;
  const auto start = std::chrono::steady_clock::now();
  for (size_t n = 0; n < input.size() / channels; ++n) {
    limiter.processFrame(input.data() + n * channels, output, 1);
    sum += output[0];
  }
  const double ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  benchmarkSink = sum;
  return ms;
}
void benchmark() {
  std::cout << "Compiler: " << __VERSION__ << "\nAlternating seven rounds; one warmup discarded; 1s audio per round.\n";
  for (double rate : {44100., 48000., 96000.}) {
    for (size_t channels : {1u, 2u}) {
      for (bool silent : {false, true}) {
        std::vector<float> input(static_cast<size_t>(rate) * channels);
        for (size_t n = 0; n < input.size(); ++n)
          input[n] = silent ? 0.f : static_cast<float>(1.3 * std::sin(n * .31));
        std::vector<double> oldTimes, newTimes;
        for (int round = 0; round < 7; ++round) {
          double oldMs, newMs;
          if (round % 2) {
            newMs = timeLimiter<gainpilot::dsp::TruePeakLimiter>(rate, channels, input);
            oldMs = timeLimiter<gainpilot::dsp::ScalarReferenceLimiter>(rate, channels, input);
          } else {
            oldMs = timeLimiter<gainpilot::dsp::ScalarReferenceLimiter>(rate, channels, input);
            newMs = timeLimiter<gainpilot::dsp::TruePeakLimiter>(rate, channels, input);
          }
          if (round) { oldTimes.push_back(oldMs); newTimes.push_back(newMs); }
        }
        std::sort(oldTimes.begin(), oldTimes.end());
        std::sort(newTimes.begin(), newTimes.end());
        const double oldMs = (oldTimes[2] + oldTimes[3]) / 2;
        const double newMs = (newTimes[2] + newTimes[3]) / 2;
        std::cout << rate << " Hz, " << channels << " channels, silence=" << silent
                  << ": scalar median=" << oldMs << "ms [" << oldTimes.front() << ',' << oldTimes.back()
                  << "], optimized median=" << newMs << "ms [" << newTimes.front() << ',' << newTimes.back()
                  << "], reduction=" << 100 * (1 - newMs / oldMs) << "%\n";
      }
    }
  }
}
} // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
    benchmark();
    return 0;
  }
  if (argc != 1) return 1;
  float worst = 0;
  for (double rate : {44100., 48000., 96000.}) {
    for (size_t channels : {1u, 2u}) {
      gainpilot::dsp::TruePeakLimiter optimized;
      gainpilot::dsp::ScalarReferenceLimiter scalar;
      optimized.prepare(rate, channels);
      scalar.prepare(rate, channels);
      if (optimized.latencySamples() != scalar.latencySamples()) return 1;
      std::mt19937 rng(5312);
      std::uniform_real_distribution<float> noise(-1.f, 1.f);
      const size_t frames = static_cast<size_t>(rate * 3);
      for (size_t n = 0; n < frames; ++n) {
        if (n % 4096 == 0) {
          const float ceiling = n % 8192 == 0 ? -1.f : -9.f;
          optimized.setCeilingDb(ceiling);
          scalar.setCeilingDb(ceiling);
        }
        if (n == frames / 2) { optimized.reset(); scalar.reset(); }
        float in[2]{}, a[2]{}, b[2]{};
        // Silence, isolated bursts, broadband limiting and near-Nyquist tones.
        for (size_t ch = 0; ch < channels; ++ch) {
          const size_t section = n * 6 / frames;
          in[ch] = section == 0 || section == 5 ? 0.f :
                   section == 1 ? (n % 1000 < 17 ? noise(rng) : 0.f) :
                   section == 2 ? noise(rng) :
                   static_cast<float>(std::sin(n * 3.13 + ch * .7));
        }
        const float gain = static_cast<float>(2. + 1.8 * std::sin(n * .017));
        optimized.processFrame(in, a, gain);
        scalar.processFrame(in, b, gain);
        for (size_t ch = 0; ch < channels; ++ch) {
          if (!std::isfinite(a[ch])) return 1;
          worst = std::max(worst, std::abs(a[ch] - b[ch]));
        }
      }
    }
  }
  // Across-phase SIMD preserves tap order but can contract multiply-add on
  // architectures where the old reduction used separate instructions. Bound
  // only that rounding error; strict independent ceilings are tested separately.
  std::cout << "Maximum scalar/SIMD output difference: " << worst << '\n';
  return worst <= 5e-7f ? 0 : 1;
}
