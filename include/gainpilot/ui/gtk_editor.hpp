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
  void addCombo(GtkWidget* parent, ParamId param);
  void updateSlider(ParamId id, float value);
  void updateChoice(ParamId id, int value);
  void updateMeter(float value);

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
  GtkWidget* latencyLabel_{nullptr};
  GtkComboBox* corrMixModeCombo_{nullptr};
  GtkComboBox* meterModeCombo_{nullptr};
  bool suppressEvents_{false};
  std::array<float, kNumParameters> values_{};
  std::array<SliderBinding, 3> sliders_{};
};

bool ensureGtkUiRuntime();

}  // namespace gainpilot::ui
