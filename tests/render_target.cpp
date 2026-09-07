#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include <ebur128.h>
#include "gainpilot/dsp/processor.hpp"

// Synthetic convergence probe, not a model of arbitrary speech or music.
// Optional arguments: duration in seconds, pattern (0..2), Speech mode (0..1).
int main(int argc, char** argv) {
  const int seconds = argc > 1 ? std::stoi(argv[1]) : 120;
  const int pattern = argc > 2 ? std::stoi(argv[2]) : 0;
  const int speech = argc > 3 ? std::stoi(argv[3]) : 0;
  if (seconds < 30 || seconds > 3600 || pattern < 0 || pattern > 2 || speech < 0 || speech > 1)
    return 1;
  constexpr int rate = 48000, block = 256;
  constexpr double pi = 3.14159265358979323846;
  gainpilot::dsp::GainPilotProcessor processor;
  gainpilot::ParameterState parameters;
  parameters.set(gainpilot::ParamId::targetLevel, -14);
  parameters.set(gainpilot::ParamId::programMode, static_cast<float>(speech));
  processor.prepare(rate, 2, block);
  processor.setParameters(parameters);
  processor.reset();
  processor.setOfflineMode(true);
  auto* meter = ebur128_init(2, rate, EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK);
  if (!meter) return 1;
  std::vector<float> left(block), right(block), outLeft(block), outRight(block), interleaved(block * 2);
  const float* inputs[]{left.data(), right.data()};
  float* outputs[]{outLeft.data(), outRight.data()};
  for (int64_t offset = 0; offset < int64_t(rate) * seconds; offset += block) {
    const auto frames = static_cast<size_t>(std::min<int64_t>(block, int64_t(rate) * seconds - offset));
    for (size_t n = 0; n < frames; ++n) {
      const double time = static_cast<double>(offset + n) / rate;
      double amplitude = .08;
      if (pattern == 1) {
        const auto section = ((offset + n) / (rate / 2)) % 3;
        amplitude = section == 0 ? .1 : (section == 1 ? .03 : .22);
      } else if (pattern == 2) {
        amplitude = std::fmod(time, 8.) > 6. ? 0 :
            .12 * (.15 + .85 * std::pow(.5 + .5 * std::sin(2 * pi * 3.7 * time), 2));
      }
      left[n] = right[n] = static_cast<float>(amplitude * std::sin(2 * pi * 997 * time));
    }
    processor.process({inputs, outputs, 2, frames});
    for (size_t n = 0; n < frames; ++n) {
      interleaved[2 * n] = outLeft[n];
      interleaved[2 * n + 1] = outRight[n];
    }
    if (ebur128_add_frames_float(meter, interleaved.data(), frames)) {
      ebur128_destroy(&meter);
      return 1;
    }
  }
  double integrated = 0, peak = 0;
  const bool measured = ebur128_loudness_global(meter, &integrated) == 0 &&
                        ebur128_true_peak(meter, 0, &peak) == 0;
  ebur128_destroy(&meter);
  std::cout << "seconds=" << seconds << " pattern=" << pattern << " speech=" << speech
            << " output=" << integrated << " LUFS-I peak=" << 20 * std::log10(peak)
            << " dBTP internal=" << processor.currentOutputIntegratedLufs() << '\n';
  return measured && std::isfinite(integrated) && std::abs(integrated + 14) <= .5 &&
         std::abs(integrated - processor.currentOutputIntegratedLufs()) <= .05 ? 0 : 1;
}
