#include "gainpilot/dsp/processor.hpp"
#include <algorithm>
#include <cmath>
#include <ebur128.h>
#include <iostream>
#include <fstream>
#include <random>
#include <vector>
#ifdef GAINPILOT_TEST_SOXR
#include <soxr.h>
#endif

int main(int argc, char** argv) {
  // Optional raw stereo float dump of the original failing burst at 48 kHz,
  // for an additional resampler/meter. Normal CTest runs write no audio files.
  std::ofstream dump;
  if (argc == 2) dump.open(argv[1], std::ios::binary);
  if (argc > 2 || (argc == 2 && !dump)) return 1;
  constexpr double pi = 3.14159265358979323846;
  bool passed = true;
  for (double rate : {44100., 48000., 96000.}) {
    for (int shape = 0; shape < 20; ++shape) {
      gainpilot::dsp::GainPilotProcessor processor;
      gainpilot::ParameterState state;
      const size_t block = shape % 3 == 0 ? 1 : (shape % 3 == 1 ? 127 : 1024);
      state.set(gainpilot::ParamId::targetLevel, -23);
      state.set(gainpilot::ParamId::truePeak, shape == 7 ? -1 : -9);
      state.set(gainpilot::ParamId::correctionHigh, 0);
      state.set(gainpilot::ParamId::correctionLow, 0);
      state.set(gainpilot::ParamId::inputTrim, 12);
      processor.prepare(rate, 2, block);
      processor.setParameters(state);
      auto *meter = ebur128_init(2, static_cast<unsigned long>(rate),
                                 EBUR128_MODE_TRUE_PEAK);
      if (!meter)
        return 1;
      std::mt19937 random(12345 + (shape > 7 ? shape - 7 : 0));
      std::uniform_real_distribution<float> noise(-1, 1);
      float left[1024]{}, right[1024]{}, outLeft[1024]{}, outRight[1024]{},
          output[2048]{};
      const float *inputs[]{left, right};
      float *outputs[]{outLeft, outRight};
      const auto frames = static_cast<size_t>(rate * 2);
      bool changed = false;
      std::vector<float> rendered;
      for (size_t offset = 0;
           offset < frames + processor.latencySamples() + 1024; offset += block) {
        if (shape == 7 && !changed && offset >= static_cast<size_t>(rate)) {
          state.set(gainpilot::ParamId::truePeak, -9);
          processor.setParameters(state);
          changed = true;
        }
        for (size_t n = 0; n < block; ++n) {
          const size_t i = offset + n;
          double value = 0;
          switch (shape) {
          case 0:
            value = .99 * std::sin(pi * .5 * i + pi / 4);
            break;
          case 1:
            value = i % 4799 == 0 ? .99 : 0;
            break;
          case 2:
            value = std::clamp(4 * std::sin(2 * pi * .073 * i), -.99, .99);
            break;
          case 3:
            value = noise(random);
            break;
          case 4:
            value = i % 9000 < 32 ? noise(random) : 0;
            break;
          case 5:
            value = .99 * std::sin(2 * pi * .499 * i + pi / 4);
            break;
          case 6:
            value = (i % 9000 < 4500 ? .99 : .02) * std::sin(2 * pi * .23 * i);
            break;
          case 7:
            value = .99 * std::sin(2 * pi * .1 * i);
            break;
          default:
            value = i % 9000 < static_cast<size_t>((shape - 7) * 7)
                        ? noise(random)
                        : 0;
            break;
          }
          left[n] = i < frames ? static_cast<float>(value) : 0;
          right[n] = -.37f * left[n];
        }
        processor.process({inputs, outputs, 2, block});
        for (size_t n = 0; n < block; ++n) {
          output[n * 2] = outLeft[n];
          output[n * 2 + 1] = outRight[n];
          if (!std::isfinite(outLeft[n]) || !std::isfinite(outRight[n]) ||
              std::abs(outRight[n] + .37f * outLeft[n]) > 1e-6f) passed = false;
          if ((shape != 7 || changed) &&
              (std::abs(outLeft[n]) > std::pow(10.0, -9.0 / 20.0) ||
               std::abs(outRight[n]) > std::pow(10.0, -9.0 / 20.0))) passed = false;
        }
        if (dump.is_open() && rate == 48000 && shape == 4)
          dump.write(reinterpret_cast<const char*>(output), block * 2 * sizeof(float));
        if ((shape != 7 || changed) &&
            ebur128_add_frames_float(meter, output, block))
          return 1;
        rendered.insert(rendered.end(), output, output + block * 2);
      }
      double peak = 0, rightPeak = 0;
      if (ebur128_true_peak(meter, 0, &peak) ||
          ebur128_true_peak(meter, 1, &rightPeak))
        return 1;
      ebur128_destroy(&meter);
      const double db = 20 * std::log10(std::max(peak, rightPeak));
      std::cout << "rate=" << rate << " block=" << block << " shape=" << shape
                << " ceiling=-9 peak=" << db << " dBTP\n";
      if (!(db <= -9.0))
        passed = false;
#ifdef GAINPILOT_TEST_SOXR
      std::vector<float> reconstructed((rendered.size() + 2048) * 16);
      auto quality = soxr_quality_spec(SOXR_VHQ, 0);
      quality.precision = 33;
      auto format = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
      size_t consumed = 0, produced = 0;
      const auto error = soxr_oneshot(rate, rate * 16, 2, rendered.data(), rendered.size() / 2,
          &consumed, reconstructed.data(), reconstructed.size() / 2, &produced, &format, &quality, nullptr);
      if (error || consumed != rendered.size() / 2) return 1;
      double reconstructedPeak = 0;
      for (size_t i = 0; i < produced * 2; ++i) reconstructedPeak = std::max(reconstructedPeak, std::abs(double(reconstructed[i])));
      const double soxrDb = 20 * std::log10(reconstructedPeak);
      std::cout << "  SoX 16x peak=" << soxrDb << " dBTP\n";
      // Preserve the complete waveform when reconstructing an automated file.
      // Cutting it at the ceiling change introduces an artificial discontinuity.
      // Its first second was legitimately limited to -1; samples following the
      // change are checked against -9 above, and separately with libebur128.
      if (!(soxrDb <= (shape == 7 ? -1.0 : -9.0))) passed = false;
#endif
    }
  }
  return passed ? 0 : 1;
}
