#include "gainpilot/ui/gtk_editor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace gainpilot::ui {

namespace {

GtkWidget* createPanel() {
  auto* frame = gtk_frame_new(nullptr);
  gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
  gtk_style_context_add_class(gtk_widget_get_style_context(frame), "gainpilot-panel");
  return frame;
}

GtkWidget* createPanelBox(GtkWidget* panel, GtkOrientation orientation, int spacing) {
  auto* box = gtk_box_new(orientation, spacing);
  gtk_container_add(GTK_CONTAINER(panel), box);
  return box;
}

GtkWidget* createLabel(const char* text, const char* cssClass, float xalign = 0.0f) {
  auto* label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), xalign);
  gtk_style_context_add_class(gtk_widget_get_style_context(label), cssClass);
  return label;
}

void ensureGtkCss() {
  static std::once_flag once;
  std::call_once(once, [] {
    auto* provider = gtk_css_provider_new();
    const char* css = R"css(
      .gainpilot-root {
        background: #f2eddc;
        color: #2d2419;
        padding: 14px;
      }
      .gainpilot-panel {
        background: #fffaf0;
        border: 1px solid #d5c4aa;
        border-radius: 12px;
        padding: 14px;
      }
      .gainpilot-title {
        font-weight: 700;
        font-size: 20px;
        color: #2d2419;
      }
      .gainpilot-subtitle {
        color: #6d5f4d;
      }
      .gainpilot-section {
        font-weight: 700;
        color: #2d2419;
      }
      .gainpilot-readout {
        font-weight: 700;
        color: #c55d1e;
        background: #fffef9;
        border: 1px solid #c5b598;
        border-radius: 8px;
        padding: 6px 10px;
      }
      .gainpilot-badge {
        font-weight: 700;
        color: #c55d1e;
        background: #fffef9;
        border: 1px solid #c5b598;
        border-radius: 999px;
        padding: 6px 12px;
      }
    )css";
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    if (auto* screen = gdk_screen_get_default()) {
      gtk_style_context_add_provider_for_screen(
          screen,
          GTK_STYLE_PROVIDER(provider),
          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
  });
}

std::once_flag gGtkInitOnce;
bool gGtkInitOk = false;
constexpr std::array<const char*, 2> kProgramModeLabels{
    "Auto",
    "Speech",
};
constexpr std::array<const char*, 4> kCorrMixModeLabels{
    "Linear / Linear",
    "Linear / Log",
    "Log / Linear",
    "Log / Log",
};
constexpr std::array<const char*, 3> kMeterModeLabels{
    "Momentary",
    "Short-Term",
    "Integrated",
};

}  // namespace

bool ensureGtkUiRuntime() {
  std::call_once(gGtkInitOnce, [] {
    int argc = 0;
    char** argv = nullptr;
    gGtkInitOk = gtk_init_check(&argc, &argv) != FALSE;
  });
  return gGtkInitOk;
}

GainPilotGtkEditor::GainPilotGtkEditor(GtkEditorCallbacks callbacks, const char* badgeText)
    : callbacks_(std::move(callbacks)) {
  ensureGtkCss();
  for (const auto& spec : kParameterSpecs) {
    values_[paramIndex(spec.id)] = spec.defaultValue;
  }
  build(badgeText);
}

GainPilotGtkEditor::~GainPilotGtkEditor() {
  if (root_ != nullptr) {
    gtk_widget_destroy(root_);
    root_ = nullptr;
  }
}

void GainPilotGtkEditor::setParameterValue(ParamId id, float value) {
  value = clampToSpec(id, value);
  values_[paramIndex(id)] = value;

  if (id == ParamId::meterValue) {
    updateReadout(meterValueLabel_, id, value);
    return;
  }

  if (id == ParamId::targetLevel || id == ParamId::truePeak || id == ParamId::maxGain || id == ParamId::inputTrim) {
    updateSlider(id, value);
    return;
  }

  if (id == ParamId::programMode) {
    updateChoice(id, static_cast<int>(std::lround(value)));
    return;
  }

  if (id == ParamId::inputIntegratedValue) {
    updateReadout(inputIntegratedLabel_, id, value);
    return;
  }

  if (id == ParamId::outputIntegratedValue) {
    updateReadout(outputIntegratedLabel_, id, value);
    return;
  }

  if (id == ParamId::outputShortTermValue) {
    updateReadout(outputShortTermLabel_, id, value);
    return;
  }

  if (id == ParamId::gainReductionValue) {
    updateMeter(value);
    updateReadout(gainReductionLabel_, id, value);
  }
}

void GainPilotGtkEditor::setLatencyMilliseconds(float latencyMs) {
  if (latencyLabel_ == nullptr) {
    return;
  }

  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "Latency: %.2f ms", latencyMs);
  gtk_label_set_text(GTK_LABEL(latencyLabel_), buffer);
}

void GainPilotGtkEditor::setLatencySamples(float latencySamples) {
  if (latencyLabel_ == nullptr) {
    return;
  }

  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "Latency: %.0f samples", latencySamples);
  gtk_label_set_text(GTK_LABEL(latencyLabel_), buffer);
}

void GainPilotGtkEditor::build(const char* badgeText) {
  root_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
  gtk_widget_set_size_request(root_, 860, 520);
  gtk_style_context_add_class(gtk_widget_get_style_context(root_), "gainpilot-root");

  auto* meterPanel = createPanel();
  gtk_box_pack_start(GTK_BOX(root_), meterPanel, FALSE, FALSE, 0);

  auto* contentColumn = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_box_pack_start(GTK_BOX(root_), contentColumn, TRUE, TRUE, 0);

  buildMeterPanel(meterPanel);
  buildHeader(contentColumn, badgeText);

  auto* controlsRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  gtk_box_pack_start(GTK_BOX(contentColumn), controlsRow, TRUE, TRUE, 0);

  buildTargetPanel(controlsRow);
  buildDynamicsPanel(controlsRow);

  gtk_widget_show_all(root_);
}

void GainPilotGtkEditor::buildMeterPanel(GtkWidget* panel) {
  auto* box = createPanelBox(panel, GTK_ORIENTATION_VERTICAL, 14);

  gtk_box_pack_start(GTK_BOX(box), createLabel("GAIN REDUCTION", "gainpilot-title", 0.5f), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), createLabel("Live readout", "gainpilot-subtitle", 0.5f), FALSE, FALSE, 0);

  meterBar_ = gtk_level_bar_new_for_interval(0.0, 24.0);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(meterBar_), GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request(meterBar_, 54, 250);
  gtk_level_bar_set_value(GTK_LEVEL_BAR(meterBar_), 0.0);
  gtk_box_pack_start(GTK_BOX(box), meterBar_, TRUE, TRUE, 0);

  gainReductionLabel_ = createLabel("0.00 dB", "gainpilot-readout", 0.5f);
  gtk_box_pack_start(GTK_BOX(box), gainReductionLabel_, FALSE, FALSE, 0);

  meterValueLabel_ = createLabel("In: -70.00 LUFS-I", "gainpilot-readout", 0.5f);
  gtk_box_pack_start(GTK_BOX(box), meterValueLabel_, FALSE, FALSE, 0);
  inputIntegratedLabel_ = createLabel("Input: -70.00 LUFS-I", "gainpilot-subtitle", 0.5f);
  gtk_box_pack_start(GTK_BOX(box), inputIntegratedLabel_, FALSE, FALSE, 0);
  outputIntegratedLabel_ = createLabel("Output: -70.00 LUFS-I", "gainpilot-subtitle", 0.5f);
  gtk_box_pack_start(GTK_BOX(box), outputIntegratedLabel_, FALSE, FALSE, 0);
  outputShortTermLabel_ = createLabel("Short-Term: -70.00 LUFS", "gainpilot-subtitle", 0.5f);
  gtk_box_pack_start(GTK_BOX(box), outputShortTermLabel_, FALSE, FALSE, 0);

  latencyLabel_ = createLabel("Latency: --", "gainpilot-subtitle", 0.5f);
  gtk_box_pack_end(GTK_BOX(box), latencyLabel_, FALSE, FALSE, 0);
}

void GainPilotGtkEditor::buildHeader(GtkWidget* parent, const char* badgeText) {
  auto* panel = createPanel();
  gtk_box_pack_start(GTK_BOX(parent), panel, FALSE, FALSE, 0);

  auto* row = createPanelBox(panel, GTK_ORIENTATION_HORIZONTAL, 12);

  auto* heading = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_pack_start(GTK_BOX(heading), createLabel("GainPilot", "gainpilot-title"), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(heading),
                     createLabel("Auto leveling with trim, speech mode, and relearn", "gainpilot-subtitle"),
                     FALSE,
                     FALSE,
                     0);
  gtk_box_pack_start(GTK_BOX(row), heading, TRUE, TRUE, 0);

  auto* badge = createLabel(badgeText, "gainpilot-badge", 0.5f);
  gtk_box_pack_end(GTK_BOX(row), badge, FALSE, FALSE, 0);
}

void GainPilotGtkEditor::buildTargetPanel(GtkWidget* parent) {
  auto* panel = createPanel();
  gtk_widget_set_hexpand(panel, TRUE);
  gtk_box_pack_start(GTK_BOX(parent), panel, TRUE, TRUE, 0);

  auto* box = createPanelBox(panel, GTK_ORIENTATION_VERTICAL, 12);
  gtk_box_pack_start(GTK_BOX(box), createLabel("Level Targeting", "gainpilot-section"), FALSE, FALSE, 0);

  addSlider(box, ParamId::targetLevel);
  addSlider(box, ParamId::inputTrim);
  addProgramModeChoice(box);
}

void GainPilotGtkEditor::buildDynamicsPanel(GtkWidget* parent) {
  auto* panel = createPanel();
  gtk_widget_set_hexpand(panel, TRUE);
  gtk_box_pack_start(GTK_BOX(parent), panel, TRUE, TRUE, 0);

  auto* box = createPanelBox(panel, GTK_ORIENTATION_VERTICAL, 12);
  gtk_box_pack_start(GTK_BOX(box), createLabel("Dynamics & Ceiling", "gainpilot-section"), FALSE, FALSE, 0);

  addSlider(box, ParamId::truePeak);
  addSlider(box, ParamId::maxGain);

  auto* reset = gtk_button_new_with_label("Reset / Relearn");
  g_signal_connect(reset, "clicked", G_CALLBACK(onResetClicked), this);
  gtk_box_pack_start(GTK_BOX(box), reset, FALSE, FALSE, 0);
}

void GainPilotGtkEditor::addSlider(GtkWidget* parent, ParamId param) {
  const auto& spec = parameterSpec(param);
  auto& binding = sliders_[sliderIndex(param)];
  binding.param = param;

  auto* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_pack_start(GTK_BOX(parent), row, FALSE, FALSE, 0);

  auto* headerRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(row), headerRow, FALSE, FALSE, 0);

  auto* label = gtk_label_new(spec.name.data());
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_box_pack_start(GTK_BOX(headerRow), label, TRUE, TRUE, 0);

  char buffer[64];
  binding.valueLabel = GTK_LABEL(gtk_label_new(formatValue(param, spec.defaultValue, buffer, sizeof(buffer))));
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(binding.valueLabel)), "gainpilot-readout");
  gtk_box_pack_end(GTK_BOX(headerRow), GTK_WIDGET(binding.valueLabel), FALSE, FALSE, 0);

  auto* scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, spec.minValue, spec.maxValue, 0.1);
  gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
  gtk_range_set_value(GTK_RANGE(scale), spec.defaultValue);
  gtk_widget_set_hexpand(scale, TRUE);
  g_object_set_data(G_OBJECT(scale), "gainpilot-editor", this);
  g_signal_connect(scale, "value-changed", G_CALLBACK(onSliderChanged), &binding);
  gtk_box_pack_start(GTK_BOX(row), scale, FALSE, FALSE, 0);
  binding.range = GTK_RANGE(scale);
}

void GainPilotGtkEditor::addProgramModeChoice(GtkWidget* parent) {
  gtk_box_pack_start(GTK_BOX(parent), createLabel("Program Mode", "gainpilot-subtitle"), FALSE, FALSE, 0);
  auto* combo = gtk_combo_box_text_new();
  for (const auto* label : kProgramModeLabels) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), label);
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), static_cast<int>(ProgramMode::automatic));
  g_object_set_data(G_OBJECT(combo), "gainpilot-param", GUINT_TO_POINTER(static_cast<unsigned>(ParamId::programMode)));
  g_signal_connect(combo, "changed", G_CALLBACK(onComboChanged), this);
  gtk_box_pack_start(GTK_BOX(parent), combo, FALSE, FALSE, 0);
  programModeCombo_ = GTK_COMBO_BOX(combo);
}

void GainPilotGtkEditor::updateSlider(ParamId id, float value) {
  auto& binding = sliders_[sliderIndex(id)];
  if (binding.range == nullptr || binding.valueLabel == nullptr) {
    return;
  }

  char buffer[64];
  suppressEvents_ = true;
  gtk_range_set_value(binding.range, value);
  gtk_label_set_text(binding.valueLabel, formatValue(id, value, buffer, sizeof(buffer)));
  suppressEvents_ = false;
}

void GainPilotGtkEditor::updateChoice(ParamId id, int value) {
  suppressEvents_ = true;
  if (id == ParamId::programMode && programModeCombo_ != nullptr) {
    gtk_combo_box_set_active(programModeCombo_, std::clamp(value, 0, static_cast<int>(kProgramModeLabels.size() - 1)));
  }
  suppressEvents_ = false;
}

void GainPilotGtkEditor::updateMeter(float value) {
  if (meterBar_ != nullptr) {
    gtk_level_bar_set_value(GTK_LEVEL_BAR(meterBar_), std::clamp(value, 0.0f, 24.0f));
  }
}

void GainPilotGtkEditor::updateReadout(GtkWidget* label, ParamId id, float value) {
  if (label != nullptr) {
    char buffer[64];
    gtk_label_set_text(GTK_LABEL(label), formatValue(id, value, buffer, sizeof(buffer)));
  }
}

void GainPilotGtkEditor::onSliderChanged(GtkRange* range, gpointer userData) {
  auto& binding = *static_cast<SliderBinding*>(userData);
  auto* editor = static_cast<GainPilotGtkEditor*>(g_object_get_data(G_OBJECT(range), "gainpilot-editor"));
  if (editor == nullptr) {
    return;
  }

  const auto value = static_cast<float>(gtk_range_get_value(range));
  editor->values_[paramIndex(binding.param)] = value;
  char buffer[64];
  gtk_label_set_text(binding.valueLabel, formatValue(binding.param, value, buffer, sizeof(buffer)));
  if (!editor->suppressEvents_ && editor->callbacks_.setParameterValue) {
    editor->callbacks_.setParameterValue(binding.param, value);
  }
}

void GainPilotGtkEditor::onComboChanged(GtkComboBox* comboBox, gpointer userData) {
  auto* editor = static_cast<GainPilotGtkEditor*>(userData);
  if (editor == nullptr || editor->suppressEvents_) {
    return;
  }

  const auto index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(comboBox), "gainpilot-param"));
  const auto id = static_cast<ParamId>(index);
  if (editor->callbacks_.setParameterValue) {
    editor->callbacks_.setParameterValue(id, static_cast<float>(gtk_combo_box_get_active(comboBox)));
  }
}

void GainPilotGtkEditor::onResetClicked(GtkButton*, gpointer userData) {
  auto* editor = static_cast<GainPilotGtkEditor*>(userData);
  if (editor != nullptr && editor->callbacks_.resetIntegrated) {
    editor->callbacks_.resetIntegrated();
  }
}

std::size_t GainPilotGtkEditor::paramIndex(ParamId id) {
  return static_cast<std::size_t>(id);
}

std::size_t GainPilotGtkEditor::sliderIndex(ParamId id) {
  switch (id) {
    case ParamId::targetLevel:
      return 0;
    case ParamId::truePeak:
      return 1;
    case ParamId::maxGain:
      return 2;
    case ParamId::inputTrim:
      return 3;
    default:
      return 0;
  }
}

const char* GainPilotGtkEditor::formatValue(ParamId id, float value, char* buffer, std::size_t size) {
  switch (id) {
    case ParamId::targetLevel:
    case ParamId::inputLevel:
    case ParamId::freezeLevel:
      std::snprintf(buffer, size, "%.2f LUFS", value);
      return buffer;
    case ParamId::meterValue:
      std::snprintf(buffer, size, "In: %.2f LUFS-I", value);
      return buffer;
    case ParamId::inputIntegratedValue:
      std::snprintf(buffer, size, "Input: %.2f LUFS-I", value);
      return buffer;
    case ParamId::outputIntegratedValue:
      std::snprintf(buffer, size, "Output: %.2f LUFS-I", value);
      return buffer;
    case ParamId::outputShortTermValue:
      std::snprintf(buffer, size, "Short-Term: %.2f LUFS", value);
      return buffer;
    case ParamId::truePeak:
    case ParamId::maxGain:
    case ParamId::inputTrim:
    case ParamId::gainReductionValue:
      std::snprintf(buffer, size, "%.2f dB", value);
      return buffer;
    case ParamId::correctionHigh:
    case ParamId::correctionLow:
      std::snprintf(buffer, size, "%.1f %%", value);
      return buffer;
    case ParamId::programMode:
      return kProgramModeLabels[std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(kProgramModeLabels.size() - 1))];
    case ParamId::corrMixMode:
      return kCorrMixModeLabels[std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(kCorrMixModeLabels.size() - 1))];
    case ParamId::meterMode:
      return kMeterModeLabels[std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(kMeterModeLabels.size() - 1))];
    case ParamId::meterReset:
      return value > 0.5f ? "Reset" : "Idle";
    case ParamId::count:
      break;
  }
  std::snprintf(buffer, size, "%.2f", value);
  return buffer;
}

}  // namespace gainpilot::ui
