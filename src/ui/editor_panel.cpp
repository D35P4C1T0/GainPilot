#include "gainpilot/ui/editor_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
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
constexpr std::array<const char*, 2> kChannelModeLabels{
    "Stereo",
    "Mono",
};

const wxColour kCanvas(0x0B, 0x0D, 0x0E);
const wxColour kPanel(0x13, 0x15, 0x16);
const wxColour kText(0xC7, 0xCC, 0xCE);
const wxColour kSubtle(0x7A, 0x80, 0x82);
const wxColour kAccent(0x2E, 0xE6, 0xD6);

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

class GainHistoryPanel final : public wxPanel {
public:
  explicit GainHistoryPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(420, 220)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &GainHistoryPanel::onPaint, this);
  }

  void appendGain(float value) {
    history_.push_back(std::clamp(value, -12.0f, 12.0f));
    if (history_.size() > 180) {
      history_.erase(history_.begin());
    }
    Refresh(false);
  }

private:
  void onPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(0x0B, 0x0D, 0x0E)));
    dc.Clear();

    const auto size = GetClientSize();
    constexpr int kLeft = 38;
    constexpr int kTop = 12;
    const int graphWidth = std::max(1, size.GetWidth() - kLeft - 12);
    const int graphHeight = std::max(1, size.GetHeight() - kTop - 24);

    dc.SetFont(wxFontInfo(8).Family(wxFONTFAMILY_TELETYPE));
    dc.SetTextForeground(kSubtle);
    dc.SetPen(wxPen(wxColour(0x27, 0x2B, 0x2C), 1));
    for (int value = -12; value <= 12; value += 4) {
      const int y = kTop + static_cast<int>(std::lround(
                                   static_cast<double>(12 - value) / 24.0 * graphHeight));
      dc.DrawLine(kLeft, y, kLeft + graphWidth, y);
      dc.DrawText(wxString::Format("%+d", value), 5, y - 6);
    }

    if (history_.size() < 2) {
      return;
    }

    dc.SetPen(wxPen(kAccent, 2));
    wxPoint previous{};
    for (std::size_t index = 0; index < history_.size(); ++index) {
      const int x = kLeft + static_cast<int>(std::lround(
                                  static_cast<double>(index) /
                                  static_cast<double>(history_.size() - 1) * graphWidth));
      const int y = kTop + static_cast<int>(std::lround(
                                  static_cast<double>(12.0f - history_[index]) / 24.0 * graphHeight));
      const wxPoint current{x, y};
      if (index > 0) {
        dc.DrawLine(previous, current);
      }
      previous = current;
    }
  }

  std::vector<float> history_{};
};

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

  if (id == ParamId::programMode || id == ParamId::channelMode) {
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
    return;
  }

  if (id == ParamId::appliedGainValue) {
    updateReadout(appliedGainLabel_, id, value);
    if (gainGraph_ != nullptr) {
      gainGraph_->appendGain(value);
    }
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
  auto* root = new wxBoxSizer(wxVERTICAL);
  auto* headerPanel = new wxPanel(this, wxID_ANY);
  auto* headerSizer = new wxBoxSizer(wxHORIZONTAL);
  headerPanel->SetSizer(headerSizer);

  auto* titleSizer = new wxBoxSizer(wxVERTICAL);
  titleSizer->Add(makeLabel(headerPanel, "GainPilot", true), 0, wxBOTTOM, 1);
  titleSizer->Add(makeLabel(headerPanel, "Adaptive LUFS leveling - linked true-peak protection", false, kSubtle), 0);
  headerSizer->Add(titleSizer, 1, wxEXPAND);

  auto* badge = makeLabel(headerPanel, "BS.1770 / EBU R128", true, kAccent);
  headerSizer->Add(badge, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

  auto* bodySizer = new wxBoxSizer(wxHORIZONTAL);
  auto* targetPanel = new wxPanel(this, wxID_ANY);
  auto* targetSizer = new wxBoxSizer(wxVERTICAL);
  targetPanel->SetSizer(targetSizer);
  targetSizer->Add(makeLabel(targetPanel, "TARGET LUFS", true, kSubtle),
                   0,
                   wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM,
                   10);
  addTargetControl(targetPanel);

  meterValueLabel_ = makeLabel(targetPanel, "In: -70.00 LUFS-I", false, kText);
  targetSizer->Add(meterValueLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 8);
  inputIntegratedLabel_ = makeLabel(targetPanel, "Input: -70.00 LUFS-I", false, kSubtle);
  targetSizer->Add(inputIntegratedLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 3);
  outputIntegratedLabel_ = makeLabel(targetPanel, "Output: -70.00 LUFS-I", false, kSubtle);
  targetSizer->Add(outputIntegratedLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 3);
  outputShortTermLabel_ = makeLabel(targetPanel, "Short-Term: -70.00 LUFS", false, kSubtle);
  targetSizer->Add(outputShortTermLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 3);
  latencyLabel_ = makeLabel(targetPanel, "Latency: 0.00 ms", false, kSubtle);
  targetSizer->Add(latencyLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, 8);

  auto* responsePanel = new wxPanel(this, wxID_ANY);
  auto* responseSizer = new wxBoxSizer(wxVERTICAL);
  responsePanel->SetSizer(responseSizer);

  auto* modeRow = new wxBoxSizer(wxHORIZONTAL);
  auto* programColumn = new wxBoxSizer(wxVERTICAL);
  auto* channelColumn = new wxBoxSizer(wxVERTICAL);
  auto* programHost = new wxPanel(responsePanel, wxID_ANY);
  auto* channelHost = new wxPanel(responsePanel, wxID_ANY);
  programHost->SetSizer(programColumn);
  channelHost->SetSizer(channelColumn);
  addProgramModeChoice(programHost);
  addChannelModeChoice(channelHost);
  modeRow->Add(programHost, 1, wxEXPAND | wxRIGHT, 8);
  modeRow->Add(channelHost, 1, wxEXPAND);
  responseSizer->Add(modeRow, 0, wxEXPAND | wxALL, 10);

  gainGraph_ = new GainHistoryPanel(responsePanel);
  responseSizer->Add(gainGraph_, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);
  appliedGainLabel_ = makeLabel(responsePanel, "Current Gain: +0.00 dB", true, kAccent);
  responseSizer->Add(appliedGainLabel_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, 8);

  auto* dynamicsRow = new wxBoxSizer(wxHORIZONTAL);
  auto* inputTrimPanel = new wxPanel(responsePanel, wxID_ANY);
  auto* truePeakPanel = new wxPanel(responsePanel, wxID_ANY);
  auto* maxGainPanel = new wxPanel(responsePanel, wxID_ANY);
  inputTrimPanel->SetSizer(new wxBoxSizer(wxVERTICAL));
  truePeakPanel->SetSizer(new wxBoxSizer(wxVERTICAL));
  maxGainPanel->SetSizer(new wxBoxSizer(wxVERTICAL));
  addSliderRow(inputTrimPanel, ParamId::inputTrim, "dB", 2);
  addSliderRow(truePeakPanel, ParamId::truePeak, "dB", 2);
  addSliderRow(maxGainPanel, ParamId::maxGain, "dB", 2);
  dynamicsRow->Add(inputTrimPanel, 1, wxEXPAND | wxRIGHT, 8);
  dynamicsRow->Add(truePeakPanel, 1, wxEXPAND | wxRIGHT, 8);
  dynamicsRow->Add(maxGainPanel, 1, wxEXPAND);
  responseSizer->Add(dynamicsRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

  auto* footerRow = new wxBoxSizer(wxHORIZONTAL);
  gainReductionLabel_ = makeLabel(responsePanel, "Gain reduction: 0.00 dB", true, kText);
  meterGauge_ = new wxGauge(responsePanel, wxID_ANY, 1000);
  auto* relearn = new wxButton(responsePanel, wxID_ANY, "Reset / Relearn");
  footerRow->Add(gainReductionLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  footerRow->Add(meterGauge_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
  footerRow->Add(relearn, 0, wxALIGN_CENTER_VERTICAL);
  responseSizer->Add(footerRow, 0, wxEXPAND | wxALL, 10);

  relearn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (callbacks_.resetIntegrated) {
      callbacks_.resetIntegrated();
    }
  });

  bodySizer->Add(targetPanel, 0, wxEXPAND | wxRIGHT, 8);
  bodySizer->Add(responsePanel, 1, wxEXPAND);
  root->Add(headerPanel, 0, wxEXPAND | wxALL, 8);
  root->Add(bodySizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

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
  updateReadout(appliedGainLabel_, ParamId::appliedGainValue, values_[paramIndex(ParamId::appliedGainValue)]);
  setLatencyMilliseconds(0.0f);
}

void GainPilotEditorPanel::applyTheme() {
  SetBackgroundColour(kCanvas);
  SetForegroundColour(kText);

  std::function<void(wxWindow*)> apply = [&apply](wxWindow* window) {
    if (window == nullptr) {
      return;
    }
    window->SetBackgroundColour(kPanel);
    window->SetForegroundColour(kText);
    for (auto* child : window->GetChildren()) {
      apply(child);
    }
  };

  for (auto* child : GetChildren()) {
    apply(child);
  }
}

void GainPilotEditorPanel::addTargetControl(wxWindow* parent) {
  const auto id = ParamId::targetLevel;
  const auto& spec = parameterSpec(id);
  constexpr int kScale = 100;
  sliderScales_[paramIndex(id)] = kScale;

  auto* parentSizer = parent->GetSizer();
  auto* ruler = new wxBoxSizer(wxHORIZONTAL);
  auto* scaleLabels = new wxBoxSizer(wxVERTICAL);
  for (const int value : {-10, -14, -18, -22, -26, -30}) {
    scaleLabels->Add(makeLabel(parent, wxString::Format("%d", value), false, kSubtle),
                     value == -30 ? 0 : 1,
                     value == -30 ? 0 : wxEXPAND);
  }

  auto* slider = new wxSlider(parent,
                              wxID_ANY,
                              toSliderValue(id, spec.defaultValue, kScale),
                              0,
                              kScale * 1000,
                              wxDefaultPosition,
                              wxSize(34, 286),
                              wxSL_VERTICAL);
  ruler->Add(scaleLabels, 0, wxEXPAND | wxRIGHT, 8);
  ruler->Add(slider, 0, wxEXPAND);
  parentSizer->Add(ruler, 1, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, 18);

  auto* valueLabel = makeLabel(parent, formatValue(id, spec.defaultValue), true, kAccent);
  parentSizer->Add(valueLabel, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 8);
  sliderRows_[paramIndex(id)] = SliderWidgets{slider, valueLabel};
  slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent& event) {
    const auto value = fromSliderValue(ParamId::targetLevel, event.GetInt(), kScale);
    values_[paramIndex(ParamId::targetLevel)] = value;
    updateSliderRow(ParamId::targetLevel, value);
    if (!suppressEvents_ && callbacks_.setParameterValue) {
      callbacks_.setParameterValue(ParamId::targetLevel, value);
    }
  });
}

void GainPilotEditorPanel::addSliderRow(wxWindow* parent, ParamId id, const char*, int, int scale) {
  const auto& spec = parameterSpec(id);
  sliderScales_[paramIndex(id)] = scale;

  auto* parentSizer = parent->GetSizer();
  auto* label = makeLabel(parent, wxString::FromUTF8(spec.name.data(), spec.name.size()), false, kSubtle);
  parentSizer->Add(label, 0, wxBOTTOM, 1);

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

  row->Add(slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  row->Add(valueLabel, 0, wxALIGN_CENTER_VERTICAL);
  parentSizer->Add(row, 0, wxEXPAND | wxBOTTOM, 6);

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
  parentSizer->Add(makeLabel(parent, "Program Mode", false, kSubtle), 0, wxBOTTOM, 1);
  programModeChoice_ = new wxChoice(parent, wxID_ANY);
  for (const auto* label : kProgramModeLabels) {
    programModeChoice_->Append(label);
  }
  programModeChoice_->SetSelection(static_cast<int>(ProgramMode::automatic));
  parentSizer->Add(programModeChoice_, 0, wxEXPAND | wxBOTTOM, 6);
  programModeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
    values_[paramIndex(ParamId::programMode)] = static_cast<float>(event.GetSelection());
    if (!suppressEvents_ && callbacks_.setParameterValue) {
      callbacks_.setParameterValue(ParamId::programMode, static_cast<float>(event.GetSelection()));
    }
  });
}

void GainPilotEditorPanel::addChannelModeChoice(wxWindow* parent) {
  auto* parentSizer = parent->GetSizer();
  parentSizer->Add(makeLabel(parent, "Channel Mode", false, kSubtle), 0, wxBOTTOM, 1);
  channelModeChoice_ = new wxChoice(parent, wxID_ANY);
  for (const auto* label : kChannelModeLabels) {
    channelModeChoice_->Append(label);
  }
  channelModeChoice_->SetSelection(static_cast<int>(ChannelMode::stereo));
  parentSizer->Add(channelModeChoice_, 0, wxEXPAND | wxBOTTOM, 6);
  channelModeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
    values_[paramIndex(ParamId::channelMode)] = static_cast<float>(event.GetSelection());
    if (!suppressEvents_ && callbacks_.setParameterValue) {
      callbacks_.setParameterValue(ParamId::channelMode, static_cast<float>(event.GetSelection()));
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
  if (id == ParamId::channelMode && channelModeChoice_ != nullptr) {
    channelModeChoice_->SetSelection(std::clamp(value, 0, static_cast<int>(kChannelModeLabels.size() - 1)));
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
    case ParamId::appliedGainValue:
      return wxString::Format("Current Gain: %+.2f dB", value);
    case ParamId::correctionHigh:
    case ParamId::correctionLow:
      return wxString::Format("%.1f %%", value);
    case ParamId::programMode:
      return kProgramModeLabels[std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(kProgramModeLabels.size() - 1))];
    case ParamId::channelMode:
      return kChannelModeLabels[std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(kChannelModeLabels.size() - 1))];
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
