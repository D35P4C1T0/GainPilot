#pragma once

#include <functional>
#include <memory>

#include <gtk/gtk.h>

#include "public.sdk/source/common/pluginview.h"

#include "gainpilot/parameters.hpp"

namespace gainpilot::ui {
class GainPilotGtkEditor;
}

namespace gainpilot::vst3 {

struct GtkViewCallbacks {
  std::function<float(ParamId)> getParameterValue;
  std::function<float()> getMeterValue;
  std::function<float()> getLatencyMilliseconds;
  std::function<void(ParamId, float)> setParameterValue;
  std::function<void()> resetIntegrated;
};

class GainPilotGtkView final : public Steinberg::CPluginView {
public:
  explicit GainPilotGtkView(GtkViewCallbacks callbacks);
  ~GainPilotGtkView() SMTG_OVERRIDE;

  Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;
  void refreshFromModel();

private:
  void destroyUi();
  bool registerHostTimer();

  GtkViewCallbacks callbacks_;
  GtkWidget* plug_{nullptr};
  std::unique_ptr<gainpilot::ui::GainPilotGtkEditor> editor_{};
  Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop> runLoop_{};
  Steinberg::IPtr<Steinberg::Linux::ITimerHandler> timerHandler_{};
};

}  // namespace gainpilot::vst3
