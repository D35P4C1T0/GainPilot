#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace gainpilot {

enum class ParamId : std::uint32_t {
  targetLevel,
  truePeak,
  maxGain,
  freezeLevel,
  inputLevel,
  correctionHigh,
  correctionLow,
  corrMixMode,
  meterMode,
  meterReset,
  meterValue,
  count
};

enum class MeterMode : std::uint32_t {
  momentary = 0,
  shortTerm = 1,
  integrated = 2
};

struct ParameterSpec {
  ParamId id;
  std::string_view key;
  std::string_view name;
  float minValue;
  float maxValue;
  float defaultValue;
  bool automatable;
  bool outputOnly;
};

inline constexpr std::array kParameterSpecs{
    ParameterSpec{ParamId::targetLevel, "target_level", "Target Level", -30.0f, -10.0f, -16.0f, true, false},
    ParameterSpec{ParamId::truePeak, "true_peak", "True Peak", -5.0f, 0.0f, -1.0f, true, false},
    ParameterSpec{ParamId::maxGain, "max_gain", "Max Gain", -10.0f, 30.0f, 17.0f, true, false},
    ParameterSpec{ParamId::freezeLevel, "freeze_level", "Legacy Freeze", -70.0f, -10.0f, -50.0f, true, false},
    ParameterSpec{ParamId::inputLevel, "input_level", "Input Level", -40.0f, 0.0f, -23.0f, true, false},
    ParameterSpec{ParamId::correctionHigh, "correction_high", "Correction High", 0.0f, 100.0f, 100.0f, true, false},
    ParameterSpec{ParamId::correctionLow, "correction_low", "Correction Low", 0.0f, 100.0f, 100.0f, true, false},
    ParameterSpec{ParamId::corrMixMode, "corr_mix_mode", "Corr Mix Mode", 0.0f, 3.0f, 0.0f, true, false},
    ParameterSpec{ParamId::meterMode, "meter_mode", "Meter Mode", 0.0f, 2.0f, 0.0f, true, false},
    ParameterSpec{ParamId::meterReset, "meter_reset", "Meter Reset", 0.0f, 1.0f, 0.0f, true, false},
    ParameterSpec{ParamId::meterValue, "meter_value", "Meter Value", -70.0f, 10.0f, -70.0f, false, true},
};

inline constexpr std::size_t kNumParameters = kParameterSpecs.size();

class ParameterState {
public:
  ParameterState();

  [[nodiscard]] float get(ParamId id) const;
  void set(ParamId id, float value);
  void setNormalized(ParamId id, float normalized);
  [[nodiscard]] float getNormalized(ParamId id) const;
  [[nodiscard]] std::span<const float, kNumParameters> values() const;
  [[nodiscard]] std::span<float, kNumParameters> values();

private:
  std::array<float, kNumParameters> values_{};
};

[[nodiscard]] const ParameterSpec& parameterSpec(ParamId id);
[[nodiscard]] float clampToSpec(ParamId id, float value);
[[nodiscard]] float sanitizePlainValue(ParamId id, float value);
[[nodiscard]] float normalizedToPlain(ParamId id, float normalized);
[[nodiscard]] float plainToNormalized(ParamId id, float plain);

}  // namespace gainpilot
