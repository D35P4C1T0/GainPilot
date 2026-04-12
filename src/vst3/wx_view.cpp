#include "wx_view.hpp"

#include <array>
#include <cmath>
#include <cstdint>

#include <wx/init.h>
#include <wx/sizer.h>

#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/gui/iplugview.h"

#include "gainpilot/ui/editor_panel.hpp"
#include "gainpilot/ui/wx_runtime.hpp"

namespace gainpilot::vst3 {

namespace {

const Steinberg::ViewRect kDefaultViewRect{0, 0, 800, 470};
constexpr std::array<ParamId, 10> kUiParameters{
    ParamId::targetLevel,
    ParamId::truePeak,
    ParamId::maxGain,
    ParamId::inputTrim,
    ParamId::programMode,
    ParamId::meterValue,
    ParamId::inputIntegratedValue,
    ParamId::outputIntegratedValue,
    ParamId::outputShortTermValue,
    ParamId::gainReductionValue,
};

}  // namespace

GainPilotWxView::GainPilotWxView(WxViewCallbacks callbacks)
    : Steinberg::CPluginView(&kDefaultViewRect), callbacks_(std::move(callbacks)) {}

GainPilotWxView::~GainPilotWxView() {
  destroyUi();
}

Steinberg::tresult PLUGIN_API GainPilotWxView::isPlatformTypeSupported(Steinberg::FIDString type) {
#if defined(_WIN32)
  return Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeHWND) ? Steinberg::kResultTrue
                                                                         : Steinberg::kResultFalse;
#elif defined(__APPLE__)
  return Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeNSView) ? Steinberg::kResultTrue
                                                                           : Steinberg::kResultFalse;
#elif defined(__linux__)
  return Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeX11EmbedWindowID) ? Steinberg::kResultTrue
                                                                                      : Steinberg::kResultFalse;
#else
  (void)type;
  return Steinberg::kResultFalse;
#endif
}

Steinberg::tresult PLUGIN_API GainPilotWxView::attached(void* parent, Steinberg::FIDString type) {
  if (isPlatformTypeSupported(type) != Steinberg::kResultTrue) {
    return Steinberg::kResultFalse;
  }

  if (!gainpilot::ui::acquireWxRuntime()) {
    return Steinberg::kResultFalse;
  }
  wxInitialized_ = true;

  if (!createHostContainer(parent, type)) {
    destroyUi();
    return Steinberg::kResultFalse;
  }

  auto callbacks = gainpilot::ui::EditorCallbacks{
      .setParameterValue =
          [this](ParamId id, float value) {
            if (callbacks_.setParameterValue) {
              callbacks_.setParameterValue(id, value);
            }
          },
      .resetIntegrated =
          [this]() {
            if (callbacks_.resetIntegrated) {
              callbacks_.resetIntegrated();
            }
          },
  };

  panel_ = new gainpilot::ui::GainPilotEditorPanel(container_, std::move(callbacks));
  auto* sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(panel_, 1, wxEXPAND);
  container_->SetSizer(sizer);
  container_->SetSize(getRect().getWidth(), getRect().getHeight());
  container_->Layout();
  container_->Show();

  timer_ = std::make_unique<wxTimer>();
  timer_->SetOwner(panel_);
  panel_->Bind(
      wxEVT_TIMER,
      [this](wxTimerEvent&) {
        refreshFromModel();
      },
      timer_->GetId());
  timer_->Start(50);

  systemWindow = parent;
  refreshFromModel();
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API GainPilotWxView::removed() {
  destroyUi();
  systemWindow = nullptr;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API GainPilotWxView::onSize(Steinberg::ViewRect* newSize) {
  const auto result = Steinberg::CPluginView::onSize(newSize);
  if (newSize != nullptr && container_ != nullptr) {
    container_->SetSize(newSize->getWidth(), newSize->getHeight());
    container_->Layout();
  }
  return result;
}

Steinberg::tresult PLUGIN_API GainPilotWxView::canResize() {
  return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API GainPilotWxView::checkSizeConstraint(Steinberg::ViewRect* rect) {
  return rect != nullptr ? Steinberg::kResultTrue : Steinberg::kResultFalse;
}

bool GainPilotWxView::createHostContainer(void* parent, Steinberg::FIDString type) {
  container_ = new wxNativeContainerWindow();

#if defined(_WIN32)
  if (Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeHWND)) {
    return container_->Create(reinterpret_cast<wxNativeContainerWindowHandle>(parent));
  }
#elif defined(__APPLE__)
  if (Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeNSView)) {
    return container_->Create(reinterpret_cast<wxNativeContainerWindowHandle>(parent));
  }
#elif defined(__linux__)
  if (Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeX11EmbedWindowID)) {
    const auto parentId = static_cast<wxNativeContainerWindowId>(reinterpret_cast<std::uintptr_t>(parent));
    return container_->Create(parentId);
  }
#endif

  return false;
}

void GainPilotWxView::destroyUi() {
  if (timer_ != nullptr) {
    timer_->Stop();
    timer_.reset();
  }

  if (container_ != nullptr) {
    delete container_;
    container_ = nullptr;
    panel_ = nullptr;
  }

  if (wxInitialized_) {
    gainpilot::ui::releaseWxRuntime();
    wxInitialized_ = false;
  }
}

void GainPilotWxView::refreshFromModel() {
  if (panel_ == nullptr) {
    return;
  }

  for (const auto id : kUiParameters) {
    if (callbacks_.getParameterValue) {
      panel_->setParameterValue(id, callbacks_.getParameterValue(id));
    }
  }

  if (callbacks_.getLatencyMilliseconds) {
    panel_->setLatencyMilliseconds(callbacks_.getLatencyMilliseconds());
  }
}

}  // namespace gainpilot::vst3
