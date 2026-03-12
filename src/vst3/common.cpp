#include "common.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "pluginterfaces/base/ustring.h"

namespace gainpilot::vst3 {

namespace {

class PercentParameter : public Steinberg::Vst::RangeParameter {
public:
  PercentParameter(const Steinberg::Vst::TChar* title, Steinberg::Vst::ParamID tag, double defaultValue)
      : RangeParameter(title,
                       tag,
                       STR16("%"),
                       0.0,
                       100.0,
                       defaultValue,
                       0,
                       Steinberg::Vst::ParameterInfo::kCanAutomate) {
    setPrecision(1);
  }
};

class DbParameter : public Steinberg::Vst::RangeParameter {
public:
  DbParameter(const Steinberg::Vst::TChar* title,
              Steinberg::Vst::ParamID tag,
              double minValue,
              double maxValue,
              double defaultValue)
      : RangeParameter(title,
                       tag,
                       STR16("dB"),
                       minValue,
                       maxValue,
                       defaultValue,
                       0,
                       Steinberg::Vst::ParameterInfo::kCanAutomate) {
    setPrecision(2);
  }
};

class LufsParameter : public Steinberg::Vst::RangeParameter {
public:
  LufsParameter(const Steinberg::Vst::TChar* title,
                Steinberg::Vst::ParamID tag,
                double minValue,
                double maxValue,
                double defaultValue)
      : RangeParameter(title,
                       tag,
                       STR16("LUFS"),
                       minValue,
                       maxValue,
                       defaultValue,
                       0,
                       Steinberg::Vst::ParameterInfo::kCanAutomate) {
    setPrecision(2);
  }
};

class SafeInputLevelParameter final : public LufsParameter {
public:
  using LufsParameter::LufsParameter;

  Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plainValue) const SMTG_OVERRIDE {
    const auto normalized = LufsParameter::toNormalized(plainValue);
    if (normalized >= 1.0) {
      return std::nextafter(1.0, 0.0);
    }
    return normalized;
  }

  void toString(Steinberg::Vst::ParamValue valueNormalized, Steinberg::Vst::String128 string) const SMTG_OVERRIDE {
    if (valueNormalized > std::nextafter(1.0, 0.0)) {
      valueNormalized = std::nextafter(1.0, 0.0);
    }

    auto plainValue = toPlain(valueNormalized);
    if (std::abs(plainValue) < 1.0e-4) {
      plainValue = 0.0;
    }
    Steinberg::UString wrapper(string, 128);
    if (!wrapper.printFloat(plainValue, precision)) {
      string[0] = 0;
    }
  }
};

void copyAsciiToString128(const char* text, Steinberg::Vst::String128 out) {
  Steinberg::UString(out, 128).fromAscii(text);
}

Steinberg::Vst::StringListParameter* makeEnumParameter(const Steinberg::Vst::TChar* title,
                                                       Steinberg::Vst::ParamID tag,
                                                       const auto& labels,
                                                       int32_t defaultIndex) {
  auto* parameter = new Steinberg::Vst::StringListParameter(
      title,
      tag,
      nullptr,
      Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);

  for (const char* label : labels) {
    Steinberg::Vst::String128 text{};
    copyAsciiToString128(label, text);
    parameter->appendString(text);
  }

  parameter->setNormalized(parameter->toNormalized(defaultIndex));
  return parameter;
}

}  // namespace

const std::array<const char*, 4>& corrMixModeLabels() {
  static const std::array<const char*, 4> labels{
      "Linear / Linear",
      "Linear / Log",
      "Log / Linear",
      "Log / Log",
  };
  return labels;
}

const std::array<const char*, 3>& meterModeLabels() {
  static const std::array<const char*, 3> labels{
      "Momentary",
      "Short-Term",
      "Integrated",
  };
  return labels;
}

Steinberg::Vst::Parameter* makeParameter(ParamId id) {
  using Steinberg::Vst::Parameter;
  using Steinberg::Vst::ParameterInfo;
  using Steinberg::Vst::RangeParameter;

  switch (id) {
    case ParamId::targetLevel:
      return new LufsParameter(STR16("Target Level"), toVstParamId(id), -30.0, -10.0, -16.0);
    case ParamId::truePeak:
      return new DbParameter(STR16("True Peak"), toVstParamId(id), -5.0, 0.0, -1.0);
    case ParamId::maxGain:
      return new DbParameter(STR16("Max Gain"), toVstParamId(id), -10.0, 30.0, 17.0);
    case ParamId::freezeLevel: {
      auto* parameter = new LufsParameter(STR16("Legacy Freeze"), toVstParamId(id), -70.0, -10.0, -50.0);
      parameter->getInfo().flags = ParameterInfo::kIsHidden;
      return parameter;
    }
    case ParamId::inputLevel:
      return new SafeInputLevelParameter(STR16("Input Level"), toVstParamId(id), -40.0, 0.0, -23.0);
    case ParamId::correctionHigh:
      return new PercentParameter(STR16("Correction High"), toVstParamId(id), 100.0);
    case ParamId::correctionLow:
      return new PercentParameter(STR16("Correction Low"), toVstParamId(id), 100.0);
    case ParamId::corrMixMode:
      return makeEnumParameter(STR16("Corr Mix Mode"), toVstParamId(id), corrMixModeLabels(), 0);
    case ParamId::meterMode:
      return makeEnumParameter(STR16("Meter Mode"), toVstParamId(id), meterModeLabels(), 0);
    case ParamId::meterReset: {
      auto* parameter = new RangeParameter(
          STR16("Reset Integrated"),
          toVstParamId(id),
          nullptr,
          0.0,
          1.0,
          0.0,
          1,
          ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
      parameter->setPrecision(0);
      return parameter;
    }
    case ParamId::meterValue: {
      auto* parameter = new RangeParameter(
          STR16("Input Loudness"),
          toVstParamId(id),
          STR16("LUFS"),
          -70.0,
          10.0,
          -70.0,
          0,
          ParameterInfo::kIsReadOnly);
      parameter->setPrecision(2);
      return parameter;
    }
    case ParamId::count:
      break;
  }

  return new Parameter();
}

}  // namespace gainpilot::vst3
