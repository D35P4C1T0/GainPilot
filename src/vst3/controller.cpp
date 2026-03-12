#include "controller.hpp"

#include "pluginterfaces/base/ibstream.h"

#include "common.hpp"
#include "gainpilot/state.hpp"

namespace gainpilot::vst3 {

Steinberg::tresult PLUGIN_API GainPilotController::initialize(Steinberg::FUnknown* context) {
  const auto result = Steinberg::Vst::EditControllerEx1::initialize(context);
  if (result != Steinberg::kResultOk) {
    return result;
  }

  parameters.removeAll();
  for (std::size_t index = 0; index < static_cast<std::size_t>(ParamId::count); ++index) {
    parameters.addParameter(makeParameter(static_cast<ParamId>(index)));
  }
  syncFromState(gainpilot::ParameterState{});
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API GainPilotController::setComponentState(Steinberg::IBStream* state) {
  if (state == nullptr) {
    return Steinberg::kResultFalse;
  }

  std::vector<std::byte> bytes(sizeof(std::uint32_t) * 2 + 4 + sizeof(float) * gainpilot::kStateParamIds.size());
  Steinberg::int32 bytesRead = 0;
  if (state->read(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &bytesRead) != Steinberg::kResultTrue ||
      bytesRead != static_cast<Steinberg::int32>(bytes.size())) {
    return Steinberg::kResultFalse;
  }

  const auto restored = gainpilot::deserializeState(bytes);
  if (!restored) {
    return Steinberg::kResultFalse;
  }

  syncFromState(*restored);
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API GainPilotController::setState(Steinberg::IBStream* state) {
  return setComponentState(state);
}

Steinberg::tresult PLUGIN_API GainPilotController::getState(Steinberg::IBStream* state) {
  if (state == nullptr) {
    return Steinberg::kResultFalse;
  }

  const auto bytes = gainpilot::serializeState(snapshotState());
  Steinberg::int32 bytesWritten = 0;
  return state->write(const_cast<std::byte*>(bytes.data()),
                      static_cast<Steinberg::int32>(bytes.size()),
                      &bytesWritten) == Steinberg::kResultTrue &&
                 bytesWritten == static_cast<Steinberg::int32>(bytes.size())
             ? Steinberg::kResultOk
             : Steinberg::kResultFalse;
}

Steinberg::IPlugView* PLUGIN_API GainPilotController::createView(Steinberg::FIDString) {
  return nullptr;
}

void GainPilotController::syncFromState(const gainpilot::ParameterState& state) {
  for (std::size_t index = 0; index < static_cast<std::size_t>(ParamId::count); ++index) {
    const auto id = static_cast<ParamId>(index);
    if (auto* parameter = parameters.getParameter(toVstParamId(id))) {
      parameter->setNormalized(state.getNormalized(id));
    }
  }
}

gainpilot::ParameterState GainPilotController::snapshotState() {
  gainpilot::ParameterState state;
  for (std::size_t index = 0; index < static_cast<std::size_t>(ParamId::count); ++index) {
    const auto id = static_cast<ParamId>(index);
    if (isAutomatableStateParam(id)) {
      state.setNormalized(id, getParamNormalized(toVstParamId(id)));
    }
  }
  state.set(ParamId::meterReset, 0.0f);
  state.set(ParamId::meterValue, -70.0f);
  return state;
}

}  // namespace gainpilot::vst3
