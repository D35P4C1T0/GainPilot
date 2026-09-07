#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
#include <ebur128.h>
#include "gainpilot/dsp/loudness_meter.hpp"
#include "gainpilot/dsp/true_peak_limiter.hpp"

struct Reference {
  ebur128_state* state;
  Reference(unsigned channels, unsigned long rate, int mode) : state(ebur128_init(channels, rate, mode)) {
    if (!state) std::abort();
    ebur128_set_channel(state, 0, channels == 1 ? EBUR128_CENTER : EBUR128_LEFT);
    if (channels == 2) ebur128_set_channel(state, 1, EBUR128_RIGHT);
  }
  ~Reference() { ebur128_destroy(&state); }
};
int main() {
  constexpr double pi = 3.14159265358979323846;
  double worstLufs = 0, worstPeakDb = -100;
  for (unsigned long rate : {44100UL, 48000UL, 96000UL}) {
    for (unsigned channels : {1U, 2U}) {
      for (double frequency : {60.0, 1000.0, 10000.0}) {
        gainpilot::dsp::LoudnessMeter meter;
        meter.prepare(rate, channels);
        Reference reference(channels, rate, EBUR128_MODE_I | EBUR128_MODE_S);
        for (std::size_t n = 0; n < rate * 12; ++n) {
          const auto section = n / (rate * 2);
          // Include absolute/relative gating and silent sections.
          const double amplitude = section == 1 ? .00001 : (section == 2 ? 0 : (section == 3 ? .015 : .2));
          float frame[]{static_cast<float>(amplitude * std::sin(2 * pi * frequency * n / rate)), 0};
          frame[1] = frame[0];
          const bool hop = meter.processFrame(frame);
          ebur128_add_frames_float(reference.state, frame, 1);
          if (hop && n >= rate * 3) {
            double values[3]{};
            if (ebur128_loudness_global(reference.state, &values[0]) ||
                ebur128_loudness_momentary(reference.state, &values[1]) ||
                ebur128_loudness_shortterm(reference.state, &values[2])) return 1;
            const float actual[]{meter.integratedLufs(), meter.momentaryLufs(), meter.shortTermLufs()};
            for (int i = 0; i < 3; ++i) {
              const double expected = std::isfinite(values[i]) ? std::max(-70.0, values[i]) : -70;
              const double error = std::abs(actual[i] - expected);
              worstLufs = std::max(worstLufs, error);
              if (error > .05) {
                std::cerr << "Meter mismatch " << error << " LU: rate=" << rate
                          << " channels=" << channels << " frequency=" << frequency
                          << " mode=" << i << " sample=" << n << '\n';
                return 1;
              }
            }
          }
        }
      }
    }
    for (double frequency : {.01, .25, .40, .45, .49}) {
      for (int shape = 0; shape < 3; ++shape) {
        gainpilot::dsp::TruePeakLimiter limiter;
        limiter.prepare(rate, 2, .035375);
        limiter.setCeilingDb(-1);
        Reference reference(2, rate, EBUR128_MODE_TRUE_PEAK);
        for (std::size_t n = 0; n < rate * 2; ++n) {
          float sample = static_cast<float>(1.2 * std::sin(2 * pi * frequency * n + pi / 4));
          if (shape == 1 && n % (rate / 4) > rate / 100) sample = 0;
          if (shape == 2) sample = n % (rate / 4) == 0 ? 2.f : 0.f;
          if (n > rate * 3 / 2) sample = 0;
          float in[]{sample, -.3f * sample}, out[2]{};
          limiter.processFrame(in, out, 1);
          ebur128_add_frames_float(reference.state, out, 1);
        }
        double peak = 0;
        if (ebur128_true_peak(reference.state, 0, &peak)) return 1;
        const double db = 20 * std::log10(peak);
        worstPeakDb = std::max(worstPeakDb, db);
        if (db > -.9) {
          std::cerr << "Reference true peak " << db << " dBTP: rate=" << rate
                    << " frequency/fs=" << frequency << " shape=" << shape << '\n';
          return 1;
        }
      }
    }
  }
  std::cout << "Maximum meter difference: " << worstLufs << " LU; maximum reference peak: "
            << worstPeakDb << " dBTP (ceiling -1)\n";
}
