#pragma once

#include <array>

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/funknown.h"

#include "gainpilot/parameters.hpp"
#include "gainpilot/version.hpp"

namespace gainpilot::vst3 {

inline constexpr char kCompanyName[] = "GainPilot contributors";
inline constexpr char kCompanyWeb[] = "https://gainpilot.dev";
inline constexpr char kCompanyEmail[] = "opensource@gainpilot.dev";
inline constexpr char kPluginCategory[] = "Fx|Dynamics";

inline const Steinberg::FUID kMonoProcessorCid(0xC3A0B497, 0x9C2B44A0, 0x9DBFD07B, 0x9D9D7CF1);
inline const Steinberg::FUID kStereoProcessorCid(0x0A835034, 0x00E04B33, 0x874F4B24, 0xFBE9BB10);
inline const Steinberg::FUID kControllerCid(0x1DD495EA, 0x45E54B92, 0xAAFE7B40, 0x51EB89BC);

Steinberg::Vst::Parameter* makeParameter(ParamId id);

[[nodiscard]] constexpr Steinberg::Vst::ParamID toVstParamId(ParamId id) {
  return static_cast<Steinberg::Vst::ParamID>(id);
}

[[nodiscard]] constexpr bool isRuntimeOutputParam(ParamId id) {
  return id == ParamId::meterValue;
}

[[nodiscard]] constexpr bool isAutomatableStateParam(ParamId id) {
  return id != ParamId::meterValue;
}

[[nodiscard]] const std::array<const char*, 4>& corrMixModeLabels();
[[nodiscard]] const std::array<const char*, 3>& meterModeLabels();

}  // namespace gainpilot::vst3
