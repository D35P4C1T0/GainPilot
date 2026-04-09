#pragma once

#include <functional>

#include "public.sdk/source/common/pluginview.h"

#include "gainpilot/parameters.hpp"

namespace gainpilot::vst3 {

struct MacViewCallbacks {
  std::function<float(ParamId)> getParameterValue;
  std::function<float()> getMeterValue;
  std::function<float()> getLatencyMilliseconds;
  std::function<void(ParamId, float)> setParameterValue;
  std::function<void()> resetIntegrated;
};

class GainPilotMacView final : public Steinberg::CPluginView {
public:
  explicit GainPilotMacView(MacViewCallbacks callbacks);
  ~GainPilotMacView() SMTG_OVERRIDE;

  Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;

  void handleSliderChanged(ParamId id, float value);
  void handleResetClicked();
  void refreshFromModel();

private:
  struct Impl;

  void destroyUi();
  void layoutUi(float width, float height);

  MacViewCallbacks callbacks_;
  Impl* impl_{nullptr};
  bool suppressCallbacks_{false};
};

}  // namespace gainpilot::vst3
