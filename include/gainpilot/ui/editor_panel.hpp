#pragma once

#include <array>
#include <functional>

#include <wx/panel.h>

#include "gainpilot/parameters.hpp"

class wxButton;
class wxChoice;
class wxGauge;
class wxSlider;
class wxStaticText;
class wxWindow;

namespace gainpilot::ui {

struct EditorCallbacks {
  std::function<void(ParamId, float)> setParameterValue;
  std::function<void()> resetIntegrated;
};

class GainPilotEditorPanel final : public wxPanel {
public:
  GainPilotEditorPanel(wxWindow* parent, EditorCallbacks callbacks);

  void setParameterValue(ParamId id, float value);
  void setLatencyMilliseconds(float latencyMs);
  void setLatencySamples(float latencySamples);

private:
  struct SliderWidgets {
    wxSlider* slider{nullptr};
    wxStaticText* value{nullptr};
  };

  void buildUi();
  void applyTheme();
  void addSliderRow(wxWindow* parent, ParamId id, const char* unit, int precision, int scale = 100);
  void addProgramModeChoice(wxWindow* parent);
  void updateSliderRow(ParamId id, float value);
  void updateChoice(ParamId id, int value);
  void updateMeter(float value);
  void updateReadout(wxStaticText* label, ParamId id, float value);

  static int toSliderValue(ParamId id, float value, int scale);
  static float fromSliderValue(ParamId id, int sliderValue, int scale);
  static wxString formatValue(ParamId id, float value);
  static wxString formatLatency(float latencyMs);
  static wxString formatLatencySamples(float latencySamples);

  EditorCallbacks callbacks_;
  std::array<float, kNumParameters> values_{};
  std::array<SliderWidgets, kNumParameters> sliderRows_{};
  std::array<int, kNumParameters> sliderScales_{};
  wxChoice* programModeChoice_{nullptr};
  wxGauge* meterGauge_{nullptr};
  wxStaticText* meterValueLabel_{nullptr};
  wxStaticText* inputIntegratedLabel_{nullptr};
  wxStaticText* outputIntegratedLabel_{nullptr};
  wxStaticText* outputShortTermLabel_{nullptr};
  wxStaticText* gainReductionLabel_{nullptr};
  wxStaticText* latencyLabel_{nullptr};
  bool suppressEvents_{false};
};

}  // namespace gainpilot::ui
