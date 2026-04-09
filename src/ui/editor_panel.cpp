#include "gainpilot/ui/editor_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/gauge.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>

namespace gainpilot::ui {

namespace {

constexpr std::array<const char*, 2> kProgramModeLabels{
    "Auto",
    "Speech",
};

const wxColour kCanvas(0xF2, 0xED, 0xDC);
const wxColour kPanel(0xFF, 0xFA, 0xF0);
const wxColour kText(0x2D, 0x24, 0x19);
const wxColour kSubtle(0x6D, 0x5F, 0x4D);
const wxColour kAccent(0xC5, 0x5D, 0x1E);

std::size_t paramIndex(ParamId id) {
  return static_cast<std::size_t>(id);
}

wxStaticText* makeLabel(wxWindow* parent, const wxString& text, bool bold = false, const wxColour& color = kText) {
  auto* label = new wxStaticText(parent, wxID_ANY, text);
  auto font = label->GetFont();
  font.SetWeight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
  label->SetFont(font);
  label->SetForegroundColour(color);
  return label;
}

}  // namespace

GainPilotEditorPanel::GainPilotEditorPanel(wxWindow* parent, EditorCallbacks callbacks)
    : wxPanel(parent, wxID_ANY), callbacks_(std::move(callbacks)) {
  for (const auto& spec : kParameterSpecs) {
    values_[paramIndex(spec.id)] = spec.defaultValue;
    sliderScales_[paramIndex(spec.id)] = 100;
  }
  buildUi();
  applyTheme();
}

void GainPilotEditorPanel::setParameterValue(ParamId id, float value) {
  value = clampToSpec(id, value);
  values_[paramIndex(id)] = value;

  if (id == ParamId::meterValue) {
    updateReadout(meterValueLabel_, ParamId::meterValue, value);
    return;
  }

  if (id == ParamId::targetLevel || id == ParamId::truePeak || id == ParamId::maxGain || id == ParamId::inputTrim) {
    updateSliderRow(id, value);
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

void GainPilotEditorPanel::setLatencyMilliseconds(float latencyMs) {
  if (latencyLabel_ != nullptr) {
    latencyLabel_->SetLabel(formatLatency(latencyMs));
  }
}

void GainPilotEditorPanel::setLatencySamples(float latencySamples) {
  if (latencyLabel_ != nullptr) {
    latencyLabel_->SetLabel(formatLatencySamples(latencySamples));
  }
}

void GainPilotEditorPanel::buildUi() {
  auto* root = new wxBoxSizer(wxHORIZONTAL);
  auto* meterPanel = new wxPanel(this, wxID_ANY);
  auto* meterSizer = new wxBoxSizer(wxVERTICAL);
  meterPanel->SetSizer(meterSizer);

  meterSizer->Add(makeLabel(meterPanel, "GAIN REDUCTION", true), 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 8);
  meterSizer->Add(makeLabel(meterPanel, "Live readout", false, kSubtle), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

  meterGauge_ = new wxGauge(meterPanel, wxID_ANY, 1000, wxDefaultPosition, wxSize(54, 220), wxGA_VERTICAL);
  meterSizer->Add(meterGauge_, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

  gainReductionLabel_ = makeLabel(meterPanel, "0.00 dB", true, kAccent);
  meterSizer->Add(gainReductionLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 8);

  meterValueLabel_ = makeLabel(meterPanel, "In: -70.00 LUFS-I", false, kText);
  meterSizer->Add(meterValueLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 12);
  inputIntegratedLabel_ = makeLabel(meterPanel, "Input: -70.00 LUFS-I", false, kText);
  meterSizer->Add(inputIntegratedLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 6);
  outputIntegratedLabel_ = makeLabel(meterPanel, "Output: -70.00 LUFS-I", false, kText);
  meterSizer->Add(outputIntegratedLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 6);
  outputShortTermLabel_ = makeLabel(meterPanel, "Short-Term: -70.00 LUFS", false, kText);
  meterSizer->Add(outputShortTermLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 6);

  auto* contentSizer = new wxBoxSizer(wxVERTICAL);
  auto* headerPanel = new wxPanel(this, wxID_ANY);
  auto* headerSizer = new wxBoxSizer(wxHORIZONTAL);
  headerPanel->SetSizer(headerSizer);

  auto* titleSizer = new wxBoxSizer(wxVERTICAL);
  titleSizer->Add(makeLabel(headerPanel, "GainPilot", true), 0, wxBOTTOM, 4);
  titleSizer->Add(makeLabel(headerPanel, "Auto leveling with trim, speech mode, and relearn", false, kSubtle), 0);
  headerSizer->Add(titleSizer, 1, wxEXPAND);
  headerSizer->Add(makeLabel(headerPanel, "Mono / Stereo", true, kAccent), 0, wxALIGN_CENTER_VERTICAL);

  auto* controlsSizer = new wxBoxSizer(wxHORIZONTAL);
  auto* targetPanel = new wxPanel(this, wxID_ANY);
  auto* targetSizer = new wxBoxSizer(wxVERTICAL);
  targetPanel->SetSizer(targetSizer);
  targetSizer->Add(makeLabel(targetPanel, "Level Targeting", true), 0, wxBOTTOM, 10);
  addSliderRow(targetPanel, ParamId::targetLevel, "LUFS", 2);
  addSliderRow(targetPanel, ParamId::inputTrim, "dB", 2);
  addProgramModeChoice(targetPanel);
  targetSizer->AddStretchSpacer();

  auto* dynamicsPanel = new wxPanel(this, wxID_ANY);
  auto* dynamicsSizer = new wxBoxSizer(wxVERTICAL);
  dynamicsPanel->SetSizer(dynamicsSizer);
  dynamicsSizer->Add(makeLabel(dynamicsPanel, "Dynamics & Ceiling", true), 0, wxBOTTOM, 10);
  addSliderRow(dynamicsPanel, ParamId::truePeak, "dB", 2);
  addSliderRow(dynamicsPanel, ParamId::maxGain, "dB", 2);
  auto* relearn = new wxButton(dynamicsPanel, wxID_ANY, "Reset / Relearn");
  dynamicsSizer->Add(relearn, 0, wxTOP | wxBOTTOM, 8);
  relearn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (callbacks_.resetIntegrated) {
      callbacks_.resetIntegrated();
    }
  });
  dynamicsSizer->AddStretchSpacer();
  latencyLabel_ = makeLabel(dynamicsPanel, "Latency: 0.00 ms", false, kSubtle);
  dynamicsSizer->Add(latencyLabel_, 0, wxTOP, 10);

  controlsSizer->Add(targetPanel, 1, wxEXPAND | wxRIGHT, 12);
  controlsSizer->Add(dynamicsPanel, 1, wxEXPAND);

  contentSizer->Add(headerPanel, 0, wxEXPAND | wxBOTTOM, 12);
  contentSizer->Add(controlsSizer, 1, wxEXPAND);

  root->Add(meterPanel, 0, wxEXPAND | wxALL, 14);
  root->Add(contentSizer, 1, wxEXPAND | wxALL, 14);

  SetSizer(root);

  updateMeter(values_[paramIndex(ParamId::gainReductionValue)]);
  updateReadout(meterValueLabel_, ParamId::meterValue, values_[paramIndex(ParamId::meterValue)]);
  updateReadout(inputIntegratedLabel_, ParamId::inputIntegratedValue, values_[paramIndex(ParamId::inputIntegratedValue)]);
  updateReadout(outputIntegratedLabel_,
                ParamId::outputIntegratedValue,
                values_[paramIndex(ParamId::outputIntegratedValue)]);
  updateReadout(outputShortTermLabel_,
                ParamId::outputShortTermValue,
                values_[paramIndex(ParamId::outputShortTermValue)]);
  updateReadout(gainReductionLabel_, ParamId::gainReductionValue, values_[paramIndex(ParamId::gainReductionValue)]);
  setLatencyMilliseconds(0.0f);
}

void GainPilotEditorPanel::applyTheme() {
  SetBackgroundColour(kCanvas);
  SetForegroundColour(kText);

  auto apply = [](wxWindow* window) {
    if (window == nullptr) {
      return;
    }
    window->SetBackgroundColour(kPanel);
    window->SetForegroundColour(kText);
  };

  const auto children = GetChildren();
  for (auto* child : children) {
    apply(child);
  }
}

void GainPilotEditorPanel::addSliderRow(wxWindow* parent, ParamId id, const char*, int, int scale) {
  const auto& spec = parameterSpec(id);
  sliderScales_[paramIndex(id)] = scale;

  auto* parentSizer = parent->GetSizer();
  auto* label = makeLabel(parent, wxString::FromUTF8(spec.name.data(), spec.name.size()), false, kSubtle);
  parentSizer->Add(label, 0, wxBOTTOM, 4);

  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto* slider = new wxSlider(parent,
                              wxID_ANY,
                              toSliderValue(id, spec.defaultValue, scale),
                              0,
                              scale * 1000,
                              wxDefaultPosition,
                              wxDefaultSize,
                              wxSL_HORIZONTAL);
  auto* valueLabel = makeLabel(parent, formatValue(id, spec.defaultValue), true, kAccent);

  row->Add(slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
  row->Add(valueLabel, 0, wxALIGN_CENTER_VERTICAL);
  parentSizer->Add(row, 0, wxEXPAND | wxBOTTOM, 10);

  sliderRows_[paramIndex(id)] = SliderWidgets{slider, valueLabel};
  slider->Bind(wxEVT_SLIDER, [this, id, scale](wxCommandEvent& event) {
    const auto value = fromSliderValue(id, event.GetInt(), scale);
    values_[paramIndex(id)] = value;
    updateSliderRow(id, value);
    if (!suppressEvents_ && callbacks_.setParameterValue) {
      callbacks_.setParameterValue(id, value);
    }
  });
}

void GainPilotEditorPanel::addProgramModeChoice(wxWindow* parent) {
  auto* parentSizer = parent->GetSizer();
  parentSizer->Add(makeLabel(parent, "Program Mode", false, kSubtle), 0, wxBOTTOM, 4);
  programModeChoice_ = new wxChoice(parent, wxID_ANY);
  for (const auto* label : kProgramModeLabels) {
    programModeChoice_->Append(label);
  }
  programModeChoice_->SetSelection(static_cast<int>(ProgramMode::automatic));
  parentSizer->Add(programModeChoice_, 0, wxEXPAND | wxBOTTOM, 10);
  programModeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
    values_[paramIndex(ParamId::programMode)] = static_cast<float>(event.GetSelection());
    if (!suppressEvents_ && callbacks_.setParameterValue) {
      callbacks_.setParameterValue(ParamId::programMode, static_cast<float>(event.GetSelection()));
    }
  });
}

void GainPilotEditorPanel::updateSliderRow(ParamId id, float value) {
  const auto& row = sliderRows_[paramIndex(id)];
  if (row.slider == nullptr || row.value == nullptr) {
    return;
  }

  suppressEvents_ = true;
  row.slider->SetValue(toSliderValue(id, value, sliderScales_[paramIndex(id)]));
  row.value->SetLabel(formatValue(id, value));
  suppressEvents_ = false;
}

void GainPilotEditorPanel::updateChoice(ParamId id, int value) {
  suppressEvents_ = true;
  if (id == ParamId::programMode && programModeChoice_ != nullptr) {
    programModeChoice_->SetSelection(std::clamp(value, 0, static_cast<int>(kProgramModeLabels.size() - 1)));
  }
  suppressEvents_ = false;
}

void GainPilotEditorPanel::updateMeter(float value) {
  if (meterGauge_ != nullptr) {
    const auto normalized = static_cast<int>(std::clamp(value / 24.0f, 0.0f, 1.0f) * 1000.0f);
    meterGauge_->SetValue(normalized);
  }
}

void GainPilotEditorPanel::updateReadout(wxStaticText* label, ParamId id, float value) {
  if (label != nullptr) {
    label->SetLabel(formatValue(id, value));
  }
}

int GainPilotEditorPanel::toSliderValue(ParamId id, float value, int scale) {
  const auto& spec = parameterSpec(id);
  const auto clamped = clampToSpec(id, value);
  return static_cast<int>(std::lround((clamped - spec.minValue) / (spec.maxValue - spec.minValue) * (scale * 1000)));
}

float GainPilotEditorPanel::fromSliderValue(ParamId id, int sliderValue, int scale) {
  const auto& spec = parameterSpec(id);
  const auto normalized = static_cast<float>(sliderValue) / static_cast<float>(scale * 1000);
  return spec.minValue + normalized * (spec.maxValue - spec.minValue);
}

wxString GainPilotEditorPanel::formatValue(ParamId id, float value) {
  switch (id) {
    case ParamId::targetLevel:
    case ParamId::inputLevel:
    case ParamId::freezeLevel:
      return wxString::Format("%.2f LUFS", value);
    case ParamId::meterValue:
      return wxString::Format("In: %.2f LUFS-I", value);
    case ParamId::inputIntegratedValue:
      return wxString::Format("Input: %.2f LUFS-I", value);
    case ParamId::outputIntegratedValue:
      return wxString::Format("Output: %.2f LUFS-I", value);
    case ParamId::outputShortTermValue:
      return wxString::Format("Short-Term: %.2f LUFS", value);
    case ParamId::truePeak:
    case ParamId::maxGain:
    case ParamId::inputTrim:
      return wxString::Format("%.2f dB", value);
    case ParamId::gainReductionValue:
      return wxString::Format("%.2f dB", value);
    case ParamId::correctionHigh:
    case ParamId::correctionLow:
      return wxString::Format("%.1f %%", value);
    case ParamId::programMode:
      return kProgramModeLabels[std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(kProgramModeLabels.size() - 1))];
    case ParamId::corrMixMode:
      return "Legacy";
    case ParamId::meterMode:
      return "Integrated";
    case ParamId::meterReset:
      return value > 0.5f ? "Relearning" : "Ready";
    case ParamId::count:
      break;
  }
  return wxString::Format("%.2f", value);
}

wxString GainPilotEditorPanel::formatLatency(float latencyMs) {
  return wxString::Format("Latency: %.2f ms", latencyMs);
}

wxString GainPilotEditorPanel::formatLatencySamples(float latencySamples) {
  return wxString::Format("Latency: %.0f samples", latencySamples);
}

}  // namespace gainpilot::ui
