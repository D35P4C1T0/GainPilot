#pragma once

#include <functional>
#include <memory>
#include <string>

#include <wx/nativewin.h>
#include <wx/timer.h>

#include "public.sdk/source/common/pluginview.h"

#include "gainpilot/parameters.hpp"

namespace gainpilot::ui {
class GainPilotEditorPanel;
}

namespace gainpilot::vst3 {

struct WxViewCallbacks {
  std::function<float(ParamId)> getParameterValue;
  std::function<float()> getMeterValue;
  std::function<float()> getLatencyMilliseconds;
  std::function<void(ParamId, float)> setParameterValue;
  std::function<void()> resetIntegrated;
};

class GainPilotWxView final : public Steinberg::CPluginView {
public:
  explicit GainPilotWxView(WxViewCallbacks callbacks);
  ~GainPilotWxView() SMTG_OVERRIDE;

  Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;

private:
  bool createHostContainer(void* parent, Steinberg::FIDString type);
  void destroyUi();
  void refreshFromModel();

  WxViewCallbacks callbacks_;
  wxNativeContainerWindow* container_{nullptr};
  gainpilot::ui::GainPilotEditorPanel* panel_{nullptr};
  std::unique_ptr<wxTimer> timer_{};
  bool wxInitialized_{false};
};

}  // namespace gainpilot::vst3
