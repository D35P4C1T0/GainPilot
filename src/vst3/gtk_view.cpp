#include "gtk_view.hpp"

#include <array>
#include <cmath>
#include <cstdint>

#include <gtk/gtkx.h>

#include "base/source/fobject.h"
#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"

#include "gainpilot/ui/gtk_editor.hpp"

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

class GtkHostTimerHandler final : public Steinberg::FObject, public Steinberg::Linux::ITimerHandler {
public:
  explicit GtkHostTimerHandler(GainPilotGtkView* owner) : owner_(owner) {}

  void PLUGIN_API onTimer() override {
    if (owner_ != nullptr) {
      owner_->refreshFromModel();
    }
  }

  OBJ_METHODS(GtkHostTimerHandler, Steinberg::FObject)
  DEFINE_INTERFACES
    DEF_INTERFACE(Steinberg::Linux::ITimerHandler)
  END_DEFINE_INTERFACES(Steinberg::FObject)
  REFCOUNT_METHODS(Steinberg::FObject)

private:
  GainPilotGtkView* owner_{nullptr};
};

}  // namespace

GainPilotGtkView::GainPilotGtkView(GtkViewCallbacks callbacks)
    : Steinberg::CPluginView(&kDefaultViewRect), callbacks_(std::move(callbacks)) {}

GainPilotGtkView::~GainPilotGtkView() {
  destroyUi();
}

Steinberg::tresult PLUGIN_API GainPilotGtkView::isPlatformTypeSupported(Steinberg::FIDString type) {
  return Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeX11EmbedWindowID) ? Steinberg::kResultTrue
                                                                                     : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API GainPilotGtkView::attached(void* parent, Steinberg::FIDString type) {
  if (isPlatformTypeSupported(type) != Steinberg::kResultTrue || parent == nullptr) {
    return Steinberg::kResultFalse;
  }

  if (!gainpilot::ui::ensureGtkUiRuntime()) {
    return Steinberg::kResultFalse;
  }

  plug_ = gtk_plug_new(static_cast<Window>(reinterpret_cast<std::uintptr_t>(parent)));
  if (plug_ == nullptr) {
    return Steinberg::kResultFalse;
  }

  gainpilot::ui::GtkEditorCallbacks callbacks{
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
  editor_ = std::make_unique<gainpilot::ui::GainPilotGtkEditor>(std::move(callbacks), "VST3");
  gtk_container_add(GTK_CONTAINER(plug_), editor_->widget());
  gtk_widget_set_size_request(plug_, getRect().getWidth(), getRect().getHeight());
  gtk_widget_show_all(plug_);

  if (!registerHostTimer()) {
    destroyUi();
    return Steinberg::kResultFalse;
  }

  refreshFromModel();
  systemWindow = parent;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API GainPilotGtkView::removed() {
  destroyUi();
  systemWindow = nullptr;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API GainPilotGtkView::onSize(Steinberg::ViewRect* newSize) {
  const auto result = Steinberg::CPluginView::onSize(newSize);
  if (newSize != nullptr && plug_ != nullptr) {
    gtk_widget_set_size_request(plug_, newSize->getWidth(), newSize->getHeight());
    gtk_window_resize(GTK_WINDOW(plug_), newSize->getWidth(), newSize->getHeight());
  }
  return result;
}

Steinberg::tresult PLUGIN_API GainPilotGtkView::canResize() {
  return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API GainPilotGtkView::checkSizeConstraint(Steinberg::ViewRect* rect) {
  return rect != nullptr ? Steinberg::kResultTrue : Steinberg::kResultFalse;
}

void GainPilotGtkView::destroyUi() {
  if (runLoop_ != nullptr && timerHandler_ != nullptr) {
    runLoop_->unregisterTimer(timerHandler_.get());
  }
  timerHandler_ = nullptr;
  runLoop_ = nullptr;

  editor_.reset();

  if (plug_ != nullptr) {
    gtk_widget_destroy(plug_);
    plug_ = nullptr;
  }
}

bool GainPilotGtkView::registerHostTimer() {
  if (plugFrame == nullptr) {
    return false;
  }

  runLoop_ = Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop>(plugFrame);
  if (runLoop_ == nullptr) {
    return false;
  }

  timerHandler_ = Steinberg::owned(new GtkHostTimerHandler(this));
  return runLoop_->registerTimer(timerHandler_.get(), 16) == Steinberg::kResultTrue;
}

void GainPilotGtkView::refreshFromModel() {
  while (gtk_events_pending()) {
    gtk_main_iteration_do(FALSE);
  }

  if (!editor_) {
    return;
  }

  for (const auto id : kUiParameters) {
    if (callbacks_.getParameterValue) {
      editor_->setParameterValue(id, callbacks_.getParameterValue(id));
    }
  }

  if (callbacks_.getLatencyMilliseconds) {
    editor_->setLatencyMilliseconds(callbacks_.getLatencyMilliseconds());
  }
}

}  // namespace gainpilot::vst3
