#include "common.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "pluginterfaces/base/ustring.h"

namespace gainpilot::vst3 {

namespace {

template <typename ParameterT>
ParameterT* hideParameter(ParameterT* parameter) {
  parameter->getInfo().flags &= ~Steinberg::Vst::ParameterInfo::kCanAutomate;
  parameter->getInfo().flags |= Steinberg::Vst::ParameterInfo::kIsHidden;
  return parameter;
}

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

const std::array<const char*, 4> kCorrMixModeLabels{
    "Linear / Linear",
    "Linear / Log",
    "Log / Linear",
    "Log / Log",
};

const std::array<const char*, 3> kMeterModeLabels{
    "Momentary",
    "Short-Term",
    "Integrated",
};

const std::array<const char*, 2> kProgramModeLabels{
    "Auto",
    "Speech",
};

Steinberg::Vst::Parameter* makeParameter(ParamId id) {
  using Steinberg::Vst::Parameter;
  using Steinberg::Vst::ParameterInfo;
  using Steinberg::Vst::RangeParameter;

  switch (id) {
    case ParamId::targetLevel:
      return new LufsParameter(STR16("Target Level"), toVstParamId(id), -30.0, -10.0, -16.0);
    case ParamId::truePeak:
      return new DbParameter(STR16("True Peak"), toVstParamId(id), -10.0, 0.0, -1.0);
    case ParamId::maxGain:
      return new DbParameter(STR16("Max Gain"), toVstParamId(id), -10.0, 30.0, 17.0);
    case ParamId::inputTrim:
      return new DbParameter(STR16("Input Trim"), toVstParamId(id), -12.0, 12.0, 0.0);
    case ParamId::programMode:
      return makeEnumParameter(STR16("Program Mode"), toVstParamId(id), kProgramModeLabels, 0);
    case ParamId::freezeLevel: {
      return hideParameter(new LufsParameter(STR16("Freeze Level"), toVstParamId(id), -70.0, -10.0, -50.0));
    }
    case ParamId::inputLevel:
      return hideParameter(new SafeInputLevelParameter(STR16("Input Level"), toVstParamId(id), -40.0, 0.0, -23.0));
    case ParamId::correctionHigh:
      return hideParameter(new PercentParameter(STR16("Correction High"), toVstParamId(id), 100.0));
    case ParamId::correctionLow:
      return hideParameter(new PercentParameter(STR16("Correction Low"), toVstParamId(id), 100.0));
    case ParamId::corrMixMode:
      return hideParameter(makeEnumParameter(STR16("Corr Mix Mode"), toVstParamId(id), kCorrMixModeLabels, 0));
    case ParamId::meterMode:
      return hideParameter(makeEnumParameter(STR16("Meter Mode"), toVstParamId(id), kMeterModeLabels, 2));
    case ParamId::meterReset: {
      auto* parameter = new RangeParameter(
          STR16("Reset / Relearn"),
          toVstParamId(id),
          nullptr,
          0.0,
          1.0,
          0.0,
          1,
          ParameterInfo::kCanAutomate | ParameterInfo::kIsList);
      parameter->setPrecision(0);
      return hideParameter(parameter);
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
    case ParamId::inputIntegratedValue: {
      auto* parameter = new RangeParameter(
          STR16("Input LUFS-I"),
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
    case ParamId::outputIntegratedValue: {
      auto* parameter = new RangeParameter(
          STR16("Output LUFS-I"),
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
    case ParamId::outputShortTermValue: {
      auto* parameter = new RangeParameter(
          STR16("Output Short-Term"),
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
    case ParamId::gainReductionValue: {
      auto* parameter = new RangeParameter(
          STR16("Gain Reduction"),
          toVstParamId(id),
          STR16("dB"),
          0.0,
          24.0,
          0.0,
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
