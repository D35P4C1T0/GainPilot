#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>

#include "gainpilot/parameters.hpp"
#include "gainpilot/presets.hpp"
#include "gainpilot/ui/gain_history.hpp"

START_NAMESPACE_DISTRHO

namespace {

using gainpilot::ParamId;

constexpr float kDesignWidth = 960.0f;
constexpr float kDesignHeight = 544.0f;
constexpr std::array<ParamId, 5> kSliderParameters{
    ParamId::targetLevel,
    ParamId::inputTrim,
    ParamId::truePeak,
    ParamId::maxGain,
    ParamId::maxCut,
};
constexpr std::array<float, 4> kUiScales{0.75f, 1.0f, 1.25f, 1.5f};
constexpr std::array<const char *, 4> kUiScaleLabels{"75%", "100%", "125%",
                                                     "150%"};

constexpr std::uint32_t paramIndex(const ParamId id) noexcept {
  return static_cast<std::uint32_t>(id);
}

struct Bounds {
  float x{};
  float y{};
  float width{};
  float height{};

  bool contains(const float px, const float py) const noexcept {
    return px >= x && py >= y && px <= x + width && py <= y + height;
  }
};

constexpr Bounds kTargetTrack{50, 370, 140, 22};
constexpr Bounds kTargetDial{40, 104, 264, 252};
constexpr Bounds kSettings{347, 78, 598, 365};

Bounds sliderBounds(const ParamId id) noexcept {
  switch (id) {
  case ParamId::targetLevel: return kTargetTrack;
  case ParamId::inputTrim: return {382, 325, 102, 117};
  case ParamId::truePeak: return {587, 325, 102, 117};
  case ParamId::maxGain: return {790, 325, 102, 117};
  case ParamId::maxCut: return {779, 146, 102, 130};
  default: return {};
  }
}

Bounds scaleButtonBounds(const std::size_t index) noexcept {
  return {729 + static_cast<float>(index) * 44, 21, 44, 27};
}

const char *shortLabel(const ParamId id) noexcept {
  switch (id) {
  case ParamId::targetLevel:
    return "TARGET LEVEL";
  case ParamId::inputTrim:
    return "INPUT TRIM";
  case ParamId::truePeak:
    return "TRUE-PEAK CEILING";
  case ParamId::maxGain:
    return "MAX GAIN";
  case ParamId::maxCut:
    return "MAX CUT";
  default:
    return "";
  }
}

} // namespace

class GainPilotDPFUI final : public UI {
public:
  GainPilotDPFUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT) {
    for (const gainpilot::ParameterSpec &spec : gainpilot::kParameterSpecs)
      values_[paramIndex(spec.id)] = spec.defaultValue;

    if constexpr (DISTRHO_PLUGIN_NUM_INPUTS == 1)
      values_[paramIndex(ParamId::channelMode)] =
          static_cast<float>(gainpilot::ChannelMode::mono);

    loadSharedResources();
    updateGeometryConstraints();
  }

protected:
  void parameterChanged(const std::uint32_t index, const float value) override {
    if (index >= gainpilot::kNumParameters || values_[index] == value)
      return;

    values_[index] = value;
    if (capturing_ && index == paramIndex(ParamId::meterResetCount) && value != captureResetCount_)
      captureAcknowledged_ = true;
    repaint();
  }

  // DPF owns uiIdle's editor lifecycle; no registered callback survives close.
  // steady_clock measures editor wall time, including stopped playback. Missed
  // callbacks remain timestamped gaps instead of inventing past gain samples.
  void uiIdle() override {
    historyNow_ = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (history_.sample(historyNow_, values_[paramIndex(ParamId::appliedGainValue)]))
      repaint();
  }

  void uiScaleFactorChanged(double) override {
    updateGeometryConstraints();
    repaint();
  }

  void stateChanged(const char *, const char *) override {}

  void onNanoDisplay() override {
    const float sx = static_cast<float>(getWidth()) / kDesignWidth;
    const float sy = static_cast<float>(getHeight()) / kDesignHeight;
    save();
    scale(sx, sy);

    drawChassis();
    drawHeader();
    drawTargetCard();
    if (settingsOpen_)
      drawSettings();
    else
      drawResponseCard();
    drawWorkflow();

    restore();
  }

  bool onMouse(const MouseEvent &event) override {
    if (event.button != 1)
      return false;

    const float x = static_cast<float>(event.pos.getX()) * kDesignWidth /
                    static_cast<float>(getWidth());
    const float y = static_cast<float>(event.pos.getY()) * kDesignHeight /
                    static_cast<float>(getHeight());

    if (!event.press) {
      if (activeSlider_ != ParamId::count) {
        editParameter(paramIndex(activeSlider_), false);
        activeSlider_ = ParamId::count;
        return true;
      }
      if (resetPressed_) {
        setParameterValue(paramIndex(ParamId::meterReset), 0.0f);
        editParameter(paramIndex(ParamId::meterReset), false);
        resetPressed_ = false;
        repaint();
        return true;
      }
      return false;
    }

    for (const ParamId id : kSliderParameters) {
      if ((id == ParamId::maxCut) != settingsOpen_ && id != ParamId::targetLevel)
        continue;
      const Bounds bounds = sliderBounds(id);
      const bool targetDial = id == ParamId::targetLevel &&
          (kTargetDial.contains(x, y) || Bounds{221, 365, 65, 33}.contains(x, y));
      if (!bounds.contains(x, y) && !targetDial)
        continue;

      activeSlider_ = id;
      editParameter(paramIndex(id), true);
      targetTrackDrag_ = id == ParamId::targetLevel && !targetDial;
      if (targetTrackDrag_) {
        updateSliderFromX(id, x);
      } else {
        dragStartY_ = y;
        dragStartValue_ = values_[paramIndex(id)];
      }
      return true;
    }

    for (std::size_t index = 0; index < kUiScales.size(); ++index) {
      if (scaleButtonBounds(index).contains(x, y))
        return setUiScale(index);
    }

    if (Bounds{918, 18, 30, 32}.contains(x, y) ||
        (settingsOpen_ && Bounds{908, 86, 28, 28}.contains(x, y))) {
      settingsOpen_ = !settingsOpen_;
      repaint();
      return true;
    }

    if (settingsOpen_ && Bounds{366, 191, 92, 35}.contains(x, y)) {
      capturing_ = false;
      return setDiscreteParameter(ParamId::referenceMode, 0);
    }
    if (settingsOpen_ && Bounds{470, 191, 119, 35}.contains(x, y)) {
      capturing_ = true;
      captureAcknowledged_ = false;
      captureResetCount_ = values_[paramIndex(ParamId::meterResetCount)];
      setDiscreteParameter(ParamId::referenceMode, 0);
      resetPressed_ = true;
      editParameter(paramIndex(ParamId::meterReset), true);
      setParameterValue(paramIndex(ParamId::meterReset), 1);
      status_ = "Play the passage, then Stop & Lock";
      repaint();
      return true;
    }
    if (settingsOpen_ && Bounds{601, 191, 123, 35}.contains(x, y)) {
      const float measured = values_[paramIndex(ParamId::inputIntegratedValue)];
      if (measured <= -70 || (capturing_ && !captureAcknowledged_)) {
        status_ = "Waiting for a fresh loudness measurement";
        repaint();
        return true;
      }
      const float reference = capturing_ ? measured : values_[paramIndex(ParamId::inputReferenceValue)];
      setDiscreteParameter(ParamId::lockedReference, reference);
      capturing_ = false;
      status_ = "Reference locked; saved with the session";
      return setDiscreteParameter(ParamId::referenceMode, 1);
    }
    if (settingsOpen_ && Bounds{366, 268, 209, 34}.contains(x, y)) {
      presetIndex_ = (presetIndex_ + 1) % gainpilot::kFactoryPresetNames.size();
      applyPreset(gainpilot::factoryPreset(presetIndex_));
      status_ = gainpilot::kFactoryPresetNames[presetIndex_];
      return true;
    }
    if (settingsOpen_ && (Bounds{587, 268, 65, 34}.contains(x, y) || Bounds{664, 268, 65, 34}.contains(x, y))) {
      savingPreset_ = x < 660;
      FileBrowserOptions options;
      options.saving = savingPreset_;
      options.defaultName = "GainPilot.gainpilot";
      options.title = savingPreset_ ? "Save GainPilot preset" : "Load GainPilot preset";
      if (!openFileBrowser(options)) {
        status_ = "Could not open file dialog";
        repaint();
      }
      return true;
    }

    if (Bounds{22, 485, 145, 38}.contains(x, y))
      return setDiscreteParameter(ParamId::programMode, 0.0f);
    if (Bounds{175, 485, 155, 38}.contains(x, y))
      return setDiscreteParameter(ParamId::programMode, 1.0f);
    if (Bounds{385, 485, 132, 38}.contains(x, y))
      return setDiscreteParameter(ParamId::channelMode, 0.0f);
    if (Bounds{525, 485, 142, 38}.contains(x, y))
      return setDiscreteParameter(ParamId::channelMode, 1.0f);

    if (Bounds{823, 482, 120, 40}.contains(x, y)) {
      resetPressed_ = true;
      editParameter(paramIndex(ParamId::meterReset), true);
      setParameterValue(paramIndex(ParamId::meterReset), 1.0f);
      repaint();
      return true;
    }

    return false;
  }

  bool onMotion(const MotionEvent &event) override {
    if (activeSlider_ == ParamId::count)
      return false;

    const float x = static_cast<float>(event.pos.getX()) * kDesignWidth /
                    static_cast<float>(getWidth());
    if (targetTrackDrag_) {
      updateSliderFromX(activeSlider_, x);
    } else {
      const float y = static_cast<float>(event.pos.getY()) * kDesignHeight /
                      static_cast<float>(getHeight());
      updateKnobFromY(activeSlider_, y);
    }
    return true;
  }

  bool onScroll(const ScrollEvent &event) override {
    const float x = static_cast<float>(event.pos.getX()) * kDesignWidth /
                    static_cast<float>(getWidth());
    const float y = static_cast<float>(event.pos.getY()) * kDesignHeight /
                    static_cast<float>(getHeight());

    for (const ParamId id : kSliderParameters) {
      if ((id == ParamId::maxCut) != settingsOpen_ && id != ParamId::targetLevel)
        continue;
      if (!sliderBounds(id).contains(x, y) &&
          !(id == ParamId::targetLevel && (kTargetDial.contains(x, y) ||
                                        Bounds{221, 365, 65, 33}.contains(x, y))))
        continue;

      const gainpilot::ParameterSpec &spec = gainpilot::parameterSpec(id);
      const float step = (spec.maxValue - spec.minValue) / 100.0f;
      const float next = gainpilot::clampToSpec(
          id, values_[paramIndex(id)] +
                  static_cast<float>(event.delta.getY()) * step);
      editParameter(paramIndex(id), true);
      values_[paramIndex(id)] = next;
      setParameterValue(paramIndex(id), next);
      editParameter(paramIndex(id), false);
      repaint();
      return true;
    }
    return false;
  }

  bool onKeyboard(const KeyboardEvent& event) override {
    if (event.press && event.key == kKeyEscape && settingsOpen_) {
      settingsOpen_ = false;
      repaint();
      return true;
    }
    return false;
  }

  void uiFileBrowserSelected(const char* filename) override {
    if (filename == nullptr) return;
    if (savingPreset_) {
      gainpilot::ParameterState state;
      for (const auto id : gainpilot::kStateParamIds)
        state.set(id, values_[paramIndex(id)]);
      status_ = gainpilot::savePreset(std::filesystem::u8path(filename), state)
                    ? "Preset saved" : "Could not save preset";
    } else if (const auto state = gainpilot::loadPreset(std::filesystem::u8path(filename))) {
      applyPreset(*state);
      status_ = "Preset loaded";
    } else {
      status_ = "Could not load a valid GainPilot preset";
    }
    repaint();
  }

private:
  void applyPreset(const gainpilot::ParameterState& state) {
    capturing_ = false;
    for (const auto id : gainpilot::kStateParamIds) {
      if constexpr (DISTRHO_PLUGIN_NUM_INPUTS == 1)
        if (id == ParamId::channelMode) continue;
      setDiscreteParameter(id, state.get(id));
    }
  }

  void updateGeometryConstraints() {
    const double dpi = getScaleFactor();
    setGeometryConstraints(static_cast<uint>(std::lround(720 * dpi)),
                           static_cast<uint>(std::lround(408 * dpi)), true);
  }

  static constexpr float kPi = 3.14159265358979323846f;

  float normalized(const ParamId id) const {
    const auto& spec = gainpilot::parameterSpec(id);
    return std::clamp((values_[paramIndex(id)] - spec.minValue) /
                      (spec.maxValue - spec.minValue), 0.0f, 1.0f);
  }

  void line(float x1, float y1, float x2, float y2, float width,
            int r, int g, int b) {
    beginPath(); moveTo(x1, y1); lineTo(x2, y2);
    strokeWidth(width); strokeColor(r, g, b); stroke();
  }

  void ring(float x, float y, float radius, float amount, float width,
            int r, int g, int b) {
    if (amount <= 0) return;
    beginPath();
    arc(x, y, radius, .75f * kPi, (.75f + 1.5f * amount) * kPi, CW);
    lineCap(ROUND); strokeWidth(width); strokeColor(r, g, b); stroke();
    lineCap(BUTT);
  }

  void label(float x, float y, const char* value, float size = 11,
             int align = ALIGN_LEFT | ALIGN_MIDDLE) {
    drawText(x, y, size, value, 173, 184, 197, align);
  }

  void drawChassis() {
    beginPath(); rect(0, 0, kDesignWidth, kDesignHeight);
    fillPaint(linearGradient(0, 0, 960, 544,
                            Color(17, 25, 31), Color(12, 19, 24)));
    fill();
    strokeRounded(.5f, .5f, 959, 543, 7, 1, 42, 56, 65);
  }

  void drawHeader() {
    drawText(22, 33, 34, "GainPilot", 242, 245, 250, ALIGN_LEFT | ALIGN_MIDDLE);
    label(23, 61, "Adaptive LUFS leveling  ·  Linked true-peak protection", 12);
    label(711, 34, "Scale", 10, ALIGN_RIGHT | ALIGN_MIDDLE);
    strokeRounded(729, 21, 176, 27, 7, 1, 45, 57, 67);
    for (std::size_t i = 0; i < kUiScales.size(); ++i) {
      const auto b = scaleButtonBounds(i);
      // The actual width also reflects resizing performed by the host.
      const bool selected = std::abs(static_cast<float>(getWidth()) /
                                     kDesignWidth / static_cast<float>(getScaleFactor()) - kUiScales[i]) < .02f;
      if (selected) {
        fillRounded(b.x, b.y, b.width, b.height, 7, 14, 34, 42);
        strokeRounded(b.x, b.y, b.width, b.height, 7, 1.2f, 42, 229, 247);
      }
      drawText(b.x + 22, 34, 10, kUiScaleLabels[i], selected ? 49 : 173,
               selected ? 229 : 184, selected ? 247 : 197, ALIGN_CENTER | ALIGN_MIDDLE);
    }
    // Gear remains vector artwork at every UI scale.
    for (int i = 0; i < 8; ++i) {
      const float a = static_cast<float>(i) * kPi / 4;
      line(933 + std::cos(a) * 5, 34 + std::sin(a) * 5,
           933 + std::cos(a) * 7, 34 + std::sin(a) * 7, 2.3f, 181, 193, 207);
    }
    circleStroke(933, 34, 5, 1.8f, 181, 193, 207);
    circleFill(933, 34, 2, 17, 25, 31);
  }

  void drawTargetCard() {
    constexpr float cx = 171, cy = 237, radius = 129;
    ring(cx, cy, radius, 1, 9, 37, 46, 54);
    const float n = normalized(ParamId::targetLevel);
    ring(cx, cy, radius, n, 9, 45, 216, 239);
    const float a = (.75f + 1.5f * n) * kPi;
    circleFill(cx + std::cos(a) * radius, cy + std::sin(a) * radius,
               6.5f, 243, 247, 252);
    beginPath(); circle(cx, cy, 81);
    fillPaint(radialGradient(cx, cy, 5, 88, Color(8, 14, 19), Color(12, 19, 24)));
    fill();
    label(cx, 195, "TARGET", 11, ALIGN_CENTER | ALIGN_MIDDLE);
    char value[32];
    std::snprintf(value, sizeof(value), "%.1f", values_[paramIndex(ParamId::targetLevel)]);
    drawText(cx, 238, 54, value, 43, 218, 242, ALIGN_CENTER | ALIGN_MIDDLE);
    drawText(cx, 281, 16, "LUFS", 206, 213, 223, ALIGN_CENTER | ALIGN_MIDDLE);
    fillRounded(50, 379, 140, 5, 2.5f, 71, 80, 90);
    fillRounded(50, 379, std::max(1.0f, 140 * n), 5, 2.5f, 38, 155, 218);
    circleFill(50 + 140 * n, 381.5f, 9, 243, 245, 249);
    const auto& spec = gainpilot::parameterSpec(ParamId::targetLevel);
    char minimum[16], maximum[16];
    std::snprintf(minimum, sizeof(minimum), "%.0f", spec.minValue);
    std::snprintf(maximum, sizeof(maximum), "%.0f", spec.maxValue);
    label(22, 382, minimum); label(200, 382, maximum);
    fillRounded(221, 365, 65, 33, 7, 15, 23, 29);
    strokeRounded(221, 365, 65, 33, 7, 1, 45, 58, 68);
    drawText(253.5f, 382, 12, value, 242, 245, 250, ALIGN_CENTER | ALIGN_MIDDLE);
    label(293, 382, "LUFS", 12);
  }

  void drawResponseCard() {
    strokeRounded(347, 78, 598, 211, 8, 1, 37, 50, 60);
    label(362, 97, "APPLIED GAIN");
    char gain[32];
    std::snprintf(gain, sizeof(gain), "%+.1f dB", values_[paramIndex(ParamId::appliedGainValue)]);
    drawText(930, 98, 13, gain, 47, 229, 249, ALIGN_RIGHT | ALIGN_MIDDLE);
    drawHistory();
    drawRotaryControl(ParamId::inputTrim, 433, 370);
    drawRotaryControl(ParamId::truePeak, 638, 370);
    drawRotaryControl(ParamId::maxGain, 841, 370);
  }

  void drawHistory() {
    constexpr float x = 387, y = 115, w = 540, h = 152;
    for (int i = 0; i <= 20; ++i)
      line(x + w * i / 20, y, x + w * i / 20, y + h, .5f, 30, 43, 53);
    for (int i = 0; i <= 6; ++i) {
      const float yy = y + h * i / 6;
      line(x, yy, x + w, yy, i == 3 ? .8f : .5f,
           i == 3 ? 53 : 30, i == 3 ? 70 : 43, i == 3 ? 80 : 53);
      char tick[12]; const int value = 15 - i * 5;
      std::snprintf(tick, sizeof(tick), value > 0 ? "+%d" : "%d", value);
      label(x - 9, yy, tick, 10, ALIGN_RIGHT | ALIGN_MIDDLE);
    }
    label(x + w - 2, y + h + 12, "60 s", 10, ALIGN_RIGHT | ALIGN_MIDDLE);
    save(); scissor(x, y, w, h);
    // Fill and stroke each contiguous run independently, preserving clock gaps.
    const double firstTime = historyNow_ - gainpilot::ui::GainHistory::duration;
    const auto pointX = [&](std::size_t i) {
      return x + w * static_cast<float>((history_.at(i).time - firstTime) /
                                       gainpilot::ui::GainHistory::duration);
    };
    const auto pointY = [&](std::size_t i) { return y + h * (15 - history_.at(i).gain) / 30; };
    for (std::size_t start = 0; start < history_.size();) {
      std::size_t end = start + 1;
      while (end < history_.size() && history_.at(end).time - history_.at(end - 1).time <=
             gainpilot::ui::GainHistory::interval * 1.5) ++end;
      if (end - start >= 2) {
        const auto areaPaint = linearGradient(0, y, 0, y + h,
            Color(43, 218, 242, .23f), Color(43, 218, 242, .04f));
        // Convex strips keep the fill below the curve on every host renderer.
        for (std::size_t i = start + 1; i < end; ++i) {
          if (pointY(i - 1) >= y + h && pointY(i) >= y + h) continue;
          beginPath(); moveTo(pointX(i - 1), y + h);
          lineTo(pointX(i - 1), pointY(i - 1));
          lineTo(pointX(i), pointY(i)); lineTo(pointX(i), y + h);
          closePath(); fillPaint(areaPaint); fill();
        }
        beginPath(); moveTo(pointX(start), pointY(start));
        for (std::size_t i = start + 1; i < end; ++i) lineTo(pointX(i), pointY(i));
        strokeWidth(2); strokeColor(43, 222, 243); stroke();
      }
      start = end;
    }
    restore();
  }

  void drawRotaryControl(ParamId id, float cx, float cy) {
    const float n = normalized(id);
    label(cx, cy - 57, shortLabel(id), 11, ALIGN_CENTER | ALIGN_MIDDLE);
    for (int i = 0; i <= 24; ++i) {
      const float a = (.75f + 1.5f * static_cast<float>(i) / 24) * kPi;
      circleFill(cx + std::cos(a) * 48, cy + std::sin(a) * 48,
                 i % 3 == 0 ? .85f : .6f, i <= n * 24 ? 195 : 90,
                 i <= n * 24 ? 211 : 107, i <= n * 24 ? 223 : 120);
    }
    ring(cx, cy, 41, 1, 2, 47, 58, 68);
    ring(cx, cy, 41, n, 2.3f, 44, 220, 241);
    circleFill(cx, cy + 2, 36, 5, 10, 14);
    beginPath(); circle(cx, cy, 34);
    fillPaint(linearGradient(cx - 30, cy - 34, cx + 25, cy + 34,
                            Color(46, 55, 64), Color(24, 31, 38))); fill();
    circleStroke(cx, cy, 34, 1.2f, 67, 79, 89);
    ring(cx, cy, 30, 1, .6f, 62, 73, 83);
    const float a = (.75f + 1.5f * n) * kPi;
    line(cx + std::cos(a) * 18, cy + std::sin(a) * 18,
         cx + std::cos(a) * 32, cy + std::sin(a) * 32, 2, 244, 246, 249);
    const auto& spec = gainpilot::parameterSpec(id);
    char minimum[16], maximum[16], value[32];
    std::snprintf(minimum, sizeof(minimum), "%g", static_cast<double>(spec.minValue));
    std::snprintf(maximum, sizeof(maximum), id == ParamId::maxCut ? "-%g" : spec.maxValue > 0 ? "+%g" : "%g", static_cast<double>(spec.maxValue));
    label(cx - 50, cy + 44, minimum, 10, ALIGN_CENTER | ALIGN_MIDDLE);
    label(cx + 50, cy + 44, maximum, 10, ALIGN_CENTER | ALIGN_MIDDLE);
    std::snprintf(value, sizeof(value), "%s%.1f dB",
                  id == ParamId::maxCut && values_[paramIndex(id)] > 0 ? "-" : values_[paramIndex(id)] > 0 ? "+" : "", values_[paramIndex(id)]);
    fillRounded(cx - 35, cy + 47, 70, 24, 8, 14, 22, 28);
    strokeRounded(cx - 35, cy + 47, 70, 24, 8, 1, 40, 53, 62);
    drawText(cx, cy + 59, 12, value, 237, 242, 247, ALIGN_CENTER | ALIGN_MIDDLE);
  }

  enum class Icon { none, waveform, speech, stereo, mono, reset };

  void drawIcon(Icon icon, float x, float y, bool selected) {
    const int r = selected ? 47 : 196, g = selected ? 227 : 204, b = selected ? 247 : 214;
    if (icon == Icon::stereo) {
      circleStroke(x - 3, y, 6, 1.3f, r, g, b); circleStroke(x + 3, y, 6, 1.3f, r, g, b);
    } else if (icon == Icon::mono) {
      line(x - 5, y - 4, x - 5, y + 4, 1.4f, r, g, b);
      line(x, y - 8, x, y + 8, 1.4f, r, g, b);
      line(x + 5, y - 4, x + 5, y + 4, 1.4f, r, g, b);
    } else if (icon == Icon::waveform) {
      beginPath(); moveTo(x - 8, y + 1); lineTo(x - 4, y + 1);
      lineTo(x - 1, y - 8); lineTo(x + 2, y + 8);
      lineTo(x + 4, y + 1); lineTo(x + 8, y + 1);
      strokeWidth(1.3f); strokeColor(r, g, b); stroke();
    } else if (icon == Icon::speech) {
      beginPath(); ellipse(x, y - 1, 7, 6); strokeWidth(1.3f); strokeColor(r, g, b); stroke();
      line(x - 4, y + 3, x - 6, y + 8, 1.3f, r, g, b);
      line(x - 6, y + 8, x + 1, y + 5, 1.3f, r, g, b);
    } else if (icon == Icon::reset) {
      beginPath(); arc(x, y, 6, -kPi / 2, kPi * 1.2f, CW);
      strokeWidth(1.5f); strokeColor(r, g, b); stroke();
      line(x, y - 6, x - 3, y - 8, 1.5f, r, g, b);
      line(x, y - 6, x - 2, y - 3, 1.5f, r, g, b);
    }
  }

  void drawModeButton(const Bounds& b, const char* title, bool selected,
                      Icon icon = Icon::none, bool disabled = false) {
    fillRounded(b.x, b.y, b.width, b.height, 7, selected ? 14 : 20,
                selected ? 35 : 28, selected ? 44 : 35);
    strokeRounded(b.x, b.y, b.width, b.height, 7, selected ? 1.2f : 1,
                  selected ? 43 : 50, selected ? 228 : 63, selected ? 247 : 73);
    const float offset = icon == Icon::none ? 0 : 10;
    drawIcon(icon, b.x + b.width / 2 - 29, b.y + b.height / 2, selected);
    drawText(b.x + b.width / 2 + offset, b.y + b.height / 2, 12, title,
             disabled ? 99 : selected ? 43 : 221, disabled ? 114 : selected ? 229 : 228,
             disabled ? 126 : selected ? 249 : 237, ALIGN_CENTER | ALIGN_MIDDLE);
  }

  void drawWorkflow() {
    line(22, 451, 923, 451, .8f, 40, 53, 63);
    label(22, 469, "PROGRAM", 10); label(385, 469, "CHANNEL", 10);
    line(357, 463, 357, 530, .8f, 40, 53, 63);
    const bool speech = values_[paramIndex(ParamId::programMode)] >= .5f;
    const bool mono = values_[paramIndex(ParamId::channelMode)] >= .5f;
    drawModeButton({22, 485, 145, 38}, "Auto", !speech, Icon::waveform);
    drawModeButton({175, 485, 155, 38}, "Speech", speech, Icon::speech);
    drawModeButton({385, 485, 132, 38}, "Stereo", !mono, Icon::stereo,
                   DISTRHO_PLUGIN_NUM_INPUTS == 1);
    drawModeButton({525, 485, 142, 38}, "Mono", mono, Icon::mono);
    drawModeButton({823, 482, 120, 40}, "Reset", resetPressed_, Icon::reset);
  }

  void drawSettings() {
    fillRounded(kSettings.x, kSettings.y, kSettings.width, kSettings.height, 8, 17, 26, 33);
    strokeRounded(kSettings.x, kSettings.y, kSettings.width, kSettings.height, 8, 1, 55, 74, 86);
    drawText(366, 100, 16, "Settings", 241, 245, 250, ALIGN_LEFT | ALIGN_MIDDLE);
    line(917, 95, 926, 104, 1.3f, 182, 196, 208);
    line(926, 95, 917, 104, 1.3f, 182, 196, 208);
    label(366, 139, "INPUT REFERENCE");
    char reference[48];
    std::snprintf(reference, sizeof(reference), "%.1f LUFS%s",
                  values_[paramIndex(ParamId::inputReferenceValue)], capturing_ ? "  ·  Capturing" : "");
    drawText(366, 163, 18, reference, 45, 219, 243, ALIGN_LEFT | ALIGN_MIDDLE);
    drawModeButton({366, 191, 92, 35}, "Follow", !capturing_ && values_[paramIndex(ParamId::referenceMode)] < .5f);
    drawModeButton({470, 191, 119, 35}, "Learn input", capturing_);
    drawModeButton({601, 191, 123, 35}, capturing_ ? "Stop & Lock" : "Lock", values_[paramIndex(ParamId::referenceMode)] >= .5f);
    drawRotaryControl(ParamId::maxCut, 830, 203);
    label(366, 252, "NEXT FACTORY PRESET");
    drawModeButton({366, 268, 209, 34}, gainpilot::kFactoryPresetNames[(presetIndex_ + 1) % gainpilot::kFactoryPresetNames.size()], false);
    drawModeButton({587, 268, 65, 34}, "Save", false);
    drawModeButton({664, 268, 65, 34}, "Load", false);
    line(366, 320, 926, 320, .8f, 40, 55, 66);
    constexpr std::array<ParamId, 3> meters{ParamId::inputIntegratedValue, ParamId::outputIntegratedValue, ParamId::outputShortTermValue};
    constexpr std::array<const char*, 3> names{"INPUT INTEGRATED", "OUTPUT INTEGRATED", "OUTPUT SHORT-TERM"};
    for (std::size_t i = 0; i < meters.size(); ++i) {
      const float x = 366 + static_cast<float>(i) * 188;
      label(x, 340, names[i], 10);
      char value[32]; std::snprintf(value, sizeof(value), "%.1f LUFS", values_[paramIndex(meters[i])]);
      drawText(x, 367, 17, value, 226, 235, 243, ALIGN_LEFT | ALIGN_MIDDLE);
    }
    char reduction[48];
    std::snprintf(reduction, sizeof(reduction), "Peak reduction  %.1f dB", values_[paramIndex(ParamId::gainReductionValue)]);
    label(366, 397, reduction, 10);
    if (!status_.empty()) label(366, 421, status_.c_str(), 10);
  }

  void fillRounded(const float x, const float y, const float width,
                   const float height, const float radius, const int red,
                   const int green, const int blue) {
    beginPath();
    roundedRect(x, y, width, height, radius);
    fillColor(red, green, blue);
    fill();
  }

  void strokeRounded(const float x, const float y, const float width,
                     const float height, const float radius,
                     const float lineWidth, const int red, const int green,
                     const int blue) {
    beginPath();
    roundedRect(x, y, width, height, radius);
    strokeWidth(lineWidth);
    strokeColor(red, green, blue);
    stroke();
  }

  void circleFill(const float x, const float y, const float radius,
                  const int red, const int green, const int blue) {
    beginPath();
    circle(x, y, radius);
    fillColor(red, green, blue);
    fill();
  }

  void circleStroke(const float x, const float y, const float radius,
                    const float lineWidth, const int red, const int green,
                    const int blue) {
    beginPath();
    circle(x, y, radius);
    strokeWidth(lineWidth);
    strokeColor(red, green, blue);
    stroke();
  }

  void drawText(const float x, const float y, const float size,
                const char *const value, const int red, const int green,
                const int blue, const int alignment) {
    fontSize(size);
    textAlign(alignment);
    fillColor(red, green, blue);
    text(x, y, value, nullptr);
  }

  bool setDiscreteParameter(const ParamId id, const float value) {
    if constexpr (DISTRHO_PLUGIN_NUM_INPUTS == 1) {
      if (id == ParamId::channelMode && value < 0.5f)
        return true;
    }

    values_[paramIndex(id)] = value;
    editParameter(paramIndex(id), true);
    setParameterValue(paramIndex(id), value);
    editParameter(paramIndex(id), false);
    repaint();
    return true;
  }

  bool setUiScale(const std::size_t index) {
    if (index >= kUiScales.size())
      return false;

    const float scaleFactor = kUiScales[index] * static_cast<float>(getScaleFactor());
    setSize(
        static_cast<std::uint32_t>(std::lround(kDesignWidth * scaleFactor)),
        static_cast<std::uint32_t>(std::lround(kDesignHeight * scaleFactor)));
    repaint();
    return true;
  }

  void updateSliderFromX(const ParamId id, const float x) {
    const Bounds bounds = sliderBounds(id);
    const gainpilot::ParameterSpec &spec = gainpilot::parameterSpec(id);
    const float normalized =
        std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f);
    const float value =
        spec.minValue + normalized * (spec.maxValue - spec.minValue);
    values_[paramIndex(id)] = value;
    setParameterValue(paramIndex(id), value);
    repaint();
  }

  void updateKnobFromY(const ParamId id, const float y) {
    const gainpilot::ParameterSpec &spec = gainpilot::parameterSpec(id);
    const float range = spec.maxValue - spec.minValue;
    const float value = gainpilot::clampToSpec(
        id, dragStartValue_ + (dragStartY_ - y) / 120.0f * range);
    values_[paramIndex(id)] = value;
    setParameterValue(paramIndex(id), value);
    repaint();
  }

  std::array<float, gainpilot::kNumParameters> values_{};
  gainpilot::ui::GainHistory history_{};
  double historyNow_{0.0};
  ParamId activeSlider_{ParamId::count};
  float dragStartY_{0.0f};
  float dragStartValue_{0.0f};
  bool settingsOpen_{false};
  bool targetTrackDrag_{false};
  bool resetPressed_{false};
  bool capturing_{false};
  bool captureAcknowledged_{false};
  float captureResetCount_{0};
  bool savingPreset_{false};
  std::size_t presetIndex_{0};
  std::string status_{};

  DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainPilotDPFUI)
};

UI *createUI() { return new GainPilotDPFUI(); }

END_NAMESPACE_DISTRHO
