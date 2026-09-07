#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include "gainpilot/dsp/true_peak_limiter.hpp"

int main() {
  constexpr double pi = 3.14159265358979323846;
  double worstDb = -100;
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    for (double phase : {0.0, pi / 8, pi / 4}) {
      gainpilot::dsp::TruePeakLimiter limiter;
      limiter.prepare(rate, 2, .035375);
      limiter.setCeilingDb(-1);
      double peak = 0;
      float previous = 0;
      for (std::size_t n = 0; n < static_cast<std::size_t>(rate); ++n) {
        const float sample = static_cast<float>(1.2 * std::sin(pi * .5 * n + phase));
        float in[]{sample, sample * -.3f}, out[2]{};
        limiter.processFrame(in, out, 1);
        if (n > static_cast<std::size_t>(rate / 2)) {
          // Independent analytic reconstruction of a settled fs/4 sinusoid.
          peak = std::max(peak, static_cast<double>(std::hypot(previous, out[0])));
          if (std::abs(out[1] + .3f * out[0]) > 1e-6f) return 1;
        }
        previous = out[0];
      }
      const double db = 20 * std::log10(peak);
      worstDb = std::max(worstDb, db);
      if (db > -.9 || db < -1.7) {
        std::cerr << "Analytic true peak " << db << " dBTP at " << rate << '\n';
        return 1;
      }
      limiter.reset();
      const auto delay = limiter.latencySamples();
      for (std::size_t n = 0; n <= delay + 1; ++n) {
        float in[]{n == 0 ? .1f : 0.f, 0.f}, out[2]{};
        limiter.processFrame(in, out, 1);
        if (std::abs(out[0] - (n == delay ? .1f : 0.f)) > 1e-6f) {
          std::cerr << "Measured delay disagrees with reported latency\n";
          return 1;
        }
      }
    }
  }
  std::cout << "Worst analytic peak: " << worstDb << " dBTP (ceiling -1)\n";
}
