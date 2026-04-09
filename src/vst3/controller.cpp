#include "controller.hpp"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/gui/iplugview.h"

#include "common.hpp"
#include "gainpilot/state.hpp"

#if defined(__APPLE__)
#include "mac_view.hpp"
#endif

#if GAINPILOT_VST3_USE_WX_VIEW
#include "wx_view.hpp"
#endif

namespace gainpilot::vst3 {

namespace {

constexpr float kLatencyMilliseconds = 35.375f;

float plainParameterValue(GainPilotController& controller, ParamId id) {
  return normalizedToPlain(id, static_cast<float>(controller.getParamNormalized(toVstParamId(id))));
}

float meterValue(GainPilotController& controller) {
  return plainParameterValue(controller, ParamId::meterValue);
}

void performPlainParameterEdit(GainPilotController& controller, ParamId id, float value) {
  if (!isAutomatableStateParam(id)) {
    return;
  }

  const auto normalized = static_cast<Steinberg::Vst::ParamValue>(plainToNormalized(id, value));
  const auto paramId = toVstParamId(id);
  controller.beginEdit(paramId);
  controller.setParamNormalized(paramId, normalized);
  controller.performEdit(paramId, normalized);
  controller.endEdit(paramId);
}

void triggerIntegratedReset(GainPilotController& controller) {
  const auto paramId = toVstParamId(ParamId::meterReset);
  controller.beginEdit(paramId);
  controller.setParamNormalized(paramId, 1.0);
  controller.performEdit(paramId, 1.0);
  controller.setParamNormalized(paramId, 0.0);
  controller.performEdit(paramId, 0.0);
  controller.endEdit(paramId);
}

}  // namespace

Steinberg::tresult PLUGIN_API GainPilotController::initialize(Steinberg::FUnknown* context) {
  const auto result = Steinberg::Vst::EditControllerEx1::initialize(context);
  if (result != Steinberg::kResultOk) {
    return result;
  }

  parameters.removeAll();
  for (const ParamId id : kExportedVstParamIds) {
    parameters.addParameter(makeParameter(id));
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

Steinberg::IPlugView* PLUGIN_API GainPilotController::createView(Steinberg::FIDString name) {
  if (!Steinberg::FIDStringsEqual(name, Steinberg::Vst::ViewType::kEditor)) {
    return nullptr;
  }

#if defined(__APPLE__)
  return static_cast<Steinberg::IPlugView*>(new GainPilotMacView({
      .getParameterValue =
          [this](ParamId id) {
            return plainParameterValue(*this, id);
          },
      .getMeterValue =
          [this]() {
            return meterValue(*this);
          },
      .getLatencyMilliseconds =
          []() {
            return kLatencyMilliseconds;
          },
      .setParameterValue =
          [this](ParamId id, float value) {
            performPlainParameterEdit(*this, id, value);
          },
      .resetIntegrated =
          [this]() {
            triggerIntegratedReset(*this);
          },
  }));
#elif GAINPILOT_VST3_USE_WX_VIEW
  return static_cast<Steinberg::IPlugView*>(new GainPilotWxView({
      .getParameterValue =
          [this](ParamId id) {
            return plainParameterValue(*this, id);
          },
      .getMeterValue =
          [this]() {
            return meterValue(*this);
          },
      .getLatencyMilliseconds =
          []() {
            return kLatencyMilliseconds;
          },
      .setParameterValue =
          [this](ParamId id, float value) {
            performPlainParameterEdit(*this, id, value);
          },
      .resetIntegrated =
          [this]() {
            triggerIntegratedReset(*this);
          },
  }));
#else
  return nullptr;
#endif
}

void GainPilotController::syncFromState(const gainpilot::ParameterState& state) {
  for (const auto id : kExportedVstParamIds) {
    if (auto* parameter = parameters.getParameter(toVstParamId(id))) {
      parameter->setNormalized(state.getNormalized(id));
    }
  }
}

gainpilot::ParameterState GainPilotController::snapshotState() {
  gainpilot::ParameterState state;
  for (const auto id : kExportedVstParamIds) {
    if (isAutomatableStateParam(id)) {
      state.setNormalized(id, getParamNormalized(toVstParamId(id)));
    }
  }
  state.set(ParamId::meterReset, 0.0f);
  state.set(ParamId::meterValue, -70.0f);
  state.set(ParamId::inputIntegratedValue, -70.0f);
  state.set(ParamId::outputIntegratedValue, -70.0f);
  state.set(ParamId::outputShortTermValue, -70.0f);
  state.set(ParamId::gainReductionValue, 0.0f);
  return state;
}

}  // namespace gainpilot::vst3
