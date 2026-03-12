#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "gainpilot/parameters.hpp"

namespace gainpilot {

inline constexpr std::array kStateParamIds{
    ParamId::targetLevel,
    ParamId::truePeak,
    ParamId::maxGain,
    ParamId::freezeLevel,
    ParamId::inputLevel,
    ParamId::correctionHigh,
    ParamId::correctionLow,
    ParamId::corrMixMode,
    ParamId::meterMode,
};

namespace detail {

inline constexpr std::array<char, 4> kStateMagic{'G', 'P', 'S', '1'};

template <typename T>
void appendBytes(std::vector<std::byte>& out, const T& value) {
  const auto* begin = reinterpret_cast<const std::byte*>(&value);
  out.insert(out.end(), begin, begin + sizeof(T));
}

template <typename T>
bool readBytes(std::span<const std::byte> data, std::size_t& offset, T& value) {
  if (offset + sizeof(T) > data.size()) {
    return false;
  }

  std::memcpy(&value, data.data() + offset, sizeof(T));
  offset += sizeof(T);
  return true;
}

}  // namespace detail

inline std::vector<std::byte> serializeState(const ParameterState& state) {
  std::vector<std::byte> bytes;
  bytes.reserve(detail::kStateMagic.size() + sizeof(std::uint32_t) * 2 + sizeof(float) * kStateParamIds.size());

  for (const char value : detail::kStateMagic) {
    bytes.push_back(static_cast<std::byte>(value));
  }

  const std::uint32_t version = 1;
  const std::uint32_t count = static_cast<std::uint32_t>(kStateParamIds.size());
  detail::appendBytes(bytes, version);
  detail::appendBytes(bytes, count);
  for (const ParamId id : kStateParamIds) {
    detail::appendBytes(bytes, state.get(id));
  }

  return bytes;
}

inline std::optional<ParameterState> deserializeState(std::span<const std::byte> data) {
  if (data.size() < detail::kStateMagic.size() + sizeof(std::uint32_t) * 2) {
    return std::nullopt;
  }

  if (!std::equal(detail::kStateMagic.begin(), detail::kStateMagic.end(), reinterpret_cast<const char*>(data.data()))) {
    return std::nullopt;
  }

  std::size_t offset = detail::kStateMagic.size();
  std::uint32_t version = 0;
  std::uint32_t count = 0;
  if (!detail::readBytes(data, offset, version) || !detail::readBytes(data, offset, count)) {
    return std::nullopt;
  }

  if (version != 1 || count != kStateParamIds.size()) {
    return std::nullopt;
  }

  ParameterState state;
  for (const ParamId id : kStateParamIds) {
    float value = 0.0f;
    if (!detail::readBytes(data, offset, value)) {
      return std::nullopt;
    }
    state.set(id, value);
  }

  return state;
}

}  // namespace gainpilot
