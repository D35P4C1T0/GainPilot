#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include "gainpilot/dsp/processor.hpp"

int main() {
  constexpr double pi = 3.14159265358979323846;
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    for (std::size_t block : {std::size_t{127}, std::size_t{1024}}) {
      for (float amplitude : {0.02f, 0.2f}) {
        gainpilot::dsp::GainPilotProcessor processor;
        processor.prepare(rate, 1, block);
        gainpilot::ParameterState state;
        processor.setParameters(state);
        std::vector<float> input(block), output(block);
        const auto frames = static_cast<std::size_t>(rate * 18);
        float earlyReference = 0;
        for (std::size_t offset = 0; offset < frames; offset += block) {
          const auto count = std::min(block, frames - offset);
          for (std::size_t i = 0; i < count; ++i)
            input[i] = amplitude * static_cast<float>(std::sin(2 * pi * 1000 * (offset + i) / rate));
          const float* in[]{input.data()};
          float* out[]{output.data()};
          processor.process({in, out, 1, count});
          if (offset < static_cast<std::size_t>(rate * 3))
            earlyReference = processor.currentInputReferenceLufs();
        }
        const float measured = processor.currentInputIntegratedLufs();
        const float reference = processor.currentInputReferenceLufs();
        const float remaining = std::abs((reference - measured) / (-23.0f - measured));
        // After ~14.7 seconds of learning, a 3-second descent is nearly
        // settled; a 12-second ascent has ~29% of its initial error left.
        const bool quiet = amplitude < 0.1f;
        if (std::abs(earlyReference + 23) > 0.01f ||
            (quiet ? remaining > 0.015f : (remaining < 0.26f || remaining > 0.33f))) {
          std::cerr << "Learning timing: rate=" << rate << " block=" << block
                    << " measured=" << measured << " reference=" << reference
                    << " remaining=" << remaining << '\n';
          return 1;
        }
        state.set(gainpilot::ParamId::meterReset, 1);
        processor.setParameters(state);
        const float* in[]{input.data()};
        float* out[]{output.data()};
        processor.process({in, out, 1, 1});
        if (std::abs(processor.currentInputReferenceLufs() + 23) > 0.01f) {
          std::cerr << "Reset/Relearn did not reset the learned reference\n";
          return 1;
        }
      }
    }
  }
  // A quiet tone below the automatic -50 LUFS threshold must only receive
  // boost when manual freeze is lowered. Check live mode/threshold changes too.
  gainpilot::dsp::GainPilotProcessor freezeProcessor;
  freezeProcessor.prepare(48000, 1, 256);
  gainpilot::ParameterState freezeState;
  freezeState.set(gainpilot::ParamId::referenceMode, 1);
  freezeState.set(gainpilot::ParamId::lockedReference, -23);
  std::vector<float> quietInput(256), quietOutput(256);
  const float* quietIn[]{quietInput.data()};
  float* quietOut[]{quietOutput.data()};
  std::size_t sample = 0;
  auto playQuiet = [&] {
    freezeProcessor.setParameters(freezeState);
    for (int block = 0; block < 600; ++block) {
      for (auto& value : quietInput)
        value = .003f * static_cast<float>(std::sin(2 * pi * 1000 * sample++ / 48000));
      freezeProcessor.process({quietIn, quietOut, 1, quietInput.size()});
    }
    return freezeProcessor.currentAppliedGainDb();
  };
  if (std::abs(playQuiet()) > .01f) return 1;
  freezeState.set(gainpilot::ParamId::freezeMode, 1);
  freezeState.set(gainpilot::ParamId::freezeLevel, -60);
  if (playQuiet() < 5) { std::cerr << "Manual freeze failed to enable quiet boost\n"; return 1; }
  freezeState.set(gainpilot::ParamId::freezeLevel, -40);
  if (std::abs(playQuiet()) > .01f) { std::cerr << "Manual freeze failed to remove boost\n"; return 1; }
  freezeState.set(gainpilot::ParamId::freezeLevel, -60);
  if (playQuiet() < 5) return 1;
  freezeState.set(gainpilot::ParamId::freezeMode, 0);
  if (std::abs(playQuiet()) > .01f) return 1;

  std::cout << "Learning timing and reset passed at three rates and two block sizes\n";
}
