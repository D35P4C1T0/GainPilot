#pragma once

#include <array>
#include <functional>

#include <gtk/gtk.h>

#include "gainpilot/parameters.hpp"

namespace gainpilot::ui {

struct GtkEditorCallbacks {
  std::function<void(ParamId, float)> setParameterValue;
  std::function<void()> resetIntegrated;
};

class GainPilotGtkEditor final {
public:
  explicit GainPilotGtkEditor(GtkEditorCallbacks callbacks, const char* badgeText);
  ~GainPilotGtkEditor();

  [[nodiscard]] GtkWidget* widget() const { return root_; }
  void setParameterValue(ParamId id, float value);
  void setLatencyMilliseconds(float latencyMs);
  void setLatencySamples(float latencySamples);

private:
  struct SliderBinding {
    ParamId param{};
    GtkRange* range{};
    GtkLabel* valueLabel{};
  };

  void build(const char* badgeText);
  void buildMeterPanel(GtkWidget* panel);
  void buildHeader(GtkWidget* parent, const char* badgeText);
  void buildTargetPanel(GtkWidget* parent);
  void buildDynamicsPanel(GtkWidget* parent);
  void addSlider(GtkWidget* parent, ParamId param);
  void addProgramModeChoice(GtkWidget* parent);
  void updateSlider(ParamId id, float value);
  void updateChoice(ParamId id, int value);
  void updateMeter(float value);
  void updateReadout(GtkWidget* label, ParamId id, float value);

  static void onSliderChanged(GtkRange* range, gpointer userData);
  static void onComboChanged(GtkComboBox* comboBox, gpointer userData);
  static void onResetClicked(GtkButton*, gpointer userData);
  static std::size_t paramIndex(ParamId id);
  static std::size_t sliderIndex(ParamId id);
  static const char* formatValue(ParamId id, float value, char* buffer, std::size_t size);

  GtkEditorCallbacks callbacks_;
  GtkWidget* root_{nullptr};
  GtkWidget* meterBar_{nullptr};
  GtkWidget* meterValueLabel_{nullptr};
  GtkWidget* inputIntegratedLabel_{nullptr};
  GtkWidget* outputIntegratedLabel_{nullptr};
  GtkWidget* outputShortTermLabel_{nullptr};
  GtkWidget* gainReductionLabel_{nullptr};
  GtkWidget* latencyLabel_{nullptr};
  GtkComboBox* programModeCombo_{nullptr};
  bool suppressEvents_{false};
  std::array<float, kNumParameters> values_{};
  std::array<SliderBinding, 4> sliders_{};
};

bool ensureGtkUiRuntime();

}  // namespace gainpilot::ui
