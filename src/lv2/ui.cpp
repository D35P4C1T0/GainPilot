#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>

#include <gtk/gtk.h>
#include <lv2/ui/ui.h>

#include "gainpilot/parameters.hpp"
#include "gainpilot/ui/gtk_editor.hpp"

namespace {

using gainpilot::ParamId;

constexpr std::uint32_t kControlBase = GAINPILOT_LV2_CHANNELS * 2;
constexpr std::uint32_t kTargetLevel = kControlBase + 0;
constexpr std::uint32_t kTruePeak = kControlBase + 1;
constexpr std::uint32_t kMaxGain = kControlBase + 2;
constexpr std::uint32_t kInputTrim = kControlBase + 3;
constexpr std::uint32_t kProgramMode = kControlBase + 4;
constexpr std::uint32_t kFreezeLevel = kControlBase + 5;
constexpr std::uint32_t kInputLevel = kControlBase + 6;
constexpr std::uint32_t kCorrectionHigh = kControlBase + 7;
constexpr std::uint32_t kCorrectionLow = kControlBase + 8;
constexpr std::uint32_t kCorrMixMode = kControlBase + 9;
constexpr std::uint32_t kMeterMode = kControlBase + 10;
constexpr std::uint32_t kMeterReset = kControlBase + 11;
constexpr std::uint32_t kMeterValue = kControlBase + 12;
constexpr std::uint32_t kInputIntegratedValue = kControlBase + 13;
constexpr std::uint32_t kOutputIntegratedValue = kControlBase + 14;
constexpr std::uint32_t kOutputShortTermValue = kControlBase + 15;
constexpr std::uint32_t kGainReductionValue = kControlBase + 16;
constexpr std::uint32_t kLatency = kControlBase + 17;

std::uint32_t portForParam(ParamId id) {
  switch (id) {
    case ParamId::targetLevel:
      return kTargetLevel;
    case ParamId::truePeak:
      return kTruePeak;
    case ParamId::maxGain:
      return kMaxGain;
    case ParamId::inputTrim:
      return kInputTrim;
    case ParamId::programMode:
      return kProgramMode;
    case ParamId::freezeLevel:
      return kFreezeLevel;
    case ParamId::inputLevel:
      return kInputLevel;
    case ParamId::correctionHigh:
      return kCorrectionHigh;
    case ParamId::correctionLow:
      return kCorrectionLow;
    case ParamId::corrMixMode:
      return kCorrMixMode;
    case ParamId::meterMode:
      return kMeterMode;
    case ParamId::meterReset:
      return kMeterReset;
    case ParamId::meterValue:
    case ParamId::count:
      break;
  }
  return std::numeric_limits<std::uint32_t>::max();
}

bool paramForPort(std::uint32_t port, ParamId& id) {
  switch (port) {
    case kTargetLevel:
      id = ParamId::targetLevel;
      return true;
    case kTruePeak:
      id = ParamId::truePeak;
      return true;
    case kMaxGain:
      id = ParamId::maxGain;
      return true;
    case kInputTrim:
      id = ParamId::inputTrim;
      return true;
    case kProgramMode:
      id = ParamId::programMode;
      return true;
    case kFreezeLevel:
      id = ParamId::freezeLevel;
      return true;
    case kInputLevel:
      id = ParamId::inputLevel;
      return true;
    case kCorrectionHigh:
      id = ParamId::correctionHigh;
      return true;
    case kCorrectionLow:
      id = ParamId::correctionLow;
      return true;
    case kCorrMixMode:
      id = ParamId::corrMixMode;
      return true;
    case kMeterMode:
      id = ParamId::meterMode;
      return true;
    case kMeterReset:
      id = ParamId::meterReset;
      return true;
    case kMeterValue:
      id = ParamId::meterValue;
      return true;
    case kInputIntegratedValue:
      id = ParamId::inputIntegratedValue;
      return true;
    case kOutputIntegratedValue:
      id = ParamId::outputIntegratedValue;
      return true;
    case kOutputShortTermValue:
      id = ParamId::outputShortTermValue;
      return true;
    case kGainReductionValue:
      id = ParamId::gainReductionValue;
      return true;
    default:
      return false;
  }
}

class GainPilotLv2Ui {
public:
  GainPilotLv2Ui(LV2UI_Write_Function writeFunction, LV2UI_Controller controller)
      : writeFunction_(writeFunction), controller_(controller) {
    gainpilot::ui::GtkEditorCallbacks callbacks{
        .setParameterValue =
            [this](ParamId id, float value) {
              const auto port = portForParam(id);
              if (port != std::numeric_limits<std::uint32_t>::max()) {
                sendFloat(port, value);
              }
            },
        .resetIntegrated =
            [this]() {
              sendReset();
            },
    };

    editor_ = std::make_unique<gainpilot::ui::GainPilotGtkEditor>(
        std::move(callbacks),
        GAINPILOT_LV2_CHANNELS == 1 ? "Mono" : "Stereo");
  }

  GtkWidget* widget() const { return editor_->widget(); }

  void portEvent(std::uint32_t portIndex, std::uint32_t bufferSize, std::uint32_t format, const void* buffer) {
    if (format != 0 || bufferSize != sizeof(float) || buffer == nullptr) {
      return;
    }

    const float value = *static_cast<const float*>(buffer);
    ParamId id{};
    if (paramForPort(portIndex, id)) {
      if (id != ParamId::freezeLevel && id != ParamId::meterReset) {
        editor_->setParameterValue(id, gainpilot::sanitizePlainValue(id, value));
      }
      return;
    }

    if (portIndex == kLatency) {
      editor_->setLatencySamples(value);
    }
  }

private:
  void sendFloat(std::uint32_t port, float value) const {
    if (writeFunction_ != nullptr) {
      writeFunction_(controller_, port, sizeof(float), 0, &value);
    }
  }

  void sendReset() const {
    float high = 1.0f;
    float low = 0.0f;
    sendFloat(kMeterReset, high);
    sendFloat(kMeterReset, low);
  }

  LV2UI_Write_Function writeFunction_{};
  LV2UI_Controller controller_{};
  std::unique_ptr<gainpilot::ui::GainPilotGtkEditor> editor_{};
};

LV2UI_Handle instantiate(const LV2UI_Descriptor*,
                         const char* pluginUri,
                         const char*,
                         LV2UI_Write_Function writeFunction,
                         LV2UI_Controller controller,
                         LV2UI_Widget* widget,
                         const LV2_Feature* const*) {
  if (pluginUri == nullptr || std::strcmp(pluginUri, GAINPILOT_LV2_URI) != 0 || widget == nullptr) {
    return nullptr;
  }
  if (!gainpilot::ui::ensureGtkUiRuntime()) {
    return nullptr;
  }

  auto* ui = new GainPilotLv2Ui(writeFunction, controller);
  *widget = ui->widget();
  return ui;
}

void cleanup(LV2UI_Handle ui) {
  delete static_cast<GainPilotLv2Ui*>(ui);
}

void portEvent(LV2UI_Handle ui, std::uint32_t portIndex, std::uint32_t bufferSize, std::uint32_t format, const void* buffer) {
  if (ui == nullptr) {
    return;
  }
  static_cast<GainPilotLv2Ui*>(ui)->portEvent(portIndex, bufferSize, format, buffer);
}

const void* extensionData(const char*) {
  return nullptr;
}

constexpr LV2UI_Descriptor kDescriptor{
    GAINPILOT_LV2_UI_URI,
    instantiate,
    cleanup,
    portEvent,
    extensionData,
};

}  // namespace

LV2_SYMBOL_EXPORT
const LV2UI_Descriptor* lv2ui_descriptor(std::uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
