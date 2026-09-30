#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "gainpilot/dsp/loudness_meter.hpp"
#include "gainpilot/dsp/processor.hpp"

namespace gainpilot::dsp {
struct LoudnessMeterTestAccess {
  static void forceGenerationWrap(LoudnessMeter& meter) {
    meter.histogramGeneration_ = std::numeric_limits<std::uint64_t>::max();
  }
};
}

namespace {
using gainpilot::dsp::LoudnessMeter;
float lufs(double energy) {
  return static_cast<float>(std::max(-70.0, -0.691 + 10.0 * std::log10(std::max(energy, 1e-12))));
}
// Deliberately retains the original separate windows and full histogram scan.
// This checks storage/query optimizations against the original arithmetic,
// independently of libebur128's separate standards-conformance checks.
struct Baseline {
  struct Bin { double sum{}; std::uint64_t count{}; };
  std::vector<double> momentary, shortTerm;
  std::vector<Bin> bins = std::vector<Bin>(17001);
  gainpilot::dsp::KWeightingFilter filter;
  std::size_t mi{}, si{}, samples{}, integratedSamples{}, hop{}, blocks{};
  double ms{}, ss{}, absoluteSum{};
  std::uint64_t absoluteCount{};
  float integrated{-70};
  Baseline(double rate, std::size_t channels)
      : momentary(static_cast<std::size_t>(std::ceil(rate * .4))),
        shortTerm(static_cast<std::size_t>(std::ceil(rate * 3))),
        hop(static_cast<std::size_t>(std::ceil(rate * .1))) { filter.prepare(rate, channels); }
  void resetIntegrated() {
    std::fill(bins.begin(), bins.end(), Bin{});
    integratedSamples = blocks = absoluteCount = 0;
    absoluteSum = 0;
    integrated = -70;
  }
  void reset() {
    filter.reset(); mi = si = samples = 0; ms = ss = 0;
    std::fill(momentary.begin(), momentary.end(), 0);
    std::fill(shortTerm.begin(), shortTerm.end(), 0);
    resetIntegrated();
  }
  bool process(const float* frame, std::size_t channels) {
    double e = 0;
    for (std::size_t c = 0; c < channels; ++c) {
      const float w = filter.processSample(c, frame[c]); e += static_cast<double>(w) * w;
    }
    ms += e - momentary[mi]; momentary[mi] = e; mi = (mi + 1) % momentary.size();
    ss += e - shortTerm[si]; shortTerm[si] = e; si = (si + 1) % shortTerm.size();
    ++samples; ++integratedSamples;
    if (samples % hop) return false;
    if (integratedSamples < momentary.size()) return true;
    ++blocks;
    e = std::max(0.0, ms / momentary.size());
    if (e >= 1.172465304582296e-7) {
      const auto bin = static_cast<std::size_t>(std::clamp((lufs(e) + 70.0) * 100.0, 0.0, 17000.0));
      bins[bin].sum += e; ++bins[bin].count; absoluteSum += e; ++absoluteCount;
    }
    if (!absoluteCount) return true;
    const auto first = static_cast<std::size_t>(std::clamp(std::round((lufs(absoluteSum / absoluteCount) + 60.0) * 100.0), 0.0, 17000.0));
    double sum = 0; std::uint64_t count = 0;
    for (auto i = first; i < bins.size(); ++i) { sum += bins[i].sum; count += bins[i].count; }
    integrated = count ? lufs(sum / count) : -70;
    return true;
  }
};
}

int main() {
  for (double rate : {44100., 48000., 48001., 96000.}) for (std::size_t channels : {1U, 2U}) {
    LoudnessMeter meter; meter.prepare(rate, channels); Baseline old(rate, channels);
    for (std::size_t n = 0; n < static_cast<std::size_t>(rate * 12); ++n) {
      if (n == static_cast<std::size_t>(rate * 2.371) || n == static_cast<std::size_t>(rate * 8.73)) {
        meter.resetIntegrated(); old.resetIntegrated();
      }
      if (n == static_cast<std::size_t>(rate * 5.131)) { meter.reset(); old.reset(); }
      if (n == static_cast<std::size_t>(rate * 9.1)) {
        gainpilot::dsp::LoudnessMeterTestAccess::forceGenerationWrap(meter);
        meter.resetIntegrated(); old.resetIntegrated();
      }
      // Sweep occupied bins and both gate boundaries, with silence and abrupt
      // transitions. Distinct channels exercise summed K-weighted energy.
      const double db = n > rate * 10 ? -100 : -80 + 190.0 * ((n / 3079) % 97) / 96;
      const float a = static_cast<float>(std::pow(10.0, db / 20));
      float frame[]{a * static_cast<float>(std::sin(n * .113)), a * static_cast<float>(std::cos(n * .037))};
      const bool hop = meter.processFrame(frame);
      if (hop != old.process(frame, channels) || meter.momentaryReady() != (old.samples >= old.momentary.size()) ||
          meter.shortTermReady() != (old.samples >= old.shortTerm.size()) || meter.integratedBlockCount() != old.blocks) return 1;
      if (!hop) continue;
      if (meter.momentaryLufs() != lufs(old.ms / old.momentary.size()) ||
          meter.shortTermLufs() != lufs(old.ss / old.shortTerm.size()) ||
          meter.integratedLufs() < std::nextafter(old.integrated, -std::numeric_limits<float>::infinity()) ||
          meter.integratedLufs() > std::nextafter(old.integrated, std::numeric_limits<float>::infinity())) {
        std::cerr << "Baseline mismatch rate=" << rate << " channels=" << channels << " sample=" << n << '\n'; return 1;
      }
    }
  }
  gainpilot::dsp::GainPilotProcessor processor;
  processor.prepare(48000, 2, 256); const auto stereoStorage = processor.meterStorageBytes();
  processor.prepare(48000, 1, 256); const auto monoStorage = processor.meterStorageBytes();
  // Two histories are released when re-preparing a stereo instance as mono.
  if (monoStorage >= stereoStorage * .51 || stereoStorage >= 5800000 || monoStorage >= 2900000) return 1;
  std::cout << "Window, gate, freshness, generation wrap and mono storage checks passed: "
            << monoStorage << " / " << stereoStorage << " bytes\n";
}
