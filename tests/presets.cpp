#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>
#include "gainpilot/presets.hpp"
#include "gainpilot/dsp/processor.hpp"

int main() {
  auto state = gainpilot::factoryPreset(1);
  state.set(gainpilot::ParamId::referenceMode, 1);
  state.set(gainpilot::ParamId::lockedReference, -29.25f);
  state.set(gainpilot::ParamId::inputReferenceValue, -12);
  auto restored = gainpilot::deserializeState(gainpilot::serializeState(state));
  if (!restored || restored->get(gainpilot::ParamId::lockedReference) != -29.25f ||
      restored->get(gainpilot::ParamId::referenceMode) != 1 ||
      restored->get(gainpilot::ParamId::inputReferenceValue) != -23) return 1;

  // Construct a real V4 payload, preserving its original parameter order.
  auto legacy = gainpilot::serializeState(state);
  const std::uint32_t version = 4, count = 13;
  std::memcpy(legacy.data() + 4, &version, sizeof(version));
  std::memcpy(legacy.data() + 8, &count, sizeof(count));
  legacy.resize(12 + count * sizeof(float));
  auto old = gainpilot::deserializeState(legacy);
  if (!old || old->get(gainpilot::ParamId::referenceMode) != 0 ||
      old->get(gainpilot::ParamId::programMode) != 1) return 1;

  gainpilot::dsp::GainPilotProcessor processor;
  processor.prepare(48000, 1, 256);
  processor.setParameters(*restored);
  processor.reset();
  std::vector<float> input(256, .05f), output(256);
  const float* in[]{input.data()};
  float* out[]{output.data()};
  const auto epoch = processor.meterResetCount();
  for (int b = 0; b < 4000; ++b) {
    state.set(gainpilot::ParamId::meterReset, b == 1000 ? 1 : 0);
    state.set(gainpilot::ParamId::channelMode, (b / 100) % 2);
    processor.setParameters(state);
    for (std::size_t n = 0; n < input.size(); ++n)
      input[n] = .2f * std::sin(static_cast<float>(b * 256 + n) * .13f);
    processor.process({in, out, 1, input.size()});
    if (std::abs(processor.currentInputReferenceLufs() + 29.25f) > 1e-6f) {
      std::cerr << "Locked reference changed during playback/reset\n";
      return 1;
    }
  }
  if (processor.meterResetCount() == epoch) return 1;
  processor.reset();
  if (processor.currentInputReferenceLufs() != -29.25f) return 1;
  state.set(gainpilot::ParamId::referenceMode, 0);
  processor.setParameters(state);
  if (processor.currentInputReferenceLufs() != -23.f) return 1;

  const auto path = std::filesystem::temp_directory_path() / "gainpilot-preset-regression.gainpilot";
  if (std::filesystem::exists(path)) {
    std::cerr << "Test path already exists: " << path << '\n';
    return 1;
  }
  for (std::size_t index = 0; index < gainpilot::kFactoryPresetNames.size(); ++index) {
    const auto preset = gainpilot::factoryPreset(index);
    if (!gainpilot::savePreset(path, preset)) return 1;
    const auto loaded = gainpilot::loadPreset(path);
    std::filesystem::remove(path);
    if (!loaded) return 1;
    for (auto id : gainpilot::kStateParamIds)
      if (preset.get(id) != loaded->get(id)) return 1;
  }
  if (gainpilot::loadPreset(path)) return 1;
  for (auto id : gainpilot::kStateParamIds) {
    state.set(id, std::numeric_limits<float>::quiet_NaN());
    if (!std::isfinite(state.get(id))) return 1;
  }
  std::cout << "Locked reference, reset, V4 migration and preset round trips passed\n";
}
