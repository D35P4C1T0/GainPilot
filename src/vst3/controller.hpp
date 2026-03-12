#pragma once

#ifdef setState
#undef setState
#endif

#ifdef getState
#undef getState
#endif

#include "public.sdk/source/vst/vsteditcontroller.h"

#include "gainpilot/state.hpp"

namespace gainpilot::vst3 {

class GainPilotController final : public Steinberg::Vst::EditControllerEx1 {
public:
  static Steinberg::FUnknown* createInstance(void*) {
    return static_cast<Steinberg::Vst::IEditController*>(new GainPilotController());
  }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

  DEFINE_INTERFACES
  END_DEFINE_INTERFACES(Steinberg::Vst::EditControllerEx1)
  DELEGATE_REFCOUNT(Steinberg::Vst::EditControllerEx1)

private:
  void syncFromState(const gainpilot::ParameterState& state);
  gainpilot::ParameterState snapshotState();
};

}  // namespace gainpilot::vst3
