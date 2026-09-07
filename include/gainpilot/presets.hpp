#pragma once

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include "gainpilot/state.hpp"

namespace gainpilot {

inline constexpr std::array<const char*, 3> kFactoryPresetNames{
    "Default", "Speech -16 LUFS", "Gentle -18 LUFS"};

inline ParameterState factoryPreset(std::size_t index) {
  ParameterState state;
  if (index == 1) {
    state.set(ParamId::programMode, 1);
    state.set(ParamId::maxGain, 12);
    state.set(ParamId::maxCut, 12);
  } else if (index == 2) {
    state.set(ParamId::targetLevel, -18);
    state.set(ParamId::maxGain, 6);
    state.set(ParamId::maxCut, 6);
    state.set(ParamId::correctionHigh, 50);
    state.set(ParamId::correctionLow, 50);
  }
  return state;
}

// File I/O is called only by the editor, never from the DSP callback.
inline bool savePreset(const std::filesystem::path& path, const ParameterState& state) {
  const auto bytes = serializeState(state);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  return !stream.fail();
}

inline std::optional<ParameterState> loadPreset(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  const auto size = stream.tellg();
  if (size < 0 || size > 4096) return std::nullopt;
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    return std::nullopt;
  return deserializeState(bytes);
}

} // namespace gainpilot
